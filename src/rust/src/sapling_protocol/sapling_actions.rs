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

impl Output {
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
