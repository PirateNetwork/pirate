use group::GroupEncoding;
use zcash_primitives::{
    sapling::{
        keys::{ExpandedSpendingKey, FullViewingKey},
        Diversifier, SaplingIvk,
    },
    zip32::{sapling::DiversifiableFullViewingKey, Scope},
};

// ── Diversifier ──────────────────────────────────────────────────────────────

/// Returns true if the 11-byte diversifier produces a valid Jubjub point.
pub fn check_diversifier(diversifier: &[u8; 11]) -> bool {
    Diversifier(*diversifier).g_d().is_some()
}

// ── IVK operations ───────────────────────────────────────────────────────────

/// Returns the full 43-byte serialized PaymentAddress for the given ivk and diversifier.
/// Returns false if the ivk scalar is not canonical or the diversifier produces no valid point.
pub fn ivk_to_address(ivk: &[u8; 32], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool {
    let ivk = Option::from(jubjub::Fr::from_bytes(ivk));
    if let Some(ivk) = ivk {
        if let Some(addr) = SaplingIvk(ivk).to_payment_address(Diversifier(*diversifier)) {
            *out = addr.to_bytes();
            return true;
        }
    }
    false
}

/// Converts the 11-byte diversifier index directly to a Sapling diversifier, then
/// derives the full 43-byte serialized PaymentAddress using the IVK.
/// Returns false if the diversifier index does not hash to a valid Jubjub point,
/// or if the ivk scalar is not canonical.
pub fn ivk_to_address_from_index(ivk: &[u8; 32], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool {
    let diversifier = Diversifier(*diversifier_index);
    if diversifier.g_d().is_none() {
        return false;
    }
    let ivk_scalar = Option::from(jubjub::Fr::from_bytes(ivk));
    if let Some(ivk_scalar) = ivk_scalar {
        if let Some(addr) = SaplingIvk(ivk_scalar).to_payment_address(diversifier) {
            *out = addr.to_bytes();
            return true;
        }
    }
    false
}



// ── FVK derivation ───────────────────────────────────────────────────────────

// ── FVK → IVK (scoped) ──────────────────────────────────────────────────────

/// Builds a `DiversifiableFullViewingKey` from a 96-byte raw FVK by appending
/// a zeroed 32-byte `DiversifierKey`. The dk is irrelevant for IVK derivation
/// but required by the DFVK API.
fn dfvk_from_fvk(fvk: &[u8; 96]) -> Option<DiversifiableFullViewingKey> {
    let mut bytes = [0u8; 128];
    bytes[..96].copy_from_slice(fvk);
    // bytes[96..] remain zero — dk is unused for ivk derivation
    DiversifiableFullViewingKey::from_bytes(&bytes)
}

/// Derives the external incoming viewing key (ivk) from a 96-byte Sapling FVK.
/// ivk = CRH^ivk(ak, nk) using Scope::External.
/// Returns false if the FVK bytes are invalid.
pub fn fvk_to_ivk(fvk: &[u8; 96], out: &mut [u8; 32]) -> bool {
    match dfvk_from_fvk(fvk) {
        Some(dfvk) => {
            *out = dfvk.to_ivk(Scope::External).to_repr();
            true
        }
        None => false,
    }
}

/// Derives the internal incoming viewing key (ivk) from a 96-byte Sapling FVK.
/// ivk_internal = CRH^ivk(ak, nk_internal) where nk_internal is tweaked via ZIP 32.
/// Returns false if the FVK bytes are invalid.
pub fn fvk_to_ivk_internal(fvk: &[u8; 96], out: &mut [u8; 32]) -> bool {
    match dfvk_from_fvk(fvk) {
        Some(dfvk) => {
            *out = dfvk.to_ivk(Scope::Internal).to_repr();
            true
        }
        None => false,
    }
}

// ── FVK → Address ───────────────────────────────────────────────────────────

/// Scans raw 11-byte diversifiers starting from `[0u8; 11]` (little-endian) until one
/// produces a valid Jubjub point and a payment address for the given IVK.
///
/// This treats index bytes **directly** as diversifier bytes (no dk PRF mapping),
/// matching the same convention as `ivk_to_address_from_index`.
/// `dfvk.default_address()` / `dfvk.change_address()` must NOT be used here
/// because they apply a `DiversifierKey` FF1-AES-256 PRF to map indices to
/// diversifiers — with a zeroed dk that produces results entirely inconsistent
/// with the real-dk path used by the legacy `SaplingExtendedFullViewingKey::DefaultAddress()`.
fn ivk_default_address(ivk: &SaplingIvk, out: &mut [u8; 43]) -> bool {
    let mut d = [0u8; 11];
    loop {
        let diversifier = Diversifier(d);
        if diversifier.g_d().is_some() {
            if let Some(addr) = ivk.to_payment_address(diversifier) {
                *out = addr.to_bytes();
                return true;
            }
        }
        // Increment d as a little-endian 88-bit counter
        let mut carry = true;
        for byte in d.iter_mut() {
            if carry {
                let (v, c) = byte.overflowing_add(1);
                *byte = v;
                carry = c;
            } else {
                break;
            }
        }
        if carry {
            return false; // Exhausted the full 2^88 diversifier space
        }
    }
}

/// Derives the default external payment address from a 96-byte Sapling FVK.
/// Follows the FVK → IVK (External) → first-valid-raw-diversifier path,
/// consistent with `ivk_to_address_from_index`.
/// Returns false if the FVK bytes are invalid.
pub fn fvk_to_default_address(fvk: &[u8; 96], out: &mut [u8; 43]) -> bool {
    match dfvk_from_fvk(fvk) {
        Some(dfvk) => ivk_default_address(&dfvk.to_ivk(Scope::External), out),
        None => false,
    }
}

/// Derives the default internal payment address from a 96-byte Sapling FVK.
/// Follows the FVK → IVK (Internal) → first-valid-raw-diversifier path,
/// consistent with `ivk_to_address_from_index`.
/// Returns false if the FVK bytes are invalid.
pub fn fvk_to_default_address_internal(fvk: &[u8; 96], out: &mut [u8; 43]) -> bool {
    match dfvk_from_fvk(fvk) {
        Some(dfvk) => ivk_default_address(&dfvk.to_ivk(Scope::Internal), out),
        None => false,
    }
}

/// Derives the external payment address from a 96-byte Sapling FVK and explicit diversifier.
/// Follows the FVK → IVK (External) → PaymentAddress flow.
/// Returns false if the FVK bytes are invalid or the diversifier produces no valid Jubjub point.
pub fn fvk_to_address(fvk: &[u8; 96], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool {
    match dfvk_from_fvk(fvk) {
        Some(dfvk) => {
            let ivk = dfvk.to_ivk(Scope::External);
            match ivk.to_payment_address(Diversifier(*diversifier)) {
                Some(addr) => { *out = addr.to_bytes(); true }
                None => false,
            }
        }
        None => false,
    }
}

/// Derives the internal payment address from a 96-byte Sapling FVK and explicit diversifier.
/// Follows the FVK → IVK (Internal) → PaymentAddress flow.
/// Returns false if the FVK bytes are invalid or the diversifier produces no valid Jubjub point.
pub fn fvk_to_address_internal(fvk: &[u8; 96], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool {
    match dfvk_from_fvk(fvk) {
        Some(dfvk) => {
            let ivk = dfvk.to_ivk(Scope::Internal);
            match ivk.to_payment_address(Diversifier(*diversifier)) {
                Some(addr) => { *out = addr.to_bytes(); true }
                None => false,
            }
        }
        None => false,
    }
}

/// Derives the external payment address at the given diversifier index from a 96-byte Sapling FVK.
/// Follows the FVK → IVK (External) → PaymentAddress flow, treating the index bytes as a raw Diversifier.
/// Returns false if the FVK bytes are invalid or the diversifier index produces no valid Jubjub point.
pub fn fvk_to_address_from_index(fvk: &[u8; 96], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool {
    match dfvk_from_fvk(fvk) {
        Some(dfvk) => {
            let ivk = dfvk.to_ivk(Scope::External);
            let diversifier = Diversifier(*diversifier_index);
            if diversifier.g_d().is_none() {
                return false;
            }
            match ivk.to_payment_address(diversifier) {
                Some(addr) => { *out = addr.to_bytes(); true }
                None => false,
            }
        }
        None => false,
    }
}

/// Derives the internal payment address at the given diversifier index from a 96-byte Sapling FVK.
/// Follows the FVK → IVK (Internal) → PaymentAddress flow, treating the index bytes as a raw Diversifier.
/// Returns false if the FVK bytes are invalid or the diversifier index produces no valid Jubjub point.
pub fn fvk_to_address_from_index_internal(fvk: &[u8; 96], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool {
    match dfvk_from_fvk(fvk) {
        Some(dfvk) => {
            let ivk = dfvk.to_ivk(Scope::Internal);
            let diversifier = Diversifier(*diversifier_index);
            if diversifier.g_d().is_none() {
                return false;
            }
            match ivk.to_payment_address(diversifier) {
                Some(addr) => { *out = addr.to_bytes(); true }
                None => false,
            }
        }
        None => false,
    }
}

// ── Spending key component derivation ────────────────────────────────────────

/// Derives the 96-byte expanded spending key (ask, nsk, ovk) from a 32-byte Sapling spending key.
/// Layout of `out`: [ask(32) | nsk(32) | ovk(32)].
/// This derivation is always successful (pure Blake2b PRF — no canonicality constraints).
pub fn sk_to_expsk(sk: &[u8; 32], out: &mut [u8; 96]) -> bool {
    let expsk = ExpandedSpendingKey::from_spending_key(sk);
    out[..32].copy_from_slice(&expsk.ask.to_bytes());
    out[32..64].copy_from_slice(&expsk.nsk.to_bytes());
    out[64..].copy_from_slice(&expsk.ovk.0);
    true
}

/// Derives the full viewing key (ak, nk, ovk) from a 96-byte serialized expanded spending key.
/// Layout of `expsk`: [ask(32) | nsk(32) | ovk(32)].
/// Layout of `out`: [ak(32) | nk(32) | ovk(32)] — matches C++ SaplingFullViewingKey serialization.
/// Returns false if `ask` or `nsk` are not canonical field elements.
pub fn expsk_to_fvk(expsk: &[u8; 96], out: &mut [u8; 96]) -> bool {
    match ExpandedSpendingKey::from_bytes(expsk) {
        Ok(expsk_key) => {
            let fvk = FullViewingKey::from_expanded_spending_key(&expsk_key);
            out[..32].copy_from_slice(&fvk.vk.ak.to_bytes());
            out[32..64].copy_from_slice(&fvk.vk.nk.0.to_bytes());
            out[64..].copy_from_slice(&fvk.ovk.0);
            true
        }
        Err(_) => false,
    }
}

/// Derives the default external payment address from a 96-byte Sapling expanded spending key.
/// Follows the expsk → FVK → IVK (External) → first-valid-raw-diversifier path,
/// consistent with `fvk_to_default_address`.
/// Returns false if the expsk bytes are invalid.
pub fn expsk_to_default_address(expsk: &[u8; 96], out: &mut [u8; 43]) -> bool {
    match ExpandedSpendingKey::from_bytes(expsk) {
        Ok(expsk_key) => {
            let fvk = FullViewingKey::from_expanded_spending_key(&expsk_key);
            let mut fvk_bytes = [0u8; 96];
            fvk_bytes[..32].copy_from_slice(&fvk.vk.ak.to_bytes());
            fvk_bytes[32..64].copy_from_slice(&fvk.vk.nk.0.to_bytes());
            fvk_bytes[64..].copy_from_slice(&fvk.ovk.0);
            match dfvk_from_fvk(&fvk_bytes) {
                Some(dfvk) => ivk_default_address(&dfvk.to_ivk(Scope::External), out),
                None => false,
            }
        }
        Err(_) => false,
    }
}

/// Derives the default internal payment address from a 96-byte Sapling expanded spending key.
/// Follows the expsk → FVK → IVK (Internal) → first-valid-raw-diversifier path,
/// consistent with `fvk_to_default_address_internal`.
/// Returns false if the expsk bytes are invalid.
pub fn expsk_to_default_address_internal(expsk: &[u8; 96], out: &mut [u8; 43]) -> bool {
    match ExpandedSpendingKey::from_bytes(expsk) {
        Ok(expsk_key) => {
            let fvk = FullViewingKey::from_expanded_spending_key(&expsk_key);
            let mut fvk_bytes = [0u8; 96];
            fvk_bytes[..32].copy_from_slice(&fvk.vk.ak.to_bytes());
            fvk_bytes[32..64].copy_from_slice(&fvk.vk.nk.0.to_bytes());
            fvk_bytes[64..].copy_from_slice(&fvk.ovk.0);
            match dfvk_from_fvk(&fvk_bytes) {
                Some(dfvk) => ivk_default_address(&dfvk.to_ivk(Scope::Internal), out),
                None => false,
            }
        }
        Err(_) => false,
    }
}

