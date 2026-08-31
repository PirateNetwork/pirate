// Copyright (c) 2013-2014 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"
#include "rpc/client.h"

#include "assetchain.h"
#include "consensus/consensus.h"
#include "key_io.h"
#include "main.h"
#include "script/standard.h"
#include "komodo_globals.h"
#include "transaction_builder.h"
#include "wallet/wallet.h"
#include "wallet/walletmanager.h"

#include "gtest/gtestutils.h"

#include "zcash/Address.hpp"

#include "asyncrpcqueue.h"
#include "asyncrpcoperation.h"
#include "wallet/asyncrpcoperation_mergetoaddress.h"
#include "wallet/asyncrpcoperation_sendmany.h"
#include "wallet/asyncrpcoperation_shieldcoinbase.h"

#include "init.h"

#include <array>
#include <chrono>
#include <thread>

#include <fstream>
#include <unordered_set>

#include <boost/algorithm/string.hpp>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>

#include <univalue.h>

// Primary wallet-RPC test file: covers z_validateaddress, z_exportwallet,
// z_importwallet, and z_sendmany parameter validation, plus related
// wallet RPCs, spanning the transparent and Sapling pools. Sprout paths
// were intentionally dropped during the Boost.Test migration since
// Sprout is architecturally dead in this fork's wallet (e.g. z_importkey
// rejects Sprout keys outright).

using namespace std;

extern UniValue createArgs(int nRequired, const char* address1 = NULL, const char* address2 = NULL);
extern UniValue CallRPC(string args);

extern CWallet* pwalletMain;

bool find_error(const UniValue& objError, const std::string& expected) {
    return find_value(objError, "message").get_str().find(expected) != string::npos;
}

static UniValue ValueFromString(const std::string &str)
{
    UniValue value;
    EXPECT_TRUE(value.setNumStr(str));
    return value;
}

class rpc_wallet_tests_bitcoin : public BitcoinTestingSetup {};

TEST_F(rpc_wallet_tests_bitcoin, rpc_addmultisig)
{
    LOCK(pwalletMain->cs_wallet);

    rpcfn_type addmultisig = tableRPC["addmultisigaddress"]->actor;

    // old, 65-byte-long:
    const char address1Hex[] = "0434e3e09f49ea168c5bbf53f877ff4206923858aab7c7e1df25bc263978107c95e35065a27ef6f1b27222db0ec97e0e895eaca603d3ee0d4c060ce3d8a00286c8";
    // new, compressed:
    const char address2Hex[] = "0388c2037017c62240b6b72ac1a2a5f94da790596ebd06177c8572752922165cb4";

    UniValue v;
    CTxDestination address;
    EXPECT_NO_THROW(v = addmultisig(createArgs(1, address1Hex), false, CPubKey()));
    address = DecodeDestination(v.get_str());
    EXPECT_TRUE(IsValidDestination(address) && std::get_if<CScriptID>(&address) != nullptr);

    EXPECT_NO_THROW(v = addmultisig(createArgs(1, address1Hex, address2Hex), false, CPubKey()));
    address = DecodeDestination(v.get_str());
    EXPECT_TRUE(IsValidDestination(address) && std::get_if<CScriptID>(&address) != nullptr);

    EXPECT_NO_THROW(v = addmultisig(createArgs(2, address1Hex, address2Hex), false, CPubKey()));
    address = DecodeDestination(v.get_str());
    EXPECT_TRUE(IsValidDestination(address) && std::get_if<CScriptID>(&address) != nullptr);

    EXPECT_THROW(addmultisig(createArgs(0), false, CPubKey()), runtime_error);
    EXPECT_THROW(addmultisig(createArgs(1), false, CPubKey()), runtime_error);
    EXPECT_THROW(addmultisig(createArgs(2, address1Hex), false, CPubKey()), runtime_error);

    EXPECT_THROW(addmultisig(createArgs(1, ""), false, CPubKey()), runtime_error);
    EXPECT_THROW(addmultisig(createArgs(1, "NotAValidPubkey"), false, CPubKey()), runtime_error);

    string short1(address1Hex, address1Hex + sizeof(address1Hex) - 2); // last byte missing
    EXPECT_THROW(addmultisig(createArgs(2, short1.c_str()), false, CPubKey()), runtime_error);

    string short2(address1Hex + 1, address1Hex + sizeof(address1Hex)); // first byte missing
    EXPECT_THROW(addmultisig(createArgs(2, short2.c_str()), false, CPubKey()), runtime_error);
}

TEST_F(rpc_wallet_tests_bitcoin, rpc_wallet)
{
    // Test RPC calls for various wallet statistics
    UniValue r;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CPubKey demoPubkey = pwalletMain->GenerateNewKey();
    CTxDestination demoAddress(CTxDestination(demoPubkey.GetID()));
    UniValue retValue;
    string strAccount = "";
    string strPurpose = "receive";
    EXPECT_NO_THROW({ /*Initialize Wallet with an account */
        CWalletDB walletdb(pwalletMain->strWalletFile);
        CAccount account;
        account.vchPubKey = demoPubkey;
        pwalletMain->SetAddressBook(account.vchPubKey.GetID(), strAccount, strPurpose);
        walletdb.WriteAccount(strAccount, account);
    });

    CPubKey setaccountDemoPubkey = pwalletMain->GenerateNewKey();
    CTxDestination setaccountDemoAddress(CTxDestination(setaccountDemoPubkey.GetID()));

    /*********************************
     * 			setaccount
     *********************************/
    EXPECT_NO_THROW(CallRPC("setaccount " + EncodeDestination(setaccountDemoAddress) + " \"\""));
    // Accounts are a stubbed-out no-op in this fork (CWalletDB::ListAccountCreditDebit
    // is `{ return; }`), so account names are never looked up or validated - any
    // string, including a nonexistent one, is silently accepted now.
    EXPECT_NO_THROW(CallRPC("setaccount " + EncodeDestination(setaccountDemoAddress) + " nullaccount"));
    /* t1VtArtnn1dGPiD2WFfMXYXW5mHM3q1GpgV is not owned by the test wallet. */
    EXPECT_THROW(CallRPC("setaccount t1VtArtnn1dGPiD2WFfMXYXW5mHM3q1GpgV nullaccount"), runtime_error);
    EXPECT_THROW(CallRPC("setaccount"), runtime_error);
    /* t1VtArtnn1dGPiD2WFfMXYXW5mHM3q1Gpg (34 chars) is an illegal address (should be 35 chars) */
    EXPECT_THROW(CallRPC("setaccount t1VtArtnn1dGPiD2WFfMXYXW5mHM3q1Gpg nullaccount"), runtime_error);


    /*********************************
     *                  getbalance
     *********************************/
    EXPECT_NO_THROW(CallRPC("getbalance"));
    // AccountFromValue() is now a trivial passthrough (`return value.get_str();`)
    // with no deprecation-rejection check, so any non-"*" string - including an
    // address - is accepted as an "account" name (yielding a balance of 0) rather
    // than throwing.
    EXPECT_NO_THROW(CallRPC("getbalance " + EncodeDestination(demoAddress)));

    /*********************************
     * 			listunspent
     *********************************/
    EXPECT_NO_THROW(CallRPC("listunspent"));
    EXPECT_THROW(CallRPC("listunspent string"), runtime_error);
    EXPECT_THROW(CallRPC("listunspent 0 string"), runtime_error);
    EXPECT_THROW(CallRPC("listunspent 0 1 not_array"), runtime_error);
    EXPECT_THROW(CallRPC("listunspent 0 1 [] extra"), runtime_error);
    EXPECT_NO_THROW(r = CallRPC("listunspent 0 1 []"));
    EXPECT_TRUE(r.get_array().empty());

    /*********************************
     * 		listreceivedbyaddress
     *********************************/
    EXPECT_NO_THROW(CallRPC("listreceivedbyaddress"));
    EXPECT_NO_THROW(CallRPC("listreceivedbyaddress 0"));
    EXPECT_THROW(CallRPC("listreceivedbyaddress not_int"), runtime_error);
    EXPECT_THROW(CallRPC("listreceivedbyaddress 0 not_bool"), runtime_error);
    EXPECT_NO_THROW(CallRPC("listreceivedbyaddress 0 true"));
    EXPECT_THROW(CallRPC("listreceivedbyaddress 0 true extra"), runtime_error);

    /*********************************
     * 		listreceivedbyaccount
     *********************************/
    EXPECT_NO_THROW(CallRPC("listreceivedbyaccount"));
    EXPECT_NO_THROW(CallRPC("listreceivedbyaccount 0"));
    EXPECT_THROW(CallRPC("listreceivedbyaccount not_int"), runtime_error);
    EXPECT_THROW(CallRPC("listreceivedbyaccount 0 not_bool"), runtime_error);
    EXPECT_NO_THROW(CallRPC("listreceivedbyaccount 0 true"));
    EXPECT_THROW(CallRPC("listreceivedbyaccount 0 true extra"), runtime_error);

    /*********************************
     *          listsinceblock
     *********************************/
    EXPECT_NO_THROW(CallRPC("listsinceblock"));

    /*********************************
     *          listtransactions
     *********************************/
    EXPECT_NO_THROW(CallRPC("listtransactions"));
    EXPECT_NO_THROW(CallRPC("listtransactions " + EncodeDestination(demoAddress)));
    EXPECT_NO_THROW(CallRPC("listtransactions " + EncodeDestination(demoAddress) + " 20"));
    EXPECT_NO_THROW(CallRPC("listtransactions " + EncodeDestination(demoAddress) + " 20 0"));
    EXPECT_THROW(CallRPC("listtransactions " + EncodeDestination(demoAddress) + " not_int"), runtime_error);

    /*********************************
     *          listlockunspent
     *********************************/
    EXPECT_NO_THROW(CallRPC("listlockunspent"));

    /*********************************
     *          listaccounts
     *********************************/
    EXPECT_NO_THROW(CallRPC("listaccounts"));

    /*********************************
     *          listaddressgroupings
     *********************************/
    EXPECT_NO_THROW(CallRPC("listaddressgroupings"));

    /*********************************
     * 		getrawchangeaddress
     *********************************/
    EXPECT_NO_THROW(CallRPC("getrawchangeaddress"));

    /*********************************
     * 		getnewaddress
     *********************************/
    EXPECT_NO_THROW(CallRPC("getnewaddress"));
    EXPECT_NO_THROW(CallRPC("getnewaddress \"\""));
    /* Accounts are a stubbed-out no-op in this fork; see AccountFromValue() above. */
    EXPECT_NO_THROW(CallRPC("getnewaddress getnewaddress_demoaccount"));

    /*********************************
     * 		getaccountaddress
     *********************************/
    EXPECT_NO_THROW(CallRPC("getaccountaddress \"\""));
    /* Accounts are a stubbed-out no-op in this fork; a nonexistent account name
     * is silently accepted and gets a fresh address rather than being rejected. */
    EXPECT_NO_THROW(CallRPC("getaccountaddress accountThatDoesntExists"));
    EXPECT_NO_THROW(retValue = CallRPC("getaccountaddress " + strAccount));
    EXPECT_TRUE(DecodeDestination(retValue.get_str()) == demoAddress);

    /*********************************
     * 			getaccount
     *********************************/
    EXPECT_THROW(CallRPC("getaccount"), runtime_error);
    EXPECT_NO_THROW(CallRPC("getaccount " + EncodeDestination(demoAddress)));

    /*********************************
     * 	signmessage + verifymessage
     *********************************/
    EXPECT_NO_THROW(retValue = CallRPC("signmessage " + EncodeDestination(demoAddress) + " mymessage"));
    EXPECT_THROW(CallRPC("signmessage"), runtime_error);
    /* Should throw error because this address is not loaded in the wallet */
    EXPECT_THROW(CallRPC("signmessage t1h8SqgtM3QM5e2M8EzhhT1yL2PXXtA6oqe mymessage"), runtime_error);

    /* missing arguments */
    EXPECT_THROW(CallRPC("verifymessage " + EncodeDestination(demoAddress)), runtime_error);
    EXPECT_THROW(CallRPC("verifymessage " + EncodeDestination(demoAddress) + " " + retValue.get_str()), runtime_error);
    /* Illegal address */
    EXPECT_THROW(CallRPC("verifymessage t1VtArtnn1dGPiD2WFfMXYXW5mHM3q1Gpg " + retValue.get_str() + " mymessage"), runtime_error);
    /* wrong address - a validly-formatted address under this fork's real prefix
     * that's simply a different key than demoAddress, rather than a foreign
     * vanilla-Zcash-mainnet literal (this fork's real MAIN prefix doesn't decode
     * that address at all, making verifymessage throw instead of returning false) */
    CTxDestination wrongAddress(CTxDestination(pwalletMain->GenerateNewKey().GetID()));
    EXPECT_TRUE(CallRPC("verifymessage " + EncodeDestination(wrongAddress) + " " + retValue.get_str() + " mymessage").get_bool() == false);
    /* Correct address and signature but wrong message */
    EXPECT_TRUE(CallRPC("verifymessage " + EncodeDestination(demoAddress) + " " + retValue.get_str() + " wrongmessage").get_bool() == false);
    /* Correct address, message and signature*/
    EXPECT_TRUE(CallRPC("verifymessage " + EncodeDestination(demoAddress) + " " + retValue.get_str() + " mymessage").get_bool() == true);

    /*********************************
     * 		getaddressesbyaccount
     *********************************/
    EXPECT_THROW(CallRPC("getaddressesbyaccount"), runtime_error);
    EXPECT_NO_THROW(retValue = CallRPC("getaddressesbyaccount " + strAccount));
    UniValue arr = retValue.get_array();
    // 3, not 4: getnewaddress still tags each new address with whatever account
    // name is passed (AccountFromValue is a passthrough, not forced to ""), so
    // the earlier "getnewaddress getnewaddress_demoaccount" call (now expected
    // to succeed rather than throw, since account-name validation is gone) landed
    // under account "getnewaddress_demoaccount", not "". Only demoAddress plus
    // the two explicit ""/no-account getnewaddress calls land under "".
    EXPECT_EQ(3, arr.size());
    bool notFound = true;
    for (auto a : arr.getValues()) {
        // CNoDestination only defines operator==, not operator!= (no C++20
        // rewritten-candidate lookup here), so std::variant's != can't be
        // synthesized for CTxDestination - negate == instead.
        notFound &= !(DecodeDestination(a.get_str()) == demoAddress);
    }
    EXPECT_TRUE(!notFound);

    /*********************************
     * 	     fundrawtransaction
     *********************************/
    EXPECT_THROW(CallRPC("fundrawtransaction 28z"), runtime_error);
    EXPECT_THROW(CallRPC("fundrawtransaction 01000000000180969800000000001976a91450ce0a4b0ee0ddeb633da85199728b940ac3fe9488ac00000000"), runtime_error);

    /*
     * getblocksubsidy
     *
     * The original test assumed a Zcash-style halving schedule with a
     * separate "founders" field. GetBlockSubsidy() in this fork is Komodo-
     * style (see main.cpp): for a KMD-identified chain (chainName.isKMD(),
     * true by default here since no komodo_args() ran) it's a flat 3*COIN
     * for every height below nS8HardforkHeight (4,125,988) - no halving at
     * these heights. The RPC's founders-style split is also reported under
     * "ac_pubkey", not "founders", and is only populated at all when
     * ASSETCHAINS_OVERRIDE_PUBKEY/ASSETCHAINS_SCRIPTPUB are configured
     * (neither is, here), so "founders" is never present in the response.
     */
    EXPECT_THROW(CallRPC("getblocksubsidy too many args"), runtime_error);
    EXPECT_THROW(CallRPC("getblocksubsidy -1"), runtime_error);
    EXPECT_NO_THROW(retValue = CallRPC("getblocksubsidy 50000"));
    UniValue obj = retValue.get_obj();
    EXPECT_EQ(find_value(obj, "miner").get_real(), 3.0);
    EXPECT_TRUE(find_value(obj, "founders").isNull());
    EXPECT_NO_THROW(retValue = CallRPC("getblocksubsidy 1000000"));
    obj = retValue.get_obj();
    EXPECT_EQ(find_value(obj, "miner").get_real(), 3.0);
    EXPECT_TRUE(find_value(obj, "founders").isNull());
    EXPECT_NO_THROW(retValue = CallRPC("getblocksubsidy 2000000"));
    obj = retValue.get_obj();
    EXPECT_EQ(find_value(obj, "miner").get_real(), 3.0);
    EXPECT_TRUE(find_value(obj, "founders").isNull());

    /*
     * getblock
     */
    EXPECT_THROW(CallRPC("getblock too many args"), runtime_error);
    EXPECT_THROW(CallRPC("getblock -1"), runtime_error);
    EXPECT_THROW(CallRPC("getblock 2147483647"), runtime_error); // allowed, but > height of active chain tip
    EXPECT_THROW(CallRPC("getblock 2147483648"), runtime_error); // not allowed, > int32 used for nHeight
    EXPECT_THROW(CallRPC("getblock 100badchars"), runtime_error);
    EXPECT_NO_THROW(CallRPC("getblock 0"));
    EXPECT_NO_THROW(CallRPC("getblock 0 0"));
    EXPECT_NO_THROW(CallRPC("getblock 0 1"));
    EXPECT_NO_THROW(CallRPC("getblock 0 2"));
    EXPECT_THROW(CallRPC("getblock 0 -1"), runtime_error); // bad verbosity
    EXPECT_THROW(CallRPC("getblock 0 3"), runtime_error); // bad verbosity
}

