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
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "loadwallet \"filename\"\n"
            "\nLoads a wallet file that already exists in the data directory.\n"
            "Once loaded, a core subset of wallet RPCs (getbalance, getnewaddress,\n"
            "sendtoaddress, listtransactions, listunspent, gettransaction, getwalletinfo,\n"
            "listaddressgroupings, z_getbalance, backupwallet, dumpprivkey, importprivkey,\n"
            "walletpassphrase, walletlock, keypoolrefill) can be run against it via the\n"
            "/wallet/<name>/ endpoint. Every other wallet RPC still runs against the\n"
            "default wallet only, regardless of which wallet the request is routed to.\n"
            "\nKNOWN LIMITATION: a loaded wallet is NOT subscribed to new-block\n"
            "notifications, so its balance and transaction data reflect only what was\n"
            "already in the file at load time and will not update as new blocks arrive,\n"
            "for as long as it stays loaded. This applies to sendtoaddress too: it will\n"
            "select from and report against that same stale coin set, not just report on it.\n"
            "\nArguments:\n"
            "1. \"filename\"    (string, required) the wallet file name, in the data directory\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"filename\"    (string) the wallet name\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("loadwallet", "\"second\"")
            + HelpExampleRpc("loadwallet", "\"second\"")
        );

    std::string name = params[0].get_str();
    std::string strError;
    if (!CWalletManager::Get().LoadWallet(name, strError))
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
            "The default wallet can never be unloaded, and a wallet with a pending\n"
            "request routed to it cannot be unloaded until that request completes.\n"
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
    // settxfee and encryptwallet were in the original phase-2 scope but got
    // dropped once their code was actually read: settxfee assigns the
    // process-global `payTxFee` (confirmed read directly by
    // CWallet::CreateTransaction() via wallet_fees.cpp, documented there as
    // "user-set global variable") -- rewiring it to take a wallet parameter
    // would still change every wallet's fee behavior, not just the selected
    // one. encryptwallet unconditionally calls StartShutdown() on success --
    // encrypting one secondary wallet would restart the whole node. Both need
    // their own design before they can be exposed here, not a mechanical
    // pwalletMain->pwallet substitution.
    static const std::set<std::string> aware = {
        "loadwallet", "unloadwallet", "listwallets",
        "getbalance", "getnewaddress", "sendtoaddress",
        "listtransactions", "listunspent", "gettransaction",
        "getwalletinfo", "listaddressgroupings", "z_getbalance",
        "backupwallet", "dumpprivkey", "importprivkey",
        "walletpassphrase", "walletlock", "keypoolrefill",
    };
    return aware.count(name) != 0;
}
