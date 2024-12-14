// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#ifndef BITCOIN_PRIMITIVES_TRANSACTION_H
#define BITCOIN_PRIMITIVES_TRANSACTION_H

#include "amount.h"
#include "arith_uint256.h"
#include "consensus/consensus.h"
#include "hash.h"
#include "random.h"
#include "script/script.h"
#include "serialize.h"
#include "streams.h"
#include "uint256.h"
#include "util.h"

#ifndef __APPLE__
#include <stdint.h>
#endif

#include <array>
#include <variant>

#include "zcash/JoinSplit.hpp"
#include "zcash/NoteEncryption.hpp"
#include "zcash/Proof.hpp"
#include "zcash/Zcash.h"

#include <primitives/sapling.h>
#include <primitives/orchard.h>

/**
 * A flag that is ORed into the protocol version to designate that a transaction
 * should be (un)serialized without witness data.
 * Make sure that this does not collide with any of the values in `version.h`
 * or with `ADDRV2_FORMAT`.
 */
static const int SERIALIZE_TRANSACTION_NO_WITNESS = 0x40000000;

extern uint32_t ASSETCHAINS_MAGIC;
extern std::string ASSETCHAINS_SELFIMPORT;

// Overwinter transaction version
static const int32_t OVERWINTER_TX_VERSION = 3;
static_assert(OVERWINTER_TX_VERSION >= OVERWINTER_MIN_TX_VERSION,"Overwinter tx version must not be lower than minimum");
static_assert(OVERWINTER_TX_VERSION <= OVERWINTER_MAX_TX_VERSION,"Overwinter tx version must not be higher than maximum");

// Overwinter version group id
static constexpr uint32_t OVERWINTER_VERSION_GROUP_ID = 0x03C48270;
static_assert(OVERWINTER_VERSION_GROUP_ID != 0, "version group id must be non-zero as specified in ZIP 202");

// Sapling transaction version
static const int32_t SAPLING_TX_VERSION = 4;
static_assert(SAPLING_TX_VERSION >= SAPLING_MIN_TX_VERSION,"Sapling tx version must not be lower than minimum");
static_assert(SAPLING_TX_VERSION <= SAPLING_MAX_TX_VERSION,"Sapling tx version must not be higher than maximum");

// Sapling version group id
static constexpr uint32_t SAPLING_VERSION_GROUP_ID = 0x892F2085;
static_assert(SAPLING_VERSION_GROUP_ID != 0, "version group id must be non-zero as specified in ZIP 202");

// Orchard transaction version
static const int32_t ORCHARD_TX_VERSION = 5;
static_assert(ORCHARD_TX_VERSION >= ORCHARD_MIN_TX_VERSION,"Orchard tx version must not be lower than minimum");
static_assert(ORCHARD_TX_VERSION <= ORCHARD_MAX_TX_VERSION,"Orchard tx version must not be higher than maximum");

// Orchard transaction version group id
// (defined in section 7.1 of the protocol spec)
static constexpr uint32_t ORCHARD_VERSION_GROUP_ID = 0x26A7270A;
static_assert(ORCHARD_VERSION_GROUP_ID != 0, "version group id must be non-zero as specified in ZIP 202");

// Future transaction version. This value must only be used
// in integration-testing contexts.
static const int32_t ZFUTURE_TX_VERSION = 0x0000FFFF;

// Future transaction version group id
static constexpr uint32_t ZFUTURE_VERSION_GROUP_ID = 0xFFFFFFFF;
static_assert(ZFUTURE_VERSION_GROUP_ID != 0, "version group id must be non-zero as specified in ZIP 202");

struct TxVersionInfo {
    bool fOverwintered;
    uint32_t nVersionGroupId;
    int32_t nVersion;
};

/**
 * Returns the current transaction version and version group id,
 * based upon the specified activation height and active features.
 */
TxVersionInfo CurrentTxVersionInfo(const Consensus::Params& consensus, int nHeight);

struct TxParams {
    unsigned int expiryDelta;
};

template <typename Stream>
class SproutProofSerializer
{
    Stream& s;
    bool useGroth;

public:
    SproutProofSerializer(Stream& s, bool useGroth) : s(s), useGroth(useGroth) {}

    void operator()(const libzcash::PHGRProof& proof) const
    {
        if (useGroth) {
            throw std::ios_base::failure("Invalid Sprout proof for transaction format (expected GrothProof, found PHGRProof)");
        }
        ::Serialize(s, proof);
    }

