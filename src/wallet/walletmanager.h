// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_WALLETMANAGER_H
#define BITCOIN_WALLET_WALLETMANAGER_H

#include "sync.h"
#include "support/allocators/secure.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

class CWallet;
struct CBlockLocator;

/**
 * Multiwallet registry: tracks every CWallet the process has loaded, by name
 * -- zero or more, all symmetric (unloadable, encryptable, reloadable) with
 * no permanently privileged entry. One of them, at most, is "active" at any
 * given time (activeWalletName/SetActiveWallet() below): a wallet-touching
 * RPC request that names a wallet explicitly (/wallet/<name>/) operates on
 * that one; an unscoped request operates on whichever is currently active,
 * resolved via GetWalletForRequest() below or GetActiveWallet() directly
 * (there is no process-global mirror of the active wallet -- every external
 * reader resolves fresh from this registry on every call). The first wallet
 * ever loaded into an empty registry becomes active automatically; every
 * later load leaves active status where
 * it was until SetActiveWallet() is called explicitly. A core subset of
 * wallet RPCs (see IsMultiWalletAwareRPC, rpc/server.h) can be routed to a
 * specific, explicitly-named wallet this way; everything else still only
 * ever runs against whichever wallet is active, enforced by the same gate in
 * CRPCTable::execute().
 */
class CWalletManager
{
public:
    static CWalletManager& Get();

    CWalletManager(const CWalletManager&) = delete;
    CWalletManager& operator=(const CWalletManager&) = delete;

    // Startup (init.cpp) and zcbenchmarks.cpp's benchmark_loadwallet() only:
    // registers a wallet the caller has already constructed, without going
    // through LoadWallet()'s file-opening machinery. Not a general "switch
    // the active wallet" API (see SetActiveWallet() for that) -- it
    // unconditionally overwrites whichever wallet is currently active,
    // regardless of prior state. The wallet registered here becomes active
    // the same way any other first-loaded wallet would, via LoadWallet()'s
    // own empty-registry-promotion rule.
    void RegisterInitialWallet(const std::string& name, CWallet* wallet);

    // fRescan/nRescanHeight/fSalvage/fZapWalletTxes mirror what init.cpp's
    // startup path offers via -rescan/-rescanheight/-salvagewallet/
    // -zapwallettxes, applied to whichever wallet is loaded here.
    // fZapWalletTxes implies fRescan, same as -zapwallettxes implies -rescan.
    // fAllowCreate is for CreateWallet()'s own internal delegation below
    // ONLY -- every other caller (the loadwallet RPC, multi-`-wallet=`
    // startup parsing, tests) must leave it false. When true, it skips the
    // leading "file must already exist" check so a brand-new file can be
    // auto-created by CWallet::LoadWallet() (the per-instance DB open,
    // called further down this same function -- a different method that
    // happens to share the name) the same way it already does for a
    // legitimately missing file; every check afterwards (alias detection,
    // concurrent-load reservation, etc.) still runs unchanged either way.
    //
    // strPassphrase: per-wallet-unlock support. If the file turns out to be
    // encrypted (CWallet::InitalizeCryptedLoad() returns DB_LOAD_CRYPTED),
    // this is fed synchronously into the same
    // InitalizeCryptedLoad()/SetDBCrypted()/LoadCryptedSeedFromDB()/
    // OpenWallet() handshake init.cpp runs for the default wallet at startup
    // -- but without that startup path's blocking wait for an out-of-band
    // unlock (a GUI dialog, or the special openwallet RPC): either the
    // passphrase given here works immediately, or the load fails immediately
    // with a clear error. Leave empty for an unencrypted wallet, or to
    // deliberately fail closed with a clear "needs its passphrase" error
    // instead of silently proceeding unlocked against an encrypted one.
    //
    // pfPassphraseRequired, if given, is set to true on a false return
    // specifically when the reason was "the file is encrypted and
    // strPassphrase was empty or wrong" -- distinct from every other failure
    // reason (bad name, missing file, corrupt DB, etc.), which leave it
    // untouched. Lets a caller like the GUI's open-wallet flow decide to
    // prompt for a passphrase and retry without parsing strError's text to
    // guess why this failed.
    bool LoadWallet(const std::string& name, std::string& strError,
                     bool fRescan = false, int nRescanHeight = 0,
                     bool fSalvage = false, bool fZapWalletTxes = false,
                     bool fAllowCreate = false,
                     const SecureString& strPassphrase = SecureString(),
                     bool* pfPassphraseRequired = nullptr);

