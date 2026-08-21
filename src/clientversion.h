// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2016-2017 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#ifndef BITCOIN_CLIENTVERSION_H
#define BITCOIN_CLIENTVERSION_H

#if defined(HAVE_CONFIG_H)
// The real build always takes this branch: config/bitcoin-config.h is
// autoheader-generated straight from configure.ac's
// AC_DEFINE(CLIENT_VERSION_MAJOR, _CLIENT_VERSION_MAJOR, ...) etc., so
// configure.ac is already the single source of truth for every compiled
// binary's version.
#include "config/bitcoin-config.h"
#else

/**
 * client versioning and copyright year
 */

//! These need to be macros, as clientversion.cpp's and bitcoin*-res.rc's voodoo requires it

// Fallback for consumers that don't define HAVE_CONFIG_H - i.e. that never
// ran ./configure (IDE tooling, static analysis, or any other path that
// includes this header without the normal autotools build). Not generated,
// so it must be kept in sync with configure.ac's _CLIENT_VERSION_* macros
// (top of the file) by hand; the actual build never reads these.
#define CLIENT_VERSION_MAJOR 6
#define CLIENT_VERSION_MINOR 0
#define CLIENT_VERSION_REVISION 1
#define CLIENT_VERSION_BUILD 50

//! Set to true for release, false for prerelease or test build
#define CLIENT_VERSION_IS_RELEASE true

/**
 * Copyright year (2009-this)
 * Todo: update this when changing our copyright comments in the source
 */
#define COPYRIGHT_YEAR 2026

#endif //HAVE_CONFIG_H

/**
 * Converts the parameter X to a string after macro replacement on X has been performed.
 * Don't merge these into one macro!
 */
#define STRINGIZE(X) DO_STRINGIZE(X)
#define DO_STRINGIZE(X) #X

//! Copyright string used in Windows .rc files
#define COPYRIGHT_STR "2009-" STRINGIZE(COPYRIGHT_YEAR) " The Bitcoin Core Developers, The Zcash developers, Komodo developers, and Pirate developers"

/**
 * bitcoind-res.rc includes this file, but it cannot cope with real c++ code.
 * WINDRES_PREPROC is defined to indicate that its pre-processor is running.
 * Anything other than a define should be guarded below.
 */

#if !defined(WINDRES_PREPROC)

#include <string>
#include <vector>

static const int MIN_INDEX_VERSION = 6000027;   //Client will reindex if levelDB version below this value
static const int MIN_WALLET_TX_VERSION = 6000027;  //Wallet will zap transactions and rescan if below this value

static const int CLIENT_VERSION =
                           1000000 * CLIENT_VERSION_MAJOR
                         +   10000 * CLIENT_VERSION_MINOR
                         +     100 * CLIENT_VERSION_REVISION
                         +       1 * CLIENT_VERSION_BUILD;

extern const std::string CLIENT_NAME;
extern const std::string CLIENT_BUILD;
extern const std::string CLIENT_DATE;


std::string FormatVersion(int nVersion);
std::string FormatFullVersion();
std::string FormatSubVersion(const std::string& name, int nClientVersion, const std::vector<std::string>& comments);
std::string FormatGitVersion();

#endif // WINDRES_PREPROC

#endif // BITCOIN_CLIENTVERSION_H
