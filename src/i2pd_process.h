// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_I2PD_PROCESS_H
#define BITCOIN_I2PD_PROCESS_H

/** Default: automatically launch and manage a bundled i2pd daemon. */
static const bool DEFAULT_I2PD_AUTOSTART = true;

/**
 * Locate and launch a bundled `i2pd` binary with its SAM API enabled, at the
 * same address src/i2p.cpp's i2p::sam::Session already expects (`-i2psam`,
 * defaulted to 127.0.0.1:7656 when embedded i2pd is enabled and the user
 * hasn't set -i2psam explicitly), and block until the SAM port is accepting
 * connections (or the startup timeout elapses).
 *
 * No-op (returns true) if `-i2pdautostart` is disabled, or if no I2P SAM
 * target ends up configured. Must be called before the i2p::sam::Session is
 * constructed (net.cpp, StartNode()).
 */
bool StartEmbeddedI2Pd();

/** Stop the bundled i2pd daemon started by StartEmbeddedI2Pd(), if any. */
void StopEmbeddedI2Pd();

#endif // BITCOIN_I2PD_PROCESS_H
