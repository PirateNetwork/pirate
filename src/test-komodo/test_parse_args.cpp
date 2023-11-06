#include <gtest/gtest.h>
#include <map>
#include <string>

#include "komodo_globals.h"
#include "komodo_utils.h"
#include "main.h"

void chainparams_commandline();

namespace ParseArgumentsTests {

    struct assetchain_info {
        std::string name;
        uint16_t p2p_port, rpc_port;
        int32_t magic;
    };

    bool operator==(const assetchain_info& lhs, const assetchain_info& rhs)
    {
        return lhs.name == rhs.name &&
            lhs.p2p_port == rhs.p2p_port &&
            lhs.rpc_port == rhs.rpc_port &&
            lhs.magic == rhs.magic;
    }

    std::ostream& operator<<(std::ostream& os, const assetchain_info& ac) {

        os << "Name: " << ac.name
           << "\nP2P Port: " << ac.p2p_port
           << "\nRPC Port: " << ac.rpc_port
           << "\nMagic: 0x" << std::setfill('0') << std::setw(8) << std::hex << ac.magic;

        os << std::resetiosflags(std::ios::adjustfield);
        os << std::setiosflags(std::ios::dec);

        os << " (" << ac.magic << ")";

        // os << "{ \"" << ac.name << "\", {\"" << ac.name << "\", " << ac.p2p_port << ", " << ac.rpc_port << ", "
        //    << "0x" << std::setfill('0') << std::setw(8) << std::hex << ac.magic << "} },"
        //    << std::dec << std::endl;

        return os;
    }

    void ClearAssetchainGlobalParams() {

        ASSETCHAINS_RPCPORT = 0;

    }

    void SplitStrSpace(const std::string& strVal, std::vector<std::string> &outVals)
    {
        std::stringstream ss(strVal);

        while (!ss.eof()) {
            int c;
            std::string str;

            while (std::isspace(ss.peek()))
                ss.ignore();

            while ((c = ss.get()) != EOF && !std::isspace(c))
                str += c;

            if (!str.empty())
                outVals.push_back(str);
        }
    }

    class ParseArgumentsTests: public ::testing::Test {

        private:
            boost::filesystem::path pathDataDir;
            void printMessage(const std::string &message) {
                std::cout << "[          ] " << message;
            }
            void ClearKomodoGlobals() {
                mapArgs.clear();
                mapMultiArgs.clear();
            }
        public:
            ParseArgumentsTests() : pathDataDir("") {}

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

                mempool.clear();
                ClearKomodoGlobals();
                /* We want to ensure that global variables are cleared after the current test execution
                   because the next test that will be run may be different and may not use this fixture.
                   Therefore, we should provide clear globals for this test as well.
                */
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

