// Copyright (c) 2018 The Zcash developers
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
    const std::optional<orchard::UnauthorizedBundle>& orchardBundle);

namespace orchard
{

/// The information necessary to spend an Orchard note.
// class SpendInfo
// {
// private:
//     /// Memory is allocated by Rust.
//     std::unique_ptr<OrchardSpendInfoPtr, decltype(&orchard_spend_info_free)> inner;
//     libzcash::OrchardRawAddress from;
//     uint64_t noteValue;
//
//     // SpendInfo() : inner(nullptr, orchard_spend_info_free) {}
//     SpendInfo(
//         OrchardSpendInfoPtr* spendInfo,
//         libzcash::OrchardRawAddress fromIn,
//         uint64_t noteValueIn) : inner(spendInfo, orchard_spend_info_free), from(fromIn), noteValue(noteValueIn) {}
//
//     friend class Builder;
//     friend class ::OrchardWallet;
//
// public:
//     // SpendInfo should never be copied
//     SpendInfo(const SpendInfo&) = delete;
//     SpendInfo& operator=(const SpendInfo&) = delete;
//     SpendInfo(SpendInfo&& spendInfo) : inner(std::move(spendInfo.inner)), from(std::move(spendInfo.from)), noteValue(std::move(spendInfo.noteValue)) {}
//     SpendInfo& operator=(SpendInfo&& spendInfo)
//     {
//         if (this != &spendInfo) {
//             inner = std::move(spendInfo.inner);
//             from = std::move(spendInfo.from);
//             noteValue = std::move(spendInfo.noteValue);
//         }
//         return *this;
//     }
//
//     inline libzcash::OrchardRawAddress FromAddress() const { return from; };
//     inline uint64_t Value() const { return noteValue; };
// };

/// A builder that constructs an `UnauthorizedBundle` from a set of notes to be spent,
/// and recipients to receive funds.
class Builder
{
private:
    /// The Orchard builder. Memory is allocated by Rust. If this is `nullptr` then
    /// `Builder::Build` has been called, and all subsequent operations will throw an
    /// exception.
    std::unique_ptr<OrchardBuilderPtr, decltype(&orchard_builder_free)> inner;
    bool hasActions{false};

    Builder() : inner(nullptr, orchard_builder_free), hasActions(false) {}

public:
    Builder(bool spendsEnabled, bool outputsEnabled, uint256 anchor);

    // Builder should never be copied
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;
    Builder(Builder&& builder) : inner(std::move(builder.inner)) {}
    Builder& operator=(Builder&& builder)
    {
        if (this != &builder) {
            inner = std::move(builder.inner);
        }
        return *this;
    }

    /// Adds a note to be spent in this bundle.
    ///
    /// Returns `false` if the given Merkle path does not have the required anchor
    /// for the given note.
    bool AddSpend(
      const libzcash::OrchardFullViewingKeyPirate fvk,
      const orchard_bundle::Action* action,
      const libzcash::MerklePath orchardMerklePath);

    /// Adds an address which will receive funds in this bundle.
    void AddOutput(
        const std::optional<uint256>& ovk,
        const libzcash::OrchardPaymentAddressPirate& to,
        CAmount value,
        const std::optional<libzcash::Memo>& memo);

    /// Returns `true` if any spends or outputs have been added to this builder. This can
    /// be used to avoid calling `Build()` and creating a dummy Orchard bundle.
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

/// An unauthorized Orchard bundle, ready for its proof to be created and signatures
/// applied.
class UnauthorizedBundle
{
private:
    /// An optional Orchard bundle (with `nullptr` corresponding to `None`).
    /// Memory is allocated by Rust.
    std::unique_ptr<OrchardUnauthorizedBundlePtr, decltype(&orchard_unauthorized_bundle_free)> inner;

    UnauthorizedBundle() : inner(nullptr, orchard_unauthorized_bundle_free) {}
    UnauthorizedBundle(OrchardUnauthorizedBundlePtr* bundle) : inner(bundle, orchard_unauthorized_bundle_free) {}
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
        const std::optional<orchard::UnauthorizedBundle>& orchardBundle));

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
    std::optional<OrchardBundle> ProveAndSign(
        const unsigned char* keys,
        size_t keyCount,
        uint256 sighash);
};

} // namespace orchard

struct SaplingSpendDescriptionInfo {
    libzcash::SaplingExtendedSpendingKey extsk;
    libzcash::SaplingNote note;
    uint256 alpha;
    uint256 anchor;
    libzcash::MerklePath saplingMerklePath;

