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

#include "primitives/transaction.h"

#include "hash.h"
#include "tinyformat.h"
#include "util/strencodings.h"

#include "librustzcash.h"

uint256 JSDescription::h_sig(ZCJoinSplit& params, const uint256& joinSplitPubKey) const
{
    return params.h_sig(randomSeed, nullifiers, joinSplitPubKey);
}

class SproutProofVerifier
{
    ProofVerifier& verifier;
    const uint256& joinSplitPubKey;
    const JSDescription& jsdesc;

public:
    SproutProofVerifier(
        ProofVerifier& verifier,
        const uint256& joinSplitPubKey,
        const JSDescription& jsdesc) : jsdesc(jsdesc), verifier(verifier), joinSplitPubKey(joinSplitPubKey) {}

    bool operator()(const libzcash::PHGRProof& proof) const
    {
        // Sprout transaction are disabled, no new transation will ever post
        // We checkpoint after Sapling activation, so we can skip verification
        // for all Sprout proofs.
        return true;
    }

    bool operator()(const libzcash::GrothProof& proof) const
    {
        // Sprout transaction are disabled, no new transation will ever post
        // We checkpoint after Sapling activation, so we can skip verification
        // for all Sprout proofs.
        return true;
    }
};

ProofVerifier ProofVerifier::Strict()
{
    return ProofVerifier(true);
}

ProofVerifier ProofVerifier::Disabled()
{
    return ProofVerifier(false);
}

bool ProofVerifier::VerifySprout(
    const JSDescription& jsdesc,
    const uint256& joinSplitPubKey)
{
    if (!perform_verification) {
        return true;
    }

    auto pv = SproutProofVerifier(*this, joinSplitPubKey, jsdesc);
    return std::visit(pv, jsdesc.proof);
}

std::string COutPoint::ToString() const
{
    return strprintf("COutPoint(%s, %u)", hash.ToString().substr(0, 10), n);
}

std::string SaplingOutPoint::ToString() const
{
    return strprintf("SaplingOutPoint(%s, %u)", hash.ToString().substr(0, 10), n);
}

CTxIn::CTxIn(COutPoint prevoutIn, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CTxIn::CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CTxIn::ToString() const
{
    std::string str;
    str += "CTxIn(";
    str += prevout.ToString();
    if (prevout.IsNull())
        str += strprintf(", coinbase %s", HexStr(scriptSig));
    else
        str += strprintf(", scriptSig=%s", HexStr(scriptSig).substr(0, 24));
    if (nSequence != std::numeric_limits<unsigned int>::max())
        str += strprintf(", nSequence=%u", nSequence);
    str += ")";
    return str;
}

CTxOut::CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn)
{
    nValue = nValueIn;
    scriptPubKey = scriptPubKeyIn;
}

uint256 CTxOut::GetHash() const
{
    return SerializeHash(*this);
}

std::string CTxOut::ToString() const
{
    return strprintf("CTxOut(nValue=%d.%08d, scriptPubKey=%s)", nValue / COIN, nValue % COIN, HexStr(scriptPubKey).substr(0, 30));
}

CMutableTransaction::CMutableTransaction() : nVersion(CTransaction::SPROUT_MIN_CURRENT_VERSION), fOverwintered(false), nVersionGroupId(0), nExpiryHeight(0), nLockTime(0), valueBalance(0) {}
CMutableTransaction::CMutableTransaction(const CTransaction& tx) : nVersion(tx.nVersion), fOverwintered(tx.fOverwintered), nVersionGroupId(tx.nVersionGroupId), nExpiryHeight(tx.nExpiryHeight),
                                                                   vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime),
                                                                   valueBalance(tx.valueBalance), vShieldedSpend(tx.vShieldedSpend), vShieldedOutput(tx.vShieldedOutput),
                                                                   vjoinsplit(tx.vjoinsplit), joinSplitPubKey(tx.joinSplitPubKey), joinSplitSig(tx.joinSplitSig),
                                                                   bindingSig(tx.bindingSig), saplingBundle(tx.GetSaplingBundle())
{
}

