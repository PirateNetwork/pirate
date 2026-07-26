// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gtest/gtestutils.h"
#include "txdb.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

ShieldedSubtreeData MakeSubtreeData(uint64_t index)
{
    libzcash::SubtreeRoot root;
    root.fill(0);
    root[0] = static_cast<uint8_t>(index);
    root[1] = static_cast<uint8_t>(index >> 8);

    uint256 blockHash;
    blockHash.begin()[0] = static_cast<uint8_t>(index);
    blockHash.begin()[1] = static_cast<uint8_t>(index >> 8);

    return ShieldedSubtreeData(root, static_cast<int>(1000 + index), blockHash);
}

void CheckSubtreeRange(
    const std::vector<std::pair<uint64_t, ShieldedSubtreeData>>& subtrees,
    uint64_t firstIndex,
    size_t expectedSize)
{
    ASSERT_EQ(subtrees.size(), expectedSize);
    for (size_t offset = 0; offset < expectedSize; offset++) {
        const uint64_t index = firstIndex + offset;
        EXPECT_EQ(subtrees[offset].first, index);
        EXPECT_TRUE(subtrees[offset].second.root == MakeSubtreeData(index).root);
        EXPECT_EQ(subtrees[offset].second.nHeight, static_cast<int>(1000 + index));
        EXPECT_TRUE(subtrees[offset].second.blockHash == MakeSubtreeData(index).blockHash);
    }
}

} // namespace

class txdb_tests_bitcoin : public BitcoinBasicTestingSetup {};

TEST_F(txdb_tests_bitcoin, shielded_subtree_reads_use_numeric_index_order)
{
    CBlockTreeDB db(1 << 20, true, true);

    for (ShieldedType type : {SAPLINGFRONTIER, IRONWOODFRONTIER}) {
        std::vector<std::pair<uint64_t, ShieldedSubtreeData>> written;
        for (uint64_t index = 0; index < 300; index++) {
            written.push_back(std::make_pair(index, MakeSubtreeData(index)));
        }
        ASSERT_TRUE(db.WriteShieldedSubtrees(type, written));

        std::vector<std::pair<uint64_t, ShieldedSubtreeData>> read;
        ASSERT_TRUE(db.ReadShieldedSubtrees(type, 254, 5, read));
        CheckSubtreeRange(read, 254, 5);

        ASSERT_TRUE(db.ReadShieldedSubtrees(type, 255, 0, read));
        CheckSubtreeRange(read, 255, 45);

        ASSERT_TRUE(db.ReadShieldedSubtrees(type, 299, 10, read));
        CheckSubtreeRange(read, 299, 1);

        ASSERT_TRUE(db.ReadShieldedSubtrees(type, 300, 0, read));
        EXPECT_TRUE(read.empty());

        ASSERT_TRUE(db.ReadShieldedSubtrees(type, UINT64_MAX, 2, read));
        EXPECT_TRUE(read.empty());
    }
}

