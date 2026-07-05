use libc::c_uchar;

use orchard::{
    Address, Note,
    keys::{OutgoingViewingKey, IncomingViewingKey, PreparedIncomingViewingKey, FullViewingKey, Scope},
    note::{NoteVersion, Rho, RandomSeed},
    note_encryption::OrchardDomain,
    value::NoteValue};

use zcash_note_encryption::{try_note_decryption, try_output_recovery_with_ovk};

use crate::orchard_protocol::orchard_bundle::Action;
use subtle::CtOption;

/// Converts CtOption<t> into Option<T>
fn de_ct<T>(ct: CtOption<T>) -> Option<T> {
    if ct.is_some().into() {
        Some(ct.unwrap())
    } else {
        None
    }
}

#[no_mangle]
pub extern "C" fn try_orchard_decrypt_action_ovk(
    orchard_action: *const Action,
    ovk_bytes: *const [c_uchar; 32],
    value_out: *mut u64,
    address_out: *mut [u8; 43],
    memo_out: *mut [u8; 512],
    rho_out: *mut [u8; 32],
    rseed_out: *mut [u8; 32],
) -> bool {

    let ovk_bytes = unsafe { *ovk_bytes };
    let ovk = OutgoingViewingKey::from(ovk_bytes);

    if let Some(orchard_action) = unsafe { orchard_action.as_ref() } {

        let action = &orchard_action.inner();
        let domain = OrchardDomain::for_action(action);
        let decrypted = match try_output_recovery_with_ovk(&domain, &ovk, action, action.cv_net(), &action.encrypted_note().out_ciphertext) {
            Some(r) => r,
            None => return false,
        };

        let value_out = unsafe { &mut *value_out };
        *value_out = decrypted.0.value().inner();

        let address_out = unsafe { &mut *address_out };
        *address_out = decrypted.1.to_raw_address_bytes();

        let memo_out = unsafe { &mut *memo_out };
        *memo_out = decrypted.2;

        let rho_out = unsafe { &mut *rho_out };
        *rho_out = decrypted.0.rho().to_bytes();

        let rseed_out = unsafe { &mut *rseed_out };
        *rseed_out = *decrypted.0.rseed().as_bytes();

        return true
    }

    return false
}

#[no_mangle]
pub extern "C" fn try_orchard_decrypt_action_ivk(
    orchard_action: *const Action,
    ivk_bytes: *const [c_uchar; 64],
    value_out: *mut u64,
    address_out: *mut [u8; 43],
    memo_out: *mut [u8; 512],
    rho_out: *mut [u8; 32],
    rseed_out: *mut [u8; 32],
) -> bool {

    let ivk_bytes = unsafe { *ivk_bytes };
    let ivk = IncomingViewingKey::from_bytes(&ivk_bytes);
    if ivk.is_some().into() {

        if let Some(orchard_action) = unsafe { orchard_action.as_ref() } {

            let prepared_ivk = PreparedIncomingViewingKey::new(&ivk.unwrap());
            let action = &orchard_action.inner();
            let domain = OrchardDomain::for_action(action);
            let decrypted = match try_note_decryption(&domain, &prepared_ivk, action) {
                Some(r) => r,
                None => return false,
            };

            let value_out = unsafe { &mut *value_out };
            *value_out = decrypted.0.value().inner();

            let address_out = unsafe { &mut *address_out };
            *address_out = decrypted.1.to_raw_address_bytes();

            let memo_out = unsafe { &mut *memo_out };
            *memo_out = decrypted.2;

            let rho_out = unsafe { &mut *rho_out };
            *rho_out = decrypted.0.rho().to_bytes();

            let rseed_out = unsafe { &mut *rseed_out };
            *rseed_out = *decrypted.0.rseed().as_bytes();

            return true
        }
    }
    return false
}

#[no_mangle]
pub extern "C" fn try_orchard_decrypt_action_fvk(
    orchard_action: *const Action,
    fvk_bytes: *const [c_uchar; 96],
    value_out: *mut u64,
    address_out: *mut [u8; 43],
    memo_out: *mut [u8; 512],
    rho_out: *mut [u8; 32],
    rseed_out: *mut [u8; 32],
    nullifier_out: *mut [u8; 32],
) -> bool {

    let fvk_bytes = unsafe { *fvk_bytes };
    let fvkopt = FullViewingKey::from_bytes(&fvk_bytes);
    if fvkopt.is_some().into() {

        let fvk = fvkopt.unwrap();

        if let Some(orchard_action) = unsafe { orchard_action.as_ref() } {
            let action = &orchard_action.inner();
            let domain = OrchardDomain::for_action(action);

            // Try external scope first, then internal (change addresses).
            let prepared_ivk_ext = PreparedIncomingViewingKey::new(&fvk.to_ivk(Scope::External));
            let prepared_ivk_int = PreparedIncomingViewingKey::new(&fvk.to_ivk(Scope::Internal));
            let decrypted = match try_note_decryption(&domain, &prepared_ivk_ext, action)
                .or_else(|| try_note_decryption(&domain, &prepared_ivk_int, action))
            {
                Some(r) => r,
                None => return false,
            };

            let value_out = unsafe { &mut *value_out };
            *value_out = decrypted.0.value().inner();

            let address_out = unsafe { &mut *address_out };
            *address_out = decrypted.1.to_raw_address_bytes();

            let memo_out = unsafe { &mut *memo_out };
            *memo_out = decrypted.2;

            let rho_out = unsafe { &mut *rho_out };
            *rho_out = decrypted.0.rho().to_bytes();

            let rseed_out = unsafe { &mut *rseed_out };
            *rseed_out = *decrypted.0.rseed().as_bytes();

            let nullifier = decrypted.0.nullifier(&fvk);
            let nullifier_out = unsafe { &mut *nullifier_out };
            *nullifier_out = nullifier.to_bytes();

            return true
        }
    }
    return false
}

