// Copyright (c) 2016 The Zcash developers
// Copyright (c) 2026 Pirate Chain developers
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

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "asyncrpcoperation.h"

// This file lives in the unconditionally-built server library (see
// Makefile.am), but CWalletManager/CWallet live in libbitcoin_wallet.a,
// which is only built and linked with ENABLE_WALLET. Guard both the
// includes and the two function bodies below that touch them, rather than
// making the whole file (or the constructor's existence) conditional --
// nothing calls the wallet-aware constructor in a --disable-wallet build
// anyway, since every AsyncRPCOperation subclass that uses it lives under
// wallet/ and is itself ENABLE_WALLET-only.
//
// ENABLE_WALLET itself is defined by config/bitcoin-config.h (autoconf
// output), included above -- this file never had a reason to pull that in
// before now, and without it every #ifdef ENABLE_WALLET below silently
// resolves to "not defined" regardless of how the build was actually
// configured, compiling the inert #else stub in its place.
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/walletmanager.h"
#endif

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <string>
#include <ctime>
#include <chrono>

using namespace std;

static boost::uuids::random_generator uuidgen;

static std::map<OperationStatus, std::string> OperationStatusMap = {
    {OperationStatus::READY, "queued"},
    {OperationStatus::EXECUTING, "executing"},
    {OperationStatus::CANCELLED, "cancelled"},
    {OperationStatus::FAILED, "failed"},
    {OperationStatus::SUCCESS, "success"}
};

/**
 * Every operation instance should have a globally unique id
 */
AsyncRPCOperation::AsyncRPCOperation() : error_code_(0), error_message_() {
    // Set a unique reference for each operation
    boost::uuids::uuid uuid = uuidgen();
    id_ = "opid-" + boost::uuids::to_string(uuid);
    creation_time_ = (int64_t)time(NULL);
    set_state(OperationStatus::READY);
}

// Takes the wallet this operation should run against. Resolved and pinned
// here, on the caller's thread (the RPC handler that's about to queue this
// operation for the single AsyncRPCQueue worker thread) -- AsyncRPCQueue's
// worker is a separate, long-lived thread with no visibility into the HTTP
// request's own RPCWalletRequestGuard/thread-local wallet selection (see
// walletmanager.cpp's g_requestedWalletName), so by the time main() actually
// runs, the request that queued this operation is long gone and there is no
// other point at which this wallet could still be safely re-resolved.
//
// The ref is held for as long as this C++ object exists, released in the
// destructor rather than at "operation finished": AsyncRPCOperation has no
// single completion choke-point every subclass funnels through (each sets
// its own terminal OperationStatus independently, at its own call sites --
// see set_state()), so tying release to object lifetime instead of a
// business-logic event is what makes it impossible for some untouched
// subclass exit path to leak the ref forever. This mirrors
// RPCWalletRequestGuard's own reasoning for a request in flight, and
// correctly extends to however long a finished operation's result sits in
// AsyncRPCQueue::operation_map_ waiting to be polled -- z_getoperationresult
// calling popOperationForId() is what actually drops the last shared_ptr and
// runs this destructor in the common case.
// Lock order note: RPC handlers that construct a wallet-aware operation
// (z_sendmany et al.) do so from inside LOCK2(cs_main, pwallet->cs_wallet),
// and z_getoperationstatus_IMPL (the shared impl behind z_getoperationresult)
// destroys one (via popOperationForId(), dropping the last shared_ptr) from
// that same LOCK2 -- so this constructor/destructor's own CWalletManager::Get() call
// (ResolveAndHoldForRequest()/ReleaseRefIfCurrent(), both taking cs_wallets)
// establishes cs_main -> cs_wallet -> cs_wallets as a real, exercised lock
// order in this codebase, not just a theoretical one. Nothing currently
// reachable while cs_wallets is held acquires cs_main or any cs_wallet back
// (CWalletManager's own internals only ever take cs_nWalletUnlockTime/
// bitdb->cs_db/the RPC timer lock under cs_wallets -- see UnloadWallet()/
// CancelWalletAutoLockTimer()), so there is no cycle today. A future change
// to CWalletManager that acquires cs_main or a CWallet's cs_wallet while
// already holding cs_wallets would deadlock against an in-flight
// z_sendmany/z_getoperationstatus -- keep that from happening rather than
// fixing it here.
#ifdef ENABLE_WALLET
AsyncRPCOperation::AsyncRPCOperation(CWallet* wallet) : AsyncRPCOperation() {
    wallet_ = wallet;
    if (wallet_ == nullptr)
        return;
    walletName_ = wallet_->strWalletFile;
    CWalletManager::ResolvedWallet resolved = CWalletManager::Get().ResolveAndHoldForRequest(walletName_);
    walletGeneration_ = resolved.generation;
    // NotFound means "nothing to hold": some test fixtures construct a
    // CWallet and use it as pwalletMain without ever registering it with
    // CWalletManager -- in that environment nothing can "unload" it through
    // this system either, so not pinning a ref doesn't leave anything
    // reachable unprotected. No-default-wallet redesign: every resolved
    // wallet is now uniformly ref-countable (there is no more an exempt
    // "default" outcome the way there used to be), so Held always pins one.
    if (resolved.outcome == CWalletManager::ResolveOutcome::Held)
        walletRefHeld_ = true;
}
#else
AsyncRPCOperation::AsyncRPCOperation(CWallet* wallet) : AsyncRPCOperation() {
    wallet_ = wallet;
}
#endif

