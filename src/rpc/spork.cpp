// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2019 The Zero developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "init.h"
#include "key_io.h"
#include "main.h"
#include "rpc/server.h"
#include "zeronode/spork.h"

#include <stdint.h>

#include <univalue.h>

using namespace std;

UniValue spork(const UniValue& params, bool fHelp)
{
    if (params.size() == 1 && params[0].get_str() == "show") {
        UniValue ret(UniValue::VOBJ);
        for (int nSporkID = SPORK_START; nSporkID <= SPORK_END; nSporkID++) {
            if (sporkManager.GetSporkNameByID(nSporkID) != "Unknown")
                ret.push_back(Pair(sporkManager.GetSporkNameByID(nSporkID), GetSporkValue(nSporkID)));
        }
        return ret;
    } else if (params.size() == 1 && params[0].get_str() == "active") {
        UniValue ret(UniValue::VOBJ);
        for (int nSporkID = SPORK_START; nSporkID <= SPORK_END; nSporkID++) {
            if (sporkManager.GetSporkNameByID(nSporkID) != "Unknown")
                ret.push_back(Pair(sporkManager.GetSporkNameByID(nSporkID), IsSporkActive(nSporkID)));
        }
        return ret;
    } else if (params.size() == 2) {
        int nSporkID = sporkManager.GetSporkIDByName(params[0].get_str());
        if (nSporkID == -1) {
            return "Invalid spork name";
        }

        // SPORK VALUE
        int64_t nValue = params[1].get_int64();

        //broadcast new spork
        if (sporkManager.UpdateSpork(nSporkID, nValue)) {
            return "success";
        } else {
            return "failure";
        }
    }

    throw runtime_error(
            "spork \"name\" ( value )\n"
            "\nReturn spork values or their active state.\n"

            "\nArguments:\n"
            "1. \"name\"        (string, required)  \"show\" to show values, \"active\" to show active state.\n"
            "                       When set up as a spork signer, the name of the spork can be used to update it's value.\n"
            "2. value           (numeric, required when updating a spork) The new value for the spork.\n"

            "\nResult (show):\n"
            "{\n"
            "  \"spork_name\": nnn      (key/value) Key is the spork name, value is it's current value.\n"
            "  ,...\n"
            "}\n"

            "\nResult (active):\n"
            "{\n"
            "  \"spork_name\": true|false      (key/value) Key is the spork name, value is a boolean for it's active state.\n"
            "  ,...\n"
            "}\n"

            "\nResult (name):\n"
            " \"success|failure\"       (string) Wither or not the update succeeded.\n"

            "\nExamples:\n" +
            HelpExampleCli("spork", "show") + HelpExampleRpc("spork", "show"));
}

UniValue createsporkkeys(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
                "createsporkkeys\n"
                "\nCreate a set of private and public keys used for sporks\n"

                "\nResult:\n"
                "\"pubkey\"    (string) Spork public key\n"
                "\"privkey\"    (string) Spork private key\n"

                "\nExamples:\n" +
                HelpExampleCli("createsporkkeys", "") + HelpExampleRpc("createsporkkeys", ""));

    CKey secret;
    secret.MakeNewKey(false);

    CPubKey pubKey = secret.GetPubKey();

    std::string str;
    for (int i = 0; i < pubKey.size(); i++) {
        str += pubKey[i];
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("pubkey", HexStr(str)));
    ret.push_back(Pair("privkey", EncodeSecret(secret)));
    return ret;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "spork",               "spork",                  &spork,                  false},
    /** Not shown in help menu */
    { "hidden",              "createsporkkeys",        &createsporkkeys,        false},
};
void RegisterSporkRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
