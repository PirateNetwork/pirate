#include <boost/version.hpp>
#include <curl/curl.h>
#include <iostream>
#include <string>
#include <cstring>
#include <memory>

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "komodo_cJSON.h"
#include "hex.h"
#include <zmq.h>
#include <leveldb/db.h>
#include <leveldb/env.h>
#include <memenv.h>

#include "assetchain.h"
#include "bloom.h"
#include "amount.h"
#include "consensus/params.h"
#include "key_io.h"
#include "main.h"
#include "wallet/wallet.h"
#include <univalue.h>
#include "cc/CCinclude.h"
#include "komodo_utils.h"
#include "komodo_notary.h"
#include "komodo_interest.h"
#include "ui_interface.h"

#include <secp256k1.h>
#include <iomanip>

using namespace std;

// #include "komodo_nSPV_defs.h"
// #include "komodo_nSPV.h"            // shared defines, structs, serdes, purge functions
// #include "komodo_nSPV_fullnode.h"   // nSPV fullnode handling of the getnSPV request messages
// #include "komodo_nSPV_superlite.h"  // nSPV superlite client, issuing requests and handling nSPV responses
// #include "komodo_nSPV_wallet.h"     // nSPV_send and support functions, really all the rest is to support this

/* --- stubs for linkage --- */
CCriticalSection cs_main;
BlockMap mapBlockIndex;
CChain chainActive;
bool ShutdownRequested() { return false; }
void StartShutdown() {}
CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams) { return 0; }
uint8_t is_STAKED(const std::string& symbol) { return 0; }
bool IsInitialBlockDownload() { return false; }
bool GetTransaction(const uint256 &hash, CTransaction &txOut, uint256 &hashBlock, bool fAllowSlow) { return false; }
bool myGetTransaction(const uint256 &hash, CTransaction &txOut, uint256 &hashBlock) { return false; }
bool EnsureWalletIsAvailable(bool avoidException) { return true; }

uint32_t GetLatestTimestamp(int32_t height) { return 0; } // CCutils.cpp
struct NSPV_inforesp NSPV_inforesult;
char NSPV_pubkeystr[67],NSPV_wifstr[64];

int32_t STAKED_era(int timestamp) { return 0; } // notaries_staked.cpp
int8_t numStakedNotaries(uint8_t pubkeys[64][33],int8_t era) { return 0; }
int32_t komodo_longestchain() { return -1; }
uint64_t komodo_interestsum() { return 0; } // we don't link agains libbitcoin_wallet_a, so should have stubs
// uint64_t komodo_accrued_interest(int32_t *txheightp,uint32_t *locktimep,uint256 hash,int32_t n,int32_t checkheight,uint64_t checkvalue,int32_t tipheight) { return 0; }
CClientUIInterface uiInterface;
bool pubkey2addr(char *destaddr,uint8_t *pubkey33) { return false; }
void UpdateNotaryAddrs(uint8_t pubkeys[64][33],int8_t numNotaries) { }
uint256 GetMerkleRoot(const std::vector<uint256>& vLeaves) { return uint256(); } // cc/eval.cpp
bool CheckEquihashSolution(const CBlockHeader *pblock, const CChainParams& params) { return false; } //pow.cpp
isminetype IsMine(const CKeyStore& keystore, const CTxDestination& dest) { return ISMINE_NO; }
CMutableTransaction CreateNewContextualCMutableTransaction(const Consensus::Params& consensusParams, int nHeight) { return CMutableTransaction(); }
FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly) { return nullptr; }
// bool ReadBlockFromDisk(int32_t height,CBlock& block, const CDiskBlockPos& pos,bool checkPOW) { return false; }
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex,bool checkPOW) { return false; }
void CWallet::AvailableCoins(vector<COutput>& vCoins, bool fOnlyConfirmed, const CCoinControl *coinControl, bool fIncludeZeroValue, bool fIncludeCoinBase) const {}

CScript COINBASE_FLAGS;

assetchain chainName;
#ifdef ENABLE_WALLET
CWallet* pwalletMain = nullptr;
#endif
/* --- stubs for linkage --- */

