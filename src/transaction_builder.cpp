// Copyright (c) 2018 The Zcash developers
// Copyright (c) 2024-2025 The Pirate Network developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transaction_builder.h"

#include "consensus/consensus.h"
#include "consensus/upgrades.h"
#include "core_io.h" //for EncodeHexTx
#include "key_io.h"
#include "main.h"
#include "pubkey.h"
#include "rpc/protocol.h"
#include "script/sign.h"
#include "utilmoneystr.h"
#include "zcash/Note.hpp"

#include <librustzcash.h>

uint256 ProduceShieldedSignatureHash(
    uint32_t consensusBranchId,
    const CTransaction& tx,
    const std::vector<CTxOut>& allPrevOutputs,
    const sapling::UnauthorizedBundle& saplingBundle,
    const std::optional<ironwood::UnauthorizedBundle>& ironwoodBundle)
{
    CDataStream sTx(SER_NETWORK, PROTOCOL_VERSION);
    sTx << tx;

    CDataStream sAllPrevOutputs(SER_NETWORK, PROTOCOL_VERSION);
    sAllPrevOutputs << allPrevOutputs;

    const IronwoodUnauthorizedBundlePtr* ironwoodBundlePtr;
    if (ironwoodBundle.has_value()) {
        ironwoodBundlePtr = ironwoodBundle->inner.get();
    } else {
        ironwoodBundlePtr = nullptr;
    }

    auto dataToBeSigned = builder::shielded_signature_digest(
        consensusBranchId,
        {reinterpret_cast<const unsigned char*>(sTx.data()), sTx.size()},
        {reinterpret_cast<const unsigned char*>(sAllPrevOutputs.data()), sAllPrevOutputs.size()},
        saplingBundle,
        ironwoodBundlePtr);
    return uint256::FromRawBytes(dataToBeSigned);
}

namespace ironwood
{

Builder::Builder(
    bool spendsEnabled,
    bool outputsEnabled,
    uint256 anchor) : inner(nullptr, ironwood_builder_free)
{
    inner.reset(ironwood_builder_new(spendsEnabled, outputsEnabled, anchor.IsNull() ? nullptr : anchor.begin()));
}

bool Builder::AddSpendFromParts(
    const libzcash::IronwoodFullViewingKey fvk,
    const libzcash::IronwoodPaymentAddress addr,
    const CAmount value,
    const uint256 rho,
    const uint256 rseed,
    const libzcash::MerklePath ironwoodMerklePath)
{
    if (!inner) {
        throw std::logic_error("ironwood::Builder has already been used");
    }

    // Serialize Fullviewing key to pass to Rust
    CDataStream ssfvk(SER_NETWORK, PROTOCOL_VERSION);
    ssfvk << fvk;
    std::array<unsigned char, 96> fvk_t;
    std::move(ssfvk.begin(), ssfvk.end(), fvk_t.begin());

    // Serialize Payment address to pass to Rust
    CDataStream ssaddr(SER_NETWORK, PROTOCOL_VERSION);
    ssaddr << addr;
    std::array<unsigned char, 43> addr_t;
    std::move(ssaddr.begin(), ssaddr.end(), addr_t.begin());

    // Serialize Merkle Path to pass to Rust
    CDataStream ssMerklePath(SER_NETWORK, PROTOCOL_VERSION);
    ssMerklePath << ironwoodMerklePath;
    std::array<unsigned char, 1065> merklepath_t;
    std::move(ssMerklePath.begin(), ssMerklePath.end(), merklepath_t.begin());

    if (ironwood_builder_add_spend_from_parts(
            inner.get(),
            fvk_t.begin(),
            addr_t.begin(),
            value,
            rho.begin(),
            rseed.begin(),
            merklepath_t.begin())) {
        hasActions = true;
        return true;
    }
    return false;
}

bool Builder::AddOutput(
    const std::optional<uint256>& ovk,
    const libzcash::IronwoodPaymentAddress& to,
    CAmount value,
    const std::optional<libzcash::Memo>& memo)
{
    if (!inner) {
        throw std::logic_error("ironwood::Builder has already been used");
    }

    if (!ironwood_builder_add_recipient(
            inner.get(),
            ovk.has_value() ? ovk->begin() : nullptr,
            to.ToBytes().data(),
            value,
            memo.has_value() ? memo->ToBytes().data() : nullptr)) {
        return false;
    }

    hasActions = true;
    return true;
}

std::optional<UnauthorizedBundle> Builder::Build()
{
    if (!inner) {
        throw std::logic_error("ironwood::Builder has already been used");
    }

    auto bundle = ironwood_builder_build(inner.release());
    if (bundle == nullptr) {
        return std::nullopt;
    } else {
        return UnauthorizedBundle(bundle);
    }
}

std::optional<IronwoodBundle> UnauthorizedBundle::ProveAndSign(
    std::vector<libzcash::IronwoodSpendingKey> keys,
    uint256 sighash)
{
    if (!inner) {
        throw std::logic_error("ironwood::UnauthorizedBundle has already been used");
    }

    //Convert the vector of keys to a flat byte array
    std::vector<unsigned char> sks;
    sks.reserve(keys.size() * 32); // Each key is 32 bytes
    for (const auto& key : keys) {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << key.sk;
        uint256_t sk_t;
        std::move(ss.begin(), ss.end(), sk_t.begin());
        for (const auto& byte : sk_t) {
            sks.push_back(byte);
        }
    }

    // Return an error if no keys were provided
    if (sks.empty()) {
        throw std::logic_error("No spending keys provided to ProveAndSign");
    }

    // Call the Rust function to prove and sign the bundle
    auto authorizedBundle = ironwood_unauthorized_bundle_prove_and_sign(
        inner.release(),
        sks.data(),
        keys.size(),
        sighash.begin());
    if (authorizedBundle == nullptr) {
        return std::nullopt;
    } else {
        return IronwoodBundle(authorizedBundle);
    }
}

} // namespace ironwood


