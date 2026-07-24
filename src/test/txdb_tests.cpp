// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/test_bitcoin.h"
#include "txdb.h"

#include <boost/test/unit_test.hpp>

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
    BOOST_REQUIRE_EQUAL(subtrees.size(), expectedSize);
    for (size_t offset = 0; offset < expectedSize; offset++) {
        const uint64_t index = firstIndex + offset;
        BOOST_CHECK_EQUAL(subtrees[offset].first, index);
        BOOST_CHECK(subtrees[offset].second.root == MakeSubtreeData(index).root);
        BOOST_CHECK_EQUAL(subtrees[offset].second.nHeight, static_cast<int>(1000 + index));
        BOOST_CHECK(subtrees[offset].second.blockHash == MakeSubtreeData(index).blockHash);
    }
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(txdb_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(shielded_subtree_reads_use_numeric_index_order)
{
    CBlockTreeDB db(1 << 20, true, true);

    for (ShieldedType type : {SAPLINGFRONTIER, IRONWOODFRONTIER}) {
        std::vector<std::pair<uint64_t, ShieldedSubtreeData>> written;
        for (uint64_t index = 0; index < 300; index++) {
            written.push_back(std::make_pair(index, MakeSubtreeData(index)));
        }
        BOOST_REQUIRE(db.WriteShieldedSubtrees(type, written));

        std::vector<std::pair<uint64_t, ShieldedSubtreeData>> read;
        BOOST_REQUIRE(db.ReadShieldedSubtrees(type, 254, 5, read));
        CheckSubtreeRange(read, 254, 5);

        BOOST_REQUIRE(db.ReadShieldedSubtrees(type, 255, 0, read));
        CheckSubtreeRange(read, 255, 45);

        BOOST_REQUIRE(db.ReadShieldedSubtrees(type, 299, 10, read));
        CheckSubtreeRange(read, 299, 1);

        BOOST_REQUIRE(db.ReadShieldedSubtrees(type, 300, 0, read));
        BOOST_CHECK(read.empty());

        BOOST_REQUIRE(db.ReadShieldedSubtrees(type, UINT64_MAX, 2, read));
        BOOST_CHECK(read.empty());
    }
}

BOOST_AUTO_TEST_SUITE_END()
