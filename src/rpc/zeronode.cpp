// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zeronode/activezeronode.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "zeronode/budget.h"
#include "zeronode/payments.h"
#include "zeronode/zeronodeconfig.h"
#include "zeronode/zeronodeman.h"
#include "zeronode/zeronode-sync.h"
#include "rpc/server.h"
#include "utilmoneystr.h"

#include <boost/tokenizer.hpp>

#include <fstream>

UniValue listzeronodes(const UniValue& params, bool fHelp)
{
    std::string strFilter = "";

    if (params.size() == 1) strFilter = params[0].get_str();

    if (fHelp || (params.size() > 1))
        throw runtime_error(
            "listzeronodes ( \"filter\" )\n"
            "\nGet a ranked list of zeronodes\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by txhash, status, or addr.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"rank\": n,           (numeric) Zeronode Rank (or 0 if not enabled)\n"
            "    \"txhash\": \"hash\",    (string) Collateral transaction hash\n"
            "    \"outidx\": n,         (numeric) Collateral transaction output index\n"
            "    \"status\": s,         (string) Status (ENABLED/EXPIRED/REMOVE/etc)\n"
            "    \"addr\": \"addr\",      (string) Zeronode Zero address\n"
            "    \"version\": v,        (numeric) Zeronode protocol version\n"
            "    \"lastseen\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last seen\n"
            "    \"activetime\": ttt,   (numeric) The time in seconds since epoch (Jan 1 1970 GMT) zeronode has been active\n"
            "    \"lastpaid\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) zeronode was last paid\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("zeronodelist", "") + HelpExampleRpc("zeronodelist", ""));

    UniValue ret(UniValue::VARR);
    int nHeight;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if(!pindex) return 0;
        nHeight = pindex->nHeight;
    }
    std::vector<pair<int, CZeronode> > vZeronodeRanks = znodeman.GetZeronodeRanks(nHeight);
    BOOST_FOREACH (PAIRTYPE(int, CZeronode) & s, vZeronodeRanks) {
        UniValue obj(UniValue::VOBJ);
        std::string strVin = s.second.vin.prevout.ToStringShort();
        std::string strTxHash = s.second.vin.prevout.hash.ToString();
        uint32_t oIdx = s.second.vin.prevout.n;

        CZeronode* zn = znodeman.Find(s.second.vin);

        if (zn != NULL) {
            if (strFilter != "" && strTxHash.find(strFilter) == string::npos &&
                zn->Status().find(strFilter) == string::npos &&
                EncodeDestination(zn->pubKeyCollateralAddress.GetID()).find(strFilter) == string::npos) continue;

            std::string strStatus = zn->Status();
            std::string strHost;
            int port;
            SplitHostPort(zn->addr.ToString(), port, strHost);
            CNetAddr node = CNetAddr(strHost, false);
            std::string strNetwork = GetNetworkName(node.GetNetwork());

            obj.push_back(Pair("rank", (strStatus == "ENABLED" ? s.first : 0)));
            obj.push_back(Pair("network", strNetwork));
            obj.push_back(Pair("ip", strHost));
            obj.push_back(Pair("txhash", strTxHash));
            obj.push_back(Pair("outidx", (uint64_t)oIdx));
            obj.push_back(Pair("status", strStatus));
            obj.push_back(Pair("addr", EncodeDestination(zn->pubKeyCollateralAddress.GetID())));
            obj.push_back(Pair("version", zn->protocolVersion));
            obj.push_back(Pair("lastseen", (int64_t)zn->lastPing.sigTime));
            obj.push_back(Pair("activetime", (int64_t)(zn->lastPing.sigTime - zn->sigTime)));
            obj.push_back(Pair("lastpaid", (int64_t)zn->GetLastPaid()));

            ret.push_back(obj);
        }
    }

    return ret;
}