TransactionBuilderResult::TransactionBuilderResult(const CTransaction& tx) : maybeTx(tx) {}

TransactionBuilderResult::TransactionBuilderResult(const std::string& error) : maybeError(error) {}

bool TransactionBuilderResult::IsTx() { return maybeTx != std::nullopt; }

bool TransactionBuilderResult::IsError() { return maybeError != std::nullopt; }

CTransaction TransactionBuilderResult::GetTxOrThrow()
{
    if (maybeTx) {
        return maybeTx.value();
    } else {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to build transaction: " + GetError());
    }
}

std::string TransactionBuilderResult::GetError()
{
    if (maybeError) {
        return maybeError.value();
    } else {
        // This can only happen if isTx() is true in which case we should not call getError()
        throw std::runtime_error("getError() was called in TransactionBuilderResult, but the result was not initialized as an error.");
    }
}

TransactionBuilder::TransactionBuilder() 
{
    // Set the network the transactions will be submitted to, main, test or regtest
    strNetworkID = Params().NetworkIDString();
}

TransactionBuilder::TransactionBuilder(
    const Consensus::Params& consensusParams,
    int nHeight,
    CKeyStore* keystore) : consensusParams(consensusParams),
                           nHeight(nHeight),
                           keystore(keystore)
{
    // Create a new mutable transaction to build on
    mtx = CreateNewContextualCMutableTransaction(consensusParams, nHeight);

    // Set the consensus ID of chain to when the transaction will be submitted to the network
    consensusBranchId = CurrentEpochBranchId(nHeight, consensusParams);

    // Set the network the transactions will be submitted to, main, test or regtest
    strNetworkID = Params().NetworkIDString();
    // saplingBuilder is std::optional, defaults to nullopt.
    // Call InitializeSapling(anchor) before adding Sapling spends.
}

// Transaction Builder initialization functions
void TransactionBuilder::InitializeTransactionBuilder(const Consensus::Params& consensusParams, int nHeight)
{
    this->consensusParams = consensusParams;
    this->nHeight = nHeight;

    // Create a new mutable transaction to build on
    mtx = CreateNewContextualCMutableTransaction(consensusParams, nHeight);

    // Set the consensus ID of chain to when the transaction will be submitted to the network
    consensusBranchId = CurrentEpochBranchId(nHeight, consensusParams);

    // Set the network the transactions will be submitted to, main, test or regtest
    strNetworkID = Params().NetworkIDString();
    // saplingBuilder is std::optional, defaults to nullopt.
    // Call InitializeSapling(anchor) before adding Sapling spends.
}

void TransactionBuilder::SetFee(CAmount fee)
{
    this->fee = fee;
}

void TransactionBuilder::SetMinConfirmations(int iMinConf)
{
    this->iMinConf = iMinConf;
}

void TransactionBuilder::SetExpiryHeight(int expHeight)
{
    this->mtx.nExpiryHeight = expHeight;
}


// Validate datastream with crc16 Checksum
uint16_t TransactionBuilder::CalculateChecksum()
{
    //Serialize the current state of the transaction builder
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *this;

    //Determine size of the serialized data
    size_t s = ss.size();

    //Set begining value
    uint16_t crc = 0xFFFF;

    //Calculate the checksum without the bytes allocated for the checksum
    if (s >= 2) {
        for (size_t i = 0; i < s - 2; ++i) {
            crc ^= ss[i];
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x0001)
                    crc = (crc >> 1) ^ 0x8408; // CCITT polynomial
                else
                    crc >>= 1;
            }
        }
    }

    return crc;
}

void TransactionBuilder::SetChecksum()
{
      //Set the checksum on the transaction builder
      this->checksum = CalculateChecksum();
}

