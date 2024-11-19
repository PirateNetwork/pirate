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

std::string OrchardOutPoint::ToString() const
{
    return strprintf("OrchardOutPoint(%s, %u)", hash.ToString().substr(0, 10), n);
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
                                                                   nConsensusBranchId(tx.GetConsensusBranchId()),
                                                                   vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime),
                                                                   valueBalance(tx.ValueBalanceFromBundle()),
                                                                   vjoinsplit(tx.vjoinsplit), joinSplitPubKey(tx.joinSplitPubKey), joinSplitSig(tx.joinSplitSig),
                                                                   bindingSig(tx.BindingSigFromBundle()),
                                                                   saplingBundle(tx.GetSaplingBundle()), orchardBundle(tx.GetOrchardBundle())
{

}

uint256 CMutableTransaction::GetHash() const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *this;
    uint256 hash;
    if (!zcash_transaction_digests(
        reinterpret_cast<const unsigned char*>(ss.data()),
        ss.size(),
        hash.begin(),
        nullptr))
    {
        throw std::ios_base::failure("CMutableTransaction::GetHash: Invalid transaction format");
    }
    return hash;
}

uint256 CMutableTransaction::GetAuthDigest() const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *this;
    uint256 authDigest;
    if (!zcash_transaction_digests(
        reinterpret_cast<const unsigned char*>(ss.data()),
        ss.size(),
        nullptr,
        authDigest.begin()))
    {
        throw std::ios_base::failure("CMutableTransaction::GetAuthDigest: Invalid transaction format");
    }
    return authDigest;
}

void CTransaction::UpdateHash() const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *this;
    if (!zcash_transaction_digests(
        reinterpret_cast<const unsigned char*>(ss.data()),
        ss.size(),
        const_cast<uint256*>(&wtxid.hash)->begin(),
        const_cast<uint256*>(&wtxid.authDigest)->begin()))
    {
        throw std::ios_base::failure("CTransaction::UpdateHash: Invalid transaction format");
    }
}

CTransaction::CTransaction() : nVersion(CTransaction::SPROUT_MIN_CURRENT_VERSION),
                              fOverwintered(false), nVersionGroupId(0), nExpiryHeight(0),
                              nConsensusBranchId(std::nullopt),
                              vin(), vout(), nLockTime(0),
                              valueBalance(0),
                              vjoinsplit(), joinSplitPubKey(), joinSplitSig(), bindingSig(),
                              saplingBundle(), orchardBundle() {}

CTransaction::CTransaction(const CMutableTransaction& tx) : nVersion(tx.nVersion), fOverwintered(tx.fOverwintered), nVersionGroupId(tx.nVersionGroupId), nExpiryHeight(tx.nExpiryHeight),
                                                            nConsensusBranchId(tx.nConsensusBranchId),
                                                            vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime),
                                                            valueBalance(tx.ValueBalanceFromBundle()),
                                                            vjoinsplit(tx.vjoinsplit), joinSplitPubKey(tx.joinSplitPubKey), joinSplitSig(tx.joinSplitSig),
                                                            bindingSig(tx.BindingSigFromBundle()),
                                                            saplingBundle(tx.saplingBundle), orchardBundle(tx.orchardBundle)
{
    UpdateHash();
}

// Protected constructor which only derived classes can call.
// For developer testing only.
CTransaction::CTransaction(
    const CMutableTransaction& tx,
    bool evilDeveloperFlag) : nVersion(tx.nVersion), fOverwintered(tx.fOverwintered), nVersionGroupId(tx.nVersionGroupId), nExpiryHeight(tx.nExpiryHeight),
                              nConsensusBranchId(tx.nConsensusBranchId),
                              vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime),
                              valueBalance(tx.ValueBalanceFromBundle()),
                              vjoinsplit(tx.vjoinsplit), joinSplitPubKey(tx.joinSplitPubKey), joinSplitSig(tx.joinSplitSig),
                              bindingSig(tx.BindingSigFromBundle()),
                              saplingBundle(tx.saplingBundle), orchardBundle(tx.orchardBundle)
{
    assert(evilDeveloperFlag);
}