TEST_F(rpc_wallet_tests_bitcoin, rpc_wallet_getbalance)
{
    SelectParams(CBaseChainParams::TESTNET);

    LOCK(pwalletMain->cs_wallet);


    // "tmC6YZnCUhm19dEXxh3Jb7srdBJxDawaCab" was a vanilla-Zcash-testnet literal
    // that doesn't decode under this fork's real testnet prefix; use a freshly
    // generated address instead for the EXPECT_NO_THROW cases (the EXPECT_THROW
    // ones below still throw regardless of which reason - decode failure or
    // otherwise - so the literal is harmless there).
    CTxDestination realTestnetAddr(CTxDestination(pwalletMain->GenerateNewKey().GetID()));
    std::string realTestnetAddrStr = EncodeDestination(realTestnetAddr);
    EXPECT_THROW(CallRPC("z_getbalance too many args"), runtime_error);
    EXPECT_THROW(CallRPC("z_getbalance invalidaddress"), runtime_error);
    EXPECT_NO_THROW(CallRPC("z_getbalance " + realTestnetAddrStr));
    EXPECT_THROW(CallRPC("z_getbalance tmC6YZnCUhm19dEXxh3Jb7srdBJxDawaCab -1"), runtime_error);
    EXPECT_NO_THROW(CallRPC("z_getbalance " + realTestnetAddrStr + " 0"));
    EXPECT_THROW(CallRPC("z_getbalance tnRZ8bPq2pff3xBWhTJhNkVUkm2uhzksDeW5PvEa7aFKGT9Qi3YgTALZfjaY4jU3HLVKBtHdSXxoPoLA3naMPcHBcY88FcF 1"), runtime_error);


    EXPECT_THROW(CallRPC("z_gettotalbalance too manyargs"), runtime_error);
    EXPECT_THROW(CallRPC("z_gettotalbalance -1"), runtime_error);
    EXPECT_NO_THROW(CallRPC("z_gettotalbalance 0"));


    EXPECT_THROW(CallRPC("z_listreceivedbyaddress too many args"), runtime_error);
    // negative minconf not allowed
    EXPECT_THROW(CallRPC("z_listreceivedbyaddress tmC6YZnCUhm19dEXxh3Jb7srdBJxDawaCab -1"), runtime_error);
    // invalid zaddr, taddr not allowed
    EXPECT_THROW(CallRPC("z_listreceivedbyaddress tmC6YZnCUhm19dEXxh3Jb7srdBJxDawaCab 0"), runtime_error);
    // don't have the spending key
    EXPECT_THROW(CallRPC("z_listreceivedbyaddress tnRZ8bPq2pff3xBWhTJhNkVUkm2uhzksDeW5PvEa7aFKGT9Qi3YgTALZfjaY4jU3HLVKBtHdSXxoPoLA3naMPcHBcY88FcF 1"), runtime_error);
}

/**
 * This test covers RPC command z_validateaddress
 */
TEST_F(rpc_wallet_tests_bitcoin, rpc_wallet_z_validateaddress)
{
    SelectParams(CBaseChainParams::MAIN);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue retValue;

    // Check number of args
    EXPECT_THROW(CallRPC("z_validateaddress"), runtime_error);
    EXPECT_THROW(CallRPC("z_validateaddress toomany args"), runtime_error);

    // Wallet should be empty
    std::set<libzcash::SproutPaymentAddress> addrs;
    pwalletMain->GetSproutPaymentAddresses(addrs);
    EXPECT_TRUE(addrs.size()==0);

    // This address is not valid, it belongs to another network
    EXPECT_NO_THROW(retValue = CallRPC("z_validateaddress ztaaga95QAPyp1kSQ1hD2kguCpzyMHjxWZqaYDEkzbvo7uYQYAw2S8X4Kx98AvhhofMtQL8PAXKHuZsmhRcanavKRKmdCzk"));
    UniValue resultObj = retValue.get_obj();
    bool b = find_value(resultObj, "isvalid").get_bool();
    EXPECT_EQ(b, false);

    // The original two Sprout sub-cases here ("valid address, key not in
    // wallet" and "valid address, key imported and owned") are deleted: this
    // fork's IsValidAddressForNetwork visitor (zcash/Address.cpp) hardcodes
    // `operator()(const SproutPaymentAddress&) { return false; }`
    // unconditionally, and z_importkey explicitly rejects any Sprout spending
    // key with "Invalid spending key, Sprout not supported". No Sprout
    // address can ever validate as isvalid=true anymore, regardless of how
    // it was generated - the "belongs to another network" case just above
    // already covers "a well-formed Sprout address reports isvalid=false".

    // This Sapling address is not valid, it belongs to another network
    EXPECT_NO_THROW(retValue = CallRPC("z_validateaddress ztestsapling1knww2nyjc62njkard0jmx7hlsj6twxmxwprn7anvrv4dc2zxanl3nemc0qx2hvplxmd2uau8gyw"));
    resultObj = retValue.get_obj();
    b = find_value(resultObj, "isvalid").get_bool();
    EXPECT_EQ(b, false);

    // This Sapling address is valid, but the spending key is not in this wallet
    EXPECT_NO_THROW(retValue = CallRPC("z_validateaddress zs1z7rejlpsa98s2rrrfkwmaxu53e4ue0ulcrw0h4x5g8jl04tak0d3mm47vdtahatqrlkngh9slya"));
    resultObj = retValue.get_obj();
    b = find_value(resultObj, "isvalid").get_bool();
    EXPECT_EQ(b, true);
    EXPECT_EQ(find_value(resultObj, "type").get_str(), "sapling");
    b = find_value(resultObj, "ismine").get_bool();
    EXPECT_EQ(b, false);
    EXPECT_EQ(find_value(resultObj, "diversifier").get_str(), "1787997c30e94f050c634d");
    EXPECT_EQ(find_value(resultObj, "diversifiedtransmissionkey").get_str(), "34ed1f60f5db5763beee1ddbb37dd5f7e541d4d4fbdcc09fbfcc6b8e949bbe9d");

    // Ironwood counterpart: DescribePaymentAddressVisitor (rpc/misc.cpp)
    // branches on IronwoodPaymentAddress the same way it does on
    // SaplingPaymentAddress above, but nothing exercised that branch. Since
    // there's no pre-existing valid Ironwood literal to reuse for the
    // not-in-wallet case, generate a real one and confirm ismine=true instead.
    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }
    std::string ironwoodAddr = EncodePaymentAddress(pwalletMain->GenerateNewIronwoodZKey());
    EXPECT_NO_THROW(retValue = CallRPC("z_validateaddress " + ironwoodAddr));
    resultObj = retValue.get_obj();
    b = find_value(resultObj, "isvalid").get_bool();
    EXPECT_EQ(b, true);
    EXPECT_EQ(find_value(resultObj, "type").get_str(), "ironwood");
    b = find_value(resultObj, "ismine").get_bool();
    EXPECT_EQ(b, true);
    EXPECT_EQ(find_value(resultObj, "diversifier").get_str().size(), 22u);
    EXPECT_EQ(find_value(resultObj, "diversifiedtransmissionkey").get_str().size(), 64u);
}

/*
 * This test covers RPC command z_exportwallet
 */
TEST_F(rpc_wallet_tests_bitcoin, rpc_wallet_z_exportwallet)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    // z_exportwallet/dumpwallet_impl no longer writes Sprout keys at all (only
    // transparent and Sapling), so this test now exercises a Sapling key
    // instead of the original Sprout one - the RPC mechanism being tested
    // (does the exported file actually contain the addr+key pair) is
    // unaffected by which shielded pool the key belongs to.
    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }

    // wallet should be empty
    std::set<libzcash::SaplingPaymentAddress> addrs;
    pwalletMain->GetSaplingPaymentAddresses(addrs);
    EXPECT_TRUE(addrs.size()==0);

    // wallet should have one key - but GenerateNewSaplingZKey() also registers
    // an internal/change address alongside the external one it returns (see
    // CBasicKeyStore::AddSaplingExtendedFullViewingKey), so the keystore now
    // reports 2 addresses for a single generated key, not 1.
    libzcash::SaplingPaymentAddress addr = pwalletMain->GenerateNewSaplingZKey();
    pwalletMain->GetSaplingPaymentAddresses(addrs);
    EXPECT_TRUE(addrs.size()==2);

    // Set up paths
    boost::filesystem::path tmppath = boost::filesystem::temp_directory_path();
    boost::filesystem::path tmpfilename = boost::filesystem::unique_path("%%%%%%%%");
    boost::filesystem::path exportfilepath = tmppath / tmpfilename;

    // export will fail since exportdir is not set
    EXPECT_THROW(CallRPC(string("z_exportwallet ") + tmpfilename.string()), runtime_error);

    // set exportdir
    mapArgs["-exportdir"] = tmppath.string();

    // run some tests
    EXPECT_THROW(CallRPC("z_exportwallet"), runtime_error);

    EXPECT_THROW(CallRPC("z_exportwallet toomany args"), runtime_error);

    EXPECT_THROW(CallRPC(string("z_exportwallet invalid!*/_chars.txt")), runtime_error);

    EXPECT_NO_THROW(CallRPC(string("z_exportwallet ") + tmpfilename.string()));


    auto key = std::visit(GetSpendingKeyForPaymentAddress(pwalletMain), libzcash::PaymentAddress(addr));
    ASSERT_TRUE(key.has_value());

    std::string s1 = EncodePaymentAddress(addr);
    std::string s2 = EncodeSpendingKey(key.value());

    // There's no way to really delete a private key so we will read in the
    // exported wallet file and search for the spending key and payment address.

    EnsureWalletIsUnlocked();

    ifstream file;
    file.open(exportfilepath.string().c_str(), std::ios::in | std::ios::ate);
    EXPECT_TRUE(file.is_open());
    bool fVerified = false;
    int64_t nFilesize = std::max((int64_t)1, (int64_t)file.tellg());
    file.seekg(0, file.beg);
    while (file.good()) {
        std::string line;
        std::getline(file, line);
        if (line.empty() || line[0] == '#')
            continue;
        if (line.find(s1) != std::string::npos && line.find(s2) != std::string::npos) {
            fVerified = true;
            break;
        }
    }
    EXPECT_TRUE(fVerified);

    // mapArgs is a process-wide global; leaving "-exportdir" set here would
    // leak into every later test in the binary.
    mapArgs.erase("-exportdir");
}


