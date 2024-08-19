// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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

#include "key_io.h"
#include "main.h"
#include "crypto/equihash.h"
#include "komodo_globals.h"
#include "util.h"
#include "utilstrencodings.h"

#include <cinttypes>
#include <assert.h>
#include <boost/assign/list_of.hpp>

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
    txNew.vin[0].scriptSig = CScript() << 520617983 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
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
 * >>> 'Zcash' + blake2s(b'The Economist 2016-10-29 Known unknown: Another crypto-currency is born. BTC#436254 0000000000000000044f321997f336d2908cf8c8d6893e88dbf067e2d949487d ETH#2521903 483039a6b6bd8bd05f0584f9a078d075e454925eb71c1f13eaff59b405a721bb DJIA close on 27 Oct 2016: 18,169.68').hexdigest()
 *
 * CBlock(hash=00040fe8, ver=4, hashPrevBlock=00000000000000, hashMerkleRoot=c4eaa5, nTime=1477641360, nBits=1f07ffff, nNonce=4695, vtx=1)
 *   CTransaction(hash=c4eaa5, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff071f0104455a6361736830623963346565663862376363343137656535303031653335303039383462366665613335363833613763616331343161303433633432303634383335643334)
 *     CTxOut(nValue=0.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: c4eaa5
 */
static CBlock CreateGenesisBlock(uint32_t nTime, const uint256& nNonce, const std::vector<unsigned char>& nSolution, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "Zcash0b9c4eef8b7cc417ee5001e3500984b6fea35683a7cac141a043c42064835d34";
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
void *chainparams_commandline();
#include "komodo_defs.h"
int32_t ASSETCHAINS_BLOCKTIME = 60;
uint64_t ASSETCHAINS_NK[2];

const arith_uint256 maxUint = UintToArith256(uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));

class CMainParams : public CChainParams {
public:
    CMainParams()
    {

        strNetworkID = "main";
        strCurrencyUnits = "KMD";
        bip44CoinType = 141; // As registered in https://github.com/satoshilabs/slips/blob/master/slip-0044.md 
        consensus.fCoinbaseMustBeProtected = false;
        consensus.nSubsidySlowStartInterval = 20000;
        consensus.nSubsidyHalvingInterval = 840000;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 4000;
        consensus.powLimit = uint256S("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
        consensus.powAlternate = uint256S("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
        consensus.nPowAveragingWindow = 17;
        consensus.nMaxFutureBlockTime = 7 * 60; // 7 mins

        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 32; // 32% adjustment down
        consensus.nPowMaxAdjustUp = 16; // 16% adjustment up
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.nPowAllowMinDifficultyBlocksAfterHeight = boost::none;
        consensus.nHF22Height = 2973410; /* nS6HardforkHeight + 7 * 1440 (~1 week) */
        consensus.nHF22NotariesPriorityRotateDelta = 20;
        assert(0 < consensus.nHF22NotariesPriorityRotateDelta);
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170005;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170007;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;

        // The best chain should have at least this much work.
        // if (chainName.isKMD()) {
        //     consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000"); // TODO: fill with real KMD nMinimumChainWork value
        // }

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xf9;
        pchMessageStart[1] = 0xee;
        pchMessageStart[2] = 0xe4;
        pchMessageStart[3] = 0x8d;
        vAlertPubKey = ParseHex("020e46e79a2a8d12b9b5d12c7a91adb4e454edfae43c0a0cb805427d2ac7613fd9");
        // (Zcash) vAlertPubKey = ParseHex("04b7ecf0baa90495ceb4e4090f6b2fd37eec1e9c85fac68a487f3ce11589692e4a317479316ee814e066638e1db54e37a10689b70286e6315b1087b6615d179264");
        nDefaultPort = 7770;
        nMinerThreads = 0;
        nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 100000;
        const size_t N = 200, K = 9;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        const char* pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";
        CMutableTransaction txNew;
        txNew.vin.resize(1);
        txNew.vout.resize(1);
        txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        txNew.vout[0].nValue = 50 * COIN;
        txNew.vout[0].scriptPubKey = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
        genesis.vtx.push_back(txNew);
        genesis.hashPrevBlock.SetNull();
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion = 1;
        genesis.nTime    = 1231006505;
        genesis.nBits    = KOMODO_MINDIFF_NBITS;
        genesis.nNonce   = uint256S("0x000000000000000000000000000000000000000000000000000000000000000b");
        genesis.nSolution = ParseHex("000d5ba7cda5d473947263bf194285317179d2b0d307119c2e7cc4bd8ac456f0774bd52b0cd9249be9d40718b6397a4c7bbd8f2b3272fed2823cd2af4bd1632200ba4bf796727d6347b225f670f292343274cc35099466f5fb5f0cd1c105121b28213d15db2ed7bdba490b4cedc69742a57b7c25af24485e523aadbb77a0144fc76f79ef73bd8530d42b9f3b9bed1c135ad1fe152923fafe98f95f76f1615e64c4abb1137f4c31b218ba2782bc15534788dda2cc08a0ee2987c8b27ff41bd4e31cd5fb5643dfe862c9a02ca9f90c8c51a6671d681d04ad47e4b53b1518d4befafefe8cadfb912f3d03051b1efbf1dfe37b56e93a741d8dfd80d576ca250bee55fab1311fc7b3255977558cdda6f7d6f875306e43a14413facdaed2f46093e0ef1e8f8a963e1632dcbeebd8e49fd16b57d49b08f9762de89157c65233f60c8e38a1f503a48c555f8ec45dedecd574a37601323c27be597b956343107f8bd80f3a925afaf30811df83c402116bb9c1e5231c70fff899a7c82f73c902ba54da53cc459b7bf1113db65cc8f6914d3618560ea69abd13658fa7b6af92d374d6eca9529f8bd565166e4fcbf2a8dfb3c9b69539d4d2ee2e9321b85b331925df195915f2757637c2805e1d4131e1ad9ef9bc1bb1c732d8dba4738716d351ab30c996c8657bab39567ee3b29c6d054b711495c0d52e1cd5d8e55b4f0f0325b97369280755b46a02afd54be4ddd9f77c22272b8bbb17ff5118fedbae2564524e797bd28b5f74f7079d532ccc059807989f94d267f47e724b3f1ecfe00ec9e6541c961080d8891251b84b4480bc292f6a180bea089fef5bbda56e1e41390d7c0e85ba0ef530f7177413481a226465a36ef6afe1e2bca69d2078712b3912bba1a99b1fbff0d355d6ffe726d2bb6fbc103c4ac5756e5bee6e47e17424ebcbf1b63d8cb90ce2e40198b4f4198689daea254307e52a25562f4c1455340f0ffeb10f9d8e914775e37d0edca019fb1b9c6ef81255ed86bc51c5391e0591480f66e2d88c5f4fd7277697968656a9b113ab97f874fdd5f2465e5559533e01ba13ef4a8f7a21d02c30c8ded68e8c54603ab9c8084ef6d9eb4e92c75b078539e2ae786ebab6dab73a09e0aa9ac575bcefb29e930ae656e58bcb513f7e3c17e079dce4f05b5dbc18c2a872b22509740ebe6a3903e00ad1abc55076441862643f93606e3dc35e8d9f2caef3ee6be14d513b2e062b21d0061de3bd56881713a1a5c17f5ace05e1ec09da53f99442df175a49bd154aa96e4949decd52fed79ccf7ccbce32941419c314e374e4a396ac553e17b5340336a1a25c22f9e42a243ba5404450b650acfc826a6e432971ace776e15719515e1634ceb9a4a35061b668c74998d3dfb5827f6238ec015377e6f9c94f38108768cf6e5c8b132e0303fb5a200368f845ad9d46343035a6ff94031df8d8309415bb3f6cd5ede9c135fdabcc030599858d803c0f85be7661c88984d88faa3d26fb0e9aac0056a53f1b5d0baed713c853c4a2726869a0a124a8a5bbc0fc0ef80c8ae4cb53636aa02503b86a1eb9836fcc259823e2692d921d88e1ffc1e6cb2bde43939ceb3f32a611686f539f8f7c9f0bf00381f743607d40960f06d347d1cd8ac8a51969c25e37150efdf7aa4c2037a2fd0516fb444525ab157a0ed0a7412b2fa69b217fe397263153782c0f64351fbdf2678fa0dc8569912dcd8e3ccad38f34f23bbbce14c6a26ac24911b308b82c7e43062d180baeac4ba7153858365c72c63dcf5f6a5b08070b730adb017aeae925b7d0439979e2679f45ed2f25a7edcfd2fb77a8794630285ccb0a071f5cce410b46dbf9750b0354aae8b65574501cc69efb5b6a43444074fee116641bb29da56c2b4a7f456991fc92b2");


        /*genesis = CreateGenesisBlock(
            1477641360,
            uint256S("0x0000000000000000000000000000000000000000000000000000000000001257"),
            ParseHex("000a889f00854b8665cd555f4656f68179d31ccadc1b1f7fb0952726313b16941da348284d67add4686121d4e3d930160c1348d8191c25f12b267a6a9c131b5031cbf8af1f79c9d513076a216ec87ed045fa966e01214ed83ca02dc1797270a454720d3206ac7d931a0a680c5c5e099057592570ca9bdf6058343958b31901fce1a15a4f38fd347750912e14004c73dfe588b903b6c03166582eeaf30529b14072a7b3079e3a684601b9b3024054201f7440b0ee9eb1a7120ff43f713735494aa27b1f8bab60d7f398bca14f6abb2adbf29b04099121438a7974b078a11635b594e9170f1086140b4173822dd697894483e1c6b4e8b8dcd5cb12ca4903bc61e108871d4d915a9093c18ac9b02b6716ce1013ca2c1174e319c1a570215bc9ab5f7564765f7be20524dc3fdf8aa356fd94d445e05ab165ad8bb4a0db096c097618c81098f91443c719416d39837af6de85015dca0de89462b1d8386758b2cf8a99e00953b308032ae44c35e05eb71842922eb69797f68813b59caf266cb6c213569ae3280505421a7e3a0a37fdf8e2ea354fc5422816655394a9454bac542a9298f176e211020d63dee6852c40de02267e2fc9d5e1ff2ad9309506f02a1a71a0501b16d0d36f70cdfd8de78116c0c506ee0b8ddfdeb561acadf31746b5a9dd32c21930884397fb1682164cb565cc14e089d66635a32618f7eb05fe05082b8a3fae620571660a6b89886eac53dec109d7cbb6930ca698a168f301a950be152da1be2b9e07516995e20baceebecb5579d7cdbc16d09f3a50cb3c7dffe33f26686d4ff3f8946ee6475e98cf7b3cf9062b6966e838f865ff3de5fb064a37a21da7bb8dfd2501a29e184f207caaba364f36f2329a77515dcb710e29ffbf73e2bbd773fab1f9a6b005567affff605c132e4e4dd69f36bd201005458cfbd2c658701eb2a700251cefd886b1e674ae816d3f719bac64be649c172ba27a4fd55947d95d53ba4cbc73de97b8af5ed4840b659370c556e7376457f51e5ebb66018849923db82c1c9a819f173cccdb8f3324b239609a300018d0fb094adf5bd7cbb3834c69e6d0b3798065c525b20f040e965e1a161af78ff7561cd874f5f1b75aa0bc77f720589e1b810f831eac5073e6dd46d00a2793f70f7427f0f798f2f53a67e615e65d356e66fe40609a958a05edb4c175bcc383ea0530e67ddbe479a898943c6e3074c6fcc252d6014de3a3d292b03f0d88d312fe221be7be7e3c59d07fa0f2f4029e364f1f355c5d01fa53770d0cd76d82bf7e60f6903bc1beb772e6fde4a70be51d9c7e03c8d6d8dfb361a234ba47c470fe630820bbd920715621b9fbedb49fcee165ead0875e6c2b1af16f50b5d6140cc981122fcbcf7c5a4e3772b3661b628e08380abc545957e59f634705b1bbde2f0b4e055a5ec5676d859be77e20962b645e051a880fddb0180b4555789e1f9344a436a84dc5579e2553f1e5fb0a599c137be36cabbed0319831fea3fddf94ddc7971e4bcf02cdc93294a9aab3e3b13e3b058235b4f4ec06ba4ceaa49d675b4ba80716f3bc6976b1fbf9c8bf1f3e3a4dc1cd83ef9cf816667fb94f1e923ff63fef072e6a19321e4812f96cb0ffa864da50ad74deb76917a336f31dce03ed5f0303aad5e6a83634f9fcc371096f8288b8f02ddded5ff1bb9d49331e4a84dbe1543164438fde9ad71dab024779dcdde0b6602b5ae0a6265c14b94edd83b37403f4b78fcd2ed555b596402c28ee81d87a909c4e8722b30c71ecdd861b05f61f8b1231795c76adba2fdefa451b283a5d527955b9f3de1b9828e7b2e74123dd47062ddcc09b05e7fa13cb2212a6fdbc65d7e852cec463ec6fd929f5b8483cf3052113b13dac91b69f49d1b7d1aec01c4a68e41ce157"),
            0x1f07ffff, 4, 0);*/

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x027e3758c3a65b12aa1046462b486d0a63bfa1beae327897f56c5cfb7daaae71"));
        assert(genesis.hashMerkleRoot == uint256S("0x4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));
        vFixedSeeds.clear();
        vSeeds.clear();

        vSeeds.push_back(CDNSSeedData("komodoseeds.org", "kmd.komodoseeds.org")); // decker
        vSeeds.push_back(CDNSSeedData("kmd.sh", "seeds1.kmd.sh")); // decker
        vSeeds.push_back(CDNSSeedData("cipig.net", "kmdseed.cipig.net")); // cipig
        vSeeds.push_back(CDNSSeedData("lordofthechains.com", "kmdseeds.lordofthechains.com")); // gcharang

        /*
        vSeeds.push_back(CDNSSeedData("komodoseeds.com", "kmd.komodoseeds.com"));
        vSeeds.push_back(CDNSSeedData("komodoseeds.com", "dynamic.komodoseeds.com"));
        */

        // TODO: we need more seed crawlers from other community members
        base58Prefixes[PUBKEY_ADDRESS] = {60};
        base58Prefixes[SCRIPT_ADDRESS] = {85};
        base58Prefixes[SECRET_KEY] =     {188};
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xb2, 0x1e};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xad, 0xe4};
        // guarantees the first two characters, when base58 encoded, are "zc"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {22,154};
        // guarantees the first 4 characters, when base58 encoded, are "ZiVK"
        base58Prefixes[ZCVIEWING_KEY]      = {0xA8,0xAB,0xD3};
        // guarantees the first two characters, when base58 encoded, are "SK"
        base58Prefixes[ZCSPENDING_KEY] = {171,54};

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "zs";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "zviews";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "zivks";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "secret-extended-key-main";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = {
            boost::assign::map_list_of

            (0, consensus.hashGenesisBlock)
                (	99996,	uint256S("0x00001d43f3cd902b726650d79f1d3376076aec3aa83d6ec8609b37e088f02ca4"))
                (	199996,	uint256S("0x000000b3c719380fbe28d4003407e1f90a7d97f6341eecb8616338eaafd36185"))
                (	299989,	uint256S("0x00000041a4c5bf30567db423184ec20075dbce6d151d46b31e035f6684acdd18"))
                (	399980,	uint256S("0x000000065a274c81e3a195ab3c4d790e7fdeaded326b99c340d7c55198c194d7"))
                (	499980,	uint256S("0x00000005327af413e0bc4106b4f47975ee8cb9cd4ed7aa14b99902878d231828"))
                (	599977,	uint256S("0x00000001c0cf63f839fc858adea9abebe89626b2292bb4414a7d000bb8479079"))
                (	699950,	uint256S("0x00000007bf5edb11fe746787187c9c2e2d79a930c6b73ca002728a758bfee0d7"))
                (	799950,	uint256S("0x00000006e3bd2516abfa2c28a0c43e409e80a47b5e37855127f080489217d870"))
                (	899944,	uint256S("0x0000000cb3ec2959a6fb86be280d9489516b570d507460253376cfa2a1db34c0"))
                (	999940,	uint256S("0x000000016236093f572ea8763b3869177de88af7973546a698af890892f367f0"))
                (	1099938,	uint256S("0x0000000132ff8041b594078eea64bb9e4982422f6f2f2ad64a961778e77f336d"))
                (	1199935,	uint256S("0x000000014e76ab19c236897eea8e8a9c36ccaf93fa95ac8c04b2005683a7871c"))
                (	1299931,	uint256S("0x000000019cf86423dae5791dd55cb8c829ee0d01c854a7b37b60e1e60aa8a27d"))
                (	1399924,	uint256S("0x00000000a2410bda797555a930cbe382dc716368e7f9a3ee2a7b68702e685fab"))
                (	1499920,	uint256S("0x0000000051f27f893a32d0cb9cefdabdebf3b539bfbb0e1ded30962cc4e99c0b"))
                (	1599912,	uint256S("0x0000000020d0f23fecbf43fbcaeaefdf40c4b37af2e7cb5574b24aca68c7d89d"))
                (	1699909,	uint256S("0x000000006b189a80864ca1c50a91c74d559b7aa21eb2b195af86a168a38ff2cb"))
                (	1799906,	uint256S("0x000000011ff34e5c43357210d843694bdc439c15c64001a7c716d54a78f5b859"))
                (	1899906,	uint256S("0x00000000e2ab10adfdce19edb2056e2c0f946e9b5c5f7d96d906002cacb3a4a5"))
                (	1999904,	uint256S("0x00000001547ed23c52dbe0092e4e5b8749add9ac6c57adb65a068fa7eb18b2f4"))
                (	2099904,	uint256S("0x00000000197f0a3dae6cc69bdf48784ad5b2b2408305e962d17cfca085461e12"))
                (	2199897,	uint256S("0x0000000035390b5df6ec3f73c5cedc4dec80332fcca544472a831b80a4156dbc"))
                (	2299889,	uint256S("0x000000007d392ad85c7f26cad3df8e596ed9af355a203d1f8e3148a24803db0c"))
                (	2399884,	uint256S("0x0000000005b0b444d3602772e9024111bf0755ff265f14247cec45919126c5b8"))
                (	2499877,	uint256S("0x000000001466519b75f99f5605e4773e95e8cd505ca53a3b00198d924e1299e6"))
                (	2599875,	uint256S("0x000000001337538f8949a42a990b29ea147a0675dbb5e787b143a6b489f73181"))
                (	2699873,	uint256S("0x000000005e57f0d0d4b11680895fa9a271732a7fbb1115dfdbff453a3500d8b1"))
                (	2799863,	uint256S("0x00000000a85071f74d829a74e8a112e87d360c3031dacbda60bdecd75e6707f0"))
                (	2899861,	uint256S("0x0000000098657a392c08b38d263f27e07bccb871d31db6234523b5ad4366508b"))
                (	2999840,	uint256S("0x00000000615b2e8f845725617c9dd5918908ed0f6ddee1db724bb82d7ecac750"))
                (	3099839,	uint256S("0x00000000b0d103f868c75c6c41dc6c51a7b870a41669fa4a7439dfbee5affa67"))
                (	3199837,	uint256S("0x000000009194906815f5da4a49b666d80f1c02502429657dde1b0cc24fdb5c38"))
                (	3299834,	uint256S("0x0000000013b608f3a1d54f3982a821740e71284a5d764256c0c8190ca57c4b41"))
                (	3399829,	uint256S("0x0000000032a670b9016ba8b0d928b3bd07a37c7b8a2203a27f0fac1ece8f21d5"))
                (	3499829,	uint256S("0x000000001983f76d0caea8e5d4274c4baf2580dae75388e88bb70c8e72ee585d"))
                (	3599829,	uint256S("0x000000002be75729f722a63d952c520623e5a153803f3550dbffb14c361ba85b"))
                (	3699819,	uint256S("0x0000000056ce0eb90b0072b497b9d10ab948e60d04afb046df12043e01c48169"))
                (	3799804,	uint256S("0x0000000021bde7352b4cb366c76b4a483354e297dd3bba069b9d9b72a17271f9"))
                (	3817031,	uint256S("0x00000000731ee33eecfb1a7ceb9de7f4576f95c724fc1ca3babce42c5c3c4171")),
            1708625223,     // * UNIX timestamp of last checkpoint block
            22693459,       // * total number of transactions between genesis and last checkpoint (1708625280)
                            //   (the tx=... number in the SetBestChain debug.log lines)
            2777            // * estimated number of transactions per day after checkpoint
                            //   total number of tx / (checkpoint block height / (24 * 24))
        };

        genesisNotaries = { 
            { "jl777_testA", "03b7621b44118017a16043f19b30cc8a4cfe068ac4e42417bae16ba460c80f3828" },
            { "jl777_testB", "02ebfc784a4ba768aad88d44d1045d240d47b26e248cafaf1c5169a42d7a61d344" },
            { "pondsea_SH", "02209073bc0943451498de57f802650311b1f12aa6deffcd893da198a544c04f36" },
            { "crackers_EU", "0340c66cf2c41c41efb420af57867baa765e8468c12aa996bfd816e1e07e410728" },
            { "pondsea_EU", "0225aa6f6f19e543180b31153d9e6d55d41bc7ec2ba191fd29f19a2f973544e29d" },
            { "locomb_EU", "025c6d26649b9d397e63323d96db42a9d3caad82e1d6076970efe5056c00c0779b" },
            { "fullmoon_AE", "0204a908350b8142698fdb6fabefc97fe0e04f537adc7522ba7a1e8f3bec003d4a" },
            { "movecrypto_EU", "021ab53bc6cf2c46b8a5456759f9d608966eff87384c2b52c0ac4cc8dd51e9cc42" },
            { "badass_EU", "0209d48554768dd8dada988b98aca23405057ac4b5b46838a9378b95c3e79b9b9e" },
            { "crackers_NA", "029e1c01131974f4cd3f564cc0c00eb87a0f9721043fbc1ca60f9bd0a1f73f64a1" },
            { "proto_EU", "03681ffdf17c8f4f0008cefb7fa0779c5e888339cdf932f0974483787a4d6747c1" }, // 10
            { "jeezy_EU", "023cb3e593fb85c5659688528e9a4f1c4c7f19206edc7e517d20f794ba686fd6d6" },
            { "farl4web_EU", "035caa40684ace968677dca3f09098aa02b70e533da32390a7654c626e0cf908e1" },
            { "nxtswe_EU", "032fb104e5eaa704a38a52c126af8f67e870d70f82977e5b2f093d5c1c21ae5899" },
            { "traderbill_EU", "03196e8de3e2e5d872f31d79d6a859c8704a2198baf0af9c7b21e29656a7eb455f" },
            { "vanbreuk_EU", "024f3cad7601d2399c131fd070e797d9cd8533868685ddbe515daa53c2e26004c3" }, // 15
            { "titomane_EU", "03517fcac101fed480ae4f2caf775560065957930d8c1facc83e30077e45bdd199" },
            { "supernet_AE", "029d93ef78197dc93892d2a30e5a54865f41e0ca3ab7eb8e3dcbc59c8756b6e355" },
            { "supernet_EU", "02061c6278b91fd4ac5cab4401100ffa3b2d5a277e8f71db23401cc071b3665546" },
            { "supernet_NA", "033c073366152b6b01535e15dd966a3a8039169584d06e27d92a69889b720d44e1" },
            { "yassin_EU", "033fb7231bb66484081952890d9a03f91164fb27d392d9152ec41336b71b15fbd0" }, // 20
            { "durerus_EU", "02bcbd287670bdca2c31e5d50130adb5dea1b53198f18abeec7211825f47485d57" },
            { "badass_SH", "026b49dd3923b78a592c1b475f208e23698d3f085c4c3b4906a59faf659fd9530b" },
            { "badass_NA", "02afa1a9f948e1634a29dc718d218e9d150c531cfa852843a1643a02184a63c1a7" },
            { "pondsea_NA", "031bcfdbb62268e2ff8dfffeb9ddff7fe95fca46778c77eebff9c3829dfa1bb411" },
            { "rnr_EU", "0287aa4b73988ba26cf6565d815786caf0d2c4af704d7883d163ee89cd9977edec" },
            { "crackers_SH", "02313d72f9a16055737e14cfc528dcd5d0ef094cfce23d0348fe974b6b1a32e5f0" },
            { "grewal_SH", "03212a73f5d38a675ee3cdc6e82542a96c38c3d1c79d25a1ed2e42fcf6a8be4e68" },
            { "polycryptoblock_NA", "02708dcda7c45fb54b78469673c2587bfdd126e381654819c4c23df0e00b679622" },
            { "titomane_NA", "0387046d9745414fb58a0fa3599078af5073e10347e4657ef7259a99cb4f10ad47" },
            { "titomane_AE", "03cda6ca5c2d02db201488a54a548dbfc10533bdc275d5ea11928e8d6ab33c2185" },
            { "kolo_EU", "03f5c08dadffa0ffcafb8dd7ffc38c22887bd02702a6c9ac3440deddcf2837692b" },
            { "artik_NA", "0224e31f93eff0cc30eaf0b2389fbc591085c0e122c4d11862c1729d090106c842" },
            { "eclips_EU", "0339369c1f5a2028d44be7be6f8ec3b907fdec814f87d2dead97cab4edb71a42e9" },
            { "titomane_SH", "035f49d7a308dd9a209e894321f010d21b7793461b0c89d6d9231a3fe5f68d9960" },
        };

    } // ctor
};

static CMainParams mainParams;

void CChainParams::SetCheckpointData(CChainParams::CCheckpointData checkpointData)
{
    CChainParams::checkpointData = checkpointData;
}

/*
 To change the max block size, all that needs to be updated is the #define _MAX_BLOCK_SIZE in utils.h
 
 However, doing that without any other changes will allow forking non-updated nodes by creating a larger block. So, make sure to height activate the new blocksize properly.
 
 Assuming it is 8MB, then:
 #define _OLD_MAX_BLOCK_SIZE (4096 * 1024)
 #define _MAX_BLOCK_SIZE (2 * 4096 * 1024)
 
 change the body of if:
 {
    if ( height < saplinght+1000000 ) // activates 8MB blocks 1 million blocks after saplinght
        return(_OLD_MAX_BLOCK_SIZE);
    else return(_MAX_BLOCK_SIZE);
 }

*/

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        strCurrencyUnits = "TKMD";
        bip44CoinType = 1;
        consensus.fCoinbaseMustBeProtected = true;
        consensus.nSubsidySlowStartInterval = 20000;
        consensus.nSubsidyHalvingInterval = 840000;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 400;
        consensus.powLimit = uint256S("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
        consensus.powAlternate = uint256S("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nMaxFutureBlockTime = 7 * 60;

        vAlertPubKey = ParseHex("00");
        nDefaultPort = 17770;
        nMinerThreads = 0;
        consensus.nPowMaxAdjustDown = 32; // 32% adjustment down
        consensus.nPowMaxAdjustUp = 16; // 16% adjustment up
        consensus.nPowTargetSpacing = 2.5 * 60;
        consensus.nPowAllowMinDifficultyBlocksAfterHeight = 299187;
        consensus.nHF22Height = boost::none;
        consensus.nHF22NotariesPriorityRotateDelta = 1;
        assert(0 < consensus.nHF22NotariesPriorityRotateDelta);
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170003;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = 207500;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170007;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = 280000;

        pchMessageStart[0] = 0x5A;
        pchMessageStart[1] = 0x1F;
        pchMessageStart[2] = 0x7E;
        pchMessageStart[3] = 0x62;
        vAlertPubKey = ParseHex("020e46e79a2a8d12b9b5d12c7a91adb4e454edfae43c0a0cb805427d2ac7613fd9");
        nMaxTipAge = 24 * 60 * 60;

        nPruneAfterHeight = 1000;
        const size_t N = 200, K = 9;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        //! Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis = CreateGenesisBlock(1296688602, 
                uint256S("0x000000000000000000000000000000000000000000000000000000000000000a"),
                ParseHex("008b6bd48ca3ef23bfa3d34885483158e089ad887539fd33950f2d78d5720e39769165aa7b2c679b65060e209249f54e3279e8bf31ec13781184b109aaee6e3db57260b466ce8182122b564ce43ca77b011ea1fa038f0139e98e923b0eb1929a80b622cdb72cd5505f275b7cf0e89892ff37f53b010f5ba1fb78bedead4a0c4d39f8319605d358e36a0a0e5e5cdb25a2ffff9320f57569f7270857e2d87287fa71c24d36611b2ac502ffffdbbe425ca71b09b1f0255a66f26356fae7f210227d79e3ea9fe99f7d5e5b05febdf3a54dfec02507bdb85ff409773ce56441191734059a11d9d4c481554fbe6c93b1a93be2cfc707d146e4c28966de5ced066fc85f548fd146c9cd086fafdbf982c3c099394e0a25a5e4670dea2673e84886f5fa765a8a5f1ff3a307680a20e520b1f3d21714eb3efca769f182106a6d193aae881461a64b55d98668eb7f7b92c3527eb75b044d01ffff427d9157c301e5b69fa09776009f53c30551484020fabbb3d664c106d72844b540c133bc67048ad4ca0082ad42848e146dac76b55e3ba51937c412c817034e1e67fb3d909347d42d198599f28df8ee0fa9bd9c180beb0fad03f265a8bbbfb6ce1bff1d8223c9ea28748983393fbf1b8364e449d331b8ffb8363dfab5728c5f34b1e4cd03e3a758c3e5280994a44a47fed5f84b13bc67df9074dac4b7288d927e1b8ef50a7afc01ea4b798d6025415f26d15dc506c96896b530af775fb3648ddf983f59bb10536e1e74a6bee4640ed3275bbdceb79520ec81618ac7087e06baba12432671e185b6e1706523edd26d07435bf5289c5f703f0b6703fbfc56e46b421ca9ca325e281387353daa33274925b44ea4a7c939ef13ec6f38941ed13c7a9ae5253dba2119a0b8b1401f73d503e2c7252dd9507d305cf9dcb5f3db29214bb6c7be8b5654421baefbdc7701408f5ab4d652879d54e4e4ad6dc3c1b49835ae7e2ca806e302d33657ada2c8b86b716a239d409fafdb05a547952f6aafd5d9dd60f76070b0ee12605815ad36fca1c3bc231b15428ce6412fd37d2f27255eb19b060aadf47e0c1b67b0911459505bc9fdfd1875fdac31362dd434ab4e297b9478b74f5efdaac35e7b3deb07b2125aaf07483dd104d6e43161506a0e1b14fd7a13b729f771ca5e2a5e53c2d6eb96f66a66a4581c87018fa98856630ab1dead01afbe62c280c697d96e6d056afb94a4cca2e6d63bc866f5dceb9a5d2a38b3bb11e109de75d17e6116aad2514b5ababe09f99ddf2c130cdd7866129fa103cdb2ec9d2a44853c8bf9c31201ec1b89cca7f31542da558f57645c4e897e9e9d1038be3a796eaf1cafa8f6d69897426c5e7b8f3933c004eb3113898ac5295fb31245494b63fdf5227ece5714a13469fd86ec944b8b9924cc67ab86561f73fdb3060c8acf9a255ca96834038ef1383f69733876bc7f2524ebe92eb01049bc6863835220a555e496bb17e7067d3427f209fb00a46e48082a549af2fdd23cc7cc0b96923fd695642389a1db1a457ac5874f7c5c62e407ba7a7248f04807c516c0ba5c08194d3f1b1fa78f0841f062529d5d9354091d8fb9fecb777df7bd3508174f66a13f1d7d272cd4145762b25841ae9c3e9351209ac43d2dcb542d4ccd64b19367b56d7772fed9b00630fe9567036fd4bb1d67d2665c12c2547fd4a112128512ea4bf1d9d1f68d421c3bde90d8c22cde1aa40a257a8a0089b9b4e8aff50fb2d41cf152be7ecc892ffaa22d162a50e1f24be74207756c46370531cf9f07094d789c8758f9260214cbe6463376cc6f5fb26211740a59a68a97d27bb7e152f91d0ff8f431d3569e08420d79e957df36d4e2c601406046df386abf944f19730acd2b4bbd715cd321c7f54c8e61bf2cf73019"),
                KOMODO_MINDIFF_NBITS, 1, COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = {0};
        base58Prefixes[SCRIPT_ADDRESS] = {5};
        base58Prefixes[SECRET_KEY] =     {128};
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {20,81};
        // guarantees the first 4 characters, when base58 encoded, are "ZiVt"
        base58Prefixes[ZCVIEWING_KEY]  = {0xA8,0xAC,0x0C};
        base58Prefixes[ZCSPENDING_KEY] = {177,235};

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "ztestsapling";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "zviewtestsapling";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "zivktestsapling";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "secret-extended-key-test";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        //fRequireRPCPassword = true;
        fMiningRequiresPeers = false;//true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        genesisNotaries = { 
                { "jmj_testA",   "037c032430fd231b5797cb4a637dae3eadf87b10274fd84be31670bd2a02c4fbc5" }
        };
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
        consensus.nSubsidySlowStartInterval = 0;
        consensus.nSubsidyHalvingInterval = 150;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.powLimit = uint256S("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
        consensus.powAlternate = uint256S("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
        consensus.nPowAveragingWindow = 17;
        consensus.nMaxFutureBlockTime = 7 * 60; // 7 mins
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 0; // Turn off adjustment down
        consensus.nPowMaxAdjustUp = 0; // Turn off adjustment up
        consensus.nPowTargetSpacing = 2.5 * 60;
        consensus.nPowAllowMinDifficultyBlocksAfterHeight = 0;
        consensus.nHF22Height = boost::none;
        consensus.nHF22NotariesPriorityRotateDelta = 1;
        assert(0 < consensus.nHF22NotariesPriorityRotateDelta);
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170003;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170006;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        coinbaseMaturity = 1;

        pchMessageStart[0] = 0xaa;
        pchMessageStart[1] = 0x8e;
        pchMessageStart[2] = 0xf3;
        pchMessageStart[3] = 0xf5;
        nMinerThreads = 1;
        nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 1000;
        const size_t N = 48, K = 5;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        genesis = CreateGenesisBlock(
            1296688602,
            uint256S("0x0000000000000000000000000000000000000000000000000000000000000009"),
            ParseHex("01936b7db1eb4ac39f151b8704642d0a8bda13ec547d54cd5e43ba142fc6d8877cab07b3"),
            KOMODO_MINDIFF_NBITS, 4, 0);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x029f11d80ef9765602235e1bc9727e3eb6ba20839319f761fee920d63401e327"));
        assert(genesis.hashMerkleRoot == uint256S("0xc4eaa58879081de3c24a7b117ed2b28300e7ec4c4c1dff1d3f1268b7857a4ddb"));

        nDefaultPort = 17779;
        nPruneAfterHeight = 1000;

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData){
            MapCheckpoints {
                    {0, uint256S("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")}
                },
            0,
            0,
            0
        };
        // These prefixes are the same as the testnet prefixes
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,60);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,85);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,188);
        //base58Prefixes[PUBKEY_ADDRESS]     = {0x1D,0x25};
        //base58Prefixes[SCRIPT_ADDRESS]     = {0x1C,0xBA};
        //base58Prefixes[SECRET_KEY]         = {0xEF};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x35,0x87,0xCF};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x35,0x83,0x94};
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x16,0xB6};
        base58Prefixes[ZCVIEWING_KEY]      = {0xA8,0xAC,0x0C};
        base58Prefixes[ZCSPENDING_KEY]     = {0xAC,0x08};

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "zregtestsapling";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "zviewregtestsapling";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "zivkregtestsapling";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "secret-extended-key-regtest";

        // Founders reward script expects a vector of 2-of-3 multisig addresses
        vFoundersRewardAddress = { "t2FwcEhFdNXuFMv1tcYwaBJtYVtMj8b1uTg" };
        assert(vFoundersRewardAddress.size() <= consensus.GetLastFoundersRewardBlockHeight());
    }

    void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight)
    {
        assert(idx > Consensus::BASE_SPROUT && idx < Consensus::MAX_NETWORK_UPGRADES);
        consensus.vUpgrades[idx].nActivationHeight = nActivationHeight;
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
    int maxHeight = consensus.GetLastFoundersRewardBlockHeight();
    assert(nHeight > 0 && nHeight <= maxHeight);

    size_t addressChangeInterval = (maxHeight + vFoundersRewardAddress.size()) / vFoundersRewardAddress.size();
    size_t i = nHeight / addressChangeInterval;
    return vFoundersRewardAddress[i];
}

