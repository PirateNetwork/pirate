// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zeronode/activezeronode.h"
#include "addrman.h"
#include "zeronode/zeronode.h"
#include "zeronode/zeronodeconfig.h"
#include "zeronode/zeronodeman.h"
#include "protocol.h"
#include "zeronode/spork.h"

//
// Bootup the Zeronode, look for a 1000 Zero input and register on the network
//
void CActiveZeronode::ManageStatus()
{
    std::string errorMessage;

    if (!fZeroNode) return;

    if (fDebug) LogPrintf("CActiveZeronode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (NetworkIdFromCommandLine() != CBaseChainParams::REGTEST && !zeronodeSync.IsBlockchainSynced()) {
        status = ACTIVE_ZERONODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveZeronode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_ZERONODE_SYNC_IN_PROCESS) status = ACTIVE_ZERONODE_INITIAL;

    if (status == ACTIVE_ZERONODE_INITIAL) {
        CZeronode* pzn;
        pzn = znodeman.Find(pubKeyZeronode);
        if (pzn != NULL) {
            pzn->Check();
            if (pzn->IsEnabled() && pzn->protocolVersion == PROTOCOL_VERSION) EnableHotColdZeroNode(pzn->vin, pzn->addr);
        }
    }

    if (status != ACTIVE_ZERONODE_STARTED) {
        // Set defaults
        status = ACTIVE_ZERONODE_NOT_CAPABLE;
        notCapableReason = "";

        if (pwalletMain->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveZeronode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (pwalletMain->GetBalance() == 0) {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveZeronode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (strZeroNodeAddr.empty()) {
            if (!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the zeronodeaddr configuration option.";
                LogPrintf("CActiveZeronode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strZeroNodeAddr);
        }

        LogPrintf("CActiveZeronode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        if(NetworkIdFromCommandLine() == CBaseChainParams::MAIN) {
            if(service.GetPort() != 23801) {
                notCapableReason = strprintf("Invalid port: %u - only 23801 is supported on mainnet.", service.GetPort());
                LogPrintf("CActiveZeronode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else if(service.GetPort() == 23801) {
            notCapableReason = strprintf("Invalid port: %u - 23801 is only supported on mainnet.", service.GetPort());
            LogPrintf("CActiveZeronode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

		CNode* pnode = ConnectNode((CAddress)service, service.ToString().c_str());
        if (!pnode) {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveZeronode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
        pnode->Release();

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if (GetZeroNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {
            if (GetInputAge(vin) < ZERONODE_MIN_CONFIRMATIONS) {
                status = ACTIVE_ZERONODE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetInputAge(vin));
                LogPrintf("CActiveZeronode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyZeronode;
            CKey keyZeronode;

            if (!obfuScationSigner.SetKey(strZeroNodePrivKey, errorMessage, keyZeronode, pubKeyZeronode)) {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            if (!Register(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyZeronode, pubKeyZeronode, errorMessage)) {
                notCapableReason = "Error on Register: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LogPrintf("CActiveZeronode::ManageStatus() - Is capable master node!\n");
            status = ACTIVE_ZERONODE_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveZeronode::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if (!SendZeronodePing(errorMessage)) {
        LogPrintf("CActiveZeronode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveZeronode::GetStatus()
{
    switch (status) {
    case ACTIVE_ZERONODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_ZERONODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Zeronode";
    case ACTIVE_ZERONODE_INPUT_TOO_NEW:
        return strprintf("Zeronode input must have at least %d confirmations", ZERONODE_MIN_CONFIRMATIONS);
    case ACTIVE_ZERONODE_NOT_CAPABLE:
        return "Not capable zeronode: " + notCapableReason;
    case ACTIVE_ZERONODE_STARTED:
        return "Zeronode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveZeronode::SendZeronodePing(std::string& errorMessage)
{
    if (status != ACTIVE_ZERONODE_STARTED) {
        errorMessage = "Zeronode is not in a running status";
        return false;
    }

    CPubKey pubKeyZeronode;
    CKey keyZeronode;

    if (!obfuScationSigner.SetKey(strZeroNodePrivKey, errorMessage, keyZeronode, pubKeyZeronode)) {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveZeronode::SendZeronodePing() - Relay Zeronode Ping vin = %s\n", vin.ToString());

    CZeronodePing znp(vin);
    if (!znp.Sign(keyZeronode, pubKeyZeronode)) {
        errorMessage = "Couldn't sign Zeronode Ping";
        return false;
    }

    // Update lastPing for our zeronode in Zeronode list
    CZeronode* pzn = znodeman.Find(vin);
    if (pzn != NULL) {
        if (pzn->IsPingedWithin(ZERONODE_PING_SECONDS, znp.sigTime)) {
            errorMessage = "Too early to send Zeronode Ping";
            return false;
        }

        pzn->lastPing = znp;
        znodeman.mapSeenZeronodePing.insert(make_pair(znp.GetHash(), znp));

        //znodeman.mapSeenZeronodeBroadcast.lastPing is probably outdated, so we'll update it
        CZeronodeBroadcast znb(*pzn);
        uint256 hash = znb.GetHash();
        if (znodeman.mapSeenZeronodeBroadcast.count(hash)) znodeman.mapSeenZeronodeBroadcast[hash].lastPing = znp;

        znp.Relay();
        return true;
    } else {
        // Seems like we are trying to send a ping while the Zeronode is not registered in the network
        errorMessage = "Obfuscation Zeronode List doesn't include our Zeronode, shutting down Zeronode pinging service! " + vin.ToString();
        status = ACTIVE_ZERONODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

bool CActiveZeronode::Register(std::string strService, std::string strKeyZeronode, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage)
{
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyZeronode;
    CKey keyZeronode;

    //need correct blocks to send ping
    if (!zeronodeSync.IsBlockchainSynced()) {
        errorMessage = GetStatus();
        LogPrintf("CActiveZeronode::Register() - %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.SetKey(strKeyZeronode, errorMessage, keyZeronode, pubKeyZeronode)) {
        errorMessage = strprintf("Can't find keys for zeronode %s - %s", strService, errorMessage);
        LogPrintf("CActiveZeronode::Register() - %s\n", errorMessage);
        return false;
    }

    if (!GetZeroNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strTxHash, strOutputIndex)) {
        errorMessage = strprintf("Could not allocate vin %s:%s for zeronode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CActiveZeronode::Register() - %s\n", errorMessage);
        return false;
    }

    CService service;
    if (!Lookup(strService.c_str(), service, 0, false))
        return LogPrintf("Invalid address %s for zeronode.", strService);
    if(NetworkIdFromCommandLine() == CBaseChainParams::MAIN) {
        if (service.GetPort() != 23801) {
            errorMessage = strprintf("Invalid port %u for zeronode %s - only 23801 is supported on mainnet.", service.GetPort(), strService);
            LogPrintf("CActiveZeronode::Register() - %s\n", errorMessage);
            return false;
        }
    } else if (service.GetPort() == 23801) {
        errorMessage = strprintf("Invalid port %u for zeronode %s - 23801 is only supported on mainnet.", service.GetPort(), strService);
        LogPrintf("CActiveZeronode::Register() - %s\n", errorMessage);
        return false;
    }

    //addrman.Add(CAddress(service), CNetAddr("127.0.0.1"), 2 * 60 * 60);

    return Register(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyZeronode, pubKeyZeronode, errorMessage);
}

bool CActiveZeronode::Register(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyZeronode, CPubKey pubKeyZeronode, std::string& errorMessage)
{
    CZeronodeBroadcast znb;
    CZeronodePing znp(vin);
    if (!znp.Sign(keyZeronode, pubKeyZeronode)) {
        errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
        LogPrintf("CActiveZeronode::Register() -  %s\n", errorMessage);
        return false;
    }
    znodeman.mapSeenZeronodePing.insert(make_pair(znp.GetHash(), znp));

    LogPrintf("CActiveZeronode::Register() - Adding to Zeronode list\n    service: %s\n    vin: %s\n", service.ToString(), vin.ToString());
    znb = CZeronodeBroadcast(service, vin, pubKeyCollateralAddress, pubKeyZeronode, PROTOCOL_VERSION);
    znb.lastPing = znp;
    if (!znb.Sign(keyCollateralAddress)) {
        errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
        LogPrintf("CActiveZeronode::Register() - %s\n", errorMessage);
        return false;
    }
    znodeman.mapSeenZeronodeBroadcast.insert(make_pair(znb.GetHash(), znb));
    zeronodeSync.AddedZeronodeList(znb.GetHash());

    CZeronode* pzn = znodeman.Find(vin);
    if (pzn == NULL) {
        CZeronode zn(znb);
        znodeman.Add(zn);
    } else {
        pzn->UpdateFromNewBroadcast(znb);
    }

    //send to all peers
    LogPrintf("CActiveZeronode::Register() - RelayElectionEntry vin = %s\n", vin.ToString());
    znb.Relay();
    return true;
}

bool CActiveZeronode::GetZeroNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    return GetZeroNodeVin(vin, pubkey, secretKey, "", "");
}

bool CActiveZeronode::GetZeroNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex)
{
    // Find possible candidates
    TRY_LOCK(pwalletMain->cs_wallet, fWallet);
    if (!fWallet) return false;

    vector<COutput> possibleCoins = SelectCoinsZeronode();
    COutput* selectedOutput;

    // Find the vin
    if (!strTxHash.empty()) {
        // Let's find it
        uint256 txHash = ArithToUint256(arith_uint256(strTxHash));
        int outputIndex;
        try {
            outputIndex = std::stoi(strOutputIndex.c_str());
        } catch (const std::exception& e) {
            LogPrintf("%s: %s on strOutputIndex\n", __func__, e.what());
            return false;
        }

        bool found = false;
        BOOST_FOREACH (COutput& out, possibleCoins) {
            if (out.tx->GetHash() == txHash && out.i == outputIndex) {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if (!found) {
            LogPrintf("CActiveZeronode::GetZeroNodeVin - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if (possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            LogPrintf("CActiveZeronode::GetZeroNodeVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}

// Extract Zeronode vin information from output
bool CActiveZeronode::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    CScript pubScript;

    vin = CTxIn(out.tx->GetHash(), out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address;
    ExtractDestination(pubScript, address);

    CKeyID *keyID = boost::get<CKeyID>(&address);
    if (!keyID) {
        LogPrintf("CActiveZeronode::GetZeroNodeVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(*keyID, secretKey)) {
        LogPrintf("CActiveZeronode::GetZeroNodeVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running Zeronode
vector<COutput> CActiveZeronode::SelectCoinsZeronode()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;
    vector<COutPoint> confLockedCoins;

    // Temporary unlock ZN coins from zeronode.conf
    if (GetBoolArg("-znconflock", true)) {
        uint256 znTxHash;
        BOOST_FOREACH (CZeronodeConfig::CZeronodeEntry zne, zeronodeConfig.getEntries()) {
            znTxHash.SetHex(zne.getTxHash());

            int nIndex;
            if(!zne.castOutputIndex(nIndex))
                continue;

            COutPoint outpoint = COutPoint(znTxHash, nIndex);
            confLockedCoins.push_back(outpoint);
            pwalletMain->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Lock ZN coins from zeronode.conf back if they where temporary unlocked
    if (!confLockedCoins.empty()) {
        BOOST_FOREACH (COutPoint outpoint, confLockedCoins)
            pwalletMain->LockCoin(outpoint);
    }

    // Filter
    BOOST_FOREACH (const COutput& out, vCoins) {
        if (out.tx->vout[out.i].nValue == 10000 * COIN) { //exactly
            filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// when starting a Zeronode, this can enable to run as a hot wallet with no funds
bool CActiveZeronode::EnableHotColdZeroNode(CTxIn& newVin, CService& newService)
{
    if (!fZeroNode) return false;

    status = ACTIVE_ZERONODE_STARTED;

    //The values below are needed for signing znping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveZeronode::EnableHotColdZeroNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
