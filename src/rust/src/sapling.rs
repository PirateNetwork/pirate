// Copyright (c) 2020-2023 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

use std::convert::TryInto;
use std::io::{self};
use std::{mem, ptr};

use bellman::groth16::Proof;
use group::{cofactor::CofactorGroup, GroupEncoding};
use incrementalmerkletree::MerklePath;
use memuse::DynamicUsage;
use rand_core::OsRng;
use redjubjub::{Binding, SpendAuth};
use sapling_crypto::{
    bundle::{
        Authorized as SaplingAuthorized, Bundle as SaplingBundle,
        GrothProofBytes,
        OutputDescription as SaplingOutputDescription,
        SpendDescription as SaplingSpendDescription,
    },
    builder::{BundleType, Builder as SaplingCryptoBuilder, InProgress, Proven, Unsigned},
    circuit::{OutputParameters, SpendParameters},
    keys::{FullViewingKey, OutgoingViewingKey, SpendAuthorizingKey},
    note::ExtractedNoteCommitment,
    note_encryption::{PreparedIncomingViewingKey, Zip212Enforcement, try_sapling_note_decryption, try_sapling_output_recovery},
    value::{NoteValue, ValueCommitment},
    Anchor, BatchValidator as SaplingBatchValidator, Node, Note, NullifierDerivingKey,
    PaymentAddress, Rseed, SaplingIvk, SaplingVerificationContext,
    NOTE_COMMITMENT_TREE_DEPTH,
};
use sapling_crypto::zip32::ExtendedSpendingKey;
use zcash_primitives::{
    merkle_tree::merkle_path_from_slice,
    transaction::{
        components::{sapling as sapling_serialization, GROTH_PROOF_SIZE},
        txid::{BlockTxCommitmentDigester, TxIdDigester},
        Authorized, Transaction, TransactionDigest,
    },
};
use zcash_protocol::{
    value::ZatBalance as Amount,
};
use crate::{
    de_ct,
    SAPLING_SPEND_PARAMS, SAPLING_OUTPUT_PARAMS,
    SAPLING_SPEND_VK, SAPLING_OUTPUT_VK,
    bridge::ffi,
    bundlecache::{sapling_bundle_validity_cache, sapling_bundle_validity_cache_mut, CacheEntries},
    streams::CppStream,
};

mod zip32;

const SAPLING_TREE_DEPTH: usize = 32;

pub(crate) struct Spend(SaplingSpendDescription<SaplingAuthorized>);

pub(crate) fn parse_v4_sapling_spend(bytes: &[u8]) -> Result<Box<Spend>, String> {
    sapling_serialization::temporary_zcashd_read_spend_v4(&mut io::Cursor::new(bytes))
        .map(Spend)
        .map(Box::new)
        .map_err(|e| format!("{}", e))
}

impl Spend {
    pub(crate) fn cv(&self) -> [u8; 32] {
        self.0.cv().to_bytes()
    }

    pub(crate) fn anchor(&self) -> [u8; 32] {
        self.0.anchor().to_bytes()
    }

    pub(crate) fn nullifier(&self) -> [u8; 32] {
        self.0.nullifier().0
    }

    pub(crate) fn rk(&self) -> [u8; 32] {
        (*self.0.rk()).into()
    }

    pub(crate) fn zkproof(&self) -> [u8; 192] {
        *self.0.zkproof()
    }

    pub(crate) fn spend_auth_sig(&self) -> [u8; 64] {
        (*self.0.spend_auth_sig()).into()
    }
}

pub struct Output(SaplingOutputDescription<[u8; 192]>);

pub(crate) fn parse_v4_sapling_output(bytes: &[u8]) -> Result<Box<Output>, String> {
    sapling_serialization::temporary_zcashd_read_output_v4(&mut io::Cursor::new(bytes))
        .map(Output)
        .map(Box::new)
        .map_err(|e| format!("{}", e))
}

impl Output {
    pub(crate) fn cv(&self) -> [u8; 32] {
        self.0.cv().to_bytes()
    }

    pub(crate) fn cmu(&self) -> [u8; 32] {
        self.0.cmu().to_bytes()
    }

    pub(crate) fn ephemeral_key(&self) -> [u8; 32] {
        self.0.ephemeral_key().0
    }

    pub(crate) fn enc_ciphertext(&self) -> [u8; 580] {
        *self.0.enc_ciphertext()
    }

    pub(crate) fn out_ciphertext(&self) -> [u8; 80] {
        *self.0.out_ciphertext()
    }

    pub(crate) fn zkproof(&self) -> [u8; 192] {
        *self.0.zkproof()
    }

    pub(crate) fn as_ptr(&self) -> *const ffi::OutputPtr {
        let ret: *const SaplingOutputDescription<[u8; 192]> = &self.0;
        ret.cast()
    }

