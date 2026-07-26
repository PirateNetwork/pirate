// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// gtestutils.h (via rpc/register.h -> rpc/protocol.h) declares an HTTP_OK/
// HTTP_NOCONTENT/etc. enum; event2/http.h #defines those same names as macros.
// Whichever is included first wins, and the enum must come first or the
// #define poisons its enumerator names. Include gtestutils.h before anything
// that drags in event2/http.h (support/events.h does, transitively).
#include "gtest/gtestutils.h"

#include <event2/event.h>

#ifdef EVENT_SET_MEM_FUNCTIONS_IMPLEMENTED
// It would probably be ideal to define dummy test(s) that report skipped, but
// there doesn't seem to be a practical way to do that portably here either.

#include <map>
#include <stdlib.h>

#include "support/events.h"

#include <vector>

#include <gtest/gtest.h>

static std::map<void*, short> tags;
static std::map<void*, uint16_t> orders;
static uint16_t tagSequence = 0;

static void* tag_malloc(size_t sz) {
    void* mem = malloc(sz);
    if (!mem) return mem;
    tags[mem]++;
    orders[mem] = tagSequence++;
    return mem;
}

static void tag_free(void* mem) {
    tags[mem]--;
    orders[mem] = tagSequence++;
    free(mem);
}

class raii_event_tests : public BitcoinBasicTestingSetup {};

TEST_F(raii_event_tests, raii_event_creation)
{
    event_set_mem_functions(tag_malloc, realloc, tag_free);

    void* base_ptr = NULL;
    {
        auto base = obtain_event_base();
        base_ptr = (void*)base.get();
        EXPECT_TRUE(tags[base_ptr] == 1);
    }
    EXPECT_TRUE(tags[base_ptr] == 0);

    void* event_ptr = NULL;
    {
        auto base = obtain_event_base();
        auto event = obtain_event(base.get(), -1, 0, NULL, NULL);

        base_ptr = (void*)base.get();
        event_ptr = (void*)event.get();

        EXPECT_TRUE(tags[base_ptr] == 1);
        EXPECT_TRUE(tags[event_ptr] == 1);
    }
    EXPECT_TRUE(tags[base_ptr] == 0);
    EXPECT_TRUE(tags[event_ptr] == 0);

    event_set_mem_functions(malloc, realloc, free);
}

TEST_F(raii_event_tests, raii_event_order)
{
    event_set_mem_functions(tag_malloc, realloc, tag_free);

    void* base_ptr = NULL;
    void* event_ptr = NULL;
    {
        auto base = obtain_event_base();
        auto event = obtain_event(base.get(), -1, 0, NULL, NULL);

        base_ptr = (void*)base.get();
        event_ptr = (void*)event.get();

        // base should have allocated before event
        EXPECT_TRUE(orders[base_ptr] < orders[event_ptr]);
    }
    // base should be freed after event
    EXPECT_TRUE(orders[base_ptr] > orders[event_ptr]);

    event_set_mem_functions(malloc, realloc, free);
}

#endif  // EVENT_SET_MEM_FUNCTIONS_IMPLEMENTED
