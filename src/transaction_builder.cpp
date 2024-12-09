// Copyright (c) 2018 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transaction_builder.h"

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
    const std::optional<orchard::UnauthorizedBundle>& orchardBundle)
{
    CDataStream sTx(SER_NETWORK, PROTOCOL_VERSION);
    sTx << tx;

    CDataStream sAllPrevOutputs(SER_NETWORK, PROTOCOL_VERSION);
    sAllPrevOutputs << allPrevOutputs;

    const OrchardUnauthorizedBundlePtr* orchardBundlePtr;
    if (orchardBundle.has_value()) {
        orchardBundlePtr = orchardBundle->inner.get();
    } else {
        orchardBundlePtr = nullptr;
    }

    auto dataToBeSigned = builder::shielded_signature_digest(
        consensusBranchId,
        {reinterpret_cast<const unsigned char*>(sTx.data()), sTx.size()},
        {reinterpret_cast<const unsigned char*>(sAllPrevOutputs.data()), sAllPrevOutputs.size()},
        saplingBundle,
        orchardBundlePtr);
    return uint256::FromRawBytes(dataToBeSigned);
}

namespace orchard
{

Builder::Builder(
    bool spendsEnabled,
    bool outputsEnabled,
    uint256 anchor) : inner(nullptr, orchard_builder_free)
{
    inner.reset(orchard_builder_new(spendsEnabled, outputsEnabled, anchor.IsNull() ? nullptr : anchor.begin()));
}

bool Builder::AddSpendFromParts(
    const libzcash::OrchardFullViewingKeyPirate fvk,
    const libzcash::OrchardPaymentAddressPirate addr,
    const CAmount value,
    const uint256 rho,
    const uint256 rseed,
    const libzcash::MerklePath orchardMerklePath)
{
    if (!inner) {
        throw std::logic_error("orchard::Builder has already been used");
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
    ssMerklePath << orchardMerklePath;
    std::array<unsigned char, 1065> merklepath_t;
    std::move(ssMerklePath.begin(), ssMerklePath.end(), merklepath_t.begin());

    if (orchard_builder_add_spend_from_parts(
            inner.get(),
            fvk_t.begin(),
            addr_t.begin(),
            value,
            rho.begin(),
            rseed.begin(),
            merklepath_t.begin())) {
        hasActions = true;
        return true;
    } else {
        return false;
    }
    return false;
}

bool Builder::AddOutput(
    const std::optional<uint256>& ovk,
    const libzcash::OrchardPaymentAddressPirate& to,
    CAmount value,
    const std::array<unsigned char, ZC_MEMO_SIZE> memo)
{
    if (!inner) {
        throw std::logic_error("orchard::Builder has already been used");
    }

    if (!orchard_builder_add_recipient(
            inner.get(),
            ovk.has_value() ? ovk->begin() : nullptr,
            to.ToBytes().data(),
            value,
            memo.begin())) {
        return false;
    }

    hasActions = true;
    return true;
}

std::optional<UnauthorizedBundle> Builder::Build()
{
    if (!inner) {
        throw std::logic_error("orchard::Builder has already been used");
    }

    auto bundle = orchard_builder_build(inner.release());
    if (bundle == nullptr) {
        return std::nullopt;
    } else {
        return UnauthorizedBundle(bundle);
    }
}

std::optional<OrchardBundle> UnauthorizedBundle::ProveAndSign(
    libzcash::OrchardSpendingKeyPirate key,
    uint256 sighash)
{
    if (!inner) {
        throw std::logic_error("orchard::UnauthorizedBundle has already been used");
    }

    auto authorizedBundle = orchard_unauthorized_bundle_prove_and_sign(
        inner.release(),
        key.sk.begin(),
        sighash.begin());
    if (authorizedBundle == nullptr) {
        return std::nullopt;
    } else {
        return OrchardBundle(authorizedBundle);
    }
}

} // namespace orchard


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

TransactionBuilder::TransactionBuilder() : saplingBuilder(sapling::new_builder(*RustNetwork(), 1))
{
    // Set the network the transactions will be submitted to, main, test or regtest
    strNetworkID = Params().NetworkIDString();
}

TransactionBuilder::TransactionBuilder(
    const Consensus::Params& consensusParams,
    int nHeight,
    CKeyStore* keystore) : consensusParams(consensusParams),
                           nHeight(nHeight),
                           keystore(keystore),
                           saplingBuilder(sapling::new_builder(*RustNetwork(), nHeight))
{
    // Create a new mutable transaction to build on
    mtx = CreateNewContextualCMutableTransaction(consensusParams, nHeight);

    // Set the consensus ID of chain to when the transaction will be submitted to the network
    consensusBranchId = CurrentEpochBranchId(nHeight, consensusParams);

    // Set the network the transactions will be submitted to, main, test or regtest
    strNetworkID = Params().NetworkIDString();

    // Create a fresh sapling builder with the correct info.
    if (mtx.nVersion >= SAPLING_MIN_TX_VERSION) {
        saplingBuilder = std::move(sapling::new_builder(*RustNetwork(), nHeight));
    }
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

    // Create a fresh sapling builder with the correct info.
    if (mtx.nVersion >= SAPLING_MIN_TX_VERSION) {
        saplingBuilder = std::move(sapling::new_builder(*RustNetwork(), nHeight));
    }
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

    //Determine size of the unsigned char* array
    size_t s = sizeof(ss);

    //Set begining value
    uint16_t crc = 0xFFFF;

    //Calculate the checksum without the bytes allocated for the checksum
    for (size_t i = 0; i < s - 2; ++i) {
        crc ^= ss[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0x8408; // CCITT polynomial
            else
                crc >>= 1;
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

void TransactionBuilder::InitializeSapling()
{
    // Create a fresh sapling builder with the correct info.
    if (mtx.nVersion >= SAPLING_MIN_TX_VERSION) {
        saplingBuilder = std::move(sapling::new_builder(*RustNetwork(), nHeight));
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
    // Consistency check: all from addresses must equal the first one
    if (!vSaplingSpends.empty()) {
        if (!(vSaplingSpends[0].addr == addr)) {
            return false;
        }
    }

    // Consistency check: all anchors must equal the first one
    if (!vSaplingSpends.empty()) {
        if (!(vSaplingSpends[0].anchor == anchor)) {
            return false;
        }
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

    CDataStream ssExtSk(SER_NETWORK, PROTOCOL_VERSION);
    ssExtSk << extsk;

    // Consistency check: all anchors must equal the first one
    for (int i = 0; i < vSaplingSpends.size(); i++) {
        if (vSaplingSpends[0].anchor != vSaplingSpends[i].anchor) {
            return false;
        }

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << vSaplingSpends[i].saplingMerklePath;
        std::array<unsigned char, 1065> merkle_path;
        std::move(ss.begin(), ss.end(), merkle_path.begin());

        saplingBuilder->add_spend(
            {reinterpret_cast<uint8_t*>(ssExtSk.data()), ssExtSk.size()},
            vSaplingSpends[i].addr.d,
            vSaplingSpends[i].addr.GetRawBytes(),
            vSaplingSpends[i].value,
            vSaplingSpends[i].rcm.GetRawBytes(),
            merkle_path);

        if (!firstSaplingSpendAddr.has_value()) {
            firstSaplingSpendAddr = std::make_pair(extsk.ToXFVK().fvk.ovk, vSaplingSpends[i].addr);
        }

        valueBalanceSapling += vSaplingSpends[i].value;
        LogPrintf("Adding sapling spend value %i\n", vSaplingSpends[i].value);
        LogPrintf("Adding total orchard value %i\n", valueBalanceSapling);
    }

    std::optional<CTransaction> maybe_tx = CTransaction(mtx);
    auto tx_result = maybe_tx.value();
    auto signedtxn = EncodeHexTx(tx_result);

    // reset Spend Vector
    vSaplingSpends.resize(0);

    return true;
}

bool TransactionBuilder::AddSaplingOutputRaw(
    libzcash::SaplingPaymentAddress to,
    CAmount value,
    std::array<unsigned char, ZC_MEMO_SIZE> memo)
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

    for (int i = 0; i < vSaplingOutputs.size(); i++) {
        saplingBuilder->add_recipient(ovk.GetRawBytes(), vSaplingOutputs[i].addr.GetRawBytes(), vSaplingOutputs[i].value, vSaplingOutputs[i].memo);
        valueBalanceSapling -= vSaplingOutputs[i].value;
        LogPrintf("Adding sapling spend value %i\n", vSaplingOutputs[i].value);
        LogPrintf("Adding total orchard value %i\n", valueBalanceSapling);
    }

    // reset Output Vector
    vSaplingOutputs.resize(0);

    return true;
}

void TransactionBuilder::InitializeOrchard(
    bool spendsEnabled,
    bool outputsEnabled,
    uint256 anchor)
{
    if (mtx.nVersion < ORCHARD_MIN_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot initialize Orchard before Orchard activation");
    }

    orchardBuilder = orchard::Builder(spendsEnabled, outputsEnabled, anchor);
}

bool TransactionBuilder::AddOrchardSpendRaw(
    OrchardOutPoint op,
    libzcash::OrchardPaymentAddressPirate addr,
    CAmount value,
    uint256 rho,
    uint256 rseed,
    libzcash::MerklePath orchardMerklePath,
    uint256 anchor)
{
    if (mtx.nVersion < ORCHARD_MIN_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot add Orchard Spend before Orchard activation");
    }

    // Consistency check: all from addresses must equal the first one
    if (!vOrchardSpends.empty()) {
        if (!(vOrchardSpends[0].addr == addr)) {
            return false;
        }
    }

    // Consistency check: all anchors must equal the first one
    if (!vOrchardSpends.empty()) {
        if (!(vOrchardSpends[0].anchor == anchor)) {
            return false;
        }
    }

    vOrchardSpends.emplace_back(op, addr, value, rho, rseed, orchardMerklePath, anchor);

    return true;
}

bool TransactionBuilder::ConvertRawOrchardSpend(libzcash::OrchardExtendedSpendingKeyPirate extsk)
{
    if (!orchardBuilder.has_value()) {
        // Try to give a useful error.
        if (mtx.nVersion < ORCHARD_MIN_TX_VERSION) {
            throw std::runtime_error("TransactionBuilder cannot add Orchard Spend before Orchard activation");
        } else {
            throw std::runtime_error("TransactionBuilder cannot add Orchard Spend without Orchard Builder");
        }
    }

    auto fvkOpt = extsk.GetXFVK();
    if (fvkOpt == std::nullopt) {
        throw std::runtime_error("TransactionBuilder cannot get XFVK from EXTSK");
    }
    auto fvk = fvkOpt.value().fvk;

    if (!firstOrchardSpendAddr.has_value()) {
        auto ovkOpt = fvk.GetOVK();
        if (ovkOpt == std::nullopt) {
            throw std::runtime_error("TransactionBuilder cannot get ovk from FVK");
        }
        auto ovk = ovkOpt.value();

        auto changeAddrOpt = fvk.GetDefaultAddress();
        if (changeAddrOpt == std::nullopt) {
            throw std::runtime_error("TransactionBuilder cannot get default address from FVK");
        }
        auto changeAddr = changeAddrOpt.value();

        firstOrchardSpendAddr = std::make_pair(ovk.ovk, changeAddr);
    }

    for (int i = 0; i < vOrchardSpends.size(); i++) {
        if (vOrchardSpends[0].anchor != vOrchardSpends[i].anchor) {
            return false;
        }

        orchardBuilder.value().AddSpendFromParts(
            fvk,
            vOrchardSpends[i].addr,
            vOrchardSpends[i].value,
            vOrchardSpends[i].rho,
            vOrchardSpends[i].rseed,
            vOrchardSpends[i].orchardMerklePath);

        valueBalanceOrchard += vOrchardSpends[i].value;
        LogPrintf("Adding orchard spend value %i\n", vOrchardSpends[i].value);
        LogPrintf("Adding total orchard value %i\n", valueBalanceOrchard);
    }

    // Add to list of keys to sign bundle
    orchardSpendingKeys.push_back(extsk.sk);

    // reset spend vector
    vOrchardSpends.resize(0);

    return true;
}

bool TransactionBuilder::AddOrchardOutputRaw(
    libzcash::OrchardPaymentAddressPirate to,
    CAmount value,
    std::array<unsigned char, ZC_MEMO_SIZE> memo)
{
    // Try to give a useful error.
    if (mtx.nVersion < ORCHARD_MIN_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot add Orchard Output before Orchard activation");
    }

    vOrchardOutputs.emplace_back(to, value, memo);

    return true;
}

bool TransactionBuilder::ConvertRawOrchardOutput(uint256 ovk)
{
    // Sanity check: cannot add Sapling output to pre-Sapling transaction
    if (!orchardBuilder.has_value()) {
        // Try to give a useful error.
        if (mtx.nVersion < ORCHARD_MIN_TX_VERSION) {
            throw std::runtime_error("TransactionBuilder cannot add Orchard Output before Orchard activation");
        } else {
            throw std::runtime_error("TransactionBuilder cannot add Orchard Output without Orchard Builder");
        }
    }

    for (int i = 0; i < vOrchardOutputs.size(); i++) {
        if (!orchardBuilder.value().AddOutput(ovk, vOrchardOutputs[i].addr, vOrchardOutputs[i].value, vOrchardOutputs[i].memo)) {
            return false;
        }

        valueBalanceOrchard -= vOrchardOutputs[i].value;
        LogPrintf("Adding orchard output value %i\n", vOrchardOutputs[i].value);
        LogPrintf("Adding total orchard value %i\n", valueBalanceOrchard);
    }

    // reset Output Vector
    vOrchardOutputs.resize(0);

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


void TransactionBuilder::SendChangeTo(libzcash::OrchardPaymentAddressPirate changeAddr, uint256 ovk)
{
    orchardChangeAddr = std::make_pair(ovk, changeAddr);
    tChangeAddr = std::nullopt;
    saplingChangeAddr = std::nullopt;
}

void TransactionBuilder::SendChangeTo(libzcash::SaplingPaymentAddress changeAddr, uint256 ovk)
{
    saplingChangeAddr = std::make_pair(ovk, changeAddr);
    tChangeAddr = std::nullopt;
    orchardChangeAddr = std::nullopt;
}

bool TransactionBuilder::SendChangeTo(CTxDestination& changeAddr)
{
    if (!IsValidDestination(changeAddr)) {
        return false;
    }

    tChangeAddr = changeAddr;
    saplingChangeAddr = std::nullopt;
    orchardChangeAddr = std::nullopt;

    return true;
}

TransactionBuilderResult TransactionBuilder::Build()
{
    std::optional<CTransaction> maybe_tx = CTransaction(mtx);
    auto tx_result = maybe_tx.value();
    auto signedtxn = EncodeHexTx(tx_result);

    //
    // Consistency checks
    //

    // Clear Pending Inputs and Outputs
    vSaplingSpends.resize(0);
    vSaplingOutputs.resize(0);
    vOrchardSpends.resize(0);
    vOrchardOutputs.resize(0);

    // Validate change
    CAmount change = valueBalanceSapling + valueBalanceOrchard - fee;

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
        // Send change to the specified change address. If no change address
        // was set, send change to the first Sapling address given as input.
        if (orchardChangeAddr) {
            AddOrchardOutputRaw(orchardChangeAddr->second, change, {{0}});
            ConvertRawOrchardOutput(orchardChangeAddr->first);
        } else if (firstOrchardSpendAddr) {
            AddOrchardOutputRaw(firstOrchardSpendAddr->second, change, {{0}});
            ConvertRawOrchardOutput(firstOrchardSpendAddr->first);
        } else if (saplingChangeAddr) {
            AddSaplingOutputRaw(saplingChangeAddr->second, change, {{0}});
            ConvertRawSaplingOutput(saplingChangeAddr->first);
        } else if (firstSaplingSpendAddr) {
            AddSaplingOutputRaw(firstSaplingSpendAddr->second, change, {{0}});
            ConvertRawSaplingOutput(firstSaplingSpendAddr->first);
        } else if (tChangeAddr) {
            // tChangeAddr has already been validated.
            assert(AddTransparentOutput(tChangeAddr.value(), change));
        } else {
            return TransactionBuilderResult("Could not determine change address");
        }
    }

    //
    // Sapling spends and outputs
    //
    std::optional<rust::Box<sapling::UnauthorizedBundle>> maybeSaplingBundle;
    try {
        maybeSaplingBundle = sapling::build_bundle(std::move(saplingBuilder), nHeight);
    } catch (rust::Error e) {
        return TransactionBuilderResult("Failed to build Sapling bundle: " + std::string(e.what()));
    }
    auto saplingBundle = std::move(maybeSaplingBundle.value());

    //
    // Orchard
    //
    std::optional<orchard::UnauthorizedBundle> orchardBundle;
    if (orchardBuilder.has_value() && orchardBuilder->HasActions()) {
        auto bundle = orchardBuilder->Build();
        if (bundle.has_value()) {
            orchardBundle = std::move(bundle);
        } else {
            return TransactionBuilderResult("Failed to build Orchard bundle");
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
                *saplingBundle,
                orchardBundle);
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

    if (orchardBundle.has_value()) {
        // Populate a random key when not spending from orchard.
        if (orchardSpendingKeys.size() == 0) {
            auto randomKey = libzcash::OrchardSpendingKeyPirate().random();
            if (randomKey != std::nullopt) {
                orchardSpendingKeys.push_back(randomKey.value());
            }
        }
        auto authorizedBundle = orchardBundle.value().ProveAndSign(
            orchardSpendingKeys[0],
            dataToBeSigned);
        if (authorizedBundle.has_value()) {
            mtx.orchardBundle = authorizedBundle.value();
        } else {
            return TransactionBuilderResult("Failed to create Orchard proof or signatures");
        }
    }

    // Create Sapling spendAuth and binding signatures
    try {
        mtx.saplingBundle = sapling::apply_bundle_signatures(
            std::move(saplingBundle), dataToBeSigned.GetRawBytes());
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

    maybe_tx = CTransaction(mtx);
    tx_result = maybe_tx.value();
    signedtxn = EncodeHexTx(tx_result);

    return CTransaction(mtx);
}
