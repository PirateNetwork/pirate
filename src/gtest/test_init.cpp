// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "init.h"
#include "net.h"
#include "wallet/wallet.h"
#include "wallet/walletmanager.h"

// Regression test for a crash-on-exit reported with -disablewallet on Linux:
// StartShutdown() (init.cpp) unconditionally locked pwalletMain->cs_wallet to
// flush the wallet once the node had finished loading, without checking
// whether pwalletMain was actually set. With -disablewallet, pwalletMain
// stays NULL for the life of the process, so calling `stop` (or any other
// path that reaches StartShutdown() post-load) dereferenced a null pointer.
// SIGTERM-driven shutdown didn't hit this, since it takes a different path
// that happened not to call StartShutdown() the same way - only the RPC
// `stop` command did, which is why the crash was reported specifically on
// exit rather than on every shutdown.

extern bool loadComplete;
extern std::atomic<bool> fRequestShutdown;
extern CWallet* pwalletMain;

TEST(init_tests, StartShutdownDoesNotCrashWithWalletDisabled)
{
    bool savedLoadComplete = loadComplete;
    int savedMaxConnections = nMaxConnections;
    CWallet* savedWallet = pwalletMain;
    bool savedShutdown = fRequestShutdown;

    loadComplete = true;
    nMaxConnections = 8;
    pwalletMain = nullptr;
    fRequestShutdown = false;

    EXPECT_NO_THROW(StartShutdown());
    EXPECT_TRUE(fRequestShutdown.load());

    loadComplete = savedLoadComplete;
    nMaxConnections = savedMaxConnections;
    pwalletMain = savedWallet;
    fRequestShutdown = savedShutdown;
}

// -disablewallet means init.cpp's Step 8 never runs, so nothing ever calls
// CWalletManager::Get().RegisterInitialWallet() -- the registry simply stays
// at its default-constructed empty state. This is a same-process regression
// check for that: it doesn't re-run init.cpp's startup gating, just confirms
// an untouched registry looks exactly like the disabled-wallet case is
// supposed to look.
TEST(init_tests, WalletManagerStaysEmptyWhenWalletIsDisabled)
{
    CWalletManager::Get().Reset();

    EXPECT_TRUE(CWalletManager::Get().ListWalletNames().empty());
    EXPECT_TRUE(CWalletManager::GetRequestedWalletName().empty());
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet(CWalletManager::Get().GetActiveWalletName()));
}
