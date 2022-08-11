/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/
#include <mutex>
#include "komodo_defs.h"
#include "komodo_structs.h"

std::mutex komodo_mutex;

#define KOMODO_ELECTION_GAP 2000    //((ASSETCHAINS_SYMBOL[0] == 0) ? 2000 : 100)
#define KOMODO_ASSETCHAIN_MAXLEN 65

//int COINBASE_MATURITY = _COINBASE_MATURITY;//100;
unsigned int WITNESS_CACHE_SIZE = 100+10; //_COINBASE_MATURITY+10;
uint256 KOMODO_EARLYTXID;

bool IS_KOMODO_NOTARY;
bool IS_MODE_EXCHANGEWALLET = false;
bool IS_KOMODO_DEALERNODE;
int32_t KOMODO_EXTRASATOSHI = 0;
int32_t KOMODO_MININGTHREADS = -1,STAKED_NOTARY_ID,USE_EXTERNAL_PUBKEY,KOMODO_CHOSEN_ONE,KOMODO_PAX,KOMODO_REWIND,STAKED_ERA,KOMODO_CONNECTING = -1,ASSETCHAINS_FOUNDERS,KOMODO_NSPV;
int32_t KOMODO_INSYNC,KOMODO_LASTMINED,prevKOMODO_LASTMINED,KOMODO_CCACTIVATE,JUMBLR_PAUSE = 1;
std::string NOTARY_PUBKEY,ASSETCHAINS_OVERRIDE_PUBKEY,DONATION_PUBKEY,ASSETCHAINS_SCRIPTPUB,NOTARY_ADDRESS,ASSETCHAINS_SELFIMPORT,ASSETCHAINS_CCLIB;
uint8_t NOTARY_PUBKEY33[33],ASSETCHAINS_OVERRIDE_PUBKEY33[33],ASSETCHAINS_OVERRIDE_PUBKEYHASH[20],ASSETCHAINS_PUBLIC,ASSETCHAINS_PRIVATE,ASSETCHAINS_TXPOW;
int8_t ASSETCHAINS_ADAPTIVEPOW;
std::vector<uint8_t> Mineropret;

assetchain chainName; // the chain (mainly the symbol for now, more will be migrated)
char ASSETCHAINS_USERPASS[4096];
uint16_t ASSETCHAINS_P2PPORT,ASSETCHAINS_RPCPORT,ASSETCHAINS_BEAMPORT,ASSETCHAINS_CODAPORT;
uint32_t ASSETCHAIN_INIT,ASSETCHAINS_CC,KOMODO_STOPAT,KOMODO_DPOWCONFS = 1,STAKING_MIN_DIFF;
uint32_t ASSETCHAINS_MAGIC = 2387029918;
int64_t ASSETCHAINS_GENESISTXVAL = 5000000000;

/** No amount larger than this (in satoshi) is valid.
 *
 * Note that this constant is *not* the total money supply, which in Bitcoin
 * currently happens to be less than 21,000,000 BTC for various reasons, but
 * rather a sanity check. As this sanity check is used by consensus-critical
 * validation code, the exact value of the MAX_MONEY constant is consensus
 * critical; in unusual circumstances like a(nother) overflow bug that allowed
 * for the creation of coins out of thin air modification could lead to a fork.
 * */
int64_t MAX_MONEY = 200000000 * 100000000LL;

// consensus variables for coinbase timelock control and timelock transaction support
// time locks are specified enough to enable their use initially to lock specific coinbase transactions for emission control
// to be verifiable, timelocks require additional data that enables them to be validated and their ownership and
// release time determined from the blockchain. to do this, every time locked output according to this
// spec will use an op_return with CLTV at front and anything after |OP_RETURN|PUSH of rest|OPRETTYPE_TIMELOCK|script|
uint64_t ASSETCHAINS_TIMELOCKGTE = _ASSETCHAINS_TIMELOCKOFF;
uint64_t ASSETCHAINS_CBOPRET=0;

uint64_t ASSETCHAINS_LASTERA = 1;
uint64_t ASSETCHAINS_ENDSUBSIDY[ASSETCHAINS_MAX_ERAS+1],ASSETCHAINS_REWARD[ASSETCHAINS_MAX_ERAS+1],ASSETCHAINS_HALVING[ASSETCHAINS_MAX_ERAS+1],ASSETCHAINS_DECAY[ASSETCHAINS_MAX_ERAS+1],ASSETCHAINS_NOTARY_PAY[ASSETCHAINS_MAX_ERAS+1],ASSETCHAINS_PEGSCCPARAMS[3];
uint8_t ASSETCHAINS_CCDISABLES[256];
std::vector<std::string> ASSETCHAINS_PRICES,ASSETCHAINS_STOCKS;

uint32_t ASSETCHAINS_NUMALGOS = 1;
uint32_t ASSETCHAINS_EQUIHASH = 0;
const char *ASSETCHAINS_ALGORITHMS[] = {"equihash"};
uint64_t ASSETCHAINS_NONCEMASK[] = {0xffff};
uint32_t ASSETCHAINS_NONCESHIFT[] = {32};
uint32_t ASSETCHAINS_ALGO = ASSETCHAINS_EQUIHASH;
// min diff returned from GetNextWorkRequired needs to be added here for each algo, so they can work with ac_staked.
uint32_t ASSETCHAINS_MINDIFF[] = {537857807};
                                            // ^ wrong!

int32_t ASSETCHAINS_SAPLING = -1;
int32_t ASSETCHAINS_OVERWINTER = -1;

uint64_t KOMODO_INTERESTSUM,KOMODO_WALLETBALANCE;
int32_t ASSETCHAINS_STAKED;
uint64_t ASSETCHAINS_COMMISSION,ASSETCHAINS_SUPPLY = 10,ASSETCHAINS_FOUNDERS_REWARD;

uint32_t KOMODO_INITDONE;
char KMDUSERPASS[8192+512+1],BTCUSERPASS[8192]; uint16_t KMD_PORT = 7771,BITCOIND_RPCPORT = 7771, DEST_PORT;
unsigned int MAX_BLOCK_SIGOPS = 20000;

bool IS_KOMODO_TESTNODE;
int32_t KOMODO_SNAPSHOT_INTERVAL; 
CScript KOMODO_EARLYTXID_SCRIPTPUB;
int32_t ASSETCHAINS_EARLYTXIDCONTRACT;
int32_t ASSETCHAINS_STAKED_SPLIT_PERCENTAGE;
bool KOMODO_LOADINGBLOCKS = true;


std::map <std::int8_t, int32_t> mapHeightEvalActivate;

pthread_mutex_t KOMODO_CC_mutex;

char CURRENCIES[][8] = { "USD", "EUR", "JPY", "GBP", "AUD", "CAD", "CHF", "NZD", // major currencies
    "CNY", "RUB", "MXN", "BRL", "INR", "HKD", "TRY", "ZAR", "PLN", "NOK", "SEK", "DKK", "CZK", "HUF", "ILS", "KRW", "MYR", "PHP", "RON", "SGD", "THB", "BGN", "IDR", "HRK",
    "KMD" };

