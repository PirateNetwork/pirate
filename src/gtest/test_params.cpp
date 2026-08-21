// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>
#include "params.h"

#include <boost/filesystem.hpp>
#include <fstream>

namespace TestParams {

    static boost::filesystem::path WriteTempFile(const std::string& contents)
    {
        boost::filesystem::path path = boost::filesystem::temp_directory_path() /
            boost::filesystem::unique_path("bootstrap-test-%%%%-%%%%-%%%%-%%%%");
        std::ofstream out(path.string(), std::ofstream::binary);
        out << contents;
        out.close();
        return path;
    }

    // sha256("pirate-bootstrap-test"), computed independently via sha256sum.
    static const std::string kKnownContents = "pirate-bootstrap-test";
    static const std::string kKnownHash = "ec0a3b142d89c68e24fb44b3bc78bb5abf738042f1f9c4de3bda75b60c3b8674";

    TEST(TestParams, VerifyBootstrapHashMatches)
    {
        boost::filesystem::path path = WriteTempFile(kKnownContents);
        EXPECT_TRUE(VerifyBootstrapHash(path, kKnownHash));
        boost::filesystem::remove(path);
    }

    TEST(TestParams, VerifyBootstrapHashMismatch)
    {
        boost::filesystem::path path = WriteTempFile(kKnownContents);
        std::string wrongHash(64, '0');
        EXPECT_FALSE(VerifyBootstrapHash(path, wrongHash));
        boost::filesystem::remove(path);
    }

    TEST(TestParams, VerifyBootstrapHashRejectsMalformedExpectedHash)
    {
        boost::filesystem::path path = WriteTempFile(kKnownContents);
        EXPECT_FALSE(VerifyBootstrapHash(path, "not-a-real-hash"));
        EXPECT_FALSE(VerifyBootstrapHash(path, ""));
        boost::filesystem::remove(path);
    }

    TEST(TestParams, VerifyBootstrapHashMissingFile)
    {
        boost::filesystem::path path = boost::filesystem::temp_directory_path() / "pirate-bootstrap-does-not-exist";
        EXPECT_FALSE(VerifyBootstrapHash(path, std::string(64, 'a')));
    }

} // namespace TestParams
