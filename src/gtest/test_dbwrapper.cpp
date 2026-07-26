// Copyright (c) 2018 The Zcash developers
// Copyright (c) 2012-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dbwrapper.h"
#include "uint256.h"
#include "random.h"
#include "gtest/gtestutils.h"

#include <gtest/gtest.h>

using namespace boost::filesystem;

class dbwrapper_tests : public BitcoinBasicTestingSetup {};

TEST_F(dbwrapper_tests, dbwrapper)
{
    {
        path ph = temp_directory_path() / unique_path();
        CDBWrapper dbw(ph, (1 << 20), true, false);
        char key = 'k';
        uint256 in = GetRandHash();
        uint256 res;

        EXPECT_TRUE(dbw.Write(key, in));
        EXPECT_TRUE(dbw.Read(key, res));
        EXPECT_EQ(res.ToString(), in.ToString());
    }
}

// Test batch operations
TEST_F(dbwrapper_tests, dbwrapper_batch)
{
    {
        path ph = temp_directory_path() / unique_path();
        CDBWrapper dbw(ph, (1 << 20), true, false);

        char key = 'i';
        uint256 in = GetRandHash();
        char key2 = 'j';
        uint256 in2 = GetRandHash();
        char key3 = 'k';
        uint256 in3 = GetRandHash();

        uint256 res;
        CDBBatch batch(dbw);

        batch.Write(key, in);
        batch.Write(key2, in2);
        batch.Write(key3, in3);

        // Remove key3 before it's even been written
        batch.Erase(key3);

        dbw.WriteBatch(batch);

        EXPECT_TRUE(dbw.Read(key, res));
        EXPECT_EQ(res.ToString(), in.ToString());
        EXPECT_TRUE(dbw.Read(key2, res));
        EXPECT_EQ(res.ToString(), in2.ToString());

        // key3 should've never been written
        EXPECT_TRUE(dbw.Read(key3, res) == false);
    }
}

TEST_F(dbwrapper_tests, dbwrapper_iterator)
{
    {
        path ph = temp_directory_path() / unique_path();
        CDBWrapper dbw(ph, (1 << 20), true, false);

        // The two keys are intentionally chosen for ordering
        char key = 'j';
        uint256 in = GetRandHash();
        EXPECT_TRUE(dbw.Write(key, in));
        char key2 = 'k';
        uint256 in2 = GetRandHash();
        EXPECT_TRUE(dbw.Write(key2, in2));

        boost::scoped_ptr<CDBIterator> it(const_cast<CDBWrapper*>(&dbw)->NewIterator());

        // Be sure to seek past any earlier key (if it exists)
        it->Seek(key);

        char key_res;
        uint256 val_res;

        it->GetKey(key_res);
        it->GetValue(val_res);
        EXPECT_EQ(key_res, key);
        EXPECT_EQ(val_res.ToString(), in.ToString());

        it->Next();

        it->GetKey(key_res);
        it->GetValue(val_res);
        EXPECT_EQ(key_res, key2);
        EXPECT_EQ(val_res.ToString(), in2.ToString());

        it->Next();
        EXPECT_EQ(it->Valid(), false);
    }
}

TEST_F(dbwrapper_tests, iterator_ordering)
{
    path ph = temp_directory_path() / unique_path();
    CDBWrapper dbw(ph, (1 << 20), true, false);
    for (int x=0x00; x<256; ++x) {
        uint8_t key = x;
        uint32_t value = x*x;
        EXPECT_TRUE(dbw.Write(key, value));
    }

    boost::scoped_ptr<CDBIterator> it(const_cast<CDBWrapper*>(&dbw)->NewIterator());
    for (int seek_start : {0x00, 0x80}) {
        it->Seek((uint8_t)seek_start);
        for (int x=seek_start; x<256; ++x) {
            uint8_t key;
            uint32_t value;
            EXPECT_TRUE(it->Valid());
            if (!it->Valid()) // Avoid spurious errors about invalid iterator's key and value in case of failure
                break;
            EXPECT_TRUE(it->GetKey(key));
            EXPECT_TRUE(it->GetValue(value));
            EXPECT_EQ(key, x);
            EXPECT_EQ(value, (uint32_t)(x*x));
            it->Next();
        }
        EXPECT_TRUE(!it->Valid());
    }
}

namespace {
struct StringContentsSerializer {
    // Used to make two serialized objects the same while letting them have different lengths
    // This is a terrible idea
    std::string str;
    StringContentsSerializer() {}
    StringContentsSerializer(const std::string& inp) : str(inp) {}

    StringContentsSerializer& operator+=(const std::string& s) {
        str += s;
        return *this;
    }
    StringContentsSerializer& operator+=(const StringContentsSerializer& s) { return *this += s.str; }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (ser_action.ForRead()) {
            str.clear();
            char c = 0;
            while (true) {
                try {
                    READWRITE(c);
                    str.push_back(c);
                } catch (const std::ios_base::failure& e) {
                    break;
                }
            }
        } else {
            for (size_t i = 0; i < str.size(); i++)
                READWRITE(str[i]);
        }
    }
};
} // namespace

TEST_F(dbwrapper_tests, iterator_string_ordering)
{
    char buf[10];

    path ph = temp_directory_path() / unique_path();
    CDBWrapper dbw(ph, (1 << 20), true, false);
    for (int x=0x00; x<10; ++x) {
        for (int y = 0; y < 10; y++) {
            int n = snprintf(buf, sizeof(buf), "%d", x);
            assert(n > 0 && n < (int)sizeof(buf));
            StringContentsSerializer key(buf);
            for (int z = 0; z < y; z++)
                key += key;
            uint32_t value = x*x;
            EXPECT_TRUE(dbw.Write(key, value));
        }
    }

    boost::scoped_ptr<CDBIterator> it(const_cast<CDBWrapper*>(&dbw)->NewIterator());
    for (int seek_start : {0, 5}) {
        int n = snprintf(buf, sizeof(buf), "%d", seek_start);
        assert(n > 0 && n < (int)sizeof(buf));
        StringContentsSerializer seek_key(buf);
        it->Seek(seek_key);
        for (int x=seek_start; x<10; ++x) {
            for (int y = 0; y < 10; y++) {
                int n2 = snprintf(buf, sizeof(buf), "%d", x);
                assert(n2 > 0 && n2 < (int)sizeof(buf));
                std::string exp_key(buf);
                for (int z = 0; z < y; z++)
                    exp_key += exp_key;
                StringContentsSerializer key;
                uint32_t value;
                EXPECT_TRUE(it->Valid());
                if (!it->Valid()) // Avoid spurious errors about invalid iterator's key and value in case of failure
                    break;
                EXPECT_TRUE(it->GetKey(key));
                EXPECT_TRUE(it->GetValue(value));
                EXPECT_EQ(key.str, exp_key);
                EXPECT_EQ(value, (uint32_t)(x*x));
                it->Next();
            }
        }
        EXPECT_TRUE(!it->Valid());
    }
}