uint16_t TransactionBuilder::GetChecksum()
{
      //Get the checksum on the transaction builder
      return this->checksum;
}

bool TransactionBuilder::ValidateChecksum()
{
      //Check the checksum on the transaction builder
      if (this->checksum == CalculateChecksum()) {
          return true;
      }
      return false;
}


// Sapling

void TransactionBuilder::InitializeSapling(uint256 anchor)
{
    // Create a fresh sapling builder with the correct anchor.
    if (mtx.nVersion >= SAPLING_MIN_TX_VERSION) {
        std::array<uint8_t, 32> anchorBytes;
        std::copy(anchor.begin(), anchor.end(), anchorBytes.begin());
        saplingBuilder = std::move(sapling::new_builder(anchorBytes, false));
    }
}

bool TransactionBuilder::AddSaplingSpendRaw(
    SaplingOutPoint op,
    libzcash::SaplingPaymentAddress addr,
    CAmount value,
    uint256 rcm,
    libzcash::MerklePath saplingMerklePath,
    uint256 anchor)
{
    // Sanity check: cannot add Sapling spend to pre-Sapling transaction
    if (mtx.nVersion < SAPLING_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot add Sapling spend to pre-Sapling transaction");
    }
    
    // Consistency check: all anchors must equal the first one
    if (saplingAnchor.IsNull()) {
        // Set the anchor if not already set
        saplingAnchor = anchor;
    } else if (saplingAnchor != anchor) {
        // If the anchor is already set, it must match the new one
        return false;
    }

    vSaplingSpends.emplace_back(op, addr, value, rcm, saplingMerklePath, anchor);

    return true;
}

bool TransactionBuilder::ConvertRawSaplingSpend(libzcash::SaplingExtendedSpendingKey extsk)
{
    // Sanity check: cannot add Sapling spend to pre-Sapling transaction
    if (mtx.nVersion < SAPLING_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot add Sapling spend to pre-Sapling transaction");
    }

    //Exit early if there are no Sapling spends to process
    if (vSaplingSpends.size() == 0) {
        return true;
    }

    CDataStream ssExtSk(SER_NETWORK, PROTOCOL_VERSION);
    ssExtSk << extsk;

    // Derive the internal extended spending key. Internal (change) addresses use a
    // different nk_internal, so the zk-SNARK proof requires nsk_internal, not nsk.
    CDataStream ssIntSk(SER_NETWORK, PROTOCOL_VERSION);
    libzcash::SaplingExtendedSpendingKey xskInternal;
    if (!extsk.DeriveInternal(&xskInternal)) {
        throw std::runtime_error("TransactionBuilder: failed to derive Sapling internal spending key");
    }
    ssIntSk << xskInternal;

    libzcash::SaplingIncomingViewingKey ivk;
    extsk.ToXFVK().fvk.DeriveIVK(&ivk);
    libzcash::SaplingIncomingViewingKey ivkInternal;
    const bool haveInternalIvk = extsk.ToXFVK().DeriveIVKinternal(&ivkInternal);

    // Re-create the sapling builder with the actual anchor.
    // When called via the async wallet ops, AddSaplingSpendRaw has already
    // populated saplingAnchor. When called via rawtransaction RPC, InitializeSapling
    // was already called with the correct anchor. Recreating here ensures both
    // paths produce a builder with the correct chain anchor.
    if (!vSaplingSpends.empty() && mtx.nVersion >= SAPLING_MIN_TX_VERSION) {
        std::array<uint8_t, 32> anchorBytes;
        std::copy(saplingAnchor.begin(), saplingAnchor.end(), anchorBytes.begin());
        saplingBuilder = std::move(sapling::new_builder(anchorBytes, false));
    }

    // Consistency check: all anchors must equal the first one
    for (int i = 0; i < vSaplingSpends.size(); i++) {
        if (saplingAnchor != vSaplingSpends[i].anchor) {
            LogPrintf("TransactionBuilder cannot add Sapling Spend with differing anchor\n");
            return false;
        }

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << vSaplingSpends[i].saplingMerklePath;
        std::array<unsigned char, 1065> merkle_path;
        std::move(ss.begin(), ss.end(), merkle_path.begin());

        // Check FVK is valid for Address — try external scope first, then internal (change).
        libzcash::SaplingPaymentAddress checkAddr;
        bool addrMatched = false;
        bool isInternalSpend = false;
        if (ivk.DeriveAddress(&checkAddr, vSaplingSpends[i].addr.d) && checkAddr == vSaplingSpends[i].addr) {
            addrMatched = true;
        } else if (haveInternalIvk && ivkInternal.DeriveAddress(&checkAddr, vSaplingSpends[i].addr.d) && checkAddr == vSaplingSpends[i].addr) {
            addrMatched = true;
            isInternalSpend = true;
        }
        if (!addrMatched) {
            fprintf(stderr, "TransactionBuilder cannot add Sapling Spend with FVK that does not match Address\n");
            throw std::runtime_error("TransactionBuilder cannot add Sapling Spend with FVK that does not match Address");
        }

        // For internal (change) addresses the zk-SNARK proof requires nsk_internal,
        // not nsk. Use the pre-derived internal spending key in that case.
        auto& ssSpendSk = isInternalSpend ? ssIntSk : ssExtSk;

        // Add the spend to the sapling builder
        if (!saplingBuilder.has_value()) {
            throw std::runtime_error("TransactionBuilder cannot add Sapling spend without Sapling builder (call InitializeSapling first)");
        }
        saplingBuilder.value()->add_spend(
            {reinterpret_cast<uint8_t*>(ssSpendSk.data()), ssSpendSk.size()},
            vSaplingSpends[i].addr.ToBytes(),
            vSaplingSpends[i].value,
            vSaplingSpends[i].rcm.GetRawBytes(),
            merkle_path);

        if (!firstSaplingChangeAddr.has_value()) {
            const auto xfvk = extsk.ToXFVK();
            libzcash::SaplingPaymentAddress changeAddr;
            if (!xfvk.DefaultAddressInternal(&changeAddr)) {
                throw std::runtime_error("TransactionBuilder cannot derive internal change address from Sapling FVK");
            }
            firstSaplingChangeAddr = std::make_pair(xfvk.fvk.ovk, changeAddr);
        }

        valueBalanceSapling += vSaplingSpends[i].value;
    }

    // Accumulate committed count before clearing the staging vector
    nCommittedSaplingSpends += vSaplingSpends.size();

    // reset Spend Vector
    vSaplingSpends.resize(0);

    return true;
}

