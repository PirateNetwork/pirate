#include <gtest/gtest.h>

#include "chain.h"

namespace TestCChain
{

TEST(TestCChain, MutexTest)
{
    std::mutex my_mutex;
    MultithreadedCChain<std::mutex> my_chain(my_mutex);

    CBlockIndex *index = my_chain.LastTip();
    EXPECT_EQ(index, nullptr);

}

} // namespace TestCChain