// Neither this nor operator=() below copy wallet_/walletName_/
// walletGeneration_/walletRefHeld_ -- a copy would get a null wallet_ and,
// worse, walletRefHeld_ defaulting false regardless of the original's real
// state, so a copy's destructor would never release a ref the original is
// still holding on the original's behalf. Left uncorrected deliberately:
// these two are private and unreachable from outside the class, and every
// subclass (all 8, including the 3 that never touch wallet_ at all)
// declares its own copy/move constructor and assignment operator `= delete`,
// so nothing anywhere in this codebase can actually invoke either of these.
// If that ever stops being true, this comment is the warning that fixing it
// means copying those four fields too -- and that wallet_-holding
// AsyncRPCOperations still shouldn't be copied even then, since two
// objects would then independently release the same ref in their
// destructors.
AsyncRPCOperation::AsyncRPCOperation(const AsyncRPCOperation& o) :
        id_(o.id_), creation_time_(o.creation_time_), state_(o.state_.load()),
        start_time_(o.start_time_), end_time_(o.end_time_),
        error_code_(o.error_code_), error_message_(o.error_message_),
        result_(o.result_)
{
}

AsyncRPCOperation& AsyncRPCOperation::operator=( const AsyncRPCOperation& other ) {
    this->id_ = other.id_;
    this->creation_time_ = other.creation_time_;
    this->state_.store(other.state_.load());
    this->start_time_ = other.start_time_;
    this->end_time_ = other.end_time_;
    this->error_code_ = other.error_code_;
    this->error_message_ = other.error_message_;
    this->result_ = other.result_;
    return *this;
}


AsyncRPCOperation::~AsyncRPCOperation() {
#ifdef ENABLE_WALLET
    if (walletRefHeld_)
        CWalletManager::Get().ReleaseRefIfCurrent(walletName_, walletGeneration_);
#endif
}

/**
 * Override this cancel() method if you can interrupt main() when executing.
 */
void AsyncRPCOperation::cancel() {
    if (isReady()) {
        set_state(OperationStatus::CANCELLED);
    }
}

/**
 * Start timing the execution run of the code you're interested in
 */
void AsyncRPCOperation::start_execution_clock() {
    std::lock_guard<std::mutex> guard(lock_);
    start_time_ = std::chrono::system_clock::now();
}

/**
 * Stop timing the execution run
 */
void AsyncRPCOperation::stop_execution_clock() {
    std::lock_guard<std::mutex> guard(lock_);
    end_time_ = std::chrono::system_clock::now();
}

/**
 * Implement this virtual method in any subclass.  This is just an example implementation.
 */
void AsyncRPCOperation::main() {
    if (isCancelled()) {
        return;
    }
    
    set_state(OperationStatus::EXECUTING);

    start_execution_clock();

    // Do some work here..

    stop_execution_clock();

    // If there was an error, you might set it like this:
    /*
    setErrorCode(123);
    setErrorMessage("Murphy's law");
    setState(OperationStatus::FAILED);
    */

    // Otherwise, if the operation was a success:
    UniValue v(UniValue::VSTR, "We have a result!");
    set_result(v);
    set_state(OperationStatus::SUCCESS);
}

/**
 * Return the error of the completed operation as a UniValue object.
 * If there is no error, return null UniValue.
 */
UniValue AsyncRPCOperation::getError() const {
    if (!isFailed()) {
        return NullUniValue;
    }

    std::lock_guard<std::mutex> guard(lock_);
    UniValue error(UniValue::VOBJ);
    error.push_back(Pair("code", this->error_code_));
    error.push_back(Pair("message", this->error_message_));
    return error;
}

/**
 * Return the result of the completed operation as a UniValue object.
 * If the operation did not succeed, return null UniValue.
 */
UniValue AsyncRPCOperation::getResult() const {
    if (!isSuccess()) {
        return NullUniValue;
    }

    std::lock_guard<std::mutex> guard(lock_);
    return this->result_;
}


/**
 * Returns a status UniValue object.
 * If the operation has failed, it will include an error object.
 * If the operation has succeeded, it will include the result value.
 * If the operation was cancelled, there will be no error object or result value.
 */
UniValue AsyncRPCOperation::getStatus() const {
    OperationStatus status = this->getState();
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("id", this->id_));
    obj.push_back(Pair("status", OperationStatusMap[status]));
    obj.push_back(Pair("creation_time", this->creation_time_));
    // TODO: Issue #1354: There may be other useful metadata to return to the user.
    UniValue err = this->getError();
    if (!err.isNull()) {
        obj.push_back(Pair("error", err.get_obj()));
    }
    UniValue result = this->getResult();
    if (!result.isNull()) {
        obj.push_back(Pair("result", result));

        // Include execution time for successful operation
        std::chrono::duration<double> elapsed_seconds = end_time_ - start_time_;
        obj.push_back(Pair("execution_secs", elapsed_seconds.count()));

    }
    return obj;
}

/**
 * Return the operation state in human readable form.
 */
std::string AsyncRPCOperation::getStateAsString() const {
    OperationStatus status = this->getState();
    return OperationStatusMap[status];
}