bool TransactionBuilder::AddSaplingOutputRaw(
    libzcash::SaplingPaymentAddress to,
    CAmount value,
    const std::optional<libzcash::Memo>& memo)
{
    // Sanity check: cannot add Sapling output to pre-Sapling transaction
    if (mtx.nVersion < SAPLING_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot add Sapling output to pre-Sapling transaction");
    }

    vSaplingOutputs.emplace_back(to, value, memo);

    return true;
}

bool TransactionBuilder::ConvertRawSaplingOutput(uint256 ovk)
{
    // Sanity check: cannot add Sapling output to pre-Sapling transaction
    if (mtx.nVersion < SAPLING_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot add Sapling output to pre-Sapling transaction");
    }

    //Exit early if there are no Sapling outputs to process
    if (vSaplingOutputs.size() == 0) {
        return true;
    }

    for (int i = 0; i < vSaplingOutputs.size(); i++) {
        if (!saplingBuilder.has_value()) {
            throw std::runtime_error("TransactionBuilder cannot add Sapling output without Sapling builder (call InitializeSapling first)");
        }
        auto memoBytes = libzcash::Memo::ToBytes(vSaplingOutputs[i].memo);
        saplingBuilder.value()->add_recipient(ovk.GetRawBytes(), vSaplingOutputs[i].addr.ToBytes(), vSaplingOutputs[i].value, memoBytes);
        valueBalanceSapling -= vSaplingOutputs[i].value;
    }

    // Accumulate committed count before clearing the staging vector
    nCommittedSaplingOutputs += vSaplingOutputs.size();

    // reset Output Vector
    vSaplingOutputs.resize(0);

    return true;
}

void TransactionBuilder::InitializeIronwood(
    bool spendsEnabled,
    bool outputsEnabled,
    uint256 anchor)
{
    if (mtx.nVersion < IRONWOOD_MIN_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot initialize Ironwood before Ironwood activation");
    }

    ironwoodBuilder = ironwood::Builder(spendsEnabled, outputsEnabled, anchor);
}

bool TransactionBuilder::AddIronwoodSpendRaw(
    IronwoodOutPoint op,
    libzcash::IronwoodPaymentAddress addr,
    CAmount value,
    uint256 rho,
    uint256 rseed,
    libzcash::MerklePath ironwoodMerklePath,
    uint256 anchor)
{
    if (mtx.nVersion < IRONWOOD_MIN_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot add Ironwood Spend before Ironwood activation");
    }

    // Consistency check: all anchors must equal the first one
    if (ironwoodAnchor.IsNull()) {
        // Set the anchor if not already set
        ironwoodAnchor = anchor;
    } else if (ironwoodAnchor != anchor) {
        // If the anchor is already set, it must match the new one
        return false;
    }

    vIronwoodSpends.emplace_back(op, addr, value, rho, rseed, ironwoodMerklePath, anchor);

    return true;
}

