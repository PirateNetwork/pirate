// Copyright (c) 2018 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZCASH_ZIP32_H
#define ZCASH_ZIP32_H

#include "serialize.h"
#include "support/allocators/secure.h"
#include "uint256.h"
#include "zcash/address/sapling.hpp"

#include <optional>

const uint32_t HARDENED_KEY_LIMIT = 0x80000000;
const size_t ZIP32_XFVK_SIZE = 169;
const size_t ZIP32_XSK_SIZE = 169;
const size_t ZIP32_DXFVK_SIZE = 180;
const size_t ZIP32_DXSK_SIZE = 180;

typedef std::vector<unsigned char, secure_allocator<unsigned char>> RawHDSeed;

class HDSeed {
private:
    RawHDSeed seed;

public:
    HDSeed() {}
    HDSeed(RawHDSeed& seedIn) : seed(seedIn) {}

    static HDSeed Random(size_t len = 32);
    static HDSeed RestoreFromPhrase(std::string &phrase);
    bool IsValidPhrase(std::string &phrase);
    bool IsNull() const { return seed.empty(); };
    void GetPhrase(std::string &phrase);
    uint256 Fingerprint() const;
    uint256 EncryptionFingerprint() const;
    RawHDSeed RawSeed() const { return seed; }

    friend bool operator==(const HDSeed& a, const HDSeed& b)
    {
        return a.seed == b.seed;
    }

    friend bool operator!=(const HDSeed& a, const HDSeed& b)
    {
        return !(a == b);
    }
};

// This is not part of ZIP 32, but is here because it's linked to the HD seed.
uint256 ovkForShieldingFromTaddr(HDSeed& seed);

namespace libzcash {

typedef uint32_t AccountId;

/**
 * The account identifier used for HD derivation of
 * transparent and Sapling addresses via the legacy
 * `getnewaddress` and `z_getnewaddress` code paths,
 */
const AccountId ZCASH_LEGACY_ACCOUNT = HARDENED_KEY_LIMIT - 1;

/**
 * 88-bit diversifier index. This would ideally derive from base_uint
 * but those values must have bit widths that are multiples of 32.
 */
class diversifier_index_t : public base_blob<88> {
public:
    diversifier_index_t() {}
    diversifier_index_t(const base_blob<88>& b) : base_blob<88>(b) {}
    diversifier_index_t(uint64_t i): base_blob<88>() {
        data[0] = i & 0xFF;
        data[1] = (i >> 8) & 0xFF;
        data[2] = (i >> 16) & 0xFF;
        data[3] = (i >> 24) & 0xFF;
        data[4] = (i >> 32) & 0xFF;
        data[5] = (i >> 40) & 0xFF;
        data[6] = (i >> 48) & 0xFF;
        data[7] = (i >> 56) & 0xFF;
    }
    explicit diversifier_index_t(const std::vector<unsigned char>& vch) : base_blob<88>(vch) {}

    static diversifier_index_t FromRawBytes(std::array<uint8_t, 11> bytes)
    {
        diversifier_index_t buf;
        std::memcpy(buf.begin(), bytes.data(), 11);
        return buf;
    }

    bool increment() {
        for (int i = 0; i < 11; i++) {
            this->data[i] += 1;
            if (this->data[i] != 0) {
                return true; // no overflow
            }
        }

        return false; //overflow
    }

    std::optional<diversifier_index_t> succ() const {
        diversifier_index_t next(*this);
        if (next.increment()) {
            return next;
        } else {
            return std::nullopt;
        }
    }

    std::optional<uint32_t> ToTransparentChildIndex() const;

    friend bool operator<(const diversifier_index_t& a, const diversifier_index_t& b) {
        for (int i = 10; i >= 0; i--) {
            if (a.data[i] == b.data[i]) {
                continue;
            } else {
                return a.data[i] < b.data[i];
            }
        }

        return false;
    }
};

struct SaplingExtendedFullViewingKey {
    uint8_t depth;
    uint32_t parentFVKTag;
    uint32_t childIndex;
    uint256 chaincode;
    libzcash::SaplingFullViewingKey fvk;
    uint256 dk;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(depth);
        READWRITE(parentFVKTag);
        READWRITE(childIndex);
        READWRITE(chaincode);
        READWRITE(fvk);
        READWRITE(dk);
    }

    std::optional<SaplingExtendedFullViewingKey> Derive(uint32_t i) const;

    // Returns the first index starting from j that generates a valid
    // payment address, along with the corresponding address. Returns
    // an error if the diversifier space is exhausted.
    std::optional<std::pair<diversifier_index_t, libzcash::SaplingPaymentAddress>>
        Address(diversifier_index_t j) const;

    libzcash::SaplingPaymentAddress DefaultAddress() const;

    friend inline bool operator==(const SaplingExtendedFullViewingKey& a, const SaplingExtendedFullViewingKey& b) {
        return (
            a.depth == b.depth &&
            a.parentFVKTag == b.parentFVKTag &&
            a.childIndex == b.childIndex &&
            a.chaincode == b.chaincode &&
            a.fvk == b.fvk &&
            a.dk == b.dk);
    }
    friend inline bool operator<(const SaplingExtendedFullViewingKey& a, const SaplingExtendedFullViewingKey& b) {
        return (a.depth < b.depth ||
            (a.depth == b.depth && a.childIndex < b.childIndex) ||
            (a.depth == b.depth && a.childIndex == b.childIndex && a.fvk < b.fvk));
    }
};

struct SaplingDiversifiedExtendedFullViewingKey {
    SaplingExtendedFullViewingKey extfvk;
    diversifier_t d;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(extfvk);
        READWRITE(d);
    }
};

struct SaplingExtendedSpendingKey {
    uint8_t depth;
    uint32_t parentFVKTag;
    uint32_t childIndex;
    uint256 chaincode;
    libzcash::SaplingExpandedSpendingKey expsk;
    uint256 dk;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(depth);
        READWRITE(parentFVKTag);
        READWRITE(childIndex);
        READWRITE(chaincode);
        READWRITE(expsk);
        READWRITE(dk);
    }

    static SaplingExtendedSpendingKey Master(const HDSeed& seed, bool bip39Enabled = true);

    SaplingExtendedSpendingKey Derive(uint32_t i) const;

    SaplingExtendedFullViewingKey ToXFVK() const;

    libzcash::SaplingPaymentAddress DefaultAddress() const;

    friend bool operator==(const SaplingExtendedSpendingKey& a, const SaplingExtendedSpendingKey& b)
    {
        return a.depth == b.depth &&
            a.parentFVKTag == b.parentFVKTag &&
            a.childIndex == b.childIndex &&
            a.chaincode == b.chaincode &&
            a.expsk == b.expsk &&
            a.dk == b.dk;
    }

    friend inline bool operator<(const SaplingExtendedSpendingKey& a, const SaplingExtendedSpendingKey& b) {
        return (a.depth < b.depth ||
            (a.depth == b.depth && a.childIndex < b.childIndex) ||
            (a.depth == b.depth && a.childIndex == b.childIndex && a.expsk < b.expsk));
    }
};

struct SaplingDiversifiedExtendedSpendingKey {
    SaplingExtendedSpendingKey extsk;
    diversifier_t d;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(extsk);
        READWRITE(d);
    }
};

}

#endif // ZCASH_ZIP32_H
