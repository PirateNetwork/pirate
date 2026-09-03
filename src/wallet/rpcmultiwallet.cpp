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
    if (fHelp || params.size() < 1 || params.size() > 6)
        throw runtime_error(
            "loadwallet \"filename\" ( passphrase rescan rescanheight zapwallettxes salvage )\n"
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
            "2. passphrase      (string, optional) required if this wallet's file is encrypted --\n"
            "                   unlocks it as part of this call, synchronously (either succeeds\n"
            "                   immediately or this call fails immediately; there is no waiting\n"
            "                   for it to be supplied some other way). Omit or pass \"\" for an\n"
            "                   unencrypted wallet. The wallet remains unlocked afterward, exactly\n"
            "                   as if walletpassphrase had also been called with no timeout.\n"
            "3. rescan          (boolean or numeric, optional, default=false) if true, rescan from\n"
            "                   genesis (or from rescanheight if given) instead of this wallet's own\n"
            "                   persisted checkpoint\n"
            "4. rescanheight    (numeric, optional, default=0) block height to rescan from when\n"
            "                   rescan is set; 0 or omitted means genesis\n"
            "5. zapwallettxes   (boolean, optional, default=false) wipe this wallet's transaction\n"
            "                   history and rebuild it from a full rescan; implies rescan\n"
            "6. salvage         (boolean, optional, default=false) attempt salvage/recovery on this\n"
            "                   wallet's file specifically before loading it, if it appears corrupt\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"filename\"    (string) the wallet name\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("loadwallet", "\"second\"")
            + HelpExampleCli("loadwallet", "\"second\" \"\" true")
            + HelpExampleRpc("loadwallet", "\"second\"")
        );

    std::string name = params[0].get_str();

    // Not mlock()'d, same caveat every other passphrase-taking RPC parameter
    // in this codebase already carries (see e.g. encryptwallet/
    // walletpassphrase) -- UniValue's own string storage isn't secure-heap
    // backed either way.
    SecureString strPassphrase;
    if (params.size() > 1 && !params[1].get_str().empty()) {
        strPassphrase.reserve(100);
        strPassphrase = params[1].get_str().c_str();
    }

    bool fRescan = params.size() > 2 ? params[2].get_bool() : false;
    int nRescanHeight = params.size() > 3 ? params[3].get_int() : 0;
    bool fZapWalletTxes = params.size() > 4 ? params[4].get_bool() : false;
    bool fSalvage = params.size() > 5 ? params[5].get_bool() : false;

    std::string strError;
    if (!CWalletManager::Get().LoadWallet(name, strError, fRescan, nRescanHeight, fSalvage, fZapWalletTxes,
                                           /*fAllowCreate=*/false, strPassphrase))
        throw JSONRPCError(RPC_WALLET_ERROR, strError);

    UniValue result(UniValue::VOBJ);
    result.pushKV("name", name);
    return result;
}

UniValue createwallet(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "createwallet \"filename\" ( \"recoveryphrase\" recoverylangcode )\n"
            "\nCreates a wallet file in the data directory and loads it (same restrictions\n"
            "as loadwallet once loaded). Refuses if a file already exists under this name\n"
            "-- use loadwallet for that.\n"
            "\nWith no recoveryphrase, generates a brand-new random seed. IMPORTANT: the\n"
            "seed phrase for a newly-generated wallet is returned exactly once, in this\n"
            "call's result. Record it immediately; there is no way to retrieve it again\n"
            "later except via the wallet's own z_exportwallet/dumpwallet-style backup\n"
            "once it's loaded.\n"
            "\nWith recoveryphrase given, restores the wallet's seed from it instead of\n"
            "generating a new one -- this is the replacement for the old -seedphrase=\n"
            "startup flag (removed): start the node with nothing loaded (or -wallet=\n"
            "naming a not-yet-existing file), then call this RPC with the known phrase.\n"
            "\nArguments:\n"
            "1. \"filename\"        (string, required) the wallet file name to create, in the data directory\n"
            "2. \"recoveryphrase\"  (string, optional) an existing 12/18/24-word seed phrase to restore from,\n"
            "                     instead of generating a new random seed\n"
            "3. recoverylangcode  (numeric, optional, default=0) the BIP-39 wordlist language recoveryphrase\n"
            "                     was generated in (hd_seed::MnemonicLanguage) -- only meaningful together\n"
            "                     with recoveryphrase\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"filename\",     (string) the wallet name\n"
            "  \"seedphrase\" : \"...\"      (string) the wallet's seed phrase -- the newly-generated one, or an\n"
            "                             echo of recoveryphrase if one was given -- back this up now\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("createwallet", "\"second\"")
            + HelpExampleRpc("createwallet", "\"second\"")
        );

    std::string name = params[0].get_str();

    // Not mlock()'d beyond this point, same pre-existing caveat every other
    // SecureString-from-a-UniValue-parameter handoff in this codebase already
    // carries (see e.g. loadwallet's own passphrase parameter) -- UniValue's
    // own string storage isn't secure-heap backed either way.
    SecureString recoveryPhrase;
    if (params.size() > 1 && !params[1].get_str().empty()) {
        recoveryPhrase.reserve(params[1].get_str().size() + 1);
        recoveryPhrase = params[1].get_str().c_str();
    }
    uint32_t recoveryLangCode = params.size() > 2 ? (uint32_t)params[2].get_int() : 0;

    std::string strError, seedPhrase;
    if (!CWalletManager::Get().CreateWallet(name, strError, seedPhrase, recoveryPhrase, recoveryLangCode))
        throw JSONRPCError(RPC_WALLET_ERROR, strError);

    UniValue result(UniValue::VOBJ);
    result.pushKV("name", name);
    result.pushKV("seedphrase", seedPhrase);
    return result;
}

