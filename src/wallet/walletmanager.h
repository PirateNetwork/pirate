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
 * Multiwallet registry: tracks every CWallet the process has loaded (the
 * default wallet plus any secondary wallets opened via loadwallet), by name.
 * As of phase 2, a core subset of wallet RPCs (see IsMultiWalletAwareRPC,
 * rpc/server.h) actually operate on a selected secondary wallet via
 * GetWalletForRequest() below; everything else still runs exclusively
 * against the default wallet, enforced by the same gate in
 * CRPCTable::execute().
 */
class CWalletManager
{
public:
    static CWalletManager& Get();

    CWalletManager(const CWalletManager&) = delete;
    CWalletManager& operator=(const CWalletManager&) = delete;

    void RegisterDefaultWallet(const std::string& name, CWallet* wallet);

    // fRescan/nRescanHeight/fSalvage/fZapWalletTxes mirror what init.cpp's
    // startup path offers the default wallet via -rescan/-rescanheight/
    // -salvagewallet/-zapwallettxes -- Phase 5 of the multiwallet effort
    // exposed the same actions for a secondary wallet loaded here instead of
    // only ever applying to the default wallet (or, for -salvagewallet,
    // applying process-wide to every wallet loaded regardless of intent).
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

    // Phase 6: LoadWallet() above only ever loads a file that already
    // exists by default (see fAllowCreate just above) -- CreateWallet() is
    // the opposite: it rejects a name whose file already exists, then
    // delegates straight to LoadWallet(name, ..., /*fAllowCreate=*/true),
    // which handles "no file yet" correctly once that flag lets it past its
    // own existence check (CWallet::LoadWallet() then auto-creates the file,
    // returning fFirstRun=true, which LoadWallet()'s own registration/catch-up
    // tail already special-cases). This deliberately reuses that whole tail
    // unchanged rather than duplicating it, since it's the same delicate,
    // already-audited locking/exception-safety sequence either way. All
    // that's added on top is the seed generation a brand-new wallet needs --
    // mirroring init.cpp's own fresh-default-wallet setup, minus the
    // interactive GUI seed-phrase-confirmation flow that only makes sense
    // during first-run startup, not for adding a wallet to an already-running
    // node. `seedPhraseOut` receives the newly-generated seed phrase so the
    // caller (RPC result, then the GUI) can prompt the user to back it up
    // immediately -- restoring a new wallet from a caller-supplied phrase is
    // out of scope (use -seedphrase/-wallet= at startup instead).
    bool CreateWallet(const std::string& name, std::string& strError, std::string& seedPhraseOut);

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
    // GetWalletForRequest() falls back to pwalletMain for an unresolvable
    // name, so a concurrent request scoped to this secondary wallet would
    // silently run against the *default* wallet instead. Refusing here
    // leaves the caller on its pre-existing StartShutdown() path, which is
    // safe.
    //
    // Bumps this name's generation implicitly (via the subsequent
    // LoadWallet() call the caller is expected to make), so any other
    // request's already-outstanding ref against the old generation safely
    // no-ops on release instead of corrupting the new entry's refcount (see
    // ReleaseRefIfCurrent()). Never valid against the default wallet --
    // refuses, matching UnloadWallet() (there is no reload path for it; see
    // the "no-default-wallet redesign" backlog item). The caller must not
    // dereference the CWallet* it resolved before calling this again --
    // notably, it must release any lock it holds on that object's own
    // cs_wallet first, since the object is deleted here. The caller must
    // also make this call *before* detaching the wallet's file from the
    // shared BerkeleyDB environment (CloseDb/CheckpointLSN/mapFileUseCount
    // erase): a failed EncryptWallet() can leave the wallet's own
    // pwalletdbEncryption handle open, and ~CWallet deleting it after that
    // erase would decrement an already-removed mapFileUseCount entry to -1,
    // permanently skewing that file's use count for the rest of the process.
    bool DiscardWalletAfterFailedEncryption(const std::string& name, std::string& strError);

    // Writes a best-chain checkpoint to every currently loaded wallet
    // (including the default one). Called from StartShutdown() (init.cpp) so
    // a secondary wallet's on-disk checkpoint doesn't go stale by however
    // many blocks passed since its last periodic flush -- previously only
    // pwalletMain ever got this checkpoint written on shutdown. Caller is
    // expected to already hold cs_main while reading the chain state passed
    // in here (SetBestChain() itself only asserts cs_wallet, taken per-entry
    // below).
    void CheckpointAllWallets(const CBlockLocator& locator, int height);

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

    // Resolves the current thread's selected wallet (via
    // GetRequestedWalletName()) to an actual CWallet* for a rewired RPC to
    // operate on: pwalletMain when no wallet was selected (default request
    // path, unchanged from before multiwallet existed), or the resolved
    // secondary otherwise. Only meaningful to call from an RPC that
    // IsMultiWalletAwareRPC() has already let through the dispatch gate in
    // CRPCTable::execute() -- it does not itself re-validate the selection,
    // and falls back to pwalletMain if the name somehow isn't found (should
    // not happen in practice: the gate and RPCWalletRequestGuard::IsResolved()
    // already guarantee it exists by the time a rewired RPC runs).
    static CWallet* GetWalletForRequest();

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
