#ifndef ZC_ZCASH_H_
#define ZC_ZCASH_H_

#include <array>
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// JoinSplit (Sprout) circuit dimensions
// ---------------------------------------------------------------------------

//! Number of input notes consumed by a single JoinSplit proof.
#define ZC_NUM_JS_INPUTS  2
//! Number of output notes produced by a single JoinSplit proof.
#define ZC_NUM_JS_OUTPUTS 2

// ---------------------------------------------------------------------------
// Incremental Merkle tree depths
// ---------------------------------------------------------------------------

//! Depth of the Sprout incremental Merkle tree used in production.
#define INCREMENTAL_MERKLE_TREE_DEPTH         29
//! Reduced depth used in unit tests to keep witness generation fast.
#define INCREMENTAL_MERKLE_TREE_DEPTH_TESTING  4
//! Depth of the Sapling incremental Merkle tree (2^32 leaves).
#define SAPLING_INCREMENTAL_MERKLE_TREE_DEPTH 32

// ---------------------------------------------------------------------------
// Note encryption / AEAD tag size
// ---------------------------------------------------------------------------

//! Size in bytes of the Poly1305 authentication tag appended to every
//! encrypted note ciphertext (both enc and out ciphertexts).
#define NOTEENCRYPTION_AUTH_BYTES 16

// ---------------------------------------------------------------------------
// Diversifier size (shared by Sapling and Orchard)
// ---------------------------------------------------------------------------

//! Diversifier size in bytes — selects a distinct address from the same IVK.
#define ZC_DIVERSIFIER_SIZE 11

// ---------------------------------------------------------------------------
// Sprout note plaintext field sizes and total plaintext size
//
// Layout: [ leading(1) | v(8) | rho(32) | r(32) | memo(512) ]  = 585 bytes
// ---------------------------------------------------------------------------

//! Leading byte of the Sprout note plaintext (version/type tag).
#define ZC_NOTEPLAINTEXT_LEADING 1
//! Value field size in bytes (uint64_t, little-endian).
#define ZC_V_SIZE   8
//! Note commitment randomness (rho) size in bytes.
#define ZC_RHO_SIZE 32
//! Note randomness (r) / nullifier key size in bytes.
#define ZC_R_SIZE   32
//! Memo field size in bytes.
#define ZC_MEMO_SIZE 512

//! Total size of a Sprout note plaintext before encryption.
#define ZC_NOTEPLAINTEXT_SIZE \
    (ZC_NOTEPLAINTEXT_LEADING + ZC_V_SIZE + ZC_RHO_SIZE + ZC_R_SIZE + ZC_MEMO_SIZE)

// ---------------------------------------------------------------------------
// libzcash namespace: serialized sizes and FFI transfer typedefs
// ---------------------------------------------------------------------------

