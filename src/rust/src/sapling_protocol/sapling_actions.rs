use group::{cofactor::CofactorGroup, GroupEncoding};
use sapling_crypto::{
    keys::OutgoingViewingKey,
    note_encryption::{
        PreparedIncomingViewingKey, SaplingDomain, Zip212Enforcement,
        try_sapling_note_decryption, try_sapling_output_recovery,
    },
    value::{NoteValue, ValueCommitment},
    Note, NullifierDerivingKey, PaymentAddress, Rseed, SaplingIvk,
};
use zcash_note_encryption::{EphemeralKeyBytes, OutgoingCipherKey, try_output_recovery_with_ock};
use sapling_crypto::note_encryption::prf_ock;

use crate::de_ct;
use super::sapling_bundle::Output;

// ── Note decryption ──────────────────────────────────────────────────────────

/// A validated, call-local IVK preparation. Invalid encodings retain the same
/// always-false decryption behavior as the per-output API.
pub(crate) struct PreparedSaplingIncomingViewingKey(Option<PreparedIncomingViewingKey>);

pub(crate) fn prepare_sapling_ivk(ivk_bytes: &[u8; 32]) -> Box<PreparedSaplingIncomingViewingKey> {
    Box::new(PreparedSaplingIncomingViewingKey(
        de_ct(jubjub::Scalar::from_bytes(ivk_bytes))
            .map(|scalar| PreparedIncomingViewingKey::new(&SaplingIvk(scalar))),
    ))
}

#[cfg(test)]
mod prepared_ivk_tests {
    use super::*;
    use group::ff::PrimeField;
    use sapling_crypto::{
        bundle::OutputDescription,
        note::ExtractedNoteCommitment,
        note_encryption::sapling_note_encryption,
        value::ValueCommitTrapdoor,
        zip32::ExtendedSpendingKey,
        Diversifier,
    };
    use ::zip32::Scope;
    use zcash_note_encryption::Domain;

    type Plaintext = (u64, [u8; 11], [u8; 32], [u8; 512], [u8; 32], u8, [u8; 32], [u8; 32]);

    fn decrypt(output: &Output, key: &[u8; 32], prepared: Option<&PreparedSaplingIncomingViewingKey>)
        -> (bool, Plaintext)
    {
        let mut result: Plaintext = (0xaaaa, [0xaa; 11], [0xaa; 32], [0xaa; 512],
            [0xaa; 32], 0xaa, [0xaa; 32], [0xaa; 32]);
        let accepted = if let Some(prepared) = prepared {
            output.try_decrypt_output_prepared_ivk(prepared, &mut result.0, &mut result.1,
                &mut result.2, &mut result.3, &mut result.4, &mut result.5, &mut result.6, &mut result.7)
        } else {
            // Retain the original per-output key decoding/preparation as oracle.
            output.try_decrypt_output_ivk(key, &mut result.0, &mut result.1,
                &mut result.2, &mut result.3, &mut result.4, &mut result.5, &mut result.6, &mut result.7)
        };
        (accepted, result)
    }

    #[test]
    fn prepared_sapling_ivk_matches_original_for_both_scopes_and_plaintext_versions() {
        let dfvk = ExtendedSpendingKey::master(&[7; 32]).to_diversifiable_full_viewing_key();
        let mut rng = rand::thread_rng();
        for scope in [Scope::External, Scope::Internal] {
            let key = dfvk.to_ivk(scope);
            let key_bytes = key.0.to_repr();
            let prepared = prepare_sapling_ivk(&key_bytes);
            let recipient = key.to_payment_address(Diversifier([0; 11])).unwrap();
            for rseed in [Rseed::BeforeZip212(jubjub::Scalar::from(11)), Rseed::AfterZip212([0x42; 32])] {
                let note = recipient.create_note(NoteValue::from_raw(123456), rseed);
                let cmu = note.cmu();
                let cv = ValueCommitment::derive(note.value(), ValueCommitTrapdoor::random(&mut rng));
                let encryptor = sapling_note_encryption(None, note.clone(), [0x37; 512], &mut rng);
                let ciphertext = encryptor.encrypt_note_plaintext();
                let epk = SaplingDomain::epk_bytes(encryptor.epk());
                let output = Output(OutputDescription::from_parts(cv.clone(), cmu, epk.clone(),
                    ciphertext, [0; 80], [0; 192]));
                let original = decrypt(&output, &key_bytes, None);
                assert!(original.0);
                assert_eq!(original.1.0, note.value().inner());
                assert_eq!(original.1.1, recipient.diversifier().0);
                assert_eq!(&original.1.2, &recipient.to_bytes()[11..]);
                assert_eq!(original.1.3, [0x37; 512]);
                assert_eq!(original.1.6, cmu.to_bytes());
                assert_eq!(original.1.7, note.rcm().to_bytes());
                assert_eq!(original.1.5, if matches!(note.rseed(), Rseed::BeforeZip212(_)) { 1 } else { 2 });
                for _ in 0..8 {
                    assert_eq!(original, decrypt(&output, &key_bytes, Some(&prepared)));
                }

                let mut corrupted_ciphertext = ciphertext;
                corrupted_ciphertext[20] ^= 1;
                let different_cmu = ExtractedNoteCommitment::from_bytes(&jubjub::Base::from(9).to_repr()).unwrap();
                let malformed = [
                    Output(OutputDescription::from_parts(cv.clone(), cmu, epk.clone(), corrupted_ciphertext, [0; 80], [0; 192])),
                    Output(OutputDescription::from_parts(cv.clone(), cmu, EphemeralKeyBytes([0xff; 32]), ciphertext, [0; 80], [0; 192])),
                    Output(OutputDescription::from_parts(cv.clone(), different_cmu, epk.clone(), ciphertext, [0; 80], [0; 192])),
                ];
                for bad in malformed {
                    let reference = decrypt(&bad, &key_bytes, None);
                    assert!(!reference.0);
                    assert_eq!(reference, decrypt(&bad, &key_bytes, Some(&prepared)));
                    assert_eq!(reference.1, (0xaaaa, [0xaa; 11], [0xaa; 32], [0xaa; 512], [0xaa; 32], 0xaa, [0xaa; 32], [0xaa; 32]));
                }
                let other_scope = if scope == Scope::External { Scope::Internal } else { Scope::External };
                for foreign_key in [[0xff; 32], [0; 32], jubjub::Scalar::from(1).to_repr(),
                    dfvk.to_ivk(other_scope).0.to_repr()] {
                    let foreign_prepared = prepare_sapling_ivk(&foreign_key);
                    let reference = decrypt(&output, &foreign_key, None);
                    assert!(!reference.0);
                    assert_eq!(reference, decrypt(&output, &foreign_key, Some(&foreign_prepared)));
                    assert_eq!(reference.1, (0xaaaa, [0xaa; 11], [0xaa; 32], [0xaa; 512], [0xaa; 32], 0xaa, [0xaa; 32], [0xaa; 32]));
                }
            }
        }
    }
}