    void operator()(const libzcash::GrothProof& proof) const
    {
        if (!useGroth) {
            throw std::ios_base::failure("Invalid Sprout proof for transaction format (expected PHGRProof, found GrothProof)");
        }
        ::Serialize(s, proof);
    }
};

template <typename Stream, typename T>
inline void SerReadWriteSproutProof(Stream& s, const T& proof, bool useGroth, CSerActionSerialize ser_action)
{
    auto ps = SproutProofSerializer<Stream>(s, useGroth);
    std::visit(ps, proof);
}

template <typename Stream, typename T>
inline void SerReadWriteSproutProof(Stream& s, T& proof, bool useGroth, CSerActionUnserialize ser_action)
{
    if (useGroth) {
        libzcash::GrothProof grothProof;
        ::Unserialize(s, grothProof);
        proof = grothProof;
    } else {
        libzcash::PHGRProof pghrProof;
        ::Unserialize(s, pghrProof);
        proof = pghrProof;
    }
}

class JSDescription
{
public:
    // These values 'enter from' and 'exit to' the value
    // pool, respectively.
    CAmount vpub_old{0};
    CAmount vpub_new{0};

    // JoinSplits are always anchored to a root in the note
    // commitment tree at some point in the blockchain
    // history or in the history of the current
    // transaction.
    uint256 anchor;

    // Nullifiers are used to prevent double-spends. They
    // are derived from the secrets placed in the note
    // and the secret spend-authority key known by the
    // spender.
    std::array<uint256, ZC_NUM_JS_INPUTS> nullifiers;

    // Note commitments are introduced into the commitment
    // tree, blinding the public about the values and
    // destinations involved in the JoinSplit. The presence of
    // a commitment in the note commitment tree is required
    // to spend it.
    std::array<uint256, ZC_NUM_JS_OUTPUTS> commitments;

    // Ephemeral key
    uint256 ephemeralKey;

    // Ciphertexts
    // These contain trapdoors, values and other information
    // that the recipient needs, including a memo field. It
    // is encrypted using the scheme implemented in crypto/NoteEncryption.cpp
    std::array<ZCNoteEncryption::Ciphertext, ZC_NUM_JS_OUTPUTS> ciphertexts = {{{{0}}}};

    // Random seed
    uint256 randomSeed;

    // MACs
    // The verification of the JoinSplit requires these MACs
    // to be provided as an input.
    std::array<uint256, ZC_NUM_JS_INPUTS> macs;

    // JoinSplit proof
    // This is a zk-SNARK which ensures that this JoinSplit is valid.
    libzcash::SproutProof proof;

    JSDescription() : vpub_old(0), vpub_new(0) {}

    // Returns the calculated h_sig
    uint256 h_sig(ZCJoinSplit& params, const uint256& joinSplitPubKey) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        // nVersion is set by CTransaction and CMutableTransaction to
        // (tx.fOverwintered << 31) | tx.nVersion
        bool fOverwintered = s.GetVersion() >> 31;
        int32_t txVersion = s.GetVersion() & 0x7FFFFFFF;
        bool useGroth = fOverwintered && txVersion >= SAPLING_TX_VERSION;

        READWRITE(vpub_old);
        READWRITE(vpub_new);
        READWRITE(anchor);
        READWRITE(nullifiers);
        READWRITE(commitments);
        READWRITE(ephemeralKey);
        READWRITE(randomSeed);
        READWRITE(macs);
        ::SerReadWriteSproutProof(s, proof, useGroth, ser_action);
        READWRITE(ciphertexts);
    }

    friend bool operator==(const JSDescription& a, const JSDescription& b)
    {
        return (
            a.vpub_old == b.vpub_old &&
            a.vpub_new == b.vpub_new &&
            a.anchor == b.anchor &&
            a.nullifiers == b.nullifiers &&
            a.commitments == b.commitments &&
            a.ephemeralKey == b.ephemeralKey &&
            a.ciphertexts == b.ciphertexts &&
            a.randomSeed == b.randomSeed &&
            a.macs == b.macs &&
            a.proof == b.proof);
    }

    friend bool operator!=(const JSDescription& a, const JSDescription& b)
    {
        return !(a == b);
    }
};

class ProofVerifier
{
private:
    bool perform_verification;

    ProofVerifier(bool perform_verification) : perform_verification(perform_verification) {}

public:
    // ProofVerifier should never be copied
    ProofVerifier(const ProofVerifier&) = delete;
    ProofVerifier& operator=(const ProofVerifier&) = delete;
    ProofVerifier(ProofVerifier&&);
    ProofVerifier& operator=(ProofVerifier&&);

