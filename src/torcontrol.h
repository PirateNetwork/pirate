// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Functionality for communicating with Tor.
 */
#ifndef BITCOIN_TORCONTROL_H
#define BITCOIN_TORCONTROL_H

#include "scheduler.h"

extern const std::string DEFAULT_TOR_CONTROL;
// Where the embedded tor daemon's SOCKS proxy is expected to be listening -
// read here and by tor_process.cpp, which is what actually configures the
// managed daemon's SocksPort (reassigning and overriding this default via
// mapArgs the same way it already does for -torcontrol if the configured
// port is taken). For an externally-managed tor (-torautostart=0), this is
// just the conventional default port a system tor install listens on.
extern const std::string DEFAULT_TOR_SOCKS;
static const bool DEFAULT_LISTEN_ONION = true;

void StartTorControl(boost::thread_group& threadGroup, CScheduler& scheduler);
void InterruptTorControl();
void StopTorControl();

#endif /* BITCOIN_TORCONTROL_H */