    pub(crate) fn serialize_v4(&self, writer: &mut CppStream<'_>) -> Result<(), String> {
        sapling_serialization::temporary_zcashd_write_output_v4(writer, &self.0)
            .map_err(|e| format!("Failed to write v4 Sapling Output Description: {}", e))
    }
}

#[derive(Clone)]
pub struct Bundle(pub Option<SaplingBundle<SaplingAuthorized, Amount>>);

pub(crate) fn none_sapling_bundle() -> Box<Bundle> {
    Box::new(Bundle(None))
}

/// Parses an authorized Sapling bundle from the given stream.
pub(crate) fn parse_v5_sapling_bundle(reader: &mut CppStream<'_>) -> Result<Box<Bundle>, String> {
    Bundle::parse_v5(reader)
}

impl Bundle {
    /// Returns a copy of the value.
    pub(crate) fn box_clone(&self) -> Box<Self> {
        Box::new(self.clone())
    }

    /// Parses an authorized Sapling bundle from the given stream.
    fn parse_v5(reader: &mut CppStream<'_>) -> Result<Box<Self>, String> {
        match Transaction::temporary_zcashd_read_v5_sapling(reader) {
            Ok(parsed) => Ok(Box::new(Bundle(parsed))),
            Err(e) => Err(format!("Failed to parse v5 Sapling bundle: {}", e)),
        }
    }

    pub(crate) fn serialize_v4_components(
        &self,
        writer: &mut CppStream<'_>,
        has_sapling: bool,
    ) -> Result<(), String> {
        sapling_serialization::temporary_zcashd_write_v4_components(
            writer,
            self.0.as_ref(),
            has_sapling,
        )
        .map_err(|e| format!("{}", e))
    }

    /// Serializes an authorized Sapling bundle to the given stream.
    ///
    /// If `bundle == None`, this serializes `nSpendsSapling = nOutputsSapling = 0`.
    pub(crate) fn serialize_v5(&self, writer: &mut CppStream<'_>) -> Result<(), String> {
        Transaction::temporary_zcashd_write_v5_sapling(self.inner(), writer)
            .map_err(|e| format!("Failed to serialize Sapling bundle: {}", e))
    }

    pub(crate) fn inner(&self) -> Option<&SaplingBundle<SaplingAuthorized, Amount>> {
        self.0.as_ref()
    }

    pub(crate) fn as_ptr(&self) -> *const ffi::SaplingBundlePtr {
        if let Some(bundle) = self.inner() {
            let ret: *const SaplingBundle<SaplingAuthorized, Amount> = bundle;
            ret.cast()
        } else {
            ptr::null()
        }
    }

    /// Returns the amount of dynamically-allocated memory used by this bundle.
    pub(crate) fn recursive_dynamic_usage(&self) -> usize {
        self.inner()
            // Bundles are boxed on the heap, so we count their own size as well as the size
            // of `Vec`s they allocate.
            .map(|bundle| mem::size_of_val(bundle) + bundle.dynamic_usage())
            // If the transaction has no Sapling component, nothing is allocated for it.
            .unwrap_or(0)
    }

    /// Returns whether the Sapling bundle is present.
    pub(crate) fn is_present(&self) -> bool {
        self.0.is_some()
    }

    pub(crate) fn spends(&self) -> Vec<Spend> {
        self.0
            .iter()
            .flat_map(|b| b.shielded_spends().iter())
            .cloned()
            .map(Spend)
            .collect()
    }

    pub(crate) fn get_spend(&self, spend_index: usize) -> Result<Box<Spend>, String> {
        let bundle = match &self.0 {
            Some(b) => b,
            None => return Err("Failed to retrieve sapling bundle".to_owned()),
        };
        bundle
            .shielded_spends()
            .get(spend_index)
            .cloned()
            .map(|s| Box::new(Spend(s)))
            .ok_or_else(|| "Failed to retrieve spend description".to_owned())
    }

    pub(crate) fn outputs(&self) -> Vec<Output> {
        self.0
            .iter()
            .flat_map(|b| b.shielded_outputs().iter())
            .cloned()
            .map(Output)
            .collect()
    }

    pub(crate) fn get_output(&self, out_index: usize) -> Result<Box<Output>, String> {
        let bundle = match &self.0 {
            Some(b) => b,
            None => return Err("Failed to retrieve sapling bundle".to_owned()),
        };
        bundle
            .shielded_outputs()
            .get(out_index)
            .cloned()
            .map(|o| Box::new(Output(o)))
            .ok_or_else(|| "Failed to retrieve output description".to_owned())
    }

    pub(crate) fn num_spends(&self) -> usize {
        self.inner().map(|b| b.shielded_spends().len()).unwrap_or(0)
    }

    pub(crate) fn num_outputs(&self) -> usize {
        self.inner()
            .map(|b| b.shielded_outputs().len())
            .unwrap_or(0)
    }

    /// Returns the value balance for this Sapling bundle.
    ///
    /// A transaction with no Sapling component has a value balance of zero.
    pub(crate) fn value_balance_zat(&self) -> i64 {
        self.inner()
            .map(|b| b.value_balance().into())
            // From section 7.1 of the Zcash prototol spec:
            // If valueBalanceSapling is not present, then v^balanceSapling is defined to be 0.
            .unwrap_or(0)
    }