uint256 CMutableTransaction::GetHash() const
{
    return SerializeHash(*this);
}

void CTransaction::UpdateHash() const
{
    *const_cast<uint256*>(&hash) = SerializeHash(*this);
}

CTransaction::CTransaction() : nVersion(CTransaction::SPROUT_MIN_CURRENT_VERSION), fOverwintered(false), nVersionGroupId(0), nExpiryHeight(0), vin(), vout(), nLockTime(0), valueBalance(0), vShieldedSpend(), vShieldedOutput(), vjoinsplit(), joinSplitPubKey(), joinSplitSig(), bindingSig(), saplingBundle() {}

CTransaction::CTransaction(const CMutableTransaction& tx) : nVersion(tx.nVersion), fOverwintered(tx.fOverwintered), nVersionGroupId(tx.nVersionGroupId), nExpiryHeight(tx.nExpiryHeight),
                                                            vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime),
                                                            valueBalance(tx.valueBalance), vShieldedSpend(tx.vShieldedSpend), vShieldedOutput(tx.vShieldedOutput),
                                                            vjoinsplit(tx.vjoinsplit), joinSplitPubKey(tx.joinSplitPubKey), joinSplitSig(tx.joinSplitSig),
                                                            bindingSig(tx.bindingSig), saplingBundle(tx.saplingBundle)
{
    UpdateHash();
}

// Protected constructor which only derived classes can call.
// For developer testing only.
CTransaction::CTransaction(
    const CMutableTransaction& tx,
    bool evilDeveloperFlag) : nVersion(tx.nVersion), fOverwintered(tx.fOverwintered), nVersionGroupId(tx.nVersionGroupId), nExpiryHeight(tx.nExpiryHeight),
                              vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime),
                              valueBalance(tx.valueBalance), vShieldedSpend(tx.vShieldedSpend), vShieldedOutput(tx.vShieldedOutput),
                              vjoinsplit(tx.vjoinsplit), joinSplitPubKey(tx.joinSplitPubKey), joinSplitSig(tx.joinSplitSig),
                              bindingSig(tx.bindingSig), saplingBundle(tx.saplingBundle)
{
    assert(evilDeveloperFlag);
}

CTransaction::CTransaction(CMutableTransaction&& tx) : nVersion(tx.nVersion), fOverwintered(tx.fOverwintered), nVersionGroupId(tx.nVersionGroupId),
                                                       vin(std::move(tx.vin)), vout(std::move(tx.vout)), nLockTime(tx.nLockTime), nExpiryHeight(tx.nExpiryHeight),
                                                       valueBalance(tx.valueBalance),
                                                       vShieldedSpend(std::move(tx.vShieldedSpend)), vShieldedOutput(std::move(tx.vShieldedOutput)),
                                                       vjoinsplit(std::move(tx.vjoinsplit)),
                                                       joinSplitPubKey(std::move(tx.joinSplitPubKey)), joinSplitSig(std::move(tx.joinSplitSig)), saplingBundle(std::move(tx.saplingBundle))
{
    UpdateHash();
}

CTransaction& CTransaction::operator=(const CTransaction& tx)
{
    *const_cast<bool*>(&fOverwintered) = tx.fOverwintered;
    *const_cast<int*>(&nVersion) = tx.nVersion;
    *const_cast<uint32_t*>(&nVersionGroupId) = tx.nVersionGroupId;
    *const_cast<std::vector<CTxIn>*>(&vin) = tx.vin;
    *const_cast<std::vector<CTxOut>*>(&vout) = tx.vout;
    *const_cast<unsigned int*>(&nLockTime) = tx.nLockTime;
    *const_cast<uint32_t*>(&nExpiryHeight) = tx.nExpiryHeight;
    saplingBundle = tx.saplingBundle;
    *const_cast<CAmount*>(&valueBalance) = tx.valueBalance;
    *const_cast<std::vector<SpendDescription>*>(&vShieldedSpend) = tx.vShieldedSpend;
    *const_cast<std::vector<OutputDescription>*>(&vShieldedOutput) = tx.vShieldedOutput;
    *const_cast<std::vector<JSDescription>*>(&vjoinsplit) = tx.vjoinsplit;
    *const_cast<uint256*>(&joinSplitPubKey) = tx.joinSplitPubKey;
    *const_cast<joinsplit_sig_t*>(&joinSplitSig) = tx.joinSplitSig;
    *const_cast<binding_sig_t*>(&bindingSig) = tx.bindingSig;
    *const_cast<uint256*>(&hash) = tx.hash;
    return *this;
}

