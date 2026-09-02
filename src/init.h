// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2026 The Pirate Chain developers
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

#ifndef BITCOIN_INIT_H
#define BITCOIN_INIT_H

#include <string>

#include "support/allocators/secure.h"
#include "zcash/JoinSplit.hpp"

class CScheduler;
class CWallet;

namespace boost
{
class thread_group;
} // namespace boost

extern CWallet* pwalletMain;
extern ZCJoinSplit* pzcashParams;

void StartShutdown();
bool ShutdownRequested();
/** Interrupt threads */
void Interrupt(boost::thread_group& threadGroup);
void Shutdown();

//Delete Komodo State files
bool DeleteStateFiles();

/***
 * Initialize everything and fire up the services
 * @pre Parameters should be parsed and config file should be read
 * @param threadGroup
 * @param scheduler
 * @returns true on success
 */
bool AppInit2(boost::thread_group& threadGroup, CScheduler& scheduler);

/**
 * Splits one -secondarywalletpassphrase=<name>:<passphrase> entry.
 *
 * The split point is the FIRST ':' in the entry, so a passphrase may itself
 * contain any number of ':' characters and is taken verbatim to the end of
 * the entry -- never truncated at a later colon. This is unambiguous rather
 * than merely convenient: CWalletManager::IsValidWalletName() restricts a
 * wallet name to letters, digits, '.', '_' and '-' (walletmanager.cpp), so
 * ':' can never legitimately appear on the left of the separator.
 *
 * Returns false for a malformed entry (no ':' at all, or an empty name), in
 * which case neither out-parameter is touched. The caller must not log the
 * entry it was given on that path -- a mistyped separator ("name=pass",
 * "name pass") lands here with the plaintext passphrase still inside it.
 *
 * Broken out of AppInit2() purely so it is reachable from gtest; nothing
 * else in the startup path is.
 */
bool ParseSecondaryWalletPassphraseEntry(const std::string& strEntry, std::string& strName,
                                          SecureString& strPassphrase);

/** The help message mode determines what help message to show */
enum HelpMessageMode {
    HMM_BITCOIND,
    HMM_BITCOIN_QT
};

/** Help for options shared between UI and daemon (for -help) */
std::string HelpMessage(HelpMessageMode mode);

#endif // BITCOIN_INIT_H
