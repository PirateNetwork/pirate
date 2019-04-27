// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZERONODE_PAYMENTS_H
#define ZERONODE_PAYMENTS_H

#include "key.h"
#include "key_io.h"
#include "main.h"
#include "zeronode/zeronode.h"
#include <boost/lexical_cast.hpp>

using namespace std;

extern CCriticalSection cs_vecPayments;
extern CCriticalSection cs_mapZeronodeBlocks;
extern CCriticalSection cs_mapZeronodePayeeVotes;

class CZeronodePayments;
class CZeronodePaymentWinner;
class CZeronodeBlockPayees;

extern CZeronodePayments zeronodePayments;

#define ZNPAYMENTS_SIGNATURES_REQUIRED 6
#define ZNPAYMENTS_SIGNATURES_TOTAL 10

void ProcessMessageZeronodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue);
void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, CTxOut& txFounders, CTxOut& txZeronodes);

void DumpZeronodePayments();

/** Save Zeronode Payment Data (znpayments.dat)
 */
class CZeronodePaymentDB
{
private:
    boost::filesystem::path pathDB;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CZeronodePaymentDB();
    bool Write(const CZeronodePayments& objToSave);
    ReadResult Read(CZeronodePayments& objToLoad, bool fDryRun = false);
};

class CZeronodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CZeronodePayee()
    {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CZeronodePayee(CScript payee, int nVotesIn)
    {
        scriptPubKey = payee;
        nVotes = nVotesIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(nVotes);
    }
};

// Keep track of votes for payees from zeronodes
class CZeronodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CZeronodePayee> vecPayments;

    CZeronodeBlockPayees()
    {
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CZeronodeBlockPayees(int nBlockHeightIn)
    {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(CScript payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH (CZeronodePayee& payee, vecPayments) {
            if (payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CZeronodePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        BOOST_FOREACH (CZeronodePayee& p, vecPayments) {
            if (p.nVotes > nVotes) {
                payee = p.scriptPubKey;
                nVotes = p.nVotes;
            }
        }

        return (nVotes > -1);
    }

    bool HasPayeeWithVotes(CScript payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH (CZeronodePayee& p, vecPayments) {
            if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
    }
};

// for storing the winning payments
class CZeronodePaymentWinner
{
public:
    CTxIn vinZeronode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CZeronodePaymentWinner()
    {
        nBlockHeight = 0;
        vinZeronode = CTxIn();
        payee = CScript();
    }

    CZeronodePaymentWinner(CTxIn vinIn)
    {
        nBlockHeight = 0;
        vinZeronode = vinIn;
        payee = CScript();
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << *(CScriptBase*)(&payee);
        ss << nBlockHeight;
        ss << vinZeronode.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keyZeronode, CPubKey& pubKeyZeronode);
    bool IsValid(CNode* pnode, std::string& strError);
    bool SignatureValid();
    void Relay();

    void AddPayee(CScript payeeIn)
    {
        payee = payeeIn;
    }


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vinZeronode);
        READWRITE(nBlockHeight);
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(vchSig);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinZeronode.ToString();
        ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
        ret += ", " + EncodeDestination(payee);
        ret += ", " + boost::lexical_cast<std::string>((int)vchSig.size());
        return ret;
    }
};

//
// Zeronode Payments Class
// Keeps track of who should get paid for which blocks
//

class CZeronodePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CZeronodePaymentWinner> mapZeronodePayeeVotes;
    std::map<int, CZeronodeBlockPayees> mapZeronodeBlocks;
    std::map<uint256, int> mapZeronodesLastVote; //prevout.hash + prevout.n, nBlockHeight

    CZeronodePayments()
    {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapZeronodeBlocks, cs_mapZeronodePayeeVotes);
        mapZeronodeBlocks.clear();
        mapZeronodePayeeVotes.clear();
    }

    bool AddWinningZeronode(CZeronodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList();
    int LastPayment(CZeronode& zn);

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CZeronode& zn, int nNotBlockHeight);

    bool CanVote(COutPoint outZeronode, int nBlockHeight)
    {
        LOCK(cs_mapZeronodePayeeVotes);
        uint256 temp = ArithToUint256(UintToArith256(outZeronode.hash) + outZeronode.n);
        if(mapZeronodesLastVote.count(temp)) {
            if(mapZeronodesLastVote[temp] == nBlockHeight) {
                return false;
            }
        }

        //record this zeronode voted
        mapZeronodesLastVote[temp] = nBlockHeight;
        return true;
    }

    int GetMinZeronodePaymentsProto();
    void ProcessMessageZeronodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, CTxOut& txFounders, CTxOut& txZeronodes);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(mapZeronodePayeeVotes);
        READWRITE(mapZeronodeBlocks);
    }
};


#endif
