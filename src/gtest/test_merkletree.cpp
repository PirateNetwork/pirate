#include <gtest/gtest.h>

#include "test/data/merkle_roots.json.h"
#include "test/data/merkle_serialization.json.h"
#include "test/data/merkle_witness_serialization.json.h"
#include "test/data/merkle_path.json.h"
#include "test/data/merkle_commitments.json.h"
//#include "test/data/merkle_roots_orchard.h"

#include "test/data/merkle_roots_sapling.json.h"
#include "test/data/merkle_serialization_sapling.json.h"
#include "test/data/merkle_witness_serialization_sapling.json.h"
#include "test/data/merkle_path_sapling.json.h"
#include "test/data/merkle_commitments_sapling.json.h"

#include <iostream>

#include <stdexcept>

#include "util/strencodings.h"
#include "version.h"
#include "serialize.h"
#include "streams.h"

#include "zcash/IncrementalMerkleTree.hpp"
#include "zcash/util.h"

#include "gtest/utils.h"

#include "json_test_vectors.h"

using namespace std;

template<>
void expect_deser_same(const SproutTestingWitness& expected)
{
    // Cannot check this; IncrementalWitness cannot be
    // deserialized because it can only be constructed by
    // IncrementalMerkleTree, and it does not yet have a
    // canonical serialized representation.
}

template<typename A, typename B, typename C>
void expect_ser_test_vector(B& b, const C& c, const A& tree) {
    expect_test_vector<B, C>(b, c);
}

template<typename Tree, typename Witness>
void test_tree(
    UniValue commitment_tests,
    UniValue root_tests,
    UniValue ser_tests,
    UniValue witness_ser_tests,
    UniValue path_tests
)
{
    size_t witness_ser_i = 0;
    size_t path_i = 0;

    Tree tree;

    // The root of the tree at this point is expected to be the root of the
    // empty tree.
    ASSERT_TRUE(tree.root() == Tree::empty_root());

    // The tree doesn't have a 'last' element added since it's blank.
    ASSERT_THROW(tree.last(), std::runtime_error);

    // The tree is empty.
    ASSERT_TRUE(tree.size() == 0);

    // We need to witness at every single point in the tree, so
    // that the consistency of the tree and the merkle paths can
    // be checked.
    vector<Witness> witnesses;

    for (size_t i = 0; i < 16; i++) {
        uint256 test_commitment = uint256S(commitment_tests[i].get_str());

        // Witness here
        witnesses.push_back(tree.witness());

        // Now append a commitment to the tree
        tree.append(test_commitment);

        // Size incremented by one.
        ASSERT_TRUE(tree.size() == i+1);

        // Last element added to the tree was `test_commitment`
        ASSERT_TRUE(tree.last() == test_commitment);

        // Check tree root consistency
        expect_test_vector(root_tests[i], tree.root());

        // Check serialization of tree
        expect_ser_test_vector(ser_tests[i], tree, tree);

        bool first = true; // The first witness can never form a path
        for (Witness& wit : witnesses)
        {
            // Append the same commitment to all the witnesses
            wit.append(test_commitment);

            if (first) {
                ASSERT_THROW(wit.path(), std::runtime_error);
                ASSERT_THROW(wit.element(), std::runtime_error);
            } else {
                auto path = wit.path();
                expect_test_vector(path_tests[path_i++], path);
            }

            // Check witness serialization
            expect_ser_test_vector(witness_ser_tests[witness_ser_i++], wit, tree);

            ASSERT_TRUE(wit.root() == tree.root());

            first = false;
        }
    }

    {
        // Tree should be full now
        ASSERT_THROW(tree.append(uint256()), std::runtime_error);

        for (Witness& wit : witnesses)
        {
            ASSERT_THROW(wit.append(uint256()), std::runtime_error);
        }
    }
}

#define MAKE_STRING(x) std::string((x), (x)+sizeof(x))

TEST(merkletree, vectors) {
    UniValue root_tests = read_json(MAKE_STRING(json_tests::merkle_roots));
    UniValue ser_tests = read_json(MAKE_STRING(json_tests::merkle_serialization));
    UniValue witness_ser_tests = read_json(MAKE_STRING(json_tests::merkle_witness_serialization));
    UniValue path_tests = read_json(MAKE_STRING(json_tests::merkle_path));
    UniValue commitment_tests = read_json(MAKE_STRING(json_tests::merkle_commitments));

    test_tree<SproutTestingMerkleTree, SproutTestingWitness>(
        commitment_tests,
        root_tests,
        ser_tests,
        witness_ser_tests,
        path_tests
    );
}