UniValue startalias(const UniValue& params, bool fHelp)
{
    LogPrintf("params.size(): %d", params.size());
    if (fHelp || (params.size() != 1))
        throw runtime_error(
            "startalias \"aliasname\"\n"
            "\nAttempts to start an alias\n"

            "\nArguments:\n"
            "1. \"aliasname\"     (string, required) alias name\n"

            "\nExamples:\n" +
            HelpExampleCli("startalias", "\"zn1\"") + HelpExampleRpc("startalias", ""));
    if (!zeronodeSync.IsSynced())
    {
        UniValue obj(UniValue::VOBJ);
        std::string error = "Zeronode is not synced, please wait. Current status: " + zeronodeSync.GetSyncStatus();
        obj.push_back(Pair("result", error));
        return obj;
    }

    std::string strAlias = params[0].get_str();
    bool fSuccess = false;
    BOOST_FOREACH (CZeronodeConfig::CZeronodeEntry zne, zeronodeConfig.getEntries()) {
        if (zne.getAlias() == strAlias) {
            std::string strError;
            CZeronodeBroadcast znb;

            fSuccess = CZeronodeBroadcast::Create(zne.getIp(), zne.getPrivKey(), zne.getTxHash(), zne.getOutputIndex(), strError, znb);

            if (fSuccess) {
                znodeman.UpdateZeronodeList(znb);
                znb.Relay();
            }
            break;
        }
    }
    if (fSuccess) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("result", "Successfully started alias"));
        return obj;
    } else {
        throw runtime_error("Failed to start alias\n");
    }
}

UniValue zeronodeconnect(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 1))
        throw runtime_error(
            "zeronodeconnect \"address\"\n"
            "\nAttempts to connect to specified zeronode address\n"

            "\nArguments:\n"
            "1. \"address\"     (string, required) IP or net address to connect to\n"

            "\nExamples:\n" +
            HelpExampleCli("zeronodeconnect", "\"192.168.0.6:23821\"") + HelpExampleRpc("zeronodeconnect", "\"192.168.0.6:23821\""));

    std::string strAddress = params[0].get_str();

    CService addr = CService(strAddress);

    CNode* pnode = ConnectNode((CAddress)addr, NULL, false);
    if (pnode) {
        pnode->Release();
        return NullUniValue;
    } else {
        throw runtime_error("error connecting\n");
    }
}

UniValue getzeronodecount (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() > 0))
        throw runtime_error(
            "getzeronodecount\n"
            "\nGet zeronode count values\n"

            "\nResult:\n"
            "{\n"
            "  \"total\": n,        (numeric) Total zeronodes\n"
            "  \"stable\": n,       (numeric) Stable count\n"
            "  \"obfcompat\": n,    (numeric) Obfuscation Compatible\n"
            "  \"enabled\": n,      (numeric) Enabled zeronodes\n"
            "  \"inqueue\": n       (numeric) Zeronodes in queue\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getzeronodecount", "") + HelpExampleRpc("getzeronodecount", ""));

    UniValue obj(UniValue::VOBJ);
    int nCount = 0;
    int ipv4 = 0, ipv6 = 0, onion = 0;

    if (chainActive.Tip())
        znodeman.GetNextZeronodeInQueueForPayment(chainActive.Tip()->nHeight, true, nCount);

    znodeman.CountNetworks(ActiveProtocol(), ipv4, ipv6, onion);

    obj.push_back(Pair("total", znodeman.size()));
    obj.push_back(Pair("stable", znodeman.stable_size()));
    obj.push_back(Pair("obfcompat", znodeman.CountEnabled(ActiveProtocol())));
    obj.push_back(Pair("enabled", znodeman.CountEnabled()));
    obj.push_back(Pair("inqueue", nCount));
    obj.push_back(Pair("ipv4", ipv4));
    obj.push_back(Pair("ipv6", ipv6));
    obj.push_back(Pair("onion", onion));

    return obj;
}

UniValue zeronodecurrent (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "zeronodecurrent\n"
            "\nGet current zeronode winner\n"

            "\nResult:\n"
            "{\n"
            "  \"protocol\": xxxx,        (numeric) Protocol version\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"pubkey\": \"xxxx\",      (string) MN Public key\n"
            "  \"lastseen\": xxx,       (numeric) Time since epoch of last seen\n"
            "  \"activeseconds\": xxx,  (numeric) Seconds MN has been active\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("zeronodecurrent", "") + HelpExampleRpc("zeronodecurrent", ""));

    CZeronode* winner = znodeman.GetCurrentZeroNode(1);
    if (winner) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("protocol", (int64_t)winner->protocolVersion));
        obj.push_back(Pair("txhash", winner->vin.prevout.hash.ToString()));
        obj.push_back(Pair("pubkey", EncodeDestination(winner->pubKeyCollateralAddress.GetID())));
        obj.push_back(Pair("lastseen", (winner->lastPing == CZeronodePing()) ? winner->sigTime : (int64_t)winner->lastPing.sigTime));
        obj.push_back(Pair("activeseconds", (winner->lastPing == CZeronodePing()) ? 0 : (int64_t)(winner->lastPing.sigTime - winner->sigTime)));
        return obj;
    }

    throw runtime_error("unknown");
}