/*
 * This test covers RPC command z_importwallet
 */
TEST_F(rpc_wallet_tests_bitcoin, rpc_wallet_z_importwallet)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    // error if no args
    EXPECT_THROW(CallRPC("z_importwallet"), runtime_error);

    // error if too many args
    EXPECT_THROW(CallRPC("z_importwallet toomany args"), runtime_error);

    // create a random key locally. This fork's z_importwallet/dumpwallet_impl
    // no longer round-trip Sprout keys at all (dumpwallet_impl only ever
    // writes transparent and Sapling entries, and IsValidPaymentAddress()
    // unconditionally rejects Sprout addresses), so this test now exercises a
    // Sapling key instead - the file-import parsing logic being tested is the
    // same regardless of which shielded pool the key belongs to. Sapling
    // extended spending keys are HD-derived rather than independently random,
    // so build one from a throwaway random seed instead of a ::random() factory.
    HDSeed testSeed = HDSeed::Random();
    auto testSpendingKey = libzcash::SaplingExtendedSpendingKey::Master(testSeed, false);
    auto testPaymentAddress = testSpendingKey.DefaultAddress();
    std::string testAddr = EncodePaymentAddress(testPaymentAddress);
    std::string testKey = EncodeSpendingKey(testSpendingKey);

    // create test data using the random key
    std::string format_str = "# Wallet dump created by Komodo v0.11.2.0.z8-9155cc6-dirty (2016-08-11 11:37:00 -0700)\n"
            "# * Created on 2016-08-12T21:55:36Z\n"
            "# * Best block at time of backup was 0 (0de0a3851fef2d433b9b4f51d4342bdd24c5ddd793eb8fba57189f07e9235d52),\n"
            "#   mined on 2009-01-03T18:15:05Z\n"
            "\n"
            "# Zkeys\n"
            "\n"
            // importwallet_impl's Sapling-key branch requires exactly 7 or 9
            // space-separated fields (KEY TIME # zaddr= ADDR label= LABEL),
            // matching dumpwallet_impl's real "%s %s # zaddr= %s label= %s\n"
            // output - the original Sprout-style "# zaddr=%s" (4 fields) no
            // longer parses.
            "%s 2016-08-12T21:55:36Z # zaddr= %s label= test\n"
            "\n"
            "\n# End of dump";

    boost::format formatobject(format_str);
    std::string testWalletDump = (formatobject % testKey % testAddr).str();

    // write test data to file
    boost::filesystem::path temp = boost::filesystem::temp_directory_path() /
            boost::filesystem::unique_path();
    const std::string path = temp.string();
    std::ofstream file(path);
    file << testWalletDump;
    file << std::flush;

    // wallet should currently be empty
    std::set<libzcash::SaplingPaymentAddress> addrs;
    pwalletMain->GetSaplingPaymentAddresses(addrs);
    EXPECT_TRUE(addrs.size()==0);

    // import test data from file into wallet
    EXPECT_NO_THROW(CallRPC(string("z_importwallet ") + path));

    // wallet should now have one zkey (plus its auto-registered internal/
    // change address - see the "2, not 1" note in rpc_wallet_z_exportwallet)
    pwalletMain->GetSaplingPaymentAddresses(addrs);
    EXPECT_TRUE(addrs.size()==2);

    // check that we have the spending key for the address
    auto address = DecodePaymentAddress(testAddr);
    EXPECT_TRUE(IsValidPaymentAddress(address));
    ASSERT_TRUE(std::get_if<libzcash::SaplingPaymentAddress>(&address) != nullptr);
    auto addr = std::get_if<libzcash::SaplingPaymentAddress>(&address);
    auto k = std::visit(GetSpendingKeyForPaymentAddress(pwalletMain), address);
    EXPECT_TRUE(k.has_value());

    // Verify the spending key is the same as the test data
    EXPECT_EQ(testKey, EncodeSpendingKey(k.value()));
}


/*
 * This test covers RPC commands z_listaddresses, z_importkey, z_exportkey
 */
TEST_F(rpc_wallet_tests_bitcoin, rpc_wallet_z_importexport)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    UniValue retValue;
    int n1 = 1000; // number of times to import/export
    int n2 = 1000; // number of addresses to create and list

    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }

    // error if no args
    EXPECT_THROW(CallRPC("z_importkey"), runtime_error);
    EXPECT_THROW(CallRPC("z_exportkey"), runtime_error);

    // error if too many args
    EXPECT_THROW(CallRPC("z_importkey way too many args"), runtime_error);
    EXPECT_THROW(CallRPC("z_exportkey toomany args"), runtime_error);

    // error if invalid args. The original literal used a Sprout key to probe
    // z_importkey's numeric-arg validation; a Sapling key exercises the same
    // argument-parsing path (Sprout keys are rejected by z_importkey outright,
    // before argument validation is even reached - see z_validateaddress above).
    std::vector<unsigned char, secure_allocator<unsigned char>> probeRawSeed(32);
    HDSeed probeSeed(probeRawSeed);
    auto sk = libzcash::SaplingExtendedSpendingKey::Master(probeSeed).Derive(0 | HARDENED_KEY_LIMIT);
    std::string prefix = std::string("z_importkey ") + EncodeSpendingKey(sk) + " yes ";
    EXPECT_THROW(CallRPC(prefix + "-1"), runtime_error);
    EXPECT_THROW(CallRPC(prefix + "2147483647"), runtime_error); // allowed, but > height of active chain tip
    EXPECT_THROW(CallRPC(prefix + "2147483648"), runtime_error); // not allowed, > int32 used for nHeight
    EXPECT_THROW(CallRPC(prefix + "100badchars"), runtime_error);

    // wallet should currently be empty
    std::set<libzcash::SaplingPaymentAddress> saplingAddrs;
    pwalletMain->GetSaplingPaymentAddresses(saplingAddrs);
    EXPECT_TRUE(saplingAddrs.empty());

    std::vector<unsigned char, secure_allocator<unsigned char>> rawSeed(32);
    HDSeed seed(rawSeed);
    auto m = libzcash::SaplingExtendedSpendingKey::Master(seed);

    // verify import and export key. The original loop also exercised a
    // parallel Sprout key each iteration; Sprout import/creation is rejected
    // outright by this fork (see z_validateaddress above), so only the
    // Sapling half remains.
    for (int i = 0; i < n1; i++) {
        auto testSaplingSpendingKey = m.Derive(i | HARDENED_KEY_LIMIT);
        auto testSaplingPaymentAddress = testSaplingSpendingKey.DefaultAddress();
        std::string testSaplingAddr = EncodePaymentAddress(testSaplingPaymentAddress);
        std::string testSaplingKey = EncodeSpendingKey(testSaplingSpendingKey);
        EXPECT_NO_THROW(CallRPC(string("z_importkey ") + testSaplingKey));
        EXPECT_NO_THROW(retValue = CallRPC(string("z_exportkey ") + testSaplingAddr));
        EXPECT_EQ(retValue.get_str(), testSaplingKey);
    }

    // Verify we can list the keys imported. Each imported Sapling extsk also
    // auto-registers an internal/change address (see the "2, not 1" note in
    // rpc_wallet_z_exportwallet), so each iteration contributes 2 addresses.
    EXPECT_NO_THROW(retValue = CallRPC("z_listaddresses"));
    UniValue arr = retValue.get_array();
    EXPECT_TRUE(arr.size() == (2 * n1));

    // Put addresses into a set
    std::unordered_set<std::string> myaddrs;
    for (UniValue element : arr.getValues()) {
        myaddrs.insert(element.get_str());
    }

    // Make new addresses for the set (2 per generated key: external + internal)
    for (int i=0; i<n2; i++) {
        pwalletMain->GenerateNewSaplingZKey();
    }
    pwalletMain->GetSaplingPaymentAddresses(saplingAddrs);
    for (const auto& a : saplingAddrs) {
        myaddrs.insert(EncodePaymentAddress(a));
    }

    // Verify number of addresses stored in wallet
    int numAddrs = myaddrs.size();
    EXPECT_TRUE(numAddrs == (2 * n1) + (2 * n2));
    pwalletMain->GetSaplingPaymentAddresses(saplingAddrs);
    EXPECT_TRUE(saplingAddrs.size() == numAddrs);

    // Ask wallet to list addresses
    EXPECT_NO_THROW(retValue = CallRPC("z_listaddresses"));
    arr = retValue.get_array();
    EXPECT_TRUE(arr.size() == numAddrs);

    // Create a set from them
    std::unordered_set<std::string> listaddrs;
    for (UniValue element : arr.getValues()) {
        listaddrs.insert(element.get_str());
    }

    // Verify the two sets of addresses are the same
    EXPECT_TRUE(listaddrs.size() == numAddrs);
    EXPECT_TRUE(myaddrs == listaddrs);

    // Add one more address. z_getnewaddress always creates a Sapling address
    // now (Sprout is no longer an option - see z_validateaddress above).
    // UpdateNetworkUpgradeParameters() always mutates the REGTEST params
    // singleton specifically, regardless of which network is currently
    // selected (chainparams.cpp:670) - switch to it explicitly, since this
    // fixture otherwise defaults to MAIN, where Sapling never auto-activates.
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    EXPECT_NO_THROW(retValue = CallRPC("z_getnewaddress"));
    std::string newaddress = retValue.get_str();
    auto address = DecodePaymentAddress(newaddress);
    EXPECT_TRUE(IsValidPaymentAddress(address));
    ASSERT_TRUE(std::get_if<libzcash::SaplingPaymentAddress>(&address) != nullptr);
    auto k = std::visit(GetSpendingKeyForPaymentAddress(pwalletMain), address);
    EXPECT_TRUE(k.has_value());
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    SelectParams(CBaseChainParams::MAIN);

    // Check if too many args
    EXPECT_THROW(CallRPC("z_getnewaddress toomanyargs"), runtime_error);
}



/**
 * Test Async RPC operations.
 * Tip: Create mock operations by subclassing AsyncRPCOperation.
 */

class MockSleepOperation : public AsyncRPCOperation {
public:
    std::chrono::milliseconds naptime;
    MockSleepOperation(int t=1000) {
        this->naptime = std::chrono::milliseconds(t);
    }
    virtual ~MockSleepOperation() {
    }
    virtual void main() {
        set_state(OperationStatus::EXECUTING);
        start_execution_clock();
        std::this_thread::sleep_for(std::chrono::milliseconds(naptime));
        stop_execution_clock();
        set_result(UniValue(UniValue::VSTR, "done"));
        set_state(OperationStatus::SUCCESS);
    }
};


/*
 * Test Aysnc RPC queue and operations.
 */
TEST_F(rpc_wallet_tests_bitcoin, rpc_wallet_async_operations)
{
    std::shared_ptr<AsyncRPCQueue> q = std::make_shared<AsyncRPCQueue>();
    EXPECT_TRUE(q->getNumberOfWorkers() == 0);
    std::vector<AsyncRPCOperationId> ids = q->getAllOperationIds();
    EXPECT_TRUE(ids.size()==0);

    std::shared_ptr<AsyncRPCOperation> op1 = std::make_shared<AsyncRPCOperation>();
    q->addOperation(op1);
    EXPECT_TRUE(q->getOperationCount() == 1);

    OperationStatus status = op1->getState();
    EXPECT_TRUE(status == OperationStatus::READY);

    AsyncRPCOperationId id1 = op1->getId();
    int64_t creationTime1 = op1->getCreationTime();

    q->addWorker();
    EXPECT_TRUE(q->getNumberOfWorkers() == 1);

    // an AsyncRPCOperation doesn't do anything so will finish immediately
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_TRUE(q->getOperationCount() == 0);

    // operation should be a success
    EXPECT_EQ(op1->isCancelled(), false);
    EXPECT_EQ(op1->isExecuting(), false);
    EXPECT_EQ(op1->isReady(), false);
    EXPECT_EQ(op1->isFailed(), false);
    EXPECT_EQ(op1->isSuccess(), true);
    EXPECT_EQ(op1->getError().isNull(), true);
    EXPECT_EQ(op1->getResult().isNull(), false);
    EXPECT_EQ(op1->getStateAsString(), "success");
    EXPECT_NE(op1->getStateAsString(), "executing");

    // Create a second operation which just sleeps
    std::shared_ptr<AsyncRPCOperation> op2(new MockSleepOperation(2500));
    AsyncRPCOperationId id2 = op2->getId();
    int64_t creationTime2 = op2->getCreationTime();

    // it's different from the previous operation
    EXPECT_NE(op1.get(), op2.get());
    EXPECT_NE(id1, id2);
    EXPECT_NE(creationTime1, creationTime2);

    // Only the first operation has been added to the queue
    std::vector<AsyncRPCOperationId> v = q->getAllOperationIds();
    std::set<AsyncRPCOperationId> opids(v.begin(), v.end());
    EXPECT_TRUE(opids.size() == 1);
    EXPECT_TRUE(opids.count(id1)==1);
    EXPECT_TRUE(opids.count(id2)==0);
    std::shared_ptr<AsyncRPCOperation> p1 = q->getOperationForId(id1);
    EXPECT_EQ(p1.get(), op1.get());
    std::shared_ptr<AsyncRPCOperation> p2 = q->getOperationForId(id2);
    EXPECT_TRUE(!p2); // null ptr as not added to queue yet

    // Add operation 2 and 3 to the queue
    q->addOperation(op2);
    std::shared_ptr<AsyncRPCOperation> op3(new MockSleepOperation(1000));
    q->addOperation(op3);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(op2->isExecuting(), true);
    op2->cancel();  // too late, already executing
    op3->cancel();
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    EXPECT_EQ(op2->isSuccess(), true);
    EXPECT_EQ(op2->isCancelled(), false);
    EXPECT_EQ(op3->isCancelled(), true);


    v = q->getAllOperationIds();
    std::copy( v.begin(), v.end(), std::inserter( opids, opids.end() ) );
    EXPECT_TRUE(opids.size() == 3);
    EXPECT_TRUE(opids.count(id1)==1);
    EXPECT_TRUE(opids.count(id2)==1);
    EXPECT_TRUE(opids.count(op3->getId())==1);
    q->finishAndWait();
}