bool TransactionBuilder::ConvertRawIronwoodSpend(libzcash::IronwoodExtendedSpendingKeyPirate extsk)
{
    //Exit early if there are no Ironwood spends to process
    if (vIronwoodSpends.size() == 0) {
        return true;
    }

    // Sanity check: cannot add Ironwood Spend to pre-Ironwood transaction
    if (!ironwoodBuilder.has_value()) {
        // Try to give a useful error.
        if (mtx.nVersion < IRONWOOD_MIN_TX_VERSION) {
            throw std::runtime_error("TransactionBuilder cannot add Ironwood Spend before Ironwood activation");
        } else {
            throw std::runtime_error("TransactionBuilder cannot add Ironwood Spend without Ironwood Builder");
        }
    }

    auto fvkOpt = extsk.GetXFVK();
    if (fvkOpt == std::nullopt) {
        throw std::runtime_error("TransactionBuilder cannot get XFVK from EXTSK");
    }
    auto fvk = fvkOpt.value().fvk;

    if (!firstIronwoodChangeAddr.has_value()) {
        libzcash::IronwoodOutgoingViewingKey ovk;
        if (!fvk.DeriveOVK(&ovk)) {
            throw std::runtime_error("TransactionBuilder cannot get ovk from FVK");
        }

        libzcash::IronwoodPaymentAddress changeAddr;
        if (!fvk.DeriveDefaultAddressInternal(&changeAddr)) {
            throw std::runtime_error("TransactionBuilder cannot get default internal address from FVK");
        }

        firstIronwoodChangeAddr = std::make_pair(ovk.ovk, changeAddr);
    }

    for (int i = 0; i < vIronwoodSpends.size(); i++) {
        if (ironwoodAnchor != vIronwoodSpends[i].anchor) {
            return false;
        }

        // Check FVK is valid for Address — try external scope first, then internal (change).
        libzcash::IronwoodPaymentAddress checkAddr;
        bool addrMatched = false;
        if (fvk.DeriveAddress(&checkAddr, vIronwoodSpends[i].addr.d) && checkAddr == vIronwoodSpends[i].addr) {
            addrMatched = true;
        } else if (fvk.DeriveAddressInternal(&checkAddr, vIronwoodSpends[i].addr.d) && checkAddr == vIronwoodSpends[i].addr) {
            addrMatched = true;
        }
        if (!addrMatched) {
            fprintf(stderr, "Note Address %s\n", EncodePaymentAddress(vIronwoodSpends[i].addr).c_str());
            fprintf(stderr, "TransactionBuilder cannot add Ironwood Spend with FVK that does not match Address\n");
            throw std::runtime_error("TransactionBuilder cannot add Ironwood Spend with FVK that does not match Address");
        }

        if (!ironwoodBuilder->AddSpendFromParts(
                fvk,
                vIronwoodSpends[i].addr,
                vIronwoodSpends[i].value,
                vIronwoodSpends[i].rho,
                vIronwoodSpends[i].rseed,
                vIronwoodSpends[i].ironwoodMerklePath)) {
            throw std::runtime_error("TransactionBuilder: ironwood_builder_add_spend_from_parts failed");
        }

        valueBalanceIronwood += vIronwoodSpends[i].value;
        LogPrintf("Adding ironwood spend value %i\n", vIronwoodSpends[i].value);
        LogPrintf("Adding total ironwood value %i\n", valueBalanceIronwood);
    }

    // Add to list of keys to sign bundle
    ironwoodSpendingKeys.push_back(extsk.sk);

    // Accumulate committed count before clearing the staging vector
    nCommittedIronwoodSpends += vIronwoodSpends.size();

    // reset spend vector
    vIronwoodSpends.resize(0);

    return true;
}

bool TransactionBuilder::AddIronwoodOutputRaw(
    libzcash::IronwoodPaymentAddress to,
    CAmount value,
    const std::optional<libzcash::Memo>& memo)
{
    // Try to give a useful error.
    if (mtx.nVersion < IRONWOOD_MIN_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot add Ironwood Output before Ironwood activation");
    }

    vIronwoodOutputs.emplace_back(to, value, memo);

    return true;
}

bool TransactionBuilder::ConvertRawIronwoodOutput(uint256 ovk)
{
    // If there are no Ironwood outputs to process, return early
    if (vIronwoodOutputs.size() == 0) {
        return true;
    }

    // Sanity check: cannot add Ironwood output to pre-Ironwood transaction
    if (!ironwoodBuilder.has_value()) {
        // Try to give a useful error.
        if (mtx.nVersion < IRONWOOD_MIN_TX_VERSION) {
            throw std::runtime_error("TransactionBuilder cannot add Ironwood Output before Ironwood activation");
        } else {
            throw std::runtime_error("TransactionBuilder cannot add Ironwood Output without Ironwood Builder");
        }
    }

    for (int i = 0; i < vIronwoodOutputs.size(); i++) {
        if (!ironwoodBuilder->AddOutput(ovk, vIronwoodOutputs[i].addr, vIronwoodOutputs[i].value, vIronwoodOutputs[i].memo)) {
            return false;
        }

        valueBalanceIronwood -= vIronwoodOutputs[i].value;
    }

    // Accumulate committed count before clearing the staging vector
    nCommittedIronwoodOutputs += vIronwoodOutputs.size();

    // reset Output Vector
    vIronwoodOutputs.resize(0);

    return true;
}