    // LoadWallet() above only ever loads a file that already exists by
    // default (see fAllowCreate just above) -- CreateWallet() is the
    // opposite: it rejects a name whose file already exists, then delegates
    // straight to LoadWallet(name, ..., /*fAllowCreate=*/true), which
    // handles "no file yet" correctly (CWallet::LoadWallet() auto-creates
    // the file, returning fFirstRun=true, which LoadWallet()'s own
    // registration/catch-up tail already special-cases). All that's added
    // on top is the seed generation a brand-new wallet needs. `seedPhraseOut`
    // receives the newly-generated seed phrase so the caller (RPC result,
    // then the GUI) can prompt the user to back it up immediately.
    // recoveryPhrase/recoveryLangCode restore this wallet's HD seed from an
    // existing phrase instead of generating a brand-new random one -- the
    // createwallet RPC's and the Qt first-run "restore" flow's shared
    // implementation. Leave recoveryPhrase empty (the default) for the
    // random-seed behavior.
    bool CreateWallet(const std::string& name, std::string& strError, std::string& seedPhraseOut,
                       const SecureString& recoveryPhrase = SecureString(), uint32_t recoveryLangCode = 0);

    // Refuses to unload whichever wallet is currently active (see
    // SetActiveWallet() -- call that first, with a different name or "", to
    // make this one eligible) and refuses a wallet still bound to the mining
    // thread (see GetMiningWallet(), miner.h) regardless of active status.
    // Both exist for the same reason: GetActiveWallet() (below) is read
    // without any lock held past its own call by miner.cpp, the
    // Crypto-Conditions framework, and the Qt GUI -- deleting a wallet either
    // of those could still be dereferencing is a use-after-free these two
    // refusals exist specifically to prevent.
    bool UnloadWallet(const std::string& name, std::string& strError);

    // Used only by encryptwallet()'s failed-encryption recovery path
    // (wallet/rpcwallet.cpp): a CWallet::EncryptWallet() call that fails
    // partway through can leave the in-memory object's crypto state (keys,
    // vMasterKey, seedEncyptionFP, etc.) in an inconsistent condition that
    // isn't safe to keep using, even though the caller has already restored
    // the on-disk file from its pre-attempt backup. Discards that object and
    // its registry entry.
    //
    // Unlike UnloadWallet(), this tolerates refcount == 1 rather than
    // requiring 0: the calling request's own RPCWalletRequestGuard is
    // holding exactly one ref on this wallet for the whole duration of the
    // encryptwallet call, so demanding 0 would make the recovery path
    // unreachable by construction. It deliberately does NOT tolerate
    // anything above that. An earlier revision skipped the check entirely on
    // the theory that the calling request is the only possible ref holder;
    // that is false. Refs are also taken by (a) any second, concurrent RPC
    // request selecting the same wallet -- RPCWalletRequestGuard's
    // constructor runs in the dispatch layer and only needs cs_wallets, so
    // it is not blocked by this request's cs_main hold, and plenty of
    // rewired wallet RPCs (walletlock, the settings setters, ...) never take
    // cs_main in their bodies at all; (b) a still-running or unpolled
    // AsyncRPCOperation (asyncrpcoperation.cpp), which executes on its own
    // thread; and (c) the Qt GUI, which holds a ref for as long as a wallet
    // is open in a tab (qt/pirateoceangui.cpp). Deleting the CWallet* out
    // from under any of those is a use-after-free, and the gap between the
    // delete and the caller's follow-up LoadWallet() is worse still --
    // GetWalletForRequest() falls back to the active wallet for an
    // unresolvable name, so a concurrent request scoped to this secondary
    // wallet would silently run against the *active* wallet instead. Refusing here
    // leaves the caller on its pre-existing StartShutdown() path, which is
    // safe.
    //
    // Bumps this name's generation implicitly (via the subsequent
    // LoadWallet() call the caller is expected to make), so any other
    // request's already-outstanding ref against the old generation safely
    // no-ops on release instead of corrupting the new entry's refcount (see
    // ReleaseRefIfCurrent()). The caller must not
    // dereference the CWallet* it resolved before calling this again --
    // notably, it must release any lock it holds on that object's own
    // cs_wallet first, since the object is deleted here. The caller must
    // also make this call *before* detaching the wallet's file from the
    // shared BerkeleyDB environment (CloseDb/CheckpointLSN/mapFileUseCount
    // erase): a failed EncryptWallet() can leave the wallet's own
    // pwalletdbEncryption handle open, and ~CWallet deleting it after that
    // erase would decrement an already-removed mapFileUseCount entry to -1,
    // permanently skewing that file's use count for the rest of the process.
    //
    // Never valid against the active wallet -- refuses, matching
    // UnloadWallet()'s own active-wallet refusal: encryption-failure
    // recovery is a narrow, security-sensitive path, and a reload of the
    // sole/active wallet falls back to the caller's StartShutdown() path
    // instead, a fine outcome for what should be a rare failure.
    bool DiscardWalletAfterFailedEncryption(const std::string& name, std::string& strError);

