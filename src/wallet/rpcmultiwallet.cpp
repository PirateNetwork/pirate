// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Talks only to CWalletManager, never to pwalletMain -- deliberately not one
// of the files that reaches into the global default wallet directly, so a
// second loaded wallet can be listed/loaded/unloaded without any of this
// file needing to know how to run a request against a non-default wallet.

#include "rpc/server.h"
#include "util.h"
#include "util/strencodings.h"
#include "wallet/walletmanager.h"

#include <set>
#include <stdexcept>

using namespace std;

UniValue listwallets(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "listwallets\n"
            "\nReturns a list of currently loaded wallet names.\n"
            "\nResult:\n"
            "[\n"
            "  \"walletname\"    (string) the wallet name\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("listwallets", "")
            + HelpExampleRpc("listwallets", "")
        );

    UniValue result(UniValue::VARR);
    for (const std::string& name : CWalletManager::Get().ListWalletNames())
        result.push_back(name);
    return result;
}

UniValue loadwallet(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() < 1 || params.size() > 5)
        throw runtime_error(
            "loadwallet \"filename\" ( rescan rescanheight salvage zapwallettxes )\n"
            "\nLoads a wallet file that already exists in the data directory.\n"
            "Once loaded, a core subset of wallet RPCs (getbalance, getnewaddress,\n"
            "sendtoaddress, listtransactions, listunspent, gettransaction, getwalletinfo,\n"
            "listaddressgroupings, z_getbalance, backupwallet, dumpprivkey, importprivkey,\n"
            "walletpassphrase, walletlock, keypoolrefill, z_sendmany, z_shieldcoinbase,\n"
            "z_mergetoaddress, consolidateaddress, z_getoperationstatus,\n"
            "z_getoperationresult, z_listoperationids, and the consolidation/sweep/fee/\n"
            "pruning configuration RPCs) can be run against it via the /wallet/<name>/\n"
            "endpoint. Every other wallet RPC still runs against the default wallet only,\n"
            "regardless of which wallet the request is routed to.\n"
            "The loaded wallet is subscribed to new-block notifications and caught up to\n"
            "the current chain tip synchronously as part of this call (same as the\n"
            "default wallet's own startup) -- this call can take as long as that startup\n"
            "catch-up does for a wallet that's far behind.\n"
            "\nRemaining KNOWN LIMITATION: a loaded secondary wallet still receives no\n"
            "notification for a *transaction* it didn't cause itself beyond what the\n"
            "block-level catch-up above surfaces, and a shielded spend from one is\n"
            "broadcast via the same raw sendrawtransaction path any external transaction\n"
            "would use -- so neither the spend nor its change note is guaranteed to be\n"
            "recorded in that wallet's own file the way a same-block detection would be.\n"
            "Retrying a failed send risks selecting already-spent notes again. See\n"
            "z_getoperationresult to retrieve/free a queued z_sendmany/z_shieldcoinbase/\n"
            "z_mergetoaddress/consolidateaddress operation -- a wallet cannot be unloaded\n"
            "while one of its own is still queued or unpolled.\n"
            "\nArguments:\n"
            "1. \"filename\"      (string, required) the wallet file name, in the data directory\n"
            "2. rescan          (boolean or numeric, optional, default=false) if true, rescan from\n"
            "                   genesis (or from rescanheight if given) instead of this wallet's own\n"
            "                   persisted checkpoint\n"
            "3. rescanheight    (numeric, optional, default=0) block height to rescan from when\n"
            "                   rescan is set; 0 or omitted means genesis\n"
            "4. salvage         (boolean, optional, default=false) attempt salvage/recovery on this\n"
            "                   wallet's file specifically before loading it, if it appears corrupt\n"
            "5. zapwallettxes   (boolean, optional, default=false) wipe this wallet's transaction\n"
            "                   history and rebuild it from a full rescan; implies rescan\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"filename\"    (string) the wallet name\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("loadwallet", "\"second\"")
            + HelpExampleCli("loadwallet", "\"second\" true")
            + HelpExampleRpc("loadwallet", "\"second\"")
        );

    std::string name = params[0].get_str();
    bool fRescan = params.size() > 1 ? params[1].get_bool() : false;
    int nRescanHeight = params.size() > 2 ? params[2].get_int() : 0;
    bool fSalvage = params.size() > 3 ? params[3].get_bool() : false;
    bool fZapWalletTxes = params.size() > 4 ? params[4].get_bool() : false;

    std::string strError;
    if (!CWalletManager::Get().LoadWallet(name, strError, fRescan, nRescanHeight, fSalvage, fZapWalletTxes))
        throw JSONRPCError(RPC_WALLET_ERROR, strError);

    UniValue result(UniValue::VOBJ);
    result.pushKV("name", name);
    return result;
}

