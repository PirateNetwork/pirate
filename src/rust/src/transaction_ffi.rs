use std::convert::TryInto;
use std::io::Cursor;
use std::{ptr, slice};

use blake2b_simd::Hash;
use libc::{c_uchar, size_t};
use tracing::error;
use zcash_encoding::Vector;
use zcash_protocol::{consensus::BranchId, value::Zatoshis};
use zcash_transparent::{
    address::Script,
    bundle::{self as transparent},
    sighash::{self as transparent_sighash, TransparentAuthorizingContext},
};
use sapling_crypto::bundle::Authorized as SaplingSigAuth;
use zcash_primitives::{
    transaction::{
        sighash::{signature_hash, SignableInput},
        txid::TxIdDigester,
        Authorization, Transaction, TransactionData, TxDigests, TxVersion,
    },
};

/// Calculates identifying and authorizing digests for the given transaction.
///
/// If either `txid_ret` or `auth_digest_ret` is `nullptr`, the corresponding digest will
/// not be calculated.
///
/// Returns `false` if the transaction is invalid.
#[no_mangle]
pub extern "C" fn zcash_transaction_digests(
    tx_bytes: *const c_uchar,
    tx_bytes_len: size_t,
    txid_ret: *mut [u8; 32],
    auth_digest_ret: *mut [u8; 32],
) -> bool {
    let tx_bytes = unsafe { slice::from_raw_parts(tx_bytes, tx_bytes_len) };

    // We use a placeholder branch ID here, since it is not used for anything.
    let tx = match Transaction::read(tx_bytes, BranchId::Canopy) {
        Ok(tx) => tx,
        Err(e) => {
            error!("Failed to parse transaction: {}", e);
            return false;
        }
    };

    if let Some(txid_ret) = unsafe { txid_ret.as_mut() } {
        *txid_ret = *tx.txid().as_ref();
    }
    if let Some(auth_digest_ret) = unsafe { auth_digest_ret.as_mut() } {
        match tx.version() {
            // Pre-NU5 transaction formats don't have authorizing data commitments; when
            // included in the authDataCommitment tree, they use the [0xff; 32] value.
            TxVersion::Sprout(_) | TxVersion::V4 => {
                *auth_digest_ret = [0xff; 32]
            }
            _ => auth_digest_ret.copy_from_slice(tx.auth_commitment().as_bytes()),
        }
    }

    true
}

/// Computes v4 digests using a borrowed, decoded Sapling bundle whose complete
/// canonical encoding is checked against `tx_bytes` by the transaction reader.
/// The bundle must remain alive and immutable throughout this call.
#[no_mangle]
pub unsafe extern "C" fn zcash_v4_transaction_digests_with_sapling(
    tx_bytes: *const c_uchar,
    tx_bytes_len: size_t,
    bundle: *const sapling_crypto::Bundle<SaplingSigAuth, zcash_protocol::value::ZatBalance>,
    txid_ret: *mut [u8; 32],
    auth_digest_ret: *mut [u8; 32],
) -> bool {
    let bundle = match unsafe { bundle.as_ref() } {
        Some(bundle) => bundle,
        None => return false,
    };
    let bytes = unsafe { slice::from_raw_parts(tx_bytes, tx_bytes_len) };
    let tx = match Transaction::temporary_zcashd_read_v4_with_sapling(bytes, BranchId::Canopy, bundle) {
        Ok(tx) => tx,
        Err(e) => {
            error!("Failed to parse transaction using Sapling bundle: {}", e);
            return false;
        }
    };
    if let Some(txid_ret) = unsafe { txid_ret.as_mut() } {
        *txid_ret = *tx.txid().as_ref();
    }
    if let Some(auth_digest_ret) = unsafe { auth_digest_ret.as_mut() } {
        *auth_digest_ret = [0xff; 32];
    }
    true
}

#[derive(Clone, Debug)]
pub(crate) struct TransparentAuth {
    all_prev_outputs: Vec<transparent::TxOut>,
}