    SaplingSpendDescriptionInfo(
        libzcash::SaplingExtendedSpendingKey extsk,
        libzcash::SaplingNote note,
        uint256 anchor,
        libzcash::MerklePath saplingMerklePath);
};

class SaplingSpendDescriptionInfoRaw
{
public:
    libzcash::SaplingPaymentAddress addr;
    CAmount value;
    SaplingOutPoint op;
    libzcash::SaplingNotePlaintext notePt;
    libzcash::MerklePath saplingMerklePath;

    SaplingSpendDescriptionInfoRaw() {}

    SaplingSpendDescriptionInfoRaw(
        libzcash::SaplingPaymentAddress addrIn,
        CAmount valueIn,
        SaplingOutPoint opIn,
        libzcash::SaplingNotePlaintext notePtIn,
        libzcash::MerklePath saplingMerklePathIn) : addr(addrIn), value(valueIn), op(opIn), notePt(notePtIn), saplingMerklePath(saplingMerklePathIn) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(addr);
        READWRITE(value);
        READWRITE(op);
        READWRITE(notePt);
        READWRITE(saplingMerklePath);
    }
};


struct SaplingOutputDescriptionInfo {
    uint256 ovk;
    libzcash::SaplingNote note;
    std::array<unsigned char, ZC_MEMO_SIZE> memo;

    SaplingOutputDescriptionInfo(
        uint256 ovk,
        libzcash::SaplingNote note,
        std::array<unsigned char, ZC_MEMO_SIZE> memo) : ovk(ovk), note(note), memo(memo) {}

    // std::optional<OutputDescription> Build(void* ctx);
};


class SaplingOutputDescriptionInfoRaw
{
public:
    libzcash::SaplingPaymentAddress addr;
    CAmount value;
    std::array<unsigned char, ZC_MEMO_SIZE> memo;

