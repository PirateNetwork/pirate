// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "streams.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"

// A wallet.dat record type this binary has never heard of - e.g. one
// introduced by a future wallet version - must not crash or silently
// vanish on load: ReadKeyValue() reports it as a failure (with a message
// identifying which type was skipped), which CWalletDB::LoadWallet's
// existing IsKeyType()-based classification then correctly treats as
// non-critical (DB_NONCRITICAL_ERROR, not DB_CORRUPT) since an unrecognized
// type can never appear in that hardcoded list of known key types either.
// The wallet keeps loading, and - because LoadWallet skips any rewrite on a
// non-DB_LOAD_OK result - the original record is left untouched on disk for
// a future version that does understand it.
//
// Before this fix, an unrecognized type matched none of ReadKeyValue's
// if/else-if branches and fell straight through to `return true;` with no
// log at all - silently and invisibly dropped, not gracefully skipped.
TEST(WalletDBTests, ReadKeyValue_UnknownRecordTypeFailsGracefullyAndIsLogged)
{
    CWallet dummyWallet;
    CWalletScanState wss;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey << std::string("a_type_this_binary_has_never_heard_of");
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);

    std::string strType, strErr;
    bool fReadOK = ReadKeyValue(&dummyWallet, ssKey, ssValue, wss, strType, strErr);

    EXPECT_FALSE(fReadOK)
        << "an unrecognized record type must be reported as a (non-critical) "
           "failure, not silently treated as success";
    EXPECT_EQ(strType, "a_type_this_binary_has_never_heard_of");
    EXPECT_FALSE(strErr.empty())
        << "the caller logs strErr directly - an unrecognized record must "
           "produce a message, not vanish with zero trace";
    EXPECT_NE(strErr.find("a_type_this_binary_has_never_heard_of"), std::string::npos)
        << "the message should identify which record type was skipped";
}

// A real, currently-recognized record type must still work exactly as
// before - guards against the new catch-all `else` accidentally shadowing
// a legitimate type that should be handled by one of the earlier
// if/else-if branches in the dispatch chain.
TEST(WalletDBTests, ReadKeyValue_RecognizedRecordTypeStillSucceeds)
{
    CWallet dummyWallet;
    CWalletScanState wss;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey << std::string("orderposnext");
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue << (int64_t)42;

    std::string strType, strErr;
    bool fReadOK = ReadKeyValue(&dummyWallet, ssKey, ssValue, wss, strType, strErr);

    EXPECT_TRUE(fReadOK) << strErr;
    EXPECT_TRUE(strErr.empty());
    EXPECT_EQ(dummyWallet.nOrderPosNext, 42);
}

// Every record type this binary currently writes but doesn't act on during
// the main LoadWallet pass - because it's consumed through a separate,
// dedicated mechanism (ReadCryptedSeedValue, FindOldRecordsToZap, a direct
// Read() elsewhere, etc.) - must still read back as a silent no-op, not
// trip the new unrecognized-type branch. A first version of this fix
// treated every type ReadKeyValue didn't explicitly act on as "unknown",
// which wrongly flagged these 15 as DB_NONCRITICAL_ERROR (or, for the ones
// IsKeyType() also lists, DB_CORRUPT) on every single load.
TEST(WalletDBTests, ReadKeyValue_KnownButElsewhereHandledTypesAreSilentNoOps)
{
    static const char* kKnownElsewhereHandledTypes[] = {
        "mkey", "chdseed", "zkey", "czkey", "zkeymeta", "vkey", "cpool",
        "acentry", "arczcop", "bestblock", "iscrypted", "minversion",
        "walletbirthday", "witnesscachesize", "walletbip39enabled",
    };

    for (const char* type : kKnownElsewhereHandledTypes)
    {
        CWallet dummyWallet;
        CWalletScanState wss;

        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey << std::string(type);
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);

        std::string strType, strErr;
        bool fReadOK = ReadKeyValue(&dummyWallet, ssKey, ssValue, wss, strType, strErr);

        EXPECT_TRUE(fReadOK) << "'" << type << "' should be a silent no-op here, not "
                                "treated as an unrecognized record type: " << strErr;
        EXPECT_TRUE(strErr.empty()) << "'" << type << "': " << strErr;
    }
}