CAmount CTransaction::GetValueOut() const
{
    CAmount nValueOut = 0;
    for (std::vector<CTxOut>::const_iterator it(vout.begin()); it != vout.end(); ++it) {
        nValueOut += it->nValue;
        if (!MoneyRange(it->nValue) || !MoneyRange(nValueOut))
            throw std::runtime_error("CTransaction::GetValueOut(): value out of range");
    }

    if (valueBalance <= 0) {
        // NB: negative valueBalance "takes" money from the transparent value pool just as outputs do
        nValueOut += -valueBalance;

        if (!MoneyRange(-valueBalance) || !MoneyRange(nValueOut)) {
            throw std::runtime_error("CTransaction::GetValueOut(): value out of range");
        }
    }

    for (std::vector<JSDescription>::const_iterator it(vjoinsplit.begin()); it != vjoinsplit.end(); ++it) {
        // NB: vpub_old "takes" money from the transparent value pool just as outputs do
        nValueOut += it->vpub_old;

        if (!MoneyRange(it->vpub_old) || !MoneyRange(nValueOut))
            throw std::runtime_error("CTransaction::GetValueOut(): value out of range");
    }
    return nValueOut;
}

// SAPLINGTODO: make this accurate for all transactions, including sapling
CAmount CTransaction::GetShieldedValueIn() const
{
    CAmount nValue = 0;

    if (valueBalance >= 0) {
        // NB: positive valueBalance "gives" money to the transparent value pool just as inputs do
        nValue += valueBalance;

        if (!MoneyRange(valueBalance) || !MoneyRange(nValue)) {
            throw std::runtime_error("CTransaction::GetShieldedValueIn(): value out of range");
        }
    }

    for (std::vector<JSDescription>::const_iterator it(vjoinsplit.begin()); it != vjoinsplit.end(); ++it) {
        // NB: vpub_new "gives" money to the transparent value pool just as inputs do
        nValue += it->vpub_new;

        if (!MoneyRange(it->vpub_new) || !MoneyRange(nValue))
            throw std::runtime_error("CTransaction::GetShieldedValueIn(): value out of range");
    }

    return nValue;
}

double CTransaction::ComputePriority(double dPriorityInputs, unsigned int nTxSize) const
{
    nTxSize = CalculateModifiedSize(nTxSize);
    if (nTxSize == 0)
        return 0.0;

    return dPriorityInputs / nTxSize;
}

unsigned int CTransaction::CalculateModifiedSize(unsigned int nTxSize) const
{
    // In order to avoid disincentivizing cleaning up the UTXO set we don't count
    // the constant overhead for each txin and up to 110 bytes of scriptSig (which
    // is enough to cover a compressed pubkey p2sh redemption) for priority.
    // Providing any more cleanup incentive than making additional inputs free would
    // risk encouraging people to create junk outputs to redeem later.
    if (nTxSize == 0)
        nTxSize = ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
    for (std::vector<CTxIn>::const_iterator it(vin.begin()); it != vin.end(); ++it) {
        unsigned int offset = 41U + std::min(110U, (unsigned int)it->scriptSig.size());
        if (nTxSize > offset)
            nTxSize -= offset;
    }
    return nTxSize;
}

unsigned int CTransaction::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

