// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/walletmanager.h"

#include "util.h"
#include "wallet/db.h"
#include "wallet/wallet.h"

#include <boost/filesystem.hpp>

namespace {
// Empty means "no override for this thread" -- the request runs against the
// default wallet, exactly like before multiwallet existed.
thread_local std::string g_requestedWalletName;

// Deliberately narrower than SanitizeFilename() (util/strencodings.cpp),
// which is alphanumeric-only and would reject "wallet.dat" itself -- the
// default wallet's own on-disk name, and the plan's own worked example for a
// secondary wallet. '.', '_' and '-' are added on top of that; '/' and '\\'
// stay structurally excluded either way, so a name still can't escape the
// datadir no matter what letters/digits/./-/_ it's paired with.
const std::string SAFE_WALLET_NAME_CHARS =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-";

// Closes and checkpoints exactly this wallet's own file within the shared
// BerkeleyDB environment. Deliberately NOT CWallet::Flush(true)/
// CDBEnv::Flush(true): that's the *process shutdown* path -- it closes and
// removes the whole shared environment, which would take the still-running
// default wallet down with it. Mirrors the per-file pattern BackupWallet()
// already uses (wallet/walletdb.cpp) to detach a single file safely.
void CloseWalletDbFile(const std::string& name)
{
    LOCK(bitdb->cs_db);
    if (!bitdb->mapFileUseCount.count(name) || bitdb->mapFileUseCount[name] == 0) {
        bitdb->CloseDb(name);
        bitdb->CheckpointLSN(name);
        bitdb->mapFileUseCount.erase(name);
    }
    // If some other in-flight CDB handle still has it open, leave it alone.
    // In phase 1 this branch is unreachable in practice: UnloadWallet only
    // gets here once its own refcount check has passed (walletmanager.cpp),
    // and no code path exists yet that opens a second CDB against a
    // secondary wallet's file. Left as a no-op rather than an assert so it
    // fails safe if that stops being true later, instead of crashing.
}

// Runs `f` when the returned object goes out of scope, on every exit path
// including an exception -- used below so a name reserved in `loadingNames`
// is always released, however LoadWallet() returns. A lambda defined inside
// a CWalletManager member function has that function's access, including to
// private members via a captured `this`; a hand-written local class defined
// the same way would NOT (local classes aren't implicitly granted their
// enclosing member function's access), which is why this is a generic
// callable wrapper instead of a one-off struct at each call site.
template <typename F>
class ScopeExit
{
public:
    explicit ScopeExit(F f) : func(std::move(f)) {}
    ~ScopeExit() { func(); }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
private:
    F func;
};
template <typename F>
ScopeExit<F> MakeScopeExit(F f) { return ScopeExit<F>(std::move(f)); }
}

CWalletManager& CWalletManager::Get()
{
    static CWalletManager instance;
    return instance;
}

void CWalletManager::RegisterDefaultWallet(const std::string& name, CWallet* wallet)
{
    LOCK(cs_wallets);
    defaultWalletName = name;
    mapWallets.try_emplace(name, wallet, true, nextGeneration.fetch_add(1, std::memory_order_relaxed));
}

bool CWalletManager::IsValidWalletName(const std::string& name, std::string& strError)
{
    if (name.empty()) {
        strError = "Wallet name cannot be empty";
        return false;
    }
    if (name.size() > 128) {
        strError = "Wallet name is too long";
        return false;
    }
    // "." and ".." are rejected outright rather than relying on the charset
    // alone: boost::filesystem resolves them as directory components (GetDataDir()
    // / ".." is the datadir's parent), not literal filenames, even though neither
    // one is itself a path separator.
    if (name == "." || name == "..") {
        strError = "Wallet name cannot be '.' or '..'";
        return false;
    }
    for (char c : name) {
        if (SAFE_WALLET_NAME_CHARS.find(c) == std::string::npos) {
            strError = strprintf("Wallet name contains an invalid character: '%c'. Only letters, digits, '.', '_' and '-' are allowed.", c);
            return false;
        }
    }
    return true;
}