    // Writes a best-chain checkpoint to every currently loaded wallet
    // (including the active one). Called from StartShutdown() (init.cpp) so
    // a secondary wallet's on-disk checkpoint doesn't go stale by however
    // many blocks passed since its last periodic flush. Caller is expected
    // to already hold cs_main while reading the chain state passed in here
    // (SetBestChain() itself only asserts cs_wallet, taken per-entry below).
    void CheckpointAllWallets(const CBlockLocator& locator, int height);

    std::vector<std::string> ListWalletNames() const;
    CWallet* GetWallet(const std::string& name) const;
    std::string GetActiveWalletName() const;
    bool IsActiveWallet(const std::string& name) const;
    // Resolves activeWalletName against mapWallets fresh, under this same
    // lock, on every call -- no caching, so no stale-pointer risk from a
    // concurrent SetActiveWallet(). The sanctioned way for a
    // non-request-scoped caller to resolve "whichever wallet is active right
    // now": the mining thread's initial pin point, the Crypto-Conditions
    // framework's null-wallet fallback, the Qt GUI, zcbenchmarks, and the
    // wallet_fees.cpp fee-fallback helpers all use this. Returns nullptr if
    // no wallet is active. Same accepted lockless-read
    // tradeoff as GetWallet()/GetWalletForRequest() -- this does not
    // ref-count the returned pointer, so a caller that stores it past this
    // call has no stronger guarantee against a concurrent unload than a raw
    // pointer read ever gave; an active wallet still can't be unloaded while
    // active (UnloadWallet()'s own refusal), which is what actually protects
    // a lockless reader here.
    CWallet* GetActiveWallet() const;

    // Changes which loaded wallet an unscoped RPC request (no /wallet/<name>/
    // URI segment) resolves to, and which wallet every non-request-scoped
    // call site across the codebase (miner.cpp, the Crypto-Conditions
    // framework, the Qt GUI, ...) sees the next time it calls
    // GetActiveWallet() -- all of those resolve fresh from activeWalletName
    // under cs_wallets rather than caching anything, so this update is all
    // any of them needs (see the design note on activeWalletName below for
    // the accepted lockless-read tradeoff this implies).
    //
    // name == "" deactivates: activeWalletName becomes empty, so
    // GetActiveWallet() starts returning nullptr, while every wallet named
    // here stays loaded and fully synced to the chain tip. This is the only
    // way to reach that state without unloading anything, and therefore the
    // only way to make the last remaining loaded wallet eligible for
    // unloadwallet (see UnloadWallet()'s own refusal of the active wallet).
    // A non-empty name must already identify a currently-loaded wallet;
    // RPC_WALLET_NOT_FOUND otherwise, activeWalletName left unchanged.
    bool SetActiveWallet(const std::string& name, std::string& strError);

    // Held for the duration of a request routed to `name`. Kept as a simple
    // name-keyed pair for direct/test use; RPCWalletRequestGuard uses the
    // generation-safe pair below instead, since a name-only Release can't
    // tell "the wallet I held a ref on" apart from "whatever now lives under
    // that name" if an unload+reload happened first.
    bool AddRef(const std::string& name);
    void ReleaseRef(const std::string& name);

