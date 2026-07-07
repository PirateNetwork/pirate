use std::convert::TryFrom;

use orchard::{
    keys::{DiversifierIndex, Diversifier, FullViewingKey, IncomingViewingKey, Scope, SpendingKey},
};

use crate::ironwood_protocol::ironwood_keys::zip32::{ExtendedSpendingKey, ChildIndex, ZIP32_PURPOSE};

#[path = "zip32.rs"]
mod zip32;
#[path = "prf_expand.rs"]
mod prf_expand;

// ── IVK operations ──────────────────────────────────────────────────────────

pub fn ivk_to_address(ivk: &[u8; 64], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool {
    let ivk = IncomingViewingKey::from_bytes(ivk);
    if ivk.is_some().into() {
        let d = Diversifier::from_bytes(*diversifier);
        *out = ivk.unwrap().address(d).to_raw_address_bytes();
        true
    } else {
        false
    }
}

pub fn ivk_to_address_from_index(ivk: &[u8; 64], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool {
    let ivk = IncomingViewingKey::from_bytes(ivk);
    if ivk.is_some().into() {
        let idx = DiversifierIndex::from(*diversifier_index);
        *out = ivk.unwrap().address_at(idx).to_raw_address_bytes();
        true
    } else {
        false
    }
}

// ── FVK operations ──────────────────────────────────────────────────────────

pub fn fvk_to_ovk(fvk: &[u8; 96], out: &mut [u8; 32]) -> bool {
    let fvk = FullViewingKey::from_bytes(fvk);
    if fvk.is_some().into() {
        *out = *fvk.unwrap().to_ovk(Scope::External).as_ref();
        true
    } else {
        false
    }
}

pub fn fvk_to_ovk_internal(fvk: &[u8; 96], out: &mut [u8; 32]) -> bool {
    let fvk = FullViewingKey::from_bytes(fvk);
    if fvk.is_some().into() {
        *out = *fvk.unwrap().to_ovk(Scope::Internal).as_ref();
        true
    } else {
        false
    }
}

pub fn fvk_to_ivk(fvk: &[u8; 96], out: &mut [u8; 64]) -> bool {
    let fvk = FullViewingKey::from_bytes(fvk);
    if fvk.is_some().into() {
        *out = fvk.unwrap().to_ivk(Scope::External).to_bytes();
        true
    } else {
        false
    }
}

pub fn fvk_to_ivk_internal(fvk: &[u8; 96], out: &mut [u8; 64]) -> bool {
    let fvk = FullViewingKey::from_bytes(fvk);
    if fvk.is_some().into() {
        *out = fvk.unwrap().to_ivk(Scope::Internal).to_bytes();
        true
    } else {
        false
    }
}

pub fn fvk_to_default_address(fvk: &[u8; 96], out: &mut [u8; 43]) -> bool {
    let fvk = FullViewingKey::from_bytes(fvk);
    if fvk.is_some().into() {
        *out = fvk.unwrap().address_at(0u32, Scope::External).to_raw_address_bytes();
        true
    } else {
        false
    }
}

pub fn fvk_to_default_address_internal(fvk: &[u8; 96], out: &mut [u8; 43]) -> bool {
    let fvk = FullViewingKey::from_bytes(fvk);
    if fvk.is_some().into() {
        *out = fvk.unwrap().address_at(0u32, Scope::Internal).to_raw_address_bytes();
        true
    } else {
        false
    }
}

pub fn fvk_to_address(fvk: &[u8; 96], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool {
    let fvk = FullViewingKey::from_bytes(fvk);
    if fvk.is_some().into() {
        let d = Diversifier::from_bytes(*diversifier);
        *out = fvk.unwrap().address(d, Scope::External).to_raw_address_bytes();
        true
    } else {
        false
    }
}

pub fn fvk_to_address_internal(fvk: &[u8; 96], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool {
    let fvk = FullViewingKey::from_bytes(fvk);
    if fvk.is_some().into() {
        let d = Diversifier::from_bytes(*diversifier);
        *out = fvk.unwrap().address(d, Scope::Internal).to_raw_address_bytes();
        true
    } else {
        false
    }
}

pub fn fvk_to_address_from_index(fvk: &[u8; 96], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool {
    let fvk = FullViewingKey::from_bytes(fvk);
    if fvk.is_some().into() {
        let idx = DiversifierIndex::from(*diversifier_index);
        *out = fvk.unwrap().address_at(idx, Scope::External).to_raw_address_bytes();
        true
    } else {
        false
    }
}

pub fn fvk_to_address_from_index_internal(fvk: &[u8; 96], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool {
    let fvk = FullViewingKey::from_bytes(fvk);
    if fvk.is_some().into() {
        let idx = DiversifierIndex::from(*diversifier_index);
        *out = fvk.unwrap().address_at(idx, Scope::Internal).to_raw_address_bytes();
        true
    } else {
        false
    }
}

// ── SK operations ────────────────────────────────────────────────────────────

pub fn sk_is_valid(sk: &[u8; 32]) -> bool {
    SpendingKey::from_bytes(*sk).is_some().into()
}

pub fn sk_to_fvk(sk: &[u8; 32], out: &mut [u8; 96]) -> bool {
    let sk = SpendingKey::from_bytes(*sk);
    if sk.is_some().into() {
        *out = FullViewingKey::from(&sk.unwrap()).to_bytes();
        true
    } else {
        false
    }
}

pub fn sk_to_default_address(sk: &[u8; 32], out: &mut [u8; 43]) -> bool {
    let sk = SpendingKey::from_bytes(*sk);
    if sk.is_some().into() {
        *out = FullViewingKey::from(&sk.unwrap())
            .address_at(0u32, Scope::External)
            .to_raw_address_bytes();
        true
    } else {
        false
    }
}

pub fn sk_to_default_address_internal(sk: &[u8; 32], out: &mut [u8; 43]) -> bool {
    let sk = SpendingKey::from_bytes(*sk);
    if sk.is_some().into() {
        *out = FullViewingKey::from(&sk.unwrap())
            .address_at(0u32, Scope::Internal)
            .to_raw_address_bytes();
        true
    } else {
        false
    }
}

// ── ZIP32 key derivation ─────────────────────────────────────────────────────

pub fn derive_master_key(seed: &[u8], out: &mut [u8; 73]) -> bool {
    match ExtendedSpendingKey::master(seed) {
        Ok(master) => {
            *out = master.to_bytes();
            true
        }
        Err(_) => false,
    }
}

pub fn derive_child_key(xsk: &[u8; 73], coin_type: u32, account: u32, out: &mut [u8; 73]) -> bool {
    let mut key = match ExtendedSpendingKey::from_bytes(xsk.as_ref()) {
        Ok(k) => k,
        Err(_) => return false,
    };
    for idx in [ZIP32_PURPOSE, coin_type, account] {
        key = match ChildIndex::try_from(idx) {
            Ok(i) => match key.derive_child(i) {
                Ok(k) => k,
                Err(_) => return false,
            },
            Err(_) => return false,
        };
    }
    *out = key.to_bytes();
    true
}