    // Creates a verification context that strictly verifies
    // all proofs.
    static ProofVerifier Strict();

    // Creates a verification context that performs no
    // verification, used when avoiding duplicate effort
    // such as during reindexing.
    static ProofVerifier Disabled();

    // Verifies that the JoinSplit proof is correct.
    bool VerifySprout(
        const JSDescription& jsdesc,
        const uint256& joinSplitPubKey);
};

class BaseOutPoint
{
public:
    uint256 hash;
    uint32_t n;

    BaseOutPoint() { SetNull(); }
    BaseOutPoint(uint256 hashIn, uint32_t nIn)
    {
        hash = hashIn;
        n = nIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(hash);
        READWRITE(n);
    }

    void SetNull()
    {
        hash.SetNull();
        n = (uint32_t)-1;
    }
    bool IsNull() const { return (hash.IsNull() && n == (uint32_t)-1); }

    friend bool operator<(const BaseOutPoint& a, const BaseOutPoint& b)
    {
        return (a.hash < b.hash || (a.hash == b.hash && a.n < b.n));
    }

    friend bool operator==(const BaseOutPoint& a, const BaseOutPoint& b)
    {
        return (a.hash == b.hash && a.n == b.n);
    }

    friend bool operator!=(const BaseOutPoint& a, const BaseOutPoint& b)
    {
        return !(a == b);
    }
};

/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint : public BaseOutPoint
{
public:
    COutPoint() : BaseOutPoint(){};
    COutPoint(uint256 hashIn, uint32_t nIn) : BaseOutPoint(hashIn, nIn){};
    std::string ToString() const;
};

/** An outpoint - a combination of a transaction hash and an index n into its sapling
 * output description (vShieldedOutput) */
class SaplingOutPoint : public BaseOutPoint
{
public:
    // In-Memory Only
    bool writeToDisk = true;

    SaplingOutPoint() : BaseOutPoint() { writeToDisk = true; };
    SaplingOutPoint(uint256 hashIn, uint32_t nIn) : BaseOutPoint(hashIn, nIn) { writeToDisk = true; };
    std::string ToString() const;
};

/** An outpoint - a combination of a txid and an index n into its orchard
 * actions */
class OrchardOutPoint : public BaseOutPoint
{
public:
    // In-Memory Only
    bool writeToDisk = true;

    OrchardOutPoint() : BaseOutPoint() {};
    OrchardOutPoint(uint256 hashIn, uint32_t nIn) : BaseOutPoint(hashIn, nIn) {};
    std::string ToString() const;
};

/** An block location point used to log the exact chain location of archived transactions */
class ArchiveTxPoint
{
public:
    uint256 hashBlock;
    int nIndex;
    std::set<uint256> saplingIvks;
    std::set<uint256> saplingOvks;
    std::set<libzcash::OrchardIncomingViewingKeyPirate> orchardIvks;
    std::set<libzcash::OrchardOutgoingViewingKey> orchardOvks;

    // In-Memory Only
    bool writeToDisk = true;

    ArchiveTxPoint() { SetNull(); }
    ArchiveTxPoint(uint256 hashIn, int nIn)
    {
        hashBlock = hashIn;
        nIndex = nIn;
        writeToDisk = true;
    }
    ArchiveTxPoint(uint256 hashIn, int nIn, std::set<uint256> nSaplingIvks, std::set<uint256> nSaplingOvks, std::set<libzcash::OrchardIncomingViewingKeyPirate> nOrchardIvks, std::set<libzcash::OrchardOutgoingViewingKey> nOrchardOvks)
    {
        hashBlock = hashIn;
        nIndex = nIn;
        saplingIvks = nSaplingIvks;
        saplingOvks = nSaplingOvks;
        orchardIvks = nOrchardIvks;
        orchardOvks = nOrchardOvks;
        writeToDisk = true;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(hashBlock);
        READWRITE(nIndex);
        READWRITE(saplingIvks);
        READWRITE(saplingOvks);
        READWRITE(orchardIvks);
        READWRITE(orchardOvks);
    }

    void SetNull()
    {
        hashBlock.SetNull();
        nIndex = -1;
        saplingIvks.clear();
        saplingOvks.clear();
        orchardIvks.clear();
        orchardOvks.clear();
        writeToDisk = true;
    }
    bool IsNull() const { return (hashBlock.IsNull() && nIndex == -1 && saplingIvks.size() == 0 && saplingOvks.size() == 0 && orchardIvks.size() == 0 && orchardOvks.size() == 0); }
};

