#include "key.h"
#include "base58.h"
#include "script/sigcache.h"
#include "chainparams.h"
#include "gtest/gtest.h"
#include "crypto/common.h"
#include "gtest/gtestutils.h"

struct ECCryptoClosure
{
    ECCVerifyHandle handle;
};

ECCryptoClosure instance_of_eccryptoclosure;

ZCJoinSplit* params;

int main(int argc, char **argv) {
    assert(init_and_check_sodium() != -1);
    ECC_Start();
    ECCVerifyHandle handle;  // Inits secp256k1 verify context

    //Load the Sapling parameters
    boost::filesystem::path sapling_spend = ZC_GetParamsDir() / "sapling-spend.params";
    boost::filesystem::path sapling_output = ZC_GetParamsDir() / "sapling-output.params";
    boost::filesystem::path sprout_groth16 = ZC_GetParamsDir() / "sprout-groth16.params";

    static_assert(
        sizeof(boost::filesystem::path::value_type) == sizeof(codeunit),
        "librustzcash not configured correctly");
    auto sapling_spend_str = sapling_spend.native();
    auto sapling_output_str = sapling_output.native();
    auto sprout_groth16_str = sprout_groth16.native();

    librustzcash_init_zksnark_params(
        reinterpret_cast<const codeunit*>(sprout_groth16_str.c_str()),
        sprout_groth16_str.length(),
        true
    );

    // Initialize the validity caches. We currently have three:
    // - Transparent signature validity.
    // - Sapling bundle validity.
    // - Orchard bundle validity.
    // Assign half of the cap to transparent signatures, and split the rest
    // between Sapling and Orchard bundles.
    size_t nMaxCacheSize = DEFAULT_MAX_SIG_CACHE_SIZE * ((size_t) 1 << 20);
    InitSignatureCache(nMaxCacheSize / 2);
    bundlecache::init(nMaxCacheSize / 4);


    SetupNetworking();
    SelectParams(CBaseChainParams::REGTEST);
    chainName = assetchain(); // KMD by default

    CBitcoinSecret vchSecret;
    // this returns false due to network prefix mismatch but works anyway
    vchSecret.SetString(notarySecret);
    notaryKey = vchSecret.GetKey();

    testing::InitGoogleTest(&argc, argv);
    
    auto ret = RUN_ALL_TESTS();

    ECC_Stop();
    return ret;
}