    /// Returns the binding signature for the bundle.
    ///
    /// # Panics
    ///
    /// Panics if the bundle is not present.
    pub(crate) fn binding_sig(&self) -> [u8; 64] {
        let binding_sig = self.inner()
            .expect("Bundle actions should have been checked to be non-empty")
            .authorization()
            .binding_sig;
        let sig_bytes: [u8; 64] = binding_sig.into();
        let mut ret = [0; 64];
        ret.copy_from_slice(&sig_bytes);
        ret
    }

    fn commitment<D: TransactionDigest<Authorized>>(&self, digester: D) -> D::SaplingDigest {
        digester.digest_sapling(self.inner())
    }
}

pub(crate) struct BundleAssembler {
    value_balance: Amount,
    shielded_spends: Vec<SaplingSpendDescription<SaplingAuthorized>>,
    shielded_outputs: Vec<SaplingOutputDescription<GrothProofBytes>>,
}

pub(crate) fn new_bundle_assembler() -> Box<BundleAssembler> {
    Box::new(BundleAssembler {
        value_balance: Amount::zero(),
        shielded_spends: vec![],
        shielded_outputs: vec![],
    })
}

pub(crate) fn parse_v4_sapling_components(
    reader: &mut CppStream<'_>,
    has_sapling: bool,
) -> Result<Box<BundleAssembler>, String> {
    BundleAssembler::parse_v4_components(reader, has_sapling)
        .map_err(|e| format!("Failed to parse v4 Sapling bundle: {}", e))
}

impl BundleAssembler {
    pub(crate) fn parse_v4_components(
        reader: &mut CppStream<'_>,
        has_sapling: bool,
    ) -> io::Result<Box<Self>> {
        let (value_balance, shielded_spends, shielded_outputs) =
            sapling_serialization::temporary_zcashd_read_v4_components(reader, has_sapling)?;

        if shielded_spends.is_empty()
            && shielded_outputs.is_empty()
            && value_balance != Amount::zero()
        {
            Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "nonzero valueBalanceSapling has no sources or sinks",
            ))
        } else {
            Ok(Box::new(Self {
                value_balance,
                shielded_spends,
                shielded_outputs,
            }))
        }
    }

    pub(crate) fn add_spend_to_assembler(
        &mut self,
        spend_bytes: [u8; 384],
    ) -> bool {
        let spend = match sapling_serialization::temporary_zcashd_read_spend_v4(
                &mut io::Cursor::new(&spend_bytes))
            .map_err(|e| format!("{}", e)) {
                Ok(s) => s,
                Err(_) => return false
            };

        self.shielded_spends.push(spend);
        return true
    }

    pub(crate) fn add_output_to_assembler(
        &mut self,
        output_bytes: [u8; 948],
    ) -> bool {
        let output = match sapling_serialization::temporary_zcashd_read_output_v4(
                &mut io::Cursor::new(&output_bytes))
            .map_err(|e| format!("{}", e)) {
                Ok(s) => s,
                Err(_) => return false
            };

        self.shielded_outputs.push(output);
        return true
    }

    pub(crate) fn add_value_balance_to_assembler(
        &mut self,
        value_balance: i64,
    ) -> bool {

        let value_balance = match Amount::from_i64(value_balance) {
            Ok(vb) => vb,
            Err(_e) => return false,
        };

        self.value_balance = value_balance;
        return true
    }


    pub(crate) fn have_actions(&self) -> bool {
        !(self.shielded_spends.is_empty() && self.shielded_outputs.is_empty())
    }
}

#[allow(clippy::boxed_local)]
pub(crate) fn finish_bundle_assembly(
    assembler: Box<BundleAssembler>,
    binding_sig: [u8; 64],
) -> Box<Bundle> {
    let binding_sig: redjubjub::Signature<Binding> = binding_sig.into();

    Box::new(Bundle(SaplingBundle::from_parts(
        assembler.shielded_spends,
        assembler.shielded_outputs,
        assembler.value_balance,
        SaplingAuthorized { binding_sig },
    )))
}

pub(crate) struct SaplingBuilder {
    inner: SaplingCryptoBuilder,
    extsks: Vec<ExtendedSpendingKey>,
}

pub(crate) fn new_sapling_builder(anchor: [u8; 32], coinbase: bool) -> Result<Box<SaplingBuilder>, String> {
    let zip212 = sapling_crypto::note_encryption::Zip212Enforcement::On;
    let bundle_type = if coinbase { BundleType::Coinbase } else { BundleType::DEFAULT };
    let anchor = de_ct(Anchor::from_bytes(anchor))
        .ok_or_else(|| "Invalid Sapling anchor".to_owned())?;
    Ok(Box::new(SaplingBuilder {
        inner: SaplingCryptoBuilder::new(zip212, bundle_type, anchor),
        extsks: vec![],
    }))
}

