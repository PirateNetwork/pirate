// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_RPCPIRATEWALLET_H
#define BITCOIN_WALLET_RPCPIRATEWALLET_H

#include "zcash/address/sapling.hpp"
#include "zcash/address/ironwood.hpp"

struct balancestruct {
    CAmount confirmed;
    CAmount unconfirmed;
    CAmount locked;
    CAmount immature;
    bool spendable;
};

enum {
    ALL_SECTIONS = 0,
    SPEND_SECTION = 1,
    SEND_SECTION = 2,
    RECEIVE_SECTION = 3
};

class TransactionSpendT
{
public:
    string encodedAddress;
    string encodedScriptPubKey;
    CAmount amount;
    string spendTxid;
    int spendVout;
    bool spendable;
};

class TransactionSendT
{
public:
    string encodedAddress;
    string encodedScriptPubKey;
    CAmount amount;
    int vout;
    bool mine;
};

class TransactionReceivedT
{
public:
    string encodedAddress;
    string encodedScriptPubKey;
    CAmount amount;
    int vout;
    bool spendable;
};

class TransactionSpendZS
{
public:
    string encodedAddress;
    CAmount amount;
    string spendTxid;
    int spendShieldedOutputIndex;
    bool spendable;
};

class TransactionSpendZO
{
public:
    string encodedAddress;
    CAmount amount;
    string spendTxid;
    int spendShieldedActionIndex;
    bool spendable;
};

class TransactionSendZS
{
public:
    string encodedAddress;
    CAmount amount;
    int shieldedOutputIndex;
    string memo;
    string memoStr;
    bool mine;
    bool isInternalScope = false;
};

class TransactionSendZO
{
public:
    string encodedAddress;
    CAmount amount;
    int shieldedActionIndex;
    string memo;
    string memoStr;
    bool mine;
    bool isInternalScope = false;
};

class TransactionReceivedZS
{
public:
    string encodedAddress;
    CAmount amount;
    int shieldedOutputIndex;
    string memo;
    string memoStr;
    bool spendable;
    bool isInternalScope = false;
};

class TransactionReceivedZO
{
public:
    string encodedAddress;
    CAmount amount;
    int shieldedActionIndex;
    string memo;
    string memoStr;
    bool spendable;
    bool isInternalScope = false;
};

enum ArchiveType {
    ARCHIVED = 0,
    ACTIVE = 1
};

class RpcArcTransaction
{
public:
    uint256 txid;
    bool coinbase;
    string category;
    int64_t blockHeight;
    uint256 blockHash;
    int blockIndex;
    int64_t nBlockTime;
    int confirmations;
    int rawconfirmations;
    int64_t nTime;
    int64_t expiryHeight;
    uint64_t size;
    CAmount transparentValue = 0;
    CAmount saplingValue = 0;
    CAmount ironwoodValue = 0;
    int archiveType;
    std::set<libzcash::SaplingIncomingViewingKey> saplingIvks;
    std::set<uint256> saplingOvks;
    std::set<libzcash::IronwoodIncomingViewingKey> ironwoodIvks;
    std::set<libzcash::IronwoodOutgoingViewingKey> ironwoodOvks;
    std::set<string> spentFrom;
    std::set<string> sendTo;
    std::set<string> receivedIn;
    std::set<string> addresses;
    std::vector<TransactionSpendT> vTSpend;
    std::vector<TransactionSpendZS> vZsSpend;
    std::vector<TransactionSpendZO> vZoSpend;
    std::vector<TransactionSendT> vTSend;
    std::vector<TransactionSendZS> vZsSend;
    std::vector<TransactionSendZO> vZoSend;
    std::vector<TransactionReceivedT> vTReceived;
    std::vector<TransactionReceivedZS> vZsReceived;
    std::vector<TransactionReceivedZO> vZoReceived;
};

class RpcArcTransactions
{
public:
    std::map<std::pair<int, int>, RpcArcTransaction> mapArcTx;
};

// Transparent
template <typename RpcTx>
void getTransparentSends(CWallet* pwallet, RpcTx& tx, vector<TransactionSendT>& vSend, CAmount& transparentValue);

template <typename RpcTx>
void getTransparentSpends(CWallet* pwallet, RpcTx& tx, vector<TransactionSpendT>& vSpend, CAmount& transparentValue, bool fIncludeWatchonly = false);

template <typename RpcTx>
void getTransparentRecieves(CWallet* pwallet, RpcTx& tx, vector<TransactionReceivedT>& vReceived, bool fIncludeWatchonly = false);

// Sapling
template <typename RpcTx>
void getSaplingSends(CWallet* pwallet, const Consensus::Params& params, int nHeight, RpcTx& tx, std::set<uint256>& ovks, std::set<uint256>& ovksOut, vector<TransactionSendZS>& vSend);