#[no_mangle]
/// Compute nullifier for an Orchard note from its constituent parts
/// 
/// This is the standalone version that takes note components directly.
/// For computing from an encrypted action, use action_compute_nullifier instead.
pub(crate) fn compute_nullifier(
    fvk_bytes: &[c_uchar; 96],
    address_bytes: &[c_uchar; 43],
    value: u64,
    rho_bytes: &[c_uchar; 32],
    rseed_bytes: &[c_uchar; 32],
    nullifier_out: &mut [u8; 32],
) -> bool {
    //Deserialize FVK
    let fvk = match FullViewingKey::from_bytes(fvk_bytes) {
        Some(k) => k,
        None => return false
    };

    //Deserialize Address
    let address = match de_ct(Address::from_raw_address_bytes(address_bytes))  {
        Some(k) => k,
        None => return false
    };

    //Get NoteValue from u64
    let note_value = NoteValue::from_raw(value);

    //Deserialize Rho directly from bytes
    let rho = match de_ct(Rho::from_bytes(rho_bytes)) {
        Some(k) => k,
        None => return false
    };

    //Deserialize rseed
    let rseed = match de_ct(RandomSeed::from_bytes(*rseed_bytes, &rho)) {
        Some(k) => k,
        None => return false
    };

    let note = match de_ct(Note::from_parts(address, note_value, rho, rseed, NoteVersion::V2)) {
        Some(n) => n,
        None => return false,
    };

    let nullifier = note.nullifier(&fvk);
    *nullifier_out = nullifier.to_bytes();

    return true
}

/// Derives the Orchard Outgoing Cipher Key (OCK) for a specific output.
///
/// The OCK can decrypt the outgoing ciphertext of a shielded output, allowing recovery
/// of the note plaintext without access to the full viewing key. This is used for
/// proof-of-payment disclosures.
pub(crate) fn derive_orchard_ock(
    orchard_action: &Action,
    ovk_bytes: &[u8; 32],
    ock_out: &mut [u8; 32],
) -> bool {
    let ovk = OutgoingViewingKey::from(*ovk_bytes);
    let action = &orchard_action.inner();

    let cv = action.cv_net();
    let cmx_bytes = action.cmx().to_bytes();
    let epk_bytes = action.encrypted_note().epk_bytes;

    use blake2b_simd::Params;
    const PRF_OCK_ORCHARD_PERSONALIZATION: &[u8; 16] = b"Zcash_Orchardock";

    let ock = Params::new()
        .hash_length(32)
        .personal(PRF_OCK_ORCHARD_PERSONALIZATION)
        .to_state()
        .update(ovk.as_ref())
        .update(&cv.to_bytes())
        .update(&cmx_bytes)
        .update(&epk_bytes)
        .finalize();

    ock_out.copy_from_slice(ock.as_bytes());
    true
}

/// Attempts to decrypt an Orchard action output using an Outgoing Cipher Key (OCK).
///
/// This function uses the OCK (derived from derive_orchard_ock) to decrypt the outgoing
/// ciphertext and then uses that information to decrypt the note plaintext. Returns true
/// if decryption succeeds and populates the output parameters with note details.
pub(crate) fn try_orchard_decrypt_action_ock(
    orchard_action: &Action,
    ock_bytes: &[u8; 32],
    value_out: &mut u64,
    address_out: &mut [u8; 43],
    memo_out: &mut [u8; 512],
    rho_out: &mut [u8; 32],
    rseed_out: &mut [u8; 32],
) -> bool {
    use zcash_note_encryption::{OutgoingCipherKey, try_output_recovery_with_ock};

    let ock = OutgoingCipherKey::from(*ock_bytes);
    let action = &orchard_action.inner();
    let domain = OrchardDomain::for_action(action);

    let decrypted = match try_output_recovery_with_ock(
        &domain,
        &ock,
        action,
        &action.encrypted_note().out_ciphertext,
    ) {
        Some(r) => r,
        None => return false,
    };

    *value_out = decrypted.0.value().inner();
    *address_out = decrypted.1.to_raw_address_bytes();
    *memo_out = decrypted.2;
    *rho_out = decrypted.0.rho().to_bytes();
    *rseed_out = *decrypted.0.rseed().as_bytes();

    true
}
