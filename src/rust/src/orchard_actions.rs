use byteorder::{ByteOrder,LittleEndian};
use libc::c_uchar;

use orchard::{
    Address, Note,
    keys::{IncomingViewingKey, PreparedIncomingViewingKey, FullViewingKey, Scope},
    note::{Nullifier, RandomSeed},
    note_encryption::OrchardDomain,
    value::NoteValue};

use zcash_note_encryption::try_note_decryption;

use crate::orchard_bundle::Action;
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
            let value = decrypted.0.value().inner();
            let mut buf = [0; 8];
            LittleEndian::write_u64(&mut buf, value.into());
            *value_out = LittleEndian::read_u64(&buf);

            let address_out = unsafe { &mut *address_out };
            *address_out = decrypted.1.to_raw_address_bytes();

            let memo_out = unsafe { &mut *memo_out };
            *memo_out = decrypted.2;

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
            let ivk = fvk.to_ivk(Scope::External);
            let prepared_ivk = PreparedIncomingViewingKey::new(&ivk);
            let action = &orchard_action.inner();
            let domain = OrchardDomain::for_action(action);
            let decrypted = match try_note_decryption(&domain, &prepared_ivk, action) {
                Some(r) => r,
                None => return false,
            };

            let value_out = unsafe { &mut *value_out };
            let value = decrypted.0.value().inner();
            let mut buf = [0; 8];
            LittleEndian::write_u64(&mut buf, value.into());
            *value_out = LittleEndian::read_u64(&buf);

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
pub extern "C" fn get_nullifer_from_parts(
    fvk_bytes: *const [c_uchar; 96],
    address_bytes: *const [c_uchar; 43],
    value: u64,
    rho_bytes: *const [c_uchar; 32],
    rseed_bytes: *const [c_uchar; 32],
    nullifier_out: *mut [u8; 32],
) -> bool {
    //Deserialize FVK
    let fvk_bytes = unsafe { *fvk_bytes };
    let fvk = match FullViewingKey::from_bytes(&fvk_bytes) {
        Some(k) => k,
        None => return false
    };

    //Deserialize Address
    let address_bytes = unsafe { *address_bytes };
    let address = match de_ct(Address::from_raw_address_bytes(&address_bytes))  {
        Some(k) => k,
        None => return false
    };

    //Get NoteValue from u64
    let note_value = NoteValue::from_raw(value);

    //Deserialize Rho
    let rho_bytes = unsafe { *rho_bytes };
    let rho = match de_ct(Nullifier::from_bytes(&rho_bytes)) {
        Some(k) => k,
        None => return false
    };

    //Deserialize rseed
    let rseed_bytes = unsafe { *rseed_bytes };
    let rseed = match de_ct(RandomSeed::from_bytes(rseed_bytes, &rho)) {
        Some(k) => k,
        None => return false
    };

    let note = Note::from_parts(address, note_value, rho, rseed).unwrap();

    let nullifier = note.nullifier(&fvk);
    let nullifier_out = unsafe { &mut *nullifier_out };
    *nullifier_out = nullifier.to_bytes();

    return true

}