bool CWalletManager::LoadWallet(const std::string& name, std::string& strError)
{
    if (!IsValidWalletName(name, strError))
        return false;

    // Everything below, including CWallet::Verify() below, uses throwing
    // boost::filesystem calls (its own exists() at wallet.cpp, not just the
    // one here) -- without this, a symlink loop or a permission error
    // resolving the path would propagate a filesystem_error out of this
    // function, out of the multi-`-wallet=` startup loop in init.cpp, and
    // abort the whole node instead of just failing this one secondary
    // wallet's load.
    try {
        boost::filesystem::path candidatePath = GetDataDir() / name;
        if (!boost::filesystem::exists(candidatePath)) {
            strError = strprintf("Wallet file does not exist: %s", name);
            return false;
        }

        // Fast section, cs_wallets held only for this: reject an outright
        // duplicate/alias, then reserve `name` in loadingNames and release
        // the lock before the slow work below (CWallet::Verify(), then
        // constructing and loading the CWallet, which for a large wallet can
        // run for a long time). Holding cs_wallets for that whole duration
        // would serialize every other cs_wallets-holding caller behind it --
        // RPCWalletRequestGuard for every /wallet/<name>/ request, listwallets,
        // unloadwallet -- and if enough HTTP worker threads end up blocked on
        // it, the whole RPC port stalls, not just wallet RPCs. The reservation
        // is enough to prevent two concurrent loadwallet calls for the *same*
        // name from both reaching CWallet::Verify() with a stale "never
        // touched" assumption (see the comment on that below); loads of
        // *different* names now run fully in parallel instead.
        {
            LOCK(cs_wallets);
            if (mapWallets.count(name)) {
                strError = strprintf("Wallet %s is already loaded", name);
                return false;
            }
            // Exact-name matching above isn't enough by itself: on a case-insensitive
            // filesystem (or Windows' trailing-dot filename aliasing), a name that
            // isn't byte-identical to an already-loaded one can still resolve to the
            // exact same on-disk file -- e.g. loading "Wallet.dat" a second time
            // alongside the default wallet's "wallet.dat" would silently duplicate
            // the live default wallet in memory as a "secondary". Compare the actual
            // resolved files, not just the registry keys. This has to run *before*
            // CWallet::Verify() below, not after: Verify() can salvage/rewrite the
            // file it's given, and running it on a name that turns out to alias an
            // already-loaded (possibly the live default) wallet would mean
            // rewriting that wallet's file out from under it before ever getting
            // to reject the request.
            for (const auto& entry : mapWallets) {
                boost::system::error_code ec;
                if (boost::filesystem::equivalent(candidatePath, GetDataDir() / entry.first, ec) && !ec) {
                    strError = strprintf("Wallet file is already loaded as \"%s\"", entry.first);
                    return false;
                }
            }
            if (loadingNames.count(name)) {
                strError = strprintf("Wallet %s is already being loaded", name);
                return false;
            }
            loadingNames.insert(name);
        }

        // `name` is reserved from here on: no other LoadWallet() call can
        // pass the loadingNames check above for it until this releases the
        // reservation, on every exit path below including an exception.
        auto reservationGuard = MakeScopeExit([this, &name]() {
            LOCK(cs_wallets);
            loadingNames.erase(name);
        });

        // Pre-open integrity check + auto-recover-on-corruption, same as the
        // default wallet gets at startup (init.cpp) -- without this, opening a
        // corrupt file straight into the shared BerkeleyDB environment risks
        // taking that whole environment (and the default wallet with it) into
        // DB_RUNRECOVERY instead of just failing this one load cleanly.
        //
        // CDBEnv::Verify() (wallet/db.cpp) asserts this exact filename has never
        // been registered in the shared environment's mapFileUseCount at all --
        // not just "not currently open" -- because it's designed to run once,
        // pre-open, before any CDB for that file has ever existed in this
        // process. That won't hold for every legitimate load here (e.g. this
        // process already touched the same file some other way), so skip the
        // verify/salvage step rather than crash in that case: LoadWallet()'s own
        // DBErrors return code below still catches genuine corruption either way.
        // The loadingNames reservation above is what makes this check race-free
        // now that cs_wallets itself isn't held here: no other loadwallet call
        // for this exact name can be in progress concurrently to falsify it
        // between the check and CWallet::Verify() actually running.
        bool fAlreadyTouched;
        {
            LOCK(bitdb->cs_db);
            fAlreadyTouched = bitdb->mapFileUseCount.count(name) != 0;
        }
        if (!fAlreadyTouched) {
            std::string warningString, errorString;
            if (!CWallet::Verify(name, warningString, errorString) || !errorString.empty()) {
                strError = !errorString.empty() ? errorString : "Wallet verification failed";
                return false;
            }
            // Surfaced only to the log, not back to the RPC caller: a salvage
            // having happened at all is the kind of thing that belongs in
            // debug.log regardless of who or what triggered this load, and
            // plumbing it through LoadWallet's return value would mean
            // touching every existing caller/test for a phase-1 corner case.
            if (!warningString.empty())
                LogPrintf("loadwallet \"%s\": %s\n", name, warningString);
        }

        CWallet* wallet = nullptr;
        try {
            wallet = new CWallet(name);
            bool fFirstRun = false;
            DBErrors loadResult = wallet->LoadWallet(fFirstRun);
            if (loadResult != DB_LOAD_OK) {
                delete wallet;
                CloseWalletDbFile(name); // Release whatever handle the failed open left behind.
                strError = strprintf("Failed to load wallet %s (error code %d)", name, (int)loadResult);
                return false;
            }
        } catch (const std::exception& e) {
            // CWallet::LoadWallet's CWalletDB/CDB construction throws std::runtime_error
            // on an unopenable file (e.g. an alphanumeric name that resolves to some
            // other, non-wallet file in the datadir) rather than returning a DBErrors
            // code -- without this catch, that both leaks `wallet` and, at startup,
            // propagates out of AppInit2 and aborts the whole node instead of just
            // skipping this one secondary wallet as the caller intends.
            delete wallet;
            CloseWalletDbFile(name); // Same as above: don't leave a handle/mapFileUseCount entry behind.
            strError = strprintf("Failed to load wallet %s: %s", name, e.what());
            return false;
        }

        // Briefly re-take cs_wallets just to commit. Two concurrent loads of
        // two *different* names that happen to be aliases of each other could
        // in principle both pass their own equivalence check above (each ran
        // before the other's entry existed) and both get here -- vanishingly
        // unlikely for phase 1's admin-only, low-frequency use of this RPC,
        // and the existing "already loaded"/"already loaded as" checks still
        // catch it on the next load attempt of either name either way.
        {
            LOCK(cs_wallets);
            mapWallets.try_emplace(name, wallet, false, nextGeneration.fetch_add(1, std::memory_order_relaxed));
        }
        return true;
    } catch (const std::exception& e) {
        strError = strprintf("Failed to load wallet %s: %s", name, e.what());
        return false;
    }
}