TEST(merkletree, SaplingVectors) {
    UniValue root_tests = read_json(MAKE_STRING(json_tests::merkle_roots_sapling));
    UniValue ser_tests = read_json(MAKE_STRING(json_tests::merkle_serialization_sapling));
    UniValue witness_ser_tests = read_json(MAKE_STRING(json_tests::merkle_witness_serialization_sapling));
    UniValue path_tests = read_json(MAKE_STRING(json_tests::merkle_path_sapling));
    UniValue commitment_tests = read_json(MAKE_STRING(json_tests::merkle_commitments_sapling));

    test_tree<SaplingTestingMerkleTree, SaplingTestingWitness>(
        commitment_tests,
        root_tests,
        ser_tests,
        witness_ser_tests,
        path_tests
    );
}

TEST(merkletree, emptyroots) {
    libzcash::EmptyMerkleRoots<64, libzcash::SHA256Compress> emptyroots;
    std::array<libzcash::SHA256Compress, 65> computed;

    computed.at(0) = libzcash::SHA256Compress::uncommitted();
    ASSERT_TRUE(emptyroots.empty_root(0) == computed.at(0));
    for (size_t d = 1; d <= 64; d++) {
        computed.at(d) = libzcash::SHA256Compress::combine(computed.at(d-1), computed.at(d-1), d-1);
        ASSERT_TRUE(emptyroots.empty_root(d) == computed.at(d));
    }

    // Double check that we're testing (at least) all the empty roots we'll use.
    ASSERT_TRUE(INCREMENTAL_MERKLE_TREE_DEPTH <= 64);
}

TEST(merkletree, EmptyrootsSapling) {
    libzcash::EmptyMerkleRoots<62, libzcash::PedersenHash> emptyroots;
    std::array<libzcash::PedersenHash, 63> computed;

    computed.at(0) = libzcash::PedersenHash::uncommitted();
    ASSERT_TRUE(emptyroots.empty_root(0) == computed.at(0));
    for (size_t d = 1; d <= 62; d++) {
        computed.at(d) = libzcash::PedersenHash::combine(computed.at(d-1), computed.at(d-1), d-1);
        ASSERT_TRUE(emptyroots.empty_root(d) == computed.at(d));
    }

    // Double check that we're testing (at least) all the empty roots we'll use.
    ASSERT_TRUE(INCREMENTAL_MERKLE_TREE_DEPTH <= 62);
}

TEST(merkletree, emptyroot) {
    // This literal is the depth-29 empty tree root with the bytes reversed to
    // account for the fact that uint256S() loads a big-endian representation of
    // an integer which converted to little-endian internally.
    uint256 expected = uint256S("59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7");

    ASSERT_TRUE(SproutMerkleTree::empty_root() == expected);
}

TEST(merkletree, EmptyrootSapling) {
    // This literal is the depth-32 empty tree root with the bytes reversed to
    // account for the fact that uint256S() loads a big-endian representation of
    // an integer which converted to little-endian internally.
    uint256 expected = uint256S("3e49b5f954aa9d3545bc6c37744661eea48d7c34e3000d82b7f0010c30f4c2fb");

    ASSERT_TRUE(SaplingMerkleTree::empty_root() == expected);
}

TEST(merkletree, deserializeInvalid) {
    // attempt to deserialize a small tree from a serialized large tree
    // (exceeds depth well-formedness check)
    SproutMerkleTree newTree;

    for (size_t i = 0; i < 16; i++) {
        newTree.append(uint256S("54d626e08c1c802b305dad30b7e54a82f102390cc92c7d4db112048935236e9c"));
    }

    newTree.append(uint256S("54d626e08c1c802b305dad30b7e54a82f102390cc92c7d4db112048935236e9c"));

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << newTree;

    SproutTestingMerkleTree newTreeSmall;
    ASSERT_THROW({ss >> newTreeSmall;}, std::ios_base::failure);
}

TEST(merkletree, deserializeInvalid2) {
    // the most ancestral parent is empty
    CDataStream ss(
        ParseHex("0155b852781b9995a44c939b64e441ae2724b96f99c8f4fb9a141cfc9842c4b0e3000100"),
        SER_NETWORK,
        PROTOCOL_VERSION
    );

    SproutMerkleTree tree;
    ASSERT_THROW(ss >> tree, std::ios_base::failure);
}

