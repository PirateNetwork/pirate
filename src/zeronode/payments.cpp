// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zeronode/payments.h"
#include "addrman.h"
#include "zeronode/budget.h"
#include "zeronode/zeronode-sync.h"
#include "zeronode/zeronodeman.h"
#include "zeronode/obfuscation.h"
#include "zeronode/spork.h"
#include "sync.h"
#include "util.h"
#include "utilmoneystr.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#include "key_io.h"

/** Object for who's going to get paid on which blocks */
CZeronodePayments zeronodePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapZeronodeBlocks;
CCriticalSection cs_mapZeronodePayeeVotes;

//
// CZeronodePaymentDB
//

CZeronodePaymentDB::CZeronodePaymentDB()
{
    pathDB = GetDataDir() / "znpayments.dat";
    strMagicMessage = "ZeronodePayments";
}

bool CZeronodePaymentDB::Write(const CZeronodePayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // zeronode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint("zeronode","Written info to znpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CZeronodePaymentDB::ReadResult CZeronodePaymentDB::Read(CZeronodePayments& objToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (zeronode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid zeronode payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CZeronodePayments object
        ssObj >> objToLoad;
    } catch (std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("zeronode","Loaded info from znpayments.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("zeronode","  %s\n", objToLoad.ToString());
    if (!fDryRun) {
        LogPrint("zeronode","Zeronode payments manager - cleaning....\n");
        objToLoad.CleanPaymentList();
        LogPrint("zeronode","Zeronode payments manager - result:\n");
        LogPrint("zeronode","  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpZeronodePayments()
{
    int64_t nStart = GetTimeMillis();

    CZeronodePaymentDB paymentdb;
    CZeronodePayments tempPayments;

    LogPrint("zeronode","Verifying znpayments.dat format...\n");
    CZeronodePaymentDB::ReadResult readResult = paymentdb.Read(tempPayments, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CZeronodePaymentDB::FileError)
        LogPrint("zeronode","Missing budgets file - znpayments.dat, will try to recreate\n");
    else if (readResult != CZeronodePaymentDB::Ok) {
        LogPrint("zeronode","Error reading znpayments.dat: ");
        if (readResult == CZeronodePaymentDB::IncorrectFormat)
            LogPrint("zeronode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("zeronode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("zeronode","Writting info to znpayments.dat...\n");
    paymentdb.Write(zeronodePayments);

    LogPrint("zeronode","Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return true;

    int nHeight = 0;
    if (pindexPrev->GetBlockHash() == block.hashPrevBlock) {
        nHeight = pindexPrev->nHeight + 1;
    } else { //out of order
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
            nHeight = (*mi).second->nHeight + 1;
    }

    if (nHeight == 0) {
        LogPrint("zeronode","IsBlockValueValid() : WARNING: Couldn't find previous block\n");
    }

    if (!zeronodeSync.IsSynced()) { //there is no budget data to use to check anything
        //super blocks will always be on these blocks, max 100 per budgeting
        if (nHeight % GetBudgetPaymentCycleBlocks() < 100) {
            return true;
        } else {
            if(block.vtx[0].GetValueOut() > nExpectedValue) return false;
        }
    } else { // we're synced and have data so check the budget schedule

        //are these blocks even enabled
        if (!IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
            if(block.vtx[0].GetValueOut() > nExpectedValue) return false;
        }

        if (budget.IsBudgetPaymentBlock(nHeight)) {
            //the value of the block is evaluated in CheckBlock
            return true;
        } else {
            if(block.vtx[0].GetValueOut() > nExpectedValue) return false;
        }
    }

    return true;
}

bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight)
{
    if (!zeronodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrint("zeronode", "Client not synced, skipping block payee checks\n");
        return true;
    }

    const CTransaction& txNew = block.vtx[0];

    //check if it's a budget block
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
        if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
            if (budget.IsTransactionValid(txNew, nBlockHeight))
                return true;

            LogPrint("zeronode","Invalid budget payment detected %s\n", txNew.ToString().c_str());
            if (IsSporkActive(SPORK_9_ZERONODE_BUDGET_ENFORCEMENT))
                return false;

            LogPrint("zeronode","Budget enforcement is disabled, accepting block\n");
            return true;
        }
    }

    //check for zeronode payee
    if (zeronodePayments.IsTransactionValid(txNew, nBlockHeight))
        return true;

    LogPrint("zeronode","Invalid zn payment detected %s\n", txNew.ToString().c_str());

    if (IsSporkActive(SPORK_8_ZERONODE_PAYMENT_ENFORCEMENT))
        return false;
    LogPrint("zeronode","Zeronode payment enforcement is disabled, accepting block\n");

    return true;
}


void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, CTxOut& txFounders, CTxOut& txZeronodes)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(pindexPrev->nHeight + 1)) {
        budget.FillBlockPayee(txNew, nFees, txFounders, txZeronodes);
    } else {
        zeronodePayments.FillBlockPayee(txNew, nFees, txFounders, txZeronodes);
    }
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(nBlockHeight)) {
        return budget.GetRequiredPaymentsString(nBlockHeight);
    } else {
        return zeronodePayments.GetRequiredPaymentsString(nBlockHeight);
    }
}

void CZeronodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, CTxOut& txFounders, CTxOut& txZeronodes)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    bool hasPayment = true;
    int nHeight = pindexPrev->nHeight+1;
    CScript payee;

    //spork
    if(!zeronodePayments.GetBlockPayee(nHeight, payee)){
        //no zeronode detected
        CZeronode* winningNode = znodeman.GetCurrentZeroNode(1);
        if (winningNode) {
            payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
        } else {
            LogPrint("zeronode","CreateNewBlock: Failed to detect zeronode to pay\n");
            hasPayment = false;
        }
    }

    CAmount blockValue = GetBlockSubsidy(nHeight, Params().GetConsensus());
    CAmount zeronodePayment = GetZeronodePayment(nHeight, blockValue);
    CAmount minerValue = blockValue;

    // Founders reward
    CAmount vFoundersReward = blockValue * 7.5 / 100;

    if(hasPayment){
        minerValue -= zeronodePayment;
    }

    txNew.vout[0].nValue = minerValue + nFees;

    if ((nHeight >= Params().GetConsensus().nFeeStartBlockHeight) && (nHeight <= Params().GetConsensus().GetLastFoundersRewardBlockHeight(nHeight))) {
        // Take some reward away from us
        txNew.vout[0].nValue -= vFoundersReward;

        // And give it to the founders
        txFounders = CTxOut(vFoundersReward, Params().GetFoundersRewardScriptAtHeight(nHeight));
        txNew.vout.push_back(txFounders);
    }

    //@TODO zeronode
    if(hasPayment == true && zeronodePayment > 0) {
        txZeronodes = CTxOut(zeronodePayment, payee);
        txNew.vout.push_back(txZeronodes);

        CTxDestination address1;
        ExtractDestination(payee, address1);

          LogPrint("zeronode","Zeronode payment to %s\n", EncodeDestination(address1));
    }
    LogPrint("zeronode","Total miner to %s\n", FormatMoney(txNew.vout[0].nValue).c_str());
    LogPrint("zeronode","Total founder to %s\n", FormatMoney(txFounders.nValue).c_str());
    LogPrint("zeronode","Total zero node to %s\n", FormatMoney(txZeronodes.nValue).c_str());
    LogPrint("zeronode","Total Coinbase to %s\n", FormatMoney(txNew.vout[0].nValue+txFounders.nValue+txZeronodes.nValue).c_str());
}

int CZeronodePayments::GetMinZeronodePaymentsProto()
{
        return MIN_PEER_PROTO_VERSION_ENFORCEMENT; // Also allow old peers as long as they are allowed to run
}