impl transparent::Authorization for TransparentAuth {
    type ScriptSig = Script;
}

impl TransparentAuthorizingContext for TransparentAuth {
    fn input_amounts(&self) -> Vec<Zatoshis> {
        self.all_prev_outputs
            .iter()
            .map(|prevout| prevout.value())
            .collect()
    }

    fn input_scriptpubkeys(&self) -> Vec<Script> {
        self.all_prev_outputs
            .iter()
            .map(|prevout| prevout.script_pubkey().clone())
            .collect()
    }
}

pub(crate) struct MapTransparent {
    auth: TransparentAuth,
}

impl MapTransparent {
    pub(crate) fn parse(all_prev_outputs: &[u8], tx: &Transaction) -> Result<Self, String> {
        let mut cursor = Cursor::new(all_prev_outputs);
        match Vector::read(&mut cursor, transparent::TxOut::read) {
            Err(e) => Err(format!("Invalid all_prev_outputs field: {}", e)),
            Ok(_) if (cursor.position() as usize) != all_prev_outputs.len() => {
                Err("all_prev_outputs had trailing data".into())
            }
            Ok(all_prev_outputs)
                if tx.transparent_bundle().map_or(false, |t| {
                    // Coinbase txs have one fake input.
                    t.is_coinbase() && !all_prev_outputs.is_empty()
                }) =>
            {
                Err(format!(
                    "all_prev_outputs should be empty for a coinbase tx but has length {}",
                    all_prev_outputs.len()
                ))
            }
            Ok(all_prev_outputs)
                if tx
                    .transparent_bundle()
                    .map(|t| {
                        // For non-coinbase txs, every input is real.
                        !t.is_coinbase() && t.vin.len() != all_prev_outputs.len()
                    })
                    // If we have no transparent part, we should have no prev outputs.
                    .unwrap_or_else(|| !all_prev_outputs.is_empty()) =>
            {
                Err(format!(
                    "all_prev_outputs is incorrect length {} (should be {})",
                    all_prev_outputs.len(),
                    tx.transparent_bundle().map(|t| t.vin.len()).unwrap_or(0),
                ))
            }
            Ok(all_prev_outputs) => Ok(MapTransparent {
                auth: TransparentAuth { all_prev_outputs },
            }),
        }
    }
}

impl transparent::MapAuth<transparent::Authorized, TransparentAuth> for MapTransparent {
    fn map_script_sig(
        &self,
        s: <transparent::Authorized as transparent::Authorization>::ScriptSig,
    ) -> <TransparentAuth as transparent::Authorization>::ScriptSig {
        s
    }

    fn map_authorization(&self, _: transparent::Authorized) -> TransparentAuth {
        // TODO: This map should consume self, so we can move self.auth
        self.auth.clone()
    }
}

pub(crate) struct PrecomputedAuth;

impl Authorization for PrecomputedAuth {
    type TransparentAuth = TransparentAuth;
    type SaplingAuth = SaplingSigAuth;
    type OrchardAuth = orchard::bundle::Authorized;
}

pub struct PrecomputedTxParts {
    pub(crate) tx: TransactionData<PrecomputedAuth>,
    pub(crate) txid_parts: TxDigests<Hash>,
}

