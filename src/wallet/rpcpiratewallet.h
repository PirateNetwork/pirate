// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_RPCPIRATEWALLET_H
#define BITCOIN_WALLET_RPCPIRATEWALLET_H

struct balancestruct {
  CAmount confirmed;
  CAmount unconfirmed;
  CAmount locked;
  CAmount immature;
  bool spendable;
};

enum
{
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


class TransactionSpendZC
{
public:
    string encodedAddress;
    CAmount amount;
    string spendTxid;
    int spendJsIndex;
    int spendJsOutIndex;
    bool spendable;
};

class TransactionReceivedZC
{
public:
    string encodedAddress;
    CAmount amount;
    int jsIndex;
    int jsOutIndex;
    string memo;
    string memoStr;
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

class TransactionSendZS
{
public:
    string encodedAddress;
    CAmount amount;
    int shieldedOutputIndex;
    string memo;
    string memoStr;
    bool mine;
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
      CAmount sproutValue = 0;
      CAmount sproutValueSpent = 0;
      CAmount saplingValue = 0;
      int archiveType;
      std::set<uint256> ivks;
      std::set<uint256> ovks;
      std::set<string> spentFrom;
      std::set<string> addresses;
      std::vector<TransactionSpendT> vTSpend;
      std::vector<TransactionSpendZC> vZcSpend;
      std::vector<TransactionSpendZS> vZsSpend;
      std::vector<TransactionSendT> vTSend;
      std::vector<TransactionSendZS> vZsSend;
      std::vector<TransactionReceivedT> vTReceived;
      std::vector<TransactionReceivedZC> vZcReceived;
      std::vector<TransactionReceivedZS> vZsReceived;
};

class RpcArcTransactions
{
public:
      std::map<std::pair<int,int>, RpcArcTransaction> mapArcTx;
};

void getRpcArcTransactions(RpcArcTransactions &arcTxs);

template<typename RpcTx>
void getTransparentSends(RpcTx &tx, vector<TransactionSendT> &vSend, CAmount &transparentValue);

template<typename RpcTx>
void getTransparentSpends(RpcTx &tx, vector<TransactionSpendT> &vSpend, CAmount &transparentValue, bool fIncludeWatchonly = false);

template<typename RpcTx>
void getTransparentRecieves(RpcTx &tx, vector<TransactionReceivedT> &vReceived, bool fIncludeWatchonly = false);


template<typename RpcTx>
void getSproutSpends(RpcTx &tx, vector<TransactionSpendZC> &vSpend, CAmount &sproutValue, CAmount &sproutValueSpent, bool fIncludeWatchonly = false);

template<typename RpcTx>
void getSproutReceives(RpcTx &tx, vector<TransactionReceivedZC> &vReceived, bool fIncludeWatchonly = false);


template<typename RpcTx>
void getSaplingSends(const Consensus::Params& params, int nHeight, RpcTx &tx, std::set<uint256> &ovks, std::set<uint256> &ovksOut, vector<TransactionSendZS> &vSend);

template<typename RpcTx>
void getSaplingSpends(const Consensus::Params& params, int nHeight, RpcTx &tx, std::set<uint256> &ivks, std::set<uint256> &ivksOut, vector<TransactionSpendZS> &vSpend, bool fIncludeWatchonly = false);

template<typename RpcTx>
void getSaplingReceives(const Consensus::Params& params, int nHeight, RpcTx &tx, std::set<uint256> &ivks, std::set<uint256> &ivksOut , vector<TransactionReceivedZS> &vReceived, bool fIncludeWatchonly = false);

void getAllSproutRKs(vector<uint256> &rks);
void getAllSaplingOVKs(std::set<uint256> &ovks, bool fIncludeWatchonly = false);
void getAllSaplingIVKs(std::set<uint256> &ivks, bool fIncludeWatchonly = false);

void getRpcArcTx(CWalletTx &tx, RpcArcTransaction &arcTx, bool fIncludeWatchonly = false, bool rescan = false);
void getRpcArcTx(uint256 &txid, RpcArcTransaction &arcTx, bool fIncludeWatchonly = false, bool rescan = false);

void getRpcArcTxJSONHeader(RpcArcTransaction &arcTx, UniValue& ArcTxJSON);
void getRpcArcTxJSONSpends(RpcArcTransaction &arcTx, UniValue& ArcTxJSON, bool filterAddress = false, string addressString = "");
void getRpcArcTxJSONSends(RpcArcTransaction &arcTx, UniValue& ArcTxJSON, bool filterAddress = false, string addressString = "");
void getRpcArcTxJSONReceives(RpcArcTransaction &arcTx, UniValue& ArcTxJSON, bool filterAddress = false, string addressString = "");

void decrypttransaction(CTransaction &tx, RpcArcTransaction &arcTx, int nHeight);

class CRPCTable;

void RegisterPirateExclusiveRPCCommands(CRPCTable &tableRPC);

#endif //BITCOIN_WALLET_RPCWALLET_H
