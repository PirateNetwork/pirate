// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "utiltime.h"

#include <chrono>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>

using namespace std;

static int64_t nMockTime = 0;  //! For unit testing

int64_t GetTime()
{
    if (nMockTime) return nMockTime;

    return time(NULL);
}

template <typename T>
T GetTime()
{
    const std::chrono::seconds mocktime{nMockTime};

    return std::chrono::duration_cast<T>(
        mocktime.count() ?
            mocktime :
            std::chrono::microseconds{GetTimeMicros()});
}
template std::chrono::seconds GetTime();
template std::chrono::milliseconds GetTime();
template std::chrono::microseconds GetTime();


void SetMockTime(int64_t nMockTimeIn)
{
    nMockTime = nMockTimeIn;
}

int64_t GetTimeMillis()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t GetTimeMicros()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t GetSystemTimeInSeconds()
{
    return GetTimeMicros()/1000000;
}

void MilliSleep(int64_t n)
{
    boost::this_thread::sleep_for(boost::chrono::milliseconds(n));
}

std::string DateTimeStrFormat(const char* pszFormat, int64_t nTime)
{
    // std::locale takes ownership of the pointer
    std::locale loc(std::locale::classic(), new boost::posix_time::time_facet(pszFormat));
    std::stringstream ss;
    ss.imbue(loc);
    ss << boost::posix_time::from_time_t(nTime);
    return ss.str();
}

struct timeval MillisToTimeval(int64_t nTimeout)
{
    struct timeval timeout;
    timeout.tv_sec  = nTimeout / 1000;
    timeout.tv_usec = (nTimeout % 1000) * 1000;
    return timeout;
}