/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxIn
{
public:
    COutPoint prevout;
    CScript scriptSig;
    uint32_t nSequence;

    CTxIn()
    {
        nSequence = std::numeric_limits<unsigned int>::max();
    }

    explicit CTxIn(COutPoint prevoutIn, CScript scriptSigIn = CScript(), uint32_t nSequenceIn = std::numeric_limits<unsigned int>::max());
    CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn = CScript(), uint32_t nSequenceIn = std::numeric_limits<uint32_t>::max());

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(prevout);
        READWRITE(*(CScriptBase*)(&scriptSig));
        READWRITE(nSequence);
    }

    bool IsFinal() const
    {
        return (nSequence == std::numeric_limits<uint32_t>::max());
    }

    friend bool operator==(const CTxIn& a, const CTxIn& b)
    {
        return (a.prevout == b.prevout &&
                a.scriptSig == b.scriptSig &&
                a.nSequence == b.nSequence);
    }

    friend bool operator!=(const CTxIn& a, const CTxIn& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};

/** An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut
{
public:
    CAmount nValue;
    CScript scriptPubKey;
    uint64_t interest;
    CTxOut()
    {
        SetNull();
    }

    CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nValue);
        READWRITE(*(CScriptBase*)(&scriptPubKey));
    }

    void SetNull()
    {
        nValue = -1;
        scriptPubKey.clear();
    }

    bool IsNull() const
    {
        return (nValue == -1);
    }

    uint256 GetHash() const;

    CAmount GetDustThreshold(const CFeeRate& minRelayTxFee) const
    {
        // "Dust" is defined in terms of CTransaction::minRelayTxFee,
        // which has units satoshis-per-kilobyte.
        // If you'd pay more than 1/3 in fees
        // to spend something, then we consider it dust.
        // A typical spendable txout is 34 bytes big, and will
        // need a CTxIn of at least 148 bytes to spend:
        // so dust is a spendable txout less than 54 satoshis
        // with default minRelayTxFee.
        if (scriptPubKey.IsUnspendable())
            return 0;

        size_t nSize = GetSerializeSize(*this, SER_DISK, 0) + 148u;
        return 3 * minRelayTxFee.GetFee(nSize);
    }

    bool IsDust(const CFeeRate& minRelayTxFee) const
    {
        return (nValue < GetDustThreshold(minRelayTxFee));
    }

    friend bool operator==(const CTxOut& a, const CTxOut& b)
    {
        return (a.nValue == b.nValue && a.scriptPubKey == b.scriptPubKey);
    }

    friend bool operator!=(const CTxOut& a, const CTxOut& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};

struct WTxId
{
    const uint256 hash;
    const uint256 authDigest;

    WTxId() :
        authDigest(LEGACY_TX_AUTH_DIGEST) {}

    WTxId(const uint256& hashIn, const uint256& authDigestIn) :
        hash(hashIn), authDigest(authDigestIn) {}

    const std::vector<unsigned char> ToBytes() const {
        std::vector<unsigned char> vData(hash.begin(), hash.end());
        vData.insert(vData.end(), authDigest.begin(), authDigest.end());
        return vData;
    }

    friend bool operator<(const WTxId& a, const WTxId& b)
    {
        return (a.hash < b.hash ||
            (a.hash == b.hash && a.authDigest < b.authDigest));
    }

    friend bool operator==(const WTxId& a, const WTxId& b)
    {
        return a.hash == b.hash && a.authDigest == b.authDigest;
    }

    friend bool operator!=(const WTxId& a, const WTxId& b)
    {
        return a.hash != b.hash || a.authDigest != b.authDigest;
    }
};

struct CMutableTransaction;

/** The basic transaction that is broadcasted on the network and contained in
 * blocks.  A transaction can contain multiple inputs and outputs.
 */
class CTransaction
{
private:
    /// The consensus branch ID that this transaction commits to.
    /// Serialized from v5 onwards.
    std::optional<uint32_t> nConsensusBranchId;
    OrchardBundle orchardBundle;
    SaplingBundle saplingBundle;