void CZeronodePayments::ProcessMessageZeronodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (!zeronodeSync.IsBlockchainSynced()) return;

    if (fLiteMode) return; //disable all Obfuscation/Zeronode related functionality


    if (strCommand == "znget") { //Zeronode Payments Request Sync
        if (fLiteMode) return;   //disable all Obfuscation/Zeronode related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (NetworkIdFromCommandLine() == CBaseChainParams::MAIN) {
            if (pfrom->HasFulfilledRequest("znget")) {
                LogPrint("zeronode","znget - peer already asked me for the list\n");
                Misbehaving(pfrom->GetId(), 20);
                return;
            }
        }

        pfrom->FulfilledRequest("znget");
        zeronodePayments.Sync(pfrom, nCountNeeded);
        LogPrint("znpayments", "znget - Sent Zeronode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == "znw") { //Zeronode Payments Declare Winner
        //this is required in litemodef
        CZeronodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < ActiveProtocol()) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        if (zeronodePayments.mapZeronodePayeeVotes.count(winner.GetHash())) {
            LogPrint("znpayments", "znw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            zeronodeSync.AddedZeronodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (znodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint("znpayments", "znw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError)) {
            if(strError != "") LogPrint("zeronode","znw - invalid message - %s\n", strError);
            return;
        }

        if (!zeronodePayments.CanVote(winner.vinZeronode.prevout, winner.nBlockHeight)) {
            LogPrint("zeronode","znw - zeronode already voted - %s\n", winner.vinZeronode.prevout.ToStringShort());
            return;
        }

        if (!winner.SignatureValid()) {
            LogPrint("zeronode","znw - invalid signature\n");
            if (zeronodeSync.IsSynced())
                {
                    Misbehaving(pfrom->GetId(), 20);
                }
            // it could just be a non-synced zeronode
            znodeman.AskForZN(pfrom, winner.vinZeronode);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);

        //   LogPrint("znpayments", "znw - winning vote - Addr %s Height %d bestHeight %d - %s\n", EncodeDestination(address1), winner.nBlockHeight, nHeight, winner.vinZeronode.prevout.ToStringShort());

        if (zeronodePayments.AddWinningZeronode(winner)) {
            winner.Relay();
            zeronodeSync.AddedZeronodeWinner(winner.GetHash());
        }
    }
}

bool CZeronodePaymentWinner::Sign(CKey& keyZeronode, CPubKey& pubKeyZeronode)
{
    std::string errorMessage;
    std::string strZeroNodeSignMessage;

    std::string strMessage = vinZeronode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             EncodeDestination(payee);

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig, keyZeronode)) {
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyZeronode, vchSig, strMessage, errorMessage)) {
        return false;
    }

    return true;
}