    // Every registry entry, including whichever one is currently active, is
    // uniformly ref-countable -- there is no exempt entry that skips
    // refcounting.
    enum class ResolveOutcome { NotFound, Held };
    // name is populated only alongside outcome == Held, and specifically
    // matters for ResolveAndHoldActiveForRequest(): the caller there didn't
    // supply a name (that's the whole point of resolving "whichever wallet is
    // active"), but still needs the actual registry key this ref was taken
    // under in order to release it correctly later via ReleaseRefIfCurrent()
    // -- which is keyed by name, not by "was this the active wallet."
    struct ResolvedWallet { ResolveOutcome outcome; uint64_t generation; std::string name; };

    // Looks up `name` and, if it identifies a currently-loaded wallet, holds
    // a reference to it -- both under one lock acquisition, so there's no gap
    // between "confirmed it's loaded" and "started holding a ref" for
    // UnloadWallet's refcount check to race against. Returns the entry's
    // generation regardless of outcome so a later release can target the
    // exact entry instance (see ReleaseRefIfCurrent).
    ResolvedWallet ResolveAndHoldForRequest(const std::string& name);

    // Same as above but resolves whichever wallet is currently active,
    // instead of a caller-given name -- used by RPCWalletRequestGuard for an
    // unscoped request (no /wallet/<name>/ segment). Returns NotFound when
    // no wallet is currently active. An unscoped request holds a ref for the
    // same reason a scoped one does: the active wallet can be unloaded once
    // deactivated.
    ResolvedWallet ResolveAndHoldActiveForRequest();

    // Releases a ref taken by ResolveAndHoldForRequest, but only if `name`
    // still identifies the same entry instance (same generation) -- guards
    // against releasing into a *different* wallet that was loaded under the
    // same name after the original one was unloaded, which a plain name
    // lookup can't distinguish and which would otherwise underflow the new
    // entry's refcount and permanently pin it as "in use".
    void ReleaseRefIfCurrent(const std::string& name, uint64_t generation);

    void FlushAndUnloadAllExceptActiveWallet();
    void Reset();

    // Shared by the multiwallet RPCs and by multi-`-wallet=` startup
    // parsing. Whitelists letters/digits/'.'/'_'/'-' (a superset of
    // SanitizeFilename()'s alphanumeric-only charset, so conventional names
    // like "wallet.dat" remain loadable) and separately rejects "." and "..";
    // '/' and '\\' are never in the allowed set, so a name containing "../"
    // or an absolute path can't even be constructed, let alone slip past a
    // check.
    static bool IsValidWalletName(const std::string& name, std::string& strError);

    // Thread-local read: the wallet this request has resolved to and pinned
    // for its own duration -- for a /wallet/<name>/ request, `name` itself;
    // for an unscoped request, whichever wallet was active at the moment
    // RPCWalletRequestGuard resolved it (NOT re-read live afterward -- see
    // WasWalletExplicitlySelected() for the distinction this exists to
    // preserve). Empty only when nothing was resolved at all (an unscoped
    // request with no wallet currently active).
    static std::string GetRequestedWalletName();

    // True if this request's URI actually named a wallet
    // (/wallet/<name>/...), false for an unscoped request -- independent of
    // GetRequestedWalletName(), which is non-empty in both cases once a
    // wallet resolves. Exists specifically for callers like
    // z_buildrawtransaction (rpc/rawtransaction.cpp) that need to tell "the
    // caller explicitly picked one wallet" apart from "nothing was picked,
    // search every loaded wallet", a distinction GetRequestedWalletName()
    // alone can't make once it pins the resolved name for the unscoped case
    // too.
    static bool WasWalletExplicitlySelected();

    // Resolves the current thread's selected wallet (via
    // GetRequestedWalletName(), a name-keyed registry lookup) to an actual
    // CWallet* for a multiwallet-aware RPC to operate on. GetRequestedWalletName()
    // is pinned to the *resolved* wallet's name for an unscoped request too,
    // so the lookup here finds the same object GetActiveWallet() would
    // currently return, by name rather than by re-resolving live.
    // GetActiveWallet() is only the fallback for the two cases where
    // GetRequestedWalletName() is empty: nothing resolved at all (an
    // unscoped request with no wallet active), or -- should not happen in
    // practice, the gate and RPCWalletRequestGuard::IsResolved() already
    // guarantee otherwise by the time this runs -- a resolved name that
    // somehow isn't found in the registry. Only meaningful to call from an
    // RPC that IsMultiWalletAwareRPC() has already let through the dispatch
    // gate in CRPCTable::execute() -- it does not itself re-validate the
    // selection.
    static CWallet* GetWalletForRequest();

private:
    CWalletManager() = default;

