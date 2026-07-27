// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "komodo_utils.h"
#include "komodo_notary.h"
#include "komodo_structs.h"
#include "komodo_globals.h"
#include "komodo_extern_globals.h"

#include <vector>
#include <fstream>

// Test-data provider for gtest/test_parse_notarisation.cpp (declared in
// test_parse_notarisation.h). Not a test file itself -- it contains no
// TEST/TEST_F cases, only get_test_checkpoints_from_file() below, which
// reads a binary fixture (notarizationdata.tst) of historical Komodo
// notarization checkpoints used as input data by the real tests.
//
// This file used to be gtest/test_parse_notarisation_data.cpp and carried
// ~8100 lines of commented-out dead code: a literal notarized_checkpoint[]
// array (the plaintext source the binary fixture was originally generated
// from) and a write_test_checkpoints()/TEST(BuildTestData, WriteTestData)
// pair used one-time to produce that fixture file. None of it compiled or
// ran -- it was all inside a single /* ... */ block -- so it was deleted
// outright rather than carried forward, and the file was renamed to drop
// the misleading "test_" prefix (it produces zero test cases).

/**
 * @brief read a binary file to get a long list of notarized_checkpoints for testing
 * @param filename the file to read
 * @returns a big vector
 */
std::vector<notarized_checkpoint> get_test_checkpoints_from_file(const std::string& filename)
{
    std::vector<notarized_checkpoint> retval;

    std::ifstream in(filename, std::ios::in
            | std::ios::binary);

    if (!in.is_open())
    {
        // look in the test directory
        std::string test = std::string("./test/") + filename;
        in.open(test, std::ios::in
            | std::ios::binary);
    }
    if (!in.is_open())
    {
        // look in the ../test directory
        std::string test = std::string("../test/") + filename;
        in.open(test, std::ios::in
            | std::ios::binary);
    }

    while(!in.eof())
    {
        notarized_checkpoint cp;
        cp.notarized_hash.Unserialize(in);
        cp.notarized_desttxid.Unserialize(in);
        cp.MoM.Unserialize(in);
        cp.MoMoM.Unserialize(in);
        in.read(reinterpret_cast<char*>( &(cp.nHeight) ), sizeof(int32_t) );
        in.read(reinterpret_cast<char*>( &(cp.notarized_height) ), sizeof(int32_t) );
        in.read(reinterpret_cast<char*>( &(cp.MoMdepth) ), sizeof(int32_t) );
        in.read(reinterpret_cast<char*>( &(cp.MoMoMdepth) ), sizeof(int32_t) );
        in.read(reinterpret_cast<char*>( &(cp.MoMoMoffset) ), sizeof(int32_t) );
        in.read(reinterpret_cast<char*>( &(cp.kmdstarti) ), sizeof(int32_t) );
        in.read(reinterpret_cast<char*>( &(cp.kmdendi) ), sizeof(int32_t) );
        if (cp.nHeight == 0)
            break;
        retval.push_back(cp);
    }
    in.close();
    return retval;
}
