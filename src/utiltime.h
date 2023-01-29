// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#pragma once

#include <stdint.h>
#include <string>

/***
 * Set the current time (for unit testing)
 * @param nMocKtimeIn the time that will be returned by GetTime()
 */
void SetMockTime(int64_t nMockTimeIn);

/***
 * @brief Get the current time
 */
int64_t GetTime();

/***
 * @returns the system time in milliseconds since the epoch (1/1/1970)
 */
int64_t GetTimeMillis();

/****
 * @returns the system time in microseconds since the epoch (1/1/1970)
 */
int64_t GetTimeMicros();

/****
 * @param n the number of milliseconds to put the current thread to sleep
 */
void MilliSleep(int64_t n);

/***
 * Convert time into a formatted string
 * @param pszFormat the format
 * @param nTime the time
 * @returns the time in the format
 */
std::string DateTimeStrFormat(const char* pszFormat, int64_t nTime);