int main(int argc, char* argv[])
{
    std::string sPackageString = "v0.01a";
#ifdef PACKAGE_STRING
    sPackageString = sPackageString + " (" + std::string(PACKAGE_STRING) + ")";
#endif
    std::cerr << "Test Linkage : Runner by Decker " << sPackageString << std::endl;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
    if (info) {
        printf("libcurl version: %s\n", info->version);
        printf("Version number: %x (%d.%d.%d)\n", info->version_num,
               (info->version_num >> 16) & 0xff, (info->version_num >> 8) & 0xff, info->version_num & 0xff);
    }

    std::cout << "Boost version: "
              << BOOST_VERSION / 100000 << "."     // Major version
              << BOOST_VERSION / 100 % 1000
               << "." // Minor version
              << BOOST_VERSION % 100               // Patch version
              << std::endl;

    curl_global_cleanup();

    // cjson test
    std::cout << "cJSON version: " << cJSON_Version() << std::endl;

    // decode_hex test from bitcoin_common
    const char hexString[] = "4465636B6572";
    size_t len = std::strlen(hexString);
    size_t byteLen = len / 2 + 1;
    std::unique_ptr<char[]> byteArray(new char[byteLen]);
    decode_hex((uint8_t *)byteArray.get(), len / 2, hexString);
    std::cerr << "Decoded hex: '" << byteArray.get() << "'" << std::endl;

    // libzmq test
    int zmq_major, zmq_minor, zmq_patch;
    zmq_version(&zmq_major, &zmq_minor, &zmq_patch);
    std::cout << "ZeroMQ version: " << zmq_major << "." << zmq_minor << "." << zmq_patch << std::endl;

    // leveldb test
    std::cout << "LevelDB version " << leveldb::kMajorVersion << "." << leveldb::kMinorVersion << std::endl;
    leveldb::Env* penv = leveldb::NewMemEnv(leveldb::Env::Default());
    delete penv;

    /* libbitcoin server test */
    CBloomFilter filter;
    filter.clear();
    // to use this we should link GetRand from libbitcoin_util and this will require libsecp256k1 also, and libzcash_libs (lsodium) 
    // and libbitcoin crypto for hashes ... 

    secp256k1_pubkey pubkey;
    size_t pubkeylen = 0;

    std::cout << "Public Key: ";
    for (int i = 0; i < sizeof(pubkey.data); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(pubkey.data[i]);
    }
    std::cout << std::dec << std::endl;

    /* libbitcoin_util call */
    std::vector<unsigned char>vch(32, 0);
    std::cout << "Zero bytes  : " << HexStr(vch, true) << std::endl;
    GetRandBytes(vch.data(), vch.size());
    std::cout << "Random bytes: " << HexStr(vch, true) << std::endl;

    /* libsecp256k1 call */
    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    unsigned char seed[32];
    LockObject(seed);
    GetRandBytes(seed, 32);
    bool ret = secp256k1_context_randomize(ctx, seed);
    UnlockObject(seed);
    std::cerr << secp256k1_ec_pubkey_create(ctx, &pubkey, vch.data()) << std::endl;
    if (ctx) {
        secp256k1_context_destroy(ctx);
    }

    /* libbitcoin common call */
    CScript script = CScript() << OP_1;
    std::cout << script.ToString() << std::endl;

    /* libunivalue call */
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("status", "still buggy =)"));
    std::cout << obj.write() << std::endl;

    /* libbitcoin crypto call */
    string data = "DECKER";
    uint256 sha256;
    CSHA256().Write((const unsigned char *)data.c_str(), data.length()).Finalize(sha256.begin());
    std::cout << sha256.ToString() << std::endl;
    std::cout << HexStr(sha256) << std::endl;

    /* libzcash call */
    std::vector<unsigned char> bytes{1, 1, 1, 0, 0, 0, 1, 1, 1};
    std::vector<bool> vBool = convertBytesVectorToVector(bytes);
    std::cout << "vBool contents: ";
    for (const bool bit : vBool) {
        std::cout << bit;
    }
    std::cout << std::endl;

    /* libcryptoconditions call */

    // std::cout << "CClib name: " << CClib_name() << std::endl;
    // nb! libcc can't be added without bitcoin_server and other dependencies

    /*

    Remaining libs to test:

    $(LIBBITCOIN_SERVER)
    $(LIBBITCOIN_ZMQ)
    $(LIBBITCOIN_PROTON)
    $(LIBLEVELDB)
    $(LIBMEMENV)
    $(EVENT_PTHREADS_LIBS)
    $(ZMQ_LIBS)
    $(PROTON_LIBS)
    $(LIBCC)
    -lcurl (explicitly added)

    libbitcoin_wallet.a
    */
}