    SaplingOutputDescriptionInfoRaw() {}
    SaplingOutputDescriptionInfoRaw(
        libzcash::SaplingPaymentAddress addrIn,
        CAmount valueIn,
        std::array<unsigned char, ZC_MEMO_SIZE> memoIn)
    {
        addr = addrIn;
        value = valueIn;
        memo = memoIn;
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

class OrchardOutputDescriptionInfoRaw
{
public:
    libzcash::OrchardPaymentAddressPirate addr;
    CAmount value;
    std::array<unsigned char, ZC_MEMO_SIZE> memo;

    OrchardOutputDescriptionInfoRaw() {}
    OrchardOutputDescriptionInfoRaw(
        libzcash::OrchardPaymentAddressPirate addrIn,
        CAmount valueIn,
        std::array<unsigned char, ZC_MEMO_SIZE> memoIn)
    {
        addr = addrIn;
        value = valueIn;
        memo = memoIn;
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

#define SAPLING_TREE_DEPTH 32 // From rustzcash.rs
struct myCharArray_s {
    unsigned char cArray[1 + 33 * SAPLING_TREE_DEPTH + 8];
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
    uint8_t cZip212_enabled;

    std::string fromAddress_;
    typedef struct
    {
        std::string sAddr;
        CAmount iValue;
        std::string sMemo;
    } Output_s;

    // Split MerklePath into its components that are used by the transaction builder:
    std::vector<uint64_t> alMerklePathPosition;
    std::vector<myCharArray_s> asMerklePath; // From zcash/IncrementalMerkleTree.hpp

    std::vector<SaplingSpendDescriptionInfo> saplingSpends;
    std::vector<SaplingOutputDescriptionInfo> saplingOutputs;
    std::vector<std::string> sOutputRecipients;
    std::vector<Output_s> outputs_offline;
    std::vector<CTxOut> tIns;

    std::optional<SaplingBundle> saplingBundle;
    std::optional<OrchardBundle> orchardBundle;

    std::optional<orchard::Builder> orchardBuilder;
    CAmount valueBalanceSapling = 0;
    rust::Box<sapling::Builder> saplingBuilder;
    CAmount valueBalanceOrchard = 0;

    std::optional<std::pair<uint256, libzcash::SaplingPaymentAddress>> firstSaplingSpendAddr;

    std::optional<std::pair<uint256, libzcash::SaplingPaymentAddress>> saplingChangeAddr;
    std::optional<std::pair<uint256, libzcash::OrchardPaymentAddressPirate>> orchardChangeAddr;
    std::optional<CTxDestination> tChangeAddr;
    std::optional<CScript> opReturn;

    bool AddOpRetLast(CScript& s);

public:
    std::vector<SaplingSpendDescriptionInfoRaw> rawSaplingSpends;
    std::vector<SaplingOutputDescriptionInfoRaw> rawSaplingOutputs;

    // std::vector<SaplingSpendDescriptionInfoRaw> rawOrchardSpends;
    std::vector<OrchardOutputDescriptionInfoRaw> rawOrchardOutputs;
    uint256 ovkOrchard;


    TransactionBuilder();
    TransactionBuilder(const Consensus::Params& consensusParams, int nHeight, CKeyStore* keyStore = nullptr);

    // TransactionBuilder should never be copied
    TransactionBuilder(const TransactionBuilder&) = delete;
    TransactionBuilder& operator=(const TransactionBuilder&) = delete;

    // For signing offline transactions:
    TransactionBuilder(bool fOverwintered, uint32_t nExpiryHeight, uint32_t nVersionGroupId, int32_t nVersion, int nBlockHeight, uint32_t branchId, uint8_t cZip212Enabled);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(rawSaplingSpends);
        READWRITE(rawSaplingOutputs);
        READWRITE(rawOrchardOutputs);
        READWRITE(mtx);
        READWRITE(fee);
        READWRITE(nHeight);
    }


    void SetFee(CAmount fee);
    void SetMinConfirmations(int iMinConf);

    void SetConsensus(const Consensus::Params& consensusParams);
    void SetHeight(int nHeight);
    void SetExpiryHeight(int expHeight);

    CTransaction getTransaction() { return mtx; }

    // Returns false if the anchor does not match the anchor used by
    // previously-added Sapling spends.
    bool AddSaplingSpend(
        libzcash::SaplingExtendedSpendingKey extsk,
        libzcash::SaplingNote note,
        uint256 anchor,
        libzcash::MerklePath saplingMerklePath);

    bool AddSaplingSpend_process_offline_transaction(
        libzcash::SaplingExtendedSpendingKey extsk,
        libzcash::SaplingNote note,
        uint256 anchor,
        uint64_t lMerklePathPosition,
        unsigned char* pcMerkle);

    bool AddSaplingSpend_prepare_offline_transaction(
        std::string sFromAddr,
        libzcash::SaplingNote note,
        uint256 anchor,
        uint64_t lMerklePosition,
        unsigned char* pcMerkle);

    bool AddSaplingSpendRaw(
        libzcash::SaplingPaymentAddress from,
        CAmount value,
        SaplingOutPoint op,
        libzcash::SaplingNotePlaintext notePt,
        libzcash::MerklePath saplingMerklePath);

    void AddSaplingOutput(
        uint256 ovk,
        libzcash::SaplingPaymentAddress to,
        CAmount value,
        std::array<unsigned char, ZC_MEMO_SIZE> memo = {{0}});

    void AddSaplingOutput_offline_transaction(
        // uint256 ovk,
        std::string address,
        CAmount value,
        std::array<unsigned char, ZC_MEMO_SIZE> memo = {{0}});

    void AddPaymentOutput(std::string sAddr, CAmount iValue, std::string sMemo);

    void AddSaplingOutputRaw(
        libzcash::SaplingPaymentAddress to,
        CAmount value,
        std::array<unsigned char, ZC_MEMO_SIZE> memo = {{0}});

    void ConvertRawSaplingOutput(uint256 ovk);

    // Orchard
    void setOrchardOvk(uint256 ovk);

    void AddOrchardOutput(
        const std::optional<uint256>& ovk,
        const libzcash::OrchardPaymentAddressPirate to,
        CAmount value,
        const std::optional<std::array<unsigned char, ZC_MEMO_SIZE>>& memo = {{0}});

    void AddOrchardOutputRaw(
        libzcash::OrchardPaymentAddressPirate to,
        CAmount value,
        std::array<unsigned char, ZC_MEMO_SIZE> memo = {{0}});

    void createOrchardBuilderFromRawInputs();

    // Assumes that the value correctly corresponds to the provided UTXO.
    void AddTransparentInput(COutPoint utxo, CScript scriptPubKey, CAmount value, uint32_t nSequence = 0xffffffff);

    bool AddTransparentOutput(CTxDestination& to, CAmount value);

    void AddOpRet(CScript& s);

    bool AddOpRetLast();

    void SendChangeTo(libzcash::SaplingPaymentAddress changeAddr, uint256 ovk);

    bool SendChangeTo(CTxDestination& changeAddr);

    void SetLockTime(uint32_t time) { this->mtx.nLockTime = time; }

    TransactionBuilderResult Build();
    std::string Build_offline_transaction();
};

#endif /* TRANSACTION_BUILDER_H */