template <typename RpcTx>
void getSaplingSpends(CWallet* pwallet, const Consensus::Params& params, int nHeight, RpcTx& tx, std::set<libzcash::SaplingIncomingViewingKey>& ivks, std::set<libzcash::SaplingIncomingViewingKey>& ivksOut, vector<TransactionSpendZS>& vSpend, bool fIncludeWatchonly = false);

template <typename RpcTx>
void getSaplingReceives(CWallet* pwallet, const Consensus::Params& params, int nHeight, RpcTx& tx, std::set<libzcash::SaplingIncomingViewingKey>& ivks, std::set<libzcash::SaplingIncomingViewingKey>& ivksOut, vector<TransactionReceivedZS>& vReceived, bool fIncludeWatchonly = false);

// Ironwood
template <typename RpcTx>
void getIronwoodSends(CWallet* pwallet, const Consensus::Params& params, int nHeight, RpcTx& tx, std::set<libzcash::IronwoodOutgoingViewingKey>& ovks, std::set<libzcash::IronwoodOutgoingViewingKey>& ovksOut, vector<TransactionSendZO>& vSend);

template <typename RpcTx>
void getIronwoodSpends(CWallet* pwallet, const Consensus::Params& params, int nHeight, RpcTx& tx, std::set<libzcash::IronwoodIncomingViewingKey>& ivks, std::set<libzcash::IronwoodIncomingViewingKey>& ivksOut, vector<TransactionSpendZO>& vSpend, bool fIncludeWatchonly = false);

template <typename RpcTx>
void getIronwoodReceives(CWallet* pwallet, const Consensus::Params& params, int nHeight, RpcTx& tx, std::set<libzcash::IronwoodIncomingViewingKey>& ivks, std::set<libzcash::IronwoodIncomingViewingKey>& ivksOut, vector<TransactionReceivedZO>& vReceived, bool fIncludeWatchonly = false);


void getAllSaplingOVKs(CWallet* pwallet, std::set<uint256>& ovks, bool fIncludeWatchonly = false);
void getAllSaplingIVKs(CWallet* pwallet, std::set<libzcash::SaplingIncomingViewingKey>& ivks, bool fIncludeWatchonly = false);
void getAllIronwoodOVKs(CWallet* pwallet, std::set<libzcash::IronwoodOutgoingViewingKey>& ovks, bool fIncludeWatchonly = false);
void getAllIronwoodIVKs(CWallet* pwallet, std::set<libzcash::IronwoodIncomingViewingKey>& ivks, bool fIncludeWatchonly = false);

void getRpcArcTxSaplingKeys(CWallet* pwallet, const CWalletTx& tx, int txHeight, RpcArcTransaction& arcTx, bool fIncludeWatchonly = false);
void getRpcArcTxIronwoodKeys(CWallet* pwallet, const CWalletTx& tx, int txHeight, RpcArcTransaction& arcTx, bool fIncludeWatchonly = false);
void getRpcArcTx(CWallet* pwallet, CWalletTx& tx, RpcArcTransaction& arcTx, bool fIncludeWatchonly = false, bool rescan = false);
void getRpcArcTx(CWallet* pwallet, uint256& txid, RpcArcTransaction& arcTx, bool fIncludeWatchonly = false, bool rescan = false);

void getRpcArcTxJSONHeader(RpcArcTransaction& arcTx, UniValue& ArcTxJSON);
void getRpcArcTxJSONSpends(RpcArcTransaction& arcTx, UniValue& ArcTxJSON, bool filterAddress = false, string addressString = "");
void getRpcArcTxJSONSends(RpcArcTransaction& arcTx, UniValue& ArcTxJSON, bool filterAddress = false, string addressString = "");
void getRpcArcTxJSONReceives(RpcArcTransaction& arcTx, UniValue& ArcTxJSON, bool filterAddress = false, string addressString = "");

// Used by decoderawtransaction (rpc/rawtransaction.cpp) to annotate a raw,
// not-yet-broadcast transaction's inputs/outputs with whatever the given
// wallet can decrypt. pwallet may be null (-disablewallet, NSPV superlite):
// that yields an arcTx with no spends/sends/receives rather than walking the
// wallet. Requires cs_main; takes cs_wallet itself when pwallet is non-null.
void decrypttransaction(CWallet* pwallet, CTransaction& tx, RpcArcTransaction& arcTx, int nHeight);

class CRPCTable;

void RegisterPirateExclusiveRPCCommands(CRPCTable& tableRPC);

#endif // BITCOIN_WALLET_RPCWALLET_H
