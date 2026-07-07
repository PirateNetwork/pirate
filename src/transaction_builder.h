// Copyright (c) 2018 The Zcash developers
// Copyright (c) 2024-2025 The Pirate Network developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TRANSACTION_BUILDER_H
#define TRANSACTION_BUILDER_H

#include "consensus/params.h"
#include "keystore.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/standard.h"
#include "serialize.h"
#include "streams.h"
#include "uint256.h"
#include "zcash/Address.hpp"
#include "zcash/IncrementalMerkleTree.hpp"
#include "zcash/Note.hpp"
#include "zcash/NoteEncryption.hpp"
#include "zcash/memo.h"

#include <optional>

uint256 ProduceShieldedSignatureHash(
    uint32_t consensusBranchId,
    const CTransaction& tx,
    const std::vector<CTxOut>& allPrevOutputs,
    const sapling::UnauthorizedBundle& saplingBundle,
    const std::optional<ironwood::UnauthorizedBundle>& ironwoodBundle);

namespace ironwood
{

/// A builder that constructs an `UnauthorizedBundle` from a set of notes to be spent,
/// and recipients to receive funds.
class Builder
{
private:
    /// The Ironwood builder. Memory is allocated by Rust. If this is `nullptr` then
    /// `Builder::Build` has been called, and all subsequent operations will throw an
    /// exception.
    std::unique_ptr<IronwoodBuilderPtr, decltype(&ironwood_builder_free)> inner;
    bool hasActions{false};

    Builder() : inner(nullptr, ironwood_builder_free), hasActions(false) {}

public:
    Builder(bool spendsEnabled, bool outputsEnabled, uint256 anchor);

    // Builder should never be copied
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;
    Builder(Builder&& builder) : inner(std::move(builder.inner)), hasActions(builder.hasActions) {}
    Builder& operator=(Builder&& builder)
    {
        if (this != &builder) {
            inner = std::move(builder.inner);
            hasActions = builder.hasActions;
        }
        return *this;
    }

    /// Adds a note's serialized parts to be spent in this bundle.
    ///
    /// Returns 'false' if deserialzation or note contruction fails
    /// Returns `false` if the given Merkle path does not have the required anchor
    /// for the given note.
    bool AddSpendFromParts(
        const libzcash::IronwoodFullViewingKey fvk,
        const libzcash::IronwoodPaymentAddress addr,
        const CAmount value,
        const uint256 rho,
        const uint256 rseed,
        const libzcash::MerklePath ironwoodMerklePath);

    /// Adds an address which will receive funds in this bundle.
    bool AddOutput(
        const std::optional<uint256>& ovk,
        const libzcash::IronwoodPaymentAddress& to,
        CAmount value,
        const std::optional<libzcash::Memo>& memo = std::nullopt);

    /// Returns `true` if any spends or outputs have been added to this builder. This can
    /// be used to avoid calling `Build()` and creating a dummy Ironwood bundle.
    bool HasActions()
    {
        return hasActions;
    }

    /// Builds a bundle containing the given spent notes and recipients.
    ///
    /// Returns `std::nullopt` if an error occurs.
    ///
    /// Calling this method invalidates this object; in particular, if an error occurs
    /// this builder must be discarded and a new builder created. Subsequent usage of this
    /// object in any way will cause an exception. This emulates Rust's compile-time move
    /// semantics at runtime.
    std::optional<UnauthorizedBundle> Build();
};

/// An unauthorized Ironwood bundle, ready for its proof to be created and signatures
/// applied.
class UnauthorizedBundle
{
private:
    /// An optional Ironwood bundle (with `nullptr` corresponding to `None`).
    /// Memory is allocated by Rust.
    std::unique_ptr<IronwoodUnauthorizedBundlePtr, decltype(&ironwood_unauthorized_bundle_free)> inner;