/// Precomputes the `TxDigest` struct for the given transaction.
///
/// Returns `nullptr` if the transaction is invalid, or a v1-v4 transaction format.
#[no_mangle]
pub extern "C" fn zcash_transaction_precomputed_init(
    tx_bytes: *const c_uchar,
    tx_bytes_len: size_t,
    all_prev_outputs: *const c_uchar,
    all_prev_outputs_len: size_t,
) -> *mut PrecomputedTxParts {
    let tx_bytes = unsafe { slice::from_raw_parts(tx_bytes, tx_bytes_len) };

    // We use a placeholder branch ID here, since it is not used for anything.
    //
    // TODO: This is also parsing a transaction that may have partially-filled fields.
    // This doesn't matter for transparent components (the only such field would be the
    // scriptSig fields of transparent inputs, which would serialize as empty Scripts),
    // but is ill-defined for shielded components (we'll be serializing 64 bytes of zeroes
    // for each signature). This is an internal FFI so it's fine for now, but we should
    // refactor the transaction builder (which is the only source of partially-created
    // shielded components) to use a different FFI for obtaining the sighash, that passes
    // across the transaction components and then constructs the TransactionData. This is
    // already being done as part of the Orchard changes to the transaction builder, since
    // the Orchard bundle will already be built on the Rust side, and we can avoid passing
    // it back and forward across the FFI with this change. We should similarly refactor
    // the Sapling code to do the same.
    let tx = match Transaction::read(tx_bytes, BranchId::Canopy) {
        Ok(tx) => tx,
        Err(e) => {
            error!("Failed to parse transaction: {}", e);
            return ptr::null_mut();
        }
    };

    match tx.version() {
        TxVersion::Sprout(_) | TxVersion::V4 => {
            // We don't support these legacy transaction formats in this API.
            ptr::null_mut()
        }
        _ => {
            let all_prev_outputs =
                unsafe { slice::from_raw_parts(all_prev_outputs, all_prev_outputs_len) };

            let f_transparent = match MapTransparent::parse(all_prev_outputs, &tx) {
                Ok(f) => f,
                Err(e) => {
                    error!("{}", e);
                    return ptr::null_mut();
                }
            };

            let tx = tx.into_data().map_authorization(f_transparent, (), ());

            let txid_parts = tx.digest(TxIdDigester);
            Box::into_raw(Box::new(PrecomputedTxParts { tx, txid_parts }))
        }
    }
}

/// Frees a precomputed transaction from `zcash_transaction_precomputed_init`.
#[no_mangle]
pub extern "C" fn zcash_transaction_precomputed_free(precomputed_tx: *mut PrecomputedTxParts) {
    if !precomputed_tx.is_null() {
        drop(unsafe { Box::from_raw(precomputed_tx) });
    }
}

/// This MUST match `NOT_AN_INPUT` in `src/script/interpreter.h`.
const NOT_AN_INPUT: usize = 0xffffffff;

/// Calculates a ZIP 244 signature digest for the given transaction.
///
/// `index` must be an index into the transaction's `vin`, or `NOT_AN_INPUT` for
/// calculating the signature digest for shielded signatures.
///
/// `sighash_ret` must point to a 32-byte array.
///
/// Returns `false` if any of the parameters are invalid; in this case, `sighash_ret`
/// will be unaltered.
#[no_mangle]
pub extern "C" fn zcash_transaction_zip244_signature_digest(
    precomputed_tx: *const PrecomputedTxParts,
    hash_type: u32,
    index: size_t,
    sighash_ret: *mut [u8; 32],
) -> bool {
    let precomputed_tx = if let Some(res) = unsafe { precomputed_tx.as_ref() } {
        res
    } else {
        error!("Invalid precomputed transaction");
        return false;
    };
    if matches!(
        precomputed_tx.tx.version(),
        TxVersion::Sprout(_) | TxVersion::V4,
    ) {
        error!("Cannot calculate ZIP 244 digest for pre-v5 transaction");
        return false;
    }

    let signable_input = if index == NOT_AN_INPUT {
        SignableInput::Shielded
    } else {
        let prevout = match precomputed_tx.tx.transparent_bundle() {
            Some(bundle) => match bundle.authorization.all_prev_outputs.get(index) {
                Some(prevout) => prevout,
                None => {
                    error!("nIn out of range");
                    return false;
                }
            },
            None => {
                error!("Tried to create a transparent sighash for a tx without transparent data");
                return false;
            }
        };

        SignableInput::Transparent(
            transparent_sighash::SignableInput::from_parts(
                precomputed_tx.tx.transparent_bundle().unwrap(),
                transparent_sighash::SighashType::from_raw(hash_type.try_into().unwrap()),
                index,
                prevout.script_pubkey(),
                prevout.script_pubkey(),
                prevout.value(),
            ).expect("index already validated")
        )
    };

    // Dispatches to the version-appropriate sighash function (v4/v5/v6); using a
    // hardcoded v5_signature_hash here would silently ignore the Ironwood bundle for
    // v6 transactions, since v5 has no concept of it.
    let sighash = signature_hash(
        &precomputed_tx.tx,
        &signable_input,
        &precomputed_tx.txid_parts,
    );

    // `signature_hash` output is always 32 bytes.
    *unsafe { &mut *sighash_ret } = *sighash.as_ref();
    true
}