    /** Memory only. */
    const WTxId wtxid;
    void UpdateHash() const;

protected:
    /** Developer testing only.  Set evilDeveloperFlag to true.
     * Convert a CMutableTransaction into a CTransaction without invoking UpdateHash()
     */
    CTransaction(const CMutableTransaction& tx, bool evilDeveloperFlag);

public:
    typedef std::array<unsigned char, 64> joinsplit_sig_t;
    typedef std::array<unsigned char, 64> binding_sig_t;

    // Transactions that include a list of JoinSplits are >= version 2.
    static const int32_t SPROUT_MIN_CURRENT_VERSION = 1;
    static const int32_t SPROUT_MAX_CURRENT_VERSION = 2;
    static const int32_t OVERWINTER_MIN_CURRENT_VERSION = 3;
    static const int32_t OVERWINTER_MAX_CURRENT_VERSION = 3;
    static const int32_t SAPLING_MIN_CURRENT_VERSION = 4;
    static const int32_t SAPLING_MAX_CURRENT_VERSION = 4;
    static const int32_t ORCHARD_MIN_CURRENT_VERSION = 5;
    static const int32_t ORCHARD_MAX_CURRENT_VERSION = 5;

    static_assert(SPROUT_MIN_CURRENT_VERSION >= SPROUT_MIN_TX_VERSION,
                  "standard rule for tx version should be consistent with network rule");

    static_assert(OVERWINTER_MIN_CURRENT_VERSION >= OVERWINTER_MIN_TX_VERSION,
                  "standard rule for tx version should be consistent with network rule");

    static_assert((OVERWINTER_MAX_CURRENT_VERSION <= OVERWINTER_MAX_TX_VERSION &&
                   OVERWINTER_MAX_CURRENT_VERSION >= OVERWINTER_MIN_CURRENT_VERSION),
                  "standard rule for tx version should be consistent with network rule");

    static_assert(SAPLING_MIN_CURRENT_VERSION >= SAPLING_MIN_TX_VERSION,
                  "standard rule for tx version should be consistent with network rule");

    static_assert((SAPLING_MAX_CURRENT_VERSION <= SAPLING_MAX_TX_VERSION &&
                   SAPLING_MAX_CURRENT_VERSION >= SAPLING_MIN_CURRENT_VERSION),
                  "standard rule for tx version should be consistent with network rule");

    static_assert(ORCHARD_MIN_CURRENT_VERSION >= ORCHARD_MIN_TX_VERSION,
                  "standard rule for tx version should be consistent with network rule");

    static_assert((ORCHARD_MAX_CURRENT_VERSION <= ORCHARD_MAX_TX_VERSION &&
                   ORCHARD_MAX_CURRENT_VERSION >= ORCHARD_MIN_CURRENT_VERSION),
                  "standard rule for tx version should be consistent with network rule");

    // The local variables are made const to prevent unintended modification
    // without updating the cached hash value. However, CTransaction is not
    // actually immutable; deserialization and assignment are implemented,
    // and bypass the constness. This is safe, as they update the entire
    // structure, including the hash.
    const bool fOverwintered{false};
    const int32_t nVersion{0};
    const uint32_t nVersionGroupId{0};
    const std::vector<CTxIn> vin;
    const std::vector<CTxOut> vout;
    const uint32_t nLockTime{0};
    const uint32_t nExpiryHeight{0};
    const std::vector<JSDescription> vjoinsplit;
    const uint256 joinSplitPubKey;
    const joinsplit_sig_t joinSplitSig = {{0}};

    /** Construct a CTransaction that qualifies as IsNull() */
    CTransaction();

    /** Convert a CMutableTransaction into a CTransaction. */
    CTransaction(const CMutableTransaction& tx);
    CTransaction(CMutableTransaction&& tx);

