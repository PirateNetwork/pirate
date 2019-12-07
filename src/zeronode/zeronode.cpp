// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zeronode/zeronode.h"
#include "addrman.h"
#include "zeronode/zeronodeman.h"
#include "zeronode/obfuscation.h"
#include "sync.h"
#include "util.h"
#include "consensus/validation.h"
#include <boost/lexical_cast.hpp>

#include "key_io.h"

// keep track of the scanning errors I've seen
map<uint256, int> mapSeenZeronodeScanningErrors;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    if (chainActive.Tip() == NULL) return false;

    if (nBlockHeight == 0)
        nBlockHeight = chainActive.Tip()->nHeight;

    if (mapCacheBlockHashes.count(nBlockHeight)) {
        hash = mapCacheBlockHashes[nBlockHeight];
        return true;
    }

    const CBlockIndex* BlockLastSolved = chainActive.Tip();
    const CBlockIndex* BlockReading = chainActive.Tip();

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || chainActive.Tip()->nHeight + 1 < nBlockHeight) return false;

    int nBlocksAgo = 0;
    if (nBlockHeight > 0) nBlocksAgo = (chainActive.Tip()->nHeight + 1) - nBlockHeight;
    assert(nBlocksAgo >= 0);

    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nBlocksAgo) {
            hash = BlockReading->GetBlockHash();
            mapCacheBlockHashes[nBlockHeight] = hash;
            return true;
        }
        n++;

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return false;
}

CZeronode::CZeronode()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyZeronode = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = ZERONODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CZeronodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    nActiveState = ZERONODE_ENABLED,
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
}

CZeronode::CZeronode(const CZeronode& other)
{
    LOCK(cs);
    vin = other.vin;
    addr = other.addr;
    pubKeyCollateralAddress = other.pubKeyCollateralAddress;
    pubKeyZeronode = other.pubKeyZeronode;
    sig = other.sig;
    activeState = other.activeState;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    cacheInputAge = other.cacheInputAge;
    cacheInputAgeBlock = other.cacheInputAgeBlock;
    unitTest = other.unitTest;
    allowFreeTx = other.allowFreeTx;
    nActiveState = ZERONODE_ENABLED,
    protocolVersion = other.protocolVersion;
    nLastDsq = other.nLastDsq;
    nScanningErrorCount = other.nScanningErrorCount;
    nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
    lastTimeChecked = 0;
}

CZeronode::CZeronode(const CZeronodeBroadcast& znb)
{
    LOCK(cs);
    vin = znb.vin;
    addr = znb.addr;
    pubKeyCollateralAddress = znb.pubKeyCollateralAddress;
    pubKeyZeronode = znb.pubKeyZeronode;
    sig = znb.sig;
    activeState = ZERONODE_ENABLED;
    sigTime = znb.sigTime;
    lastPing = znb.lastPing;
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    nActiveState = ZERONODE_ENABLED,
    protocolVersion = znb.protocolVersion;
    nLastDsq = znb.nLastDsq;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
}

//
// When a new zeronode broadcast is sent, update our information
//
bool CZeronode::UpdateFromNewBroadcast(CZeronodeBroadcast& znb)
{
    if (znb.sigTime > sigTime) {
        pubKeyZeronode = znb.pubKeyZeronode;
        pubKeyCollateralAddress = znb.pubKeyCollateralAddress;
        sigTime = znb.sigTime;
        sig = znb.sig;
        protocolVersion = znb.protocolVersion;
        addr = znb.addr;
        lastTimeChecked = 0;
        int nDoS = 0;
        if (znb.lastPing == CZeronodePing() || (znb.lastPing != CZeronodePing() && znb.lastPing.CheckAndUpdate(nDoS, false))) {
            lastPing = znb.lastPing;
            znodeman.mapSeenZeronodePing.insert(make_pair(lastPing.GetHash(), lastPing));
        }
        return true;
    }
    return false;
}

//
// Deterministically calculate a given "score" for a Zeronode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
arith_uint256 CZeronode::CalculateScore(const uint256& blockHash)
{
    uint256 aux = ArithToUint256(UintToArith256(vin.prevout.hash) + vin.prevout.n);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << blockHash;
    arith_uint256 hash2 = UintToArith256(ss.GetHash());

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << blockHash;
    ss2 << aux;
    arith_uint256 hash3 = UintToArith256(ss2.GetHash());

    return (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);
}