#[cfg(test)]
mod reused_sapling_tests {
    use super::*;
    use std::convert::TryFrom;
    use sapling_crypto::{
        bundle::{Authorized, Bundle, OutputDescription, SpendDescription},
        value::{NoteValue, ValueCommitment, ValueCommitTrapdoor},
    };
    use zcash_protocol::value::ZatBalance;

    fn bundle(spends: usize, outputs: usize) -> Bundle<Authorized, ZatBalance> {
        crate::test_harness_ffi::test_only_invalid_sapling_bundle(spends, outputs, 0)
            .0.unwrap()
    }

    fn encoded(bundle: &Bundle<Authorized, ZatBalance>) -> Vec<u8> {
        let transparent = transparent::Bundle {
            vin: vec![],
            vout: vec![transparent::TxOut::new(Zatoshis::from_u64(1234).unwrap(), Script::default())],
            authorization: transparent::Authorized,
        };
        let tx = TransactionData::<zcash_primitives::transaction::Authorized>::from_parts(
            TxVersion::V4, BranchId::Canopy, 0, 0u32.into(),
            Some(transparent), None, Some(bundle.clone()), None,
        ).freeze().unwrap();
        let mut bytes = Vec::new();
        tx.write(&mut bytes).unwrap();
        bytes
    }

    fn reused(bytes: &[u8], bundle: &Bundle<Authorized, ZatBalance>) -> std::io::Result<Transaction> {
        Transaction::temporary_zcashd_read_v4_with_sapling(bytes, BranchId::Canopy, bundle)
    }

    #[test]
    fn matching_reader_preserves_hashes_and_fields_at_count_boundaries() {
        for (spends, outputs) in [(0, 1), (1, 0), (1, 1), (252, 253), (253, 252)] {
            let bundle = bundle(spends, outputs);
            let bytes = encoded(&bundle);
            // The original reader remains an independent decoder reference.
            let reference = Transaction::read(&bytes[..], BranchId::Canopy).unwrap();
            let actual = reused(&bytes, &bundle).unwrap();
            assert_eq!(reference.txid(), actual.txid());
            assert_eq!(reference.auth_commitment(), actual.auth_commitment());
            let mut rewritten = Vec::new();
            actual.write(&mut rewritten).unwrap();
            assert_eq!(bytes, rewritten);
        }
    }

    #[test]
    fn mismatched_fields_cannot_reuse_a_different_bundle() {
        let bundle = bundle(2, 2);
        let bytes = encoded(&bundle);
        // Header, empty input vector, one 9-byte output, lock time and expiry.
        const START: usize = 8 + 1 + 1 + 9 + 4 + 4;
        // Balance, spend count, all spend fields, output count and all output fields.
        for offset in [0, 8, 9, 41, 73, 105, 137, 329,
                       8 + 1 + 2 * 384,
                       8 + 1 + 2 * 384 + 1,
                       8 + 1 + 2 * 384 + 1 + 32,
                       8 + 1 + 2 * 384 + 1 + 64,
                       8 + 1 + 2 * 384 + 1 + 96,
                       8 + 1 + 2 * 384 + 1 + 676,
                       8 + 1 + 2 * 384 + 1 + 756] {
            let mut changed = bytes.clone();
            changed[START + offset] ^= 1;
            assert!(reused(&changed, &bundle).is_err(), "offset {}", offset);
        }
        let mut changed = bytes.clone();
        *changed.last_mut().unwrap() ^= 1; // Binding signature remains at its original position.
        assert!(reused(&changed, &bundle).is_err());
        let mut changed = bytes.clone();
        for i in 0..384 { changed.swap(START + 9 + i, START + 9 + 384 + i); }
        assert!(reused(&changed, &bundle).is_err());
        let mut changed = bytes;
        let outputs = START + 8 + 1 + 2 * 384 + 1;
        for i in 0..948 { changed.swap(outputs + i, outputs + 948 + i); }
        assert!(reused(&changed, &bundle).is_err());
    }