pub(crate) fn build_sapling_bundle(
    builder: Box<SaplingBuilder>,
) -> Result<Box<SaplingUnauthorizedBundle>, String> {
    builder.build().map(Box::new)
}

impl SaplingBuilder {
    pub(crate) fn add_spend(
        &mut self,
        extsk: &[u8],
        recipient: [u8; 43],
        value: u64,
        rcm: [u8; 32],
        merkle_path: [u8; 1 + 33 * SAPLING_TREE_DEPTH + 8],
    ) -> Result<(), String> {
        let extsk =
            ExtendedSpendingKey::from_bytes(extsk).map_err(|_| "Invalid ExtSK".to_owned())?;
        let recipient =
            PaymentAddress::from_bytes(&recipient).ok_or("Invalid recipient address")?;
        let value = NoteValue::from_raw(value);
        let rseed = de_ct(jubjub::Scalar::from_bytes(&rcm))
            .map(Rseed::BeforeZip212)
            .ok_or("Invalid rcm")?;
        let note = Note::from_parts(recipient, value, rseed);
        let merkle_path: MerklePath<Node, NOTE_COMMITMENT_TREE_DEPTH> =
            merkle_path_from_slice(&merkle_path)
                .map_err(|e| format!("Invalid Sapling Merkle path: {}", e))?;

        let fvk = FullViewingKey::from_expanded_spending_key(&extsk.expsk);
        self.inner
            .add_spend(fvk, note, merkle_path)
            .map_err(|e| format!("Failed to add Sapling spend: {}", e))?;
        self.extsks.push(extsk);
        Ok(())
    }

    pub(crate) fn add_recipient(
        &mut self,
        ovk: [u8; 32],
        to: [u8; 43],
        value: u64,
        memo: [u8; 512],
    ) -> Result<(), String> {
        let ovk = Some(OutgoingViewingKey(ovk));
        let to = PaymentAddress::from_bytes(&to).ok_or("Invalid recipient address")?;
        let value = NoteValue::from_raw(value);
        self.inner
            .add_output(ovk, to, value, memo)
            .map_err(|e| format!("Failed to add Sapling recipient: {}", e))
    }

    fn build(self) -> Result<SaplingUnauthorizedBundle, String> {
        let spend_params = unsafe { SAPLING_SPEND_PARAMS.as_ref() }
            .expect("Parameters not loaded: SAPLING_SPEND_PARAMS");
        let output_params = unsafe { SAPLING_OUTPUT_PARAMS.as_ref() }
            .expect("Parameters not loaded: SAPLING_OUTPUT_PARAMS");

        let result = self.inner
            .build::<SpendParameters, OutputParameters, _, Amount>(
                &self.extsks,
                OsRng,
            )
            .map_err(|e| format!("Failed to build Sapling bundle: {}", e))?;

        let (bundle, _metadata) = match result {
            Some(pair) => pair,
            None => return Ok(SaplingUnauthorizedBundle { bundle: None, signing_keys: vec![] }),
        };

        let proven = bundle.create_proofs(spend_params, output_params, OsRng, ());
        let signing_keys = self.extsks.iter().map(|sk| sk.expsk.ask.clone()).collect();
        Ok(SaplingUnauthorizedBundle { bundle: Some(proven), signing_keys })
    }
}

pub(crate) struct SaplingUnauthorizedBundle {
    pub(crate) bundle: Option<SaplingBundle<InProgress<Proven, Unsigned>, Amount>>,
    signing_keys: Vec<SpendAuthorizingKey>,
}

pub(crate) fn apply_sapling_bundle_signatures(
    bundle: Box<SaplingUnauthorizedBundle>,
    sighash_bytes: [u8; 32],
) -> Result<Box<crate::sapling::Bundle>, String> {
    bundle.apply_signatures(sighash_bytes).map(Box::new)
}

impl SaplingUnauthorizedBundle {
    fn apply_signatures(self, sighash_bytes: [u8; 32]) -> Result<crate::sapling::Bundle, String> {
        let SaplingUnauthorizedBundle { bundle, signing_keys } = self;

        let authorized = if let Some(bundle) = bundle {
            let authorized = bundle
                .apply_signatures(OsRng, sighash_bytes, &signing_keys)
                .map_err(|e| format!("Failed to apply signatures to Sapling bundle: {}", e))?;
            Some(authorized)
        } else {
            None
        };

        Ok(crate::sapling::Bundle(authorized))
    }
}

pub(crate) struct Verifier(SaplingVerificationContext);

pub(crate) fn init_verifier() -> Box<Verifier> {
    Box::new(Verifier(SaplingVerificationContext::new()))
}