UniValue zeronodedebug (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "zeronodedebug\n"
            "\nPrint zeronode status\n"

            "\nResult:\n"
            "\"status\"     (string) Zeronode status message\n"
            "\nExamples:\n" +
            HelpExampleCli("zeronodedebug", "") + HelpExampleRpc("zeronodedebug", ""));

    if (activeZeronode.status != ACTIVE_ZERONODE_INITIAL || !zeronodeSync.IsSynced())
        return activeZeronode.GetStatus();

    CTxIn vin = CTxIn();
    CPubKey pubkey;
    CKey key;
    if (!activeZeronode.GetZeroNodeVin(vin, pubkey, key))
        throw runtime_error("Missing zeronode input, please look at the documentation for instructions on zeronode creation\n");
    else
        return activeZeronode.GetStatus();
}

UniValue startzeronode (const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();

        // Backwards compatibility with legacy 'zeronode' super-command forwarder
        if (strCommand == "start") strCommand = "local";
        if (strCommand == "start-alias") strCommand = "alias";
        if (strCommand == "start-all") strCommand = "all";
        if (strCommand == "start-many") strCommand = "many";
        if (strCommand == "start-missing") strCommand = "missing";
        if (strCommand == "start-disabled") strCommand = "disabled";
    }

    if (fHelp || params.size() < 2 || params.size() > 3 ||
        (params.size() == 2 && (strCommand != "local" && strCommand != "all" && strCommand != "many" && strCommand != "missing" && strCommand != "disabled")) ||
        (params.size() == 3 && strCommand != "alias"))
        throw runtime_error(
            "startzeronode \"local|all|many|missing|disabled|alias\" lockwallet ( \"alias\" )\n"
            "\nAttempts to start one or more zeronode(s)\n"

            "\nArguments:\n"
            "1. set         (string, required) Specify which set of zeronode(s) to start.\n"
            "2. lockwallet  (boolean, required) Lock wallet after completion.\n"
            "3. alias       (string) Zeronode alias. Required if using 'alias' as the set.\n"

            "\nResult: (for 'local' set):\n"
            "\"status\"     (string) Zeronode status message\n"

            "\nResult: (for other sets):\n"
            "{\n"
            "  \"overall\": \"xxxx\",     (string) Overall status message\n"
            "  \"detail\": [\n"
            "    {\n"
            "      \"node\": \"xxxx\",    (string) Node name or alias\n"
            "      \"result\": \"xxxx\",  (string) 'success' or 'failed'\n"
            "      \"error\": \"xxxx\"    (string) Error message, if failed\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("startzeronode", "\"alias\" \"0\" \"my_zn\"") + HelpExampleRpc("startzeronode", "\"alias\" \"0\" \"my_zn\""));

    if (!zeronodeSync.IsSynced())
    {
        UniValue resultsObj(UniValue::VARR);
        int successful = 0;
        int failed = 0;
        BOOST_FOREACH (CZeronodeConfig::CZeronodeEntry zne, zeronodeConfig.getEntries()) {
            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", zne.getAlias()));
            statusObj.push_back(Pair("result", "failed"));

            failed++;
            {
                std::string error = "Zeronode is not synced, please wait. Current status: " + zeronodeSync.GetSyncStatus();
                statusObj.push_back(Pair("error", error));
            }
            resultsObj.push_back(statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d zeronodes, failed to start %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    bool fLock = (params[1].get_str() == "true" ? true : false);

    if (strCommand == "local") {
        if (!fZeroNode) throw runtime_error("you must set zeronode=1 in the configuration\n");

        if (pwalletMain->IsLocked())
            throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");

        if (activeZeronode.status != ACTIVE_ZERONODE_STARTED) {
            activeZeronode.status = ACTIVE_ZERONODE_INITIAL; // TODO: consider better way
            activeZeronode.ManageStatus();
            if (fLock)
                pwalletMain->Lock();
        }

        return activeZeronode.GetStatus();
    }

    if (strCommand == "all" || strCommand == "many" || strCommand == "missing" || strCommand == "disabled") {
        if (pwalletMain->IsLocked())
            throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");

        if ((strCommand == "missing" || strCommand == "disabled") &&
            (zeronodeSync.RequestedZeronodeAssets <= ZERONODE_SYNC_LIST ||
                zeronodeSync.RequestedZeronodeAssets == ZERONODE_SYNC_FAILED)) {
            throw runtime_error("You can't use this command until zeronode list is synced\n");
        }

        std::vector<CZeronodeConfig::CZeronodeEntry> znEntries;
        znEntries = zeronodeConfig.getEntries();

        int successful = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VARR);

        BOOST_FOREACH (CZeronodeConfig::CZeronodeEntry zne, zeronodeConfig.getEntries()) {
            std::string errorMessage;
            int nIndex;
            if(!zne.castOutputIndex(nIndex))
                continue;
            CTxIn vin = CTxIn(uint256S(zne.getTxHash()), uint32_t(nIndex));
            CZeronode* pzn = znodeman.Find(vin);

            if (pzn != NULL) {
                if (strCommand == "missing") continue;
                if (strCommand == "disabled" && pzn->IsEnabled()) continue;
            }

            bool result = activeZeronode.Register(zne.getIp(), zne.getPrivKey(), zne.getTxHash(), zne.getOutputIndex(), errorMessage);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", zne.getAlias()));
            statusObj.push_back(Pair("result", result ? "success" : "failed"));

            if (result) {
                successful++;
                statusObj.push_back(Pair("error", ""));
            } else {
                failed++;
                statusObj.push_back(Pair("error", errorMessage));
            }

            resultsObj.push_back(statusObj);
        }
        if (fLock)
            pwalletMain->Lock();

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d zeronodes, failed to start %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if (strCommand == "alias") {
        std::string alias = params[2].get_str();

        if (pwalletMain->IsLocked())
            throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");

        bool found = false;
        int successful = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VARR);
        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair("alias", alias));

        BOOST_FOREACH (CZeronodeConfig::CZeronodeEntry zne, zeronodeConfig.getEntries()) {
            if (zne.getAlias() == alias) {
                found = true;
                std::string errorMessage;

                bool result = activeZeronode.Register(zne.getIp(), zne.getPrivKey(), zne.getTxHash(), zne.getOutputIndex(), errorMessage);

                statusObj.push_back(Pair("result", result ? "successful" : "failed"));

                if (result) {
                    successful++;
                    statusObj.push_back(Pair("error", ""));
                } else {
                    failed++;
                    statusObj.push_back(Pair("error", errorMessage));
                }
                break;
            }
        }

        if (!found) {
            failed++;
            statusObj.push_back(Pair("result", "failed"));
            statusObj.push_back(Pair("error", "could not find alias in config. Verify with list-conf."));
        }

        resultsObj.push_back(statusObj);

        if (fLock)
            pwalletMain->Lock();

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d zeronodes, failed to start %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }
    return NullUniValue;
}