// The CountOperation will increment this global
std::atomic<int64_t> gCounter(0);

class CountOperation : public AsyncRPCOperation {
public:
    CountOperation() {}
    virtual ~CountOperation() {}
    virtual void main() {
        set_state(OperationStatus::EXECUTING);
        gCounter++;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        set_state(OperationStatus::SUCCESS);
    }
};

// This tests the queue waiting for multiple workers to finish
TEST_F(rpc_wallet_tests_bitcoin, rpc_wallet_async_operations_parallel_wait)
{
    gCounter = 0;

    std::shared_ptr<AsyncRPCQueue> q = std::make_shared<AsyncRPCQueue>();
    q->addWorker();
    q->addWorker();
    q->addWorker();
    q->addWorker();
    EXPECT_TRUE(q->getNumberOfWorkers() == 4);

    int64_t numOperations = 10;     // 10 * 1000ms / 4 = 2.5 secs to finish
    for (int i=0; i<numOperations; i++) {
        std::shared_ptr<AsyncRPCOperation> op(new CountOperation());
        q->addOperation(op);
    }

    std::vector<AsyncRPCOperationId> ids = q->getAllOperationIds();
    EXPECT_TRUE(ids.size()==numOperations);
    q->finishAndWait();
    EXPECT_EQ(q->isFinishing(), true);
    EXPECT_EQ(numOperations, gCounter.load());
}

// This tests the queue shutting down immediately
TEST_F(rpc_wallet_tests_bitcoin, rpc_wallet_async_operations_parallel_cancel)
{
    gCounter = 0;

    std::shared_ptr<AsyncRPCQueue> q = std::make_shared<AsyncRPCQueue>();
    q->addWorker();
    q->addWorker();
    EXPECT_TRUE(q->getNumberOfWorkers() == 2);

    int numOperations = 10000;  // 10000 seconds to complete
    for (int i=0; i<numOperations; i++) {
        std::shared_ptr<AsyncRPCOperation> op(new CountOperation());
        q->addOperation(op);
    }
    std::vector<AsyncRPCOperationId> ids = q->getAllOperationIds();
    EXPECT_TRUE(ids.size()==numOperations);
    q->closeAndWait();

    int numSuccess = 0;
    int numCancelled = 0;
    for (auto & id : ids) {
        std::shared_ptr<AsyncRPCOperation> ptr = q->popOperationForId(id);
        if (ptr->isCancelled()) {
            numCancelled++;
        } else if (ptr->isSuccess()) {
            numSuccess++;
        }
    }

    EXPECT_EQ(numOperations, numSuccess+numCancelled);
    EXPECT_EQ(gCounter.load(), numSuccess);
    EXPECT_TRUE(q->getOperationCount() == 0);
    ids = q->getAllOperationIds();
    EXPECT_TRUE(ids.size()==0);
}

// This tests z_getoperationstatus, z_getoperationresult, z_listoperationids
TEST_F(rpc_wallet_tests_bitcoin, rpc_z_getoperations)
{
    std::shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    std::shared_ptr<AsyncRPCQueue> sharedInstance = AsyncRPCQueue::sharedInstance();
    EXPECT_TRUE(q == sharedInstance);

    EXPECT_NO_THROW(CallRPC("z_getoperationstatus"));
    EXPECT_NO_THROW(CallRPC("z_getoperationstatus []"));
    EXPECT_NO_THROW(CallRPC("z_getoperationstatus [\"opid-1234\"]"));
    EXPECT_THROW(CallRPC("z_getoperationstatus [] toomanyargs"), runtime_error);
    EXPECT_THROW(CallRPC("z_getoperationstatus not_an_array"), runtime_error);

    EXPECT_NO_THROW(CallRPC("z_getoperationresult"));
    EXPECT_NO_THROW(CallRPC("z_getoperationresult []"));
    EXPECT_NO_THROW(CallRPC("z_getoperationresult [\"opid-1234\"]"));
    EXPECT_THROW(CallRPC("z_getoperationresult [] toomanyargs"), runtime_error);
    EXPECT_THROW(CallRPC("z_getoperationresult not_an_array"), runtime_error);

    // getAsyncRPCQueue() is a process-wide singleton shared with every other
    // test in this binary (e.g. MultiWalletDispatchTest.
    // AsyncOperationStatusIsScopedToTheRequestingWallet, in test_httprpc.cpp,
    // also queues operations against it), so its pending-operation and
    // worker counts are not guaranteed to start at 0 here -- assert deltas
    // relative to whatever was already there, not absolute values.
    size_t operationCountBefore = q->getOperationCount();
    size_t workerCountBefore = q->getNumberOfWorkers();

    std::shared_ptr<AsyncRPCOperation> op1 = std::make_shared<AsyncRPCOperation>();
    q->addOperation(op1);
    std::shared_ptr<AsyncRPCOperation> op2 = std::make_shared<AsyncRPCOperation>();
    q->addOperation(op2);

    EXPECT_TRUE(q->getOperationCount() == operationCountBefore + 2);
    q->addWorker();
    EXPECT_TRUE(q->getNumberOfWorkers() == workerCountBefore + 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    EXPECT_TRUE(q->getOperationCount() == 0);

    // Check if too many args
    EXPECT_THROW(CallRPC("z_listoperationids toomany args"), runtime_error);

    UniValue retValue;
    EXPECT_NO_THROW(retValue = CallRPC("z_listoperationids"));
    EXPECT_TRUE(retValue.get_array().size() == 2);

    EXPECT_NO_THROW(retValue = CallRPC("z_getoperationstatus"));
    UniValue array = retValue.get_array();
    EXPECT_TRUE(array.size() == 2);

    // idempotent
    EXPECT_NO_THROW(retValue = CallRPC("z_getoperationstatus"));
    array = retValue.get_array();
    EXPECT_TRUE(array.size() == 2);

    for (UniValue v : array.getValues()) {
        UniValue obj = v.get_obj();
        UniValue id = find_value(obj, "id");

        UniValue result;
        // removes result from internal storage
        EXPECT_NO_THROW(result = CallRPC("z_getoperationresult [\"" + id.get_str() + "\"]"));
        UniValue resultArray = result.get_array();
        EXPECT_TRUE(resultArray.size() == 1);

        UniValue resultObj = resultArray[0].get_obj();
        UniValue resultId = find_value(resultObj, "id");
        EXPECT_EQ(id.get_str(), resultId.get_str());

        // verify the operation has been removed
        EXPECT_NO_THROW(result = CallRPC("z_getoperationresult [\"" + id.get_str() + "\"]"));
        resultArray = result.get_array();
        EXPECT_TRUE(resultArray.size() == 0);
    }

    // operations removed
    EXPECT_NO_THROW(retValue = CallRPC("z_getoperationstatus"));
    array = retValue.get_array();
    EXPECT_TRUE(array.size() == 0);

    // Deliberately no q->close() here: q is getAsyncRPCQueue(), a process-wide
    // singleton (AsyncRPCQueue::sharedInstance()) shared with every other test
    // in this binary, not something this test owns -- close() sets a one-way
    // flag with no public "reopen," so calling it here would permanently
    // disable async operations for whatever test happens to run afterward
    // (e.g. MultiWalletDispatchTest.AsyncOperationStatusIsScopedToTheRequestingWallet
    // in test_httprpc.cpp, which needs to addOperation() against this same
    // singleton). This test's own assertions are already complete by this
    // point without it.
    //
    // Trade-off, not fully resolved: this also means the worker added above
    // now survives to process exit instead of exiting via close(). Harmless
    // under this binary's normal (non-repeated, non-shuffled) run order --
    // nothing after this point in the default run order queues against the
    // singleton except the test named above, which only checks membership
    // of its own ids -- but a `--gtest_repeat`/`--gtest_shuffle` run could
    // race this test's own delta-based operationCount assertion above
    // against that same now-persistent worker draining the queue
    // concurrently, and any future test added after this one that queues
    // against the singleton would find a live worker actually executing its
    // operations rather than leaving them READY. Not exercised by this
    // project's normal `./pirate-gtest` invocation; worth revisiting if
    // that ever changes.
}

TEST_F(rpc_wallet_tests_bitcoin, rpc_z_sendmany_parameters)
{
    SelectParams(CBaseChainParams::TESTNET);

    LOCK(pwalletMain->cs_wallet);

    EXPECT_THROW(CallRPC("z_sendmany"), runtime_error);
    EXPECT_THROW(CallRPC("z_sendmany toofewargs"), runtime_error);
    EXPECT_THROW(CallRPC("z_sendmany just too many args here"), runtime_error);

    // bad from address
    EXPECT_THROW(CallRPC("z_sendmany "
            "INVALIDtmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ []"), runtime_error);
    // empty amounts
    EXPECT_THROW(CallRPC("z_sendmany "
            "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ []"), runtime_error);

    // don't have the spending key for this address
    EXPECT_THROW(CallRPC("z_sendmany "
            "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"
            "UkJ1oSfbhTJhm72WiZizvkZz5aH1 []"), runtime_error);

    // duplicate address
    EXPECT_THROW(CallRPC("z_sendmany "
            "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
            "[{\"address\":\"tmQP9L3s31cLsghVYf2Jb5MhKj1jRBPoeQn\", \"amount\":50.0},"
            " {\"address\":\"tmQP9L3s31cLsghVYf2Jb5MhKj1jRBPoeQn\", \"amount\":12.0} ]"
            ), runtime_error);

    // invalid fee amount, cannot be negative
    EXPECT_THROW(CallRPC("z_sendmany "
            "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
            "[{\"address\":\"tmQP9L3s31cLsghVYf2Jb5MhKj1jRBPoeQn\", \"amount\":50.0}] "
            "1 -0.0001"
            ), runtime_error);

    // invalid fee amount, bigger than MAX_MONEY
    EXPECT_THROW(CallRPC("z_sendmany "
            "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
            "[{\"address\":\"tmQP9L3s31cLsghVYf2Jb5MhKj1jRBPoeQn\", \"amount\":50.0}] "
            "1 21000001"
            ), runtime_error);

    // fee amount is bigger than sum of outputs
    EXPECT_THROW(CallRPC("z_sendmany "
            "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
            "[{\"address\":\"tmQP9L3s31cLsghVYf2Jb5MhKj1jRBPoeQn\", \"amount\":50.0}] "
            "1 50.00000001"
            ), runtime_error);

    // memo bigger than allowed length of ZC_MEMO_SIZE
    // (previously: badmemo was computed but never placed in the RPC call,
    // and the recipient was a Sprout address - but more fundamentally, this
    // fork's z_sendmany rejects a *transparent* source address outright
    // ("Transparent addresses are not supported as a source for
    // z_sendmany", wallet/rpcwallet.cpp ~5512), which every EXPECT_THROW in
    // this whole test satisfies regardless of intent since they all reuse
    // the same "tmRr..." literal as fromaddress - carried over from
    // upstream Zcash's test, which allows a transparent z_sendmany source.
    // This specific case is fixed by using a real, spendable Sapling source
    // (this test function's other cases likely have the same latent issue
    // but are out of scope for this pass - flagged separately).
    std::vector<char> v (2 * (ZC_MEMO_SIZE+1));     // x2 for hexadecimal string format
    std::fill(v.begin(),v.end(), 'A');
    std::string badmemo(v.begin(), v.end());
    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }
    std::string fromZaddr = EncodePaymentAddress(pwalletMain->GenerateNewSaplingZKey());
    std::string toZaddr = EncodePaymentAddress(pwalletMain->GenerateNewSaplingZKey());
    try {
        CallRPC(string("z_sendmany ") + fromZaddr + " "
            + "[{\"address\":\"" + toZaddr + "\",\"amount\":123.456,\"memo\":\"" + badmemo + "\"}]");
        FAIL() << "expected z_sendmany to reject an oversized memo";
    } catch (const runtime_error& e) {
        EXPECT_THAT(e.what(), testing::HasSubstr("size of memo is larger"));
    }

    // Mutable tx containing contextual information we need to build tx
    UniValue retValue = CallRPC("getblockcount");
    int nHeight = retValue.get_int();
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nHeight + 1);
    if (mtx.nVersion == 1) {
        mtx.nVersion = 2;
    }

    // Test constructor of AsyncRPCOperation_sendmany. This fork's constructor
    // takes (consensusParams, nHeight, fromAddress, saplingOutputs,
    // ironwoodOutputs, minDepth, ...) - no TransactionBuilder/transparent
    // fromaddress support (z_sendmany now rejects transparent sources
    // outright, directing callers to z_shieldcoinbase instead).
    auto consensusParams = Params().GetConsensus();
    try {
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_sendmany(pwalletMain, consensusParams, nHeight, "", {}, {}, -1));
    } catch (const UniValue& objError) {
        EXPECT_TRUE( find_error(objError, "Minconf cannot be negative"));
    }

    try {
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_sendmany(pwalletMain, consensusParams, nHeight, "", {}, {}, 1));
    } catch (const UniValue& objError) {
        EXPECT_TRUE( find_error(objError, "From address parameter missing"));
    }

    try {
        // Recipients-empty is still checked before the from-address's
        // transparent/shielded classification, so this still surfaces
        // "No recipients" rather than the transparent-source rejection.
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_sendmany(pwalletMain, consensusParams, nHeight, "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ", {}, {}, 1) );
    } catch (const UniValue& objError) {
        EXPECT_TRUE( find_error(objError, "No recipients"));
    }

    try {
        std::vector<SendManyRecipient> recipients = { SendManyRecipient("dummy",1.0, "") };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_sendmany(pwalletMain, consensusParams, nHeight, "INVALID", recipients, {}, 1) );
    } catch (const UniValue& objError) {
        EXPECT_TRUE( find_error(objError, "Invalid from address"));
    }

    // Testnet payment addresses begin with 'zt'.  This test detects an incorrect prefix.
    try {
        std::vector<SendManyRecipient> recipients = { SendManyRecipient("dummy",1.0, "") };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_sendmany(pwalletMain, consensusParams, nHeight, "zcMuhvq8sEkHALuSU2i4NbNQxshSAYrpCExec45ZjtivYPbuiFPwk6WHy4SvsbeZ4siy1WheuRGjtaJmoD1J8bFqNXhsG6U", recipients, {}, 1) );
    } catch (const UniValue& objError) {
        EXPECT_TRUE( find_error(objError, "Invalid from address"));
    }

    // A valid zaddr the wallet doesn't hold the spending key for throws again --
    // the previous "constructs successfully instead" behavior (hasOfflineSpendingKey)
    // existed solely to support z_sendmany_prepare_offline, which has been removed
    // as dead code (it never had a working path to actually produce unsigned build
    // data for external signing). The original literal here was a Sprout testnet
    // address ("zt..."), which is always rejected by IsValidPaymentAddress()
    // regardless of network - generate a real (never-imported) Sapling testnet
    // address instead.
    {
        std::vector<unsigned char, secure_allocator<unsigned char>> notOwnedRawSeed(32);
        for (auto& b : notOwnedRawSeed) b = 0x42;
        HDSeed notOwnedSeed(notOwnedRawSeed);
        auto notOwnedKey = libzcash::SaplingExtendedSpendingKey::Master(notOwnedSeed).Derive(0 | HARDENED_KEY_LIMIT);
        std::string notOwnedAddr = EncodePaymentAddress(notOwnedKey.DefaultAddress());
        std::vector<SendManyRecipient> recipients = { SendManyRecipient("dummy",1.0, "") };
        try {
            std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_sendmany(pwalletMain, consensusParams, nHeight, notOwnedAddr, recipients, {}, 1) );
            FAIL() << "expected construction to throw for an address with no spending key";
        } catch (const UniValue& objError) {
            EXPECT_TRUE(find_error(objError, "spending key not found"));
        }
    }
}


