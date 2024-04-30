use libc::{c_uchar};
// use std::convert::TryInto;
use std::convert::TryFrom;

use orchard::{
    keys::{DiversifierIndex, FullViewingKey, IncomingViewingKey, OutgoingViewingKey, Scope, SpendingKey},
    Address,
};

use crate::orchard_keys::zip32::{ExtendedSpendingKey, ChildIndex, ZIP32_PURPOSE};

mod zip32;
mod prf_expand;

// #[no_mangle]
// pub extern "C" fn orchard_ivk_to_address(
//     ivk_bytes: *const [c_uchar; 64],
//     diversifier_index: *const [c_uchar; 11],
//     out_bytes: *mut [c_uchar; 43],
// ) -> bool {
//     let ivk_bytes = unsafe { *ivk_bytes };
//     let ivk = IncomingViewingKey::from_bytes(&ivk_bytes);
//     if ivk.is_some().into() {
//
//         let diversifier_index = DiversifierIndex::from(unsafe { *diversifier_index });
//         let address = ivk.unwrap().address_at(diversifier_index);
//         let out_bytes = unsafe { &mut *out_bytes };
//         *out_bytes = address.to_raw_address_bytes();
//
//         return true
//     }
//     return false
//
// }

#[no_mangle]
pub extern "C" fn orchard_fvk_to_ovk(
    fvk_bytes: *const [c_uchar; 96],
    out_bytes: *mut [c_uchar; 32],
) -> bool {
    let fvk_bytes = unsafe { *fvk_bytes };
    let fvk = FullViewingKey::from_bytes(&fvk_bytes);
    if fvk.is_some().into() {

        let ovk = fvk.unwrap().to_ovk(Scope::External);
        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = *ovk.as_ref();

        return true
    }
    return false
}

#[no_mangle]
pub extern "C" fn orchard_fvk_to_ovk_internal(
    fvk_bytes: *const [c_uchar; 96],
    out_bytes: *mut [c_uchar; 32],
) -> bool {
    let fvk_bytes = unsafe { *fvk_bytes };
    let fvk = FullViewingKey::from_bytes(&fvk_bytes);
    if fvk.is_some().into() {

        let ovk = fvk.unwrap().to_ovk(Scope::Internal);
        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = *ovk.as_ref();

        return true
    }
    return false
}

#[no_mangle]
pub extern "C" fn orchard_fvk_to_ivk(
    fvk_bytes: *const [c_uchar; 96],
    out_bytes: *mut [c_uchar; 64],
) -> bool {
    let fvk_bytes = unsafe { *fvk_bytes };
    let fvk = FullViewingKey::from_bytes(&fvk_bytes);
    if fvk.is_some().into() {

        let ivk = fvk.unwrap().to_ivk(Scope::External);
        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = ivk.to_bytes();

        return true
    }
    return false
}

#[no_mangle]
pub extern "C" fn orchard_fvk_to_ivk_internal(
    fvk_bytes: *const [c_uchar; 96],
    out_bytes: *mut [c_uchar; 64],
) -> bool {
    let fvk_bytes = unsafe { *fvk_bytes };
    let fvk = FullViewingKey::from_bytes(&fvk_bytes);
    if fvk.is_some().into() {

        let ivk = fvk.unwrap().to_ivk(Scope::Internal);
        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = ivk.to_bytes();

        return true
    }
    return false
}

#[no_mangle]
pub extern "C" fn orchard_fvk_to_default_address_internal(
    fvk_bytes: *const [c_uchar; 96],
    out_bytes: *mut [c_uchar; 43],
) -> bool {
    let fvk_bytes = unsafe { *fvk_bytes };
    let fvk = FullViewingKey::from_bytes(&fvk_bytes);
    if fvk.is_some().into() {

        let address = fvk.unwrap().address_at(0u32,Scope::Internal);
        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = address.to_raw_address_bytes();

        return true
    }
    return false
}

#[no_mangle]
pub extern "C" fn orchard_fvk_to_default_address(
    fvk_bytes: *const [c_uchar; 96],
    out_bytes: *mut [c_uchar; 43],
) -> bool {
    let fvk_bytes = unsafe { *fvk_bytes };
    let fvk = FullViewingKey::from_bytes(&fvk_bytes);
    if fvk.is_some().into() {

        let address = fvk.unwrap().address_at(0u32,Scope::External);
        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = address.to_raw_address_bytes();

        return true
    }
    return false
}

#[no_mangle]
pub extern "C" fn orchard_fvk_to_address_internal(
    fvk_bytes: *const [c_uchar; 96],
    diversifier_index: *const [c_uchar; 11],
    out_bytes: *mut [c_uchar; 43],
) -> bool {
    let fvk_bytes = unsafe { *fvk_bytes };
    let fvk = FullViewingKey::from_bytes(&fvk_bytes);
    if fvk.is_some().into() {

        let diversifier_index = DiversifierIndex::from(unsafe { *diversifier_index });
        let address = fvk.unwrap().address_at(diversifier_index,Scope::Internal);
        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = address.to_raw_address_bytes();

        return true
    }
    return false
}