    CTransaction& operator=(const CTransaction& tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        uint32_t header;
        if (ser_action.ForRead()) {
            // When deserializing, unpack the 4 byte header to extract fOverwintered and nVersion.
            READWRITE(header);
            *const_cast<bool*>(&fOverwintered) = header >> 31;
            *const_cast<int32_t*>(&this->nVersion) = header & 0x7FFFFFFF;
        } else {
            header = GetHeader();
            READWRITE(header);
        }
        if (fOverwintered) {
            READWRITE(*const_cast<uint32_t*>(&this->nVersionGroupId));
        }

        bool isOverwinterV3 =
            fOverwintered &&
            nVersionGroupId == OVERWINTER_VERSION_GROUP_ID &&
            nVersion == OVERWINTER_TX_VERSION;

        bool isSaplingV4 =
            fOverwintered &&
            nVersionGroupId == SAPLING_VERSION_GROUP_ID &&
            nVersion == SAPLING_TX_VERSION;

        bool isOrchardV5 =
            fOverwintered &&
            nVersionGroupId == ORCHARD_VERSION_GROUP_ID &&
            nVersion == ORCHARD_TX_VERSION;

        // It is not possible to make the transaction's serialized form vary on
        // a per-enabled-feature basis. The approach here is that all
        // serialization rules for not-yet-released features must be
        // non-conflicting and transaction version/group must be set to
        // ZFUTURE_TX_(VERSION/GROUP_ID)
        bool isFuture =
            fOverwintered &&
            nVersionGroupId == ZFUTURE_VERSION_GROUP_ID &&
            nVersion == ZFUTURE_TX_VERSION;

        if (fOverwintered && !(isOverwinterV3 || isSaplingV4 || isOrchardV5 || isFuture)) {
            throw std::ios_base::failure("Unknown transaction format");
        }

        if (isOrchardV5) {
            // Common Transaction Fields (plus version bytes above)
            if (ser_action.ForRead()) {
                uint32_t consensusBranchId;
                READWRITE(consensusBranchId);
                *const_cast<std::optional<uint32_t>*>(&nConsensusBranchId) = consensusBranchId;
            } else {
                uint32_t consensusBranchId = nConsensusBranchId.value();
                READWRITE(consensusBranchId);
            }
            READWRITE(*const_cast<uint32_t*>(&nLockTime));
            READWRITE(*const_cast<uint32_t*>(&nExpiryHeight));

            // Transparent Transaction Fields
            READWRITE(*const_cast<std::vector<CTxIn>*>(&vin));
            READWRITE(*const_cast<std::vector<CTxOut>*>(&vout));

            // Sapling Transaction Fields
            READWRITE(saplingBundle);

            // Orchard Transaction Fields
            READWRITE(orchardBundle);
        } else {
            // Legacy transaction formats
            READWRITE(*const_cast<std::vector<CTxIn>*>(&vin));
            READWRITE(*const_cast<std::vector<CTxOut>*>(&vout));
            READWRITE(*const_cast<uint32_t*>(&nLockTime));
            if (isOverwinterV3 || isSaplingV4 || isFuture) {
                READWRITE(*const_cast<uint32_t*>(&nExpiryHeight));
            }
            SaplingV4Reader saplingReader(isSaplingV4 || isFuture);
            bool haveSaplingActions;
            if (ser_action.ForRead()) {
                READWRITE(saplingReader);
                haveSaplingActions = saplingReader.HaveActions();
            } else {
                SaplingV4Writer saplingWriter(saplingBundle, isSaplingV4 || isFuture);
                READWRITE(saplingWriter);
                haveSaplingActions = saplingBundle.IsPresent();
            }
            if (nVersion >= 2) {
                // These fields do not depend on fOverwintered
                auto os = WithVersion(&s, static_cast<int>(header));
                ::SerReadWrite(os, *const_cast<std::vector<JSDescription>*>(&vjoinsplit), ser_action);
                if (vjoinsplit.size() > 0) {
                    READWRITE(*const_cast<uint256*>(&joinSplitPubKey));
                    READWRITE(*const_cast<joinsplit_sig_t*>(&joinSplitSig));
                }
            }
            if ((isSaplingV4 || isFuture) && haveSaplingActions) {
                binding_sig_t bindingSigForReadWrite;

                if (!ser_action.ForRead()) {
                    bindingSigForReadWrite = saplingBundle.GetDetails().binding_sig();
                }
                READWRITE(bindingSigForReadWrite);

                if (ser_action.ForRead()) {
                    saplingBundle = saplingReader.FinishBundleAssembly(bindingSigForReadWrite);
                }
            }
        }

        if (ser_action.ForRead()) {
            UpdateHash();
        }
    }

    template <typename Stream>
    CTransaction(deserialize_type, Stream& s) : CTransaction(CMutableTransaction(deserialize, s))
    {
    }

    bool IsNull() const
    {
        return vin.empty() && vout.empty();
    }

    const uint256& GetHash() const {
        return wtxid.hash;
    }

    /**
     * Returns the authorizing data commitment for this transaction.
     *
     * For v1-v4 transactions, this returns the null hash (i.e. all-zeroes).
     */
    const uint256& GetAuthDigest() const {
        return wtxid.authDigest;
    }

    const WTxId& GetWTxId() const {
        return wtxid;
    }

