// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "key_io.h"
#include "main.h"
#include "crypto/equihash.h"

#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "base58.h"

using namespace std;

#include "chainparamsseeds.h"

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, const uint256& nNonce, const std::vector<unsigned char>& nSolution, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    // To create a genesis block for a new chain which is Overwintered:
    //   txNew.nVersion = OVERWINTER_TX_VERSION
    //   txNew.fOverwintered = true
    //   txNew.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID
    //   txNew.nExpiryHeight = <default value>

    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 537534368 << CScriptNum(4) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nSolution = nSolution;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = genesis.BuildMerkleTree();
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database (and is in any case of zero value).
 *
 * >>> from pyblake2 import blake2s
 * >>> 'ZERO' + blake2s(b'ZERO is born. BTC #453749 - 00000000000000000252b2fb7e477185e56228a4f5c31ff9e8b5604b88adbbbe').hexdigest()
 */
static CBlock CreateGenesisBlock(uint32_t nTime, const uint256& nNonce, const std::vector<unsigned char>& nSolution, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{

    const char* pszTimestamp = "ZEROe374a91d7fcdfe63b1c4662de6c76a5eb1d16f50a4f52ad6c71a541f550f83bb";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nSolution, nBits, nVersion, genesisReward);
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

const arith_uint256 maxUint = UintToArith256(uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        strCurrencyUnits = "ZER";
        bip44CoinType = 323; // As registered in https://github.com/satoshilabs/slips/blob/master/slip-0044.md
        consensus.fCoinbaseMustBeProtected = true;
        consensus.nFeeStartBlockHeight = 412300;
        consensus.nPreBlossomSubsidyHalvingInterval = Consensus::PRE_BLOSSOM_HALVING_INTERVAL;
        consensus.nPostBlossomSubsidyHalvingInterval = Consensus::POST_BLOSSOM_HALVING_INTERVAL;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 4000;

        const size_t N = 192, K = 7;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        consensus.nEquihashN = N;
        consensus.nEquihashK = K;
        consensus.powLimit = uint256S("0AB1Efffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 30; // 30% adjustment down
        consensus.nPowMaxAdjustUp = 10; // 10% adjustment up
        consensus.nPreBlossomPowTargetSpacing = Consensus::PRE_BLOSSOM_POW_TARGET_SPACING;
        consensus.nPostBlossomPowTargetSpacing = Consensus::POST_BLOSSOM_POW_TARGET_SPACING;
        consensus.nPowAllowMinDifficultyBlocksAfterHeight = boost::none;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170005;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = 492850;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].hashActivationBlock =
           uint256S("0000004c1c7583dfda0e37d1f52a00f0452dba3054444011c332858ad48b5e66");
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170007;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = 492850;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].hashActivationBlock =
            uint256S("0000004c1c7583dfda0e37d1f52a00f0452dba3054444011c332858ad48b5e66");
        consensus.vUpgrades[Consensus::UPGRADE_COSMOS].nProtocolVersion = 170008;
        consensus.vUpgrades[Consensus::UPGRADE_COSMOS].nActivationHeight = 620450;
        consensus.vUpgrades[Consensus::UPGRADE_COSMOS].hashActivationBlock =
           uint256S("00000049b61eca40af429e1552500116d40ea446d0271dce141af5d51956cb6e");
        consensus.vUpgrades[Consensus::UPGRADE_BLOSSOM].nProtocolVersion = 170009;
        consensus.vUpgrades[Consensus::UPGRADE_BLOSSOM].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000023297bef3cd");

        /**
         * The message start string should be awesome!
         */
        pchMessageStart[0] = 0x5A; // Z
        pchMessageStart[1] = 0x45; // E
        pchMessageStart[2] = 0x52; // R
        pchMessageStart[3] = 0x4F; // O
        vAlertPubKey = ParseHex("73B0");
        nDefaultPort = 23801;
        //nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 100000;

        //Start Zeronode
        nZeronodeCountDrift = 0;
        strSporkKey = "0477f4d5094e70c26bf998ba0d0e06af8c31b399c5b794895da2158dac086260353c50eaf477e7c5ec6b87349fc63bacdd56f0ffe4dcc112dca71d8335cd1ad2c1";
        strZeronodeDummyAddress = "t1TLNF3seMZennWmmxik8r1PVEKj5zudgRw";
        nBudget_Fee_Confirmations = 6; // Number of confirmations for the finalization fee
        //End Zeronode

        genesis = CreateGenesisBlock(
            1487500000,
            uint256S("4c697665206c6f6e6720616e642070726f7370657221014592005a64336e336b"),
            ParseHex("06aa402279cac1b6f8d0b364d09deee9f578ba95ac97dd02ce337b1e39a095efecab9f3572c41b6b3a3d4521e2ef4b278f8a16110778dd580c26c13673eaf4fb6ce4e0c725e60219c8372017226f74aa16932498ce82f0e2c61f0b7b8936ca528bd3e81223d5256c02556156c61a94c323a4618cb43d4596422ae65cdf37ae61d6d7965150dd9f7833166f705e804d4a2490b37fcbc528fa8660c0c52610a87db8b3a33bef0b51d3f23e6d7327643c2d2fe3363b1dea511b7c84a1e919f25f830a6eb6bdc9bcf07f080138765e8ec6d4081e6f5b824df3bcbc30fa1efa4df797160c4417ee94b7908bca17333c350622b333c0377dbcb7c28445164ad6290ca41f066e99d596024a703f78b5352ed6157d6f8f64379173b2fb27ef0e77b49cd6f218063ec846336f27a827fc0a181feb63b09786aa76cc5585e8bc57ad9cf9bb031d15387e119bdd629ef11b3e4af13b9ff1abf4ccfc98208ee823f3d0d7155c57d85088135250fb6befd2423fd169390129ffdb1dfead37a38bcc0f5c161c972cef5b58a88d6554877e33887d98b61b"),
            0x200A1FA<<4, 4, 0);

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x068cbb5db6bc11be5b93479ea4df41fa7e012e92ca8603c315f9b1a2202205c6"));
        assert(genesis.hashMerkleRoot == uint256S("0x094ef7f8882f3ec07edf16aa707c9511562b0e6211a8ed9db36332134bfe5357"));

        vFixedSeeds.clear();
        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("zerocurrency0", "seed0.zerocurrency.io"));
        vSeeds.push_back(CDNSSeedData("zerocurrency1", "seed1.zerocurrency.io"));
        vSeeds.push_back(CDNSSeedData("zerocurrency2", "seed2.zerocurrency.io"));
        vSeeds.push_back(CDNSSeedData("zerocurrency3", "seed3.zerocurrency.io"));
        vSeeds.push_back(CDNSSeedData("zerocurrency4", "seed4.zerocurrency.io"));
        vSeeds.push_back(CDNSSeedData("zerocurrency5", "seed5.zerocurrency.io"));
        vSeeds.push_back(CDNSSeedData("zerocurrency6", "seed6.zerocurrency.io"));
        vSeeds.push_back(CDNSSeedData("zerocurrency7", "seed7.zerocurrency.io"));
        vSeeds.push_back(CDNSSeedData("zerocurrency8", "seed8.zerocurrency.io"));
        vSeeds.push_back(CDNSSeedData("zerocurrency9", "seed9.zerocurrency.io"));

        // guarantees the first 2 characters, when base58 encoded, are "t1"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x1C,0xB8};
        // guarantees the first 2 characters, when base58 encoded, are "t3"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x1C,0xBD};
        // the first character, when base58 encoded, is "5" or "K" or "L" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0x80};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x88,0xB2,0x1E};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x88,0xAD,0xE4};
        // guarantees the first 2 characters, when base58 encoded, are "zc"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x16,0x9A};
        // guarantees the first 4 characters, when base58 encoded, are "ZiVK"
        base58Prefixes[ZCVIEWING_KEY]      = {0xA8,0xAB,0xD3};
        // guarantees the first 2 characters, when base58 encoded, are "SK"
        base58Prefixes[ZCSPENDING_KEY]     = {0xAB,0x36};

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "zs";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "zviews";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "zivks";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "secret-extended-key-main";

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            ( 0, consensus.hashGenesisBlock)
            ( 60000, uint256S("0x00002ae1f476c997f3800d6c7fc733efaa0fc6172d91075724a60a9fd42dcf3a"))
            ( 140000, uint256S("0x00000eb02cca5b05f4be1ee4016790e5b1a3817eb12b1aba605024602665ce7e"))
            ( 200000, uint256S("0x0000090d945b23a43b757d57f8f396ac748861f22bf1d8914cba440fd59a5c43"))
            ( 300000, uint256S("0x000001e0b15c1f74af391c39bcb2d61ea879238b53ba738a45303cccd84c2c3f"))
            ( 400000, uint256S("0x000001ebcc62c257dd00d70bb7fbc210580875e8dbc9f1f9c9aafdb4dc1d8a4e"))
            ( 500000, uint256S("0x000000c642fda400a464c50dcf310e65efa2627799b9b0c378524205ba2307b7"))
            ( 600000, uint256S("0x000000a0e6efed6a536c2c5063c666102fb2f9ddb4a226b34a0894c723519e48"))
            ( 700000, uint256S("0x000001bee8c5bc29fb8d6c3642662ecb479ec85df648e851edaaa0e090b2c797")),
            1571551061,     // * UNIX timestamp of last checkpoint block
            1376076,         // * total number of transactions between genesis and last checkpoint
                                        //   (the tx=... number in the SetBestChain debug.log lines)
            1132            // * estimated number of transactions per day after checkpoint
                                        //   total number of tx / (checkpoint block height / (24 * 24))
        };

        // Hardcoded fallback value for the Sprout shielded value pool balance
        // for nodes that have not reindexed since the introduction of monitoring
        // in #2795.
        nSproutValuePoolCheckpointHeight = 492850;
        nSproutValuePoolCheckpointBalance = 7517901832365;
        fZIP209Enabled = true;
        hashSproutValuePoolCheckpointBlock = uint256S("0000004c1c7583dfda0e37d1f52a00f0452dba3054444011c332858ad48b5e66");

        // Founders reward script expects a vector of 2-of-3 multisig addresses
        vFoundersRewardAddress = {
          "t3hmg6WApjqVFw9oPWTDy4JLEqXcUWthg5v", /* main-index: 0*/
          "t3hrh5M7eaGA5zXCitPXz2pbe146GkVPWHs", /* main-index: 1*/
          "t3aWmHqBGS7watoKQLa7uykeTaYHoYqM361", /* main-index: 2*/
          "t3hsi89hPsZzmnbs3pny6cfAxMxV5TJLErj", /* main-index: 3*/
          "t3TdGxPVUdMXd6qDrDCEuJETLadZ9Ki3s9r", /* main-index: 4*/
          "t3cb5ZjKmbGbqDaYk97Auam9kXXikGQBmyY", /* main-index: 5*/
          "t3V1YovGUPW9WSBoAHS48FDdUfUTo6LDpZR", /* main-index: 6*/
          "t3KB9n28MVg31oo856t1tQGfJuYq8usTvSi", /* main-index: 7*/
          "t3dqSV4YGj5V3WjQhqFGrKTMUf9Tgc6xnJM", /* main-index: 8*/
          "t3aJkYT1i6tyytq8J6khPaDNtgZsBSXgfBf", /* main-index: 9*/

        };
        assert(vFoundersRewardAddress.size() <= consensus.GetLastFoundersRewardBlockHeight(0));
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        strCurrencyUnits = "ZET";
        bip44CoinType = 1;
        consensus.fCoinbaseMustBeProtected = true;
        consensus.nFeeStartBlockHeight = 1;
        consensus.nPreBlossomSubsidyHalvingInterval = Consensus::PRE_BLOSSOM_HALVING_INTERVAL;
        consensus.nPostBlossomSubsidyHalvingInterval = Consensus::POST_BLOSSOM_HALVING_INTERVAL;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 400;

        const size_t N = 192, K = 7;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        consensus.nEquihashN = N;
        consensus.nEquihashK = K;
        consensus.powLimit = uint256S("0effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 30; // 30% adjustment down
        consensus.nPowMaxAdjustUp = 10; // 10% adjustment up
        consensus.nPreBlossomPowTargetSpacing = Consensus::PRE_BLOSSOM_POW_TARGET_SPACING;
        consensus.nPostBlossomPowTargetSpacing = Consensus::POST_BLOSSOM_POW_TARGET_SPACING;
        consensus.nPowAllowMinDifficultyBlocksAfterHeight = boost::none;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170005;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = 50;
        // consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].hashActivationBlock =
            // uint256S("0000257c4331b098045023fcfbfa2474681f4564ab483f84e4e1ad078e4acf44");
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170007;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = 50;
        // consensus.vUpgrades[Consensus::UPGRADE_SAPLING].hashActivationBlock =
        //     uint256S("000420e7fcc3a49d729479fb0b560dd7b8617b178a08e9e389620a9d1dd6361a");
        consensus.vUpgrades[Consensus::UPGRADE_COSMOS].nProtocolVersion = 170008;
        consensus.vUpgrades[Consensus::UPGRADE_COSMOS].nActivationHeight =47925;
        // consensus.vUpgrades[Consensus::UPGRADE_COSMOS].hashActivationBlock =
        //     uint256S("00367515ef2e781b8c9358b443b6329572599edd02c59e8af67db9785122f298");
        consensus.vUpgrades[Consensus::UPGRADE_BLOSSOM].nProtocolVersion = 170009;
        consensus.vUpgrades[Consensus::UPGRADE_BLOSSOM].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        // consensus.vUpgrades[Consensus::UPGRADE_BLOSSOM].hashActivationBlock =
        //     uint256S("00367515ef2e781b8c9358b443b6329572599edd02c59e8af67db9785122f298");


        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        pchMessageStart[0] = 0x5B; // Z+1
        pchMessageStart[1] = 0x46; // E+1
        pchMessageStart[2] = 0x53; // R+1
        pchMessageStart[3] = 0x50; // O+1
        vAlertPubKey = ParseHex("73B0");
        nDefaultPort = 23802;
        //nMaxTipAge = 1000000000;
        nPruneAfterHeight = 1000;

        //Start Zeronode
        nZeronodeCountDrift = 0;
        strSporkKey = "04f249a25f6708898afead4e01fc726269ffbdcbbecad7f675ed2470f68571e57ac32bde7111781e476b0c0256cc5e7b71cc5fd56fcffbfb1ead0cb6fe89d91303";
        strZeronodeDummyAddress = "tmWuQ8Yh3pHDa8MingmN8ECPRBxo2n8uZRs";
        nBudget_Fee_Confirmations = 6; // Number of confirmations for the finalization fee
        //End Zeronode

        genesis = CreateGenesisBlock(
            1542244402,
            uint256S("0000000000000000000000000000000000000000000000000000000000000007"),
            ParseHex("0112369e83d7c1667babefb9901f494ca75088c6e3cd4231a1099789c7bc2125da167d562261ec5d856c45614a28578a378a118ef63d77c1d5bf8b159d26d2649f21d31d5cdbc31ac2317c31072ade90a0e557e4f6b5f45718c997080ea65d63a7d90cc50160bf20d536abbac91af665d11e1f69c068556e5094f0edaa1bf7ac44ce072456237a18cae937477fc3590ecb0babff50511712877c99a4d29ed4d7d99d32f7cc221c67f541df6eec718f20dc90aadec4908541dc09be52f135f244b55df32031fb00b808916c99ee23072fade4657287a9b6168178c30e746de5bff7143f955b534179d1bf1d6724a1a42845f7ff389322a4c416913cf95e6ee36252df00d803347433815d58eced61974300aa734d1fc533cb9ce09a0ffa83f405d56937ac283a70c7b5804c260e43c349d2a30ebb3e58de7e033d0a7290da6390bbf9f56dd53c1d7f3886471214c50a66fb394145ee891ecb71cc7bc3f44022609fcc97ca0e58755d89d513b9e8c5ed9b393b6655f17ba1371f6bb1ff3651cb1737f0cc18d89d5d940ab2fd3f9dcd8ca6"),
            0x200A1FA<<4, 4, 0);
        consensus.hashGenesisBlock = genesis.GetHash();

        //LogPrintf("TESTNET\n");
        //LogPrintf("consensus.hashGenesisBlock=%s\n", consensus.hashGenesisBlock.GetHex());
        //LogPrintf("genesis.hashMerkleRoot=%s\n", genesis.hashMerkleRoot.GetHex());
        //LogPrintf("genesis.nTime=%is\n", genesis.nTime);

        assert(consensus.hashGenesisBlock == uint256S("09c59b03b8d80af0d0e05737fd3ccbd29cfa389fd17395ae5c99143c9e3ad434"));
        assert(genesis.hashMerkleRoot == uint256S("094ef7f8882f3ec07edf16aa707c9511562b0e6211a8ed9db36332134bfe5357"));

        vFixedSeeds.clear();
        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("testnet1", "testnet1.zerocurrency.io"));
        vSeeds.push_back(CDNSSeedData("testnet2", "testnet2.zerocurrency.io"));

        // guarantees the first 2 characters, when base58 encoded, are "tm"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x1D,0x25};
        // guarantees the first 2 characters, when base58 encoded, are "t2"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x1C,0xBA};
        // the first character, when base58 encoded, is "9" or "c" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0xEF};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x35,0x87,0xCF};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x35,0x83,0x94};
        // guarantees the first 2 characters, when base58 encoded, are "zt"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x16,0xB6};
        // guarantees the first 4 characters, when base58 encoded, are "ZiVt"
        base58Prefixes[ZCVIEWING_KEY]      = {0xA8,0xAC,0x0C};
        // guarantees the first 2 characters, when base58 encoded, are "ST"
        base58Prefixes[ZCSPENDING_KEY]     = {0xAC,0x08};

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "ztestsapling";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "zviewtestsapling";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "zivktestsapling";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "secret-extended-key-test";

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;


        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (0, consensus.hashGenesisBlock),
            genesis.nTime,  // * UNIX timestamp of last checkpoint block
            0,       // * total number of transactions between genesis and last checkpoint
                     //   (the tx=... number in the SetBestChain debug.log lines)
            0        //   total number of tx / (checkpoint block height / (24 * 24))
          };

        // Hardcoded fallback value for the Sprout shielded value pool balance
        // for nodes that have not reindexed since the introduction of monitoring
        // in #2795.
        //nSproutValuePoolCheckpointHeight = 440329;
        //nSproutValuePoolCheckpointBalance = 40000029096803;
        //fZIP209Enabled = true;
        //hashSproutValuePoolCheckpointBlock = uint256S("000a95d08ba5dcbabe881fc6471d11807bcca7df5f1795c99f3ec4580db4279b");


        // Founders reward script expects a vector of 2-of-3 multisig addresses
        vFoundersRewardAddress = {
          "t2BEnZwurNtPyhyWdZ82zTdS93rKyoUpgMJ",
          "t2AwNRubry4rQrEvHwAdpYve4Gz5cSmjGXA",
          "t2FM2EZqQZANA18rbhBGLWbxKcpGk9soAvS",
          "t2TBZfnXRCmFPc3nSrQcNtnR6AGWNzF9hzz",
          "t2QMyS1C3jXJ5hcR7zJTPSbWFvMWQEyjVTE",
          "t2SxA3E34aZJDDxnUxC2gZXBVrXWuoH5v7W",
          "t2FhR8STLvuKTUPgM586T1LMUG4JAHV2XSt",
          "t2NScRwr3FHPN8fNSgMKn8ngZG2kgfC8JNt",
          "t2LYZj3LiCDaCGUXQNXH1ZFPmHiC46wB9se",
          "t28azskSALVTtgbW63CqDwzXgtWQ56GVVXa"
        };
        assert(vFoundersRewardAddress.size() <= consensus.GetLastFoundersRewardBlockHeight(0));
    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        strCurrencyUnits = "REG";
        bip44CoinType = 1;
        consensus.fCoinbaseMustBeProtected = false;
        consensus.nFeeStartBlockHeight = 5000;
        consensus.nPreBlossomSubsidyHalvingInterval = Consensus::PRE_BLOSSOM_REGTEST_HALVING_INTERVAL;
        consensus.nPostBlossomSubsidyHalvingInterval = Consensus::POST_BLOSSOM_REGTEST_HALVING_INTERVAL;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        const size_t N = 48, K = 5;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        consensus.nEquihashN = N;
        consensus.nEquihashK = K;
        consensus.powLimit = uint256S("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 0; // Turn off adjustment down
        consensus.nPowMaxAdjustUp = 0; // Turn off adjustment up
        consensus.nPreBlossomPowTargetSpacing = Consensus::PRE_BLOSSOM_POW_TARGET_SPACING;
        consensus.nPostBlossomPowTargetSpacing = Consensus::POST_BLOSSOM_POW_TARGET_SPACING;
        consensus.nPowAllowMinDifficultyBlocksAfterHeight = 0;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170005;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170007;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_COSMOS].nProtocolVersion = 170008;
        consensus.vUpgrades[Consensus::UPGRADE_COSMOS].nActivationHeight =
                Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_BLOSSOM].nProtocolVersion = 170009;
        consensus.vUpgrades[Consensus::UPGRADE_BLOSSOM].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;


        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        pchMessageStart[0] = 0x5C; // Z+2
        pchMessageStart[1] = 0x47; // E+2
        pchMessageStart[2] = 0x54; // R+2
        pchMessageStart[3] = 0x51; // O+2
        vAlertPubKey = ParseHex("73B0");
        nDefaultPort = 23803;
        //nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 1000;

        //Start Zeronode
        nZeronodeCountDrift = 0;
        strSporkKey = "045da9271f5d9df405d9e83c7c7e62e9c831cc85c51ffaa6b515c4f9c845dec4bf256460003f26ba9d394a17cb57e6759fe231eca75b801c20bccd19cbe4b7942d";
        strZeronodeDummyAddress = "s1eQnJdoWDhKhxDrX8ev3aFjb1J6ZwXCxUT";
        nBudget_Fee_Confirmations = 6; // Number of confirmations for the finalization fee
        //End Zeronode

        genesis = CreateGenesisBlock(
            1531037936,
            uint256S("0000000000000000000000000000000000000000000000000000000000000001"),
            ParseHex("09354815a3ad96efa233c6edbff6d3a245490c12d71971cf2969791411cd11132fcec3e8"),
            0x200f0f0f, 4, 0);
        consensus.hashGenesisBlock = genesis.GetHash();

        //LogPrintf("REGTEST\n");
        //LogPrintf("consensus.hashGenesisBlock=%s\n", consensus.hashGenesisBlock.GetHex());
        //LogPrintf("genesis.hashMerkleRoot=%s\n", genesis.hashMerkleRoot.GetHex());
        //LogPrintf("genesis.nTime=%is\n", genesis.nTime);

        assert(consensus.hashGenesisBlock == uint256S("0275863eebea76f824674494b7f6a3770ac46c732aaa62b07328feaa9d79798b"));
        //assert(genesis.hashMerkleRoot == uint256S(""));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        // guarantees the first 2 characters, when base58 encoded, are "tm"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x1D,0x25};
        // guarantees the first 2 characters, when base58 encoded, are "t2"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x1C,0xBA};
        // the first character, when base58 encoded, is "9" or "c" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0xEF};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x35,0x87,0xCF};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x35,0x83,0x94};
        // guarantees the first 2 characters, when base58 encoded, are "zt"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x16,0xB6};
        // guarantees the first 4 characters, when base58 encoded, are "ZiVt"
        base58Prefixes[ZCVIEWING_KEY]      = {0xA8,0xAC,0x0C};
        // guarantees the first 2 characters, when base58 encoded, are "ST"
        base58Prefixes[ZCSPENDING_KEY]     = {0xAC,0x08};

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "zregtestsapling";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "zviewregtestsapling";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "zivkregtestsapling";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "secret-extended-key-regtest";

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData){
            boost::assign::map_list_of
            ( 0, consensus.hashGenesisBlock),
            genesis.nTime,
            0,
            0
        };

        // Founders reward script expects a vector of 2-of-3 multisig addresses
        vFoundersRewardAddress = { "t2FwcEhFdNXuFMv1tcYwaBJtYVtMj8b1uTg" };
        assert(vFoundersRewardAddress.size() <= consensus.GetLastFoundersRewardBlockHeight(0));
    }

    void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight)
    {
        assert(idx > Consensus::BASE_SPROUT && idx < Consensus::MAX_NETWORK_UPGRADES);
        consensus.vUpgrades[idx].nActivationHeight = nActivationHeight;
    }

    void UpdateRegtestPow(int64_t nPowMaxAdjustDown, int64_t nPowMaxAdjustUp, uint256 powLimit)
    {
        consensus.nPowMaxAdjustDown = nPowMaxAdjustDown;
        consensus.nPowMaxAdjustUp = nPowMaxAdjustUp;
        consensus.powLimit = powLimit;
    }

    void SetRegTestZIP209Enabled() {
        fZIP209Enabled = true;
    }
};
static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams &Params(CBaseChainParams::Network network) {
    switch (network) {
        case CBaseChainParams::MAIN:
            return mainParams;
        case CBaseChainParams::TESTNET:
            return testNetParams;
        case CBaseChainParams::REGTEST:
            return regTestParams;
        default:
            assert(false && "Unimplemented network");
            return mainParams;
    }
}

