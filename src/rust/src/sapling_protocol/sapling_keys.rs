use group::GroupEncoding;
use sapling_crypto::{
    keys::{ExpandedSpendingKey, FullViewingKey},
    zip32::{DiversifiableFullViewingKey, ExtendedSpendingKey},
    Diversifier, SaplingIvk,
};
use zip32::Scope;

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

/// Derives the default internal (change) payment address from a 128-byte Sapling DFVK
/// encoding: [fvk(96) || dk(32)].
///
/// This is the correct ZIP 32 change address derivation. It calls
/// `DiversifiableFullViewingKey::change_address()` which:
///   1. Derives `dk_internal` via `sapling_derive_internal_fvk(fvk, dk)` — a PRF over the
///      external dk — producing a completely different DiversifierKey than the external dk.
///   2. Scans from diversifier index 0 using `dk_internal` → produces diversifier bytes
///      that are completely different from the external default address diversifier.
///
/// Result: the change address looks visually distinct from the Normal address, matching
/// the same behavior as Orchard (which also derives a separate internal dk).
/// Returns false if the DFVK bytes are invalid.
pub fn dfvk_to_change_address(dfvk: &[u8; 128], out: &mut [u8; 43]) -> bool {
    match DiversifiableFullViewingKey::from_bytes(dfvk) {
        Some(dfvk) => {
            let (_, addr) = dfvk.change_address();
            *out = addr.to_bytes();
            true
        }
        None => false,
    }
}

/// Derives the internal (change scope) incoming viewing key from a 128-byte Sapling DFVK
/// encoding: [fvk(96) || dk(32)].
///
/// IMPORTANT: `sapling_derive_internal_fvk` hashes `fvk || dk` together to produce
/// `nk_internal`. Using a zeroed dk (as the 96-byte-only path does) produces a completely
/// different `nk_internal` and therefore a different IVK — meaning the wallet would fail
/// to decrypt change notes. The full 128-byte DFVK with the real `dk` must be used here.
///
/// Returns false if the DFVK bytes are invalid.
pub fn dfvk_to_ivk_internal(dfvk: &[u8; 128], out: &mut [u8; 32]) -> bool {
    match DiversifiableFullViewingKey::from_bytes(dfvk) {
        Some(dfvk) => {
            *out = dfvk.to_ivk(Scope::Internal).to_repr();
            true
        }
        None => false,
    }
}

/// Derives the internal (change scope) nullifier deriving key from a 128-byte Sapling DFVK
/// encoding: [fvk(96) || dk(32)].
///
/// `nk_internal = H*(i_nsk) + nk` where `i_nsk` is derived from dk via
/// `sapling_derive_internal_fvk`. This key is required to compute the correct nullifier
/// for notes encrypted to the internal IVK. Using the external `nk` would produce the
/// wrong nullifier and the note would never appear spent.
/// Returns false if the DFVK bytes are invalid.
pub fn dfvk_to_nk_internal(dfvk: &[u8; 128], out: &mut [u8; 32]) -> bool {
    match DiversifiableFullViewingKey::from_bytes(dfvk) {
        Some(dfvk) => {
            *out = dfvk.to_nk(Scope::Internal).0.to_bytes();
            true
        }
        None => false,
    }
}

/// Derives the internal (change scope) outgoing viewing key from a 128-byte Sapling DFVK
/// encoding: [fvk(96) || dk(32)].
///
/// The internal OVK is derived by `sapling_derive_internal_fvk(fvk, dk)` as `r[32..]`
/// (second 32 bytes of `PRF_expand(i, [0x18])`), which is completely different from the
/// external OVK. Using a zeroed dk would produce the wrong internal OVK.
/// Returns false if the DFVK bytes are invalid.
pub fn dfvk_to_ovk_internal(dfvk: &[u8; 128], out: &mut [u8; 32]) -> bool {
    match DiversifiableFullViewingKey::from_bytes(dfvk) {
        Some(dfvk) => {
            *out = dfvk.to_ovk(Scope::Internal).0;
            true
        }
        None => false,
    }
}

/// Derives the internal payment address from a 128-byte Sapling DFVK [fvk(96)||dk(32)]
/// and an explicit 11-byte diversifier.
///
/// Uses the real dk to compute `nk_internal` correctly via `sapling_derive_internal_fvk`.
/// Returns false if the DFVK bytes are invalid or the diversifier produces no valid Jubjub point.
pub fn dfvk_to_address_internal(dfvk: &[u8; 128], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool {
    match DiversifiableFullViewingKey::from_bytes(dfvk) {
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

/// Derives the internal payment address at a given diversifier index from a 128-byte Sapling
/// DFVK [fvk(96)||dk(32)], treating the index bytes directly as a raw Diversifier.
///
/// Uses the real dk to compute `nk_internal` correctly via `sapling_derive_internal_fvk`.
/// Returns false if the DFVK bytes are invalid or the index produces no valid Jubjub point.
pub fn dfvk_to_address_from_index_internal(dfvk: &[u8; 128], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool {
    match DiversifiableFullViewingKey::from_bytes(dfvk) {
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

// ── Extended spending key derivation ─────────────────────────────────────────

/// Derives the internal (change) ExtendedSpendingKey from the external one.
///
/// The internal key has `nk_internal = H*(i_nsk) + nk` where `i_nsk` is a
/// tweak derived via ZIP 32. Spending an internal (change) note in a zk-SNARK
/// proof requires `nsk_internal`, not `nsk`. The resulting 169-byte XSK can
/// be serialized and passed to `SaplingBuilder::add_spend` for internal notes.
/// Always succeeds for a valid 169-byte XSK.
pub fn xsk_derive_internal(xsk: &[u8; 169]) -> [u8; 169] {
    let xsk_ext = ExtendedSpendingKey::read(&xsk[..])
        .expect("valid 169-byte Sapling ExtendedSpendingKey");
    let xsk_int = xsk_ext.derive_internal();
    let mut out = [0u8; 169];
    xsk_int
        .write(&mut out[..])
        .expect("should be able to serialize an ExtendedSpendingKey");
    out
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