impl Verifier {
    #[allow(clippy::too_many_arguments)]
    pub(crate) fn check_spend(
        &mut self,
        cv: &[u8; 32],
        anchor: &[u8; 32],
        nullifier: &[u8; 32],
        rk: &[u8; 32],
        zkproof: &[u8; GROTH_PROOF_SIZE],
        spend_auth_sig: &[u8; 64],
        sighash_value: &[u8; 32],
    ) -> bool {
        let cv = match Option::from(ValueCommitment::from_bytes_not_small_order(cv)) {
            Some(p) => p,
            None => return false,
        };
        let anchor = match de_ct(bls12_381::Scalar::from_bytes(anchor)) {
            Some(a) => a,
            None => return false,
        };
        let rk: redjubjub::VerificationKey<SpendAuth> = match (*rk).try_into() {
            Ok(k) => k,
            Err(_) => return false,
        };
        let spend_auth_sig: redjubjub::Signature<SpendAuth> = (*spend_auth_sig).into();
        let zkproof = match Proof::read(&zkproof[..]) {
            Ok(p) => p,
            Err(_) => return false,
        };

        let spend_vk = unsafe { SAPLING_SPEND_VK.as_ref() }
            .expect("Parameters not loaded: SAPLING_SPEND_VK");
        self.0.check_spend(
            &cv,
            anchor,
            nullifier,
            rk,
            sighash_value,
            spend_auth_sig,
            zkproof,
            &spend_vk.prepare(),
        )
    }

    pub(crate) fn check_output(
        &mut self,
        cv: &[u8; 32],
        cm: &[u8; 32],
        ephemeral_key: &[u8; 32],
        zkproof: &[u8; GROTH_PROOF_SIZE],
    ) -> bool {
        let cv = match Option::from(ValueCommitment::from_bytes_not_small_order(cv)) {
            Some(p) => p,
            None => return false,
        };
        let cmu = match Option::from(ExtractedNoteCommitment::from_bytes(cm)) {
            Some(a) => a,
            None => return false,
        };
        let epk = match de_ct(jubjub::ExtendedPoint::from_bytes(ephemeral_key)) {
            Some(p) => p,
            None => return false,
        };
        let zkproof = match Proof::read(&zkproof[..]) {
            Ok(p) => p,
            Err(_) => return false,
        };

        let output_vk = unsafe { SAPLING_OUTPUT_VK.as_ref() }
            .expect("Parameters not loaded: SAPLING_OUTPUT_VK");
        self.0.check_output(&cv, cmu, epk, zkproof, &output_vk.prepare())
    }

    pub(crate) fn final_check(
        &self,
        value_balance: i64,
        binding_sig: &[u8; 64],
        sighash_value: &[u8; 32],
    ) -> bool {
        let binding_sig: redjubjub::Signature<Binding> = (*binding_sig).into();
        self.0.final_check(value_balance, sighash_value, binding_sig)
    }
}

struct BatchValidatorInner {
    validator: SaplingBatchValidator,
    queued_entries: CacheEntries,
}

pub(crate) struct BatchValidator(Option<BatchValidatorInner>);

pub(crate) fn init_batch_validator(cache_store: bool) -> Box<BatchValidator> {
    Box::new(BatchValidator(Some(BatchValidatorInner {
        validator: SaplingBatchValidator::new(),
        queued_entries: CacheEntries::new(cache_store),
    })))
}

impl BatchValidator {
    /// Checks the bundle against Sapling-specific consensus rules, and queues its
    /// authorization for validation.
    ///
    /// Returns `false` if the bundle doesn't satisfy the checked consensus rules. This
    /// `BatchValidator` can continue to be used regardless, but some or all of the proofs
    /// and signatures from this bundle may have already been added to the batch even if
    /// it fails other consensus rules.
    ///
    /// `sighash` must be for the transaction this bundle is within.
    ///
    /// If this batch was configured to not cache the results, then if the bundle was in
    /// the global bundle validity cache, it will have been removed (and this method will
    /// return `true`).
    #[allow(clippy::boxed_local)]
    pub(crate) fn check_bundle(&mut self, bundle: Box<Bundle>, sighash: [u8; 32]) -> bool {
        match (&mut self.0, bundle.inner()) {
            (Some(inner), Some(_)) => {
                let cache = sapling_bundle_validity_cache();

                // Compute the cache entry for this bundle.
                let cache_entry = {
                    let bundle_commitment = bundle.commitment(TxIdDigester).unwrap();
                    let bundle_authorizing_commitment =
                        bundle.commitment(BlockTxCommitmentDigester);
                    cache.compute_entry(
                        bundle_commitment.as_bytes().try_into().unwrap(),
                        bundle_authorizing_commitment.as_bytes().try_into().unwrap(),
                        &sighash,
                    )
                };

                // Check if this bundle's validation result exists in the cache.
                if cache.contains(cache_entry, &mut inner.queued_entries) {
                    true
                } else {
                    // The bundle has been added to `inner.queued_entries` because it was not
                    // in the cache. We now check the bundle against the Sapling-specific
                    // consensus rules, and add its authorization to the validation batch.
                    inner.validator.check_bundle(bundle.0.unwrap(), sighash)
                }
            }
            (Some(_), None) => {
                tracing::debug!("Tx has no Sapling component");
                true
            }
            (None, _) => {
                tracing::error!("sapling::BatchValidator has already been used");
                false
            }
        }
    }