// Block height must be >0 and <=last founders reward block height
// The founders reward address is expected to be a multisig (P2SH) address
CScript CChainParams::GetFoundersRewardScriptAtHeight(int nHeight) const {
    assert(nHeight > 0 && nHeight <= consensus.GetLastFoundersRewardBlockHeight());

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

int32_t MAX_BLOCK_SIZE(int32_t height)
{
    int32_t saplinght = pCurrentParams->consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight;
    //fprintf(stderr,"MAX_BLOCK_SIZE %d vs. %d\n",height,mainParams.consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight);
    if ( height <= 0 || (saplinght > 0 && height >= saplinght) )
    {
        return(_MAX_BLOCK_SIZE);
    }
    else return(2000000);
}

void komodo_setactivation(int32_t height)
{
    pCurrentParams->consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = height;
    pCurrentParams->consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = height;
    ASSETCHAINS_SAPLING = height;
    fprintf(stderr,"SET SAPLING ACTIVATION height.%d\n",height);
}

/* This function returns the checkpoint list for a currently active asset chain.
   pCurrentParams should be initialized by a SelectParams call before use. */
const CChainParams::CCheckpointData GetACCheckPoints()
{
    /* 1. The checkpoints utilized in this translation unit can be accessed using the get-checkpoints.py
      script from the komodo_scripts repository - https://github.com/DeckerSU/komodo_scripts .
       2. These checkpoints consist of valid PoW blocks, meaning they are not easy-mined.
    */

    /* Default */
    const CChainParams::CCheckpointData checkpointDataDefault = {
        MapCheckpoints{
            {0, Params().GetConsensus().hashGenesisBlock},
        },
        (int64_t)1231006505, /* nTimeLastCheckpoint */
        (int64_t)1,          /* nTransactionsLastCheckpoint */
        (double)2777         /* fTransactionsPerDay */
                             // * estimated number of transactions per day after checkpoint
                             //   total number of tx / (checkpoint block height / (24 * 24))
    };

    if (Params().NetworkIDString() != "main") {
        return checkpointDataDefault;
    }

    /* CCL */
    const CChainParams::CCheckpointData checkpointDataCCL = {
                boost::assign::map_list_of
                (	0,	Params(CBaseChainParams::MAIN).GetConsensus().hashGenesisBlock)
                (	100000,	uint256S("0x00b774a53512dc7ae3239e4df46bfac0fabac49522377c1dcac27b4093a3aab7"))
                (	200000,	uint256S("0x003b5d9c1bf4995eaf34799bec27ca707b0878340f5ba60f18f038ec0d4a36e6"))
                (	300000,	uint256S("0x0137fb3dc8ce3f53b9cabf2f503e0e61152c5e78ded9e6c191263ba107d75bcb"))
                (	400000,	uint256S("0x0221af5d6daafe322e083b23e3854776689fda3c6b1df68811257cd03ae88c10"))
                (	500000,	uint256S("0x020816b4eb6efc1d58990458b71c45df5191633fb8d1761e506dddccc956c2d5"))
                (	600000,	uint256S("0x00f6ca970821e94c4b4d7d177024898ec24b906abe9577fa41b0cf1c28ed2ab3"))
                (	700000,	uint256S("0x0948abb30b3d2f475585518abdd2f439e632a1ee1f10068f758b2be25b8db607"))
                (	800000,	uint256S("0x0945fd336f8d046bd2a21a04cf40ec47cc2ff78d0cca5d6ee41f40d01aca17f4"))
                (	900000,	uint256S("0x0ab6d14dd52de403552414ee83952bd2194cb620a7d2143b784f4947b8644a86"))
                (	1000000,	uint256S("0x00c6debdd76815713c0b778de89cb60fbb722403ae8109b878c44d3bb677ce03"))
                (	1100000,	uint256S("0x009a4babe02e2029792363b1126d91e883ef74b1a23e5cfa5166021e669d30f4"))
                (	1200000,	uint256S("0x02d8cebeadebfb37cc493df4d245ab8d97ccc355853eb191918ec0fc70a02962"))
                (	1300000,	uint256S("0x022b8fe2410f78e136d6ecf6b9076c3f7d128306d5a76e8f71c56e2dacb0406b"))
                (	1400000,	uint256S("0x00cd39f10bbb7e8514ffbff317acc665ced790f31f5d67d3bb73164f9c5d6a62"))
                (	1500000,	uint256S("0x00409256fdbbca1b6d9ed2687dcfc98f0fc4b94a872c3a63878015496f17ed2f"))
                (	1600000,	uint256S("0x01123bb5be90a0a602f5c18355184dd099ebd19a64c72eee476cdcbd0e8781f0"))
                (	1700000,	uint256S("0x00a1856b8c7efb60d5be9fff5face4b7e79eb77ede83c9cf0f79656089bc9e2c"))
                (	1714336,	uint256S("0x00e2d16eb526024c4e597d54a19c0a11c04fb1ca07254c1c50ab85cd00f19a5a")),
                1708625294,     // * UNIX timestamp of last checkpoint block
                2474803,       // * total number of transactions between genesis and last checkpoint (1708625294)
                                //   (the tx=... number in the SetBestChain debug.log lines)
                2777            // * estimated number of transactions per day after checkpoint
                                //   total number of tx / (checkpoint block height / (24 * 24))
                };
    /* CLC */
    const CChainParams::CCheckpointData checkpointDataCLC = {
                boost::assign::map_list_of
                (	0,	Params(CBaseChainParams::MAIN).GetConsensus().hashGenesisBlock)
                (	100000,	uint256S("0x0000053f20f1c77739f396be83657fa35c2d2bcaa6fa07becd006426a261e325"))
                (	200000,	uint256S("0x00380a7afe22013d8d4abe54fac00dfabab4db397e0b7312d890c97eadf91afe"))
                (	300000,	uint256S("0x0041a2ce54c05526a736357d99063a096596b6aa62dbc013c59dc503ff742c0f"))
                (	400000,	uint256S("0x0050265ce8ca282a15e3a45c1a71f4eb52ff64a0651cf4de9524b05648438623"))
                (	500000,	uint256S("0x00000b53432fda224b5091e42d228e35ff1ac34be900b453bd1a6ae041c1d5a7"))
                (	600000,	uint256S("0x00000aeceebe3741bc9fadc7fa18b613824015c4ce60518e73b7f726189021d3"))
                (	700000,	uint256S("0x0000093e819d49105ef4984eca60de2a3302e047c93bb92a89eab65ff6c8f938"))
                (	800000,	uint256S("0x000014c7ec17d7caeae8ba8ae9e653cab227cc800845e9123024cb586ee4c3c3"))
                (	855889,	uint256S("0x00003d809a88bfa42bd775b22a086c9d84d8f3064583a6e066c9280c2bbbf821")),
                1708625259,     // * UNIX timestamp of last checkpoint block
                1185905,       // * total number of transactions between genesis and last checkpoint (1708625259)
                                //   (the tx=... number in the SetBestChain debug.log lines)
                2777            // * estimated number of transactions per day after checkpoint
                                //   total number of tx / (checkpoint block height / (24 * 24))
                };
    /* DOC */
    const CChainParams::CCheckpointData checkpointDataDOC = {
                boost::assign::map_list_of
                (	0,	Params(CBaseChainParams::MAIN).GetConsensus().hashGenesisBlock)
                (	99985,	uint256S("0x0006acd728efa7959a634ee929fbc4c62aebdd94bf8ce1d7b0d496d1c7f51341"))
                (	199985,	uint256S("0x0003a7fd826164539f6edf4af4121720ca0bc1b76c5bed433470f596dcc6a8c1"))
                (	299966,	uint256S("0x000060b74e3a11be4e7664737e9854a1279794710afd534d4b26533aa728bb71"))
                (	399934,	uint256S("0x00058ce89982e6a32f25796ac334f6ae1ac61645380facb58aaca67417cc7857"))
                (	438259,	uint256S("0x00007ae89f7565de2be160621f547c3d37535065cb98596ed5bef799a9ab6f49")),
                1708620228,     // * UNIX timestamp of last checkpoint block
                744432,       // * total number of transactions between genesis and last checkpoint (1708625348)
                                //   (the tx=... number in the SetBestChain debug.log lines)
                2777            // * estimated number of transactions per day after checkpoint
                                //   total number of tx / (checkpoint block height / (24 * 24))
                };
    /* GLEEC */
    const CChainParams::CCheckpointData checkpointDataGLEEC = {
                boost::assign::map_list_of
                (	0,	Params(CBaseChainParams::MAIN).GetConsensus().hashGenesisBlock)
                (	97090,	uint256S("0x00008075af58a2d0a52ac480b31a74beb6b2f1261ae2984d29b8429ee4d86f9a"))
                (	197090,	uint256S("0x060c9061e48564a7c52547dcbd2a11f8ebfa69c54b438e6a28376c728ff394a8"))
                (	297090,	uint256S("0x034a9119f0ccf8d451670153322c9e9f0a6ee2218a46d68d718aae7ac04fc4b8"))
                (	397090,	uint256S("0x01aecb98552df137ca2a87ec5da9ddde39c440ab01a53487f14339a73af35aac"))
                (	497090,	uint256S("0x06f1ab2e988ca8bb01ac8b2c61db8683fd5f91cfd6dfb33801fc4e008d583d3c"))
                (	597090,	uint256S("0x0b5dfcc0a3a77b76c50f7a255e550933906dacbc9e8e010c437dff7bd274851e"))
                (	697090,	uint256S("0x09d5e46c9604e97d46a534ada0d3053534bc56a06d78c0591714c05c53e61c72"))
                (	797090,	uint256S("0x0d5f22caf6b6c369b31d6171bef0729c9de4a2e66058b822eff32c1c6fafe469"))
                (	800471,	uint256S("0x0d538185490353a8d9609d4119539b54880684e902b6bf2131ba0592f6fda0ff")),
                1708625402,     // * UNIX timestamp of last checkpoint block
                1932594,       // * total number of transactions between genesis and last checkpoint (1708625402)
                                //   (the tx=... number in the SetBestChain debug.log lines)
                2777            // * estimated number of transactions per day after checkpoint
                                //   total number of tx / (checkpoint block height / (24 * 24))
                };
    /* ILN */
    const CChainParams::CCheckpointData checkpointDataILN = {
                boost::assign::map_list_of
                (	0,	Params(CBaseChainParams::MAIN).GetConsensus().hashGenesisBlock)
                (	100000,	uint256S("0x0255f6eeb048be44e25b07c7e77546a0ccad281c1056ecc20db1470cdb593622"))
                (	200000,	uint256S("0x0a77f6222cae12b2b3cec2a8bfe1ca3ee996dcaf17f6a908113e732f81048c9a"))
                (	300000,	uint256S("0x02afcf16d001d3780402371605ab6d31f3fc9e737813c04020ba44dd0e9e7ffc"))
                (	400000,	uint256S("0x00e213def828888414e3a3ccd116c560b965c22d2c10b82d4f557dfeae658640"))
                (	500000,	uint256S("0x071f32d867fed4c6181a2c0fe31536d942b7828e2f9390134d5582e374d09632"))
                (	600000,	uint256S("0x0078e00f286f4b51522693ca4af38d05f1fe62f8b03eb559702a3ce7733f51c0"))
                (	700000,	uint256S("0x001237cf280d2c23870b7270009b79580220bb528c4dd69f475634e67c3139cc"))
                (	800000,	uint256S("0x08c0f16f4a33bb6aa8b390e2bd17cfa10730e77548d2db731482e34d739e41f2"))
                (	900000,	uint256S("0x01302fdea83dbce0c0b88ae75e553ca140d0458b7b29926a507989201a96ba92"))
                (	1000000,	uint256S("0x018375b59dab316301d884e1d2ff3e6ad8926d5bce19a61780307d274708b417"))
                (	1100000,	uint256S("0x0425072113955ada4cd10600182ca2bdbab4bf6c2e7f485ebf7db1eb754e95e0"))
                (	1200000,	uint256S("0x0916ff2a0361560862d8e53dc69fb34d500d650457e1b85bf60a9ae1ce5ca503"))
                (	1300000,	uint256S("0x0b1b14a7ff79af1748d69e5f26a25ed8fdffb0eae2e92ef9fb5e2e44bf7d5136"))
                (	1400000,	uint256S("0x00cfc16e6b69220b3a85ca852a6407c51e5f11ec3aa85d858ed897ff2253484f"))
                (	1500000,	uint256S("0x028a0de1129f988865d58d2dff96affff124be97757698cd347437fd0778a62c"))
                (	1600000,	uint256S("0x00cb4bfeb17143a9cff24e11242b4c9d0292f600b70701fd428580df00a4f83d"))
                (	1700000,	uint256S("0x02b68ab117120f770fb767003e2c0463f76c677e6acbbabc90ff6a9dfb909350"))
                (	1800000,	uint256S("0x0020a682fe9e65bf9820c36b4e01ac0e3c5aa6f775b0b1974a2fa07b09d517e3"))
                (	1900000,	uint256S("0x0021c436ba826416c7a6b3ebccd3677c577df77c37cea00cea501cf2f14d387e"))
                (	2000000,	uint256S("0x003e6641d94c46f0396910ed037a4e501df6c0cd2ae91261b21ee0c0b905bd0b"))
                (	2100000,	uint256S("0x016927a09b65dc8eb2bdf0e01c1b3a05bf73fb4bc02e1525a48f781bd932998f"))
                (	2121020,	uint256S("0x001e4e8f960407e16e284027e4226230f209185d399c98d743f53cfa100cdc1f")),
                1708625280,     // * UNIX timestamp of last checkpoint block
                3175260,       // * total number of transactions between genesis and last checkpoint (1708625280)
                                //   (the tx=... number in the SetBestChain debug.log lines)
                2777            // * estimated number of transactions per day after checkpoint
                                //   total number of tx / (checkpoint block height / (24 * 24))
                };
    /* KOIN */
    const CChainParams::CCheckpointData checkpointDataKOIN = {
                boost::assign::map_list_of
                (	0,	Params(CBaseChainParams::MAIN).GetConsensus().hashGenesisBlock)
                (	100000,	uint256S("0x001dfa68b6b0c22fb92ac2de5ece869946d39af99a5f8f56df6213edc8714792"))
                (	200000,	uint256S("0x029b1801a7ac2dcedf96ffd3aea3e750e17d4e90b0cfacf8213c6cbd58d77b88"))
                (	300000,	uint256S("0x005415331953e4ddc5a2293d663d4a077115c312b12caf7b7415a943c90dc3f0"))
                (	400000,	uint256S("0x0018ff60fb4934def322bbceddff4d0e68c4ddbdb749fc3ac7f7166908f76d5c"))
                (	500000,	uint256S("0x043e6e574db9b7ba7ee276cb552495966a3207b2ac19ffa4ad604738f33ec46d"))
                (	600000,	uint256S("0x08c115912995b92958706468c39cae61d8eb295759936c3d9072c676f7692a0a"))
                (	700000,	uint256S("0x03dfb5a33df2338b2aa84ce85718a79dd4b5fbc9806cfc2237b97c4b9f3d5843"))
                (	800000,	uint256S("0x034db19b0c1255c9a1907ed7f3d155985870325472966739e993382dd0d5237e"))
                (	900000,	uint256S("0x039508734f733c86df7863592d8f9ac5237edc351439b0265bf93eebf4647a13"))
                (	1000000,	uint256S("0x0189d3c90914e009efc86cd9af3a71f657531dbab040f6bf9c9fd15ba551a0bb"))
                (	1100000,	uint256S("0x00b52b6ed25202f1c03da9d742590f49ff1b7112dc654f770f4400ce39fb4f25"))
                (	1200000,	uint256S("0x012eed04f7cf3fbc6da0553b3d2511e654cd5e2122d325db648a2e3dcf830e4d"))
                (	1300000,	uint256S("0x01c87629cb85cf618561506ec0ee15bb3d1c8171a97005493e01f2fb217e87cd"))
                (	1400000,	uint256S("0x018716846747fb9953b2cb8d9903315f95d3662fe861d05971eb3b6edcfcdf67"))
                (	1500000,	uint256S("0x005376bd3da229f5c202876dcf3dca9e7a2e257262a09a6a82c472626d49ed2d"))
                (	1600000,	uint256S("0x00a4329390e85898bfb1bb974e2198cfac84ce50435c3a6ff98de66ba17a21cf"))
                (	1700000,	uint256S("0x0dd40b85431249574ad06705b8fa14d49d1823ce25eb5706082b038bb79aa9e1"))
                (	1790665,	uint256S("0x003bb396cd1dd5a4c1d92bee7153f7ad630fc8e6aa15ec64981baf73adc7fc5c")),
                1708625369,     // * UNIX timestamp of last checkpoint block
                2309748,       // * total number of transactions between genesis and last checkpoint (1708625369)
                                //   (the tx=... number in the SetBestChain debug.log lines)
                2777            // * estimated number of transactions per day after checkpoint
                                //   total number of tx / (checkpoint block height / (24 * 24))
                };
    /* MARTY */
    const CChainParams::CCheckpointData checkpointDataMARTY = {
                boost::assign::map_list_of
                (	0,	Params(CBaseChainParams::MAIN).GetConsensus().hashGenesisBlock)
                (	99975,	uint256S("0x0000a259f1d2e1137b837d9d311cae09bc7e78496cf1a8441cabb2f32bd75ed5"))
                (	199952,	uint256S("0x0001772dce8ce4eb9ebece31db12fedc740229feff108f08ee78f382fc3d8d2e"))
                (	299950,	uint256S("0x0006f6b4a0fbae53ad220a362cb6f297366e7969fbcae958466d0ee4f3aa8216"))
                (	399907,	uint256S("0x00017dd1264a1852fdcacbbd74acb5897f87ee3cb2f453262ac8ac7524cb29d0"))
                (	452802,	uint256S("0x0001ffc5074e925193d96191f97e8ad3f7736def2a6d252519aa10b6950edc97")),
                1708623455,     // * UNIX timestamp of last checkpoint block
                755009,       // * total number of transactions between genesis and last checkpoint (1708625233)
                                //   (the tx=... number in the SetBestChain debug.log lines)
                2777            // * estimated number of transactions per day after checkpoint
                                //   total number of tx / (checkpoint block height / (24 * 24))
                };
    /* NINJA */
    const CChainParams::CCheckpointData checkpointDataNINJA = {
                boost::assign::map_list_of
                (	0,	Params(CBaseChainParams::MAIN).GetConsensus().hashGenesisBlock)
                (	100000,	uint256S("0x05df9d478e2e8ea081f68cf2114bb9b5a1d1383f6a4405a53eed312bfbbe7df7"))
                (	200000,	uint256S("0x01ca78fe373896dc7dfb267d924d62d0321f7189be9344291417c0c4220d1e18"))
                (	300000,	uint256S("0x03048a4e64422115d6f21d85382b3921fe6d99b24bee8aafb61915fdffc060a2"))
                (	400000,	uint256S("0x01f1589d67aeda3516e4ab8a04877f3fdb32a1f89e4aa368378d17748a5b968e"))
                (	500000,	uint256S("0x004abd7b29b08b0efb400eb674c24460cea409302f553965d2e1ea9475c0fe15"))
                (	600000,	uint256S("0x063df7424d69c8d30b7dc3ba40376e36903d8e9c00e416881a8ff1edf91abe68"))
                (	700000,	uint256S("0x067a31b26ebd49029d898818d07f405897ccceb319275e40832740fac39521ba"))
                (	800000,	uint256S("0x045ec204280c1de956c9cf8ee3f4b6a9df3654c36193e68f200cc312306101da"))
                (	900000,	uint256S("0x00df8c9cec3f4ba5de90a5c2b4a443b01cf5cfcfa3508b6df4c55f348c0a21c3"))
                (	1000000,	uint256S("0x01e9f7a0cd28cf1879df91abf33d99b6bf3e1876b90b2e89ee45943bcdb5c6e0"))
                (	1100000,	uint256S("0x0196a3d0d38884cb6f59857aebef3933c154b3a2ef5a40ded54a7ee98e1f2818"))
                (	1200000,	uint256S("0x007a2491142314e7023a87d4baa63aca8b7e16a9e10455294d76314472209399"))
                (	1300000,	uint256S("0x00c97fa3da8387fb711d0ba29eaea4bc41c76e00d2a82896d3a30edd907c5aee"))
                (	1400000,	uint256S("0x0011af5cb9d2648efaee90309384ff66869fb6816e667fbac6a84c6e31108c26"))
                (	1500000,	uint256S("0x01082d03a4e9eb09a23803993b75cf67a1fd3037ef375e1437066e0d43b035ef"))
                (	1600000,	uint256S("0x0057ad1370f5abf7cd618c5d47a7514f464181bb92379b0474aa9276b87cd4fd"))
                (	1700000,	uint256S("0x0103d0a1e79f8cb2b94a59fc95101832c39ddbb7a971c8d6d545bdb3c7703fc2"))
                (	1743459,	uint256S("0x001e9a57238a7395ce61cf99903252d37f41246f6f0dad9e613a11ed6382ca92")),
                1708625349,     // * UNIX timestamp of last checkpoint block
                2187696,       // * total number of transactions between genesis and last checkpoint (1708625349)
                                //   (the tx=... number in the SetBestChain debug.log lines)
                2777            // * estimated number of transactions per day after checkpoint
                                //   total number of tx / (checkpoint block height / (24 * 24))
                };
    /* PIRATE */
    const CChainParams::CCheckpointData checkpointDataPIRATE = {
                boost::assign::map_list_of
                (	0,	Params(CBaseChainParams::MAIN).GetConsensus().hashGenesisBlock)
                (	100000,	uint256S("0x0000000085ba91a4ef72274aa1e549399e9cda0c3f7070ea612d2a45007bdfe7"))
                (	200000,	uint256S("0x0000000097934aab530005e99148224c3c863f2c785e4934645972dd7c611f70"))
                (	300000,	uint256S("0x000000001598dc03f64b36169a482dfa3d85a10d42cde06aa6cc6a75707389b2"))
                (	400000,	uint256S("0x000000001352818dcd2bc7e348492b3e56cb271c916f1b66a8a90ceda3d9fbe9"))
                (	500000,	uint256S("0x000000004a7eeb2b19dc893030a233e27be847597a274f65eab9a3234d296b81"))
                (	600000,	uint256S("0x00000000b260ab44b2f41b1bb759ae8b1b20022bd43a1c1240bb6bd3c83ee2e6"))
                (	700000,	uint256S("0x000000002db31fb7f11a33614ef482797cb3102d1e262aeda13b1de548a7eace"))
                (	800000,	uint256S("0x00000000c9a88eb08320fb87edd6e4bf3bfe94f51b0580e36229c781c50c8d4b"))
                (	900000,	uint256S("0x00000000df520299974b31272c8f3d8730a4bbc937ceead899fa97d0ce47282a"))
                (	1000000,	uint256S("0x000000005ba1dab60631d4215aaf814151b20a38f33fde0dca014fefcc2c85d4"))
                (	1100000,	uint256S("0x000000007fef990ae987b44d35bce4daae6a67d021bf853a38ecc4560cffd10c"))
                (	1200000,	uint256S("0x0000000015d119c13493372848b3ffbaeb8de486d6f28cd5040cea5af2cd3675"))
                (	1300000,	uint256S("0x000000001428f2e99327a47761b0da3d3b7a8921ca57765329bbad011348ec0e"))
                (	1400000,	uint256S("0x0000000000a011d156e423a6e9dcc96705c5b7157cc0efef12120ad26370f22a"))
                (	1500000,	uint256S("0x0000000003966e23bb72162a84d2ba0bd4ca5dbf12c4201e79629719725bc05e"))
                (	1600000,	uint256S("0x0000000002838d76f769cd7bb87bb196ebe0556750fb14b038eb149a31b5aeb3"))
                (	1700000,	uint256S("0x000000000a6999bede4901e9d07de66fafbd5a81c18b038d205960887b0b522a"))
                (	1800000,	uint256S("0x000000000e69ec04a4c9ebaba913f844d159c7f308da85626804f8ff295a7c74"))
                (	1900000,	uint256S("0x00000000176a6f8ebd5d3833345b0b9e68a389e2f1077071a72383b73b7f7944"))
                (	2000000,	uint256S("0x000000000c56ec1237cb98457510ee3144120e0af977d39ded7b34dd0c22f5ab"))
                (	2100000,	uint256S("0x00000000189c3d02a97c9ed45c194d5ae9a7723598af640f268cfdbf59a69cbb"))
                (	2200000,	uint256S("0x00000000215d10231183f53550314c915d3119bb3782ae717fca498f97c3b1e4"))
                (	2300000,	uint256S("0x000000002f81899792792c294ae4e313bd9e07b8522df23d5d49e6c13c8ca7d4"))
                (	2400000,	uint256S("0x0000000038f671599a462b3faa16e597b6bfd4f7e0dd78aa4707b0a0e828f70b"))
                (	2500000,	uint256S("0x00000000795ed0c786dbd6d55e36cba9ec3e490c665cfa2a5b28071a842d5c21"))
                (	2600000,	uint256S("0x00000000c135c9425c03741adef89d5d92474dcf1538ed426f02995b4f8d24e7"))
                (	2700000,	uint256S("0x000000008f6b0e73d13d20da78cfff7ccb72c13ac45be315176e924681a38b3a"))
                (	2798658,	uint256S("0x00000000d8edf022c326b06e89113a47b4fd037257c1d806d25238f8f5b86097")),
                1708625140,     // * UNIX timestamp of last checkpoint block
                5637396,       // * total number of transactions between genesis and last checkpoint (1708625140)
                                //   (the tx=... number in the SetBestChain debug.log lines)
                2777            // * estimated number of transactions per day after checkpoint
                                //   total number of tx / (checkpoint block height / (24 * 24))
                };
    /* SUPERNET */
    const CChainParams::CCheckpointData checkpointDataSUPERNET = {
                boost::assign::map_list_of
                (	0,	Params(CBaseChainParams::MAIN).GetConsensus().hashGenesisBlock)
                (	100000,	uint256S("0x0000254a74bc92b8a8f75b924213185471b30040a31a81fb4462f94edafd7464"))
                (	200000,	uint256S("0x0000150bb004f57d4dbf103a6edb03b02c6ef69743c2184a8d4cf58b3561dd4a"))
                (	300000,	uint256S("0x0000dfbbec9b156c99914bad855575c759b13c6c6a7cc90c6a16f0feda8b0315"))
                (	400000,	uint256S("0x0000341ed25c4bf8de762a71e1511ff5baba968b67b9a91bc65e17d331a0cf2d"))
                (	500000,	uint256S("0x00016d3ab7ce41d8221f111467b609e444d4151ce38e2578f2666a880022d6bf"))
                (	600000,	uint256S("0x05045f8d724c9898dd64bf08cfeac7ca3512f39d111e14cde9a4e049e97aa034"))
                (	700000,	uint256S("0x0004b23dba3990403d043f9f5383b989d260136119720a55337f952c61b8ccc5"))
                (	800000,	uint256S("0x0627e3a4d2dd894e469f7bd444a5914364a88c3bdd3d88465738cd53d1eb5b8b"))
                (	900000,	uint256S("0x035f2499db684311613f66bc2706ba2be6324994ff9bdc71914979f28e8fc700"))
                (	1000000,	uint256S("0x000395f12080d21ce9bbd57e1994fc97297b26782c16c10b066c3d9e2e9119ba"))
                (	1100000,	uint256S("0x034a8b2a09150f84132160d7bd3bd973d8fb8a43580b4b34ab568e683d0eb9d9"))
                (	1200000,	uint256S("0x000bf57b96834fcbf38c1e99119ba0b2844cf39f6447ca9d1c797875285922e2"))
                (	1300000,	uint256S("0x0132d2d843c731a8e2ecceea41929c56192125d8e7f0a16dc84174a2cccfa9dc"))
                (	1400000,	uint256S("0x07ccd8ffa17d991cf25a9b872a9e440104122fa285acdd183f2ddaea6b844bc2"))
                (	1500000,	uint256S("0x000117e0eacc69bb2e2520132dd31d53cfdec7f4819607e909b6f4c215a5bccc"))
                (	1600000,	uint256S("0x000c57f73465254c95b429190d38a2ba6017ba505ed0c56564475205d24d6918"))
                (	1700000,	uint256S("0x000069e23885d8271d58dc41d3c1791ffd21bec8df7b22fb6e4ffee0fe1a2490"))
                (	1800000,	uint256S("0x00003ea9afa40f32dad7d47a834b9aadfec9b0ddf1a9ff1e51115c5b3b4b6f17"))
                (	1900000,	uint256S("0x00003608d560fd9568f499ff982089f8773cc945ab111a7a47ab9d2056fce022"))
                (	2000000,	uint256S("0x00001f4d51be8518eedde1e59e9488d53f6906f17c44a1acf6d3c2f9d34268e2"))
                (	2100000,	uint256S("0x00002b97ef87249962f4818a27128aa188b245da70ff9bae3f51c74a07eded0a"))
                (	2200000,	uint256S("0x00003457994610bb49a2e2ad9075332eba76ac197d3b8c51865a4b3a6be41b5d"))
                (	2300000,	uint256S("0x004cd8a03fec35097cf0296a8ca94dff52e8f2dc7a9187ee1cb42a47a45cc1f1"))
                (	2400000,	uint256S("0x037a8a13e1c3e32bf3b9807253c81cd4f428821f9be32c98251aa7e06597fa57"))
                (	2500000,	uint256S("0x005b8d948859fdb041594004918813fea07bd2d79567456d3680db987f61f9d9"))
                (	2600000,	uint256S("0x006e0394a0b4c547a2e1ba845f672ed76d695c3cc2388fb28aeded5035aa8a1c"))
                (	2700000,	uint256S("0x00007f54311dd9001ade1bade3da7c4d86e3f5c2d26aa5bd99c955835e47f0f3"))
                (	2800000,	uint256S("0x008b33d01f17e9393beb95632d9639e546bb160a934e327ca8d760ebf49fcecc"))
                (	2900000,	uint256S("0x00d298970a22b43a3e3b63b93411d402dfaeb8e180cc9f8a04c1d71725bf5cab"))
                (	2922193,	uint256S("0x004a13d67a59af12889b2b4875f988a1ebc2bdd6ac0850fa2515f35d71888622")),
                1708625347,     // * UNIX timestamp of last checkpoint block
                3633150,       // * total number of transactions between genesis and last checkpoint (1708625347)
                                //   (the tx=... number in the SetBestChain debug.log lines)
                2777            // * estimated number of transactions per day after checkpoint
                                //   total number of tx / (checkpoint block height / (24 * 24))
                };

    /* should not be called with KMD, as the intention is to use it exclusively with AC, but anyway ... */
    const std::unordered_map<std::string, CChainParams::CCheckpointData> mapACCheckpointsData =
        boost::assign::map_list_of
        ("KMD", Params().Checkpoints())
        ("CCL", checkpointDataCCL)
        ("CLC", checkpointDataCLC)
        ("DOC", checkpointDataDOC)
        ("GLEEC", checkpointDataGLEEC)
        ("ILN", checkpointDataILN)
        ("KOIN", checkpointDataKOIN)
        ("MARTY", checkpointDataMARTY)
        ("NINJA", checkpointDataNINJA)
        ("PIRATE", checkpointDataPIRATE)
        ("SUPERNET", checkpointDataSUPERNET);

    auto it = mapACCheckpointsData.find(chainName.ToString());
    if (it != mapACCheckpointsData.end()) {
        return it->second;
    } else {
        return checkpointDataDefault;
    }
}

void *chainparams_commandline()
{
    CChainParams::CCheckpointData checkpointData;
    if ( !chainName.isKMD() )
    {
        if ( ASSETCHAINS_BLOCKTIME != 60 )
        {
            pCurrentParams->consensus.nMaxFutureBlockTime = 7 * ASSETCHAINS_BLOCKTIME; // 7 blocks
            pCurrentParams->consensus.nPowTargetSpacing = ASSETCHAINS_BLOCKTIME;
        }
        pCurrentParams->SetDefaultPort(ASSETCHAINS_P2PPORT);
        if ( ASSETCHAINS_NK[0] != 0 && ASSETCHAINS_NK[1] != 0 )
        {
            pCurrentParams->SetNValue(ASSETCHAINS_NK[0]);
            pCurrentParams->SetKValue(ASSETCHAINS_NK[1]);
        }
        if ( IS_KOMODO_TESTNODE )
            pCurrentParams->SetMiningRequiresPeers(false);
        if ( ASSETCHAINS_RPCPORT == 0 )
            ASSETCHAINS_RPCPORT = ASSETCHAINS_P2PPORT + 1;
        pCurrentParams->pchMessageStart[0] = ASSETCHAINS_MAGIC & 0xff;
        pCurrentParams->pchMessageStart[1] = (ASSETCHAINS_MAGIC >> 8) & 0xff;
        pCurrentParams->pchMessageStart[2] = (ASSETCHAINS_MAGIC >> 16) & 0xff;
        pCurrentParams->pchMessageStart[3] = (ASSETCHAINS_MAGIC >> 24) & 0xff;
        fprintf(stderr,">>>>>>>>>> %s: p2p.%u rpc.%u magic.%08x %u %u coins\n",chainName.symbol().c_str(),ASSETCHAINS_P2PPORT,ASSETCHAINS_RPCPORT,ASSETCHAINS_MAGIC,ASSETCHAINS_MAGIC,(uint32_t)ASSETCHAINS_SUPPLY);

        pCurrentParams->consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = ASSETCHAINS_SAPLING;
        pCurrentParams->consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = ASSETCHAINS_OVERWINTER;

        checkpointData = GetACCheckPoints();
    }
    else
    {
        checkpointData = pCurrentParams->Checkpoints();
    }

    /* launch with -debug=params to log additional data about chain params,
       fDebug is not set yet, so, we can't use LogAcceptCategory or LogPrint */
    if (!mapMultiArgs["-debug"].empty() && pCurrentParams != nullptr) {
        const std::vector<std::string>& categories = mapMultiArgs["-debug"];
        if (std::find(categories.begin(), categories.end(), std::string("params")) != categories.end()) {
            MapCheckpoints::reverse_iterator lastCheckpoint = checkpointData.mapCheckpoints.rbegin();
            if (lastCheckpoint != checkpointData.mapCheckpoints.rend())
                LogPrintf("Last checkpoint [%s]: height=%d, hash=%s\n", chainName.ToString(), lastCheckpoint->first, lastCheckpoint->second.ToString());
            else
                LogPrintf("Last checkpoint [%s]: MapCheckpoints is NOT set!\n", chainName.ToString());
            int32_t nMagic = 0;
            if (MESSAGE_START_SIZE <= sizeof(nMagic)) {
                for (size_t i = 0; i < MESSAGE_START_SIZE; ++i) {
                    nMagic = (nMagic << 8) | pCurrentParams->pchMessageStart[i];
                }
                nMagic = htobe32(nMagic);
                LogPrintf("MessageStart: %s (s.%" PRId32 ", u.%" PRIu32 ", 0x%08" PRIx32 ")\n",
                          HexStr(FLATDATA(pCurrentParams->pchMessageStart), true),
                          nMagic, static_cast<uint32_t>(nMagic), nMagic);
            }
        }
    }

    pCurrentParams->SetCheckpointData(checkpointData);

    ASSETCHAIN_INIT = 1;
    return(0);
}