UniValue unloadwallet(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "unloadwallet \"walletname\"\n"
            "\nUnloads a currently loaded, non-default wallet.\n"
            "The default wallet can never be unloaded, and a wallet cannot be unloaded\n"
            "while it has a pending request routed to it, or a queued/finished-but-\n"
            "unpolled z_sendmany/z_shieldcoinbase/z_mergetoaddress/consolidateaddress\n"
            "operation of its own -- see z_getoperationresult to retrieve and free one.\n"
            "\nArguments:\n"
            "1. \"walletname\"    (string, required) the wallet name to unload\n"
            "\nExamples:\n"
            + HelpExampleCli("unloadwallet", "\"second\"")
            + HelpExampleRpc("unloadwallet", "\"second\"")
        );

    std::string name = params[0].get_str();
    std::string strError;
    if (!CWalletManager::Get().UnloadWallet(name, strError)) {
        if (strError.find("not found") != std::string::npos)
            throw JSONRPCError(RPC_WALLET_NOT_FOUND, strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    return NullUniValue;
}

static const CRPCCommand commands[] =
{ //  category              name                actor (function)     okSafeMode
  //  --------------------- ------------------- --------------------- ----------
    { "wallet",             "listwallets",      &listwallets,         true  },
    { "wallet",             "loadwallet",       &loadwallet,          true  },
    { "wallet",             "unloadwallet",     &unloadwallet,        true  },
};

void RegisterMultiWalletRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

bool IsMultiWalletAwareRPC(const std::string& name)
{
    // Kept next to the registry RPCs themselves (rather than inline in
    // CRPCTable::execute()) so it's obvious, when a future phase rewires
    // more of rpcwallet.cpp/rpcdump.cpp to resolve
    // CWalletManager::GetWalletForRequest(), exactly which names need adding
    // here too -- this set and the actual rewiring must move together.
    //
    // encryptwallet unconditionally calls StartShutdown() on success --
    // encrypting one secondary wallet would restart the whole node. Needs its
    // own design before it can be exposed here, not a mechanical
    // pwalletMain->pwallet substitution.
    //
    // settxfee (and the whole consolidation/sweep/fee/pruning settings
    // family) was in the original phase-2 scope but got dropped once its code
    // was actually read: it assigned the process-global `payTxFee` (confirmed
    // read directly by CWallet::CreateTransaction() via wallet_fees.cpp,
    // documented there as "user-set global variable") -- rewiring it to take
    // a wallet parameter would still have changed every wallet's fee
    // behavior, not just the selected one. Phase 5 promoted payTxFee (and the
    // rest of that settings family) to genuine per-CWallet fields, so these
    // are now safe to include below.
    static const std::set<std::string> aware = {
        "loadwallet", "unloadwallet", "listwallets",
        "getbalance", "getnewaddress", "sendtoaddress",
        "listtransactions", "listunspent", "gettransaction",
        "getwalletinfo", "listaddressgroupings", "z_getbalance",
        "backupwallet", "dumpprivkey", "importprivkey",
        "walletpassphrase", "walletlock", "keypoolrefill",
        // Phase 3: async operations, constructed on the HTTP thread and
        // executed later against the wallet they were built with (see
        // AsyncRPCOperation's wallet-aware constructor, asyncrpcoperation.h) --
        // not the automatic ChainTip()-triggered sweep/consolidation classes,
        // which stay pwalletMain-only (see walletmanager.cpp's documented
        // limitation on why secondary wallets never receive ChainTip() at all).
        "z_sendmany", "z_shieldcoinbase", "z_mergetoaddress", "consolidateaddress",
        // Scoped by requesting wallet (OperationBelongsToWallet(), rpcwallet.cpp)
        // rather than refused outright, now that the operations they report on
        // can genuinely belong to a secondary wallet.
        "z_getoperationstatus", "z_getoperationresult", "z_listoperationids",
        // Phase 5: consolidation/sweep settings, now genuine per-CWallet
        // fields (wallet.h) rather than process-globals or pwalletMain-only
        // fields, persisted per wallet via CWalletDB.
        "enablesaplingconsolidation", "enableironwoodconsolidation", "enableconsolidation",
        "consolidationaddresses", "consolidationstatus",
        "setconsolidationtarget", "setconsolidationfee", "setconsolidationinterval",
        "setironwoodconsolidationtarget", "setironwoodconsolidationfee", "setironwoodconsolidationinterval",
        "enablesweep", "sweepstatus", "setsweepfee", "setsweepinterval", "setsweepaddress",
        // Phase 5: fee/behavior/pruning settings and change-address/upgrade,
        // same promotion.
        "settxfee", "setmintxfee", "settxconfirmtarget", "setspendzeroconfchange",
        "setmintxvalue", "setkeypoolsize", "setwalletnotify",
        "setdeletetx", "setdeleteconflicttx", "setdeleteinterval",
        "setkeeptxnum", "setkeeptxfornblocks",
        "setchangeaddress", "upgradewallet",
    };
    return aware.count(name) != 0;
}