void SelectParams(CBaseChainParams::Network network) {
    SelectBaseParams(network);
    pCurrentParams = &Params(network);

    // Some python qa rpc tests need to enforce the coinbase consensus rule
    if (network == CBaseChainParams::REGTEST && mapArgs.count("-regtestprotectcoinbase")) {
        regTestParams.SetRegTestCoinbaseMustBeProtected();
    }

    // When a developer is debugging turnstile violations in regtest mode, enable ZIP209
    if (network == CBaseChainParams::REGTEST && mapArgs.count("-developersetpoolsizezero")) {
        regTestParams.SetRegTestZIP209Enabled();
    }
}

bool SelectParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectParams(network);
    return true;
}


// Block height must be >0 and <=last founders reward block height
// Index variable i ranges from 0 - (vFoundersRewardAddress.size()-1)
std::string CChainParams::GetFoundersRewardAddressAtHeight(int nHeight) const {
    int maxHeight = consensus.GetLastFoundersRewardBlockHeight(nHeight);
    assert(nHeight >= consensus.nFeeStartBlockHeight && nHeight <= maxHeight);

    size_t addressChangeInterval = (maxHeight + vFoundersRewardAddress.size()) / vFoundersRewardAddress.size();
    size_t i = nHeight / addressChangeInterval;
    return vFoundersRewardAddress[i];
}