void TransactionBuilder::AddTransparentInput(COutPoint utxo, CScript scriptPubKey, CAmount value, uint32_t _nSequence)
{
    if (keystore == nullptr) {
        if (!scriptPubKey.IsPayToCryptoCondition()) {
            throw std::runtime_error("Cannot add transparent inputs to a TransactionBuilder without a keystore, except with crypto conditions");
        }
    }

    mtx.vin.emplace_back(utxo);
    mtx.vin[mtx.vin.size() - 1].nSequence = _nSequence;
    tIns.emplace_back(value, scriptPubKey);
}

bool TransactionBuilder::AddTransparentOutput(CTxDestination& to, CAmount value)
{
    if (!IsValidDestination(to)) {
        return false;
    }

    CScript scriptPubKey = GetScriptForDestination(to);
    CTxOut out(value, scriptPubKey);
    mtx.vout.push_back(out);

    return true;
}

bool TransactionBuilder::AddOpRetLast()
{
    CScript s;
    if (opReturn) {
        s = opReturn.value();
        CTxOut out(0, s);
        mtx.vout.push_back(out);
    }
    return true;
}

void TransactionBuilder::AddOpRet(CScript& s)
{
    opReturn.emplace(CScript(s));
}


void TransactionBuilder::SendChangeTo(libzcash::IronwoodPaymentAddress changeAddr, uint256 ovk)
{
    ironwoodChangeAddr = std::make_pair(ovk, changeAddr);
    tChangeAddr = std::nullopt;
    saplingChangeAddr = std::nullopt;
}

void TransactionBuilder::SendChangeTo(libzcash::SaplingPaymentAddress changeAddr, uint256 ovk)
{
    saplingChangeAddr = std::make_pair(ovk, changeAddr);
    tChangeAddr = std::nullopt;
    ironwoodChangeAddr = std::nullopt;
}

bool TransactionBuilder::SendChangeTo(CTxDestination& changeAddr)
{
    if (!IsValidDestination(changeAddr)) {
        return false;
    }

    tChangeAddr = changeAddr;
    saplingChangeAddr = std::nullopt;
    ironwoodChangeAddr = std::nullopt;

    return true;
}