void CZeronode::Check(bool forceCheck)
{
    if (ShutdownRequested()) return;

    if (!forceCheck && (GetTime() - lastTimeChecked < ZERONODE_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();


    //once spent, stop doing the checks
    if (activeState == ZERONODE_VIN_SPENT) return;


    if (!IsPingedWithin(ZERONODE_REMOVAL_SECONDS)) {
        activeState = ZERONODE_REMOVE;
        return;
    }

    if (!IsPingedWithin(ZERONODE_EXPIRATION_SECONDS)) {
        activeState = ZERONODE_EXPIRED;
        return;
    }

    if (!unitTest) {
        CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CScript scriptPubKey;
        if (!GetTestingCollateralScript(Params().ZeronodeDummyAddress(), scriptPubKey)){
            LogPrintf("%s: Failed to get a valid scriptPubkey\n", __func__);
            return;
        }

        CTxOut vout = CTxOut(9999.99 * COIN, scriptPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;

            if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
                activeState = ZERONODE_VIN_SPENT;
                return;
            }
        }
    }

    activeState = ZERONODE_ENABLED; // OK
}

int64_t CZeronode::SecondsSincePayment()
{
    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    int64_t sec = (GetAdjustedTime() - GetLastPaid());
    int64_t month = 60 * 60 * 24 * 30;
    if (sec < month) return sec; //if it's less than 30 days, give seconds

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // return some deterministic value for unknown/unpaid but force it to be more than 30 days old
    return month + (UintToArith256(hash)).GetCompact(false);
}

int64_t CZeronode::GetLastPaid()
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return false;

    CScript znpayee;
    znpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // use a deterministic offset to break a tie -- 2.5 minutes
    int64_t nOffset = (UintToArith256(hash)).GetCompact(false) % 150;

    if (chainActive.Tip() == NULL) return false;

    const CBlockIndex* BlockReading = chainActive.Tip();

    int nMnCount = znodeman.CountEnabled() * 1.25;
    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nMnCount) {
            return 0;
        }
        n++;

        if (zeronodePayments.mapZeronodeBlocks.count(BlockReading->nHeight)) {
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network
                to converge on the same payees quickly, then keep the same schedule.
            */
            if (zeronodePayments.mapZeronodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(znpayee, 2)) {
                return BlockReading->nTime + nOffset;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return 0;
}

std::string CZeronode::GetStatus()
{
    switch (nActiveState) {
    case CZeronode::ZERONODE_PRE_ENABLED:
        return "PRE_ENABLED";
    case CZeronode::ZERONODE_ENABLED:
        return "ENABLED";
    case CZeronode::ZERONODE_EXPIRED:
        return "EXPIRED";
    case CZeronode::ZERONODE_OUTPOINT_SPENT:
        return "OUTPOINT_SPENT";
    case CZeronode::ZERONODE_REMOVE:
        return "REMOVE";
    case CZeronode::ZERONODE_WATCHDOG_EXPIRED:
        return "WATCHDOG_EXPIRED";
    case CZeronode::ZERONODE_POSE_BAN:
        return "POSE_BAN";
    default:
        return "UNKNOWN";
    }
}

bool CZeronode::IsValidNetAddr()
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return NetworkIdFromCommandLine() == CBaseChainParams::REGTEST ||
           (IsReachable(addr) && addr.IsRoutable());
}