// TODO: test private methods
// rpc_z_sendmany_internals and rpc_z_sendmany_taddr_to_sapling - both probe
// AsyncRPCOperation_sendmany's removed TEST_FRIEND Sprout-joinsplit internals
// (perform_joinsplit, add_taddr_change_output_to_tx, add_taddr_outputs_to_tx,
// sign_send_raw_transaction, AsyncJoinSplitInfo) and/or construct it with a
// transparent fromAddress, which this fork's z_sendmany now rejects outright
// (transparent sources must go through z_shieldcoinbase instead). Deleted
// outright, not adapted: the internal architecture and supported source-
// address types these tests exercised no longer exist.


/*
 * This test covers storing encrypted zkeys in the wallet.
 */
// rpc_wallet_encrypted_wallet_zkeys is deleted: it tested wallet
// encryption using z_getnewaddress's original default (Sprout) key type.
// z_getnewaddress now always creates a Sapling address (Sprout is no
// longer an option anywhere in this fork - see z_validateaddress above),
// making this test byte-for-byte identical to rpc_wallet_encrypted_wallet_sapzkeys
// below once adapted, not a distinct scenario worth keeping separately.

TEST_F(rpc_wallet_tests_bitcoin, rpc_wallet_encrypted_wallet_sapzkeys)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    UniValue retValue;
    int n = 100;

    // z_getnewaddress requires Sapling active; UpdateNetworkUpgradeParameters()
    // always mutates the REGTEST params singleton regardless of what's
    // currently selected (chainparams.cpp:670), so switch to it explicitly.
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    // z_listaddresses now requires the wallet unlocked by default even for
    // read-only reporting (EnsureWalletIsUnlockedForReporting), unless this
    // opt-in flag is set - needed here since the test lists addresses while
    // deliberately still locked, below.
    bool previousUnlockedForReporting = fUnlockedForReporting;
    fUnlockedForReporting = true;

    if(!pwalletMain->HaveHDSeed())
    {
        pwalletMain->GenerateNewSeed();
    }

    // wallet should currently be empty
    std::set<libzcash::SaplingPaymentAddress> addrs;
    pwalletMain->GetSaplingPaymentAddresses(addrs);
    EXPECT_TRUE(addrs.size()==0);

    // create keys
    for (int i = 0; i < n; i++) {
        CallRPC("z_getnewaddress sapling");
    }

    // Verify we can list the keys imported. Only the *first* (primary,
    // hdChain.saplingAccountCounter==0) generated key gets an internal/change
    // address auto-registered alongside it - GenerateNewSaplingZKey() gates
    // that on being the primary key, unlike z_importkey's path (see
    // rpc_wallet_z_importexport) which registers one per imported key
    // unconditionally. So n generated keys yield n+1 addresses here, not 2*n.
    EXPECT_NO_THROW(retValue = CallRPC("z_listaddresses"));
    UniValue arr = retValue.get_array();
    EXPECT_TRUE(arr.size() == n+1);

    // Verify that the wallet encryption RPC is disabled
    EXPECT_THROW(CallRPC("encryptwallet passphrase"), runtime_error);

    // Encrypt the wallet (we can't call RPC encryptwallet as that shuts down node)
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";

    // Process-wide CWD change - restore it before this fixture's TearDown
    // deletes pathTemp, or the *next* test's SetUp throws trying to resolve
    // a CWD that no longer exists on disk.
    boost::filesystem::path previousCwd = boost::filesystem::current_path();
    boost::filesystem::current_path(GetArg("-datadir","/tmp/thisshouldnothappen"));
    EXPECT_TRUE(pwalletMain->EncryptWallet(strWalletPass));

    // Verify we can still list the keys imported
    EXPECT_NO_THROW(retValue = CallRPC("z_listaddresses"));
    arr = retValue.get_array();
    EXPECT_TRUE(arr.size() == n+1);

    // Try to add a new key, but we can't as the wallet is locked
    EXPECT_THROW(CallRPC("z_getnewaddress sapling"), runtime_error);

    // We can't call RPC walletpassphrase as that invokes RPCRunLater which breaks tests.
    // So we manually unlock.
    EXPECT_TRUE(pwalletMain->Unlock(strWalletPass));

    // Now add a key
    EXPECT_NO_THROW(CallRPC("z_getnewaddress sapling"));

    // Verify the key has been added (one more external address only - the
    // shared internal/change address was already counted above)
    EXPECT_NO_THROW(retValue = CallRPC("z_listaddresses"));
    arr = retValue.get_array();
    EXPECT_TRUE(arr.size() == n+2);

    // We can't simulate over RPC the wallet closing and being reloaded
    // but there are tests for this in gtest.

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    SelectParams(CBaseChainParams::MAIN);
    fUnlockedForReporting = previousUnlockedForReporting;
    boost::filesystem::current_path(previousCwd);
}


TEST_F(rpc_wallet_tests_bitcoin, rpc_z_viewtransaction_sapling_outputs_without_spends)
{
    // Regression test for a copy-paste bug in z_viewtransaction: the Sapling
    // outputs loop bounded itself with GetSaplingSpendsCount() instead of
    // GetSaplingOutputsCount() (wallet/rpcwallet.cpp), so any transaction with
    // more Sapling outputs than Sapling spends had the excess outputs silently
    // dropped from the RPC result. An ordinary shielded receive - zero spends,
    // one or more outputs - hit this worst-case: outputCount was truncated to
    // zero and the outputs array came back empty even though the wallet could
    // decrypt the note. Build exactly that shape and confirm the RPC reports it.
    LOCK2(cs_main, pwalletMain->cs_wallet);

    // TransactionBuilder needs Sapling active; UpdateNetworkUpgradeParameters()
    // always mutates the REGTEST params singleton (chainparams.cpp:670).
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            SelectParams(CBaseChainParams::MAIN);
        }
    } upgradeReverter;

    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }
    libzcash::SaplingPaymentAddress myAddr = pwalletMain->GenerateNewSaplingZKey();
    libzcash::SaplingExtendedSpendingKey myExtsk;
    ASSERT_TRUE(pwalletMain->GetSaplingExtendedSpendingKey(myAddr, myExtsk));

    // Funding key for the transparent input; only used to build the tx
    // locally, never broadcast or mined.
    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret("UuRoAgHmjHZqexxVAPjzW8N6hr3o7aETZqCZon2m8EYAmjmdTcj1");
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    CScript scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(Params().GetConsensus(), 1);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << 1 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKey;
    txNew.vout[0].nValue = 50000;
    txNew.nExpiryHeight = 0;
    CTransaction coinbaseTx(txNew);

    auto builder = TransactionBuilder(Params().GetConsensus(), 1, &keystore);
    builder.AddTransparentInput(COutPoint(coinbaseTx.GetHash(), 0), scriptPubKey, 50000);
    builder.InitializeSapling(uint256());
    ASSERT_TRUE(builder.AddSaplingOutputRaw(myAddr, 40000, {}));
    ASSERT_TRUE(builder.ConvertRawSaplingOutput(myExtsk.expsk.ovk));
    auto maybeTx = builder.Build();
    ASSERT_TRUE(maybeTx.IsTx());
    CTransaction tx = maybeTx.GetTxOrThrow();

    // Confirm this reproduces the bug's exact shape: zero spends, at least
    // one output (the builder pads to a 2-output privacy floor, but that
    // padding is irrelevant here - what matters is spends == 0 < outputs).
    ASSERT_EQ(tx.GetSaplingSpendsCount(), 0);
    ASSERT_GT(tx.GetSaplingOutputsCount(), 0u);

    // Feed the tx through the same wallet path a block-connect/rescan would
    // use, so mapSaplingNoteData ends up populated exactly as it would for a
    // real received transaction.
    std::vector<CTransaction> vtx{tx};
    std::vector<CTransaction> vAdded;
    std::set<libzcash::SaplingPaymentAddress> saplingAddressesFound;
    std::set<libzcash::IronwoodPaymentAddress> ironwoodAddressesFound;
    pwalletMain->AddToWalletIfInvolvingMe(vtx, vAdded, nullptr, 1, true, saplingAddressesFound, ironwoodAddressesFound, false);
    ASSERT_EQ(vAdded.size(), 1u);

    UniValue result;
    EXPECT_NO_THROW(result = CallRPC("z_viewtransaction " + tx.GetHash().GetHex()));
    UniValue outputs = find_value(result, "outputs");
    ASSERT_TRUE(outputs.isArray());

    bool foundOurOutput = false;
    for (const UniValue& out : outputs.getValues()) {
        if (find_value(out, "type").get_str() == "sapling" &&
            find_value(out, "address").get_str() == EncodePaymentAddress(myAddr)) {
            foundOurOutput = true;
            EXPECT_EQ(find_value(out, "valueZat").get_int64(), 40000);
        }
    }
    EXPECT_TRUE(foundOurOutput)
        << "z_viewtransaction omitted the wallet's own Sapling output on a "
           "transaction with zero Sapling spends";
}