UniValue unloadwallet(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "unloadwallet \"walletname\"\n"
            "\nUnloads a currently loaded wallet.\n"
            "The currently active wallet (see getactivewallet/setactivewallet) can never\n"
            "be unloaded directly -- call setactivewallet with a different name (or \"\")\n"
            "first. A wallet currently bound to the mining thread (setgenerate true) can\n"
            "also not be unloaded until mining is stopped. A wallet cannot be unloaded\n"
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

UniValue getactivewallet(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getactivewallet\n"
            "\nReturns the name of the currently active wallet, or null if no wallet is\n"
            "active. An unscoped RPC call (no /wallet/<name>/ URI segment) always resolves\n"
            "to this wallet.\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"filename\"    (string or null) the active wallet's name, or null if none is active\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getactivewallet", "")
            + HelpExampleRpc("getactivewallet", "")
        );

    std::string name = CWalletManager::Get().GetActiveWalletName();
    UniValue result(UniValue::VOBJ);
    if (name.empty())
        result.pushKV("name", NullUniValue);
    else
        result.pushKV("name", name);
    return result;
}

UniValue setactivewallet(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "setactivewallet ( \"walletname\" )\n"
            "\nSets which currently-loaded wallet is active. An unscoped RPC call (no\n"
            "/wallet/<name>/ URI segment) always resolves to the active wallet -- loading a\n"
            "new wallet (loadwallet/createwallet) never changes which wallet is active,\n"
            "call this afterward if you want the newly loaded wallet to become the target\n"
            "of unscoped RPCs.\n"
            "\nPass \"\" (or omit) to deactivate -- no wallet will be active until this is\n"
            "called again. Deactivating is the only way to make the very last loaded\n"
            "wallet eligible for unloadwallet: the active wallet itself can never be\n"
            "unloaded directly.\n"
            "\nArguments:\n"
            "1. \"walletname\"    (string, optional, default=\"\") the wallet name to activate, or \"\" to deactivate\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"walletname\"    (string or null) the new active wallet's name, or null if deactivated\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("setactivewallet", "\"second\"")
            + HelpExampleRpc("setactivewallet", "\"second\"")
        );

    std::string name = params.size() > 0 ? params[0].get_str() : "";
    std::string strError;
    if (!CWalletManager::Get().SetActiveWallet(name, strError)) {
        if (strError.find("not found") != std::string::npos)
            throw JSONRPCError(RPC_WALLET_NOT_FOUND, strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    UniValue result(UniValue::VOBJ);
    if (name.empty())
        result.pushKV("name", NullUniValue);
    else
        result.pushKV("name", name);
    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                actor (function)     okSafeMode
  //  --------------------- ------------------- --------------------- ----------
    { "wallet",             "listwallets",      &listwallets,         true  },
    { "wallet",             "loadwallet",       &loadwallet,          true  },
    { "wallet",             "createwallet",     &createwallet,        true  },
    { "wallet",             "unloadwallet",     &unloadwallet,        true  },
    { "wallet",             "getactivewallet",  &getactivewallet,     true  },
    { "wallet",             "setactivewallet",  &setactivewallet,     true  },
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
    // (An earlier note here claimed encryptwallet calls StartShutdown() on
    // success. Reading it shows that was wrong: only its *failure* path ever
    // restarted the node, to safely restore the pre-encryption backup. The
    // real blocker turned out to be the other end -- see its entry in the set
    // below and the refusal in the handler itself.)
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
        "loadwallet", "unloadwallet", "listwallets", "createwallet",
        // No-default-wallet redesign: same reasoning as loadwallet/unloadwallet
        // above -- these ignore any URI-selected wallet by design, but a
        // /wallet/x/setactivewallet call shouldn't be refused outright by the
        // dispatch gate just because it isn't yet rewired the way ordinary
        // wallet-operating RPCs are.
        "getactivewallet", "setactivewallet",
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
        // Backlog item 9: encryptwallet now resolves GetWalletForRequest()
        // instead of always pwalletMain, and a failed attempt against a
        // secondary wallet recovers in-process (CWalletManager::
        // DiscardWalletAfterFailedEncryption() + LoadWallet(), rpcwallet.cpp)
        // instead of restarting the whole node -- only the default wallet's
        // own failure path still does that, since it has no unload/reload
        // route at all (see the "no-default-wallet redesign" backlog item).
        // Encrypting a secondary wallet is now fully supported end to end:
        // the resulting file can be loaded again with its own passphrase,
        // via loadwallet's passphrase argument, -secondarywalletpassphrase=
        // at startup, or the GUI's open-wallet prompt (CWalletManager::
        // LoadWallet()'s per-wallet unlock path). The temporary refusal that
        // used to live in the handler -- added while that reload path did
        // not yet exist, so that a successful encryption could not brick the
        // wallet -- has been removed.
        "encryptwallet",
        // Phase 9: the Crypto-Conditions (CC) smart-contract RPCs. These now
        // resolve CWalletManager::GetWalletForRequest() instead of always
        // pwalletMain (see CCtx.cpp/CCutils.cpp for the choke-point threading,
        // and rpcwallet.cpp's CNSPVWalletLockGuard for the former
        // Lock2NSPV/Unlock2NSPV pair). Read-only RPCs in the same modules are
        // included too even where they never touch pwalletMain at all -- they're
        // chain-derived and wallet-independent, so allowlisting them is a
        // formality that stops them being refused against a secondary wallet
        // for no reason. Left out (see cc/ and rpc/crosschain.cpp comments for
        // why): tokenswapask/tokenfillswap (commented out of the command
        // table), importgatewaydumpprivkey (not registered; its
        // pwalletMain->GetKey() call is itself already commented out), lotto/
        // auction (only lottoaddress/auctionaddress are live, no wallet touch),
        // musig (not part of the node build at all).
        "assetsaddress", "tokeninfo", "tokenlist", "tokenorders", "mytokenorders",
        "tokenaddress", "tokenbalance", "tokencreate", "tokentransfer",
        "tokenbid", "tokencancelbid", "tokenfillbid", "tokenask",
        "tokencancelask", "tokenfillask", "tokenconvert",
        "dicelist", "diceinfo", "dicefund", "diceaddfunds", "dicebet",
        "dicefinish", "dicestatus", "diceaddress",
        "rewardslist", "rewardsinfo", "rewardscreatefunding", "rewardsaddfunding",
        "rewardslock", "rewardsunlock", "rewardsaddress",
        "faucetinfo", "faucetfund", "faucetget", "faucetaddress",
        "heiraddress", "heirfund", "heiradd", "heirclaim", "heirinfo", "heirlist",
        "paymentsaddress", "paymentstxidopret", "paymentscreate", "paymentsairdrop",
        "paymentsairdroptokens", "paymentslist", "paymentsinfo", "paymentsfund",
        "paymentsmerge", "paymentsrelease",
        "FSMaddress", "FSMcreate", "FSMlist", "FSMinfo",
        "cclibaddress", "cclibinfo", "cclib",
        "gatewaysaddress", "gatewayslist", "gatewaysexternaladdress",
        "gatewaysdumpprivkey", "gatewaysinfo", "gatewaysbind", "gatewaysdeposit",
        "gatewaysclaim", "gatewayswithdraw", "gatewayspartialsign",
        "gatewayscompletesigning", "gatewaysmarkdone", "gatewayspendingdeposits",
        "gatewayspendingwithdraws", "gatewaysprocessed",
        "oraclesaddress", "oracleslist", "oraclesinfo", "oraclescreate",
        "oraclesfund", "oraclesregister", "oraclessubscribe", "oraclesdata",
        "oraclessample", "oraclessamples",
        "channelsaddress", "channelslist", "channelsinfo", "channelsopen",
        "channelspayment", "channelsclose", "channelsrefund",
        // Phase 9: the crosschain.cpp import/self-import/migrate RPCs.
        // importdual and importgatewaydeposit never touch pwalletMain (they
        // build unsigned proof transactions, not wallet-signed ones) but are
        // included for the same read-only-consistency reason as above.
        "migrate_checkburntransactionsource", "migrate_createnotaryapprovaltransaction",
        // No-default-wallet redesign: migrate_createburntransaction was
        // conspicuously missing from this list despite being wallet-aware
        // like its siblings just above -- it had no EnsureWalletIsAvailable
        // guard at all until this phase (rpc/crosschain.cpp).
        "migrate_createburntransaction",
        "selfimport", "importdual", "importgatewayddress", "importgatewayinfo",
        "importgatewaybind", "importgatewaydeposit", "importgatewaywithdraw",
        "importgatewaypartialsign", "importgatewaycompletesigning",
        "importgatewaymarkdone", "importgatewaypendingwithdraws",
        "importgatewayprocessed",
        // Phase 10: the 46 mechanical NEEDS-REWIRING functions from Phase 8's
        // census (wallet/rpcwallet.cpp, wallet/rpcdump.cpp, and a handful of
        // misc RPC files) -- plain pwalletMain -> GetWalletForRequest() swaps,
        // same shape as Phases 2/5's original rewiring.
        "getaccountaddress", "getrawchangeaddress", "setaccount", "getaccount",
        "getaddressesbyaccount", "signmessage", "getreceivedbyaddress",
        "getreceivedbyaccount", "cleanwallettransactions", "getunconfirmedbalance",
        "sendfrom", "sendmany", "listreceivedbyaddress", "listreceivedbyaccount",
        "listaccounts", "listsinceblock", "walletpassphrasechange", "lockunspent",
        "listlockunspent", "getkeypoolsize", "resendwallettransactions",
        "z_listunspent", "z_getnewaddresskey", "z_getnewaddress",
        "z_setprimaryspendingkey", "z_listaddresses", "z_listreceivedbyaddress",
        "z_getbalances", "z_gettotalbalance", "z_viewtransaction",
        "z_exportsaplingdisclosure", "z_exportironwooddisclosure", "getbalance64",
        "importaddress", "rescan", "z_importkey", "z_importviewingkey",
        "z_exportkey", "z_exportviewingkey", "z_setaddressbook",
        "getinfo", "validateaddress", "z_validateaddress", "nn_getwalletinfo",
        "getwalletburntransactions", "signrawtransaction",
        // rpcpiratewallet/rpcdump plumbing phase: the 6 zs_*/getalldata RPCs
        // (wallet/rpcpiratewallet.cpp) whose shared getRpcArcTx()/getAll*VKs()
        // helper layer previously hardcoded pwalletMain, and the 5
        // wallet/rpcdump.cpp RPCs blocked on importwallet_impl()/
        // dumpwallet_impl() (shared by the t-only and z-inclusive variants of
        // each) plus z_exportseedphrase, which was simply missed by Phase 10.
        "zs_listtransactions", "zs_gettransaction", "zs_listspentbyaddress",
        "zs_listreceivedbyaddress", "zs_listsentbyaddress", "getalldata",
        "importwallet", "z_importwallet", "dumpwallet", "z_exportwallet",
        "z_exportseedphrase",
        // decoderawtransaction (rpc/rawtransaction.cpp) optionally annotates a
        // raw transaction with whatever the resolved wallet can decrypt --
        // was unconditionally pwalletMain via decrypttransaction() until this
        // phase resolved it per-request too, for the same reason.
        "decoderawtransaction",
        // Phase 12: the offline-signing trio (rpc/rawtransaction.cpp).
        // z_createbuildinstructions/z_createbuildinstructionscoincontrol are
        // rewired the standard way (GetWalletForRequest()). z_buildrawtransaction
        // is listed here purely so selecting a wallet in its request URI doesn't
        // get refused outright -- the handler itself ignores any such selection
        // and searches every loaded wallet for whichever one holds the needed
        // spending key instead, since the two-step offline round trip gives it
        // no other way to know in advance which wallet that is.
        "z_createbuildinstructions", "z_createbuildinstructionscoincontrol",
        "z_buildrawtransaction",
    };
    return aware.count(name) != 0;
}