// will return the open time or block if this is a time locked transaction output that we recognize.
// if we can't determine that it has a valid time lock, it returns 0
int64_t CTransaction::UnlockTime(uint32_t voutNum) const
{
    if (vout.size() > voutNum + 1 && vout[voutNum].scriptPubKey.IsPayToScriptHash()) {
        uint32_t voutNext = voutNum + 1;

        std::vector<uint8_t> opretData;
        uint160 scriptID = uint160(std::vector<unsigned char>(vout[voutNum].scriptPubKey.begin() + 2, vout[voutNum].scriptPubKey.begin() + 22));
        CScript::const_iterator it = vout[voutNext].scriptPubKey.begin() + 1;

        opcodetype op;
        if (vout[voutNext].scriptPubKey.GetOp2(it, op, &opretData)) {
            if (opretData.size() > 0 && opretData.data()[0] == OPRETTYPE_TIMELOCK) {
                int64_t unlocktime;
                CScript opretScript = CScript(opretData.begin() + 1, opretData.end());
                if (Hash160(opretScript) == scriptID &&
                    opretScript.IsCheckLockTimeVerify(&unlocktime)) {
                    return (unlocktime);
                }
            }
        }
    }
    return (0);
}

std::string CTransaction::ToString() const
{
    std::string str;
    if (!fOverwintered) {
        str += strprintf("CTransaction(hash=%s, ver=%d, vin.size=%u, vout.size=%u, nLockTime=%u)\n",
                         GetHash().ToString().substr(0, 10),
                         nVersion,
                         vin.size(),
                         vout.size(),
                         nLockTime);
    } else if (nVersion >= SAPLING_MIN_TX_VERSION) {
        str += strprintf("CTransaction(hash=%s, ver=%d, fOverwintered=%d, nVersionGroupId=%08x, vin.size=%u, vout.size=%u, nLockTime=%u, nExpiryHeight=%u, valueBalance=%u, vShieldedSpend.size=%u, vShieldedOutput.size=%u)\n",
                         GetHash().ToString().substr(0, 10),
                         nVersion,
                         fOverwintered,
                         nVersionGroupId,
                         vin.size(),
                         vout.size(),
                         nLockTime,
                         nExpiryHeight,
                         valueBalance,
                         vShieldedSpend.size(),
                         vShieldedOutput.size());
    } else if (nVersion >= 3) {
        str += strprintf("CTransaction(hash=%s, ver=%d, fOverwintered=%d, nVersionGroupId=%08x, vin.size=%u, vout.size=%u, nLockTime=%u, nExpiryHeight=%u)\n",
                         GetHash().ToString().substr(0, 10),
                         nVersion,
                         fOverwintered,
                         nVersionGroupId,
                         vin.size(),
                         vout.size(),
                         nLockTime,
                         nExpiryHeight);
    }
    for (unsigned int i = 0; i < vin.size(); i++)
        str += "    " + vin[i].ToString() + "\n";
    for (unsigned int i = 0; i < vout.size(); i++)
        str += "    " + vout[i].ToString() + "\n";
    return str;
}

size_t CTransaction::GetSaplingSpendsCount() const
{
    return saplingBundle.GetSpendsCount();
}

size_t CTransaction::GetSaplingOutputsCount() const
{
    return saplingBundle.GetOutputsCount();
}

std::optional<uint256> CTransaction::GetSpendCV(size_t spendIndex) const
{
    if (spendIndex >= GetSaplingSpendsCount()) {
        return std::nullopt;
    }

    uint256 cv;
    auto spendRust = saplingBundle.GetDetails().get_spend(spendIndex);
    auto rustCV = spendRust->cv();
    std::memcpy(&cv, &rustCV, 32);
    return cv;
}

std::optional<uint256> CTransaction::GetSpendAnchor(size_t spendIndex) const
{
    if (spendIndex >= GetSaplingSpendsCount()) {
        return std::nullopt;
    }

    uint256 anchor;
    auto spendRust = saplingBundle.GetDetails().get_spend(spendIndex);
    auto rustAnchor = spendRust->anchor();
    std::memcpy(&anchor, &rustAnchor, 32);
    return anchor;
}

