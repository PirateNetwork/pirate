// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/walletmanager.h"

#include "init.h"
#include "rpc/server.h"
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
    // Erase any existing entry for `name` first rather than try_emplace's
    // no-op-if-present behavior (and Entry isn't assignable in place, since
    // it holds a std::atomic<int> refcount, so insert_or_assign isn't an
    // option either): zcbenchmarks.cpp's benchmark_loadwallet() deletes and
    // replaces pwalletMain, then calls this again so the registry's default
    // entry repoints at the new object instead of being left pointing at the
    // just-deleted one (which CheckpointAllWallets(), reachable from
    // StartShutdown(), would otherwise dereference on the next shutdown).
    // No outstanding ref/generation state is lost by erasing: the default
    // wallet is never refcount-pinned by ResolveAndHoldForRequest() (it
    // returns ResolveOutcome::IsDefault without taking a ref, since the
    // default can't be unloaded anyway).
    mapWallets.erase(name);
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

// Historical note: earlier phases left this function's secondary-wallet load
// path without any ChainTip() registration at all -- a permanently frozen
// snapshot as of load time, with getbalance/z_getbalance/listunspent/
// listtransactions/getwalletinfo on it reporting stale data indefinitely.
// Phase 4 added RegisterValidationInterface() plus the synchronous catch-up
// below (mirroring init.cpp's own startup sequence), and Phase 5 finished
// retargeting the consolidation/sweep/delete-transaction dispatch this
// triggers (RunSaplingConsolidation/RunIronwoodConsolidation/RunSaplingSweep/
// DeleteWalletTransactions, and the settings that control them) to operate on
// `wallet` specifically rather than the process-global pwalletMain -- so a
// secondary wallet's own chain-tip notifications now drive its own funds and
// its own configuration, not the default wallet's.
//
// Two gaps this left, both since closed: fBuilingWitnessCache (wallet.cpp)
// used to be a single process-global flag checked by CRPCTable::execute()
// (rpc/server.cpp) for every RPC on the node, so a secondary wallet's own
// witness-cache rebuild stalled the whole RPC interface for its duration --
// it's now a per-CWallet member, checked against whichever wallet a request
// actually resolves to. And a secondary wallet used to receive no
// notification for a *transaction* it didn't cause itself beyond what
// ChainTip()'s own block-level scan surfaces, which risked a retried send
// double-spending by reselecting an already-broadcast note -- confirmed (via
// a real gtest, not just re-derived) that validation-interface registration
// already covers this correctly, once a real, unrelated CTxMemPool::clear()
// use-after-free that the investigation surfaced was fixed (txmempool.cpp).
bool CWalletManager::LoadWallet(const std::string& name, std::string& strError,
                                 bool fRescan, int nRescanHeight, bool fSalvage, bool fZapWalletTxes,
                                 bool fAllowCreate, const SecureString& strPassphrase,
                                 bool* pfPassphraseRequired)
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
        if (!fAllowCreate && !boost::filesystem::exists(candidatePath)) {
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
        // bitdb's mapFileUseCount alone only answers "currently open right
        // now", not "ever touched by this process" --
        // CloseWalletDbFile()/CDBEnv::Flush()/CDB::Rewrite() all erase() a
        // file's entry once its use count reaches 0 (deliberately, so a
        // *different*, never-before-seen file that happens to reuse the name
        // can still pass CDBEnv::Verify()'s assert later). A plain
        // unload-then-reload of the same name therefore looks like a
        // first-ever open, and re-runs Verify() on a file this process has
        // definitely touched. That is harmless after a plain load+unload,
        // but reproducibly reports spurious corruption if the file was put
        // through CWallet::EncryptWallet()'s CDB::Rewrite() (physical
        // delete+rename via detached Db handles outside bitdb's own
        // bookkeeping) in between -- and CWallet::Verify() responds to that
        // by handing the file to CWalletDB::Recover(), which renames the
        // original aside as wallet.{timestamp}.bak and rewrites it from
        // whatever it could salvage -- destructive, on a healthy file.
        // CDB::Rewrite() is not only EncryptWallet()'s: CWallet::LoadWallet()
        // and ZapWalletTx() both call it on a DB_NEED_REWRITE result too, so
        // this is not tied to encryption alone. everLoadedNames closes it:
        // it records every name this process has opened and is never erased
        // short of Reset(), so a reload takes the skip-Verify branch the
        // same way an already-open file does.
        bool fEverLoadedInThisProcess;
        {
            LOCK(cs_wallets);
            fEverLoadedInThisProcess = everLoadedNames.count(name) != 0;
        }
        bool fAlreadyTouched;
        {
            // Sequential, not nested: the established order is
            // cs_wallets -> bitdb->cs_db (UnloadWallet holds cs_wallets
            // across CloseWalletDbFile), so cs_wallets must never be taken
            // while cs_db is held.
            LOCK(bitdb->cs_db);
            fAlreadyTouched = bitdb->mapFileUseCount.count(name) != 0;
        }
        if (fSalvage && (fAlreadyTouched || fEverLoadedInThisProcess)) {
            // Pre-existing behavior, now logged rather than silent: an
            // explicit salvage request only ever runs through
            // CWallet::Verify(), so it is a no-op on a file this process has
            // already opened. Salvaging such a wallet needs a restart.
            LogPrintf("loadwallet \"%s\": salvage requested but skipped -- this process has already opened this file; restart the node to salvage it\n", name);
        }
        if (!fAlreadyTouched && !fEverLoadedInThisProcess) {
            std::string warningString, errorString;
            if (!CWallet::Verify(name, warningString, errorString, fSalvage) || !errorString.empty()) {
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

        // Recorded here, before the open rather than after a successful one:
        // "this process has touched the file" becomes true the moment a CDB
        // is constructed against it, including on the failure paths below.
        {
            LOCK(cs_wallets);
            everLoadedNames.insert(name);
        }

        CWallet* wallet = nullptr;
        bool fFirstRun = false;
        try {
            wallet = new CWallet(name);
            // An encrypted wallet cannot simply be opened -- init.cpp runs a
            // whole handshake before the default wallet's own LoadWallet()
            // call: InitalizeCryptedLoad(), SetDBCrypted(),
            // LoadCryptedSeedFromDB(), OpenWallet(passphrase) and, only once
            // the seed is decrypted, seedEncyptionFP -- the salt every
            // encrypted record's key and integrity tag is derived from.
            // Skipping straight to LoadWallet() below on a crypted file
            // leaves every encrypted record undecryptable and returns
            // DB_CORRUPT on a perfectly intact file.
            //
            // Unlike init.cpp's own startup version of this handshake (which
            // blocks the whole process in a busy-wait for an out-of-band
            // unlock -- a GUI dialog, or the special openwallet RPC -- since
            // no passphrase is available yet at that point), this runs the
            // whole thing synchronously against whatever strPassphrase the
            // caller already supplied: it either succeeds immediately or
            // fails immediately, and never blocks. That shape only works
            // because every caller of this function already has the
            // passphrase in hand before calling (an RPC parameter, a CLI
            // flag, or a GUI dialog shown first) -- there is no equivalent
            // here of "wait for someone else to supply it later".
            DBErrors nInitalizeCryptedLoad = wallet->InitalizeCryptedLoad();
            if (nInitalizeCryptedLoad == DB_LOAD_CRYPTED) {
                wallet->SetDBCrypted();
                DBErrors nLoadCryptedSeed = wallet->LoadCryptedSeedFromDB();
                if (nLoadCryptedSeed != DB_LOAD_OK) {
                    delete wallet;
                    CloseWalletDbFile(name);
                    strError = strprintf("Wallet %s: encrypted seed record is corrupted (error code %d)", name, (int)nLoadCryptedSeed);
                    return false;
                }
                if (strPassphrase.empty()) {
                    delete wallet;
                    CloseWalletDbFile(name);
                    strError = strprintf("Wallet %s is encrypted and requires its passphrase to load -- "
                                         "retry with the passphrase argument. The file itself is intact -- "
                                         "do not salvage it.", name);
                    if (pfPassphraseRequired)
                        *pfPassphraseRequired = true;
                    return false;
                }
                if (!wallet->OpenWallet(strPassphrase)) {
                    delete wallet;
                    CloseWalletDbFile(name);
                    strError = strprintf("Wallet %s: the passphrase given was incorrect", name);
                    if (pfPassphraseRequired)
                        *pfPassphraseRequired = true;
                    return false;
                }
                // A crypted wallet must have an HD seed, same check init.cpp
                // makes for the default wallet immediately after unlocking.
                HDSeed seed;
                if (!wallet->GetHDSeed(seed)) {
                    delete wallet;
                    CloseWalletDbFile(name);
                    strError = strprintf("Wallet %s: HD seed not found after unlocking", name);
                    return false;
                }
                // DO NOT SAVE THIS TO THE WALLET -- matches init.cpp's own
                // handling of the exact same value for the default wallet.
                // Used to salt hashes of known values such as transaction
                // ids and public addresses; the LoadWallet() call just below
                // depends on it already being set to read those back
                // correctly. Not taken under any lock, matching init.cpp's
                // own assignment of the same member for the default wallet:
                // this object is not in mapWallets yet and not registered
                // with the validation interface, so nothing else in the
                // process can reach it.
                wallet->seedEncyptionFP = seed.EncryptionFingerprint();

                // DELIBERATE GAP: init.cpp additionally runs
                // NeedsKDFUpgrade()/ChangeWalletPassphrase() for the default
                // wallet here, transparently re-wrapping a legacy-SHA512
                // master key with the memory-hard KDF. Not done for secondary
                // wallets: ChangeWalletPassphrase() begins by Lock()ing and
                // only re-unlocks on the success path, so a failed upgrade
                // would silently leave this wallet locked -- contradicting
                // loadwallet's documented "remains unlocked afterward"
                // contract, on a wallet the caller has no way to notice went
                // locked. Wants its own explicit per-wallet step rather than
                // a side effect of loading.
            }
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

        // Migrate plaintext records that have an encrypted-on-disk form, same
        // as init.cpp does for the default wallet at startup. This used to be
        // unreachable for a secondary wallet -- there was no way to supply a
        // passphrase here, so an encrypted wallet was always still locked at
        // this point and every migration below (all of which need the master
        // key) short-circuited. With the per-wallet unlock above it is now
        // genuinely live, so it has to carry the *same* set of records
        // init.cpp migrates, not a subset: the HD chain was missing here,
        // which would have left an encrypted secondary wallet's `hdchain`
        // record -- seed fingerprint and account/key counters -- readable in
        // plaintext in a file whose whole point is that it isn't.
        if (wallet->IsCrypted() && !wallet->IsLocked()) {
            if (!wallet->WriteHDChainToDisk(wallet->GetHDChain()))
                LogPrintf("Warning: could not migrate HD chain to encrypted storage for wallet %s.\n", name);
            if (!wallet->MigrateDestDataToEncrypted())
                LogPrintf("Warning: could not migrate destination data to encrypted storage for wallet %s.\n", name);
            if (!wallet->MigrateSettingsToEncrypted())
                LogPrintf("Warning: could not migrate consolidation/sweep/fee/pruning settings to encrypted storage for wallet %s.\n", name);
        }

        if (fZapWalletTxes) {
            // Mirrors init.cpp's own -zapwallettxes handling for the default
            // wallet: wipe transaction history and reset the shielded witness
            // trees, then always force a full rescan below (same as
            // -zapwallettxes implying -rescan) since there's nothing left to
            // incrementally catch up from.
            std::vector<CWalletTx> vWtx;
            DBErrors nZapWalletRet = wallet->ZapWalletTx(vWtx);
            if (nZapWalletRet != DB_LOAD_OK) {
                delete wallet;
                CloseWalletDbFile(name);
                strError = strprintf("Failed to zap transactions for wallet %s (error code %d)", name, (int)nZapWalletRet);
                return false;
            }
            // ZapWalletTx() only erases the on-disk tx/arc-tx/nullifier
            // records (CWalletDB::ZapWalletTx, walletdb.cpp) -- it does not
            // touch this object's own mapWallet/mapArcTxs/nullifier maps,
            // already populated by the LoadWallet() call above. init.cpp's
            // own -zapwallettxes handling avoids this entirely by always
            // zapping a *fresh*, not-yet-loaded CWallet (construct, zap,
            // then load) -- discard this object and reload a new one from
            // the now-zapped file to match, so nothing below still
            // references data that's gone on disk, or witness positions the
            // reset just below invalidates.
            delete wallet;
            CloseWalletDbFile(name);
            wallet = new CWallet(name);
            DBErrors reloadResult = wallet->LoadWallet(fFirstRun);
            if (reloadResult != DB_LOAD_OK) {
                delete wallet;
                CloseWalletDbFile(name);
                strError = strprintf("Failed to reload wallet %s after zapping transactions (error code %d)", name, (int)reloadResult);
                return false;
            }
            wallet->SaplingWalletReset();
            wallet->IronwoodWalletReset();
            fRescan = true;
        }

        // Register for chain-tip notifications and catch this wallet up to
        // the current tip before it's ever visible in the registry (i.e.
        // before the mapWallets commit below) -- mirrors, as closely as
        // possible, exactly what init.cpp does for the default wallet at
        // startup: RegisterValidationInterface(), then (still under the same
        // cs_main hold, so no ChainTip() call on the block-connection thread
        // can interleave mid-catch-up) an incremental ScanForWalletTransactions
        // from this wallet's own persisted "bestblock" locator, then
        // Validate*WalletTrackedPositions()-or-rebuild for both pools. This
        // is what makes a newly-loaded secondary wallet's staleness a
        // bounded, synchronous cost paid once by this RPC call, rather than
        // a surprise stall discovered later on some unrelated future block
        // (see the "Witness-cache DoS" scoping decision this phase was
        // built against). It is exactly as capable of stalling the node's
        // RPC surface for the duration as the default wallet's own startup
        // sequence already is -- not a new risk, the same one reachable
        // from a second entry point.
        //
        // RegisterValidationInterface() itself must run *inside* the
        // LOCK(cs_main) below, not before it: registering first and then
        // taking cs_main leaves a real window, on a live node, where a
        // block connects (on a thread that already holds cs_main to do so)
        // between the two -- ChainTip() would fire on this wallet before
        // any of its catch-up has run, and its own SetBestChain() call
        // (wallet.cpp) can persist a "bestblock" locator at the *new* tip,
        // which would then make the catch-up below believe it's already
        // caught up and rescan only the last block -- silently skipping
        // this wallet's entire missed range with no error anywhere.
        //
        // Committing this wallet's ref-counted registry entry is handled by
        // a scope-exit rather than the try/catch alone: mapWallets.try_emplace()
        // below runs after this whole block, and if it ever throws (e.g.
        // std::bad_alloc), falling through to the outer catch (this
        // function's own, further down) without unregistering would leave a
        // wallet permanently receiving chain-tip notifications forever with
        // no registry entry to ever unload it through.
        bool fCommitted = false;
        auto registrationGuard = MakeScopeExit([&fCommitted, &wallet, &name]() {
            if (fCommitted)
                return;
            // cs_main here, not just at the call site below: this guard can
            // run *after* the LOCK(cs_main) scope below has already closed
            // (e.g. the catch-up's own catch block returns false, which
            // unwinds that scope before this guard's body runs) -- without
            // re-taking it, UnregisterValidationInterface()+delete would run
            // unprotected against a block-connection thread that grabs the
            // now-free cs_main and dispatches ChainTip() to this
            // already-registered wallet concurrently with the delete. This
            // is exactly the use-after-free UnloadWallet()'s own cs_main
            // hold exists to prevent, just reachable from this function's
            // error path instead. Recursive-safe with the LOCK(cs_main)
            // below on the success-of-registration-but-later-failure path,
            // and matches how init.cpp holds cs_main across its own
            // -wallet= loading loop.
            LOCK(cs_main);
            UnregisterValidationInterface(wallet);
            delete wallet;
            CloseWalletDbFile(name);
        });
        {
            LOCK(cs_main);
            RegisterValidationInterface(wallet);
            try {
                if (fFirstRun) {
                    // Brand-new wallet, nothing to catch up: pin its
                    // checkpoint at the current tip, same as init.cpp does
                    // for a freshly created default wallet.
                    if (chainActive.Tip()) {
                        LOCK(wallet->cs_wallet); // SetBestChain() requires this (AssertLockHeld)
                        wallet->SetBestChain(chainActive.GetLocator(), chainActive.Tip()->nHeight);
                    }
                } else if (chainActive.Tip()) {
                    // ReadWalletBirthday() -- unlike ReadBestBlock() below,
                    // CWallet::LoadWallet()'s own CWalletDB pass never reads
                    // this key (see init.cpp, the only other caller), so
                    // without this nBirthday stays at CWallet::SetNull()'s
                    // default and the birthday-skip loop inside
                    // ScanForWalletTransactions() (the only two callers that
                    // pass fIgnoreBirthday=false are this and init.cpp) runs
                    // with a value this wallet's file never actually had.
                    {
                        CWalletDB walletdb(name);
                        walletdb.ReadWalletBirthday(wallet->nBirthday);
                    }
                    CBlockIndex* pindexRescan = nullptr;
                    if (fRescan) {
                        // Caller explicitly asked for a full rescan (or this
                        // load implies one via fZapWalletTxes above) --
                        // mirrors -rescan/-rescanheight: rescan from the given
                        // height, or genesis if none given, ignoring whatever
                        // checkpoint this wallet's file has persisted. Also
                        // mirrors init.cpp's own forced-rescan branch in
                        // resetting nBirthday to 0 -- ReadWalletBirthday()
                        // just above loaded whatever this wallet's file had
                        // persisted, but a forced rescan means genuinely
                        // re-scanning every block, not silently skipping
                        // everything before that birthday.
                        wallet->nBirthday = 0;
                        if (nRescanHeight > 0 && nRescanHeight > chainActive.Height()) {
                            // Same clamp-to-tip-with-log as init.cpp's default-
                            // wallet path: an out-of-range height shouldn't
                            // silently fall back to a full genesis rescan
                            // instead (the only other branch below), which
                            // would otherwise make this flag behave
                            // oppositely for a secondary wallet vs. the
                            // default one on the exact same typo.
                            pindexRescan = chainActive.Tip();
                            LogPrintf("Wallet \"%s\": rescan height %d exceeds chain tip, starting from tip at height %d\n",
                                      name, nRescanHeight, pindexRescan->nHeight);
                        } else {
                            pindexRescan = (nRescanHeight > 0) ? chainActive[nRescanHeight] : chainActive.Genesis();
                        }
                    } else {
                        CWalletDB walletdb(name);
                        CBlockLocator locator;
                        pindexRescan = walletdb.ReadBestBlock(locator)
                            ? FindForkInGlobalIndex(chainActive, locator)
                            : chainActive.Genesis();
                    }
                    if (pindexRescan && chainActive.Tip() != pindexRescan) {
                        wallet->ScanForWalletTransactions(pindexRescan, true, false, false, false);
                    } else if (chainActive.Height() > 0) {
                        // Same as init.cpp: rescan at minimum the last block
                        // even when the persisted checkpoint already matches
                        // the tip, rather than skipping the scan entirely.
                        wallet->ScanForWalletTransactions(chainActive[chainActive.Tip()->nHeight - 1], true, false, false, false);
                    }
                }
                if (chainActive.Tip() && chainActive.Height() > 0) {
                    LOCK(wallet->cs_wallet);
                    if (!wallet->ValidateSaplingWalletTrackedPositions(chainActive.Tip())) {
                        wallet->SaplingWalletReset();
                        wallet->IncrementSaplingWallet(chainActive.Tip());
                    }
                    wallet->saplingWalletPositionsValidated = true;
                    if (!wallet->ValidateIronwoodWalletTrackedPositions(chainActive.Tip())) {
                        wallet->IronwoodWalletReset();
                        wallet->IncrementIronwoodWallet(chainActive.Tip());
                    }
                    wallet->ironwoodWalletPositionsValidated = true;
                }
                // Same as init.cpp: whether a loaded wallet's own
                // transactions get relayed/rebroadcast at all is gated on
                // this flag, which CWallet::SetNull() defaults to false.
                // Without it, sendtoaddress against this wallet would
                // build, sign, and record a transaction as if it succeeded
                // (CommitTransaction() returns true regardless) while never
                // actually broadcasting it to the network.
                wallet->SetBroadcastTransactions(GetBoolArg("-walletbroadcast", true));
            } catch (const std::exception& e) {
                strError = strprintf("Failed to catch up wallet %s to the current chain tip: %s", name, e.what());
                return false; // registrationGuard unregisters+deletes+closes.
            }
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
            // .second, not just calling try_emplace() and assuming success:
            // if this ever returned false (name already present -- shouldn't
            // happen given the loadingNames reservation and the earlier
            // mapWallets.count(name) check, but both of those run in their
            // own separate cs_wallets critical sections, not one continuous
            // hold spanning to here), unconditionally setting fCommitted
            // would disarm registrationGuard and leak `wallet` still fully
            // registered on all 9 validation-interface signals -- the exact
            // zombie this guard exists to prevent, arrived at from the
            // commit side instead of an exception.
            fCommitted = mapWallets.try_emplace(name, wallet, false, nextGeneration.fetch_add(1, std::memory_order_relaxed)).second;
        }
        if (!fCommitted) {
            strError = strprintf("Wallet %s is already loaded", name);
            return false; // registrationGuard unregisters+deletes+closes.
        }
        return true;
    } catch (const std::exception& e) {
        strError = strprintf("Failed to load wallet %s: %s", name, e.what());
        return false;
    }
}

bool CWalletManager::CreateWallet(const std::string& name, std::string& strError, std::string& seedPhraseOut)
{
    if (!IsValidWalletName(name, strError))
        return false;

    // The one check LoadWallet() doesn't make: "create" must refuse a name
    // that already resolves to a file, rather than silently adopting/
    // reopening whatever's already there -- that's what loadwallet is for.
    // Narrow, accepted TOCTOU: something else creating a file under this
    // exact name in between this check and LoadWallet()'s own is not a
    // realistic concern for a single-operator node datadir.
    try {
        if (boost::filesystem::exists(GetDataDir() / name)) {
            strError = strprintf("Wallet file already exists: %s", name);
            return false;
        }
    } catch (const std::exception& e) {
        strError = strprintf("Failed to check for existing wallet file %s: %s", name, e.what());
        return false;
    }

    // Delegates entirely to LoadWallet(): CWallet::LoadWallet() (the
    // per-instance DB open, not this function) already auto-creates a file
    // that doesn't exist and reports fFirstRun=true, which LoadWallet()'s
    // own registration/catch-up tail already special-cases (pins the
    // checkpoint at the current tip, nothing to rescan). Reusing it here
    // avoids duplicating that already-audited locking/exception-safety
    // sequence for what is otherwise an identical code path.
    if (!LoadWallet(name, strError, /*fRescan=*/false, /*nRescanHeight=*/0,
                     /*fSalvage=*/false, /*fZapWalletTxes=*/false, /*fAllowCreate=*/true))
        return false;

    CWallet* wallet = GetWallet(name);
    if (!wallet) {
        // Shouldn't happen: LoadWallet() just returned true, which only
        // happens after committing the entry to mapWallets.
        strError = strprintf("Internal error: wallet %s created but not found in registry", name);
        return false;
    }

    // Fresh wallet: seed it, mirroring init.cpp's own fresh-default-wallet
    // setup (GenerateNewSeed() then a first Sapling address) -- minus the
    // interactive GUI seed-phrase-confirmation flow that only makes sense
    // during first-run node startup, not for adding a wallet to an
    // already-running one. The generated phrase is handed back so the
    // caller can prompt the user to back it up immediately; there is no way
    // to retrieve it again later.
    try {
        wallet->GenerateNewSeed();
        wallet->GetSeedPhrase(seedPhraseOut);
        LOCK(wallet->cs_wallet);
        auto zAddress = wallet->GenerateNewSaplingZKey();
        wallet->SetZAddressBook(zAddress, "Sapling", "");
    } catch (const std::exception& e) {
        // The wallet is already fully registered and usable at this point
        // (LoadWallet() above succeeded) -- a seeding failure here doesn't
        // warrant unwinding that; surface it and leave the (seedless, so
        // unusable for shielded sends until the caller retries seeding or
        // unloads/reloads) wallet loaded rather than silently deleting
        // something already visible to any concurrent listwallets caller.
        strError = strprintf("Wallet %s created but seeding failed: %s", name, e.what());
        return false;
    }
    return true;
}

bool CWalletManager::UnloadWallet(const std::string& name, std::string& strError)
{
    // Phase 4: cs_main is taken here, *before* cs_wallets, not nested inside
    // it -- this function needs both because ChainTip() is always invoked by
    // main.cpp's block-(dis)connection code while it already holds cs_main
    // (a recursive mutex), across every registered wallet's callback in one
    // synchronous dispatch, and this function must not delete a wallet that
    // dispatch could still be executing against. Without cs_main here at
    // all, a block connecting at the same moment as this unload could have
    // ChainTip() mid-execution on `wallet` on the block-connection thread
    // while this thread deletes it out from under that call -- the refcount
    // check below only ever tracked RPC-request/async-op-held refs, never
    // validation-interface callbacks.
    //
    // The order matters as much as the fact of it: asyncrpcoperation.cpp
    // documents this codebase's established order as
    // cs_main -> cs_wallet -> cs_wallets (z_sendmany et al. take
    // LOCK2(cs_main, pwallet->cs_wallet) and then construct an
    // AsyncRPCOperation, whose constructor takes cs_wallets internally). An
    // earlier version of this fix took cs_main nested *inside* cs_wallets --
    // the inverse order -- which deadlocks the whole node: this thread
    // holding cs_wallets and waiting on cs_main, against a z_sendmany
    // holding cs_main+cs_wallet and waiting on cs_wallets in its own
    // AsyncRPCOperation constructor, or against ChainTip() itself needing
    // cs_wallets from inside RunSaplingConsolidation's construction of its
    // own AsyncRPCOperation while already holding cs_main.
    LOCK2(cs_main, cs_wallets);

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
    // Must run before delete: walletpassphrase (wallet/rpcwallet.cpp) can have
    // armed a timer for this wallet. That timer is bound to the wallet's name
    // and generation rather than this raw pointer, so it won't dereference
    // freed memory if it fires afterwards anyway -- but without this call
    // it's still left ticking, and if this same name is reloaded before the
    // deadline, it would fire and relock a wallet it was never armed for.
    CancelWalletAutoLockTimer(wallet);
    // Already holding cs_main (see above) for exactly this: no in-flight
    // ChainTip() dispatch can be executing against `wallet` right now, and
    // none can start until this function returns and releases it.
    UnregisterValidationInterface(wallet);
    delete wallet;
    CloseWalletDbFile(name);
    mapWallets.erase(it);
    return true;
}

bool CWalletManager::DiscardWalletAfterFailedEncryption(const std::string& name, std::string& strError)
{
    // Same cs_main -> cs_wallets order as UnloadWallet(), for the same
    // reason: no in-flight ChainTip() dispatch can be executing against
    // `wallet` while this holds cs_main, and none can start until this
    // function returns.
    LOCK2(cs_main, cs_wallets);

    if (defaultWalletName == name) {
        strError = "The default wallet cannot be discarded this way";
        return false;
    }

    auto it = mapWallets.find(name);
    if (it == mapWallets.end()) {
        strError = strprintf("Wallet not found: %s", name);
        return false;
    }

    // refcount <= 1, not == 0: the calling request's own
    // RPCWalletRequestGuard holds exactly one ref on this wallet for the
    // whole encryptwallet call, so requiring 0 would make this unreachable.
    // Anything above that means a *second* holder -- a concurrent RPC
    // request (whose guard is constructed in the dispatch layer under
    // cs_wallets alone, so this function's cs_main hold does not keep it
    // out), a running or unpolled AsyncRPCOperation on its own thread, or a
    // wallet open in the Qt GUI -- and deleting the object under any of
    // those is a use-after-free. Refuse and let the caller fall back to its
    // restart path instead. See the full reasoning on the declaration
    // (walletmanager.h). The load is stable rather than racy: refs are only
    // ever taken and dropped under cs_wallets, which this holds.
    if (it->second.refcount.load() > 1) {
        strError = strprintf("Wallet %s is in use by another request or operation and cannot be discarded", name);
        return false;
    }

    CWallet* wallet = it->second.wallet;
    CancelWalletAutoLockTimer(wallet);
    UnregisterValidationInterface(wallet);
    delete wallet;
    CloseWalletDbFile(name);
    mapWallets.erase(it);
    return true;
}

void CWalletManager::CheckpointAllWallets(const CBlockLocator& locator, int height)
{
    // Snapshot under cs_wallets, then release it before taking any wallet's
    // cs_wallet -- the established order elsewhere in this class (see
    // UnloadWallet/FlushAndUnloadAllSecondaryWallets, and the deadlock
    // warning in asyncrpcoperation.cpp) is cs_main -> cs_wallet -> cs_wallets,
    // never the reverse. Locking cs_wallet while still holding cs_wallets
    // here would invert that against, e.g., CWallet::ChainTip()'s
    // LOCK2(cs_main, cs_wallet) leading into a consolidation/sweep operation
    // that itself takes cs_wallets via ResolveAndHoldForRequest().
    // Snapshotting the raw pointers is safe without holding cs_wallets for
    // the rest of the function only because the caller (StartShutdown())
    // holds cs_main for the whole call, and both UnloadWallet() and
    // FlushAndUnloadAllSecondaryWallets() take cs_main before cs_wallets --
    // so no entry can be deleted out from under us in between.
    AssertLockHeld(cs_main);
    std::vector<std::pair<std::string, CWallet*>> snapshot;
    {
        LOCK(cs_wallets);
        snapshot.reserve(mapWallets.size());
        for (auto& entry : mapWallets) {
            if (entry.second.wallet != nullptr)
                snapshot.emplace_back(entry.first, entry.second.wallet);
        }
    }
    for (auto& item : snapshot) {
        CWallet* wallet = item.second;
        LOCK(wallet->cs_wallet);
        LogPrintf("Flushing wallet \"%s\" to disk on shutdown.\n", item.first);
        wallet->SetBestChain(locator, height);
    }
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
    // Unlike UnloadWallet(), this does not check refcount == 0 before
    // deleting. That's only safe because of shutdown ordering, not because
    // the race this would otherwise open is impossible: by the time
    // Shutdown() (init.cpp) reaches this call, StopHTTPServer() has already
    // run -- specifically its workQueue->WaitExit() (joins the HTTP RPC
    // worker threads, so no ResolveAndHoldForRequest() can still be in
    // flight and no in-flight request can be holding a ref via
    // RPCWalletRequestGuard) and its threadHTTP.join() (joins the libevent
    // thread that dispatches LockWallet timer callbacks). StopRPC(), which
    // runs just before StopHTTPServer(), joins neither of these -- it only
    // clears deadlineTimers and drains the async z_* operation queue, so it
    // is NOT the call this safety property actually depends on. If a future
    // caller ever invokes this outside that specific shutdown sequence, it
    // would need the same refcount == 0 wait UnloadWallet() does first.
    //
    // One narrower exception even at this point in shutdown: a finished (or
    // never-polled) AsyncRPCOperation can still be sitting in
    // AsyncRPCQueue::operation_map_ holding a ref, since StopRPC()'s
    // closeAndWait() only joins the worker thread -- it never clears that
    // map (only an explicit z_getoperationresult, or the queue object's own
    // eventual destruction, drops those shared_ptrs). This is harmless here:
    // ReleaseRefIfCurrent() (called from ~AsyncRPCOperation() whenever that
    // eventually runs) already no-ops safely against a name this function
    // has erased, and nothing re-reads that operation's CWallet* afterwards
    // (main() already ran; only stored result/error/status values are read
    // from then on) -- the same "pwalletMain deleted out from under a
    // finished operation still sitting in the map" exposure already existed
    // for every wallet RPC operation before this class had its own wallet
    // pointer at all.
    // Phase 4 addendum: this shutdown-time deletion loop now also races a
    // possible in-flight ChainTip() dispatch, for the same reason and with
    // the same fix as UnloadWallet() -- see the comment there, including why
    // cs_main must be taken *before* cs_wallets (this codebase's established
    // order is cs_main -> cs_wallet -> cs_wallets; nesting it the other way
    // around deadlocks against anything that takes cs_wallets while already
    // holding cs_main, which includes ChainTip() itself via
    // RunSaplingConsolidation's AsyncRPCOperation construction). Interrupt()
    // (bitcoind.cpp) deliberately does not join the thread group before
    // Shutdown() runs (its own comment: "was left out intentionally...
    // because we didn't re-test all of" the shutdown ordering), so block
    // connection is not guaranteed to have stopped by the time this
    // function runs.
    LOCK2(cs_main, cs_wallets);
    for (auto it = mapWallets.begin(); it != mapWallets.end(); ) {
        if (it->second.isDefault) {
            ++it;
            continue;
        }
        const std::string name = it->first;
        // See UnloadWallet(): must run before delete for the same reason.
        CancelWalletAutoLockTimer(it->second.wallet);
        UnregisterValidationInterface(it->second.wallet);
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
    // Same reasoning, and load-bearing for gtest specifically: each fixture
    // installs a fresh datadir and a fresh CDBEnv, so a name carried over
    // from a previous fixture would wrongly suppress CWallet::Verify() for a
    // completely different file that happens to reuse it.
    everLoadedNames.clear();
}

std::string CWalletManager::GetRequestedWalletName()
{
    return g_requestedWalletName;
}

CWallet* CWalletManager::GetWalletForRequest()
{
    std::string name = GetRequestedWalletName();
    if (name.empty())
        return pwalletMain;
    CWallet* wallet = CWalletManager::Get().GetWallet(name);
    // Falls back to pwalletMain rather than returning nullptr: by the time a
    // rewired RPC calls this, CRPCTable::execute()'s gate has already
    // confirmed the name is either the default or IsMultiWalletAwareRPC(), and
    // httprpc.cpp's RPCWalletRequestGuard::IsResolved() already confirmed it
    // was loaded when the request started -- reaching here with `wallet ==
    // nullptr` means it was unloaded in the brief window since (the guard
    // holds a ref on a secondary specifically to prevent that, so this is
    // belt-and-braces, not the expected path).
    return wallet ? wallet : pwalletMain;
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
