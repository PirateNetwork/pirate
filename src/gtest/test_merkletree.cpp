// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "test/data/merkle_roots.json.h"
#include "test/data/merkle_roots_empty.json.h"
#include "test/data/merkle_serialization.json.h"
#include "test/data/merkle_witness_serialization.json.h"
#include "test/data/merkle_path.json.h"
#include "test/data/merkle_commitments.json.h"

#include "test/data/merkle_roots_sapling.json.h"
#include "test/data/merkle_roots_empty_sapling.json.h"
#include "test/data/merkle_serialization_sapling.json.h"
#include "test/data/merkle_witness_serialization_sapling.json.h"
#include "test/data/merkle_path_sapling.json.h"
#include "test/data/merkle_commitments_sapling.json.h"

#include <iostream>

#include <stdexcept>
#include <thread>

#include "util/strencodings.h"
#include "version.h"
#include "serialize.h"
#include "streams.h"

#include "zcash/IncrementalMerkleTree.hpp"
#include "zcash/util.h"

#include <boost/foreach.hpp>

#include "json_test_vectors.h"

// Incremental Merkle tree tests: root computation, witnessing, path
// generation, and serialization/deserialization, checked against test
// vectors for both Sprout and Sapling commitment trees (test_tree<> is
// instantiated for SproutTestingMerkleTree and SaplingTestingMerkleTree).
// No Ironwood/Orchard commitment tree coverage in this file.

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
        BOOST_FOREACH(Witness& wit, witnesses)
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

        BOOST_FOREACH(Witness& wit, witnesses)
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
    UniValue empty_roots = read_json(MAKE_STRING(json_tests::merkle_roots_empty));

    libzcash::EmptyMerkleRoots<64, libzcash::SHA256Compress> emptyroots;

    for (size_t depth = 0; depth <= 64; depth++) {
        expect_test_vector(empty_roots[depth], emptyroots.empty_root(depth));
    }

    // Double check that we're testing (at least) all the empty roots we'll use.
    ASSERT_TRUE(INCREMENTAL_MERKLE_TREE_DEPTH <= 64);
}

TEST(merkletree, EmptyrootsSapling) {
    UniValue empty_roots = read_json(MAKE_STRING(json_tests::merkle_roots_empty_sapling));

    libzcash::EmptyMerkleRoots<62, libzcash::PedersenHash> emptyroots;

    for (size_t depth = 0; depth <= 62; depth++) {
        expect_test_vector(empty_roots[depth], emptyroots.empty_root(depth));
    }

    // Double check that we're testing (at least) all the empty roots we'll use.
    ASSERT_TRUE(INCREMENTAL_MERKLE_TREE_DEPTH <= 62);
}

TEST(merkletree, emptyroot) {
    // This literal is the depth-20 empty tree root with the bytes reversed to
    // account for the fact that uint256S() loads a big-endian representation of
    // an integer which converted to little-endian internally.
    uint256 expected = uint256S("59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7");

    ASSERT_TRUE(SproutMerkleTree::empty_root() == expected);
}

TEST(merkletree, EmptyrootSapling) {
    // This literal is the depth-20 empty tree root with the bytes reversed to
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

template <typename Tree>
static void CheckRootMemoization(const UniValue& commitments)
{
    auto append = [&](Tree& tree, size_t index) {
        tree.append(uint256S(commitments[index].get_str()));
    };
    auto independentRoot = [](const Tree& tree) {
        // Witness roots use the private depth/filler implementation directly,
        // independently of the full-tree root cache.
        return tree.witness().root();
    };
    auto encoded = [](const Tree& tree) {
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << tree;
        return stream.str();
    };
    auto checkRoot = [&](const Tree& tree) {
        const auto expected = independentRoot(tree);
        EXPECT_EQ(expected, tree.root());
        EXPECT_EQ(expected, tree.root());
    };

    Tree tree;
    checkRoot(tree);
    for (size_t i = 0; i < 7; ++i) {
        append(tree, i);
        checkRoot(tree);
    }
    const auto originalRoot = independentRoot(tree);
    const auto originalBytes = encoded(tree);

    Tree copied(tree);
    checkRoot(copied);
    append(copied, 7);
    checkRoot(copied);
    EXPECT_EQ(originalRoot, tree.root());
    EXPECT_EQ(originalBytes, encoded(tree));

    Tree assigned;
    checkRoot(assigned);
    assigned = tree;
    checkRoot(assigned);
    assigned = assigned;
    checkRoot(assigned);
    append(assigned, 8);
    checkRoot(assigned);
    EXPECT_EQ(originalRoot, tree.root());

    Tree moved(std::move(copied));
    checkRoot(moved);
    checkRoot(copied); // A moved-from tree must not retain its old cached root.
    assigned = std::move(moved);
    checkRoot(assigned);
    checkRoot(moved);
    assigned = std::move(assigned);
    checkRoot(assigned);

    // Deserialization replaces a warm destination, and never serializes a cache.
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tree;
    stream >> assigned;
    EXPECT_EQ(originalBytes, encoded(assigned));
    EXPECT_TRUE(tree == assigned);
    checkRoot(assigned);

    // A failed read can already have replaced the first leaf. Invalidation must
    // precede that first read, rather than waiting for successful deserialization.
    Tree different;
    append(different, 12);
    CDataStream truncated(SER_NETWORK, PROTOCOL_VERSION);
    truncated << different;
    truncated.resize(33); // The present marker and complete first leaf only.
    ASSERT_THROW(truncated >> assigned, std::ios_base::failure);
    EXPECT_NE(originalRoot, independentRoot(assigned));
    checkRoot(assigned);

    // Concurrent const calls and copies may race to populate the same cold
    // memo, while the underlying tree value remains immutable.
    Tree shared;
    for (size_t i = 0; i < 7; ++i) {
        append(shared, i);
    }
    const auto expected = independentRoot(shared);
    std::atomic<bool> start(false);
    std::array<bool, 8> correct;
    std::array<std::thread, 8> readers;
    for (size_t i = 0; i < readers.size(); ++i) {
        readers[i] = std::thread([&, i] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            bool same = true;
            for (size_t j = 0; j < 50; ++j) {
                Tree copy(shared);
                same = same && shared.root() == expected && copy.root() == expected;
            }
            correct[i] = same;
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& reader : readers) {
        reader.join();
    }
    for (bool result : correct) {
        EXPECT_TRUE(result);
    }
}

TEST(merkletree, SproutRootMemoization)
{
    CheckRootMemoization<SproutTestingMerkleTree>(
        read_json(MAKE_STRING(json_tests::merkle_commitments)));
}

TEST(merkletree, SaplingRootMemoization)
{
    CheckRootMemoization<SaplingTestingMerkleTree>(
        read_json(MAKE_STRING(json_tests::merkle_commitments_sapling)));
}
