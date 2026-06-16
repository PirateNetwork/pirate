// Copyright (c) 2020-2023 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

use std::convert::TryInto;
use std::io::{self};
use std::{mem, ptr};

use bellman::groth16::Proof;
use group::GroupEncoding;
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
    note_encryption::Zip212Enforcement,
    value::{NoteValue, ValueCommitment},
    Anchor, Node, Note, PaymentAddress, Rseed,
    SaplingVerificationContext,
    NOTE_COMMITMENT_TREE_DEPTH,
};
use sapling_crypto::zip32::ExtendedSpendingKey;
use zcash_primitives::{
    merkle_tree::merkle_path_from_slice,
    transaction::{components::{sapling as sapling_serialization, GROTH_PROOF_SIZE},
        Authorized, Transaction, TransactionDigest,
    },
};
use zcash_protocol::value::ZatBalance as Amount;
use crate::{
    de_ct,
    SAPLING_SPEND_PARAMS, SAPLING_OUTPUT_PARAMS,
    SAPLING_SPEND_VK, SAPLING_OUTPUT_VK,
    bridge::ffi,
    streams::CppStream,
};

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

pub struct Output(pub(super) SaplingOutputDescription<[u8; 192]>);

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

    pub(crate) fn recursive_dynamic_usage(&self) -> usize {
        self.inner()
            .map(|bundle| mem::size_of_val(bundle) + bundle.dynamic_usage())
            .unwrap_or(0)
    }

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

    pub(crate) fn value_balance_zat(&self) -> i64 {
        self.inner()
            .map(|b| b.value_balance().into())
            .unwrap_or(0)
    }

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

    pub(crate) fn commitment<D: TransactionDigest<Authorized>>(&self, digester: D) -> D::SaplingDigest {
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

    pub(crate) fn add_spend_to_assembler(&mut self, spend_bytes: [u8; 384]) -> bool {
        let spend = match sapling_serialization::temporary_zcashd_read_spend_v4(
                &mut io::Cursor::new(&spend_bytes))
            .map_err(|e| format!("{}", e)) {
                Ok(s) => s,
                Err(_) => return false,
            };
        self.shielded_spends.push(spend);
        true
    }

    pub(crate) fn add_output_to_assembler(&mut self, output_bytes: [u8; 948]) -> bool {
        let output = match sapling_serialization::temporary_zcashd_read_output_v4(
                &mut io::Cursor::new(&output_bytes))
            .map_err(|e| format!("{}", e)) {
                Ok(s) => s,
                Err(_) => return false,
            };
        self.shielded_outputs.push(output);
        true
    }

    pub(crate) fn add_value_balance_to_assembler(&mut self, value_balance: i64) -> bool {
        let value_balance = match Amount::from_i64(value_balance) {
            Ok(vb) => vb,
            Err(_e) => return false,
        };
        self.value_balance = value_balance;
        true
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
    let zip212 = Zip212Enforcement::On;
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
) -> Result<Box<Bundle>, String> {
    bundle.apply_signatures(sighash_bytes).map(Box::new)
}

impl SaplingUnauthorizedBundle {
    fn apply_signatures(self, sighash_bytes: [u8; 32]) -> Result<Bundle, String> {
        let SaplingUnauthorizedBundle { bundle, signing_keys } = self;

        let authorized = if let Some(bundle) = bundle {
            let authorized = bundle
                .apply_signatures(OsRng, sighash_bytes, &signing_keys)
                .map_err(|e| format!("Failed to apply signatures to Sapling bundle: {}", e))?;
            Some(authorized)
        } else {
            None
        };

        Ok(Bundle(authorized))
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