CZeronodeBroadcast::CZeronodeBroadcast()
{
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyZeronode1 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = ZERONODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CZeronodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CZeronodeBroadcast::CZeronodeBroadcast(CService newAddr, CTxIn newVin, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyZeronodeNew, int protocolVersionIn)
{
    vin = newVin;
    addr = newAddr;
    pubKeyCollateralAddress = pubKeyCollateralAddressNew;
    pubKeyZeronode = pubKeyZeronodeNew;
    sig = std::vector<unsigned char>();
    activeState = ZERONODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CZeronodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = protocolVersionIn;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CZeronodeBroadcast::CZeronodeBroadcast(const CZeronode& zn)
{
    vin = zn.vin;
    addr = zn.addr;
    pubKeyCollateralAddress = zn.pubKeyCollateralAddress;
    pubKeyZeronode = zn.pubKeyZeronode;
    sig = zn.sig;
    activeState = zn.activeState;
    sigTime = zn.sigTime;
    lastPing = zn.lastPing;
    cacheInputAge = zn.cacheInputAge;
    cacheInputAgeBlock = zn.cacheInputAgeBlock;
    unitTest = zn.unitTest;
    allowFreeTx = zn.allowFreeTx;
    protocolVersion = zn.protocolVersion;
    nLastDsq = zn.nLastDsq;
    nScanningErrorCount = zn.nScanningErrorCount;
    nLastScanningErrorBlockHeight = zn.nLastScanningErrorBlockHeight;
}

bool CZeronodeBroadcast::Create(std::string strService, std::string strKeyZeronode, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CZeronodeBroadcast& znbRet, bool fOffline)
{
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyZeronodeNew;
    CKey keyZeronodeNew;

    //need correct blocks to send ping
    if (!fOffline && !zeronodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Zeronode";
        LogPrint("zeronode","CZeronodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!obfuScationSigner.GetKeysFromSecret(strKeyZeronode, keyZeronodeNew, pubKeyZeronodeNew)) {
        strErrorRet = strprintf("Invalid zeronode key %s", strKeyZeronode);
        LogPrint("zeronode","CZeronodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!pwalletMain->GetZeronodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for zeronode %s", strTxHash, strOutputIndex, strService);
        LogPrint("zeronode","CZeronodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    CService service = CService(strService);
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (NetworkIdFromCommandLine() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            strErrorRet = strprintf("Invalid port %u for zeronode %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
            LogPrint("zeronode","CZeronodeBroadcast::Create -- %s\n", strErrorRet);
            return false;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for zeronode %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
        LogPrint("zeronode","CZeronodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    return Create(txin, CService(strService), keyCollateralAddressNew, pubKeyCollateralAddressNew, keyZeronodeNew, pubKeyZeronodeNew, strErrorRet, znbRet);
}

bool CZeronodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyZeronodeNew, CPubKey pubKeyZeronodeNew, std::string& strErrorRet, CZeronodeBroadcast& znbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("zeronode", "CZeronodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyZeronodeNew.GetID() = %s\n",
        EncodeDestination(pubKeyCollateralAddressNew.GetID()),
        pubKeyZeronodeNew.GetID().ToString());

    CZeronodePing znp(txin);
    if (!znp.Sign(keyZeronodeNew, pubKeyZeronodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, zeronode=%s", txin.prevout.hash.ToString());
        LogPrint("zeronode","CZeronodeBroadcast::Create -- %s\n", strErrorRet);
        znbRet = CZeronodeBroadcast();
        return false;
    }

    znbRet = CZeronodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyZeronodeNew, PROTOCOL_VERSION);

    if (!znbRet.IsValidNetAddr()) {
        strErrorRet = strprintf("Invalid IP address %s, zeronode=%s", znbRet.addr.ToStringIP (), txin.prevout.hash.ToString());
        LogPrint("zeronode","CZeronodeBroadcast::Create -- %s\n", strErrorRet);
        znbRet = CZeronodeBroadcast();
        return false;
    }

    znbRet.lastPing = znp;
    if (!znbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, zeronode=%s", txin.prevout.hash.ToString());
        LogPrint("zeronode","CZeronodeBroadcast::Create -- %s\n", strErrorRet);
        znbRet = CZeronodeBroadcast();
        return false;
    }

    return true;
}

bool CZeronodeBroadcast::CheckAndUpdate(int& nDos)
{
    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("zeronode","znb - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyZeronode.begin(), pubKeyZeronode.end());
    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if (protocolVersion < zeronodePayments.GetMinZeronodePaymentsProto()) {
        LogPrint("zeronode","znb - ignoring outdated Zeronode %s protocol version %d\n", vin.prevout.hash.ToString(), protocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25) {
        LogPrint("zeronode","znb - pubkey the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyZeronode.GetID());

    if (pubkeyScript2.size() != 25) {
        LogPrint("zeronode","znb - pubkey2 the wrong size\n");
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty()) {
        LogPrint("zeronode","znb - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
        return false;
    }

    std::string errorMessage = "";
    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, strMessage, errorMessage)) {
        LogPrint("zeronode","znb - Got bad Zeronode address signature\n");
        nDos = 100;
        return false;
    }

    if (NetworkIdFromCommandLine() == CBaseChainParams::MAIN) {
        if (addr.GetPort() != 23801) return false;
    } else if (addr.GetPort() == 23801)
        return false;

    //search existing Zeronode list, this is where we update existing Zeronodes with new znb broadcasts
    CZeronode* pzn = znodeman.Find(vin);

    // no such zeronode, nothing to update
    if (pzn == NULL)
        return true;
    else {
        // this broadcast older than we have, it's bad.
        if (pzn->sigTime > sigTime) {
            LogPrint("zeronode","znb - Bad sigTime %d for Zeronode %s (existing broadcast is at %d)\n",
                sigTime, vin.prevout.hash.ToString(), pzn->sigTime);
            return false;
        }
        // zeronode is not enabled yet/already, nothing to update
        if (!pzn->IsEnabled()) return true;
    }

    // zn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if (pzn->pubKeyCollateralAddress == pubKeyCollateralAddress && !pzn->IsBroadcastedWithin(ZERONODE_MIN_MNB_SECONDS)) {
        //take the newest entry
        LogPrint("zeronode","znb - Got updated entry for %s\n", vin.prevout.hash.ToString());
        if (pzn->UpdateFromNewBroadcast((*this))) {
            pzn->Check();
            if (pzn->IsEnabled()) Relay();
        }
        zeronodeSync.AddedZeronodeList(GetHash());
    }

    return true;
}

bool CZeronodeBroadcast::CheckInputsAndAdd(int& nDoS)
{
    LogPrint("zeronode", "CheckInputsAndAdd\n");
    // we are a zeronode with the same vin (i.e. already activated) and this znb is ours (matches our Zeronode privkey)
    // so nothing to do here for us
    if (fZeroNode && vin.prevout == activeZeronode.vin.prevout && pubKeyZeronode == activeZeronode.pubKeyZeronode)
        return true;

    // search existing Zeronode list
    CZeronode* pzn = znodeman.Find(vin);

    if (pzn != NULL) {
        // nothing to do here if we already know about this zeronode and it's enabled
        if (pzn->IsEnabled()) return true;
        // if it's not enabled, remove old MN first and continue
        else
            znodeman.Remove(pzn->vin);
    }

    CValidationState state;
    CMutableTransaction tx = CMutableTransaction();
    CScript scriptPubKey;
    if (!GetTestingCollateralScript(Params().ZeronodeDummyAddress(), scriptPubKey)){
        LogPrintf("%s: Failed to get a valid scriptPubkey\n", __func__);
        return false;
    }

    CTxOut vout = CTxOut(9999.99 * COIN, scriptPubKey);
    tx.vin.push_back(vin);
    tx.vout.push_back(vout);

    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            LogPrint("zeronode", "lockMain\n");
            // not znb fault, let it to be checked again later
            znodeman.mapSeenZeronodeBroadcast.erase(GetHash());
            zeronodeSync.mapSeenSyncZNB.erase(GetHash());
            return false;
        }

        if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
            LogPrint("zeronode", "!AcceptableInputs\n");
            //set nDos
            state.IsInvalid(nDoS);
            return false;
        }
    }

    LogPrint("zeronode", "znb - Accepted Zeronode entry\n");

    if (GetInputAge(vin) < ZERONODE_MIN_CONFIRMATIONS) {
        LogPrint("zeronode","znb - Input must have at least %d confirmations\n", ZERONODE_MIN_CONFIRMATIONS);
        // maybe we miss few blocks, let this znb to be checked again later
        znodeman.mapSeenZeronodeBroadcast.erase(GetHash());
        zeronodeSync.mapSeenSyncZNB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 10000 ZER tx got ZERONODE_MIN_CONFIRMATIONS
    uint256 hashBlock = uint256();
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, Params().GetConsensus(), hashBlock, true);
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second) {
        CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 1000 Zero tx -> 1 confirmation
        CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + ZERONODE_MIN_CONFIRMATIONS - 1]; // block where tx got ZERONODE_MIN_CONFIRMATIONS
        if (pConfIndex->GetBlockTime() > sigTime) {
            LogPrint("zeronode","znb - Bad sigTime %d for Zeronode %s (%i conf block is at %d)\n",
                sigTime, vin.prevout.hash.ToString(), ZERONODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
            return false;
        }
    }

    LogPrint("zeronode","znb - Got NEW Zeronode entry - %s - %lli \n", vin.prevout.hash.ToString(), sigTime);
    CZeronode zn(*this);
    znodeman.Add(zn);

    // if it matches our Zeronode privkey, then we've been remotely activated
    if (pubKeyZeronode == activeZeronode.pubKeyZeronode && protocolVersion == PROTOCOL_VERSION) {
        activeZeronode.EnableHotColdZeroNode(vin, addr);
    }

    bool isLocal = addr.IsRFC1918() || addr.IsLocal();
    if (NetworkIdFromCommandLine() == CBaseChainParams::REGTEST) isLocal = false;

    if (!isLocal) Relay();

    return true;
}

void CZeronodeBroadcast::Relay()
{
    CInv inv(MSG_ZERONODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

bool CZeronodeBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string errorMessage;

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyZeronode.begin(), pubKeyZeronode.end());

    sigTime = GetAdjustedTime();

    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, sig, keyCollateralAddress)) {
        LogPrint("zeronode","CZeronodeBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, strMessage, errorMessage)) {
        LogPrint("zeronode","CZeronodeBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

CZeronodePing::CZeronodePing()
{
    vin = CTxIn();
    blockHash = uint256();
    sigTime = 0;
    vchSig = std::vector<unsigned char>();
}

CZeronodePing::CZeronodePing(CTxIn& newVin)
{
    vin = newVin;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}


bool CZeronodePing::Sign(CKey& keyZeronode, CPubKey& pubKeyZeronode)
{
    std::string errorMessage;
    std::string strZeroNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig, keyZeronode)) {
        LogPrint("zeronode","CZeronodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyZeronode, vchSig, strMessage, errorMessage)) {
        LogPrint("zeronode","CZeronodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CZeronodePing::CheckAndUpdate(int& nDos, bool fRequireEnabled)
{
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("zeronode","CZeronodePing::CheckAndUpdate - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    if (sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrint("zeronode","CZeronodePing::CheckAndUpdate - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
        nDos = 1;
        return false;
    }

    LogPrint("zeronode","CZeronodePing::CheckAndUpdate - New Ping - %s - %lli\n", blockHash.ToString(), sigTime);

    // see if we have this Zeronode
    CZeronode* pzn = znodeman.Find(vin);
    if (pzn != NULL && pzn->protocolVersion >= zeronodePayments.GetMinZeronodePaymentsProto()) {
        if (fRequireEnabled && !pzn->IsEnabled()) return false;

        // LogPrint("zeronode","znping - Found corresponding zn for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this zeronode or
        // last ping was more then ZERONODE_MIN_MNP_SECONDS-60 ago comparing to this one
        if (!pzn->IsPingedWithin(ZERONODE_MIN_MNP_SECONDS - 60, sigTime)) {
            std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

            std::string errorMessage = "";
            if (!obfuScationSigner.VerifyMessage(pzn->pubKeyZeronode, vchSig, strMessage, errorMessage)) {
                LogPrint("zeronode","CZeronodePing::CheckAndUpdate - Got bad Zeronode address signature %s\n", vin.prevout.hash.ToString());
                nDos = 33;
                return false;
            }

            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                if ((*mi).second->nHeight < chainActive.Height() - 24) {
                    LogPrint("zeronode","CZeronodePing::CheckAndUpdate - Zeronode %s block hash %s is too old\n", vin.prevout.hash.ToString(), blockHash.ToString());
                    // Do nothing here (no Zeronode update, no znping relay)
                    // Let this node to be visible but fail to accept znping

                    return false;
                }
            } else {
                if (fDebug) LogPrint("zeronode","CZeronodePing::CheckAndUpdate - Zeronode %s block hash %s is unknown\n", vin.prevout.hash.ToString(), blockHash.ToString());
                // maybe we stuck so we shouldn't ban this node, just fail to accept it
                // TODO: or should we also request this block?

                return false;
            }

            pzn->lastPing = *this;

            //znodeman.mapSeenZeronodeBroadcast.lastPing is probably outdated, so we'll update it
            CZeronodeBroadcast znb(*pzn);
            uint256 hash = znb.GetHash();
            if (znodeman.mapSeenZeronodeBroadcast.count(hash)) {
                znodeman.mapSeenZeronodeBroadcast[hash].lastPing = *this;
            }

            pzn->Check(true);
            if (!pzn->IsEnabled()) return false;

            LogPrint("zeronode", "CZeronodePing::CheckAndUpdate - Zeronode ping accepted, vin: %s\n", vin.prevout.hash.ToString());

            Relay();
            return true;
        }
        LogPrint("zeronode", "CZeronodePing::CheckAndUpdate - Zeronode ping arrived too early, vin: %s\n", vin.prevout.hash.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint("zeronode", "CZeronodePing::CheckAndUpdate - Couldn't find compatible Zeronode entry, vin: %s\n", vin.prevout.hash.ToString());

    return false;
}

void CZeronodePing::Relay()
{
    CInv inv(MSG_ZERONODE_PING, GetHash());
    RelayInv(inv);
}