UniValue createzeronodekey (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "createzeronodekey\n"
            "\nCreate a new zeronode private key\n"

            "\nResult:\n"
            "\"key\"    (string) Zeronode private key\n"
            "\nExamples:\n" +
            HelpExampleCli("createzeronodekey", "") + HelpExampleRpc("createzeronodekey", ""));

    CKey secret;
    secret.MakeNewKey(false);

    return EncodeSecret(secret);
}

UniValue getzeronodeoutputs (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "getzeronodeoutputs\n"
            "\nPrint all zeronode transaction outputs\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txhash\": \"xxxx\",    (string) output transaction hash\n"
            "    \"outputidx\": n       (numeric) output index number\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getzeronodeoutputs", "") + HelpExampleRpc("getzeronodeoutputs", ""));

    // Find possible candidates
    vector<COutput> possibleCoins = activeZeronode.SelectCoinsZeronode();

    UniValue ret(UniValue::VARR);
    BOOST_FOREACH (COutput& out, possibleCoins) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("txhash", out.tx->GetHash().ToString()));
        obj.push_back(Pair("outputidx", out.i));
        ret.push_back(obj);
    }

    return ret;
}

UniValue listzeronodeconf (const UniValue& params, bool fHelp)
{
    std::string strFilter = "";

    if (params.size() == 1) strFilter = params[0].get_str();

    if (fHelp || (params.size() > 1))
        throw runtime_error(
            "listzeronodeconf ( \"filter\" )\n"
            "\nPrint zeronode.conf in JSON format\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match on alias, address, txHash, or status.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"alias\": \"xxxx\",        (string) zeronode alias\n"
            "    \"address\": \"xxxx\",      (string) zeronode IP address\n"
            "    \"privateKey\": \"xxxx\",   (string) zeronode private key\n"
            "    \"txHash\": \"xxxx\",       (string) transaction hash\n"
            "    \"outputIndex\": n,       (numeric) transaction output index\n"
            "    \"status\": \"xxxx\"        (string) zeronode status\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listzeronodeconf", "") + HelpExampleRpc("listzeronodeconf", ""));

    std::vector<CZeronodeConfig::CZeronodeEntry> znEntries;
    znEntries = zeronodeConfig.getEntries();

    UniValue ret(UniValue::VARR);

    BOOST_FOREACH (CZeronodeConfig::CZeronodeEntry zne, zeronodeConfig.getEntries()) {
        int nIndex;
        if(!zne.castOutputIndex(nIndex))
            continue;
        CTxIn vin = CTxIn(uint256S(zne.getTxHash()), uint32_t(nIndex));
        CZeronode* pzn = znodeman.Find(vin);

        std::string strStatus = pzn ? pzn->Status() : "MISSING";

        if (strFilter != "" && zne.getAlias().find(strFilter) == string::npos &&
            zne.getIp().find(strFilter) == string::npos &&
            zne.getTxHash().find(strFilter) == string::npos &&
            strStatus.find(strFilter) == string::npos) continue;

        UniValue znObj(UniValue::VOBJ);
        znObj.push_back(Pair("alias", zne.getAlias()));
        znObj.push_back(Pair("address", zne.getIp()));
        znObj.push_back(Pair("privateKey", zne.getPrivKey()));
        znObj.push_back(Pair("txHash", zne.getTxHash()));
        znObj.push_back(Pair("outputIndex", zne.getOutputIndex()));
        znObj.push_back(Pair("status", strStatus));
        ret.push_back(znObj);
    }

    return ret;
}