    /// Batch-validates the accumulated bundles.
    ///
    /// Returns `true` if every proof and signature in every bundle added to the batch
    /// validator is valid, or `false` if one or more are invalid. No attempt is made to
    /// figure out which of the accumulated bundles might be invalid; if that information
    /// is desired, construct separate [`BatchValidator`]s for sub-batches of the bundles.
    ///
    /// This method MUST NOT be called if any prior call to `Self::check_bundle` returned
    /// `false`.
    ///
    /// If this batch was configured to cache the results, then if this method returns
    /// `true` every bundle added to the batch will have also been added to the global
    /// bundle validity cache.
    pub(crate) fn validate(&mut self) -> bool {
        if let Some(inner) = self.0.take() {
            if inner.validator.validate(
                unsafe { SAPLING_SPEND_VK.as_ref() }
                    .expect("Parameters not loaded: SAPLING_SPEND_VK should have been initialized"),
                unsafe { SAPLING_OUTPUT_VK.as_ref() }.expect(
                    "Parameters not loaded: SAPLING_OUTPUT_VK should have been initialized",
                ),
                OsRng,
            ) {
                // `Self::validate()` is only called if every `Self::check_bundle()`
                // returned `true`, so at this point every bundle that was added to
                // `inner.queued_entries` has valid authorization and satisfies the
                // Sapling-specific consensus rules.
                sapling_bundle_validity_cache_mut().insert(inner.queued_entries);
                true
            } else {
                false
            }
        } else {
            tracing::error!("sapling::BatchValidator has already been used");
            false
        }
    }
}

// Sapling note decryption functions for CXX bridge
impl Output {
    /// Attempt to decrypt a Sapling output using an Incoming Viewing Key (IVK).
    /// 
    /// Note: PirateNetwork's librustzcash fork has disabled strict ZIP 212 enforcement.
    /// The library accepts both 0x01 (pre-ZIP212) and 0x02 (post-ZIP212) lead bytes
    /// at all heights, regardless of network upgrade activation. We use a fixed height
    /// since it doesn't affect validation.
    pub(crate) fn try_decrypt_output_ivk(
        &self,
        ivk_bytes: &[u8; 32],
        value_out: &mut u64,
        diversifier_out: &mut [u8; 11],
        pk_d_out: &mut [u8; 32],
        memo_out: &mut [u8; 512],
        rseed_out: &mut [u8; 32],
        leadbyte_out: &mut u8,
        cmu_out: &mut [u8; 32],
        rcm_out: &mut [u8; 32],
    ) -> bool {
        // Convert bytes to SaplingIvk using de_ct and jubjub::Scalar
        let ivk_scalar = match de_ct(jubjub::Scalar::from_bytes(ivk_bytes)) {
            Some(s) => s,
            None => return false,
        };
        let ivk = SaplingIvk(ivk_scalar);
        
        // Prepare the IVK for decryption
        let prepared_ivk = PreparedIncomingViewingKey::new(&ivk);
        
        // Attempt decryption with IVK
        let decrypted = match try_sapling_note_decryption(
            &prepared_ivk,
            &self.0,
            Zip212Enforcement::Off,
        ) {
            Some(r) => r,
            None => return false,
        };
        
        // decrypted is a tuple: (Note, PaymentAddress, MemoBytes)
        // Extract value using inner() method to get u64
        *value_out = decrypted.0.value().inner();
        *diversifier_out = decrypted.1.diversifier().0;
        // Extract pk_d bytes - PaymentAddress has to_bytes() method
        let addr_bytes = decrypted.1.to_bytes();
        // Payment address is 11 bytes diversifier + 32 bytes pk_d
        pk_d_out.copy_from_slice(&addr_bytes[11..43]);
        *memo_out = decrypted.2;
        
        // Determine leadbyte and extract rseed bytes based on Rseed enum
        match decrypted.0.rseed() {
            Rseed::BeforeZip212(rcm) => {
                *rseed_out = rcm.to_bytes();
                *leadbyte_out = 0x01; // BeforeZip212 format
                // For BeforeZip212, rseed IS the rcm
                *rcm_out = rcm.to_bytes();
            }
            Rseed::AfterZip212(rseed_bytes) => {
                rseed_out.copy_from_slice(rseed_bytes);
                *leadbyte_out = 0x02; // AfterZip212 (ZIP 212) format
                // For AfterZip212, compute rcm using PRF^expand
                let rcm = decrypted.0.rcm();
                *rcm_out = rcm.to_bytes();
            }
        }
        
        // Get the note commitment from the output (already computed)
        *cmu_out = self.0.cmu().to_bytes();
        
        true
    }