    uint32_t GetHeader() const
    {
        // When serializing v1 and v2, the 4 byte header is nVersion
        uint32_t header = this->nVersion;
        // When serializing Overwintered tx, the 4 byte header is the combination of fOverwintered and nVersion
        if (fOverwintered) {
            header |= 1 << 31;
        }
        return header;
    }

    /*
     * Context for the two methods below:
     * As at most one of vpub_new and vpub_old is non-zero in every JoinSplit,
     * we can think of a JoinSplit as an input or output according to which one
     * it is (e.g. if vpub_new is non-zero the joinSplit is "giving value" to
     * the outputs in the transaction). Similarly, we can think of the Sapling
     * shielded part of the transaction as an input or output according to
     * whether valueBalance - the sum of shielded input values minus the sum of
     * shielded output values - is positive or negative.
     */

    // Return sum of txouts, (negative valueBalance or zero) and JoinSplit vpub_old.
    CAmount GetValueOut() const;
    // GetValueIn() is a method on CCoinsViewCache, because
    // inputs must be known to compute value in.

    // Return sum of (positive valueBalance or zero) and JoinSplit vpub_new
    CAmount GetShieldedValueIn() const;

    // Compute priority, given priority of inputs and (optionally) tx size
    double ComputePriority(double dPriorityInputs, unsigned int nTxSize = 0) const;

    // Compute modified tx size for priority calculation (optionally given tx size)
    unsigned int CalculateModifiedSize(unsigned int nTxSize = 0) const;

    /**
     * Get the total transaction size in bytes, including witness data.
     * "Total Size" defined in BIP141 and BIP144.
     * @return Total transaction size in bytes
     */
    unsigned int GetTotalSize() const;

    bool IsMint() const
    {
        return IsCoinImport() || IsCoinBase();
    }

    bool IsCoinBase() const
    {
        return (vin.size() == 1 && vin[0].prevout.IsNull());
    }

    int64_t UnlockTime(uint32_t voutNum) const;

    bool IsCoinImport() const
    {
        return (vin.size() == 1 && vin[0].prevout.n == 10e8);
    }

    friend bool operator==(const CTransaction& a, const CTransaction& b)
    {
        return a.wtxid.hash == b.wtxid.hash;
    }

    friend bool operator!=(const CTransaction& a, const CTransaction& b)
    {
        return a.wtxid.hash != b.wtxid.hash;
    }

    std::string ToString() const;

    std::optional<uint32_t> GetConsensusBranchId() const {
        return nConsensusBranchId;
    }

    //Sapling Functions
    size_t GetSaplingSpendsCount() const;
    size_t GetSaplingOutputsCount() const;

    const rust::Vec<sapling::Spend> GetSaplingSpends() const;
    const rust::Vec<sapling::Output> GetSaplingOutputs() const;

    size_t GetOrchardActionsCount() const;
    const rust::Vec<orchard_bundle::Action> GetOrchardActions() const;

    /**
     * Returns the Sapling value balance for the transaction.
     */
    CAmount GetValueBalanceSapling() const;

    /**
     * Returns the Sapling value balance for the transaction.
     */
    CAmount GetValueBalanceOrchard() const;

    /**
     * Returns the Sapling bundle for the transaction.
     */
    const SaplingBundle& GetSaplingBundle() const;
    /**
     * Returns the Orchard bundle for the transaction.
     */
    const OrchardBundle& GetOrchardBundle() const;

    binding_sig_t BindingSigFromBundle() const {
        binding_sig_t bindingSigBundle = {{0}};

        if (saplingBundle.IsPresent()) {
            bindingSigBundle = saplingBundle.GetDetails().binding_sig();
        }
        return bindingSigBundle;
    }
};

/** A mutable version of CTransaction. */
struct CMutableTransaction {
    bool fOverwintered{false};
    int32_t nVersion{0};
    uint32_t nVersionGroupId{0};
    /// The consensus branch ID that this transaction commits to.
    /// Serialized from v5 onwards.
    std::optional<uint32_t> nConsensusBranchId;
    std::vector<CTxIn> vin;
    std::vector<CTxOut> vout;
    int32_t nLockTime{0};
    uint32_t nExpiryHeight{0};
    SaplingBundle saplingBundle;
    OrchardBundle orchardBundle;
    std::vector<JSDescription> vjoinsplit;
    uint256 joinSplitPubKey;
    CTransaction::joinsplit_sig_t joinSplitSig = {{0}};