bool CWalletManager::UnloadWallet(const std::string& name, std::string& strError)
{
    LOCK(cs_wallets);

    if (defaultWalletName == name) {
        strError = "The default wallet cannot be unloaded";
        return false;
    }

    auto it = mapWallets.find(name);
    if (it == mapWallets.end()) {
        strError = strprintf("Wallet not found: %s", name);
        return false;
    }

    if (it->second.refcount.load() != 0) {
        strError = strprintf("Wallet %s is in use and cannot be unloaded", name);
        return false;
    }

    // Still holding cs_wallets: nothing can observe refcount==0 and start a
    // new request against this name between the check above and the erase.
    CWallet* wallet = it->second.wallet;
    delete wallet;
    CloseWalletDbFile(name);
    mapWallets.erase(it);
    return true;
}

std::vector<std::string> CWalletManager::ListWalletNames() const
{
    LOCK(cs_wallets);
    std::vector<std::string> names;
    names.reserve(mapWallets.size());
    for (const auto& entry : mapWallets)
        names.push_back(entry.first);
    return names;
}

CWallet* CWalletManager::GetWallet(const std::string& name) const
{
    LOCK(cs_wallets);
    auto it = mapWallets.find(name);
    return (it == mapWallets.end()) ? nullptr : it->second.wallet;
}

