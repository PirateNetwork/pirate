// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests the secure_allocator/LockedPageManager machinery that zeroes and
// locks sensitive memory (e.g. private keys) on free. Protocol-agnostic
// low-level infrastructure, not shielded-pool-specific.

#include "util.h"

#include "support/allocators/secure.h"
#include "gtest/gtestutils.h"

#include <gtest/gtest.h>

class allocator_tests_bitcoin : public BitcoinBasicTestingSetup {};

// Dummy memory page locker for platform independent tests
static const void *last_lock_addr, *last_unlock_addr;
static size_t last_lock_len, last_unlock_len;
class TestLocker
{
public:
    bool Lock(const void *addr, size_t len)
    {
        last_lock_addr = addr;
        last_lock_len = len;
        return true;
    }
    bool Unlock(const void *addr, size_t len)
    {
        last_unlock_addr = addr;
        last_unlock_len = len;
        return true;
    }
};

TEST_F(allocator_tests_bitcoin, test_LockedPageManagerBase)
{
    const size_t test_page_size = 4096;
    LockedPageManagerBase<TestLocker> lpm(test_page_size);
    size_t addr;
    last_lock_addr = last_unlock_addr = 0;
    last_lock_len = last_unlock_len = 0;

    /* Try large number of small objects */
    addr = 0;
    for(int i=0; i<1000; ++i)
    {
        lpm.LockRange(reinterpret_cast<void*>(addr), 33);
        addr += 33;
    }
    /* Try small number of page-sized objects, straddling two pages */
    addr = test_page_size*100 + 53;
    for(int i=0; i<100; ++i)
    {
        lpm.LockRange(reinterpret_cast<void*>(addr), test_page_size);
        addr += test_page_size;
    }
    /* Try small number of page-sized objects aligned to exactly one page */
    addr = test_page_size*300;
    for(int i=0; i<100; ++i)
    {
        lpm.LockRange(reinterpret_cast<void*>(addr), test_page_size);
        addr += test_page_size;
    }
    /* one very large object, straddling pages */
    lpm.LockRange(reinterpret_cast<void*>(test_page_size*600+1), test_page_size*500);
    EXPECT_TRUE(last_lock_addr == reinterpret_cast<void*>(test_page_size*(600+500)));
    /* one very large object, page aligned */
    lpm.LockRange(reinterpret_cast<void*>(test_page_size*1200), test_page_size*500-1);
    EXPECT_TRUE(last_lock_addr == reinterpret_cast<void*>(test_page_size*(1200+500-1)));

    EXPECT_TRUE(lpm.GetLockedPageCount() == (
        (1000*33+test_page_size-1)/test_page_size + // small objects
        101 + 100 +  // page-sized objects
        501 + 500)); // large objects
    EXPECT_TRUE((last_lock_len & (test_page_size-1)) == 0); // always lock entire pages
    EXPECT_TRUE(last_unlock_len == 0); // nothing unlocked yet

    /* And unlock again */
    addr = 0;
    for(int i=0; i<1000; ++i)
    {
        lpm.UnlockRange(reinterpret_cast<void*>(addr), 33);
        addr += 33;
    }
    addr = test_page_size*100 + 53;
    for(int i=0; i<100; ++i)
    {
        lpm.UnlockRange(reinterpret_cast<void*>(addr), test_page_size);
        addr += test_page_size;
    }
    addr = test_page_size*300;
    for(int i=0; i<100; ++i)
    {
        lpm.UnlockRange(reinterpret_cast<void*>(addr), test_page_size);
        addr += test_page_size;
    }
    lpm.UnlockRange(reinterpret_cast<void*>(test_page_size*600+1), test_page_size*500);
    lpm.UnlockRange(reinterpret_cast<void*>(test_page_size*1200), test_page_size*500-1);

    /* Check that everything is released */
    EXPECT_TRUE(lpm.GetLockedPageCount() == 0);

    /* A few and unlocks of size zero (should have no effect) */
    addr = 0;
    for(int i=0; i<1000; ++i)
    {
        lpm.LockRange(reinterpret_cast<void*>(addr), 0);
        addr += 1;
    }
    EXPECT_TRUE(lpm.GetLockedPageCount() == 0);
    addr = 0;
    for(int i=0; i<1000; ++i)
    {
        lpm.UnlockRange(reinterpret_cast<void*>(addr), 0);
        addr += 1;
    }
    EXPECT_TRUE(lpm.GetLockedPageCount() == 0);
    EXPECT_TRUE((last_unlock_len & (test_page_size-1)) == 0); // always unlock entire pages
}