TEST_F(rpc_wallet_tests_bitcoin, rpc_z_viewtransaction_ironwood_outputs)
{
    // z_viewtransaction previously had no Ironwood handling at all - its
    // spends/outputs loops only ever looked at Sprout and Sapling data, so an
    // Ironwood receive was silently absent from the RPC's report regardless
    // of the GetSaplingOutputsCount()/GetSaplingSpendsCount() bug fixed above.
    // Confirm a plain Ironwood receive (own address, own OVK, zero spends) is
    // now reported.
    LOCK2(cs_main, pwalletMain->cs_wallet);

    // TransactionBuilder needs Overwinter/Sapling/Ironwood active; REGTEST is
    // the only chain UpdateNetworkUpgradeParameters() mutates.
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            SelectParams(CBaseChainParams::MAIN);
        }
    } upgradeReverter;

    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }
    libzcash::IronwoodPaymentAddress myAddr = pwalletMain->GenerateNewIronwoodZKey();
    libzcash::IronwoodExtendedSpendingKeyPirate myExtsk;
    ASSERT_TRUE(pwalletMain->GetIronwoodExtendedSpendingKey(myAddr, myExtsk));
    libzcash::IronwoodFullViewingKey myFvk;
    ASSERT_TRUE(myExtsk.sk.DeriveFVK(&myFvk));
    libzcash::IronwoodOutgoingViewingKey myOvk;
    ASSERT_TRUE(myFvk.DeriveOVK(&myOvk));

    // Funding key for the transparent input; only used to build the tx
    // locally, never broadcast or mined.
    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret("UuRoAgHmjHZqexxVAPjzW8N6hr3o7aETZqCZon2m8EYAmjmdTcj1");
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    CScript scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(Params().GetConsensus(), 1);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << 1 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKey;
    txNew.vout[0].nValue = 50000;
    txNew.nExpiryHeight = 0;
    CTransaction coinbaseTx(txNew);

    auto builder = TransactionBuilder(Params().GetConsensus(), 1, &keystore);
    builder.AddTransparentInput(COutPoint(coinbaseTx.GetHash(), 0), scriptPubKey, 50000);
    builder.InitializeIronwood(/*spendsEnabled=*/false, /*outputsEnabled=*/true, uint256());
    ASSERT_TRUE(builder.AddIronwoodOutputRaw(myAddr, 40000, {}));
    ASSERT_TRUE(builder.ConvertRawIronwoodOutput(myOvk.ovk));
    auto maybeTx = builder.Build();
    ASSERT_TRUE(maybeTx.IsTx());
    CTransaction tx = maybeTx.GetTxOrThrow();

    ASSERT_GT(tx.GetIronwoodActionsCount(), 0u);

    // Feed the tx through the same wallet path a block-connect/rescan would
    // use, so mapIronwoodNoteData ends up populated exactly as it would for a
    // real received transaction.
    std::vector<CTransaction> vtx{tx};
    std::vector<CTransaction> vAdded;
    std::set<libzcash::SaplingPaymentAddress> saplingAddressesFound;
    std::set<libzcash::IronwoodPaymentAddress> ironwoodAddressesFound;
    pwalletMain->AddToWalletIfInvolvingMe(vtx, vAdded, nullptr, 1, true, saplingAddressesFound, ironwoodAddressesFound, false);
    ASSERT_EQ(vAdded.size(), 1u);

    UniValue result;
    EXPECT_NO_THROW(result = CallRPC("z_viewtransaction " + tx.GetHash().GetHex()));
    UniValue outputs = find_value(result, "outputs");
    ASSERT_TRUE(outputs.isArray());

    bool foundOurOutput = false;
    for (const UniValue& out : outputs.getValues()) {
        if (find_value(out, "type").get_str() == "ironwood" &&
            find_value(out, "address").get_str() == EncodePaymentAddress(myAddr)) {
            foundOurOutput = true;
            EXPECT_EQ(find_value(out, "valueZat").get_int64(), 40000);
        }
    }
    EXPECT_TRUE(foundOurOutput) << "z_viewtransaction did not report the wallet's own Ironwood output";
}


TEST_F(rpc_wallet_tests_bitcoin, rpc_z_listunspent_parameters)
{
    SelectParams(CBaseChainParams::TESTNET);

    LOCK(pwalletMain->cs_wallet);

    UniValue retValue;

    // too many args
    EXPECT_THROW(CallRPC("z_listunspent 1 2 3 4 5"), runtime_error);

    // minconf must be >= 0
    EXPECT_THROW(CallRPC("z_listunspent -1"), runtime_error);

    // maxconf must be > minconf
    EXPECT_THROW(CallRPC("z_listunspent 2 1"), runtime_error);

    // maxconf must not be out of range
    EXPECT_THROW(CallRPC("z_listunspent 1 9999999999"), runtime_error);

    // The original "ztjiDe..." literal here was a Sprout testnet address,
    // which is always rejected by IsValidPaymentAddress() regardless of
    // network (see z_validateaddress above) - use a real (never-imported)
    // Sapling testnet address instead so the "not a valid zaddr" cases below
    // actually exercise the intended not-owned/watch-only logic instead of
    // failing at basic decode.
    std::vector<unsigned char, secure_allocator<unsigned char>> notOwnedRawSeed2(32);
    for (auto& b : notOwnedRawSeed2) b = 0x37;
    HDSeed notOwnedSeed2(notOwnedRawSeed2);
    std::string notOwnedTestnetZaddr = EncodePaymentAddress(
        libzcash::SaplingExtendedSpendingKey::Master(notOwnedSeed2).Derive(0 | HARDENED_KEY_LIMIT).DefaultAddress());

    // must be an array of addresses
    EXPECT_THROW(CallRPC("z_listunspent 1 999 false " + notOwnedTestnetZaddr), runtime_error);

    // address must be string
    EXPECT_THROW(CallRPC("z_listunspent 1 999 false [123456]"), runtime_error);

    // no spending key
    EXPECT_THROW(CallRPC("z_listunspent 1 999 false [\"" + notOwnedTestnetZaddr + "\"]"), runtime_error);

    // allow watch only
    EXPECT_NO_THROW(CallRPC("z_listunspent 1 999 true [\"" + notOwnedTestnetZaddr + "\"]"));

    // wrong network, mainnet instead of testnet
    EXPECT_THROW(CallRPC("z_listunspent 1 999 true [\"zcMuhvq8sEkHALuSU2i4NbNQxshSAYrpCExec45ZjtivYPbuiFPwk6WHy4SvsbeZ4siy1WheuRGjtaJmoD1J8bFqNXhsG6U\"]"), runtime_error);

    // create shielded address so we have the spending key. Using the RPC
    // (z_getnewaddress) here would require Sapling activation, which can
    // only be forced on REGTEST (UpdateNetworkUpgradeParameters always
    // mutates the REGTEST params singleton - chainparams.cpp:670), but this
    // test needs to stay on TESTNET throughout for its address-encoding
    // checks. GenerateNewSaplingZKey() is the same underlying wallet method
    // z_getnewaddress calls, minus the RPC-level activation gate.
    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }
    std::string myzaddr = EncodePaymentAddress(pwalletMain->GenerateNewSaplingZKey());

    // return empty array for this address
    EXPECT_NO_THROW(retValue = CallRPC("z_listunspent 1 999 false [\"" + myzaddr + "\"]"));
    UniValue arr = retValue.get_array();
    EXPECT_EQ(0, arr.size());

    // duplicate address error
    EXPECT_THROW(CallRPC("z_listunspent 1 999 false [\"" + myzaddr + "\", \"" + myzaddr + "\"]"), runtime_error);
}


TEST_F(rpc_wallet_tests_bitcoin, rpc_z_shieldcoinbase_parameters)
{
    SelectParams(CBaseChainParams::TESTNET);

    LOCK(pwalletMain->cs_wallet);

    EXPECT_THROW(CallRPC("z_shieldcoinbase"), runtime_error);
    EXPECT_THROW(CallRPC("z_shieldcoinbase toofewargs"), runtime_error);
    EXPECT_THROW(CallRPC("z_shieldcoinbase too many args shown here"), runtime_error);

    // bad from address
    EXPECT_THROW(CallRPC("z_shieldcoinbase "
            "INVALIDtmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"), runtime_error);

    // bad from address
    EXPECT_THROW(CallRPC("z_shieldcoinbase "
    "** tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"), runtime_error);

    // bad to address
    EXPECT_THROW(CallRPC("z_shieldcoinbase "
    "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ INVALIDtnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"), runtime_error);

    // invalid fee amount, cannot be negative
    EXPECT_THROW(CallRPC("z_shieldcoinbase "
            "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
            "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
            "-0.0001"
            ), runtime_error);

    // invalid fee amount, bigger than MAX_MONEY
    EXPECT_THROW(CallRPC("z_shieldcoinbase "
            "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
            "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
            "21000001"
            ), runtime_error);

    // invalid limit, must be at least 0
    EXPECT_THROW(CallRPC("z_shieldcoinbase "
    "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
    "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
    "100 -1"
    ), runtime_error);

    // Mutable tx containing contextual information we need to build tx
    UniValue retValue = CallRPC("getblockcount");
    int nHeight = retValue.get_int();
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nHeight + 1);
    if (mtx.nVersion == 1) {
        mtx.nVersion = 2;
    }

    // Test constructor of AsyncRPCOperation_shieldcoinbase. This fork's
    // constructor takes (consensusParams, nHeight, contextualTx, inputs,
    // toAddress, fee, ...). The negative-fee case from the original test is
    // deleted: this fork's constructor guards fee with assert(fee_ >= 0)
    // instead of throwing a catchable JSONRPCError, so it's no longer
    // testable via direct construction (the RPC layer above it, exercised
    // separately via CallRPC(...) earlier in this test, already rejects
    // negative fees before ever reaching this constructor).
    auto consensusParams = Params().GetConsensus();
    // The original "ztjiDe..." literal was a Sprout testnet address, always
    // rejected by IsValidPaymentAddress() regardless of network (see
    // z_validateaddress above) - checked here even before the empty-inputs
    // check (AsyncRPCOperation_shieldcoinbase validates the recipient address
    // first), so it would surface "Invalid recipient address" instead of the
    // intended "No coinbase inputs" case. Generate a real Sapling testnet
    // address instead.
    std::vector<unsigned char, secure_allocator<unsigned char>> shieldRawSeed(32);
    for (auto& b : shieldRawSeed) b = 0x51;
    HDSeed shieldSeed(shieldRawSeed);
    std::string testnetzaddr = EncodePaymentAddress(
        libzcash::SaplingExtendedSpendingKey::Master(shieldSeed).Derive(0 | HARDENED_KEY_LIMIT).DefaultAddress());
    std::string mainnetzaddr = "zcMuhvq8sEkHALuSU2i4NbNQxshSAYrpCExec45ZjtivYPbuiFPwk6WHy4SvsbeZ4siy1WheuRGjtaJmoD1J8bFqNXhsG6U";

    try {
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_shieldcoinbase(pwalletMain, consensusParams, nHeight, mtx, {}, testnetzaddr, 1));
    } catch (const UniValue& objError) {
        EXPECT_TRUE( find_error(objError, "No coinbase inputs provided for shielding"));
    }

    // Testnet payment addresses begin with 'zt'.  This test detects an incorrect prefix.
    try {
        std::vector<ShieldCoinbaseUTXO> inputs = { ShieldCoinbaseUTXO{uint256(),0,0} };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_shieldcoinbase(pwalletMain, consensusParams, nHeight, mtx, inputs, mainnetzaddr, 1) );
    } catch (const UniValue& objError) {
        EXPECT_TRUE( find_error(objError, "Invalid recipient address"));
    }

}

// Regression test: ShieldToAddress::operator()(SaplingPaymentAddress&) used
// to call SendChangeTo() (which adds a Sapling output) without ever calling
// builder_.InitializeSapling() first. That's fine for callers that add
// Sapling spends first (spend-adding initializes the builder along the
// way), but shielding is an outputs-only case - transparent inputs, zero
// Sapling spends - so every real z_shieldcoinbase call targeting a Sapling
// address hit TransactionBuilder::ConvertRawSaplingOutput()'s "cannot add
// Sapling output without Sapling builder (call InitializeSapling first)"
// throw. The Ironwood handler right below this one already called
// InitializeIronwood() in the equivalent spot.
TEST_F(rpc_wallet_tests_bitcoin, rpc_z_shieldcoinbase_sapling_builds_transaction)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    // TransactionBuilder needs Sapling active; UpdateNetworkUpgradeParameters()
    // always mutates the REGTEST params singleton (chainparams.cpp:670).
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            SelectParams(CBaseChainParams::MAIN);
        }
    } upgradeReverter;

    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }
    libzcash::SaplingPaymentAddress myAddr = pwalletMain->GenerateNewSaplingZKey();

    // Funding key for the transparent coinbase input. Unlike the
    // z_viewtransaction tests above (which use a standalone CBasicKeyStore),
    // this key must live in pwalletMain itself: AsyncRPCOperation_shieldcoinbase
    // builds its TransactionBuilder against pwalletMain as the signing keystore.
    CPubKey pubkey = pwalletMain->GenerateNewKey();
    CScript scriptPubKey = GetScriptForDestination(pubkey.GetID());

    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(Params().GetConsensus(), 1);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << 1 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKey;
    txNew.vout[0].nValue = 50000;
    txNew.nExpiryHeight = 0;
    CTransaction coinbaseTx(txNew);

    std::vector<ShieldCoinbaseUTXO> inputs = {
        ShieldCoinbaseUTXO{coinbaseTx.GetHash(), 0, scriptPubKey, 50000}
    };

    auto operation = std::make_shared<AsyncRPCOperation_shieldcoinbase>(
        pwalletMain, Params().GetConsensus(), 1, CMutableTransaction(), inputs, EncodePaymentAddress(myAddr), 10000);
    operation->testmode = true;

    TEST_FRIEND_AsyncRPCOperation_shieldcoinbase proxy(operation);
    bool success = false;
    EXPECT_NO_THROW(success = proxy.main_impl());
    EXPECT_TRUE(success);

    CTransaction tx = proxy.getTx();
    EXPECT_EQ(tx.GetSaplingSpendsCount(), 0);
    EXPECT_GT(tx.GetSaplingOutputsCount(), 0u);
}

// Declared with external linkage in rpc/mining.cpp but only forward-declared
// locally inside gtestutils.cpp itself (not exposed via gtestutils.h) -- redeclared
// here the same way, rather than going through TestChain's own member wrapper
// (which would need a whole separate TestChain instance just for this one call).
extern std::shared_ptr<CBlock> generateBlock(CWallet* wallet, CValidationState* state = nullptr);

// BitcoinTestingSetup::SetUp() calls InitBlockIndex() (activating a genesis
// block for whatever SelectTestParams() chose) before any TEST_F body runs,
// so switching network params *inside* the test body -- after that genesis
// is already committed to the block index -- leaves the index holding one
// network's genesis while Params() reports another, which CheckBlockIndex()
// (fCheckBlockIndex is on for this whole fixture family) correctly rejects.
// REGTEST must be selected by overriding SelectTestParams() instead, exactly
// like miner_tests_bitcoin (test_miner_bitcoin.cpp) already does for the same
// reason -- real mining needs REGTEST's difficulty, not mainnet's.
class rpc_wallet_tests_bitcoin_regtest : public BitcoinTestingSetup {
protected:
    void SelectTestParams() override { SelectParams(CBaseChainParams::REGTEST); }
};

