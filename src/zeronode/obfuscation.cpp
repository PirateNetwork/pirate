// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zeronode/obfuscation.h"
#include "coincontrol.h"
#include "core_io.h"
#include "init.h"
#include "main.h"
#include "zeronode/zeronodeman.h"
#include "script/sign.h"
#include "zeronode/swifttx.h"
#include "ui_interface.h"
#include "util.h"
#include "key_io.h"
#include "consensus/validation.h"
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <boost/assign/list_of.hpp>
#include <openssl/rand.h>

using namespace std;
using namespace boost;

// A helper object for signing messages from Zeronodes
CObfuScationSigner obfuScationSigner;
// Keep track of the active Zeronode
CActiveZeronode activeZeronode;

bool GetTestingCollateralScript(std::string strAddress, CScript& script)
{
    if (!IsValidDestinationString(strAddress)) {
        LogPrintf("GetTestingCollateralScript - Invalid collateral address\n");
        return false;
    }

    auto dest = DecodeDestination(strAddress);
    script = GetScriptForDestination(dest);
    return true;
}

bool CObfuScationSigner::IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey)
{
    CScript payee2;
    payee2 = GetScriptForDestination(pubkey.GetID());

    CTransaction txVin;
    uint256 hash;
    if (GetTransaction(vin.prevout.hash, txVin, Params().GetConsensus(), hash, true)) {
        BOOST_FOREACH (CTxOut out, txVin.vout) {
            if (out.nValue == 10000 * COIN) {
                if (out.scriptPubKey == payee2) return true;
            }
        }
    }

    return false;
}

bool CObfuScationSigner::SetKey(std::string strSecret, std::string& errorMessage, CKey& key, CPubKey& pubkey)
{
    CKey key2 = DecodeSecret(strSecret);

    if (!key2.IsValid()) {
        errorMessage = _("Invalid private key.");
        return false;
    }

    key = key2;
    pubkey = key.GetPubKey();

    return true;
}

bool CObfuScationSigner::GetKeysFromSecret(std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet)
{
    CKey key2 = DecodeSecret(strSecret);

    if (!key2.IsValid()) return false;

    keyRet = key2;
    pubkeyRet = keyRet.GetPubKey();

    return true;
}

bool CObfuScationSigner::SignMessage(std::string strMessage, std::string& errorMessage, vector<unsigned char>& vchSig, CKey key)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    if (!key.SignCompact(ss.GetHash(), vchSig)) {
        errorMessage = _("Signing failed.");
        return false;
    }

    return true;
}

bool CObfuScationSigner::VerifyMessage(CPubKey pubkey, vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey2;
    if (!pubkey2.RecoverCompact(ss.GetHash(), vchSig)) {
        errorMessage = _("Error recovering public key.");
        return false;
    }

    if (fDebug && pubkey2.GetID() != pubkey.GetID())
        LogPrintf("CObfuScationSigner::VerifyMessage -- keys don't match: %s %s\n", pubkey2.GetID().ToString(), pubkey.GetID().ToString());

    return (pubkey2.GetID() == pubkey.GetID());
}

//TODO: Rename/move to core
void ThreadCheckObfuScationPool()
{
    if (fLiteMode) return; //disable all Zeronode related functionality

    // Make this thread recognisable as the wallet flushing thread
    RenameThread("zero-obfuscation");

    unsigned int c = 0;

    while (true) {
        MilliSleep(1000);

        // try to sync from all available nodes, one step at a time
        zeronodeSync.Process();

        if (zeronodeSync.IsBlockchainSynced()) {
            c++;

            // check if we should activate or ping every few minutes,
            // start right after sync is considered to be done
            if (c % ZERONODE_PING_SECONDS == 1) activeZeronode.ManageStatus();

            if (c % 60 == 0) {
                znodeman.CheckAndRemove();
                zeronodePayments.CleanPaymentList();
                CleanTransactionLocksList();
            }

            if(c % ZERONODES_DUMP_SECONDS == 0) DumpZeronodes();

        }
    }
}