    CMutableTransaction();
    CMutableTransaction(const CTransaction& tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        uint32_t header;
        if (ser_action.ForRead()) {
            // When deserializing, unpack the 4 byte header to extract fOverwintered and nVersion.
            READWRITE(header);
            fOverwintered = header >> 31;
            this->nVersion = header & 0x7FFFFFFF;
        } else {
            // When serializing v1 and v2, the 4 byte header is nVersion
            header = this->nVersion;
            // When serializing Overwintered tx, the 4 byte header is the combination of fOverwintered and nVersion
            if (fOverwintered) {
                header |= 1 << 31;
            }
            READWRITE(header);
        }
        if (fOverwintered) {
            READWRITE(nVersionGroupId);
        }

        bool isOverwinterV3 =
            fOverwintered &&
            nVersionGroupId == OVERWINTER_VERSION_GROUP_ID &&
            nVersion == OVERWINTER_TX_VERSION;
        bool isSaplingV4 =
            fOverwintered &&
            nVersionGroupId == SAPLING_VERSION_GROUP_ID &&
            nVersion == SAPLING_TX_VERSION;
        bool isOrchardV5 =
            fOverwintered &&
            nVersionGroupId == ORCHARD_VERSION_GROUP_ID &&
            nVersion == ORCHARD_TX_VERSION;
        bool isFuture =
            fOverwintered &&
            nVersionGroupId == ZFUTURE_VERSION_GROUP_ID &&
            nVersion == ZFUTURE_TX_VERSION;

        if (fOverwintered && !(isOverwinterV3 || isSaplingV4 || isOrchardV5 || isFuture)) {
            throw std::ios_base::failure("Unknown transaction format");
        }

        if (isOrchardV5) {
            // Common Transaction Fields (plus version bytes above)
            if (ser_action.ForRead()) {
                uint32_t consensusBranchId;
                READWRITE(consensusBranchId);
                nConsensusBranchId = consensusBranchId;
            } else {
                uint32_t consensusBranchId = nConsensusBranchId.value();
                READWRITE(consensusBranchId);
            }
            READWRITE(nLockTime);
            READWRITE(nExpiryHeight);

            // Transparent Transaction Fields
            READWRITE(vin);
            READWRITE(vout);

            // Sapling Transaction Fields
            READWRITE(saplingBundle);

            // Orchard Transaction Fields
            READWRITE(orchardBundle);
        } else {
            READWRITE(vin);
            READWRITE(vout);
            READWRITE(nLockTime);
            if (isOverwinterV3 || isSaplingV4) {
                READWRITE(nExpiryHeight);
            }
            SaplingV4Reader saplingReader(isSaplingV4 || isFuture);
            bool haveSaplingActions;
            if (ser_action.ForRead()) {
                READWRITE(saplingReader);
                haveSaplingActions = saplingReader.HaveActions();
            } else {
                SaplingV4Writer saplingWriter(saplingBundle, isSaplingV4 || isFuture);
                READWRITE(saplingWriter);
                haveSaplingActions = saplingBundle.IsPresent();
            }
            if (nVersion >= 2) {
                auto os = WithVersion(&s, static_cast<int>(header));
                ::SerReadWrite(os, vjoinsplit, ser_action);
                if (vjoinsplit.size() > 0) {
                    READWRITE(joinSplitPubKey);
                    READWRITE(joinSplitSig);
                }
            }
            if ((isSaplingV4 || isFuture) && haveSaplingActions) {
                CTransaction::binding_sig_t bindingSigForReadWrite;

                if (!ser_action.ForRead()) {
                    bindingSigForReadWrite = saplingBundle.GetDetails().binding_sig();
                }
                READWRITE(bindingSigForReadWrite);

                if (ser_action.ForRead()) {
                    saplingBundle = saplingReader.FinishBundleAssembly(bindingSigForReadWrite);
                }
            }
        }
    }

    template <typename Stream>
    CMutableTransaction(deserialize_type, Stream& s)
    {
        Unserialize(s);
    }

    /** Compute the hash of this CMutableTransaction. This is computed on the
     * fly, as opposed to GetHash() in CTransaction, which uses a cached result.
     */
    uint256 GetHash() const;

    /** Compute the authentication digest of this CMutableTransaction. This is
     * computed on the fly, as opposed to GetAuthDigest() in CTransaction, which
     * uses a cached result.
     *
     * For v1-v4 transactions, this returns the null hash (i.e. all-zeroes).
     */
    uint256 GetAuthDigest() const;

};

#endif // BITCOIN_PRIMITIVES_TRANSACTION_H