    /// Attempt to decrypt a Sapling output using an Outgoing Viewing Key (OVK).
    /// 
    /// Note: PirateNetwork's librustzcash fork has disabled strict ZIP 212 enforcement.
    /// The library accepts both 0x01 (pre-ZIP212) and 0x02 (post-ZIP212) lead bytes
    /// at all heights, regardless of network upgrade activation. We use a fixed height
    /// since it doesn't affect validation.
    pub(crate) fn try_decrypt_output_ovk(
        &self,
        ovk_bytes: &[u8; 32],
        value_out: &mut u64,
        diversifier_out: &mut [u8; 11],
        pk_d_out: &mut [u8; 32],
        memo_out: &mut [u8; 512],
        rseed_out: &mut [u8; 32],
        leadbyte_out: &mut u8,
        cmu_out: &mut [u8; 32],
        rcm_out: &mut [u8; 32],
    ) -> bool {
        let ovk = OutgoingViewingKey(*ovk_bytes);
        
        // Attempt decryption with OVK using the correct API
        let decrypted = match try_sapling_output_recovery(
            &ovk,
            &self.0,
            Zip212Enforcement::Off,
        ) {
            Some(r) => r,
            None => return false,
        };
        
        // decrypted is a tuple: (Note, PaymentAddress, MemoBytes)
        // Extract value using inner() method to get u64
        *value_out = decrypted.0.value().inner();
        *diversifier_out = decrypted.1.diversifier().0;
        // Extract pk_d bytes - PaymentAddress has to_bytes() method
        let addr_bytes = decrypted.1.to_bytes();
        // Payment address is 11 bytes diversifier + 32 bytes pk_d
        pk_d_out.copy_from_slice(&addr_bytes[11..43]);
        *memo_out = decrypted.2;
        
        // Determine leadbyte and extract rseed bytes based on Rseed enum
        match decrypted.0.rseed() {
            Rseed::BeforeZip212(rcm) => {
                *rseed_out = rcm.to_bytes();
                *leadbyte_out = 0x01; // BeforeZip212 format
                // For BeforeZip212, rseed IS the rcm
                *rcm_out = rcm.to_bytes();
            }
            Rseed::AfterZip212(rseed_bytes) => {
                rseed_out.copy_from_slice(rseed_bytes);
                *leadbyte_out = 0x02; // AfterZip212 (ZIP 212) format
                // For AfterZip212, compute rcm using PRF^expand
                let rcm = decrypted.0.rcm();
                *rcm_out = rcm.to_bytes();
            }
        }
        
        // Get the note commitment from the output (already computed)
        *cmu_out = self.0.cmu().to_bytes();
        
        true
    }

    /// Compute nullifier for this output using provided viewing key components
    /// 
    /// This requires first decrypting the output with the IVK to obtain the note,
    /// then computing the nullifier using the note and nullifier deriving key.
    pub(crate) fn compute_nullifier(
        &self,
        ivk: &[u8; 32],
        _ak: &[u8; 32],
        nk: &[u8; 32],
        position: u64,
        result: &mut [u8; 32],
    ) -> bool {
        // Convert bytes to SaplingIvk
        let ivk_scalar = match de_ct(jubjub::Scalar::from_bytes(ivk)) {
            Some(s) => s,
            None => return false,
        };
        let sapling_ivk = SaplingIvk(ivk_scalar);
        
        // Prepare the IVK for decryption
        let prepared_ivk = PreparedIncomingViewingKey::new(&sapling_ivk);
        
        // Decrypt the output to get the note
        let decrypted = match try_sapling_note_decryption(
            &prepared_ivk,
            &self.0,
            Zip212Enforcement::Off,
        ) {
            Some(r) => r,
            None => return false,
        };

        // Deserialize and validate nk
        let nk = match de_ct(jubjub::ExtendedPoint::from_bytes(nk)) {
            Some(p) => p,
            None => return false,
        };

        let nk = match de_ct(nk.into_subgroup()) {
            Some(nk) => NullifierDerivingKey(nk),
            None => return false,
        };

        // Compute the nullifier using the decrypted note
        let nf = decrypted.0.nf(&nk, position);
        result.copy_from_slice(&nf.0);

        true
    }
}

