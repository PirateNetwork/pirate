use std::convert::TryInto;

use blake2b_simd::Params as Blake2bParams;
use zcash_primitives::keys::prf_expand;

// Personalization bytes for the zcashd-specific taddr → Sapling intermediate hash.
// There is no upstream equivalent for this step; the upstream `prf_expand` covers step 3.
const ZCASH_TADDR_OVK_PERSONAL: &[u8; 16] = b"ZcTaddrToSapling";

// ── Transparent OVK derivation ───────────────────────────────────────────────

/// Derives the 32-byte outgoing viewing key used when shielding from a
/// transparent address.
///
/// Algorithm (zcashd convention):
///   I   = BLAKE2b-512(personal="ZcTaddrToSapling", input=seed)   [zcashd-specific]
///   I_L = I[0..32]
///   ovk = PRF^expand(I_L, [0x02])[0..32]                         [upstream: zcash_primitives::keys::prf_expand]
///
/// Step 3 reuses the upstream `prf_expand` from `zcash_primitives::keys`
/// (`PRF^expand(sk, t) = BLAKE2b-512("Zcash_ExpandSeed", sk || t)`), which is the
/// same operation used to derive the OVK from a Sapling spending key.
///
/// The `seed` slice is the raw HD seed bytes (typically 32 bytes).
pub fn ovk_for_shielding_from_taddr(seed: &[u8], out: &mut [u8; 32]) -> bool {
    // Step 1: I = BLAKE2b-512("ZcTaddrToSapling", seed)
    let intermediate = Blake2bParams::new()
        .hash_length(64)
        .personal(ZCASH_TADDR_OVK_PERSONAL)
        .hash(seed);

    // Step 2: I_L = I[0..32]
    let i_l: [u8; 32] = intermediate.as_bytes()[..32].try_into().unwrap();

    // Step 3: ovk = PRF^expand(I_L, [0x02])[0..32]
    out.copy_from_slice(&prf_expand(&i_l, &[0x02]).as_bytes()[..32]);
    true
}