std::optional<uint256> CTransaction::GetSpendNullifier(size_t spendIndex) const
{
    if (spendIndex >= GetSaplingSpendsCount()) {
        return std::nullopt;
    }

    uint256 nullifier;
    auto spendRust = saplingBundle.GetDetails().get_spend(spendIndex);
    auto rustNullifier = spendRust->nullifier();
    std::memcpy(&nullifier, &rustNullifier, 32);
    return nullifier;
}

std::optional<uint256> CTransaction::GetSpendRK(size_t spendIndex) const
{
    if (spendIndex >= GetSaplingSpendsCount()) {
        return std::nullopt;
    }

    uint256 rk;
    auto spendRust = saplingBundle.GetDetails().get_spend(spendIndex);
    auto rustRK = spendRust->rk();
    std::memcpy(&rk, &rustRK, 32);
    return rk;
}

std::optional<libzcash::GrothProof> CTransaction::GetSpendZKProof(size_t spendIndex) const
{
    if (spendIndex >= GetSaplingSpendsCount()) {
        return std::nullopt;
    }

    libzcash::GrothProof zkproof;
    auto spendRust = saplingBundle.GetDetails().get_spend(spendIndex);
    auto rustZKProof = spendRust->zkproof();
    std::memcpy(&zkproof, &rustZKProof, 192);
    return zkproof;
}

std::optional<std::array<unsigned char, 64>> CTransaction::GetSpendAuthSig(size_t spendIndex) const
{
    if (spendIndex >= GetSaplingSpendsCount()) {
        return std::nullopt;
    }

    std::array<unsigned char, 64> spendAuthSig;
    auto spendRust = saplingBundle.GetDetails().get_spend(spendIndex);
    auto rustSpendAuthSig = spendRust->spend_auth_sig();
    std::memcpy(&spendAuthSig, &rustSpendAuthSig, 64);
    return spendAuthSig;
}

const rust::Vec<sapling::Spend> CTransaction::GetSaplingSpends() const
{
    return saplingBundle.GetDetails().spends();
}

std::vector<SpendDescription> CTransaction::GetSpendDescriptionFromBundle() const
{
    size_t spendCount = GetSaplingSpendsCount();
    std::vector<SpendDescription> returnSpends;

    for (size_t i = 0; i < spendCount; i++) {
        auto spendRust = saplingBundle.GetDetails().get_spend(i);
        SpendDescription spendDescription;

        auto rustCV = spendRust->cv();
        std::memcpy(&spendDescription.cv, &rustCV, 32);

        auto rustAnchor = spendRust->anchor();
        std::memcpy(&spendDescription.anchor, &rustAnchor, 32);

        auto rustNullifier = spendRust->nullifier();
        std::memcpy(&spendDescription.nullifier, &rustNullifier, 32);

        auto rustRK = spendRust->rk();
        std::memcpy(&spendDescription.rk, &rustRK, 32);

        auto rustZKProok = spendRust->zkproof();
        std::memcpy(&spendDescription.zkproof, &rustZKProok, 192);

        auto rustSpendAuthSig = spendRust->spend_auth_sig();
        std::memcpy(&spendDescription.spendAuthSig, &rustSpendAuthSig, 64);

        returnSpends.emplace_back(spendDescription);
    }

    return returnSpends;
}

std::optional<uint256> CTransaction::GetOutputCV(size_t outIndex) const
{
    if (outIndex >= GetSaplingOutputsCount()) {
        return std::nullopt;
    }

    uint256 cv;
    auto outRust = saplingBundle.GetDetails().get_output(outIndex);
    auto rustCV = outRust->cv();
    std::memcpy(&cv, &rustCV, 32);
    return cv;
}

std::optional<uint256> CTransaction::GetOutputCMU(size_t outIndex) const
{
    if (outIndex >= GetSaplingOutputsCount()) {
        return std::nullopt;
    }

    uint256 cmu;
    auto outRust = saplingBundle.GetDetails().get_output(outIndex);
    auto rustCMU = outRust->cmu();
    std::memcpy(&cmu, &rustCMU, 32);
    return cmu;
}