    #[test]
    fn malformed_counts_truncations_and_non_sapling_amounts_still_fail() {
        let bundle = bundle(1, 1);
        let bytes = encoded(&bundle);
        for length in 0..bytes.len() {
            assert!(Transaction::read(&bytes[..length], BranchId::Canopy).is_err());
            assert!(reused(&bytes[..length], &bundle).is_err());
        }
        // Non-canonical CompactSize for either count (one encoded in three bytes).
        for offset in [27 + 8, 27 + 8 + 1 + 384] {
            let mut changed = bytes.clone();
            changed.splice(offset..offset + 1, [253, 1, 0]);
            assert!(Transaction::read(&changed[..], BranchId::Canopy).is_err());
            assert!(reused(&changed, &bundle).is_err());
        }
        // Rust's transparent output range is independently enforced by the original reader.
        for amount in [u64::MAX, 2_100_000_000_000_001] {
            let mut changed = bytes.clone();
            changed[10..18].copy_from_slice(&amount.to_le_bytes());
            assert!(Transaction::read(&changed[..], BranchId::Canopy).is_err());
            assert!(reused(&changed, &bundle).is_err());
        }
        let mut changed = bytes;
        changed[..4].copy_from_slice(&0x8000_0005u32.to_le_bytes());
        assert!(reused(&changed, &bundle).is_err());
    }

    #[test]
    fn typed_count_precheck_retains_the_wire_decoders_limit() {
        // Exercise the count precheck without allocating enormous bundles.
        for count in [zcash_encoding::MAX_COMPACT_SIZE as usize - 1,
                      zcash_encoding::MAX_COMPACT_SIZE as usize,
                      zcash_encoding::MAX_COMPACT_SIZE as usize + 1,
                      usize::MAX] {
            let mut encoded = [0; 9];
            zcash_encoding::CompactSize::write(&mut encoded[..], count).unwrap();
            assert_eq!(zcash_encoding::CompactSize::read(&encoded[..]).is_ok(),
                count <= zcash_encoding::MAX_COMPACT_SIZE as usize);
        }
    }

    #[test]
    fn typed_small_order_value_commitments_do_not_bypass_the_decoder() {
        let seed = bundle(1, 1);
        let cv = || ValueCommitment::derive(NoteValue::from_raw(0),
            ValueCommitTrapdoor::from_bytes([0; 32]).unwrap());
        assert!(bool::from(cv().as_inner().is_small_order()));
        let spend = &seed.shielded_spends()[0];
        let output = &seed.shielded_outputs()[0];
        for bad_spend in [false, true] {
            let spends = if bad_spend {
                vec![SpendDescription::from_parts(cv(), *spend.anchor(), *spend.nullifier(),
                    *spend.rk(), *spend.zkproof(), *spend.spend_auth_sig())]
            } else { seed.shielded_spends().to_vec() };
            let outputs = if bad_spend { seed.shielded_outputs().to_vec() } else {
                vec![OutputDescription::from_parts(cv(), *output.cmu(), output.ephemeral_key().clone(),
                    *output.enc_ciphertext(), *output.out_ciphertext(), *output.zkproof())]
            };
            let invalid = Bundle::from_parts(spends, outputs, *seed.value_balance(), *seed.authorization()).unwrap();
            let bytes = encoded(&invalid);
            assert!(Transaction::read(&bytes[..], BranchId::Canopy).is_err());
            assert!(reused(&bytes, &invalid).is_err());
        }
    }

