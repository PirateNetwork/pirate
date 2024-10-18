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

bool Builder::AddSpend(
  const libzcash::OrchardFullViewingKeyPirate fvk,
  const orchard_bundle::Action* action,
  const libzcash::MerklePath orchardMerklePath)
{
    if (!inner) {
        throw std::logic_error("orchard::Builder has already been used");
    }

    CDataStream ssfvk(SER_NETWORK, PROTOCOL_VERSION);
    ssfvk << fvk;
    std::array<unsigned char, 96> fvk_t;
    std::move(ssfvk.begin(), ssfvk.end(), fvk_t.begin());

    //Serialize Merkle Path to pass to Rust
    CDataStream ssMerklePath(SER_NETWORK, PROTOCOL_VERSION);
    ssMerklePath << orchardMerklePath;
    std::array<unsigned char, 1065> merklepath_t;
    std::move(ssMerklePath.begin(), ssMerklePath.end(), merklepath_t.begin());

    if (orchard_builder_add_spend(
            inner.get(),
            fvk_t.begin(),
            action->as_ptr(),
            merklepath_t.begin()
          )) {
        hasActions = true;
        return true;
    } else {
        return false;
    }
    return false;
}

void Builder::AddOutput(
    const std::optional<uint256>& ovk,
    const libzcash::OrchardPaymentAddressPirate& to,
    CAmount value,
    const std::optional<libzcash::Memo>& memo)
{
    if (!inner) {
        throw std::logic_error("orchard::Builder has already been used");
    }

    orchard_builder_add_recipient(
        inner.get(),
        ovk.has_value() ? ovk->begin() : nullptr,
        to.ToBytes().data(),
        value,
        memo.has_value() ? memo.value().ToBytes().data() : nullptr);

    hasActions = true;
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

SaplingSpendDescriptionInfo::SaplingSpendDescriptionInfo(
    libzcash::SaplingExtendedSpendingKey extsk,
    libzcash::SaplingNote note,
    uint256 anchor,
    libzcash::MerklePath saplingMerklePath) : extsk(extsk), note(note), anchor(anchor), saplingMerklePath(saplingMerklePath)
{
    librustzcash_sapling_generate_r(alpha.begin());
    // printf("SaplingSpendDescriptionInfo saplingMerklePath.position()=%lx\n", saplingMerklePath.position() );
}

// std::optional<OutputDescription> SaplingOutputDescriptionInfo::Build(void* ctx)
// {
//     auto cmu = this->note.cmu();
//     if (!cmu) {
//         return std::nullopt;
//     }
//
//     libzcash::SaplingNotePlaintext notePlaintext(this->note, this->memo);
//
//     auto res = notePlaintext.encrypt(this->note.pk_d);
//     if (!res) {
//         return std::nullopt;
//     }
//     auto enc = res.value();
//     auto encryptor = enc.second;
//
//     libzcash::SaplingPaymentAddress address(this->note.d, this->note.pk_d);
//     CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
//     ss << address;
//     std::vector<unsigned char> addressBytes(ss.begin(), ss.end());
//
//     OutputDescription odesc;
//     uint256 rcm = this->note.rcm();
//     if (!librustzcash_sapling_output_proof(
//             ctx,
//             encryptor.get_esk().begin(),
//             addressBytes.data(),
//             rcm.begin(),
//             this->note.value(),
//             odesc.cv.begin(),
//             odesc.zkproof.begin())) {
//         return std::nullopt;
//     }
//
//     odesc.cmu = *cmu;
//     odesc.ephemeralKey = encryptor.get_epk();
//     odesc.encCiphertext = enc.first;
//
//     libzcash::SaplingOutgoingPlaintext outPlaintext(this->note.pk_d, encryptor.get_esk());
//     odesc.outCiphertext = outPlaintext.encrypt(
//         this->ovk,
//         odesc.cv,
//         odesc.cmu,
//         encryptor);
//
//     return odesc;
// }

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

TransactionBuilder::TransactionBuilder() : saplingBuilder(sapling::new_builder(*Params().RustNetwork(), 1)) {}

TransactionBuilder::TransactionBuilder(
    const Consensus::Params& consensusParams,
    int nHeight,
    CKeyStore* keystore) : consensusParams(consensusParams),
                           nHeight(nHeight),
                           keystore(keystore),
                           saplingBuilder(sapling::new_builder(*Params().RustNetwork(), nHeight))
{
    mtx = CreateNewContextualCMutableTransaction(consensusParams, nHeight);

    consensusBranchId = CurrentEpochBranchId(nHeight, consensusParams);

    // Ignore the Orchard anchor if we can't use it yet.
    if (mtx.nVersion >= ORCHARD_MIN_TX_VERSION) {
        orchardBuilder = orchard::Builder(false, true, uint256());
    }

    std::optional<CTransaction> maybe_tx = CTransaction(mtx);
    auto tx_result = maybe_tx.value();
    auto signedtxn = EncodeHexTx(tx_result);
}

TransactionBuilder::TransactionBuilder(
    bool fOverwintered,
    uint32_t nExpiryHeight,
    uint32_t nVersionGroupId,
    int32_t nVersion,
    int nBlockHeight,
    uint32_t branchId,
    uint8_t cZip212Enabled) : nHeight(nBlockHeight),
                              consensusBranchId(branchId),
                              cZip212_enabled(cZip212Enabled),
                              saplingBuilder(sapling::new_builder(*Params().RustNetwork(), nBlockHeight))
{
    mtx.fOverwintered = fOverwintered;
    mtx.nExpiryHeight = nExpiryHeight;
    mtx.nVersionGroupId = nVersionGroupId;
    mtx.nVersion = nVersion;
}

bool TransactionBuilder::AddSaplingSpend(
    libzcash::SaplingExtendedSpendingKey extsk,
    libzcash::SaplingNote note,
    uint256 anchor,
    libzcash::MerklePath saplingMerklePath)
{
    // Sanity check: cannot add Sapling spend to pre-Sapling transaction
    if (mtx.nVersion < SAPLING_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot add Sapling spend to pre-Sapling transaction");
    }

    // Consistency check: all anchors must equal the first one
    if (!saplingSpends.empty()) {
        if (saplingSpends[0].anchor != anchor) {
            return false;
        }
    }

    saplingSpends.emplace_back(extsk, note, anchor, saplingMerklePath);

    CDataStream ssExtSk(SER_NETWORK, PROTOCOL_VERSION);
    ssExtSk << extsk;


    libzcash::SaplingPaymentAddress recipient(note.d, note.pk_d);


    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << saplingMerklePath;
    std::array<unsigned char, 1065> merkle_path;
    std::move(ss.begin(), ss.end(), merkle_path.begin());

    saplingBuilder->add_spend(
        {reinterpret_cast<uint8_t*>(ssExtSk.data()), ssExtSk.size()},
        note.d,
        recipient.GetRawBytes(),
        note.value(),
        note.rcm().GetRawBytes(),
        merkle_path);

    if (!firstSaplingSpendAddr.has_value()) {
        firstSaplingSpendAddr = std::make_pair(
            extsk.ToXFVK().fvk.ovk,
            libzcash::SaplingPaymentAddress(note.d, note.pk_d));
    }

    valueBalanceSapling += note.value();

    std::optional<CTransaction> maybe_tx = CTransaction(mtx);
    auto tx_result = maybe_tx.value();
    auto signedtxn = EncodeHexTx(tx_result);
    // printf("TransactionBuilder::AddSaplingSpend() position()=%lx, mtx= %s\n", saplingMerklePath.position(), signedtxn.c_str());

    return true;
}


bool TransactionBuilder::AddSaplingSpendRaw(
    libzcash::SaplingPaymentAddress from,
    CAmount value,
    SaplingOutPoint op,
    libzcash::SaplingNotePlaintext notePt,
    libzcash::MerklePath saplingMerklePath)
{
    // Consistency check: all from addresses must equal the first one
    if (!rawSaplingSpends.empty()) {
        if (!(rawSaplingSpends[0].addr == from)) {
            return false;
        }
    }

    rawSaplingSpends.emplace_back(from, value, op, notePt, saplingMerklePath);

    return true;
}

void TransactionBuilder::AddSaplingOutput(
    uint256 ovk,
    libzcash::SaplingPaymentAddress to,
    CAmount value,
    std::array<unsigned char, ZC_MEMO_SIZE> memo)
{
    libzcash::Zip212Enabled zip_212_enabled;

    // Sanity check: cannot add Sapling output to pre-Sapling transaction
    if (mtx.nVersion < SAPLING_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot add Sapling output to pre-Sapling transaction");
    }

    if (nMaxConnections > 0) {
        // Online
        zip_212_enabled = libzcash::Zip212Enabled::BeforeZip212;
        // We use nHeight = chainActive.Height() + 1 since the output will be included in the next block
        if (NetworkUpgradeActive(nHeight + 1, consensusParams, Consensus::UPGRADE_ORCHARD)) {
            zip_212_enabled = libzcash::Zip212Enabled::AfterZip212;
        }
    } else {
        // Offline
        if (cZip212_enabled == 0) {
            zip_212_enabled = libzcash::Zip212Enabled::BeforeZip212;
        } else {
            zip_212_enabled = libzcash::Zip212Enabled::AfterZip212;
        }
    }

    saplingBuilder->add_recipient(ovk.GetRawBytes(), to.GetRawBytes(), value, memo);
    valueBalanceSapling -= value;
}


void TransactionBuilder::AddPaymentOutput(std::string sAddr, CAmount iValue, std::string sMemo)
{
    Output_s sOutput;
    sOutput.sAddr = sAddr;
    sOutput.iValue = iValue;
    sOutput.sMemo = sMemo;

    outputs_offline.emplace_back(sOutput);
}

void TransactionBuilder::AddSaplingOutputRaw(
    libzcash::SaplingPaymentAddress to,
    CAmount value,
    std::array<unsigned char, ZC_MEMO_SIZE> memo)
{
    rawSaplingOutputs.emplace_back(to, value, memo);
}

void TransactionBuilder::ConvertRawSaplingOutput(uint256 ovk)
{
    for (int i = 0; i < rawSaplingOutputs.size(); i++) {
        AddSaplingOutput(ovk, rawSaplingOutputs[i].addr, rawSaplingOutputs[i].value, rawSaplingOutputs[i].memo);
    }
}

void TransactionBuilder::setOrchardOvk(uint256 ovk)
{
    ovkOrchard = ovk;
}

void TransactionBuilder::InitalizeOrchard(
    bool spendsEnabled,
    bool outputsEnabled,
    uint256 anchor)
{
    if (mtx.nVersion < ORCHARD_MIN_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot initialize Orchard before Orchard activation");
    }

    orchardBuilder = orchard::Builder(spendsEnabled, outputsEnabled, anchor);
}

void TransactionBuilder::AddOrchardSpend(
  const libzcash::OrchardExtendedSpendingKeyPirate extsk,
  const orchard_bundle::Action* action,
  const libzcash::MerklePath orchardMerklePath,
  CAmount value)
{
    if (!orchardBuilder.has_value()) {
        // Try to give a useful error.
        if (mtx.nVersion < ORCHARD_MIN_TX_VERSION) {
            throw std::runtime_error("TransactionBuilder cannot add Orchard output before NU5 activation");
        } else {
            throw std::runtime_error("TransactionBuilder cannot add Orchard output without Orchard anchor");
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

        firstOrchardSpendAddr = std::make_pair(ovk,changeAddr);
    }

    orchardBuilder.value().AddSpend(fvk, action, orchardMerklePath);
    orchardSpendingKeys.push_back(extsk.sk);
    valueBalanceOrchard += value;
}

void TransactionBuilder::AddOrchardOutput(
    const std::optional<uint256>& ovk,
    const libzcash::OrchardPaymentAddressPirate to,
    CAmount value,
    const std::optional<std::array<unsigned char, ZC_MEMO_SIZE>>& memo)
{
    if (!orchardBuilder.has_value()) {
        // Try to give a useful error.
        if (mtx.nVersion < ORCHARD_MIN_TX_VERSION) {
            throw std::runtime_error("TransactionBuilder cannot add Orchard output before NU5 activation");
        } else {
            throw std::runtime_error("TransactionBuilder cannot add Orchard output without Orchard anchor");
        }
    }

    orchardBuilder.value().AddOutput(ovk, to, value, std::nullopt);
    valueBalanceOrchard -= value;
}

void TransactionBuilder::AddOrchardOutputRaw(
    libzcash::OrchardPaymentAddressPirate to,
    CAmount value,
    std::array<unsigned char, ZC_MEMO_SIZE> memo)
{
    rawOrchardOutputs.emplace_back(to, value, memo);
}

void TransactionBuilder::createOrchardBuilderFromRawInputs()
{
    uint256 orchardAnchor;
    bool enableSpends = false;
    bool enableOutputs = false;

    if (rawOrchardOutputs.size() > 0) {
        enableOutputs = true;
    }

    auto builder = orchard::Builder(enableSpends, enableOutputs, orchardAnchor);

    for (auto orchardOutput : rawOrchardOutputs) {
        builder.AddOutput(ovkOrchard, orchardOutput.addr, orchardOutput.value, std::nullopt);
    }
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

void TransactionBuilder::SetFee(CAmount fee)
{
    this->fee = fee;
}

void TransactionBuilder::SetMinConfirmations(int iMinConf)
{
    this->iMinConf = iMinConf;
}

void TransactionBuilder::SetConsensus(const Consensus::Params& consensusParams)
{
    this->consensusParams = consensusParams;
    consensusBranchId = CurrentEpochBranchId(nHeight, consensusParams);
}

void TransactionBuilder::SetHeight(int nHeight)
{
    this->nHeight = nHeight;
    consensusBranchId = CurrentEpochBranchId(nHeight, consensusParams);
}

void TransactionBuilder::SetExpiryHeight(int expHeight)
{
    this->mtx.nExpiryHeight = expHeight;
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


// first Orchard Tansaction`
// 9bb85b7bbf85898e825b5777acd1227672552314aa31431589329f50a274c806


TransactionBuilderResult TransactionBuilder::Build()
{
    std::optional<CTransaction> maybe_tx = CTransaction(mtx);
    auto tx_result = maybe_tx.value();
    auto signedtxn = EncodeHexTx(tx_result);

    //
    // Consistency checks
    //

    // Valid change
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
            AddOrchardOutput(orchardChangeAddr->first, orchardChangeAddr->second, change, std::nullopt);
        } else if (firstOrchardSpendAddr) {
                AddOrchardOutput(firstOrchardSpendAddr->first, firstOrchardSpendAddr->second, change, std::nullopt);
        } else if (saplingChangeAddr) {
            AddSaplingOutput(saplingChangeAddr->first, saplingChangeAddr->second, change);
        } else if (firstSaplingSpendAddr) {
            AddSaplingOutput(firstSaplingSpendAddr->first, firstSaplingSpendAddr->second, change);
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
        //Populate a random key when not spending from orchard.
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


//
// Offline Transactions used with hardwarewallet
//

// pcOutput size must be 2xiSize+1
bool CharArrayToHex(unsigned char* pcInput, unsigned int iSize, char* pcOutput)
{
    unsigned int iI;

    for (iI = 0; iI < iSize; iI++) {
        sprintf(&pcOutput[iI * 2], "%02X", pcInput[iI]);
    }
    pcOutput[iI * 2 + 1] = 0; // Null terminate the string
    return true;
}

bool TransactionBuilder::AddSaplingSpend_process_offline_transaction(
    libzcash::SaplingExtendedSpendingKey extsk,
    libzcash::SaplingNote note,
    uint256 anchor,
    uint64_t lMerklePathPosition,
    unsigned char* pcMerkle)
{
    myCharArray_s sMerkle;

    // Consistency check: all anchors must equal the first one
    if (!saplingSpends.empty()) {
        if (saplingSpends[0].anchor != anchor) {
            return false;
        }
    }

    libzcash::MerklePath saplingMerklePath; // Unused parameter. required by saplingSpends.emplace_back()
    saplingSpends.emplace_back(extsk, note, anchor, saplingMerklePath);

    alMerklePathPosition.emplace_back(lMerklePathPosition);
    memcpy(&sMerkle.cArray[0], pcMerkle, sizeof(sMerkle.cArray));
    asMerklePath.emplace_back(sMerkle);
    /*
    printf("AddSaplingSpend2() saplingSpends(%ld), alMerklePathPosition(%ld), asMerklePath(%ld)\n",saplingSpends.size(),alMerklePathPosition.size(), asMerklePath.size() );fflush(stdout);
    printf("anchor %s\n", anchor.GetHex().c_str() );
    printf("merkle position %lu\n", lMerklePathPosition);
    printf("merkle path:\n");
    for (int iI=0; iI<sizeof(sMerkle.cArray);iI++)
    {
      printf("%d ",pcMerkle[iI]);
    }
    printf("\n");
    */
    mtx.valueBalance += note.value();

    std::optional<CTransaction> maybe_tx = CTransaction(mtx);
    auto tx_result = maybe_tx.value();
    auto signedtxn = EncodeHexTx(tx_result);
    // printf("TransactionBuilder::AddSaplingSpend_process_offline_transaction() mtx= %s\n",signedtxn.c_str());

    return true;
}

bool TransactionBuilder::AddSaplingSpend_prepare_offline_transaction(
    std::string sFromAddr,
    libzcash::SaplingNote note,
    uint256 anchor,
    uint64_t lMerklePathPosition,
    unsigned char* pcMerkle)
{
    myCharArray_s sMerkle;

    // printf("AddSaplingSpend_prepare_offline_transaction()\n");

    // Consistency check: all anchors must equal the first one
    if (!saplingSpends.empty()) {
        if (saplingSpends[0].anchor != anchor) {
            return false;
        }
    }

    fromAddress_ = sFromAddr;

    libzcash::MerklePath saplingMerklePath;     // Unused, just to keep saplingSpends happy
    libzcash::SaplingExtendedSpendingKey extsk; // Unused, just to keep saplingSpends happy
    saplingSpends.emplace_back(extsk, note, anchor, saplingMerklePath);

    alMerklePathPosition.emplace_back(lMerklePathPosition);
    memcpy(&sMerkle.cArray[0], pcMerkle, sizeof(sMerkle.cArray));
    asMerklePath.emplace_back(sMerkle);

    mtx.valueBalance += note.value();
    // printf("  note.value = %ld\n", note.value() );

    return true;
}

void TransactionBuilder::AddSaplingOutput_offline_transaction(
    std::string address,
    CAmount value,
    std::array<unsigned char, ZC_MEMO_SIZE> memo)
{
    auto addr = DecodePaymentAddress(address);
    assert(std::get_if<libzcash::SaplingPaymentAddress>(&addr) != nullptr);
    auto to = *(std::get_if<libzcash::SaplingPaymentAddress>(&addr));

    libzcash::Zip212Enabled zip_212_enabled = libzcash::Zip212Enabled::BeforeZip212;
    // We use nHeight = chainActive.Height() + 1 since the output will be included in the next block
    if (NetworkUpgradeActive(nHeight + 1, consensusParams, Consensus::UPGRADE_ORCHARD)) {
        zip_212_enabled = libzcash::Zip212Enabled::AfterZip212;
    }

    auto note = libzcash::SaplingNote(to, value, zip_212_enabled);

    // Spending key not available when creating the transaction which will be signed offline
    // The offline transaction builder is not using the ovk field.
    uint256 ovk;
    saplingOutputs.emplace_back(ovk, note, memo);

    sOutputRecipients.push_back(address);

    mtx.valueBalance -= value;
}

std::string TransactionBuilder::Build_offline_transaction()
{
    std::string sReturn = "";
    std::string sTmp;

    try {
        std::string sChecksumInput = "";
        // printf("transaction_builder.cpp Build_offline_transaction() enter\n");fflush(stdout);
        //
        //  Consistency checks
        //

        unsigned int iChecksum = 0;
        unsigned int iI;

        if (saplingSpends.size() <= 0) {
            return "Error: No saplingSpends specified";
        }

        // Valid change
        CAmount change = mtx.valueBalance - fee;

        // printf("Build_offline_transaction() change = balance - fee : %ld=%ld-%ld\n",change,mtx.valueBalance,fee); fflush(stdout);
        for (auto tIn : tIns) {
            change += tIn.nValue;
            // printf("Build_offline_transaction() change+=tIn.nValue  Result:%ld,%ld\n",change,tIn.nValue); fflush(stdout);
        }
        for (auto tOut : mtx.vout) {
            change -= tOut.nValue;
            // printf("Build_offline_transaction() change-=tOut.value  Result:%ld,%ld\n",change,tOut.nValue); fflush(stdout);
        }
        if (change < 0) {
            // printf("Build_offline_transaction() Change < 0 - return\n"); fflush(stdout);
            return "Error: Change < 0";
        }

        //
        // Change output
        //
        if (change > 0) {
            // Send change to the specified change address. If no change address
            // was set, send change to the first Sapling address given as input.
            if (saplingChangeAddr) {
                // printf("Build_offline_transaction Build() 1\n");
                AddSaplingOutput(saplingChangeAddr->first, saplingChangeAddr->second, change);
            } else if (!saplingSpends.empty()) {
                // printf("Build_offline_transaction Build() 3: Pay balance to ourselved. Amount:%ld\n", change);
                // auto fvk = saplingSpends[0].expsk.full_viewing_key();
                // auto note = saplingSpends[0].note;
                // libzcash::SaplingPaymentAddress changeAddr(note.d, note.pk_d);
                // AddSaplingOutput(fvk.ovk, changeAddr,   change);
                std::array<unsigned char, ZC_MEMO_SIZE> memo = {0x00};
                AddSaplingOutput_offline_transaction(fromAddress_, change, memo);
            } else {
                // printf("Build_offline_transaction() Could not calculate change amount\n");
                return "Error: Could not calculate the amount of change";
            }
        }

        // Parameter   [0]: Project - Pirate Chain='arrr'
        //             [1]: Version - Layout of the command fields
        //  Version 1: [2] Pay from address
        //             [3] Array of spending notes, which contains the funds of the 'pay from address'. Zip212 supported
        //             [4] Array of recipient: address, amount, memo
        //             [5]..[13] Blockchain parameters
        //             [15] Checksum of all the characters in the command.
        std::string sVersion = "3";

        sReturn = "z_sign_offline arrr " + sVersion + " ";
        // Version 2 : 'Witness' was replaced by 'MerklePath'. The serialised data structure is identical to version 1,
        //             but when the data is processed and reassembled, then libzcash::MerklePath must be used.
        //             The version number is increased to indicate this difference
        // Version 3 : Store hardware wallet commission in the 'fee' field instead of the 'change back to ourselves'
        //             This allows the full amount available in the address to be paid in a single transaction
        sChecksumInput = "arrr " + sVersion + " ";


        // Parameter [2]: Pay from address:
        sReturn += "\"" + fromAddress_ + "\" ";
        sChecksumInput += fromAddress_ + " ";


        // Parameter [3]: Spending notes '[{"merkleposition":number,"merklepath":hex,"note_d":hex,"note_pkd":hex,"note_r":hex,"value":number,"zip212":number},{...},{...}]'
        if (saplingSpends.size() <= 0) {
            sReturn = "Error:No saplingSpends";
            return sReturn;
        }

        sChecksumInput += "spending notes: ";
        unsigned char cAnchor[32];
        sReturn = sReturn + "'[";
        for (size_t i = 0; i < saplingSpends.size(); i++) {
            // printf("transaction_builder.cpp process saplingSpends-for(%ld)\n",i);
            auto spend = saplingSpends[i];
            uint64_t lMerklePathPosition = alMerklePathPosition[i];
            myCharArray_s sMerkle = asMerklePath[i];
            char cMerklePathHex[2 * (1 + 33 * SAPLING_TREE_DEPTH + 8) + 1];
            cMerklePathHex[2 * (1 + 33 * SAPLING_TREE_DEPTH + 8)] = 0; // null terminate
            CharArrayToHex(&sMerkle.cArray[0], sizeof(myCharArray_s), &cMerklePathHex[0]);

            libzcash::diversifier_t d = spend.note.d;
            uint256 pk_d = spend.note.pk_d;
            uint256 rcm = spend.note.rcm();
            uint64_t lValue = spend.note.value();
            char cZip212_enabled;
            libzcash::Zip212Enabled zip_212_enabled = spend.note.get_zip_212_enabled();

            // Convert zip_212_enabled to a value that can transmitted in the transaction communication
            if (zip_212_enabled == libzcash::Zip212Enabled::BeforeZip212) {
                cZip212_enabled = 0;
            } else if (zip_212_enabled == libzcash::Zip212Enabled::AfterZip212) {
                cZip212_enabled = 1;
            } else {
                sReturn = "Internal error: Unknown value for note.zip_212_enabled. Expected BeforeZip212 or AfterZip212";
                return sReturn;
            }

            unsigned char cD[ZC_DIVERSIFIER_SIZE];
            char cDHex[2 * ZC_DIVERSIFIER_SIZE + 1];
            std::memcpy(&cD[0], &d, ZC_DIVERSIFIER_SIZE);
            CharArrayToHex(&cD[0], ZC_DIVERSIFIER_SIZE, &cDHex[0]);

            unsigned char cPK_D[32];
            char cPK_DHex[2 * 32 + 1];
            std::memcpy(&cPK_D[0], &pk_d, 32);
            CharArrayToHex(&cPK_D[0], 32, &cPK_DHex[0]);

            unsigned char cR[32];
            char cRHex[2 * 32 + 1];
            std::memcpy(&cR[0], &rcm, 32);
            CharArrayToHex(&cR[0], 32, &cRHex[0]);

            char cHex[20 + 2 * (1 + 33 * SAPLING_TREE_DEPTH + 8) + 1];
            // 20                        14                8            11             9            8              9              2
            //{"merkleposition":number,"merklepath":hex,"note_d":hex,"note_pkd":hex,"note_r":hex,"value":number,"zip212":number},{...},{...}]'
            snprintf(&cHex[0],sizeof(cHex),"{\"merkleposition\":\"%lu\",",lMerklePathPosition);
            sReturn=sReturn+cHex;
            sTmp = strprintf("%u ", lMerklePathPosition);
            sChecksumInput += sTmp;


            snprintf(&cHex[0], sizeof(cHex), "\"merklepath\":\"%s\",", &cMerklePathHex[0]);
            sReturn = sReturn + cHex;
            sTmp = strprintf("%s ", &cMerklePathHex[0]);
            sChecksumInput += sTmp;


            snprintf(&cHex[0], sizeof(cHex), "\"note_d\":\"%s\",", &cDHex[0]);
            sReturn = sReturn + cHex;
            sTmp = strprintf("%s ", &cDHex[0]);
            sChecksumInput += sTmp;


            snprintf(&cHex[0], sizeof(cHex), "\"note_pkd\":\"%s\",", &cPK_DHex[0]);
            sReturn = sReturn + cHex;
            sTmp = strprintf("%s ", &cPK_DHex[0]);
            sChecksumInput += sTmp;


            snprintf(&cHex[0], sizeof(cHex), "\"note_r\":\"%s\",", &cRHex[0]);
            sReturn = sReturn + cHex;
            sTmp = strprintf("%s ", &cRHex[0]);
            sChecksumInput += sTmp;


            snprintf(&cHex[0],sizeof(cHex),"\"value\":%lu,", lValue);
            sReturn=sReturn+cHex;
            sTmp = strprintf("%lu ",lValue);
            sChecksumInput+=sTmp;


            // Zip212 enabled for the specific note
            snprintf(&cHex[0], sizeof(cHex), "\"zip212\":%u}", cZip212_enabled);
            sReturn = sReturn + cHex;
            if (cZip212_enabled == 0) {
                sTmp = "0 ";
            } else {
                sTmp = "1 ";
            }
            sChecksumInput += sTmp;


            // Last entry: Must close the array with ]'
            // otherwide start the next entry with a ,
            if (i == saplingSpends.size() - 1) {
                sReturn = sReturn + "]' ";
            } else {
                sReturn = sReturn + ",";
            }
            std::memcpy(&cAnchor[0], &spend.anchor, 32);
        }


        // Parameter [4]: Outputs (recipients)
        // printf("Build_offline_transaction() [4] Outputs: Total=%ld\n\n", saplingOutputs.size());
        if (sOutputRecipients.size() != saplingOutputs.size()) {
            // printf("Build_offline_transaction() [4] Internal error: sOutputRecipients.size(%ld) != saplingOutputs.size(%ld)\n\n",sOutputRecipients.size(), saplingOutputs.size());
            sReturn = "Internal error: sOutputRecipients.size() != saplingOutputs.size()";
            return sReturn;
        }
        sChecksumInput += "saplingOutputs: ";
        sReturn = sReturn + "'[";
        for (size_t i = 0; i < saplingOutputs.size(); i++) {
            std::string sHexMemo;
            auto output = saplingOutputs[i];

            sReturn = sReturn + strprintf("{\"address\":\"%s\",\"amount\":%ld", sOutputRecipients[i].c_str(), output.note.value());

            // Set length to maximum.
            size_t iStrLen = ZC_MEMO_SIZE;
            if (output.memo.data()[(ZC_MEMO_SIZE - 1)] == 0) {
                // If the string is null terminated, evaluate its length to see
                // if its shorter:
                iStrLen = strlen(reinterpret_cast<char*>(output.memo.data()));
            }

            if (iStrLen > 0) {
                // Note: Memo is ASCII encoded character array of max. ZC_MEMO_SIZE characters.
                //     : The array is not null terminated if all 512 chars are populated!
                //     : Convert to hex encoded string for offline transaction:
                char caHexMemo[ZC_MEMO_SIZE * 2 + 1];
                memset(&caHexMemo[0], 0, sizeof(caHexMemo));
                CharArrayToHex(output.memo.data(), iStrLen, (char*)&caHexMemo[0]);
                sHexMemo = strprintf("%s", &caHexMemo[0]);
                sReturn = sReturn + ",\"memo\":\"" + sHexMemo + "\"}";

                // printf("transaction_builder.cpp process saplingOutputs-for(%ld). Adres=%s Amount=%ld Memo=%s\n\n",i, sOutputRecipients[i].c_str(), output.note.value(), sHexMemo.c_str() );
            } else {
                sReturn = sReturn + strprintf("}");
                // printf("transaction_builder.cpp process saplingOutputs-for(%ld). Adres=%s Amount=%ld No memo\n\n",i, sOutputRecipients[i].c_str(), output.note.value() );
            }

            if (i == (saplingOutputs.size() - 1)) {
                sReturn = sReturn + "]' ";
            } else {
                sReturn = sReturn + ",";
            }

            // sChecksumInput+=sOutputRecipients[i] +"\n"+ std::to_string(output.note.value() ) +"\n"+ sHexMemo +"\n";
            sTmp = strprintf("%s ", sOutputRecipients[i].c_str());
            sChecksumInput += sTmp;
            sTmp = strprintf("%ld ", output.note.value());
            sChecksumInput += sTmp;
            sTmp = strprintf("%s ", sHexMemo.c_str());
            sChecksumInput += sTmp;
        }


        // Parameter [5]: Minimum confirmations
        // printf("Build_offline_transaction() [5] Minimum confirmations %d\n\n",iMinConf);
        sReturn = sReturn + strprintf("%d ", iMinConf);
        sTmp = strprintf("%d ", iMinConf);
        sChecksumInput += sTmp;


        // Parameter [6]: Miners fee
        // printf("Build_offline_transaction() [6] Miners fee %ld\n\n",fee);
        sReturn = sReturn + strprintf("%ld ", fee);
        sTmp = strprintf("%ld ", fee);
        sChecksumInput += sTmp;


        // Parameter [7]: Next block height
        // printf("Build_offline_transaction() [7] Next block height: '%d'\n\n", nHeight);
        sReturn = sReturn + strprintf("%d ", nHeight);
        sTmp = strprintf("%d ", nHeight);
        sChecksumInput += sTmp;


        // Parameter [8]: BranchId (uint32_t)
        auto BranchId = CurrentEpochBranchId(nHeight, consensusParams);
        // printf("Build_offline_transaction() [8] BranchId: '%u'\n\n", BranchId);
        sReturn = sReturn + strprintf("%u ", BranchId);
        sTmp = strprintf("%u ", BranchId);
        sChecksumInput += sTmp;


        // Parameter [9]: Anchor
        // printf("Build_offline_transaction() [9] Anchor\n");
        char cAnchorHex[2 * 32 + 1];
        CharArrayToHex(&cAnchor[0], 32, &cAnchorHex[0]);
        sReturn = sReturn + "\"" + cAnchorHex + "\" ";
        sTmp = strprintf("%s ", &cAnchorHex[0]);
        sChecksumInput += sTmp;


        // Parameter [10] : bool fOverwintered
        if (mtx.fOverwintered == true) {
            sReturn = sReturn + "1 ";
            sChecksumInput += "1 ";
        } else {
            sReturn = sReturn + "0 ";
            sChecksumInput += "0 ";
        }


        // Parameter [11] : uint32_t nExpiryHeight
        sReturn = sReturn + strprintf("%u ", mtx.nExpiryHeight);
        sTmp = strprintf("%u ", mtx.nExpiryHeight);
        sChecksumInput += sTmp;


        // Parameter [12]: uint32_t nVersionGroupId
        sReturn = sReturn + strprintf("%u ", mtx.nVersionGroupId);
        sTmp = strprintf("%u ", mtx.nVersionGroupId);
        sChecksumInput += sTmp;


        // Parameter [13]: int32_t nVersion);
        sReturn = sReturn + strprintf("%d ", mtx.nVersion);
        sTmp = strprintf("%d ", mtx.nVersion);
        sChecksumInput += sTmp;

        // Parameter [14]: Zip212 enabled: Used for the saplingOutputs
        std::string sZip212_enabled = "0";
        // We use nHeight = chainActive.Height() + 1 since the output will be included in the next block
        if (NetworkUpgradeActive(nHeight + 1, consensusParams, Consensus::UPGRADE_ORCHARD)) {
            sZip212_enabled = "1";
        }
        sReturn += sZip212_enabled + " ";
        sChecksumInput += sZip212_enabled + " ";

        // Parameter [15]: checksum
        // A simple checksum of the full string, to detect copy/paste errors between the wallets
        // The checksum equals the sum of the ASCII values of all the characters in the string:
        // printf("sChecksumInput:\n%s\n\n",sChecksumInput.c_str() );
        iChecksum = 0x01;
        for (iI = 0; iI < sChecksumInput.length(); iI++) {
            unsigned int iVal = (unsigned int)sChecksumInput.at(iI);
            iChecksum = iChecksum + iVal;
        }
        sTmp = strprintf("%u", iChecksum);
        // printf("Calculated checksum: %s\n",sTmp.c_str() );
        // Append the checksum to the protocol data:
        sReturn += sTmp;

        // printf("\n\nPaste the full contents into the console of your offline wallet to sign the transaction:\n%s\n\n",sReturn.c_str());
        //  add op_return if there is one to add
        // AddOpRetLast(); ??
        // printf("Build_offline_transaction() %s\n\n",sReturn.c_str() );
        return sReturn;
    } catch (const char* pcError) {
        sReturn = "Exception occurred: ";
        sReturn.assign(pcError);
        printf("%s\n\n", sReturn.c_str());
        return sReturn;
    }
}
