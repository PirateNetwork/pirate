// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "main.h"
#include "wallet/wallet.h"

static SaplingOutPoint SaplingOp(unsigned char tag)
{
    uint256 hash;
    *hash.begin() = tag;
    return SaplingOutPoint(hash, 0);
}

static IronwoodOutPoint IronwoodOp(unsigned char tag)
{
    uint256 hash;
    *hash.begin() = tag;
    return IronwoodOutPoint(hash, 0);
}

// Two operations running at once each hold their own guard. Releasing one must
// leave the other's notes locked.
TEST(NoteLockGuardTests, ReleaseOnlyUnlocksThisGuardsNotes)
{
    CWallet wallet;
    CNoteLockGuard first(&wallet);
    CNoteLockGuard second(&wallet);
    {
        LOCK2(cs_main, wallet.cs_wallet);
        first.Lock(SaplingOp(1));
        first.Lock(IronwoodOp(1));
        second.Lock(SaplingOp(2));
        second.Lock(IronwoodOp(2));
    }

    first.Release();

    LOCK2(cs_main, wallet.cs_wallet);
    EXPECT_FALSE(wallet.IsLockedNote(SaplingOp(1)));
    EXPECT_FALSE(wallet.IsLockedNote(IronwoodOp(1)));
    EXPECT_TRUE(wallet.IsLockedNote(SaplingOp(2)));
    EXPECT_TRUE(wallet.IsLockedNote(IronwoodOp(2)));
}

// A guard that is handed a note another guard already holds must not take
// ownership of it, or its release would drop the other operation's lock.
TEST(NoteLockGuardTests, DoesNotReleaseANoteItDidNotLock)
{
    CWallet wallet;
    CNoteLockGuard owner(&wallet);
    {
        CNoteLockGuard latecomer(&wallet);
        LOCK2(cs_main, wallet.cs_wallet);
        owner.Lock(SaplingOp(3));
        owner.Lock(IronwoodOp(3));
        latecomer.Lock(SaplingOp(3));
        latecomer.Lock(IronwoodOp(3));
    }

    LOCK2(cs_main, wallet.cs_wallet);
    EXPECT_TRUE(wallet.IsLockedNote(SaplingOp(3)));
    EXPECT_TRUE(wallet.IsLockedNote(IronwoodOp(3)));
}

// Locks taken by a guard are released when it goes out of scope, including
// when that happens because an exception unwinds the operation.
TEST(NoteLockGuardTests, ReleasesWhenDestroyedByAnException)
{
    CWallet wallet;
    try {
        CNoteLockGuard guard(&wallet);
        {
            LOCK2(cs_main, wallet.cs_wallet);
            guard.Lock(SaplingOp(4));
            guard.Lock(IronwoodOp(4));
            ASSERT_TRUE(wallet.IsLockedNote(SaplingOp(4)));
        }
        throw std::runtime_error("proving failed");
    } catch (const std::runtime_error&) {
    }

    LOCK2(cs_main, wallet.cs_wallet);
    EXPECT_FALSE(wallet.IsLockedNote(SaplingOp(4)));
    EXPECT_FALSE(wallet.IsLockedNote(IronwoodOp(4)));
}
