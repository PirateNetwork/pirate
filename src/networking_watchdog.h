// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NETWORKING_WATCHDOG_H
#define BITCOIN_NETWORKING_WATCHDOG_H

#include <cstdint>
#include <string>

/**
 * Launch the pirate-networking helper (see pirate-networking.cpp), a tiny
 * standalone binary whose only job is to terminate the embedded tor/i2pd
 * daemons (see tor_process.cpp, i2pd_process.cpp) if this process disappears
 * without calling StopNetworkingWatchdog() first - a crash or an OOM-kill,
 * either of which would otherwise leave them running orphaned forever, since
 * ~CManagedProcess() never gets to run in that case.
 *
 * Call once during startup, before StartEmbeddedTor()/StartEmbeddedI2Pd() so
 * NotifyNetworkingWatchdog() has somewhere to report their pids to. No-op
 * (returns false, logs, and does not affect startup otherwise) if the
 * pirate-networking binary can't be found.
 */
bool StartNetworkingWatchdog();

/**
 * Tell the watchdog to terminate `name`'s process `pid` if we disappear
 * before calling StopNetworkingWatchdog(). Call once right after each
 * embedded daemon is successfully spawned. No-op if the watchdog isn't
 * running (StartNetworkingWatchdog() was never called or failed).
 */
void NotifyNetworkingWatchdog(const std::string& name, int64_t pid);

/**
 * Tell the watchdog this is a clean shutdown - it should stand down without
 * touching anything it was told about - and release our end of the pipe to
 * it. Call during Shutdown(), after StopEmbeddedTor()/StopEmbeddedI2Pd()
 * have already stopped the daemons themselves. No-op if the watchdog isn't
 * running.
 */
void StopNetworkingWatchdog();

#endif // BITCOIN_NETWORKING_WATCHDOG_H