impl Output {
    pub(crate) fn try_decrypt_output_prepared_ivk(
        &self,
        ivk: &PreparedSaplingIncomingViewingKey,
        value_out: &mut u64,
        diversifier_out: &mut [u8; 11],
        pk_d_out: &mut [u8; 32],
        memo_out: &mut [u8; 512],
        rseed_out: &mut [u8; 32],
        leadbyte_out: &mut u8,
        cmu_out: &mut [u8; 32],
        rcm_out: &mut [u8; 32],
    ) -> bool {
        let Some(prepared_ivk) = ivk.0.as_ref() else {
            return false;
        };
        let decrypted = match try_sapling_note_decryption(
            prepared_ivk,
            &self.0,
            Zip212Enforcement::Off,
        ) {
            Some(r) => r,
            None => return false,
        };

        *value_out = decrypted.0.value().inner();
        *diversifier_out = decrypted.1.diversifier().0;
        let addr_bytes = decrypted.1.to_bytes();
        pk_d_out.copy_from_slice(&addr_bytes[11..43]);
        *memo_out = decrypted.2;
        match decrypted.0.rseed() {
            Rseed::BeforeZip212(rcm) => {
                *rseed_out = rcm.to_bytes();
                *leadbyte_out = 0x01;
                *rcm_out = rcm.to_bytes();
            }
            Rseed::AfterZip212(rseed_bytes) => {
                rseed_out.copy_from_slice(rseed_bytes);
                *leadbyte_out = 0x02;
                *rcm_out = decrypted.0.rcm().to_bytes();
            }
        }
        *cmu_out = self.0.cmu().to_bytes();
        true
    }

    /// Attempt to decrypt a Sapling output using an Incoming Viewing Key (IVK).
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
        let ivk_scalar = match de_ct(jubjub::Scalar::from_bytes(ivk_bytes)) {
            Some(s) => s,
            None => return false,
        };
        let ivk = SaplingIvk(ivk_scalar);
        let prepared_ivk = PreparedIncomingViewingKey::new(&ivk);

        let decrypted = match try_sapling_note_decryption(
            &prepared_ivk,
            &self.0,
            Zip212Enforcement::Off,
        ) {
            Some(r) => r,
            None => return false,
        };

        *value_out = decrypted.0.value().inner();
        *diversifier_out = decrypted.1.diversifier().0;
        let addr_bytes = decrypted.1.to_bytes();
        pk_d_out.copy_from_slice(&addr_bytes[11..43]);
        *memo_out = decrypted.2;

        match decrypted.0.rseed() {
            Rseed::BeforeZip212(rcm) => {
                *rseed_out = rcm.to_bytes();
                *leadbyte_out = 0x01;
                *rcm_out = rcm.to_bytes();
            }
            Rseed::AfterZip212(rseed_bytes) => {
                rseed_out.copy_from_slice(rseed_bytes);
                *leadbyte_out = 0x02;
                let rcm = decrypted.0.rcm();
                *rcm_out = rcm.to_bytes();
            }
        }