UniValue getzeronodestatus (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "getzeronodestatus\n"
            "\nPrint zeronode status\n"

            "\nResult:\n"
            "{\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"outputidx\": n,        (numeric) Collateral transaction output index number\n"
            "  \"netaddr\": \"xxxx\",     (string) Zeronode network address\n"
            "  \"addr\": \"xxxx\",        (string) Zero address for zeronode payments\n"
            "  \"status\": \"xxxx\",      (string) Zeronode status\n"
            "  \"message\": \"xxxx\"      (string) Zeronode status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getzeronodestatus", "") + HelpExampleRpc("getzeronodestatus", ""));

    if (!fZeroNode) throw runtime_error("This is not a zeronode");

    CZeronode* pzn = znodeman.Find(activeZeronode.vin);

    if (pzn) {
        UniValue znObj(UniValue::VOBJ);
        znObj.push_back(Pair("txhash", activeZeronode.vin.prevout.hash.ToString()));
        znObj.push_back(Pair("outputidx", (uint64_t)activeZeronode.vin.prevout.n));
        znObj.push_back(Pair("netaddr", activeZeronode.service.ToString()));
        znObj.push_back(Pair("addr", EncodeDestination(pzn->pubKeyCollateralAddress.GetID())));
        znObj.push_back(Pair("status", activeZeronode.status));
        znObj.push_back(Pair("message", activeZeronode.GetStatus()));
        return znObj;
    }
    throw runtime_error("Zeronode not found in the list of available zeronodes. Current status: "
                        + activeZeronode.GetStatus());
}

