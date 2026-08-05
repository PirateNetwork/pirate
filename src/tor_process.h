// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TOR_PROCESS_H
#define BITCOIN_TOR_PROCESS_H

#include <boost/thread.hpp>

/** Default: automatically launch and manage a bundled tor daemon. */
static const bool DEFAULT_TOR_AUTOSTART = true;

/**
 * Locate and launch a bundled `tor` binary, configured to listen on the same
 * ControlPort address that torcontrol.cpp's TorController already expects
 * (`-torcontrol`, default 127.0.0.1:9051).
 *
 * Returns as soon as the process is spawned - it does not wait for the
 * ControlPort to come up. A background thread (added to `threadGroup`, so it
 * is interrupted/joined like any other thread on shutdown) polls readiness
 * and just logs a warning if it doesn't come up within the startup timeout;
 * StartTorControl()'s own reconnect-with-backoff logic is what actually picks
 * up the connection whenever the port becomes available, so nothing else
 * needs to wait on this.
 *
 * No-op (returns true) if `-torautostart` is disabled.
 */
bool StartEmbeddedTor(boost::thread_group& threadGroup);

/** Stop the bundled tor daemon started by StartEmbeddedTor(), if any. */
void StopEmbeddedTor();

#endif // BITCOIN_TOR_PROCESS_H
