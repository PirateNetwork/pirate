#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <vector>
#include <string>

#include "util.h"
#include "chainparams.h"

#include "komodo_globals.h"
#include "main.h"
#include "primitives/transaction.h"
#include "core_io.h"
#include "komodo.h"


namespace fs = boost::filesystem;

namespace LegacyEventsTests {

    class LegacyEvents: public ::testing::Test {

        private:
            boost::filesystem::path pathDataDir;
            void printMessage(const std::string &message) {
                std::cout << "[          ] " << message;
            }
        public:
            LegacyEvents() : pathDataDir("") {}

            std::ostream& LogMessage() {
                std::cout << "[          ] ";
                return std::cout;
            }

            void SetUp( ) {

                /* Set environment for each test */
                pathDataDir.clear(); ClearDatadirCache();

                fPrintToConsole = true;
                fPrintToDebugLog = false;

                fs::path tempDir = fs::temp_directory_path();
                fs::path uniqueDir = tempDir / fs::unique_path();
                if (fs::create_directories(uniqueDir)) {
                    mapArgs["-datadir"] = uniqueDir.string();
                    pathDataDir = GetDataDir(false);
                }

                STAKED_NOTARY_ID = -1; // should be set via komodo_args call in real world
                SelectParams(CBaseChainParams::MAIN); // by default it's a CBaseChainParams::REGTEST, see ./src/test-komodo/main.cpp
                chainName = assetchain();

                komodo_setactivation(Consensus::NetworkUpgrade::ALWAYS_ACTIVE); // act as UpdateNetworkUpgradeParameters for regtest, to set sapling & overwinter activation height, but for mainnet

                KOMODO_REWIND = 0;
                chainActive.SetTip(nullptr);
            }

            void TearDown( ) {

                fPrintToDebugLog = true;
                fPrintToConsole = false;

                if (!pathDataDir.empty()) {
                    fs::remove_all(pathDataDir);
                }

                mapArgs.erase("-datadir");
                pathDataDir.clear();

                chainActive.SetTip(nullptr);
                komodo_setactivation(Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
                SelectParams(CBaseChainParams::REGTEST);
            }
    };