// Block height must be >0 and <=last founders reward block height
// The founders reward address is expected to be a multisig (P2SH) address
CScript CChainParams::GetFoundersRewardScriptAtHeight(int nHeight) const {
    assert(nHeight >= consensus.nFeeStartBlockHeight && nHeight <= consensus.GetLastFoundersRewardBlockHeight(nHeight));

    CTxDestination address = DecodeDestination(GetFoundersRewardAddressAtHeight(nHeight).c_str());
    assert(IsValidDestination(address));
    assert(boost::get<CScriptID>(&address) != nullptr);
    CScriptID scriptID = boost::get<CScriptID>(address); // address is a boost variant
    CScript script = CScript() << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
    return script;
}

std::string CChainParams::GetFoundersRewardAddressAtIndex(int i) const {
    assert(i >= 0 && i < vFoundersRewardAddress.size());
    return vFoundersRewardAddress[i];
}

void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight)
{
    regTestParams.UpdateNetworkUpgradeParameters(idx, nActivationHeight);
}

void UpdateRegtestPow(int64_t nPowMaxAdjustDown, int64_t nPowMaxAdjustUp, uint256 powLimit) {
    regTestParams.UpdateRegtestPow(nPowMaxAdjustDown, nPowMaxAdjustUp, powLimit);
}