CTransaction::CTransaction(CMutableTransaction&& tx) : nVersion(tx.nVersion), fOverwintered(tx.fOverwintered), nVersionGroupId(tx.nVersionGroupId),
                                                       nConsensusBranchId(tx.nConsensusBranchId),
                                                       vin(std::move(tx.vin)), vout(std::move(tx.vout)), nLockTime(tx.nLockTime), nExpiryHeight(tx.nExpiryHeight),
                                                       valueBalance(tx.ValueBalanceFromBundle()),
                                                       vjoinsplit(std::move(tx.vjoinsplit)), joinSplitPubKey(std::move(tx.joinSplitPubKey)), joinSplitSig(std::move(tx.joinSplitSig)),
                                                       bindingSig(tx.BindingSigFromBundle()),
                                                       saplingBundle(std::move(tx.saplingBundle)), orchardBundle(std::move(tx.orchardBundle))
{
    UpdateHash();
}

CTransaction& CTransaction::operator=(const CTransaction& tx)
{
    *const_cast<bool*>(&fOverwintered) = tx.fOverwintered;
    *const_cast<int*>(&nVersion) = tx.nVersion;
    *const_cast<uint32_t*>(&nVersionGroupId) = tx.nVersionGroupId;
    nConsensusBranchId = tx.nConsensusBranchId;
    *const_cast<std::vector<CTxIn>*>(&vin) = tx.vin;
    *const_cast<std::vector<CTxOut>*>(&vout) = tx.vout;
    *const_cast<unsigned int*>(&nLockTime) = tx.nLockTime;
    *const_cast<uint32_t*>(&nExpiryHeight) = tx.nExpiryHeight;
    saplingBundle = tx.saplingBundle;
    orchardBundle = tx.orchardBundle;
    *const_cast<CAmount*>(&valueBalance) = tx.ValueBalanceFromBundle();
    *const_cast<std::vector<JSDescription>*>(&vjoinsplit) = tx.vjoinsplit;
    *const_cast<uint256*>(&joinSplitPubKey) = tx.joinSplitPubKey;
    *const_cast<joinsplit_sig_t*>(&joinSplitSig) = tx.joinSplitSig;
    *const_cast<binding_sig_t*>(&bindingSig) = tx.BindingSigFromBundle();
    *const_cast<uint256*>(&wtxid.hash) = tx.wtxid.hash;
    *const_cast<uint256*>(&wtxid.authDigest) = tx.wtxid.authDigest;
    return *this;
}

CAmount CTransaction::GetValueOut() const
{
    CAmount nValueOut = 0;
    for (const auto& out : vout) {
        if (!MoneyRange(out.nValue)) {
            throw std::runtime_error("CTransaction::GetValueOut(): nValue out of range");
        }
        nValueOut += out.nValue;
        if (!MoneyRange(nValueOut)) {
            throw std::runtime_error("CTransaction::GetValueOut(): nValueOut out of range");
        }
    }

    auto valueBalanceSapling = saplingBundle.GetValueBalance();
    if (valueBalanceSapling <= 0) {
        // NB: negative valueBalanceSapling "takes" money from the transparent value pool just as outputs do
        if (valueBalanceSapling < -MAX_MONEY) {
            throw std::runtime_error("CTransaction::GetValueOut(): valueBalanceSapling out of range");
        }
        nValueOut += -valueBalanceSapling;

        if (!MoneyRange(nValueOut)) {
            throw std::runtime_error("CTransaction::GetValueOut(): value out of range");
        }
    }

    auto valueBalanceOrchard = orchardBundle.GetValueBalance();
    if (valueBalanceOrchard <= 0) {
        // NB: negative valueBalanceOrchard "takes" money from the transparent value pool just as outputs do
        if (valueBalanceOrchard < -MAX_MONEY) {
            throw std::runtime_error("CTransaction::GetValueOut(): valueBalanceOrchard out of range");
        }
        nValueOut += -valueBalanceOrchard;

        if (!MoneyRange(nValueOut)) {
            throw std::runtime_error("CTransaction::GetValueOut(): value out of range");
        }
    }

    for (const auto& jsDescription : vjoinsplit) {
        // NB: vpub_old "takes" money from the transparent value pool just as outputs do
        if (!MoneyRange(jsDescription.vpub_old)) {
            throw std::runtime_error("CTransaction::GetValueOut(): vpub_old out of range");
        }
        nValueOut += jsDescription.vpub_old;
        if (!MoneyRange(nValueOut)) {
            throw std::runtime_error("CTransaction::GetValueOut(): value out of range");
        }
    }
    return nValueOut;
}