UniValue getzeronodewinners (const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
            "getzeronodewinners ( blocks \"filter\" )\n"
            "\nPrint the zeronode winners for the last n blocks\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Number of previous blocks to show (default: 10)\n"
            "2. filter      (string, optional) Search filter matching MN address\n"

            "\nResult (single winner):\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": {\n"
            "      \"address\": \"xxxx\",    (string) Zero MN Address\n"
            "      \"nVotes\": n,          (numeric) Number of votes for winner\n"
            "    }\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nResult (multiple winners):\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": [\n"
            "      {\n"
            "        \"address\": \"xxxx\",  (string) Zero MN Address\n"
            "        \"nVotes\": n,        (numeric) Number of votes for winner\n"
            "      }\n"
            "      ,...\n"
            "    ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getzeronodewinners", "") + HelpExampleRpc("getzeronodewinners", ""));

    int nHeight;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if(!pindex) return 0;
        nHeight = pindex->nHeight;
    }

    int nLast = 10;
    std::string strFilter = "";

    if (params.size() >= 1)
        nLast = atoi(params[0].get_str());

    if (params.size() == 2)
        strFilter = params[1].get_str();

    UniValue ret(UniValue::VARR);

    for (int i = nHeight - nLast; i < nHeight + 20; i++) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("nHeight", i));

        std::string strPayment = GetRequiredPaymentsString(i);
        if (strFilter != "" && strPayment.find(strFilter) == std::string::npos) continue;

        if (strPayment.find(',') != std::string::npos) {
            UniValue winner(UniValue::VARR);
            boost::char_separator<char> sep(",");
            boost::tokenizer< boost::char_separator<char> > tokens(strPayment, sep);
            BOOST_FOREACH (const string& t, tokens) {
                UniValue addr(UniValue::VOBJ);
                std::size_t pos = t.find(":");
                std::string strAddress = t.substr(0,pos);
                uint64_t nVotes = atoi(t.substr(pos+1));
                addr.push_back(Pair("address", strAddress));
                addr.push_back(Pair("nVotes", nVotes));
                winner.push_back(addr);
            }
            obj.push_back(Pair("winner", winner));
        } else if (strPayment.find("Unknown") == std::string::npos) {
            UniValue winner(UniValue::VOBJ);
            std::size_t pos = strPayment.find(":");
            std::string strAddress = strPayment.substr(0,pos);
            uint64_t nVotes = atoi(strPayment.substr(pos+1));
            winner.push_back(Pair("address", strAddress));
            winner.push_back(Pair("nVotes", nVotes));
            obj.push_back(Pair("winner", winner));
        } else {
            UniValue winner(UniValue::VOBJ);
            winner.push_back(Pair("address", strPayment));
            winner.push_back(Pair("nVotes", 0));
            obj.push_back(Pair("winner", winner));
        }

            ret.push_back(obj);
    }

    return ret;
}

UniValue getzeronodescores (const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getzeronodescores ( blocks )\n"
            "\nPrint list of winning zeronode by score\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Show the last n blocks (default 10)\n"

            "\nResult:\n"
            "{\n"
            "  xxxx: \"xxxx\"   (numeric : string) Block height : Zeronode hash\n"
            "  ,...\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getzeronodescores", "") + HelpExampleRpc("getzeronodescores", ""));

    int nLast = 10;

    if (params.size() == 1) {
        try {
            nLast = std::stoi(params[0].get_str());
        } catch (const boost::bad_lexical_cast &) {
            throw runtime_error("Exception on param 2");
        }
    }
    UniValue obj(UniValue::VOBJ);

    int nHeight = chainActive.Tip()->nHeight - nLast;

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nHeight - 100)) {
        return NullUniValue;
    }

    std::vector<CZeronode> vZeronodes = znodeman.GetFullZeronodeVector();
    for (int height = nHeight; height < chainActive.Tip()->nHeight + 20; height++) {
        arith_uint256 nHigh = 0;
        CZeronode* pBestZeronode = NULL;
        BOOST_FOREACH (CZeronode& zn, vZeronodes) {
            arith_uint256 n = zn.CalculateScore(blockHash);
            if (n > nHigh) {
                nHigh = n;
                pBestZeronode = &zn;
            }
        }
        if (pBestZeronode)
            obj.push_back(Pair(strprintf("%d", height), pBestZeronode->vin.prevout.hash.ToString().c_str()));
    }

    return obj;
}