#[no_mangle]
pub extern "C" fn orchard_fvk_to_address(
    fvk_bytes: *const [c_uchar; 96],
    diversifier_index: *const [c_uchar; 11],
    out_bytes: *mut [c_uchar; 43],
) -> bool {
    let fvk_bytes = unsafe { *fvk_bytes };
    let fvk = FullViewingKey::from_bytes(&fvk_bytes);
    if fvk.is_some().into() {

        let diversifier_index = DiversifierIndex::from(unsafe { *diversifier_index });
        let address = fvk.unwrap().address_at(diversifier_index,Scope::External);
        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = address.to_raw_address_bytes();

        return true
    }
    return false
}


#[no_mangle]
pub extern "C" fn orchard_sk_is_valid(
    sk_bytes: *const [c_uchar; 32],
) -> bool {
    let sk_bytes = unsafe { *sk_bytes };
    let sk = SpendingKey::from_bytes(sk_bytes);
    if sk.is_some().into() {
        return true
    }
    return false
}

#[no_mangle]
pub extern "C" fn orchard_sk_to_fvk(
    sk_bytes: *const [c_uchar; 32],
    out_bytes: *mut [c_uchar; 96],
) -> bool {
    let sk_bytes = unsafe { *sk_bytes };
    let sk = SpendingKey::from_bytes(sk_bytes);
    if sk.is_some().into() {

        let fvk = FullViewingKey::from(&sk.unwrap());
        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = fvk.to_bytes();

        return true
    }
    return false
}

#[no_mangle]
pub extern "C" fn orchard_sk_to_default_address_internal(
    sk_bytes: *const [c_uchar; 32],
    out_bytes: *mut [c_uchar; 43],
) -> bool {
    let sk_bytes = unsafe { *sk_bytes };
    let sk = SpendingKey::from_bytes(sk_bytes);
    if sk.is_some().into() {

        let fvk = FullViewingKey::from(&sk.unwrap());
        let address = fvk.address_at(0u32,Scope::Internal);
        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = address.to_raw_address_bytes();

        return true
    }
    return false
}

#[no_mangle]
pub extern "C" fn orchard_sk_to_default_address(
    sk_bytes: *const [c_uchar; 32],
    out_bytes: *mut [c_uchar; 43],
) -> bool {
    let sk_bytes = unsafe { *sk_bytes };
    let sk = SpendingKey::from_bytes(sk_bytes);
    if sk.is_some().into() {

        let fvk = FullViewingKey::from(&sk.unwrap());
        let address = fvk.address_at(0u32,Scope::External);
        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = address.to_raw_address_bytes();

        return true
    }
    return false
}

/// Derive the master ExtendedSpendingKey from a seed.
#[no_mangle]
pub extern "C" fn orchard_derive_master_key(
    seed: *const u8,
    seed_len: usize,
    out_bytes: *mut [c_uchar; 73],
) -> bool {
    let seed = unsafe { std::slice::from_raw_parts(seed, seed_len) };
    let master = ExtendedSpendingKey::master(seed);

    if master.is_ok() {

        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = master.unwrap().to_bytes();

        return true
    }
    return false

}

/// Derive the master ExtendedSpendingKey from a seed.
#[no_mangle]
pub extern "C" fn orchard_derive_child_key(
    xsk_bytes: *const u8,
    coin_type: u32,
    account: u32,
    out_bytes: *mut [c_uchar; 73],
) -> bool {
    let xsk_bytes = unsafe { std::slice::from_raw_parts(xsk_bytes, 73) };
    let master = ExtendedSpendingKey::from_bytes(xsk_bytes);

    if master.is_ok() {
        let mut xsk = master.unwrap();

        xsk = match ChildIndex::try_from(ZIP32_PURPOSE) {
            Ok(i) => match xsk.derive_child(i) {
                Ok(key) => key,
                Err(_) => return false
            },
            Err(_) => return false
        };

        xsk = match ChildIndex::try_from(coin_type) {
            Ok(i) => match xsk.derive_child(i) {
                Ok(key) => key,
                Err(_) => return false
            },
            Err(_) => return false
        };

        xsk = match ChildIndex::try_from(account) {
            Ok(i) => match xsk.derive_child(i) {
                Ok(key) => key,
                Err(_) => return false
            },
            Err(_) => return false
        };

        // let path = &[
        //     child_index1,
        //     child_index2,
        //     ChildIndex::try_from(account)?,
        // ];
        //
        //
        // for i in path {
        //     xsk = match xsk.derive_child(*i) {
        //         Ok(k) => k,
        //         Err(_) => return false
        //     };
        // }

        let out_bytes = unsafe { &mut *out_bytes };
        *out_bytes = xsk.to_bytes();

        return true
    }
    return false

}