std::optional<uint256> CTransaction::GetOutputEphemeralKey(size_t outIndex) const
{
    if (outIndex >= GetSaplingOutputsCount()) {
        return std::nullopt;
    }

    uint256 ephemeralKey;
    auto outRust = saplingBundle.GetDetails().get_output(outIndex);
    auto rustEphemeralKey = outRust->ephemeral_key();
    std::memcpy(&ephemeralKey, &rustEphemeralKey, 32);
    return ephemeralKey;
}

std::optional<libzcash::SaplingEncCiphertext> CTransaction::GetOutputEncCiphertext(size_t outIndex) const
{
    if (outIndex >= GetSaplingOutputsCount()) {
        return std::nullopt;
    }

    libzcash::SaplingEncCiphertext encCiphertext;
    auto outRust = saplingBundle.GetDetails().get_output(outIndex);
    auto rustEncCiphertext = outRust->enc_ciphertext();
    std::memcpy(&encCiphertext, &rustEncCiphertext, 580);
    return encCiphertext;
}

std::optional<libzcash::SaplingOutCiphertext> CTransaction::GetOutputOutCiphertext(size_t outIndex) const
{
    if (outIndex >= GetSaplingOutputsCount()) {
        return std::nullopt;
    }

    libzcash::SaplingOutCiphertext outCiphertext;
    auto outRust = saplingBundle.GetDetails().get_output(outIndex);
    auto rustOutCiphertext = outRust->out_ciphertext();
    std::memcpy(&outCiphertext, &rustOutCiphertext, 80);
    return outCiphertext;
}

std::optional<libzcash::GrothProof> CTransaction::GetOutputZKProof(size_t outIndex) const
{
    if (outIndex >= GetSaplingOutputsCount()) {
        return std::nullopt;
    }

    libzcash::GrothProof zkproof;
    auto outRust = saplingBundle.GetDetails().get_output(outIndex);
    auto rustZKProof = outRust->zkproof();
    std::memcpy(&zkproof, &rustZKProof, 80);
    return zkproof;
}

const rust::Vec<sapling::Output> CTransaction::GetSaplingOutputs() const
{
    return saplingBundle.GetDetails().outputs();
}

std::vector<OutputDescription> CTransaction::GetOutputDescriptionFromBundle() const
{
    size_t outCount = GetSaplingOutputsCount();
    std::vector<OutputDescription> returnOutputs;

    for (size_t i = 0; i < outCount; i++) {
        auto outRust = saplingBundle.GetDetails().get_output(i);
        OutputDescription outDescription;

        auto rustCV = outRust->cv();
        std::memcpy(&outDescription.cv, &rustCV, 32);

        auto rustCMU = outRust->cmu();
        std::memcpy(&outDescription.cmu, &rustCMU, 32);

        auto rustEphemeralKey = outRust->ephemeral_key();
        std::memcpy(&outDescription.ephemeralKey, &rustEphemeralKey, 32);

        auto rustEncCiphertext = outRust->enc_ciphertext();
        std::memcpy(&outDescription.encCiphertext, &rustEncCiphertext, 580);

        auto rustOutCiphertext = outRust->out_ciphertext();
        std::memcpy(&outDescription.outCiphertext, &rustOutCiphertext, 80);

        auto rustZKProof = outRust->zkproof();
        std::memcpy(&outDescription.zkproof, &rustZKProof, 192);

        returnOutputs.emplace_back(outDescription);
    }

    return returnOutputs;
}


/**
 * Returns the Sapling value balance for the transaction.
 */
CAmount CTransaction::GetValueBalanceSapling() const
{
    return saplingBundle.GetValueBalance();
}

/**
 * Returns the Sapling bundle for the transaction.
 */
const SaplingBundle& CTransaction::GetSaplingBundle() const
{
    return saplingBundle;
}
