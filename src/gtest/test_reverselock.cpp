// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "reverselock.h"
#include "gtest/gtestutils.h"

#include <gtest/gtest.h>

class reverselock_tests : public BitcoinBasicTestingSetup {};

TEST_F(reverselock_tests, reverselock_basics)
{
    boost::mutex mutex;
    boost::unique_lock<boost::mutex> lock(mutex);

    EXPECT_TRUE(lock.owns_lock());
    {
        reverse_lock<boost::unique_lock<boost::mutex> > rlock(lock);
        EXPECT_FALSE(lock.owns_lock());
    }
    EXPECT_TRUE(lock.owns_lock());
}

TEST_F(reverselock_tests, reverselock_errors)
{
    boost::mutex mutex;
    boost::unique_lock<boost::mutex> lock(mutex);

    // Make sure trying to reverse lock an unlocked lock fails
    lock.unlock();

    EXPECT_FALSE(lock.owns_lock());

    bool failed = false;
    try {
        reverse_lock<boost::unique_lock<boost::mutex> > rlock(lock);
    } catch(...) {
        failed = true;
    }

    EXPECT_TRUE(failed);
    EXPECT_FALSE(lock.owns_lock());

    // Locking the original lock after it has been taken by a reverse lock
    // makes no sense. Ensure that the original lock no longer owns the lock
    // after giving it to a reverse one.

    lock.lock();
    EXPECT_TRUE(lock.owns_lock());
    {
        reverse_lock<boost::unique_lock<boost::mutex> > rlock(lock);
        EXPECT_FALSE(lock.owns_lock());
    }

    EXPECT_TRUE(failed);
    EXPECT_TRUE(lock.owns_lock());
}
