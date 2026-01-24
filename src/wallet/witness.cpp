// Copyright (c) 2019 Cryptoforge
// Copyright (c) 2019 The Zero developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "witness.h"
#include "coins.h"
#include "init.h"
#include "rpc/server.h"
#include "utilmoneystr.h"
#include "wallet.h"

using namespace std;
using namespace libzcash;

bool EnsureWalletIsAvailable(bool avoidException);

UniValue getsaplingwitness(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getsaplingwitness txid sheildedOutputIndex\n" + HelpExampleCli("getsaplingwitness", "26d4c79aab980bc39ac0deb1d8224c399249a6f5d6d3b3a6d58e6374750854c1 0") + HelpExampleRpc("getsaplingwitness", "26d4c79aab980bc39ac0deb1d8224c399249a6f5d6d3b3a6d58e6374750854c1 0"));

    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "This RPC command has been deprecated and is no longer available.");

    return NullUniValue;
}

UniValue exportsaplingtree(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 0)
        throw runtime_error(
            "exportsaplingtree\n" + HelpExampleCli("exportsaplingtree", "") + HelpExampleRpc("exportsaplingtree", ""));

    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "This RPC command has been deprecated and is no longer available. Please use z_gettreestate instead.");

    return NullUniValue;
}

UniValue getsaplingwitnessatheight(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 3)
        throw runtime_error(
            "getsaplingwitnessatheight txid sheildedOutputIndex\n" + HelpExampleCli("getsaplingwitnessatheight", "26d4c79aab980bc39ac0deb1d8224c399249a6f5d6d3b3a6d58e6374750854c1 0") + HelpExampleRpc("getsaplingwitnessatheight", "26d4c79aab980bc39ac0deb1d8224c399249a6f5d6d3b3a6d58e6374750854c1 0"));

    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "This RPC command has been deprecated and is no longer available. Please use z_gettreestate instead.");

    return NullUniValue;
}


UniValue getsaplingblocks(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getsaplingblocks \"startheight blocksqty\" ( verbosity )\n"

            "\nExamples:\n" +
            HelpExampleCli("getsaplingblocks", "12800 1") + HelpExampleRpc("getsaplingblocks", "12800 1"));

    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "This RPC command has been deprecated and is no longer available. Please use z_gettreestate instead.");

    return NullUniValue;
}


static const CRPCCommand commands[] =
    {
        //  category              name                            actor (function)              okSafeMode
        //  --------------------- ------------------------        -----------------------       ----------
        {"pirate Experimental", "exportsaplingtree", &exportsaplingtree, true},
        {"pirate Experimental", "getsaplingwitness", &getsaplingwitness, true},
        {"pirate Experimental", "getsaplingwitnessatheight", &getsaplingwitnessatheight, true},
        {"pirate Experimental", "getsaplingblocks", &getsaplingblocks, true},

};

void RegisterZeroExperimentalRPCCommands(CRPCTable& tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