    UnauthorizedBundle() : inner(nullptr, ironwood_unauthorized_bundle_free) {}
    UnauthorizedBundle(IronwoodUnauthorizedBundlePtr* bundle) : inner(bundle, ironwood_unauthorized_bundle_free) {}
    friend class Builder;
    // The parentheses here are necessary to avoid the following compilation error:
    //     error: C++ requires a type specifier for all declarations
    //             friend uint256 ::ProduceShieldedSignatureHash(
    //             ~~~~~~           ^
    friend uint256(::ProduceShieldedSignatureHash(
        uint32_t consensusBranchId,
        const CTransaction& tx,
        const std::vector<CTxOut>& allPrevOutputs,
        const sapling::UnauthorizedBundle& saplingBundle,
        const std::optional<ironwood::UnauthorizedBundle>& ironwoodBundle));

public:
    // UnauthorizedBundle should never be copied
    UnauthorizedBundle(const UnauthorizedBundle&) = delete;
    UnauthorizedBundle& operator=(const UnauthorizedBundle&) = delete;
    UnauthorizedBundle(UnauthorizedBundle&& bundle) : inner(std::move(bundle.inner)) {}
    UnauthorizedBundle& operator=(UnauthorizedBundle&& bundle)
    {
        if (this != &bundle) {
            inner = std::move(bundle.inner);
        }
        return *this;
    }

    /// Adds proofs and signatures to this bundle.
    ///
    /// Returns `std::nullopt` if an error occurs.
    ///
    /// Calling this method invalidates this object; in particular, if an error occurs
    /// this bundle must be discarded and a new bundle built. Subsequent usage of this
    /// object in any way will cause an exception. This emulates Rust's compile-time
    /// move semantics at runtime.
    std::optional<IronwoodBundle> ProveAndSign(
        std::vector<libzcash::IronwoodSpendingKey> keys,
        uint256 sighash);
};

} // namespace ironwood

class SaplingSpendDescriptionInfo
{
public:
    SaplingOutPoint op;
    libzcash::SaplingPaymentAddress addr;
    CAmount value;
    uint256 rcm;
    libzcash::MerklePath saplingMerklePath;
    uint256 anchor;

    SaplingSpendDescriptionInfo() {}

    SaplingSpendDescriptionInfo(
        SaplingOutPoint opIn,
        libzcash::SaplingPaymentAddress addrIn,
        CAmount valueIn,
        uint256 rcmIn,
        libzcash::MerklePath saplingMerklePathIn,
        uint256 anchorIn) : op(opIn), addr(addrIn), value(valueIn), rcm(rcmIn), saplingMerklePath(saplingMerklePathIn), anchor(anchorIn) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(op);
        READWRITE(addr);
        READWRITE(value);
        READWRITE(rcm);
        READWRITE(saplingMerklePath);
        READWRITE(anchor);
    }
};

class IronwoodSpendDescriptionInfo
{
public:
    IronwoodOutPoint op;
    libzcash::IronwoodPaymentAddress addr;
    CAmount value;
    uint256 rho;
    uint256 rseed;
    libzcash::MerklePath ironwoodMerklePath;
    uint256 anchor;

    IronwoodSpendDescriptionInfo() {}

    IronwoodSpendDescriptionInfo(
        IronwoodOutPoint opIn,
        libzcash::IronwoodPaymentAddress addrIn,
        CAmount valueIn,
        uint256 rhoIn,
        uint256 rseedIn,
        libzcash::MerklePath ironwoodMerklePathIn,
        uint256 anchorIn) : op(opIn), addr(addrIn), value(valueIn), rho(rhoIn), rseed(rseedIn), ironwoodMerklePath(ironwoodMerklePathIn), anchor(anchorIn) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(op);
        READWRITE(addr);
        READWRITE(value);
        READWRITE(rho);
        READWRITE(rseed);
        READWRITE(ironwoodMerklePath);
        READWRITE(anchor);
    }
};


class SaplingOutputDescriptionInfo
{
public:
    libzcash::SaplingPaymentAddress addr;
    CAmount value;
    std::optional<libzcash::Memo> memo;

    SaplingOutputDescriptionInfo() : memo(std::nullopt) {}
    SaplingOutputDescriptionInfo(
        libzcash::SaplingPaymentAddress addrIn,
        CAmount valueIn,
        const std::optional<libzcash::Memo>& memoIn = std::nullopt)
    : addr(addrIn), value(valueIn), memo(memoIn)
    {
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(addr);
        READWRITE(value);
        READWRITE(memo);
    }
};