// Returns true if the estimated transaction size fits within 95% of the protocol limit.
// Transparent base is measured by serializing mtx (scriptSigs absent) + 107 bytes/input.
// Ironwood proof model (empirical, verified n=2,4,39,85): proof = 2720 + 2272*n bytes.
// Proof-length varint is 3 bytes for n<28, 5 bytes for n>=28 (proof exceeds 65535).
// extraOutputs budgets for outputs not yet queued (e.g. change), routed to the active pool.
bool TransactionBuilder::IsValidSize(int extraOutputs) const
{
    // ZIP-225 component sizes
    static constexpr size_t SAPLING_SPEND_SIZE           = 384;
    static constexpr size_t SAPLING_OUTPUT_SIZE          = 948;
    static constexpr size_t SAPLING_BUNDLE_OVERHEAD      = 104;
    static constexpr size_t IRONWOOD_ACTION_SIZE          = 884;
    // Halo2 proof: verified exact at n=2,4,39,85: proof = 2720 + 2272*n
    static constexpr size_t IRONWOOD_PROOF_BASE           = 2720;
    static constexpr size_t IRONWOOD_PROOF_PER_ACTION     = 2272;
    static constexpr size_t IRONWOOD_BUNDLE_OVERHEAD_BASE = 106; // flags+valueBalance+anchor+bindingSig+varint
    static constexpr size_t IRONWOOD_LARGE_PROOF_THRESHOLD = 28; // proof >65535 bytes at n>=28
    static constexpr size_t P2PKH_SCRIPTSIG_SIZE         = 107;

    // Transparent: serialize mtx (empty scriptSigs) + estimate scriptSig bytes
    const size_t nTransparentInputs  = tIns.size();
    const size_t nTransparentOutputs = mtx.vout.size();
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << CTransaction(mtx);
    const size_t transparentBase = ss.size() + nTransparentInputs * P2PKH_SCRIPTSIG_SIZE;

    // Shielded counts: committed (already in Rust builder) + pending in staging vectors
    const size_t nSaplingSpends      = nCommittedSaplingSpends  + vSaplingSpends.size();
    const size_t nSaplingOutputsBase = nCommittedSaplingOutputs + vSaplingOutputs.size();
    const size_t nIronwoodSpends      = nCommittedIronwoodSpends  + vIronwoodSpends.size();
    const size_t nIronwoodOutputsBase = nCommittedIronwoodOutputs + vIronwoodOutputs.size();

    const bool ironwoodPoolActive = (nIronwoodSpends > 0 || nIronwoodOutputsBase > 0);
    const size_t nSaplingOutputs = nSaplingOutputsBase + (ironwoodPoolActive ? 0 : static_cast<size_t>(extraOutputs));
    const size_t nIronwoodOutputs = nIronwoodOutputsBase + (ironwoodPoolActive ? static_cast<size_t>(extraOutputs) : 0);
    const size_t nIronwoodActions = std::max(nIronwoodSpends, nIronwoodOutputs);

    size_t estimate = transparentBase;

    if (nSaplingSpends > 0 || nSaplingOutputs > 0) {
        estimate += nSaplingSpends  * SAPLING_SPEND_SIZE
                  + nSaplingOutputs * SAPLING_OUTPUT_SIZE
                  + SAPLING_BUNDLE_OVERHEAD;
    }

    size_t ironwoodProofEst = 0;
    size_t proofVarintSize = 0;
    if (nIronwoodActions > 0) {
        ironwoodProofEst = IRONWOOD_PROOF_BASE + nIronwoodActions * IRONWOOD_PROOF_PER_ACTION;
        proofVarintSize = (nIronwoodActions >= IRONWOOD_LARGE_PROOF_THRESHOLD) ? 5 : 3;
        estimate += nIronwoodActions * IRONWOOD_ACTION_SIZE
                  + ironwoodProofEst + proofVarintSize
                  + IRONWOOD_BUNDLE_OVERHEAD_BASE;
    }

    const size_t maxTxSize = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UPGRADE_SAPLING)
        ? MAX_TX_SIZE_AFTER_SAPLING : MAX_TX_SIZE_BEFORE_SAPLING;
    const size_t limit = maxTxSize - maxTxSize / 20;

    // LogPrintf("IsValidSize: transparent_base=%u (serialized=%u scriptsig_est=%u inputs=%u outputs=%u) "
    //           "sapling_spends=%u sapling_outputs=%u "
    //           "ironwood_spends=%u ironwood_outputs=%u ironwood_actions=%u "
    //           "ironwood_proof_est=%u varint=%u "
    //           "estimated_total=%u limit=%u max=%u valid=%s\n",
    //     transparentBase,
    //     transparentBase - nTransparentInputs * P2PKH_SCRIPTSIG_SIZE,
    //     nTransparentInputs * P2PKH_SCRIPTSIG_SIZE,
    //     nTransparentInputs, nTransparentOutputs, nSaplingSpends, nSaplingOutputs,
    //     nIronwoodSpends, nIronwoodOutputs, nIronwoodActions,
    //     ironwoodProofEst, proofVarintSize,
    //     estimate, limit, maxTxSize,
    //     (estimate <= limit ? "YES" : "NO"));

    return estimate <= limit;
}

