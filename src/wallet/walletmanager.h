// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_WALLETMANAGER_H
#define BITCOIN_WALLET_WALLETMANAGER_H

#include "sync.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

class CWallet;

/**
 * Phase 1 multiwallet registry: tracks every CWallet the process has loaded
 * (the default wallet plus any secondary wallets opened via loadwallet), by
 * name. Secondary wallets are load/unload/list-able only in this phase --
 * every wallet-scoped RPC other than loadwallet/unloadwallet/listwallets
 * still runs exclusively against the default wallet (enforced in
 * CRPCTable::execute()), so nothing here needs to make a secondary wallet's
 * CWallet* safe to actually spend from yet.
 */
class CWalletManager
{
public:
    static CWalletManager& Get();

    CWalletManager(const CWalletManager&) = delete;
    CWalletManager& operator=(const CWalletManager&) = delete;

    void RegisterDefaultWallet(const std::string& name, CWallet* wallet);

    bool LoadWallet(const std::string& name, std::string& strError);
    bool UnloadWallet(const std::string& name, std::string& strError);

    std::vector<std::string> ListWalletNames() const;
    CWallet* GetWallet(const std::string& name) const;
    std::string GetDefaultWalletName() const;
    bool IsDefaultWallet(const std::string& name) const;

    // Held for the duration of a request routed to `name`; forward-looking
    // infra for phase 2. Kept as a simple name-keyed pair for direct/test use;
    // RPCWalletRequestGuard uses the generation-safe pair below instead, since
    // a name-only Release can't tell "the wallet I held a ref on" apart from
    // "whatever now lives under that name" if an unload+reload happened first.
    bool AddRef(const std::string& name);
    void ReleaseRef(const std::string& name);

    enum class ResolveOutcome { NotFound, IsDefault, HeldSecondary };
    struct ResolvedWallet { ResolveOutcome outcome; uint64_t generation; };

    // Looks up `name` and, if it identifies a currently-loaded secondary
    // wallet, holds a reference to it -- both under one lock acquisition, so
    // there's no gap between "confirmed it's loaded" and "started holding a
    // ref" for UnloadWallet's refcount check to race against. Returns the
    // entry's generation regardless of outcome so a later release can target
    // the exact entry instance (see ReleaseRefIfCurrent).
    ResolvedWallet ResolveAndHoldForRequest(const std::string& name);

    // Releases a ref taken by ResolveAndHoldForRequest, but only if `name`
    // still identifies the same entry instance (same generation) -- guards
    // against releasing into a *different* wallet that was loaded under the
    // same name after the original one was unloaded, which a plain name
    // lookup can't distinguish and which would otherwise underflow the new
    // entry's refcount and permanently pin it as "in use".
    void ReleaseRefIfCurrent(const std::string& name, uint64_t generation);

    void FlushAndUnloadAllSecondaryWallets();
    void Reset();

    // Reused by the new multiwallet RPCs and by multi-`-wallet=` startup
    // parsing. Whitelists letters/digits/'.'/'_'/'-' (a superset of
    // SanitizeFilename()'s alphanumeric-only charset, so conventional names
    // like "wallet.dat" remain loadable) and separately rejects "." and "..";
    // '/' and '\\' are never in the allowed set, so a name containing "../"
    // or an absolute path can't even be constructed, let alone slip past a
    // check.
    static bool IsValidWalletName(const std::string& name, std::string& strError);

    // Thread-local read: empty string means no override is in effect for the
    // calling thread (the request should run against the default wallet).
    static std::string GetRequestedWalletName();

private:
    CWalletManager() = default;

    struct Entry
    {
        CWallet* wallet;
        bool isDefault;
        std::atomic<int> refcount;
        uint64_t generation;

        Entry(CWallet* walletIn, bool isDefaultIn, uint64_t generationIn)
            : wallet(walletIn), isDefault(isDefaultIn), refcount(0), generation(generationIn) {}
    };

    mutable CCriticalSection cs_wallets;
    std::map<std::string, Entry> mapWallets;
    std::string defaultWalletName;
    // Every Entry gets a fresh value from this counter, never reused for the
    // life of the process -- see ReleaseRefIfCurrent.
    std::atomic<uint64_t> nextGeneration{1};
    // Names with a LoadWallet() call currently past the fast, cs_wallets-held
    // section and into the slow CWallet::Verify()/construction/load work,
    // which runs without cs_wallets held (see LoadWallet). Prevents two
    // concurrent loads of the *same* name from both reaching Verify() with a
    // stale "never touched" assumption, without serializing loads of
    // *different* names -- and therefore every other cs_wallets-holding
    // caller (RPCWalletRequestGuard, listwallets, unloadwallet) -- behind
    // however long one large wallet's LoadWallet() call takes.
    std::set<std::string> loadingNames;
};

/**
 * RAII thread-local wallet-selection guard for one RPC request. Must be
 * exception-safe: HTTP worker threads are long-lived and reused across
 * unrelated requests (DEFAULT_HTTP_THREADS in httpserver.cpp), so a
 * thread-local left set after tableRPC.execute() throws would silently
 * misdirect the next unrelated request on that thread to the wrong wallet.
 * Saving and restoring the prior value (rather than assuming it was empty)
 * also makes nesting safe, even though nothing nests in phase 1.
 */
class RPCWalletRequestGuard
{
public:
    explicit RPCWalletRequestGuard(const std::string& name);
    ~RPCWalletRequestGuard();

    RPCWalletRequestGuard(const RPCWalletRequestGuard&) = delete;
    RPCWalletRequestGuard& operator=(const RPCWalletRequestGuard&) = delete;

    // False only when a non-empty `name` didn't resolve to any currently-
    // loaded wallet (unknown or unloaded-in-the-meantime); the caller is
    // expected to throw RPC_WALLET_NOT_FOUND itself so it can word the error
    // the way the rest of that call site does. An empty `name` (no wallet
    // segment in the URI) always resolves.
    bool IsResolved() const { return fResolved; }

private:
    std::string strName;
    std::string strPrevWalletName;
    bool fRefHeld;
    bool fResolved;
    uint64_t generation;
};

#endif // BITCOIN_WALLET_WALLETMANAGER_H