class IronwoodOutputDescriptionInfo
{
public:
    libzcash::IronwoodPaymentAddress addr;
    CAmount value;
    std::optional<libzcash::Memo> memo;

    IronwoodOutputDescriptionInfo() : memo(std::nullopt) {}
    IronwoodOutputDescriptionInfo(
        libzcash::IronwoodPaymentAddress addrIn,
        CAmount valueIn,
        const std::optional<libzcash::Memo>& memoIn = std::nullopt)
    : addr(addrIn), value(valueIn), memo(memoIn)
    {
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(addr);
        READWRITE(value);
        READWRITE(memo);
    }
};

struct TransparentInputInfo {
    CScript scriptPubKey;
    CAmount value;

    TransparentInputInfo(
        CScript scriptPubKey,
        CAmount value) : scriptPubKey(scriptPubKey), value(value) {}
};

class TransactionBuilderResult
{
private:
    std::optional<CTransaction> maybeTx;
    std::optional<std::string> maybeError;

public:
    TransactionBuilderResult() = delete;
    TransactionBuilderResult(const CTransaction& tx);
    TransactionBuilderResult(const std::string& error);
    bool IsTx();
    bool IsError();
    CTransaction GetTxOrThrow();
    std::string GetError();
};

class TransactionBuilder
{
private:
    Consensus::Params consensusParams;
    int nHeight = 0;
    const CKeyStore* keystore;
    CMutableTransaction mtx;
    CAmount fee = 10000;
    int iMinConf = 1;
    uint32_t consensusBranchId;
    std::string strNetworkID = "main";
    uint16_t checksum = 0xFFFF;

    std::vector<CTxOut> tIns;

    std::optional<SaplingBundle> saplingBundle;
    std::optional<IronwoodBundle> ironwoodBundle;

    std::optional<ironwood::Builder> ironwoodBuilder;
    CAmount valueBalanceSapling = 0;
    std::optional<rust::Box<sapling::Builder>> saplingBuilder;
    CAmount valueBalanceIronwood = 0;

    uint256 saplingAnchor = uint256S("0000000000000000000000000000000000000000000000000000000000000000");
    uint256 ironwoodAnchor = uint256S("0000000000000000000000000000000000000000000000000000000000000000");

    std::optional<std::pair<uint256, libzcash::SaplingPaymentAddress>> firstSaplingChangeAddr;
    std::optional<std::pair<uint256, libzcash::SaplingPaymentAddress>> saplingChangeAddr;

    std::vector<libzcash::IronwoodSpendingKey> ironwoodSpendingKeys;
    std::optional<std::pair<uint256, libzcash::IronwoodPaymentAddress>> firstIronwoodChangeAddr;
    std::optional<std::pair<uint256, libzcash::IronwoodPaymentAddress>> ironwoodChangeAddr;

    std::optional<CTxDestination> tChangeAddr;
    std::optional<CScript> opReturn;

    bool AddOpRetLast(CScript& s);

public:
    std::vector<SaplingSpendDescriptionInfo> vSaplingSpends;
    std::vector<SaplingOutputDescriptionInfo> vSaplingOutputs;

    std::vector<IronwoodSpendDescriptionInfo> vIronwoodSpends;
    std::vector<IronwoodOutputDescriptionInfo> vIronwoodOutputs;

    // Running totals of shielded components that have been committed to the
    // Rust builders via ConvertRaw*(). The staging vectors above are cleared
    // after each convert call, so these counters preserve the committed counts
    // for use by IsValidSize().
    size_t nCommittedSaplingSpends  = 0;
    size_t nCommittedSaplingOutputs = 0;
    size_t nCommittedIronwoodSpends  = 0;
    size_t nCommittedIronwoodOutputs = 0;


    TransactionBuilder();
    TransactionBuilder(const Consensus::Params& consensusParams, int nHeight, CKeyStore* keyStore = nullptr);

    // TransactionBuilder should never be copied
    TransactionBuilder(const TransactionBuilder&) = delete;
    TransactionBuilder& operator=(const TransactionBuilder&) = delete;


    // Transaction Builder initialization functions
    void InitializeTransactionBuilder(const Consensus::Params& consensusParams, int nHeight);
    void SetFee(CAmount fee);
    void SetMinConfirmations(int iMinConf);
    void SetExpiryHeight(int expHeight);
    void SetLockTime(uint32_t time) { this->mtx.nLockTime = time; }