    struct Entry
    {
        CWallet* wallet;
        std::atomic<int> refcount;
        uint64_t generation;

        Entry(CWallet* walletIn, uint64_t generationIn)
            : wallet(walletIn), refcount(0), generation(generationIn) {}
    };

    mutable CCriticalSection cs_wallets;
    std::map<std::string, Entry> mapWallets;
    // Empty is a legal, persistent state -- "no wallet is currently active" --
    // not just a startup transient, since SetActiveWallet("") can reach it
    // deliberately at any point while wallets remain loaded. Whenever this is
    // non-empty it always names a live mapWallets entry; the two are only
    // ever updated together, under this same lock (RegisterInitialWallet(),
    // SetActiveWallet(), and LoadWallet()'s first-wallet-into-an-empty-
    // registry promotion).
    std::string activeWalletName;
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
    // Every name this process has ever opened a CWallet against, never
    // erased for the life of the process (only Reset(), i.e. shutdown and
    // gtest fixture teardown, clears it). LoadWallet()'s pre-open
    // CWallet::Verify() step is designed to run once, before any CDB for
    // that file has ever existed here, and bitdb's own mapFileUseCount is
    // not a usable record of that: CloseWalletDbFile(), CDBEnv::Flush() and
    // CDB::Rewrite() all erase a file's entry once its count reaches zero,
    // so a plain unload+reload of the same name would otherwise look like a
    // first-ever open and re-run Verify() -- which, after an
    // encryptwallet-driven CDB::Rewrite(), reports spurious corruption and
    // triggers CWalletDB::Recover()'s destructive auto-salvage on a
    // perfectly good file.
    std::set<std::string> everLoadedNames;
};

/**
 * RAII thread-local wallet-selection guard for one RPC request. Must be
 * exception-safe: HTTP worker threads are long-lived and reused across
 * unrelated requests (DEFAULT_HTTP_THREADS in httpserver.cpp), so a
 * thread-local left set after tableRPC.execute() throws would silently
 * misdirect the next unrelated request on that thread to the wrong wallet.
 * Saving and restoring the prior value (rather than assuming it was empty)
 * also makes nesting safe -- exercised for real by z_buildrawtransaction
 * (rpc/rawtransaction.cpp), which constructs a second, inner guard scoped to
 * whichever wallet it found while the outer, request-level guard from
 * httprpc.cpp is still alive.
 */
class RPCWalletRequestGuard
{
public:
    explicit RPCWalletRequestGuard(const std::string& name);
    ~RPCWalletRequestGuard();

    RPCWalletRequestGuard(const RPCWalletRequestGuard&) = delete;
    RPCWalletRequestGuard& operator=(const RPCWalletRequestGuard&) = delete;

    // False when a non-empty `name` didn't resolve to any currently-loaded
    // wallet (unknown or unloaded-in-the-meantime), or when `name` was empty
    // (no wallet segment in the URI) and no wallet is currently active. The
    // caller is expected to throw
    // RPC_WALLET_NOT_FOUND itself so it can word the error the way the rest
    // of that call site does; httprpc.cpp deliberately does NOT do this for
    // the empty-name case (a zero-wallet node must still be able to serve
    // e.g. createwallet/getinfo), so an unresolved empty-name guard is not,
    // on its own, treated as a request-ending error the way an unresolved
    // named one is.
    bool IsResolved() const { return fResolved; }

private:
    std::string strName;
    std::string strPrevWalletName;
    // Saved/restored the same way strPrevWalletName is, so a nested guard
    // (see the class comment above -- z_buildrawtransaction constructs one
    // for real) doesn't leak its own selection into the outer request once
    // it's destroyed.
    bool fPrevExplicit;
    // The actual registry key the held ref (if any) was taken under -- same
    // as strName for a scoped request, but strName is empty for an unscoped
    // one resolved against the active wallet, so this is what
    // ReleaseRefIfCurrent() needs to release the right entry (see
    // ResolvedWallet::name's own doc comment).
    std::string strResolvedName;
    bool fRefHeld;
    bool fResolved;
    uint64_t generation;
};

#endif // BITCOIN_WALLET_WALLETMANAGER_H