TransactionBuilderResult TransactionBuilder::Build()
{
    //
    // Consistency checks
    //

    // Guard: staging vectors must have been flushed via Convert* before Build().
    if (!vSaplingSpends.empty() || !vSaplingOutputs.empty() ||
        !vIronwoodSpends.empty()  || !vIronwoodOutputs.empty()) {
        return TransactionBuilderResult(
            "TransactionBuilder::Build called with unconverted staging spends/outputs; "
            "call ConvertRaw* before Build()");
    }

    // Clear Pending Inputs and Outputs
    vSaplingSpends.resize(0);
    vSaplingOutputs.resize(0);
    vIronwoodSpends.resize(0);
    vIronwoodOutputs.resize(0);

    // Validate change
    CAmount change = valueBalanceSapling + valueBalanceIronwood - fee;

    for (auto tIn : tIns) {
        change += tIn.nValue;
    }
    for (auto tOut : mtx.vout) {
        change -= tOut.nValue;
    }
    if (change < 0) {
        return TransactionBuilderResult("Change cannot be negative");
    }

    //
    // Change output
    //
    if (change > 0) {
        // Send change to the specified change address.
        if (ironwoodChangeAddr) {
            AddIronwoodOutputRaw(ironwoodChangeAddr->second, change, std::nullopt);
            ConvertRawIronwoodOutput(ironwoodChangeAddr->first);
        } else if (saplingChangeAddr) {
            AddSaplingOutputRaw(saplingChangeAddr->second, change, std::nullopt);
            ConvertRawSaplingOutput(saplingChangeAddr->first);
        } else if (tChangeAddr) {
            assert(AddTransparentOutput(tChangeAddr.value(), change));

        // If no change address was set, use the first Sapling or Ironwood address
        } else if (firstIronwoodChangeAddr) {
            AddIronwoodOutputRaw(firstIronwoodChangeAddr->second, change, std::nullopt);
            ConvertRawIronwoodOutput(firstIronwoodChangeAddr->first);

        } else if (firstSaplingChangeAddr) {
            AddSaplingOutputRaw(firstSaplingChangeAddr->second, change, std::nullopt);
            ConvertRawSaplingOutput(firstSaplingChangeAddr->first);
        
        } else {
            return TransactionBuilderResult("Could not determine change address");
        }
    }

    //
    // Sapling spends and outputs
    //
    // If no Sapling spends or outputs were added, initialize an empty builder
    // so we can produce a valid (empty) Sapling bundle for the ZIP 244 sighash.
    if (!saplingBuilder.has_value()) {
        saplingBuilder = std::move(sapling::new_builder({}, false));
    }
    std::optional<rust::Box<sapling::UnauthorizedBundle>> maybeSaplingBundle;
    try {
        maybeSaplingBundle = sapling::build_bundle(std::move(saplingBuilder.value()));
    } catch (rust::Error e) {
        return TransactionBuilderResult("Failed to build Sapling bundle: " + std::string(e.what()));
    }
    
    if (!maybeSaplingBundle.has_value()) {
        return TransactionBuilderResult("Failed to build Sapling bundle: no bundle returned");
    }
    auto saplingBundle = std::move(maybeSaplingBundle);

    //
    // Ironwood
    //
    std::optional<ironwood::UnauthorizedBundle> ironwoodBundle;
    if (ironwoodBuilder.has_value() && ironwoodBuilder->HasActions()) {
        auto bundle = ironwoodBuilder->Build();
        if (bundle.has_value()) {
            ironwoodBundle = std::move(bundle);
        } else {
            return TransactionBuilderResult("Failed to build Ironwood bundle");
        }
    }

    // add op_return if there is one to add
    AddOpRetLast();

    //
    // Signatures
    //
    auto consensusBranchId = CurrentEpochBranchId(nHeight, consensusParams);

    // Empty output script.
    uint256 dataToBeSigned;
    try {
        if (mtx.fOverwintered) {
            // ProduceShieldedSignatureHash is only usable with v3+ transactions.
            dataToBeSigned = ProduceShieldedSignatureHash(
                consensusBranchId,
                mtx,
                tIns,
                *saplingBundle.value(),
                ironwoodBundle);
        } else {
            CScript scriptCode;
            const PrecomputedTransactionData txdata(mtx, tIns);
            dataToBeSigned = SignatureHash(scriptCode, mtx, NOT_AN_INPUT, SIGHASH_ALL, 0, consensusBranchId, txdata);
        }
    } catch (std::ios_base::failure ex) {
        return TransactionBuilderResult("Could not construct signature hash: " + std::string(ex.what()));
    } catch (std::logic_error ex) {
        return TransactionBuilderResult("Could not construct signature hash: " + std::string(ex.what()));
    }

    if (ironwoodBundle.has_value()) {
        // Populate a random key when not spending from ironwood.
        if (ironwoodSpendingKeys.size() == 0) {
            auto randomKey = libzcash::IronwoodSpendingKey().random();
            if (randomKey != std::nullopt) {
                ironwoodSpendingKeys.push_back(randomKey.value());
            } else {
                return TransactionBuilderResult("Failed to generate random Ironwood spending key");
            }
        }
        auto authorizedBundle = ironwoodBundle.value().ProveAndSign(
            ironwoodSpendingKeys,
            dataToBeSigned);
        if (authorizedBundle.has_value()) {
            mtx.ironwoodBundle = std::move(authorizedBundle.value());
        } else {
            return TransactionBuilderResult("Failed to create Ironwood proof or signatures");
        }
    }

    // Create Sapling spendAuth and binding signatures
    try {
        mtx.saplingBundle = sapling::apply_bundle_signatures(
            std::move(saplingBundle.value()), dataToBeSigned.GetRawBytes());
    } catch (rust::Error e) {
        return TransactionBuilderResult(e.what());
    }

    // Transparent signatures
    CTransaction txNewConst(mtx);
    const PrecomputedTransactionData txdata(txNewConst, tIns);
    for (int nIn = 0; nIn < mtx.vin.size(); nIn++) {
        auto tIn = tIns[nIn];
        SignatureData sigdata;
        bool signSuccess = ProduceSignature(
            TransactionSignatureCreator(
                keystore, &txNewConst, txdata, nIn, tIn.nValue, SIGHASH_ALL),
            tIn.scriptPubKey, sigdata, consensusBranchId);

        if (!signSuccess) {
            return TransactionBuilderResult("Failed to sign transaction");
        } else {
            UpdateTransaction(mtx, nIn, sigdata);
        }
    }

    return CTransaction(mtx);
}