UniValue znsync(const UniValue& params, bool fHelp)
{
    std::string strMode;
    if (params.size() == 1)
        strMode = params[0].get_str();

    if (fHelp || params.size() != 1 || (strMode != "status" && strMode != "reset")) {
        throw runtime_error(
            "znsync \"status|reset\"\n"
            "\nReturns the sync status or resets sync.\n"

            "\nArguments:\n"
            "1. \"mode\"    (string, required) either 'status' or 'reset'\n"

            "\nResult ('status' mode):\n"
            "{\n"
            "  \"IsBlockchainSynced\": true|false,    (boolean) 'true' if blockchain is synced\n"
            "  \"lastZeronodeList\": xxxx,        (numeric) Timestamp of last MN list message\n"
            "  \"lastZeronodeWinner\": xxxx,      (numeric) Timestamp of last MN winner message\n"
            "  \"lastBudgetItem\": xxxx,            (numeric) Timestamp of last MN budget message\n"
            "  \"lastFailure\": xxxx,           (numeric) Timestamp of last failed sync\n"
            "  \"nCountFailures\": n,           (numeric) Number of failed syncs (total)\n"
            "  \"sumZeronodeList\": n,        (numeric) Number of MN list messages (total)\n"
            "  \"sumZeronodeWinner\": n,      (numeric) Number of MN winner messages (total)\n"
            "  \"sumBudgetItemProp\": n,        (numeric) Number of MN budget messages (total)\n"
            "  \"sumBudgetItemFin\": n,         (numeric) Number of MN budget finalization messages (total)\n"
            "  \"countZeronodeList\": n,      (numeric) Number of MN list messages (local)\n"
            "  \"countZeronodeWinner\": n,    (numeric) Number of MN winner messages (local)\n"
            "  \"countBudgetItemProp\": n,      (numeric) Number of MN budget messages (local)\n"
            "  \"countBudgetItemFin\": n,       (numeric) Number of MN budget finalization messages (local)\n"
            "  \"RequestedZeronodeAssets\": n, (numeric) Status code of last sync phase\n"
            "  \"RequestedZeronodeAttempt\": n, (numeric) Status code of last sync attempt\n"
            "}\n"

            "\nResult ('reset' mode):\n"
            "\"status\"     (string) 'success'\n"
            "\nExamples:\n" +
            HelpExampleCli("znsync", "\"status\"") + HelpExampleRpc("znsync", "\"status\""));
    }

    if (strMode == "status") {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("IsBlockchainSynced", zeronodeSync.IsBlockchainSynced()));
        obj.push_back(Pair("lastZeronodeList", zeronodeSync.lastZeronodeList));
        obj.push_back(Pair("lastZeronodeWinner", zeronodeSync.lastZeronodeWinner));
        obj.push_back(Pair("lastBudgetItem", zeronodeSync.lastBudgetItem));
        obj.push_back(Pair("lastFailure", zeronodeSync.lastFailure));
        obj.push_back(Pair("nCountFailures", zeronodeSync.nCountFailures));
        obj.push_back(Pair("sumZeronodeList", zeronodeSync.sumZeronodeList));
        obj.push_back(Pair("sumZeronodeWinner", zeronodeSync.sumZeronodeWinner));
        obj.push_back(Pair("sumBudgetItemProp", zeronodeSync.sumBudgetItemProp));
        obj.push_back(Pair("sumBudgetItemFin", zeronodeSync.sumBudgetItemFin));
        obj.push_back(Pair("countZeronodeList", zeronodeSync.countZeronodeList));
        obj.push_back(Pair("countZeronodeWinner", zeronodeSync.countZeronodeWinner));
        obj.push_back(Pair("countBudgetItemProp", zeronodeSync.countBudgetItemProp));
        obj.push_back(Pair("countBudgetItemFin", zeronodeSync.countBudgetItemFin));
        obj.push_back(Pair("RequestedZeronodeAssets", zeronodeSync.RequestedZeronodeAssets));
        obj.push_back(Pair("RequestedZeronodeAttempt", zeronodeSync.RequestedZeronodeAttempt));

        return obj;
    }

    if (strMode == "reset") {
        zeronodeSync.Reset();
        return "success";
    }
    return "failure";
}