bool CZeronodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if (mapZeronodeBlocks.count(nBlockHeight)) {
        return mapZeronodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this zeronode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CZeronodePayments::IsScheduled(CZeronode& zn, int nNotBlockHeight)
{
    LOCK(cs_mapZeronodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return false;
        nHeight = chainActive.Tip()->nHeight;
    }

    CScript znpayee;
    znpayee = GetScriptForDestination(zn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapZeronodeBlocks.count(h)) {
            if (mapZeronodeBlocks[h].GetPayee(payee)) {
                if (znpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CZeronodePayments::AddWinningZeronode(CZeronodePaymentWinner& winnerIn)
{
    uint256 blockHash = uint256();

    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100)) {
        return false;
    }

    {
        LOCK2(cs_mapZeronodePayeeVotes, cs_mapZeronodeBlocks);

        if (mapZeronodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

        mapZeronodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if (!mapZeronodeBlocks.count(winnerIn.nBlockHeight)) {
            CZeronodeBlockPayees blockPayees(winnerIn.nBlockHeight);
            mapZeronodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    mapZeronodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, 1);

    return true;
}

bool CZeronodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

	int nMaxSignatures = 0;
    int nZeronode_Drift_Count = 0;

    std::string strPayeesPossible = "";

    CAmount nReward = GetBlockSubsidy(nBlockHeight, Params().GetConsensus());

    if (IsSporkActive(SPORK_8_ZERONODE_PAYMENT_ENFORCEMENT)) {
        // Get a stable number of zeronodes by ignoring newly activated (< 8000 sec old) zeronodes
        nZeronode_Drift_Count = znodeman.stable_size() + Params().ZeronodeCountDrift();
    }
    else {
        //account for the fact that all peers do not see the same zeronode count. A allowance of being off our zeronode count is given
        //we only need to look at an increased zeronode count because as count increases, the reward decreases. This code only checks
        //for znPayment >= required, so it only makes sense to check the max node count allowed.
        nZeronode_Drift_Count = znodeman.size() + Params().ZeronodeCountDrift();
    }

    CAmount requiredZeronodePayment = GetZeronodePayment(nBlockHeight, nReward, nZeronode_Drift_Count);

	//require at least 6 signatures
	BOOST_FOREACH (CZeronodePayee& payee, vecPayments)
    {
        LogPrint("zeronode","Zeronode payment nVotes=%d nMaxSignatures=%d\n", payee.nVotes, nMaxSignatures);
		if (payee.nVotes >= nMaxSignatures && payee.nVotes >= ZNPAYMENTS_SIGNATURES_REQUIRED)
			nMaxSignatures = payee.nVotes;
    }

	//if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
	if (nMaxSignatures < ZNPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH (CZeronodePayee& payee, vecPayments) {
        bool found = false;
        BOOST_FOREACH (CTxOut out, txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                LogPrint("zeronode","Zeronode payment Paid=%s Min=%s\n", FormatMoney(out.nValue).c_str(), FormatMoney(requiredZeronodePayment).c_str());
                if(out.nValue == requiredZeronodePayment)
                    found = true;
                else
                    LogPrint("zeronode","Zeronode payment is out of drift range");
            }
        }

        if (found) return true;


		try {
			CTxDestination address1;
			ExtractDestination(payee.scriptPubKey, address1);

			if (strPayeesPossible == "") {
				strPayeesPossible += EncodeDestination(address1);
			} else {
				strPayeesPossible += "," + EncodeDestination(address1);
			}
        } catch (...) { }
    }

    LogPrint("zeronode","CZeronodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredZeronodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CZeronodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    BOOST_FOREACH (CZeronodePayee& payee, vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);

        if (ret != "Unknown") {
            ret += ", " + EncodeDestination(address1) + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = EncodeDestination(address1) + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        }
    }

    return ret;
}

std::string CZeronodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapZeronodeBlocks);

    if (mapZeronodeBlocks.count(nBlockHeight)) {
        return mapZeronodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CZeronodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapZeronodeBlocks);

    LogPrint("zeronode", "mapZeronodeBlocks size = %d, nBlockHeight = %d", mapZeronodeBlocks.size(), nBlockHeight);
    if (mapZeronodeBlocks.count(nBlockHeight)) {
        LogPrint("zeronode", "mapZeronodeBlocks check transaction");
        return mapZeronodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CZeronodePayments::CleanPaymentList()
{
    LOCK2(cs_mapZeronodePayeeVotes, cs_mapZeronodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(znodeman.size() * 1.25), 1000);

    std::map<uint256, CZeronodePaymentWinner>::iterator it = mapZeronodePayeeVotes.begin();
    while (it != mapZeronodePayeeVotes.end()) {
        CZeronodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint("znpayments", "CZeronodePayments::CleanPaymentList - Removing old Zeronode payment - block %d\n", winner.nBlockHeight);
            zeronodeSync.mapSeenSyncZNW.erase((*it).first);
            mapZeronodePayeeVotes.erase(it++);
            mapZeronodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool CZeronodePaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    CZeronode* pzn = znodeman.Find(vinZeronode);

    if (!pzn) {
        strError = strprintf("Unknown Zeronode %s", vinZeronode.prevout.hash.ToString());
        LogPrint("zeronode","CZeronodePaymentWinner::IsValid - %s\n", strError);
        znodeman.AskForZN(pnode, vinZeronode);
        return false;
    }

    if (pzn->protocolVersion < ActiveProtocol()) {
        strError = strprintf("Zeronode protocol too old %d - req %d", pzn->protocolVersion, ActiveProtocol());
        LogPrint("zeronode","CZeronodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = znodeman.GetZeronodeRank(vinZeronode, nBlockHeight - 100, ActiveProtocol());

    if (n > ZNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have zeronodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > ZNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Zeronode not in the top %d (%d)", ZNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint("zeronode","CZeronodePaymentWinner::IsValid - %s\n", strError);
            if (zeronodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CZeronodePayments::ProcessBlock(int nBlockHeight)
{
    if (!fZeroNode) return false;

    //reference node - hybrid mode

    int n = znodeman.GetZeronodeRank(activeZeronode.vin, nBlockHeight - 100, ActiveProtocol());

    if (n == -1) {
        LogPrint("zeronode", "CZeronodePayments::ProcessBlock - Unknown Zeronode\n");
        return false;
    }

    if (n > ZNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("zeronode", "CZeronodePayments::ProcessBlock - Zeronode not in the top %d (%d)\n", ZNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight) return false;

    CZeronodePaymentWinner newWinner(activeZeronode.vin);

    if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
        //is budget payment block -- handled by the budgeting software
    } else {
        LogPrint("zeronode","CZeronodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeZeronode.vin.prevout.hash.ToString());

        // pay to the oldest ZN that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CZeronode* pzn = znodeman.GetNextZeronodeInQueueForPayment(nBlockHeight, true, nCount);

        if (pzn != NULL) {
            LogPrint("zeronode","CZeronodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(pzn->pubKeyCollateralAddress.GetID());
            newWinner.AddPayee(payee);

            CTxDestination address1;
            ExtractDestination(payee, address1);

            LogPrint("zeronode","CZeronodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", EncodeDestination(address1), newWinner.nBlockHeight);
        } else {
            LogPrint("zeronode","CZeronodePayments::ProcessBlock() Failed to find zeronode to pay\n");
        }
    }

    std::string errorMessage;
    CPubKey pubKeyZeronode;
    CKey keyZeronode;

    if (!obfuScationSigner.SetKey(strZeroNodePrivKey, errorMessage, keyZeronode, pubKeyZeronode)) {
        LogPrint("zeronode","CZeronodePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrint("zeronode","CZeronodePayments::ProcessBlock() - Signing Winner\n");
    if (newWinner.Sign(keyZeronode, pubKeyZeronode)) {
        LogPrint("zeronode","CZeronodePayments::ProcessBlock() - AddWinningZeronode\n");

        if (AddWinningZeronode(newWinner)) {
            newWinner.Relay();
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CZeronodePaymentWinner::Relay()
{
    CInv inv(MSG_ZERONODE_WINNER, GetHash());
    RelayInv(inv);
}

bool CZeronodePaymentWinner::SignatureValid()
{
    CZeronode* pzn = znodeman.Find(vinZeronode);

    if (pzn != NULL) {
        std::string strMessage = vinZeronode.prevout.ToStringShort() +
                                 boost::lexical_cast<std::string>(nBlockHeight) +
                                 EncodeDestination(payee);

        std::string errorMessage = "";
        if (!obfuScationSigner.VerifyMessage(pzn->pubKeyZeronode, vchSig, strMessage, errorMessage)) {
            return error("CZeronodePaymentWinner::SignatureValid() - Got bad Zeronode address signature %s\n", vinZeronode.prevout.hash.ToString());
        }

        return true;
    }

    return false;
}

void CZeronodePayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapZeronodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    int nCount = (znodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CZeronodePaymentWinner>::iterator it = mapZeronodePayeeVotes.begin();
    while (it != mapZeronodePayeeVotes.end()) {
        CZeronodePaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_ZERONODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    node->PushMessage("ssc", ZERONODE_SYNC_ZNW, nInvCount);
}

std::string CZeronodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapZeronodePayeeVotes.size() << ", Blocks: " << (int)mapZeronodeBlocks.size();

    return info.str();
}


int CZeronodePayments::GetOldestBlock()
{
    LOCK(cs_mapZeronodeBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CZeronodeBlockPayees>::iterator it = mapZeronodeBlocks.begin();
    while (it != mapZeronodeBlocks.end()) {
        if ((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}


int CZeronodePayments::GetNewestBlock()
{
    LOCK(cs_mapZeronodeBlocks);

    int nNewestBlock = 0;

    std::map<int, CZeronodeBlockPayees>::iterator it = mapZeronodeBlocks.begin();
    while (it != mapZeronodeBlocks.end()) {
        if ((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