/// Compute a Sapling nullifier from note components.
///
/// Returns false if `diversifier` or `pk_d` is not valid.
pub(crate) fn compute_nullifier(
    diversifier: &[u8; 11],
    pk_d: &[u8; 32],
    value: u64,
    rcm: &[u8; 32],
    _ak: &[u8; 32],
    nk: &[u8; 32],
    position: u64,
    result: &mut [u8; 32],
) -> bool {
    // Construct the payment address
    let recipient_bytes = {
        let mut tmp = [0; 43];
        tmp[..11].copy_from_slice(diversifier);
        tmp[11..].copy_from_slice(pk_d);
        tmp
    };
    let recipient = match PaymentAddress::from_bytes(&recipient_bytes) {
        Some(pa) => pa,
        None => return false,
    };

    // Deserialize randomness
    // If this is after ZIP 212, the caller has calculated rcm, and we don't need to call
    // Note::derive_esk, so we just pretend the note was using this rcm all along.
    let rseed = match de_ct(jubjub::Scalar::from_bytes(rcm)) {
        Some(s) => Rseed::BeforeZip212(s),
        None => return false,
    };

    // Construct the note
    let note = Note::from_parts(recipient, NoteValue::from_raw(value), rseed);

    // Deserialize and validate nk
    let nk = match de_ct(jubjub::ExtendedPoint::from_bytes(nk)) {
        Some(p) => p,
        None => return false,
    };

    let nk = match de_ct(nk.into_subgroup()) {
        Some(nk) => NullifierDerivingKey(nk),
        None => return false,
    };

    // Compute the nullifier
    let nf = note.nf(&nk, position);
    result.copy_from_slice(&nf.0);

    true
}

/// Derive the Outgoing Cipher Key (OCK) for a Sapling output.
///
/// The OCK is derived from the Outgoing Viewing Key (OVK), value commitment (cv),
/// note commitment (cmu), and ephemeral public key (epk).
///
/// This implements PRF^ock as defined in the Zcash Protocol Specification section 5.4.2.
pub(crate) fn derive_sapling_ock(
    ovk: &[u8; 32],
    cv: &[u8; 32],
    cmu: &[u8; 32],
    epk: &[u8; 32],
    ock_out: &mut [u8; 32],
) -> bool {
    use zcash_note_encryption::EphemeralKeyBytes;
    use sapling_crypto::note_encryption::prf_ock;
    
    // Parse the value commitment directly from the raw cv bytes
    let cv = match de_ct(ValueCommitment::from_bytes_not_small_order(cv)) {
        Some(p) => p,
        None => return false,
    };
    
    // Create the OVK
    let ovk = OutgoingViewingKey(*ovk);
    
    // Create ephemeral key bytes
    let epk = EphemeralKeyBytes(*epk);
    
    // Derive the OCK using prf_ock
    let ock = prf_ock(&ovk, &cv, cmu, &epk);
    ock_out.copy_from_slice(ock.as_ref());
    
    true
}

impl Output {
    /// Attempt to decrypt a Sapling output using an Outgoing Cipher Key (OCK).
    ///
    /// The OCK can be derived using `derive_sapling_ock` or obtained from other sources.
    /// This function uses the OCK to decrypt the output ciphertext directly without
    /// requiring the OVK.
    pub(crate) fn try_decrypt_output_ock(
        &self,
        ock: &[u8; 32],
        value_out: &mut u64,
        diversifier_out: &mut [u8; 11],
        pk_d_out: &mut [u8; 32],
        memo_out: &mut [u8; 512],
        rseed_out: &mut [u8; 32],
        leadbyte_out: &mut u8,
        cmu_out: &mut [u8; 32],
        rcm_out: &mut [u8; 32],
    ) -> bool {
        use zcash_note_encryption::{OutgoingCipherKey, try_output_recovery_with_ock};
        use sapling_crypto::note_encryption::SaplingDomain;
        
        // Create the OCK
        let ock = OutgoingCipherKey(*ock);
        
        // Pirate never activated Canopy, so ZIP 212 is always Off
        let domain = SaplingDomain::new(Zip212Enforcement::Off);
        
        // Attempt decryption with OCK
        let decrypted = match try_output_recovery_with_ock(
            &domain,
            &ock,
            &self.0,
            self.0.out_ciphertext(),
        ) {
            Some(r) => r,
            None => return false,
        };
        
        // Extract the decrypted data
        // decrypted is a tuple: (Note, PaymentAddress, MemoBytes)
        *value_out = decrypted.0.value().inner();
        *diversifier_out = decrypted.1.diversifier().0;
        
        // Extract pk_d bytes - PaymentAddress has to_bytes() method
        let addr_bytes = decrypted.1.to_bytes();
        // Payment address is 11 bytes diversifier + 32 bytes pk_d
        pk_d_out.copy_from_slice(&addr_bytes[11..43]);
        *memo_out = decrypted.2;
        
        // Determine leadbyte and extract rseed bytes based on Rseed enum
        match decrypted.0.rseed() {
            Rseed::BeforeZip212(rcm) => {
                *rseed_out = rcm.to_bytes();
                *leadbyte_out = 0x01; // BeforeZip212 format
                // For BeforeZip212, rseed IS the rcm
                *rcm_out = rcm.to_bytes();
            }
            Rseed::AfterZip212(rseed_bytes) => {
                rseed_out.copy_from_slice(rseed_bytes);
                *leadbyte_out = 0x02; // AfterZip212 (ZIP 212) format
                // For AfterZip212, compute rcm using PRF^expand
                let rcm = decrypted.0.rcm();
                *rcm_out = rcm.to_bytes();
            }
        }
        
        // Get the note commitment from the output (already computed)
        *cmu_out = self.0.cmu().to_bytes();
        
        true
    }
}