TEST(merkletree, deserializeInvalid3) {
    // left doesn't exist but right does
    CDataStream ss(
        ParseHex("000155b852781b9995a44c939b64e441ae2724b96f99c8f4fb9a141cfc9842c4b0e300"),
        SER_NETWORK,
        PROTOCOL_VERSION
    );

    SproutMerkleTree tree;
    ASSERT_THROW(ss >> tree, std::ios_base::failure);
}

TEST(merkletree, deserializeInvalid4) {
    // left doesn't exist but a parent does
    CDataStream ss(
        ParseHex("000001018695873d63ec0bceeadb5bf4ccc6723ac803c1826fc7cfb34fc76180305ae27d"),
        SER_NETWORK,
        PROTOCOL_VERSION
    );

    SproutMerkleTree tree;
    ASSERT_THROW(ss >> tree, std::ios_base::failure);
}

TEST(merkletree, testZeroElements) {
    for (int start = 0; start < 20; start++) {
        SproutMerkleTree newTree;

        ASSERT_TRUE(newTree.root() == SproutMerkleTree::empty_root());

        for (int i = start; i > 0; i--) {
            newTree.append(uint256S("54d626e08c1c802b305dad30b7e54a82f102390cc92c7d4db112048935236e9c"));
        }

        uint256 oldroot = newTree.root();

        // At this point, appending tons of null objects to the tree
        // should preserve its root.

        for (int i = 0; i < 100; i++) {
            newTree.append(uint256());
        }

        ASSERT_TRUE(newTree.root() == oldroot);
    }
}

// TEST(orchardMerkleTree, emptyroot) {
//     // This literal is the depth-32 empty tree root with the bytes reversed, to
//     // account for the fact that uint256S() loads a big-endian representation of
//     // an integer, which is converted to little-endian internally.
//     uint256 expected = uint256S("2fd8e51a03d9bbe2dd809831b1497aeb68a6e37ddf707ced4aa2d8dff13529ae");

//     ASSERT_EQ(OrchardMerkleFrontier::empty_root(), expected);
// }

// TEST(orchardMerkleTree, appendBundle) {
//     OrchardMerkleFrontier newTree;

//     ASSERT_EQ(newTree.root(), OrchardMerkleFrontier::empty_root());

//     for (int i = 0; i < 1; i++) {
//         CDataStream ssBundleData(merkle_roots_orchard[i].bundle, SER_NETWORK, PROTOCOL_VERSION);
//         OrchardBundle b;
//         ssBundleData >> b;
//         newTree.AppendBundle(b);

//         uint256 anchor(merkle_roots_orchard[i].anchor);

//         ASSERT_EQ(newTree.root(), anchor);

//         // Sanity check roundtrip serialization of the updated tree
//         CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
//         ss << newTree;

//         OrchardMerkleFrontier readBack;
//         ss >> readBack;

//         EXPECT_NE(newTree.root(), OrchardMerkleFrontier::empty_root());
//         EXPECT_EQ(newTree.root(), readBack.root());
//     }
// }

// TEST(saplingMerkleTree, subtree_roots)
// {
//     SaplingMerkleTree tree;
//     for (size_t i = 0; i < (1 << 16); i++) {
//         EXPECT_FALSE(tree.complete_subtree_root().has_value());
//         EXPECT_EQ(tree.current_subtree_index(), 0);
//         AppendRandomLeaf(tree);
//     }
//     EXPECT_EQ(tree.current_subtree_index(), 1);
//     auto subtree_root = tree.complete_subtree_root();
//     ASSERT_TRUE(subtree_root.has_value());
//     uint256 cur = *subtree_root;
//     for (size_t depth = 16; depth < 32; depth++) {
//         cur = libzcash::PedersenHash::combine(cur, libzcash::PedersenHash::EmptyRoot(depth), depth);
//     }
//     EXPECT_EQ(tree.root(), cur);
//     for (size_t i = 0; i < (1 << 16); i++) {
//         if (i != 0) {
//             EXPECT_FALSE(tree.complete_subtree_root().has_value());
//         }
//         EXPECT_EQ(tree.current_subtree_index(), 1);
//         AppendRandomLeaf(tree);
//     }
//     EXPECT_EQ(tree.current_subtree_index(), 2);
//     auto subtree_root2 = tree.complete_subtree_root();
//     ASSERT_TRUE(subtree_root.has_value());
//     uint256 cur2 = libzcash::PedersenHash::combine(*subtree_root, *subtree_root2, 16);
//     for (size_t depth = 17; depth < 32; depth++) {
//         cur2 = libzcash::PedersenHash::combine(cur2, libzcash::PedersenHash::EmptyRoot(depth), depth);
//     }
//     EXPECT_EQ(tree.root(), cur2);
// }