        *cmu_out = self.0.cmu().to_bytes();
        true
    }

    /// Attempt to decrypt a Sapling output using an Outgoing Viewing Key (OVK).
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

        let decrypted = match try_sapling_output_recovery(
            &ovk,
            &self.0,
            Zip212Enforcement::Off,
        ) {
            Some(r) => r,
            None => return false,
        };

        *value_out = decrypted.0.value().inner();
        *diversifier_out = decrypted.1.diversifier().0;
        let addr_bytes = decrypted.1.to_bytes();
        pk_d_out.copy_from_slice(&addr_bytes[11..43]);
        *memo_out = decrypted.2;

        match decrypted.0.rseed() {
            Rseed::BeforeZip212(rcm) => {
                *rseed_out = rcm.to_bytes();
                *leadbyte_out = 0x01;
                *rcm_out = rcm.to_bytes();
            }
            Rseed::AfterZip212(rseed_bytes) => {
                rseed_out.copy_from_slice(rseed_bytes);
                *leadbyte_out = 0x02;
                let rcm = decrypted.0.rcm();
                *rcm_out = rcm.to_bytes();
            }
        }

        *cmu_out = self.0.cmu().to_bytes();
        true
    }

    /// Compute nullifier for this output using the IVK and nullifier deriving key.
    pub(crate) fn compute_nullifier(
        &self,
        ivk: &[u8; 32],
        _ak: &[u8; 32],
        nk: &[u8; 32],
        position: u64,
        result: &mut [u8; 32],
    ) -> bool {
        let ivk_scalar = match de_ct(jubjub::Scalar::from_bytes(ivk)) {
            Some(s) => s,
            None => return false,
        };
        let sapling_ivk = SaplingIvk(ivk_scalar);
        let prepared_ivk = PreparedIncomingViewingKey::new(&sapling_ivk);

        let decrypted = match try_sapling_note_decryption(
            &prepared_ivk,
            &self.0,
            Zip212Enforcement::Off,
        ) {
            Some(r) => r,
            None => return false,
        };

        let nk = match de_ct(jubjub::ExtendedPoint::from_bytes(nk)) {
            Some(p) => p,
            None => return false,
        };
        let nk = match de_ct(nk.into_subgroup()) {
            Some(nk) => NullifierDerivingKey(nk),
            None => return false,
        };

        let nf = decrypted.0.nf(&nk, position);
        result.copy_from_slice(&nf.0);
        true
    }

    /// Attempt to decrypt a Sapling output using an Outgoing Cipher Key (OCK).
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
        let ock = OutgoingCipherKey(*ock);
        let domain = SaplingDomain::new(Zip212Enforcement::Off);

        let decrypted = match try_output_recovery_with_ock(
            &domain,
            &ock,
            &self.0,
            self.0.out_ciphertext(),
        ) {
            Some(r) => r,
            None => return false,
        };

        *value_out = decrypted.0.value().inner();
        *diversifier_out = decrypted.1.diversifier().0;
        let addr_bytes = decrypted.1.to_bytes();
        pk_d_out.copy_from_slice(&addr_bytes[11..43]);
        *memo_out = decrypted.2;

        match decrypted.0.rseed() {
            Rseed::BeforeZip212(rcm) => {
                *rseed_out = rcm.to_bytes();
                *leadbyte_out = 0x01;
                *rcm_out = rcm.to_bytes();
            }
            Rseed::AfterZip212(rseed_bytes) => {
                rseed_out.copy_from_slice(rseed_bytes);
                *leadbyte_out = 0x02;
                let rcm = decrypted.0.rcm();
                *rcm_out = rcm.to_bytes();
            }
        }

        *cmu_out = self.0.cmu().to_bytes();
        true
    }
}

// ── Standalone note operations ───────────────────────────────────────────────

/// Compute a Sapling nullifier from raw note components.
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

    let rseed = match de_ct(jubjub::Scalar::from_bytes(rcm)) {
        Some(s) => Rseed::BeforeZip212(s),
        None => return false,
    };

    let note = Note::from_parts(recipient, NoteValue::from_raw(value), rseed);

    let nk = match de_ct(jubjub::ExtendedPoint::from_bytes(nk)) {
        Some(p) => p,
        None => return false,
    };
    let nk = match de_ct(nk.into_subgroup()) {
        Some(nk) => NullifierDerivingKey(nk),
        None => return false,
    };

    let nf = note.nf(&nk, position);
    result.copy_from_slice(&nf.0);
    true
}

/// Derive the Outgoing Cipher Key (OCK) for a Sapling output.
pub(crate) fn derive_sapling_ock(
    ovk: &[u8; 32],
    cv: &[u8; 32],
    cmu: &[u8; 32],
    epk: &[u8; 32],
    ock_out: &mut [u8; 32],
) -> bool {
    let cv = match de_ct(ValueCommitment::from_bytes_not_small_order(cv)) {
        Some(p) => p,
        None => return false,
    };
    let ovk = OutgoingViewingKey(*ovk);
    let epk = EphemeralKeyBytes(*epk);
    let ock = prf_ock(&ovk, &cv, cmu, &epk);
    ock_out.copy_from_slice(ock.as_ref());
    true
}