TEST_F(rpc_wallet_tests_bitcoin_regtest, z_createbuildinstructions_operates_on_selected_wallet_not_default)
{
    // Regression coverage for the multiwallet effort: z_createbuildinstructions
    // must read notes from the *request-resolved* wallet (CWalletManager::
    // GetWalletForRequest()), not silently fall through to pwalletMain. Unlike
    // z_buildrawtransaction's own gtest coverage (test_httprpc.cpp), there's no
    // shortcut via error messages here -- this RPC has no upfront "does this
    // wallet own the from-address" check, so calling it against the wrong wallet
    // and calling it against the right-but-empty wallet both silently return a
    // builder with zero spends. The only way to actually distinguish them is a
    // real, spendable, witnessed Sapling note -- which needs a real mined chain
    // (BitcoinTestingSetup, REGTEST difficulty) combined with a CWalletManager
    // secondary wallet, a combination no earlier test in this suite has needed.
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            // Network params themselves are restored by BitcoinBasicTestingSetup::
            // TearDown() (it snapshots previousNetwork before SelectTestParams()
            // runs) -- unconditionally forcing MAIN here would fight that when
            // previousNetwork was actually something else.
        }
    } upgradeReverter;

    // Matching TestChain::TestChain()'s own setup (gtestutils.cpp) -- these
    // globals aren't touched by BitcoinTestingSetup::SetUp() at all, and
    // CreateNewBlockWithKey()'s coinbase-construction path (used by the
    // CWallet*-taking generateBlock() overload below, unlike the plain
    // generateBlock(CBlock*) used elsewhere in this file) may depend on them
    // being in a known state rather than whatever an earlier test in this
    // process left behind.
    NOTARY_PUBKEY = notaryPubkey;
    USE_EXTERNAL_PUBKEY = 0;

    // BitcoinBasicTestingSetup::SetUp() explicitly forces chainName back to
    // the KMD default (assetchain(), empty symbol) for every test using this
    // fixture. That's the right default for this fixture in general, but it
    // means GetBlockSubsidy() takes its `chainName.isKMD()` branch here --
    // which returns a hardcoded 100,000,000 * COIN "ICO allocation" for
    // height 1, a KMD-specific value, not Pirate's. That reward is large
    // enough to overflow the vendored zcash_primitives crate's hardcoded
    // MAX_MONEY (21,000,000 * COIN, upstream Zcash's cap, unrelated to
    // Pirate's real 200,000,000 * COIN cap in komodo_globals.cpp), which
    // makes zcash_transaction_digests() reject the coinbase transparent
    // output as "value out of range" and CTransaction::UpdateHash() throw.
    // The actual fix is here, not in the digest bridge: identify this chain
    // as Pirate and give it Pirate's real subsidy parameters, matching the
    // defaults komodo_args() sets for "-ac_name=PIRATE" on a real node
    // (komodo_utils.cpp), so height 1 mines a normal ~256 PIRATE reward
    // instead of a Komodo-sized one. Captured and restored below so this
    // fixture's other tests keep seeing the KMD default they expect.
    assetchain originalChainName = chainName;
    uint64_t originalReward0 = ASSETCHAINS_REWARD[0];
    uint64_t originalHalving0 = ASSETCHAINS_HALVING[0];
    uint64_t originalSupply = ASSETCHAINS_SUPPLY;
    uint8_t originalPrivate = ASSETCHAINS_PRIVATE;
    chainName = assetchain("PIRATE");
    ASSETCHAINS_REWARD[0] = 25600000000; // 256 PIRATE, matches komodo_args()'s -ac_reward default
    ASSETCHAINS_HALVING[0] = 77777;      // matches komodo_args()'s -ac_halving default
    ASSETCHAINS_SUPPLY = 0;              // matches komodo_args()'s -ac_supply default
    ASSETCHAINS_PRIVATE = 1;             // matches komodo_args()'s -ac_private default
    struct ChainIdentityReverter {
        assetchain originalChainName;
        uint64_t originalReward0;
        uint64_t originalHalving0;
        uint64_t originalSupply;
        uint8_t originalPrivate;
        ~ChainIdentityReverter() {
            chainName = originalChainName;
            ASSETCHAINS_REWARD[0] = originalReward0;
            ASSETCHAINS_HALVING[0] = originalHalving0;
            ASSETCHAINS_SUPPLY = originalSupply;
            ASSETCHAINS_PRIVATE = originalPrivate;
        }
    } chainIdentityReverter{originalChainName, originalReward0, originalHalving0, originalSupply, originalPrivate};

    // BitcoinTestingSetup::SetUp() constructs pwalletMain directly and never
    // registers it with CWalletManager (it predates the multiwallet effort) --
    // register it now, and clean up the registry (not pwalletMain itself, which
    // BitcoinTestingSetup::TearDown() still owns and deletes) before this test
    // ends, so this process-wide singleton doesn't leak state into later tests.
    CWalletManager::Get().RegisterDefaultWallet(pwalletMain->GetName(), pwalletMain);
    struct WalletManagerCleanup {
        ~WalletManagerCleanup() {
            CWalletManager::Get().FlushAndUnloadAllSecondaryWallets();
            CWalletManager::Get().Reset();
        }
    } walletManagerCleanup;

    std::string strError, seedPhrase;
    ASSERT_TRUE(CWalletManager::Get().CreateWallet("secondary_zcbi_test.dat", strError, seedPhrase)) << strError;
    CWallet* secondaryWallet = CWalletManager::Get().GetWallet("secondary_zcbi_test.dat");
    ASSERT_NE(nullptr, secondaryWallet);

    libzcash::SaplingPaymentAddress secondaryAddr;
    {
        LOCK(secondaryWallet->cs_wallet);
        secondaryAddr = secondaryWallet->GenerateNewSaplingZKey();
    }

    // Mine a coinbase into the secondary wallet's own transparent keypool, then
    // 100 more blocks so it clears COINBASE_MATURITY and can actually be spent.
    std::shared_ptr<CBlock> coinbaseBlock;
    try {
        coinbaseBlock = generateBlock(secondaryWallet);
    } catch (const std::exception& e) {
        FAIL() << "generateBlock(secondaryWallet) threw: " << e.what();
    }
    ASSERT_NE(nullptr, coinbaseBlock);
    CTransaction coinbaseTx = coinbaseBlock->vtx[0];
    for (int i = 0; i < 100; i++) {
        ASSERT_NE(nullptr, generateBlock(pwalletMain)) << "block " << i;
    }

    // Shield the now-mature coinbase into the secondary wallet's own Sapling
    // address -- testmode=true skips this operation's own sendrawtransaction
    // call so the test controls exactly when it's broadcast and mined.
    int shieldHeight;
    {
        LOCK(cs_main);
        shieldHeight = chainActive.Height();
    }
    std::vector<ShieldCoinbaseUTXO> inputs = {
        ShieldCoinbaseUTXO{coinbaseTx.GetHash(), 0, coinbaseTx.vout[0].scriptPubKey, coinbaseTx.vout[0].nValue}
    };
    auto shieldOp = std::make_shared<AsyncRPCOperation_shieldcoinbase>(
        secondaryWallet, Params().GetConsensus(), shieldHeight, CMutableTransaction(), inputs,
        EncodePaymentAddress(secondaryAddr), 10000);
    shieldOp->testmode = true;
    TEST_FRIEND_AsyncRPCOperation_shieldcoinbase shieldProxy(shieldOp);
    bool shieldSuccess = false;
    ASSERT_NO_THROW(shieldSuccess = shieldProxy.main_impl());
    ASSERT_TRUE(shieldSuccess);
    CTransaction shieldingTx = shieldProxy.getTx();

    // Broadcast and mine the shielding tx for real, so the secondary wallet's
    // own Sapling note tracking (merkle witness, mapSaplingNoteData) gets
    // populated the normal way, via ConnectTip's wallet notification -- not
    // hand-poked, since z_createbuildinstructions reads that same real state.
    {
        UniValue sendParams(UniValue::VARR);
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << shieldingTx;
        sendParams.push_back(HexStr(ss.begin(), ss.end()));
        ASSERT_NO_THROW(sendrawtransaction(sendParams, false, CPubKey()));
    }
    ASSERT_NE(nullptr, generateBlock(pwalletMain));

    // The secondary wallet now holds a real, witnessed Sapling note pwalletMain
    // has never seen. Build the same instructions request against each wallet in
    // turn and confirm the builder blob's own spend list -- vSaplingSpends is
    // public on TransactionBuilder specifically for tests like this -- actually
    // differs: non-empty when resolved to the wallet that holds the note, empty
    // when resolved to the one that doesn't.
    UniValue outputs(UniValue::VARR);
    UniValue outputObj(UniValue::VOBJ);
    outputObj.push_back(Pair("address", EncodePaymentAddress(secondaryAddr)));
    outputObj.push_back(Pair("amount", ValueFromAmount(1000)));
    outputs.push_back(outputObj);

    UniValue params(UniValue::VARR);
    params.push_back(EncodePaymentAddress(secondaryAddr));
    params.push_back(outputs);

    // CRPCTable::execute() refuses everything with RPC_IN_WARMUP until this is
    // called; it's a global, one-way (assert-guarded) switch, so only flip it
    // if some earlier test in this binary hasn't already (test_httprpc.cpp
    // establishes this same guard for the same reason).
    if (RPCIsInWarmup(nullptr))
        SetRPCWarmupFinished();

    UniValue secondaryResult;
    {
        RPCWalletRequestGuard guard("secondary_zcbi_test.dat");
        ASSERT_TRUE(guard.IsResolved());
        try {
            secondaryResult = tableRPC.execute("z_createbuildinstructions", params);
        } catch (const UniValue& u) {
            FAIL() << "z_createbuildinstructions threw UniValue: " << u.write();
        } catch (const std::exception& e) {
            FAIL() << "z_createbuildinstructions threw: " << e.what();
        }
    }
    TransactionBuilder secondaryTb;
    {
        std::vector<unsigned char> raw = ParseHex(secondaryResult.get_str());
        CDataStream ss(raw, SER_NETWORK, PROTOCOL_VERSION);
        ss >> secondaryTb;
    }
    EXPECT_FALSE(secondaryTb.vSaplingSpends.empty());

    UniValue defaultResult;
    ASSERT_NO_THROW(defaultResult = tableRPC.execute("z_createbuildinstructions", params));
    TransactionBuilder defaultTb;
    {
        std::vector<unsigned char> raw = ParseHex(defaultResult.get_str());
        CDataStream ss(raw, SER_NETWORK, PROTOCOL_VERSION);
        ss >> defaultTb;
    }
    EXPECT_TRUE(defaultTb.vSaplingSpends.empty());
}


// rpc_z_shieldcoinbase_internals - probes AsyncRPCOperation_shieldcoinbase's
// removed TEST_FRIEND perform_joinsplit()/ShieldCoinbaseJSInfo Sprout-era
// internals; this fork's shielding is TransactionBuilder-only now.

// Phase 4 protocol-coverage audit: the consolidation/sweep RPC family
// (enablesaplingconsolidation, enableironwoodconsolidation,
// consolidationstatus, enablesweep, sweepstatus, consolidateaddress) and
// their backing AsyncRPCOperation_*consolidation*/_sweeptoaddress classes had
// zero test coverage anywhere in the suite. The enable*/consolidationstatus/
// enablesweep/sweepstatus RPCs are plain synchronous flag-toggle RPCs (not
// async operations themselves - the actual background consolidation/sweep
// runs are triggered elsewhere on a timer), so a direct round-trip is cheap
// and doesn't need any async-operation mocking.
//
// enableironwoodconsolidation was added alongside this test: previously only
// Sapling had an RPC toggle (the original RPC was named "enableconsolidation"
// with no pool qualifier) even though fIronwoodConsolidationEnabled and the
// full AsyncRPCOperation_ironwoodconsolidation* machinery already existed -
// it was only reachable via the -consolidateironwoodaddress startup arg. The
// Sapling RPC was renamed to enablesaplingconsolidation to keep the pair
// symmetric.
TEST_F(rpc_wallet_tests_bitcoin, rpc_enablesaplingconsolidation_roundtrips)
{
    SelectParams(CBaseChainParams::TESTNET);
    LOCK(pwalletMain->cs_wallet);

    UniValue status = CallRPC("consolidationstatus");
    UniValue obj = status.get_obj();
    // consolidationstatus nests per-pool status under "sapling"/"ironwood".
    ASSERT_FALSE(find_value(obj, "sapling").isNull());
    EXPECT_FALSE(find_value(find_value(obj, "sapling").get_obj(), "consolidationEnabled").get_bool());

    UniValue enabled = CallRPC("enablesaplingconsolidation true");
    EXPECT_TRUE(find_value(enabled.get_obj(), "consolidationEnabled").get_bool());

    status = CallRPC("consolidationstatus");
    obj = status.get_obj();
    EXPECT_TRUE(find_value(find_value(obj, "sapling").get_obj(), "consolidationEnabled").get_bool());

    UniValue disabled = CallRPC("enablesaplingconsolidation false");
    EXPECT_FALSE(find_value(disabled.get_obj(), "consolidationEnabled").get_bool());

    EXPECT_THROW(CallRPC("enablesaplingconsolidation"), runtime_error);
    EXPECT_THROW(CallRPC("enablesaplingconsolidation true false"), runtime_error);
    EXPECT_THROW(CallRPC("enablesaplingconsolidation notaboolean"), runtime_error);
}

