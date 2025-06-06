#include <gtest/gtest.h>

#include "main.h"
//#include "util/test.h"
#include "zcash/History.hpp"

// HistoryNode getLeafN(uint64_t block_num) {
//     HistoryNode node = libzcash::NewV1Leaf(
//         uint256(),
//         block_num*10,
//         block_num*13,
//         uint256(),
//         uint256(),
//         block_num,
//         3
//     );
//     return node;
// }

// TEST(History, Smoky) {
//     // Fake an empty view
//     CCoinsViewDummy fakeDB;
//     CCoinsViewCache view(&fakeDB);

//     uint32_t epochId = 0;

//     // Test initial value
//     EXPECT_EQ(view.GetHistoryLength(epochId), 0);

//     view.PushHistoryNode(epochId, getLeafN(1));

//     EXPECT_EQ(view.GetHistoryLength(epochId), 1);

//     view.PushHistoryNode(epochId, getLeafN(2));

//     EXPECT_EQ(view.GetHistoryLength(epochId), 3);

//     view.PushHistoryNode(epochId, getLeafN(3));

//     EXPECT_EQ(view.GetHistoryLength(epochId), 4);

//     view.PushHistoryNode(epochId, getLeafN(4));

//     uint256 h4Root = view.GetHistoryRoot(epochId);

//     EXPECT_EQ(view.GetHistoryLength(epochId), 7);

//     view.PushHistoryNode(epochId, getLeafN(5));
//     EXPECT_EQ(view.GetHistoryLength(epochId), 8);

//     view.PopHistoryNode(epochId);

//     EXPECT_EQ(view.GetHistoryLength(epochId), 7);
//     EXPECT_EQ(h4Root, view.GetHistoryRoot(epochId));
// }


// TEST(History, EpochBoundaries) {
//     // Fake an empty view
//     CCoinsViewDummy fakeDB;
//     CCoinsViewCache view(&fakeDB);

//     // Test with the Heartwood and Canopy epochs
//     uint32_t epoch1 = 0xf5b9230b;
//     uint32_t epoch2 = 0xe9ff75a6;

//     view.PushHistoryNode(epoch1, getLeafN(1));

//     EXPECT_EQ(view.GetHistoryLength(epoch1), 1);

//     view.PushHistoryNode(epoch1, getLeafN(2));

//     EXPECT_EQ(view.GetHistoryLength(epoch1), 3);

//     view.PushHistoryNode(epoch1, getLeafN(3));

//     EXPECT_EQ(view.GetHistoryLength(epoch1), 4);

//     view.PushHistoryNode(epoch1, getLeafN(4));

//     uint256 h4Root = view.GetHistoryRoot(epoch1);

//     EXPECT_EQ(view.GetHistoryLength(epoch1), 7);

//     view.PushHistoryNode(epoch1, getLeafN(5));
//     EXPECT_EQ(view.GetHistoryLength(epoch1), 8);


//     // Move to Canopy epoch
//     view.PushHistoryNode(epoch2, getLeafN(6));
//     EXPECT_EQ(view.GetHistoryLength(epoch1), 8);
//     EXPECT_EQ(view.GetHistoryLength(epoch2), 1);

//     view.PushHistoryNode(epoch2, getLeafN(7));
//     EXPECT_EQ(view.GetHistoryLength(epoch1), 8);
//     EXPECT_EQ(view.GetHistoryLength(epoch2), 3);

//     view.PushHistoryNode(epoch2, getLeafN(8));
//     EXPECT_EQ(view.GetHistoryLength(epoch1), 8);
//     EXPECT_EQ(view.GetHistoryLength(epoch2), 4);

//     // Rolling epoch back to 1
//     view.PopHistoryNode(epoch2);
//     EXPECT_EQ(view.GetHistoryLength(epoch2), 3);

//     view.PopHistoryNode(epoch2);
//     EXPECT_EQ(view.GetHistoryLength(epoch2), 1);
//     EXPECT_EQ(view.GetHistoryLength(epoch1), 8);

//     // And even rolling epoch 1 back a bit
//     view.PopHistoryNode(epoch1);
//     EXPECT_EQ(view.GetHistoryLength(epoch1), 7);

//     // And also rolling epoch 2 back to 0
//     view.PopHistoryNode(epoch2);
//     EXPECT_EQ(view.GetHistoryLength(epoch2), 0);

//     // Trying to truncate an empty tree is a no-op
//     view.PopHistoryNode(epoch2);
//     EXPECT_EQ(view.GetHistoryLength(epoch2), 0);

// }

// TEST(History, GarbageMemoryHash) {
//     const auto consensusBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_HEARTWOOD].nBranchId;

//     CCoinsViewDummy fakeDB;
//     CCoinsViewCache view(&fakeDB);

//     // Hash two history nodes
//     HistoryNode node0 = getLeafN(1);
//     HistoryNode node1 = getLeafN(2);

//     view.PushHistoryNode(consensusBranchId, node0);
//     view.PushHistoryNode(consensusBranchId, node1);

//     uint256 historyRoot = view.GetHistoryRoot(consensusBranchId);

//     // Change garbage memory and re-hash nodes
//     CCoinsViewDummy fakeDBGarbage;
//     CCoinsViewCache viewGarbage(&fakeDBGarbage);

//     HistoryNode node0Garbage = getLeafN(1);
//     HistoryNode node1Garbage = getLeafN(2);

//     node0Garbage[NODE_SERIALIZED_LENGTH - 1] = node0[NODE_SERIALIZED_LENGTH - 1] ^ 1;
//     node1Garbage[NODE_SERIALIZED_LENGTH - 1] = node1[NODE_SERIALIZED_LENGTH - 1] ^ 1;

//     viewGarbage.PushHistoryNode(consensusBranchId, node0Garbage);
//     viewGarbage.PushHistoryNode(consensusBranchId, node1Garbage);

//     uint256 historyRootGarbage = viewGarbage.GetHistoryRoot(consensusBranchId);

//     // Check history root and garbage history root are equal
//     EXPECT_EQ(historyRoot, historyRootGarbage);
// }
