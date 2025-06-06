#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "crypto/equihash.h"
#include "uint256.h"
#include "util/strencodings.h"

// void TestExpandAndCompress(const std::string &scope, size_t bit_len, size_t byte_pad,
//                            std::vector<unsigned char> compact,
//                            std::vector<unsigned char> expanded)
// {
//     SCOPED_TRACE(scope);

//     std::vector<unsigned char> out(expanded.size());
//     ExpandArray(compact.data(), compact.size(),
//                 out.data(), out.size(), bit_len, byte_pad);
//     EXPECT_EQ(expanded, out);

//     out.resize(compact.size());
//     CompressArray(expanded.data(), expanded.size(),
//                   out.data(), out.size(), bit_len, byte_pad);
//     EXPECT_EQ(compact, out);
// }

// TEST(EquihashTests, ExpandAndContractArrays) {
//     TestExpandAndCompress("8 11-bit chunks, all-ones", 11, 0,
//                           ParseHex("ffffffffffffffffffffff"),
//                           ParseHex("07ff07ff07ff07ff07ff07ff07ff07ff"));
//     TestExpandAndCompress("8 21-bit chunks, alternating 1s and 0s", 21, 0,
//                           ParseHex("aaaaad55556aaaab55555aaaaad55556aaaab55555"),
//                           ParseHex("155555155555155555155555155555155555155555155555"));
//     TestExpandAndCompress("8 21-bit chunks, based on example in the spec", 21, 0,
//                           ParseHex("000220000a7ffffe00123022b38226ac19bdf23456"),
//                           ParseHex("0000440000291fffff0001230045670089ab00cdef123456"));
//     TestExpandAndCompress("16 14-bit chunks, alternating 11s and 00s", 14, 0,
//                           ParseHex("cccf333cccf333cccf333cccf333cccf333cccf333cccf333cccf333"),
//                           ParseHex("3333333333333333333333333333333333333333333333333333333333333333"));

//     TestExpandAndCompress("8 11-bit chunks, all-ones, 2-byte padding", 11, 2,
//                           ParseHex("ffffffffffffffffffffff"),
//                           ParseHex("000007ff000007ff000007ff000007ff000007ff000007ff000007ff000007ff"));
// }

// void TestMinimalSolnRepr(const std::string &scope, size_t cBitLen,
//                          std::vector<eh_index> indices,
//                          std::vector<unsigned char> minimal)
// {
//     SCOPED_TRACE(scope);

// #if ENABLE_MINING
//     EXPECT_EQ(indices, GetIndicesFromMinimal(minimal, cBitLen));
// #endif
//     EXPECT_EQ(minimal, GetMinimalFromIndices(indices, cBitLen));
// }

// TEST(EquihashTests, MinimalSolutionRepresentation) {
//     TestMinimalSolnRepr("Test 1", 20,
//                         {1, 1, 1, 1, 1, 1, 1, 1},
//                         ParseHex("000008000040000200001000008000040000200001"));
//     TestMinimalSolnRepr("Test 2", 20,
//                         {2097151, 2097151, 2097151, 2097151,
//                          2097151, 2097151, 2097151, 2097151},
//                         ParseHex("ffffffffffffffffffffffffffffffffffffffffff"));
//     TestMinimalSolnRepr("Test 3", 20,
//                         {131071, 128, 131071, 128, 131071, 128, 131071, 128},
//                         ParseHex("0ffff8002003fffe000800ffff8002003fffe00080"));
//     TestMinimalSolnRepr("Test 4", 20,
//                         {68, 41, 2097151, 1233, 665, 1023, 1, 1048575},
//                         ParseHex("000220000a7ffffe004d10014c800ffc00002fffff"));
// }

// #if ENABLE_MINING
// TEST(EquihashTests, IsProbablyDuplicate) {
//     std::shared_ptr<eh_trunc> p1 (new eh_trunc[4] {0, 1, 2, 3}, std::default_delete<eh_trunc[]>());
//     std::shared_ptr<eh_trunc> p2 (new eh_trunc[4] {0, 1, 1, 3}, std::default_delete<eh_trunc[]>());
//     std::shared_ptr<eh_trunc> p3 (new eh_trunc[4] {3, 1, 1, 3}, std::default_delete<eh_trunc[]>());

//     ASSERT_FALSE(IsProbablyDuplicate<4>(p1, 4));
//     ASSERT_FALSE(IsProbablyDuplicate<4>(p2, 4));
//     ASSERT_TRUE(IsProbablyDuplicate<4>(p3, 4));
// }

// TEST(EquihashTests, CheckBasicSolverCancelled) {
//     Equihash<48,5> Eh48_5;
//     eh_HashState state = Eh48_5.InitialiseState();
//     uint256 V = uint256S("0x00");
//     state.Update(V.begin(), V.size());

//     {
//         ASSERT_NO_THROW(Eh48_5.BasicSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return false;
//         }));
//     }

//     {
//         ASSERT_THROW(Eh48_5.BasicSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == ListGeneration;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.BasicSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == ListSorting;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.BasicSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == ListColliding;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.BasicSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == RoundEnd;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.BasicSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == FinalSorting;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.BasicSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == FinalColliding;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_NO_THROW(Eh48_5.BasicSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == PartialGeneration;
//         }));
//     }

//     {
//         ASSERT_NO_THROW(Eh48_5.BasicSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == PartialSorting;
//         }));
//     }

//     {
//         ASSERT_NO_THROW(Eh48_5.BasicSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == PartialSubtreeEnd;
//         }));
//     }

//     {
//         ASSERT_NO_THROW(Eh48_5.BasicSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == PartialIndexEnd;
//         }));
//     }

//     {
//         ASSERT_NO_THROW(Eh48_5.BasicSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == PartialEnd;
//         }));
//     }
// }

// TEST(EquihashTests, CheckOptimisedSolverCancelled) {
//     Equihash<48,5> Eh48_5;
//     eh_HashState state = Eh48_5.InitialiseState();
//     uint256 V = uint256S("0x00");
//     state.Update(V.begin(), V.size());

//     {
//         ASSERT_NO_THROW(Eh48_5.OptimisedSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return false;
//         }));
//     }

//     {
//         ASSERT_THROW(Eh48_5.OptimisedSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == ListGeneration;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.OptimisedSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == ListSorting;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.OptimisedSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == ListColliding;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.OptimisedSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == RoundEnd;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.OptimisedSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == FinalSorting;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.OptimisedSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == FinalColliding;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.OptimisedSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == PartialGeneration;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.OptimisedSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == PartialSorting;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.OptimisedSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == PartialSubtreeEnd;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.OptimisedSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == PartialIndexEnd;
//         }), EhSolverCancelledException);
//     }

//     {
//         ASSERT_THROW(Eh48_5.OptimisedSolve(state, [](std::vector<unsigned char> soln) {
//             return false;
//         }, [](EhSolverCancelCheck pos) {
//             return pos == PartialEnd;
//         }), EhSolverCancelledException);
//     }
// }
// #endif // ENABLE_MINING