TEST_F(rpc_wallet_tests_bitcoin, rpc_enableironwoodconsolidation_roundtrips)
{
    SelectParams(CBaseChainParams::TESTNET);
    LOCK(pwalletMain->cs_wallet);

    UniValue status = CallRPC("consolidationstatus");
    UniValue obj = status.get_obj();
    ASSERT_FALSE(find_value(obj, "ironwood").isNull());
    EXPECT_FALSE(find_value(find_value(obj, "ironwood").get_obj(), "consolidationEnabled").get_bool());

    UniValue enabled = CallRPC("enableironwoodconsolidation true");
    EXPECT_TRUE(find_value(enabled.get_obj(), "consolidationEnabled").get_bool());

    status = CallRPC("consolidationstatus");
    obj = status.get_obj();
    EXPECT_TRUE(find_value(find_value(obj, "ironwood").get_obj(), "consolidationEnabled").get_bool());

    UniValue disabled = CallRPC("enableironwoodconsolidation false");
    EXPECT_FALSE(find_value(disabled.get_obj(), "consolidationEnabled").get_bool());

    EXPECT_THROW(CallRPC("enableironwoodconsolidation"), runtime_error);
    EXPECT_THROW(CallRPC("enableironwoodconsolidation true false"), runtime_error);
    EXPECT_THROW(CallRPC("enableironwoodconsolidation notaboolean"), runtime_error);
}

// enableconsolidation toggles both pools at once (a convenience combining
// enablesaplingconsolidation + enableironwoodconsolidation), added alongside them.
TEST_F(rpc_wallet_tests_bitcoin, rpc_enableconsolidation_both_pools_roundtrips)
{
    SelectParams(CBaseChainParams::TESTNET);
    LOCK(pwalletMain->cs_wallet);

    UniValue status = CallRPC("consolidationstatus");
    UniValue obj = status.get_obj();
    EXPECT_FALSE(find_value(find_value(obj, "sapling").get_obj(), "consolidationEnabled").get_bool());
    EXPECT_FALSE(find_value(find_value(obj, "ironwood").get_obj(), "consolidationEnabled").get_bool());

    UniValue enabled = CallRPC("enableconsolidation true");
    EXPECT_TRUE(find_value(enabled.get_obj(), "saplingConsolidationEnabled").get_bool());
    EXPECT_TRUE(find_value(enabled.get_obj(), "ironwoodConsolidationEnabled").get_bool());

    status = CallRPC("consolidationstatus");
    obj = status.get_obj();
    EXPECT_TRUE(find_value(find_value(obj, "sapling").get_obj(), "consolidationEnabled").get_bool());
    EXPECT_TRUE(find_value(find_value(obj, "ironwood").get_obj(), "consolidationEnabled").get_bool());

    UniValue disabled = CallRPC("enableconsolidation false");
    EXPECT_FALSE(find_value(disabled.get_obj(), "saplingConsolidationEnabled").get_bool());
    EXPECT_FALSE(find_value(disabled.get_obj(), "ironwoodConsolidationEnabled").get_bool());

    status = CallRPC("consolidationstatus");
    obj = status.get_obj();
    EXPECT_FALSE(find_value(find_value(obj, "sapling").get_obj(), "consolidationEnabled").get_bool());
    EXPECT_FALSE(find_value(find_value(obj, "ironwood").get_obj(), "consolidationEnabled").get_bool());

    EXPECT_THROW(CallRPC("enableconsolidation"), runtime_error);
    EXPECT_THROW(CallRPC("enableconsolidation true false"), runtime_error);
    EXPECT_THROW(CallRPC("enableconsolidation notaboolean"), runtime_error);
}

TEST_F(rpc_wallet_tests_bitcoin, rpc_enablesweep_roundtrips)
{
    // z_getnewaddress (needed below) requires Sapling active;
    // UpdateNetworkUpgradeParameters() only ever mutates REGTEST's params.
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    LOCK(pwalletMain->cs_wallet);

    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }

    UniValue status = CallRPC("sweepstatus");
    EXPECT_FALSE(find_value(status.get_obj(), "sweepEnabled").get_bool());

    // enablesweep requires a sweep destination already set (setsweepaddress),
    // which in turn requires a wallet-owned Sapling address with a spending key.
    UniValue newAddrResult = CallRPC("z_getnewaddress sapling");
    std::string sweepAddr = newAddrResult.get_str();
    CallRPC("setsweepaddress " + sweepAddr);

    UniValue enabled = CallRPC("enablesweep true");
    EXPECT_TRUE(find_value(enabled.get_obj(), "sweepEnabled").get_bool());

    status = CallRPC("sweepstatus");
    EXPECT_TRUE(find_value(status.get_obj(), "sweepEnabled").get_bool());
    EXPECT_EQ(find_value(status.get_obj(), "sweepAddress").get_str(), sweepAddr);

    UniValue disabled = CallRPC("enablesweep false");
    EXPECT_FALSE(find_value(disabled.get_obj(), "sweepEnabled").get_bool());

    EXPECT_THROW(CallRPC("enablesweep"), runtime_error);
    EXPECT_THROW(CallRPC("enablesweep notaboolean"), runtime_error);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST_F(rpc_wallet_tests_bitcoin, rpc_consolidateaddress_parameters)
{
    SelectParams(CBaseChainParams::TESTNET);
    LOCK(pwalletMain->cs_wallet);

    EXPECT_THROW(CallRPC("consolidateaddress"), runtime_error);
    EXPECT_THROW(CallRPC("consolidateaddress addr fee maxnotes maxtransactions toomany"), runtime_error);

    // Neither a valid transparent nor a valid shielded address.
    EXPECT_THROW(CallRPC("consolidateaddress notanaddress"), runtime_error);

    // Well-formed but unknown-to-this-wallet Sapling testnet address: rejected
    // before any async operation is created, since the wallet doesn't hold it.
    std::vector<unsigned char, secure_allocator<unsigned char>> rawSeed(32);
    for (auto& b : rawSeed) b = 0x52;
    HDSeed seed(rawSeed);
    std::string testnetzaddr = EncodePaymentAddress(
        libzcash::SaplingExtendedSpendingKey::Master(seed).Derive(0 | HARDENED_KEY_LIMIT).DefaultAddress());
    EXPECT_THROW(CallRPC("consolidateaddress " + testnetzaddr), runtime_error);
}

TEST_F(rpc_wallet_tests_bitcoin, rpc_z_mergetoaddress_parameters)
{
    SelectParams(CBaseChainParams::TESTNET);

    LOCK(pwalletMain->cs_wallet);

    EXPECT_THROW(CallRPC("z_mergetoaddress"), runtime_error);
    EXPECT_THROW(CallRPC("z_mergetoaddress toofewargs"), runtime_error);
    EXPECT_THROW(CallRPC("z_mergetoaddress just too many args present for this method"), runtime_error);

    // bad from address
    EXPECT_THROW(CallRPC("z_mergetoaddress "
            "[\"INVALIDtmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ\"] tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"), runtime_error);

    // bad from address
    EXPECT_THROW(CallRPC("z_mergetoaddress "
    "** tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"), runtime_error);

    // bad from address
    EXPECT_THROW(CallRPC("z_mergetoaddress "
    "[\"**\"] tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"), runtime_error);

    // bad from address
    EXPECT_THROW(CallRPC("z_mergetoaddress "
    "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"), runtime_error);

    // bad from address
    EXPECT_THROW(CallRPC("z_mergetoaddress "
            "[tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ] tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"), runtime_error);

    // bad to address
    EXPECT_THROW(CallRPC("z_mergetoaddress "
    "[\"tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ\"] INVALIDtnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"), runtime_error);

    // duplicate address
    EXPECT_THROW(CallRPC("z_mergetoaddress "
            "[\"tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ\", \"tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ\"] "
            "tmQP9L3s31cLsghVYf2Jb5MhKj1jRBPoeQn"
            ), runtime_error);

    // invalid fee amount, cannot be negative
    EXPECT_THROW(CallRPC("z_mergetoaddress "
            "[\"tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ\"] "
            "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
            "-0.0001"
            ), runtime_error);

    // invalid fee amount, bigger than MAX_MONEY
    EXPECT_THROW(CallRPC("z_mergetoaddress "
            "[\"tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ\"] "
            "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
            "21000001"
            ), runtime_error);

    // invalid transparent limit, must be at least 0
    EXPECT_THROW(CallRPC("z_mergetoaddress "
            "[\"tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ\"] "
            "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
            "0.0001 -1"
            ), runtime_error);

    // invalid shielded limit, must be at least 0
    EXPECT_THROW(CallRPC("z_mergetoaddress "
            "[\"tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ\"] "
            "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
            "0.0001 100 -1"
            ), runtime_error);

    // memo bigger than allowed length of ZC_MEMO_SIZE
    // (previously: the flat positional arg list was missing the
    // maximum_utxo_size argument (params[5]), so badmemo landed there
    // instead of at params[6] (memo) and never reached the memo-length
    // check. It also used "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ" as fromaddress,
    // same as several other EXPECT_THROW cases in this test - that literal
    // does not decode as a valid address at all under this fork's TESTNET
    // params (neither DecodeDestination nor DecodePaymentAddress accept it),
    // so every one of those cases was actually exercising "Unknown address
    // format" instead of its intended check, carried over unnoticed from
    // upstream Zcash's original test. This case is fixed with the
    // "ANY_SAPLING" fromaddresses keyword (wallet/rpcwallet.cpp) instead,
    // sidestepping the bad literal; the other cases in this function likely
    // share the same latent issue but are out of scope for this pass.)
    std::vector<char> v (2 * (ZC_MEMO_SIZE+1));     // x2 for hexadecimal string format
    std::fill(v.begin(),v.end(), 'A');
    std::string badmemo(v.begin(), v.end());
    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }
    // "ANY_SAPLING" requires Sapling active at the current (synthetic, low)
    // test-chain height; UpdateNetworkUpgradeParameters only ever mutates
    // REGTEST's params (chainparams.cpp), so switch there for this case and
    // switch back to TESTNET afterward to avoid disturbing the rest of this
    // test function.
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    std::string zaddr1 = EncodePaymentAddress(pwalletMain->GenerateNewSaplingZKey());
    try {
        CallRPC(string("z_mergetoaddress [\"ANY_SAPLING\"] ")
            + zaddr1 + " 0.0001 100 100 100 " + badmemo);
        FAIL() << "expected z_mergetoaddress to reject an oversized memo";
    } catch (const runtime_error& e) {
        EXPECT_THAT(e.what(), testing::HasSubstr("size of memo is larger"));
    }
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    SelectParams(CBaseChainParams::TESTNET);

    // Mutable tx containing contextual information we need to build tx
    UniValue retValue = CallRPC("getblockcount");
    int nHeight = retValue.get_int();
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nHeight + 1);

    // Test constructor of AsyncRPCOperation_mergetoaddress. This fork's
    // constructor takes (consensusParams, nHeight, contextualTx, utxoInputs,
    // saplingNoteInputs, ironwoodNoteInputs, recipient, fee, ...) - there is
    // no Sprout input parameter at all anymore (MergeToAddressInputSproutNote
    // doesn't exist), so the two "mixed Sprout/Sapling" and "Sprout notes
    // unsupported by TransactionBuilder" cases below are deleted outright:
    // their premise (a Sprout-note-input parameter) is structurally gone,
    // not just relocated.
    auto consensusParams = Params().GetConsensus();
    MergeToAddressRecipient testnetzaddr(
        "ztjiDe569DPNbyTE6TSdJTaSDhoXEHLGvYoUnBU1wfVNU52TEyT6berYtySkd21njAeEoh8fFJUT42kua9r8EnhBaEKqCpP",
        "testnet memo");
    MergeToAddressRecipient mainnetzaddr(
        "zcMuhvq8sEkHALuSU2i4NbNQxshSAYrpCExec45ZjtivYPbuiFPwk6WHy4SvsbeZ4siy1WheuRGjtaJmoD1J8bFqNXhsG6U",
        "mainnet memo");

    try {
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_mergetoaddress(pwalletMain, consensusParams, nHeight, mtx, {}, {}, {}, testnetzaddr, -1 ));
        FAIL() << "Should have caused an error";
    } catch (const UniValue& objError) {
        EXPECT_TRUE( find_error(objError, "Fee is out of range"));
    }

    try {
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_mergetoaddress(pwalletMain, consensusParams, nHeight, mtx, {}, {}, {}, testnetzaddr, 1));
        FAIL() << "Should have caused an error";
    } catch (const UniValue& objError) {
        EXPECT_TRUE( find_error(objError, "No inputs"));
    }

    std::vector<MergeToAddressInputUTXO> inputs = { MergeToAddressInputUTXO{ COutPoint{uint256(), 0}, 0, CScript()} };

    try {
        MergeToAddressRecipient badaddr("", "memo");
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_mergetoaddress(pwalletMain, consensusParams, nHeight, mtx, inputs, {}, {}, badaddr, 1));
        FAIL() << "Should have caused an error";
    } catch (const UniValue& objError) {
        EXPECT_TRUE( find_error(objError, "Recipient parameter missing"));
    }

    // Testnet payment addresses begin with 'zt'.  This test detects an incorrect prefix.
    try {
        std::vector<MergeToAddressInputUTXO> inputs = { MergeToAddressInputUTXO{ COutPoint{uint256(), 0}, 0, CScript()} };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_mergetoaddress(pwalletMain, consensusParams, nHeight, mtx, inputs, {}, {}, mainnetzaddr, 1) );
        FAIL() << "Should have caused an error";
    } catch (const UniValue& objError) {
        EXPECT_TRUE( find_error(objError, "Invalid recipient address"));
    }
}


// TODO: test private methods
// rpc_z_mergetoaddress_internals - probes AsyncRPCOperation_mergetoaddress's
// removed private-method test-friend surface (perform_joinsplit and friends),
// same architectural removal as the sendmany/shieldcoinbase internals below.