                mempool.clear();
                ClearKomodoGlobals();
            }
    };

    TEST_F(ParseArgumentsTests, ParseCommandLineArgs) {

        const std::map<std::string, std::string> mapKnownAssetchains {
            {"CCL", "-ac_name=CCL -ac_supply=200000000 -ac_end=1 -ac_cc=2 -addressindex=1 -spentindex=1 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=142.93.136.89 -addnode=195.201.22.89 -addnode=15.235.204.174 -addnode=148.113.1.52 -addnode=65.21.77.109 -addnode=89.19.26.211 -addnode=89.19.26.212"},
            {"CLC", "-ac_name=CLC -ac_supply=99000000 -ac_reward=50000000 -ac_perc=100000000 -ac_founders=1 -ac_cc=45 -ac_public=1 -ac_snapshot=1440 -ac_pubkey=02df9bda7bfe2bcaa938b29a399fb0ba58cfb6cc3ddc0001062a600f60a8237ad9 -ac_adaptivepow=6 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=node.cryptocollider.com -addnode=15.235.204.174 -addnode=148.113.1.52 -addnode=65.21.77.109 -addnode=89.19.26.211 -addnode=89.19.26.212"},
            {"DOC", "-ac_name=DOC -ac_supply=90000000000 -ac_reward=100000000 -ac_cc=3 -ac_staked=10 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=65.21.77.109 -addnode=65.21.51.47 -addnode=15.235.204.174 -addnode=148.113.1.52 -addnode=65.21.77.109 -addnode=89.19.26.211 -addnode=89.19.26.212"},
            {"GLEEC", "-ac_name=GLEEC -ac_supply=210000000 -ac_public=1 -ac_staked=100 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=95.217.161.126 -addnode=15.235.204.174 -addnode=148.113.1.52 -addnode=65.21.77.109 -addnode=89.19.26.211 -addnode=89.19.26.212"},
            {"ILN", "-ac_name=ILN -ac_supply=10000000000 -ac_cc=2 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=51.75.122.83 -addnode=15.235.204.174 -addnode=148.113.1.52 -addnode=65.21.77.109 -addnode=89.19.26.211 -addnode=89.19.26.212"},
            {"KOIN", "-ac_name=KOIN -ac_supply=125000000 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=3.0.32.10 -addnode=15.235.204.174 -addnode=148.113.1.52 -addnode=65.21.77.109 -addnode=89.19.26.211 -addnode=89.19.26.212"},
            {"MARTY", "-ac_name=MARTY -ac_supply=90000000000 -ac_reward=100000000 -ac_cc=3 -ac_staked=10 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=65.21.77.109 -addnode=65.21.51.47 -addnode=15.235.204.174 -addnode=148.113.1.52 -addnode=65.21.77.109 -addnode=89.19.26.211 -addnode=89.19.26.212"},
            {"NINJA", "-ac_name=NINJA -ac_supply=100000000 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=15.235.204.174 -addnode=148.113.1.52 -addnode=65.21.77.109 -addnode=89.19.26.211 -addnode=89.19.26.212"},
            {"PIRATE", "-ac_name=PIRATE -ac_supply=0 -ac_reward=25600000000 -ac_halving=77777 -ac_private=1 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=88.99.212.81 -addnode=15.235.204.174 -addnode=148.113.1.52 -addnode=65.21.77.109 -addnode=89.19.26.211 -addnode=89.19.26.212"},
            {"SUPERNET", "-ac_name=SUPERNET -ac_supply=816061 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=15.235.204.174 -addnode=148.113.1.52 -addnode=65.21.77.109 -addnode=89.19.26.211 -addnode=89.19.26.212"},
            {"THC", "-ac_name=THC -ac_supply=251253103 -ac_reward=360000000,300000000,240000000,180000000,150000000,90000000,0 -ac_staked=100 -ac_eras=7 -ac_end=500001,1000001,1500001,2000001,2500001,4500001,0 -ac_perc=233333333 -ac_cc=2 -ac_ccenable=229,236,240 -ac_script=2ea22c8020987fad30df055db6fd922c3a57e55d76601229ed3da3b31340112e773df3d0d28103120c008203000401ccb8 -ac_founders=150 -ac_cbmaturity=1 -ac_sapling=1 -earlytxid=7e4a76259e99c9379551389e9f757fc5f46c33ae922a8644dc2b187af2a6adc1 -addnode=209.222.101.247 -addnode=103.195.100.32 -addnode=157.230.45.184 -addnode=165.22.52.123 -addnode=15.235.204.174 -addnode=148.113.1.52 -addnode=65.21.77.109 -addnode=89.19.26.211 -addnode=89.19.26.212"},
            {"TXX001", "-ac_name=TXX001 -ac_sapling=1 -ac_founders=1 -ac_reward=0,1125000000,562500000 -ac_end=128,340000,5422111 -ac_blocktime=150 -ac_supply=6178674 -ac_halving=129,340000,840000 -ac_cc=2 -ac_cclib=txx001 -ac_ccenable=228,234,235,236,241 -ac_perc=11111111 -ac_eras=3 -ac_script=76a9145eb10cf64f2bab1b457f1f25e658526155928fac88ac -clientname=GoldenSandtrout -addnode=188.165.212.101 -addnode=136.243.227.142 -addnode=5.9.224.250",}

        };

        const std::map<std::string, assetchain_info> mapAssetchainRefParams {
            { "CCL", {"CCL", 20848, 20849, 1728000348} },
            { "CLC", {"CLC", 20931, 20932, -671859365} },
            { "DOC", {"DOC", 62415, 62416, 1450148915} },
            { "GLEEC", {"GLEEC", 23225, 23226, 1824725725} },
            { "ILN", {"ILN", 12985, 12986, 600552702} },
            { "KOIN", {"KOIN", 10701, 10702, -1235858314} },
            { "MARTY", {"MARTY", 52592, 52593, 1663880092} },
            { "NINJA", {"NINJA", 8426, 8427, -1301311821} },
            { "PIRATE", {"PIRATE", 45452, 45453, 397860952} },
            { "SUPERNET", {"SUPERNET", 11340, 11341, -1190058922} },
            { "THC", {"THC", 36789, 36790, -1111205507} },
            { "TXX001", {"TXX001", 55965, 55966, 951479465} },
        };

        auto checkKeysEqual = [&]() -> bool {
            if (mapKnownAssetchains.size() != mapAssetchainRefParams.size()) {
                return false;
            }
            for(const auto& kv: mapKnownAssetchains) {
                // Checking if the key exists in the second map
                if (mapAssetchainRefParams.find(kv.first) ==  mapAssetchainRefParams.end()){
                    return false;
                }
            }
            return true;
        };

        ASSERT_TRUE(checkKeysEqual());

        const char program_name[] = "komodo-test";

        size_t argv0Len = std::strlen(program_name);
        std::unique_ptr<char[]> argv0Data(new char[argv0Len + 1]);
        std::strcpy(argv0Data.get(), program_name);

        for(const auto& pair : mapKnownAssetchains) {

            std::cerr << "Checking: [" << pair.first << "]" << std::endl;

            // split the given args string
            std::vector<std::string> vArgs;
            const std::string strArg = std::string(pair.second.c_str());
            SplitStrSpace(strArg, vArgs);
            vArgs.insert(vArgs.begin(), std::string(program_name));

            // and fill argc and argv
            size_t argc = vArgs.size();
            std::unique_ptr<const char *[]> argv(new const char *[argc + 2]);
            for (std::size_t i = 0; i != argc; ++i)
            {
                argv[i] = vArgs[i].c_str();
            }
            argv[vArgs.size()] = nullptr;

            ClearAssetchainGlobalParams();
            ParseParameters(argc, argv.get()); // before calling komodo_args -ac_name param should be set in mapArgs
            komodo_args(argv0Data.get());      // argv0 is passed in try to get ac_name from program suffixes (works for MNZ and BTCH only)
            chainparams_commandline();         // set CChainParams (pCurrentParams) from ASSETCHAINS_* global variables

            assetchain_info current_ac = {chainName.ToString(), ASSETCHAINS_P2PPORT, ASSETCHAINS_RPCPORT, static_cast<int32_t>(ASSETCHAINS_MAGIC)};
            const assetchain_info* ref_ac = nullptr;

            try {
                ref_ac = &mapAssetchainRefParams.at(pair.first);
            }
            catch (const std::out_of_range& e) {
                FAIL() << "Key does not exist in the map, caught out_of_range exception: " << e.what();
            }

            ASSERT_TRUE(ref_ac != nullptr);
            ASSERT_EQ(current_ac, *ref_ac);
        }
    }
}
