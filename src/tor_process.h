// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TOR_PROCESS_H
#define BITCOIN_TOR_PROCESS_H

/** Default: automatically launch and manage a bundled tor daemon. */
static const bool DEFAULT_TOR_AUTOSTART = true;

/**
 * Locate and launch a bundled `tor` binary, configured to listen on the same
 * ControlPort address that torcontrol.cpp's TorController already expects
 * (`-torcontrol`, default 127.0.0.1:9051), and block until that port is
 * accepting connections (or the startup timeout elapses).
 *
 * No-op (returns true) if `-torautostart` is disabled. Must be called before
 * StartTorControl() so the very first control-port connection attempt has
 * something to connect to.
 */
bool StartEmbeddedTor();

/** Stop the bundled tor daemon started by StartEmbeddedTor(), if any. */
void StopEmbeddedTor();

#endif // BITCOIN_TOR_PROCESS_H