    TEST_F(LegacyEvents, PhantomOpReturnEvent) {

        int32_t fakeBlockHeight = 3507273;

        std::vector<std::string> vHexTxes = {
            /* 2b4a299ed7c9b3444b9bbfd091783f4f1d4b1b70987505de926312a4ba5d9026 */ "0400008085202f890de75353322bad6580d8d349b47f80362b73d7a391ebf76cb455c9a22da81162d51d000000484730440220436b3772c4f508b8ea0904b7c1c05b93b72dcbbfe284ef98c2314cc7761dd289022044207efe4d61201ba1c3fdaab9aa9b164bf9da983cfdc884532f2556d2e3332001ffffffffc2fb445d59fdf56fbdee6da85182626006e3965f6543b552408a439414784a8f0e00000048473044022053fb98f7e7a99398a2745ca6982e72a0cd29b6dc35eb7d267af60776399ad64c02206435029c413dc68227dcb4055e7f0ffd8cc53d40452d13ad8ad11e762e4b3b4301fffffffff25ba9881d55c5f7e52984ba7b8e708e0a9536335076301b74b8af648af88ba50400000049483045022100c591e4148e583a721e42dadf0c137253913a1613714580e76dcf8a59260d026902205655a76161b912a992e716341c12f43d4f74942c3105b2a4a385631287e3873301ffffffff861026dc3790e6c59292dd8db9581f317e0cf4137283668b7b1686a91291e9770a000000484730440220551301509286c016e05c1db5b6e2a052791cb7295b54cd00f396a05c0cf89dee02200450fee4174fa93b1b5e40b3ff8bc9baec19f45080c1f8c73d452d09783ddd8801ffffffff5703a4413921d8bf3b719678094419f46948b41e4bda01863c11c61f803649db020000004948304502210091c5ccade0b5e4174fbba895bc8105c2f1ca0149c5f8b071f0176b97147e20c00220563254719049becb4f8bb5a50faa737077d378ffc15e93a96f357228182ba5d201ffffffffaf9c7e4cff68ccacc7ae9f1087deaaf136e850738d43a7ed5054b37329df54910600000049483045022100c9561b0754a17f55c4b35d36e46df6d3845b83de88efd565d73bc50e83cbecba0220183865eb8446ad165733b9360d8ab9a0fdda7e73a3114d0feed7352c70d9972f01ffffffff1ed36a28c72c36710e4ae6849ee5f90511341c99f6a34235f150661f4445d2650000000049483045022100c7a7300841d671379f65075405eb85ee2930b417b5ede49ddb83d7717644cebd0220432e9aa745c65a0c30f408b5a9473dc38e1cb375eca5c640c7ed630444d503cf01fffffffff84acac4ddf0c87b9bbb40eadc485b891776153dff371872a4957ad1b40c07b50c0000004948304502210098d3c2b126f6f6022c6982c2a1ba600f575622911bdc1dff0a05fd093044340902207c50e568da145c751be8ded10bef6aabe1d4429f67253dbaac95242e2b8def2901ffffffff508000fa6bd1131fae49413a5ec8b1d3f479314e94527e39063532055aa8edde0700000049483045022100c7cb0a95afd3602ab07d2b9c1f390f3e81f438e555a8adf9fa52b810361920950220488ef1ef6b5fc50590b3d847fbffaaf9868c0f379750f0921acb73512d3a798901ffffffff4de7e7744f14114711defb6bb517bdf0b3edf53013d030da4d5ac7aad45542311300000049483045022100e6a4fe994cfbc028692eee048c4ed8d6dd9c63daa16c644bdd8ae72e3dcea56e02207597d0ceebb6e5a4a5d9802c5b9da2cf37a21f85f344b6acd8577b029410d21201ffffffffc14defef7bbd2dcf06502e18b8b0012fed83f5e644213401c8a1113501df1e6802000000494830450221009f414593f661a7b8c06defa585bdc428dac04234042357ac9a3b11108a26071d02204a862f72675dd8b46f9a1b93d44bd388eb819667bd3e7e7a30266491f8f5710501ffffffffd87d4c2d10fbf27c9800523e05bb43b2599bb9c8e9486b42b012f66c7a6f77ea13000000484730440220047c5eaabd6df36c8bfdd0737a367a12161629aa530c6012241cc36a88fa837d02207eda8879273887b954656e29ba1e98a7dce17cd66061d861e39a12521d6474fa01ffffffff774cdee54eda73eef85696f32c8f4ec8c03977e6536765ef0998482edd7c338e1d00000049483045022100cfb0758d9c1bcbb797d54ad0849b77055f1a2811b559ba2a59aaf38ac4cfb17802202d36f77b6bd9f41281937e39154708e58c82932386fdd8a75ef50d0feeff1c1701ffffffff02f0810100000000002321020e46e79a2a8d12b9b5d12c7a91adb4e454edfae43c0a0cb805427d2ac7613fd9ac00000000000000004a6a48a99470dba8fd1887b7f98e55aebbae41215c45667288662a4a529e70c9a283093c843500238ec4fea5df560a1fe88573a14c31f20c91e31d47ee9f12482bbf8f7a887f1f4b4d440000000000000000000000000000000000000000",
            /* c495d8b31491bf5ef3d5f123344b9596ac84d604ca509d7f982400f33a6fc7ec */ "0400008085202f8901a2efbc1573128738a37fa5638800357eb309db7aee22eef3c7825aa962a86b380600000048473044022073e8a265ac732ddd480d9ccfbade1f71c98976d91061bc7abeaae96665fd00dd0220299fd6a23e4ab5e4b299f7d29a2da9c87a893901975ed38722c69dd0126f8bd201ffffffff0288130000000000002321020e46e79a2a8d12b9b5d12c7a91adb4e454edfae43c0a0cb805427d2ac7613fd9ac0000000000000000226a20e9a57645780b4559dbefc76111e450a37937d8ce5d70b0f2063e88d1c7dbd24c00000000000000000000000000000000000000"
        };

        std::vector<CTransaction> txes;

        bool fAllTxesConverted = true;
        std::transform(vHexTxes.begin(), vHexTxes.end(), std::back_inserter(txes), [&fAllTxesConverted](const std::string& strHexTx) {
            CTransaction tx;
            fAllTxesConverted &= DecodeHexTx(tx, strHexTx);
            return tx;
        });

        ASSERT_TRUE(fAllTxesConverted);

        CBlock b;

        // Create coinbase transaction
        CMutableTransaction mtx;
        mtx.vin.push_back(CTxIn(COutPoint(), CScript() << OP_1, 0));
        mtx.vout.push_back(CTxOut(300000000, CScript() << ParseHex("031111111111111111111111111111111111111111111111111111111111111111") << OP_CHECKSIG));
        CTransaction tx(mtx);

        // Compose a block
        b.vtx.push_back(tx);
        b.vtx.insert(b.vtx.end(), txes.begin(), txes.end());

        CBlockIndex indexDummy(b);
        indexDummy.nHeight = fakeBlockHeight;
        //indexDummy.nTime = GetTime();
        chainActive.SetTip(&indexDummy);
        int32_t res_kcb = komodo_connectblock(false, &indexDummy, b);

        komodo_state *state_ptr = komodo_stateptrget((char *)chainName.symbol().c_str());

        ASSERT_TRUE(state_ptr != nullptr);
        ASSERT_TRUE(state_ptr->events.size() == 0);

        /*
            we shouldn't have matched == 1 on 3rd transaction in a block here, as a result,
            we shouldn't have komodo_voutupdate -> komodo_stateupdate -> write_event call
            and komodoevents file should be empty
        */

        uintmax_t stateFileSize = 0;
        fs::path filePath = GetDataDir(false) / KOMODO_STATE_FILENAME; // instead of komodo_statefname call
        if (fs::exists(filePath) && fs::is_regular_file(filePath)) {
            stateFileSize = fs::file_size(filePath);
        }

        ASSERT_TRUE(stateFileSize == 0);
    }

    TEST_F(LegacyEvents, NextUsefulTest) {

    }
}