// This command is retained for backwards compatibility, but is depreciated.
// Future removal of this command is planned to keep things clean.
UniValue zeronode(const UniValue& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp ||
        (strCommand != "start" && strCommand != "start-alias" && strCommand != "start-many" && strCommand != "start-all" && strCommand != "start-missing" &&
            strCommand != "start-disabled" && strCommand != "list" && strCommand != "list-conf" && strCommand != "count" && strCommand != "enforce" &&
            strCommand != "debug" && strCommand != "current" && strCommand != "winners" && strCommand != "genkey" && strCommand != "connect" &&
            strCommand != "outputs" && strCommand != "status" && strCommand != "calcscore"))
        throw runtime_error(
            "zeronode \"command\"...\n"
            "\nSet of commands to execute zeronode related actions\n"
            "This command is depreciated, please see individual command documentation for future reference\n\n"

            "\nArguments:\n"
            "1. \"command\"        (string or set of strings, required) The command to execute\n"

            "\nAvailable commands:\n"
            "  count        - Print count information of all known zeronodes\n"
            "  connect      - Attempts to connect to specified zeronode address\n"
            "  current      - Print info on current zeronode winner\n"
            "  debug        - Print zeronode status\n"
            "  genkey       - Generate new zeronodeprivkey\n"
            "  outputs      - Print zeronode compatible outputs\n"
            "  start        - Start zeronode configured in zero.conf\n"
            "  start-alias  - Start single zeronode by assigned alias configured in zeronode.conf\n"
            "  start-<mode> - Start zeronodes configured in zeronode.conf (<mode>: 'all', 'missing', 'disabled')\n"
            "  status       - Print zeronode status information\n"
            "  list         - Print list of all known zeronodes (see zeronodelist for more info)\n"
            "  list-conf    - Print zeronode.conf in JSON format\n"
            "  winners      - Print list of zeronode winners\n"
            "  calcscore    - Print list of winning zeronode by score\n");

    if (strCommand == "list") {
		UniValue newParams(UniValue::VARR);

        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }

        return listzeronodes(newParams, fHelp);
    }

    if (strCommand == "connect") {
        UniValue newParams(UniValue::VARR);
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return zeronodeconnect(newParams, fHelp);
    }

    if (strCommand == "count") {
        UniValue newParams(UniValue::VARR);
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getzeronodecount(newParams, fHelp);
    }

    if (strCommand == "current") {
        UniValue newParams(UniValue::VARR);
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return zeronodecurrent(newParams, fHelp);
    }

    if (strCommand == "debug") {
        UniValue newParams(UniValue::VARR);
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return zeronodedebug(newParams, fHelp);
    }

    if (strCommand == "start" || strCommand == "start-alias" || strCommand == "start-many" || strCommand == "start-all" || strCommand == "start-missing" || strCommand == "start-disabled") {
        return startzeronode(params, fHelp);
    }

    if (strCommand == "genkey") {
        UniValue newParams(UniValue::VARR);
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return createzeronodekey(newParams, fHelp);
    }

    if (strCommand == "list-conf") {
        UniValue newParams(UniValue::VARR);
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return listzeronodeconf(newParams, fHelp);
    }

    if (strCommand == "outputs") {
        UniValue newParams(UniValue::VARR);
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getzeronodeoutputs(newParams, fHelp);
    }

    if (strCommand == "status") {
        UniValue newParams(UniValue::VARR);
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getzeronodestatus(newParams, fHelp);
    }

    if (strCommand == "winners") {
        UniValue newParams(UniValue::VARR);
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getzeronodewinners(newParams, fHelp);
    }

    if (strCommand == "calcscore") {
        UniValue newParams(UniValue::VARR);
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getzeronodescores(newParams, fHelp);
    }

    return NullUniValue;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
  /* MN features */
  {"zeronode",            "getzeronodecount",       &getzeronodecount,    true},
  {"zeronode",            "zeronodeconnect",        &zeronodeconnect,     true},
  {"zeronode",            "zeronodecurrent",        &zeronodecurrent,     true},
  {"zeronode",            "zeronodedebug",          &zeronodedebug,       true},
  {"zeronode",            "createzeronodekey",      &createzeronodekey,   true},
  {"zeronode",            "getzeronodeoutputs",     &getzeronodeoutputs,  true},
  {"zeronode",            "startzeronode",          &startzeronode,       true},
  {"zeronode",            "startalias",             &startalias,          true},
  {"zeronode",            "getzeronodestatus",      &getzeronodestatus,   true},
  {"zeronode",            "listzeronodes",          &listzeronodes,       true},
  {"zeronode",            "listzeronodeconf",       &listzeronodeconf,    true},
  {"zeronode",            "getzeronodewinners",     &getzeronodewinners,  true},
  {"zeronode",            "getzeronodescores",      &getzeronodescores,   true},
  {"zeronode",            "zeronode",               &zeronode,            true},
  {"zeronode",            "znsync",                 &znsync,              true},

};

void RegisterZeronodeRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