    // Validate datastream with crc16 Checksum
    uint16_t CalculateChecksum();
    void SetChecksum();
    uint16_t GetChecksum();
    bool ValidateChecksum();


    // Return the underlying mutable transaction
    CTransaction getTransaction() { return mtx; }


    // Sapling
    void InitializeSapling(uint256 anchor);

    bool AddSaplingSpendRaw(
        SaplingOutPoint op,
        libzcash::SaplingPaymentAddress addr,
        CAmount value,
        uint256 rcm,
        libzcash::MerklePath saplingMerklePath,
        uint256 anchor);

    bool ConvertRawSaplingSpend(libzcash::SaplingExtendedSpendingKey extsk);

    bool AddSaplingOutputRaw(
        libzcash::SaplingPaymentAddress to,
        CAmount value,
        const std::optional<libzcash::Memo>& memo = std::nullopt);

    bool ConvertRawSaplingOutput(uint256 ovk);


    // Ironwood
    void InitializeIronwood(
        bool spendsEnabled,
        bool outputsEnabled,
        uint256 anchor);

    bool AddIronwoodSpendRaw(
        IronwoodOutPoint op,
        libzcash::IronwoodPaymentAddress addr,
        CAmount value,
        uint256 rho,
        uint256 rseed,
        libzcash::MerklePath ironwoodMerklePath,
        uint256 anchor);

    bool ConvertRawIronwoodSpend(libzcash::IronwoodExtendedSpendingKeyPirate extsk);

    bool AddIronwoodOutputRaw(
        libzcash::IronwoodPaymentAddress to,
        CAmount value,
        const std::optional<libzcash::Memo>& memo = std::nullopt);

    bool ConvertRawIronwoodOutput(uint256 ovk);


    // Transaparent Addresses
    void AddTransparentInput(COutPoint utxo, CScript scriptPubKey, CAmount value, uint32_t nSequence = 0xffffffff);

    bool AddTransparentOutput(CTxDestination& to, CAmount value);

    void AddOpRet(CScript& s);

    bool AddOpRetLast();


    // Change
    void SendChangeTo(libzcash::IronwoodPaymentAddress changeAddr, uint256 ovk);

    void SendChangeTo(libzcash::SaplingPaymentAddress changeAddr, uint256 ovk);

    bool SendChangeTo(CTxDestination& changeAddr);

    /**
     * @brief Check whether the estimated transaction size is within the protocol limit
     *
     * Calculates a conservative upper-bound estimate of the transaction size
     * using the current vectors of inputs and outputs, plus @p extraOutputs
     * additional outputs (e.g. change outputs not yet queued).
     *
     * Per-component sizes (Zcash v5 protocol):
     *   Transparent input:  148 bytes (outpoint + scriptsig + sequence)
     *   Transparent output:  34 bytes
     *   Sapling spend:      384 bytes
     *   Sapling output:     948 bytes
     *   Ironwood action:     820 bytes
     *   Ironwood bundle overhead (proof + binding sig): 2208 bytes
     *   Sapling bundle overhead (anchor + valueBalance + binding sig): 104 bytes
     *
     * @param extraOutputs  Number of additional outputs not yet in the vectors
     *                      (defaults to 2 for the typical change-output scenario)
     * @return              true  if the estimated size is within 95% of the maximum
     *                      allowed at @p nHeight (190 000 bytes post-Sapling, 95 000
     *                      bytes pre-Sapling); false if the limit would be exceeded
     */
    bool IsValidSize(int extraOutputs = 2) const;

    TransactionBuilderResult Build();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(consensusParams);
        READWRITE(consensusBranchId);
        READWRITE(strNetworkID);
        READWRITE(nHeight);
        READWRITE(mtx);
        READWRITE(saplingAnchor);
        READWRITE(vSaplingSpends);
        READWRITE(vSaplingOutputs);
        READWRITE(ironwoodAnchor);
        READWRITE(vIronwoodSpends);
        READWRITE(vIronwoodOutputs);
        READWRITE(fee);
        READWRITE(checksum);
    }
};

#endif /* TRANSACTION_BUILDER_H */