std::string CWalletManager::GetDefaultWalletName() const
{
    LOCK(cs_wallets);
    return defaultWalletName;
}

bool CWalletManager::IsDefaultWallet(const std::string& name) const
{
    LOCK(cs_wallets);
    return !defaultWalletName.empty() && defaultWalletName == name;
}

bool CWalletManager::AddRef(const std::string& name)
{
    LOCK(cs_wallets);
    auto it = mapWallets.find(name);
    if (it == mapWallets.end())
        return false;
    it->second.refcount.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void CWalletManager::ReleaseRef(const std::string& name)
{
    LOCK(cs_wallets);
    auto it = mapWallets.find(name);
    if (it == mapWallets.end())
        return; // Already unloaded; guard destructors must never throw.
    it->second.refcount.fetch_sub(1, std::memory_order_relaxed);
}

CWalletManager::ResolvedWallet CWalletManager::ResolveAndHoldForRequest(const std::string& name)
{
    LOCK(cs_wallets);
    auto it = mapWallets.find(name);
    if (it == mapWallets.end())
        return { ResolveOutcome::NotFound, 0 };
    if (it->second.isDefault)
        return { ResolveOutcome::IsDefault, it->second.generation };
    it->second.refcount.fetch_add(1, std::memory_order_relaxed);
    return { ResolveOutcome::HeldSecondary, it->second.generation };
}

void CWalletManager::ReleaseRefIfCurrent(const std::string& name, uint64_t generation)
{
    LOCK(cs_wallets);
    auto it = mapWallets.find(name);
    if (it == mapWallets.end())
        return; // Already unloaded; guard destructors must never throw.
    if (it->second.generation != generation)
        return; // A different wallet now lives under this name -- not ours to release.
    it->second.refcount.fetch_sub(1, std::memory_order_relaxed);
}

void CWalletManager::FlushAndUnloadAllSecondaryWallets()
{
    LOCK(cs_wallets);
    for (auto it = mapWallets.begin(); it != mapWallets.end(); ) {
        if (it->second.isDefault) {
            ++it;
            continue;
        }
        const std::string name = it->first;
        delete it->second.wallet;
        CloseWalletDbFile(name);
        it = mapWallets.erase(it);
    }
}

void CWalletManager::Reset()
{
    LOCK(cs_wallets);
    // The default wallet's CWallet* is deleted by Shutdown()'s existing
    // `delete pwalletMain`, so only drop the registry's bookkeeping here.
    mapWallets.clear();
    defaultWalletName.clear();
    // Not expected to be non-empty here (Reset() isn't meant to run
    // concurrently with an in-flight LoadWallet()), but cheap to clear
    // defensively rather than leave a stale reservation across, e.g., gtest
    // fixture boundaries.
    loadingNames.clear();
}

std::string CWalletManager::GetRequestedWalletName()
{
    return g_requestedWalletName;
}

RPCWalletRequestGuard::RPCWalletRequestGuard(const std::string& name)
    : strName(name), strPrevWalletName(g_requestedWalletName), fRefHeld(false), fResolved(true), generation(0)
{
    g_requestedWalletName = name;
    if (!name.empty()) {
        // Resolution and ref-holding happen as one atomic operation (under
        // cs_wallets) rather than a separate existence check followed by a
        // later AddRef -- a lookup-then-add split would leave a window for
        // unloadwallet to remove the entry in between.
        CWalletManager::ResolvedWallet resolved = CWalletManager::Get().ResolveAndHoldForRequest(name);
        generation = resolved.generation;
        switch (resolved.outcome) {
        case CWalletManager::ResolveOutcome::HeldSecondary:
            fRefHeld = true;
            break;
        case CWalletManager::ResolveOutcome::NotFound:
            fResolved = false;
            break;
        case CWalletManager::ResolveOutcome::IsDefault:
            break; // Nothing to hold; the default wallet is never unloadable.
        }
    }
}

RPCWalletRequestGuard::~RPCWalletRequestGuard()
{
    if (fRefHeld)
        CWalletManager::Get().ReleaseRefIfCurrent(strName, generation);
    g_requestedWalletName = strPrevWalletName;
}
