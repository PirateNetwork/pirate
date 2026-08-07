// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_I2PD_PROCESS_H
#define BITCOIN_I2PD_PROCESS_H

#include <boost/thread.hpp>

/** Default: automatically launch and manage a bundled i2pd daemon. */
static const bool DEFAULT_I2PD_AUTOSTART = true;

/**
 * Locate and launch a bundled `i2pd` binary with its SAM API enabled, at the
 * same address src/i2p.cpp's i2p::sam::Session already expects (`-i2psam`,
 * defaulted to 127.0.0.1:7656 when embedded i2pd is enabled and the user
 * hasn't set -i2psam explicitly).
 *
 * Returns as soon as the process is spawned - it does not wait for the SAM
 * port to come up. A background thread (added to `threadGroup`, so it is
 * interrupted/joined like any other thread on shutdown) polls readiness and
 * just logs a warning if it doesn't come up within the startup timeout;
 * i2p.cpp's own Session retry-with-backoff logic (ThreadI2PCheck) is what
 * actually picks up the connection whenever the SAM port becomes available,
 * so nothing else needs to wait on this.
 *
 * A second background thread (also in `threadGroup`) supervises the child for
 * the life of the process and relaunches it with backoff if it ever exits on
 * its own (crash, OOM-kill) - see ThreadSuperviseI2Pd() in i2pd_process.cpp.
 * Without that, ThreadI2PCheck's retries would just keep failing forever
 * against a SAM port nothing is listening on anymore.
 *
 * No-op (returns true) if `-i2pdautostart` is disabled, or if no I2P SAM
 * target ends up configured. Must be called before the i2p::sam::Session is
 * constructed (net.cpp, StartNode()).
 */
bool StartEmbeddedI2Pd(boost::thread_group& threadGroup);

/** Stop the bundled i2pd daemon started by StartEmbeddedI2Pd(), if any. */
void StopEmbeddedI2Pd();

#endif // BITCOIN_I2PD_PROCESS_H