    #[test]
    fn verification_key_rules_are_not_strengthened_or_weakened() {
        let seed = bundle(1, 1);
        let spend = &seed.shielded_spends()[0];
        let mut identity = [0; 32];
        identity[0] = 1;
        // The byte decoder accepts identity rk. Its small-order rejection is
        // a later consensus check, and must not move into this reader.
        let rk = redjubjub::VerificationKey::<redjubjub::SpendAuth>::try_from(identity).unwrap();
        let typed = Bundle::from_parts(
            vec![SpendDescription::from_parts(spend.cv().clone(), *spend.anchor(), *spend.nullifier(),
                rk, *spend.zkproof(), *spend.spend_auth_sig())],
            seed.shielded_outputs().to_vec(), *seed.value_balance(), *seed.authorization(),
        ).unwrap();
        let bytes = encoded(&typed);
        let expected = Transaction::read(&bytes[..], BranchId::Canopy).unwrap();
        assert_eq!(expected.txid(), reused(&bytes, &typed).unwrap().txid());

        let mut invalid = bytes;
        invalid[27 + 9 + 96..27 + 9 + 128].fill(0xff);
        assert!(Transaction::read(&invalid[..], BranchId::Canopy).is_err());
        assert!(reused(&invalid, &typed).is_err());
    }

    #[test]
    fn joinsplit_fields_keep_the_original_reader_and_amount_bounds() {
        use zcash_primitives::transaction::components::sprout;
        let bundle = bundle(1, 1);
        let seed = Transaction::read(&encoded(&bundle)[..], BranchId::Canopy).unwrap();
        let joinsplit = sprout::JsDescription::read(&[0; 1698][..], true).unwrap();
        let tx = TransactionData::<zcash_primitives::transaction::Authorized>::from_parts(
            TxVersion::V4, BranchId::Canopy, 0, 0u32.into(), seed.transparent_bundle().cloned(),
            Some(sprout::Bundle { joinsplits: vec![joinsplit], joinsplit_pubkey: [0; 32],
                                 joinsplit_sig: [0; 64] }),
            Some(bundle.clone()), None,
        ).freeze().unwrap();
        let mut bytes = Vec::new();
        tx.write(&mut bytes).unwrap();
        assert_eq!(tx.txid(), reused(&bytes, &bundle).unwrap().txid());
        let joinsplit_start = 27 + 8 + 1 + 384 + 1 + 948 + 1;
        for offset in [0, 8] {
            let mut invalid = bytes.clone();
            invalid[joinsplit_start + offset..joinsplit_start + offset + 8].fill(0xff);
            assert!(Transaction::read(&invalid[..], BranchId::Canopy).is_err());
            assert!(reused(&invalid, &bundle).is_err());
        }
    }

    #[test]
    fn ffi_matches_original_digests_and_does_not_write_on_mismatch() {
        let bundle = bundle(1, 1);
        let bytes = encoded(&bundle);
        let mut expected = [[0; 32]; 2];
        let mut actual = [[0; 32]; 2];
        assert!(zcash_transaction_digests(bytes.as_ptr(), bytes.len(), &mut expected[0], &mut expected[1]));
        assert!(unsafe { zcash_v4_transaction_digests_with_sapling(
            bytes.as_ptr(), bytes.len(), &bundle, &mut actual[0], &mut actual[1]) });
        assert_eq!(expected, actual);
        let other = self::bundle(1, 1);
        actual = [[42; 32]; 2];
        assert!(!unsafe { zcash_v4_transaction_digests_with_sapling(
            bytes.as_ptr(), bytes.len(), &other, &mut actual[0], &mut actual[1]) });
        assert_eq!([[42; 32]; 2], actual);
        assert!(!unsafe { zcash_v4_transaction_digests_with_sapling(
            bytes.as_ptr(), bytes.len(), ptr::null(), ptr::null_mut(), ptr::null_mut()) });
    }
}
