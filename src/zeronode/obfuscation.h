// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OBFUSCATION_H
#define OBFUSCATION_H

#include "main.h"
#include "zeronode/activezeronode.h"
#include "zeronode/payments.h"
#include "zeronode/zeronode-sync.h"
#include "zeronode/zeronodeman.h"
#include "sync.h"

class CTxIn;
class CObfuScationSigner;
class CZeroNodeVote;
class CActiveZeronode;

// status update message constants
#define ZERONODE_ACCEPTED 1
#define ZERONODE_REJECTED 0
#define ZERONODE_RESET -1

extern CObfuScationSigner obfuScationSigner;
extern std::string strZeroNodePrivKey;
extern CActiveZeronode activeZeronode;

bool GetTestingCollateralScript(std::string strAddress, CScript& script);

/** Helper object for signing and checking signatures
 */
class CObfuScationSigner
{
public:
    /// Is the inputs associated with this public key? (and there is 10000 ZER - checking if valid zeronode)
    bool IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey);
    /// Set the private/public key values, returns true if successful
    bool GetKeysFromSecret(std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet);
    /// Set the private/public key values, returns true if successful
    bool SetKey(std::string strSecret, std::string& errorMessage, CKey& key, CPubKey& pubkey);
    /// Sign the message, returns true if successful
    bool SignMessage(std::string strMessage, std::string& errorMessage, std::vector<unsigned char>& vchSig, CKey key);
    /// Verify the message, returns true if succcessful
    bool VerifyMessage(CPubKey pubkey, std::vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage);
};

void ThreadCheckObfuScationPool();

#endif
