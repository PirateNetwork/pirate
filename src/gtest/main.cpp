#include "gmock/gmock.h"
#include "crypto/common.h"
#include "key.h"
#include "pubkey.h"
#include "zcash/JoinSplit.hpp"
#include "util.h"

#include <libsnark/common/default_types/r1cs_ppzksnark_pp.hpp>
#include <libsnark/zk_proof_systems/ppzksnark/r1cs_ppzksnark/r1cs_ppzksnark.hpp>

#include "librustzcash.h"

struct ECCryptoClosure
{
    ECCVerifyHandle handle;
};

ECCryptoClosure instance_of_eccryptoclosure;

ZCJoinSplit* params;

int main(int argc, char **argv) {
  assert(init_and_check_sodium() != -1);
  ECC_Start();

  libsnark::default_r1cs_ppzksnark_pp::init_public_params();
  libsnark::inhibit_profiling_info = true;
  libsnark::inhibit_profiling_counters = true;
  boost::filesystem::path pk_path = ZC_GetParamsDir() / "sprout-proving.key";
  boost::filesystem::path vk_path = ZC_GetParamsDir() / "sprout-verifying.key";
  params = ZCJoinSplit::Prepared(vk_path.string(), pk_path.string());

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

  testing::InitGoogleMock(&argc, argv);

  auto ret = RUN_ALL_TESTS();

  ECC_Stop();
  return ret;
}