namespace libzcash {

// -------------------------------------------------------------------
// Sprout — serialized byte sizes
// -------------------------------------------------------------------

//! Serialized Sprout payment address: a_pk (32) || pk_enc (32).
const size_t SerializedSproutPaymentAddressSize = 64;
//! Serialized Sprout full viewing key (a_pk || sk_enc).
const size_t SerializedSproutViewingKeySize     = 64;
//! Serialized Sprout spending key (a_sk scalar, 32 bytes).
const size_t SerializedSproutSpendingKeySize    = 32;

// -------------------------------------------------------------------
// Sapling — serialized byte sizes
// -------------------------------------------------------------------

//! Serialized Sapling payment address: d (11) || pk_d (32).
const size_t SerializedSaplingPaymentAddressSize      = 43;
//! Serialized Sapling incoming viewing key (ivk scalar, 32 bytes).
const size_t SerializedSaplingIncomingViewingKeySize  = 32;
//! Serialized Sapling outgoing viewing key (ovk scalar, 32 bytes).
const size_t SerializedSaplingOutgoingViewingKeySize  = 32;
//! Serialized Sapling full viewing key: ak (32) || nk (32) || ovk (32).
const size_t SerializedSaplingFullViewingKeySize      = 96;
//! Serialized Sapling expanded spending key: ask (32) || nsk (32) || ovk (32).
const size_t SerializedSaplingExpandedSpendingKeySize = 96;
//! Serialized Sapling spending key (raw scalar, 32 bytes).
const size_t SerializedSaplingSpendingKeySize         = 32;
//! Serialized Sapling diversifiable full viewing key: fvk (96) || dk (32).
//! The dk is required for ZIP 32 internal address derivation (change address).
const size_t SerializedSaplingDiversifiableFullViewingKeySize = 128;
//! Serialized Sapling extended spending key (XSK = expsk + chain code + depth/index metadata + dk).
//! Layout matches SAPLING_ZIP32_XSK_SIZE (169 bytes) in zip32.h.
const size_t SerializedSaplingExtendedSpendingKeySize = 169;

// -------------------------------------------------------------------
// Orchard — serialized byte sizes
// -------------------------------------------------------------------

//! Serialized Orchard payment address: d (11) || pk_d (32).
const size_t SerializedOrchardPaymentAddressSize                    = 43;
//! Serialized Orchard outgoing viewing key (ovk scalar, 32 bytes).
const size_t SerializedOrchardOutgoingKeySize                       = 32;
//! Serialized Orchard incoming viewing key: dk (32) || ivk (32).
const size_t SerializedOrchardIncomingViewingKeySize                = 64;
//! Serialized Orchard full viewing key: ak (32) || nk (32) || rivk (32).
const size_t SerializedOrchardFullViewingKeySize                    = 96;
//! Serialized Orchard extended full viewing key (FVK + chain code + depth/index metadata).
const size_t SerializedOrchardExtendedFullViewingKeySize            = 137;
//! Serialized Orchard diversified extended full viewing key (XFVK + cached diversifier).
const size_t SerializedOrchardDiversifiedExtendedFullViewingKeySize = 148;
//! Serialized Orchard spending key (raw scalar, 32 bytes).
const size_t SerializedOrchardSpendingKeySize                       = 32;
//! Serialized Orchard extended spending key (XSK = sk + chain code + depth/index metadata).
const size_t SerializedOrchardExtendedSpendingKeySize               = 73;
//! Serialized Orchard diversified extended spending key (XXSK + cached diversifier).
const size_t SerializedOrchardDiversifiedExtendedSpendingKeySize    = 84;

// -------------------------------------------------------------------
// Shared primitive typedefs
// -------------------------------------------------------------------

//! 11-byte diversifier — selects a distinct address from the same IVK (Sapling & Orchard).
typedef std::array<unsigned char, ZC_DIVERSIFIER_SIZE> diversifier_t;

//! 88-bit (11-byte) alias for diversifier_t, exposed as a uint type.
typedef std::array<uint8_t, 11>  uint88_t;

//! 512-byte (4096-bit) memo field buffer.
typedef std::array<uint8_t, 512> uint4096_t;

// -------------------------------------------------------------------
// Sprout FFI transfer types
// (plain byte arrays used as opaque buffers across the C++/Rust boundary)
// -------------------------------------------------------------------

// (No Sprout FFI transfer types at present — Sprout keys are handled natively in C++.)

// -------------------------------------------------------------------
// Sapling FFI transfer types
// -------------------------------------------------------------------

//! Opaque buffer for transferring a Sapling payment address across the FFI boundary.
typedef std::array<unsigned char, SerializedSaplingPaymentAddressSize>      SaplingPaymentAddress_FFI_t;
//! Opaque buffer for a Sapling incoming viewing key.
typedef std::array<unsigned char, SerializedSaplingIncomingViewingKeySize>  SaplingIncomingViewingKey_FFI_t;
//! Opaque buffer for a Sapling outgoing viewing key.
typedef std::array<unsigned char, SerializedSaplingOutgoingViewingKeySize>  SaplingOutgoingViewingKey_FFI_t;
//! Opaque buffer for a Sapling full viewing key (ak || nk || ovk).
typedef std::array<unsigned char, SerializedSaplingFullViewingKeySize>      SaplingFullViewingKey_FFI_t;
//! Opaque buffer for a Sapling expanded spending key (ask || nsk || ovk).
typedef std::array<unsigned char, SerializedSaplingExpandedSpendingKeySize> SaplingExpandedSpendingKey_FFI_t;
//! Opaque buffer for a Sapling spending key (raw scalar).
typedef std::array<unsigned char, SerializedSaplingSpendingKeySize>         SaplingSpendingKey_FFI_t;
//! Opaque buffer for a Sapling diversifiable full viewing key (fvk[96] || dk[32] = 128 bytes).
//! Used to pass the full DFVK across the FFI boundary for operations that require the real dk,
//! such as deriving the internal change address via ZIP 32 sapling_derive_internal_fvk.
typedef std::array<unsigned char, SerializedSaplingDiversifiableFullViewingKeySize> SaplingDiversifiableFullViewingKey_FFI_t;
//! Opaque buffer for a Sapling extended spending key (169 bytes) across the FFI boundary.
//! Used to derive the internal (change) extended spending key via ZIP 32.
typedef std::array<unsigned char, SerializedSaplingExtendedSpendingKeySize> SaplingExtendedSpendingKey_FFI_t;

// -------------------------------------------------------------------
// Orchard FFI transfer types
// -------------------------------------------------------------------

//! Opaque buffer for transferring an Orchard payment address across the FFI boundary.
typedef std::array<unsigned char, SerializedOrchardPaymentAddressSize>      OrchardPaymentAddress_FFI_t;
//! Opaque buffer for an Orchard incoming viewing key (dk || ivk).
typedef std::array<unsigned char, SerializedOrchardIncomingViewingKeySize>  OrchardIncomingViewingKey_FFI_t;
//! Opaque buffer for an Orchard outgoing viewing key.
typedef std::array<unsigned char, SerializedOrchardOutgoingKeySize>         OrchardOutgoingViewingKey_FFI_t;
//! Opaque buffer for an Orchard full viewing key (ak || nk || rivk).
typedef std::array<unsigned char, SerializedOrchardFullViewingKeySize>      OrchardFullViewingKey_FFI_t;
//! Opaque buffer for an Orchard spending key (raw scalar).
typedef std::array<unsigned char, SerializedOrchardSpendingKeySize>         OrchardSpendingKey_FFI_t;
//! Opaque buffer for an Orchard extended spending key.
typedef std::array<unsigned char, SerializedOrchardExtendedSpendingKeySize> OrchardExtendedSpendingKey_FFI_t;

} // namespace libzcash

#endif // ZC_ZCASH_H_
