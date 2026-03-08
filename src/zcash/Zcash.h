#ifndef ZC_ZCASH_H_
#define ZC_ZCASH_H_

#include <array>
#include <cstddef>
#include <cstdint>

#define ZC_NUM_JS_INPUTS 2
#define ZC_NUM_JS_OUTPUTS 2
#define INCREMENTAL_MERKLE_TREE_DEPTH 29
#define INCREMENTAL_MERKLE_TREE_DEPTH_TESTING 4

#define SAPLING_INCREMENTAL_MERKLE_TREE_DEPTH 32

#define NOTEENCRYPTION_AUTH_BYTES 16

#define ZC_NOTEPLAINTEXT_LEADING 1
#define ZC_V_SIZE 8
#define ZC_RHO_SIZE 32
#define ZC_R_SIZE 32
#define ZC_MEMO_SIZE 512
#define ZC_DIVERSIFIER_SIZE 11
#define ZC_JUBJUB_POINT_SIZE 32
#define ZC_JUBJUB_SCALAR_SIZE 32

#define ZC_NOTEPLAINTEXT_SIZE (ZC_NOTEPLAINTEXT_LEADING + ZC_V_SIZE + ZC_RHO_SIZE + ZC_R_SIZE + ZC_MEMO_SIZE)

#define ZC_SAPLING_ENCPLAINTEXT_SIZE (ZC_NOTEPLAINTEXT_LEADING + ZC_DIVERSIFIER_SIZE + ZC_V_SIZE + ZC_R_SIZE + ZC_MEMO_SIZE)
#define ZC_SAPLING_OUTPLAINTEXT_SIZE (ZC_JUBJUB_POINT_SIZE + ZC_JUBJUB_SCALAR_SIZE)

#define ZC_SAPLING_ENCCIPHERTEXT_SIZE (ZC_SAPLING_ENCPLAINTEXT_SIZE + NOTEENCRYPTION_AUTH_BYTES)
#define ZC_SAPLING_OUTCIPHERTEXT_SIZE (ZC_SAPLING_OUTPLAINTEXT_SIZE + NOTEENCRYPTION_AUTH_BYTES)

namespace libzcash {
    // Orchard serialized sizes
    const size_t SerializedOrchardPaymentAddressSize                    = 43;
    const size_t SerializedOrchardOutgoingKeySize                       = 32;
    const size_t SerializedOrchardIncomingViewingKeySize                = 64;
    const size_t SerializedOrchardFullViewingKeySize                    = 96;
    const size_t SerializedOrchardExtendedFullViewingKeySize            = 137;
    const size_t SerializedOrchardDiversifiedExtendedFullViewingKeySize = 148;
    const size_t SerializedOrchardSpendingKeySize                       = 32;
    const size_t SerializedOrchardExtendedSpendingKeySize               = 73;
    const size_t SerializedOrchardDiversifiedExtendedSpendingKeySize    = 84;

    // Sapling serialized sizes
    const size_t SerializedSaplingPaymentAddressSize      = 43;
    const size_t SerializedSaplingFullViewingKeySize      = 96;
    const size_t SerializedSaplingExpandedSpendingKeySize = 96;
    const size_t SerializedSaplingSpendingKeySize         = 32;

    // Sprout serialized sizes
    const size_t SerializedSproutPaymentAddressSize = 64;
    const size_t SerializedSproutViewingKeySize     = 64;
    const size_t SerializedSproutSpendingKeySize    = 32;

    // Orchard FFI transfer types
    typedef std::array<unsigned char, SerializedOrchardPaymentAddressSize>     OrchardPaymentAddress_t;
    typedef std::array<unsigned char, SerializedOrchardIncomingViewingKeySize> OrchardIncomingViewingKey_t;
    typedef std::array<unsigned char, SerializedOrchardFullViewingKeySize>     OrchardFullViewingKey_t;
    typedef std::array<unsigned char, SerializedOrchardSpendingKeySize>        OrchardSpendingKey_t;
    typedef std::array<unsigned char, SerializedOrchardExtendedSpendingKeySize> OrchardExtendedSpendingKey_t;
    typedef std::array<unsigned char, ZC_DIVERSIFIER_SIZE>                     diversifier_t;

    /// 88-bit (11-byte) array — used for Orchard/Sapling diversifiers (ZC_DIVERSIFIER_SIZE)
    typedef std::array<uint8_t, 11>  uint88_t;

    /// 4096-bit (512-byte) array — used for note memo fields (ZC_MEMO_SIZE)
    typedef std::array<uint8_t, 512> uint4096_t;
} // namespace libzcash

#endif // ZC_ZCASH_H_