CAmount CTransaction::GetShieldedValueIn() const
{
    CAmount nValue = 0;

    auto valueBalanceSapling = saplingBundle.GetValueBalance();
    if (valueBalanceSapling >= 0) {
        // NB: positive valueBalanceSapling "gives" money to the transparent value pool just as inputs do
        if (valueBalanceSapling > MAX_MONEY) {
            throw std::runtime_error("CTransaction::GetShieldedValueIn(): valueBalanceSapling out of range");
        }
        nValue += valueBalanceSapling;

        if (!MoneyRange(nValue)) {
            throw std::runtime_error("CTransaction::GetShieldedValueIn(): value out of range");
        }
    }

    auto valueBalanceOrchard = orchardBundle.GetValueBalance();
    if (valueBalanceOrchard >= 0) {
        // NB: positive valueBalanceOrchard "gives" money to the transparent value pool just as inputs do
        if (valueBalanceOrchard > MAX_MONEY) {
            throw std::runtime_error("CTransaction::GetShieldedValueIn(): valueBalanceOrchard out of range");
        }
        nValue += valueBalanceOrchard;
        if (!MoneyRange(nValue)) {
            throw std::runtime_error("CTransaction::GetShieldedValueIn(): nValue out of range");
        }
    }

    for (const auto& jsDescription : vjoinsplit) {
        // NB: vpub_new "gives" money to the transparent value pool just as inputs do
        if (!MoneyRange(jsDescription.vpub_new)) {
            throw std::runtime_error("CTransaction::GetShieldedValueIn(): vpub_new out of range");
        }
        nValue += jsDescription.vpub_new;
        if (!MoneyRange(nValue)) {
            throw std::runtime_error("CTransaction::GetShieldedValueIn(): value out of range");
        }
    }

    if (IsCoinBase() && nValue != 0) {
        throw std::runtime_error("CTransaction::GetShieldedValueIn(): shielded value of inputs must be zero in coinbase transactions.");
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
                         GetSaplingSpendsCount(),
                         GetSaplingOutputsCount());
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

const rust::Vec<sapling::Spend> CTransaction::GetSaplingSpends() const
{
    return saplingBundle.GetDetails().spends();
}

const rust::Vec<sapling::Output> CTransaction::GetSaplingOutputs() const
{
    return saplingBundle.GetDetails().outputs();
}

const rust::Vec<orchard_bundle::Action> CTransaction::GetOrchardActions() const
{
    return orchardBundle.GetDetails().actions();
}

size_t CTransaction::GetOrchardActionsCount() const
{
    return orchardBundle.GetNumActions();
}

/**
 * Returns the Sapling value balance for the transaction.
 */
CAmount CTransaction::GetValueBalanceSapling() const
{
    return saplingBundle.GetValueBalance();
}

/**
 * Returns the Orchard value balance for the transaction.
 */
CAmount CTransaction::GetValueBalanceOrchard() const
{
    return orchardBundle.GetValueBalance();
}

/**
 * Returns the Sapling bundle for the transaction.
 */
const SaplingBundle& CTransaction::GetSaplingBundle() const
{
    return saplingBundle;
}

/**
 * Returns the Orchard bundle for the transaction.
 */
const OrchardBundle& CTransaction::GetOrchardBundle() const
{
    return orchardBundle;
}
