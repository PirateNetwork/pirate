// Copyright (c) 2016 The Zcash developers
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

#ifndef ASYNCRPCQUEUE_H
#define ASYNCRPCQUEUE_H

#include "asyncrpcoperation.h"

#include <iostream>
#include <string>
#include <chrono>
#include <queue>
#include <unordered_map>
#include <vector>
#include <future>
#include <thread>
#include <utility>
#include <memory>


typedef std::unordered_map<AsyncRPCOperationId, std::shared_ptr<AsyncRPCOperation> > AsyncRPCOperationMap; 


class AsyncRPCQueue {
public:
    static shared_ptr<AsyncRPCQueue> sharedInstance();

    AsyncRPCQueue();
    virtual ~AsyncRPCQueue();

    // We don't want queue to be copied or moved around
    AsyncRPCQueue(AsyncRPCQueue const&) = delete;             // Copy construct
    AsyncRPCQueue(AsyncRPCQueue&&) = delete;                  // Move construct
    AsyncRPCQueue& operator=(AsyncRPCQueue const&) = delete;  // Copy assign
    AsyncRPCQueue& operator=(AsyncRPCQueue &&) = delete;      // Move assign

    void addWorker();
    size_t getNumberOfWorkers() const;
    bool isClosed() const;
    bool isFinishing() const;
    void close(); // close queue and cancel all operations
    void finish(); // close queue but finishing existing operations
    void closeAndWait(); // block thread until all threads have terminated.
    void finishAndWait(); // block thread until existing operations have finished, threads terminated
    void cancelAllOperations(); // mark all operations in the queue as cancelled
    size_t getOperationCount() const;
    std::shared_ptr<AsyncRPCOperation> getOperationForId(AsyncRPCOperationId) const;
    std::shared_ptr<AsyncRPCOperation> popOperationForId(AsyncRPCOperationId);
    void addOperation(const std::shared_ptr<AsyncRPCOperation> &ptrOperation);
    std::vector<AsyncRPCOperationId> getAllOperationIds() const;

    // Drops every shared_ptr this queue still holds in operation_map_ (any
    // operation never retrieved via popOperationForId/z_getoperationresult),
    // running each one's destructor now rather than whenever this queue's
    // own shared_ptr (AsyncRPCQueue::sharedInstance()) happens to be
    // destroyed. Call only once no worker can be executing (i.e. after
    // closeAndWait()/finishAndWait()) and, for a node with wallet support,
    // before CWalletManager::Get().Reset() -- an AsyncRPCOperation built
    // against a wallet releases that wallet's ref in its own destructor,
    // and CWalletManager's static instance is constructed (via
    // RegisterDefaultWallet()) after this queue's, so plain process-exit
    // static destruction would run these destructors in the wrong order
    // otherwise: after CWalletManager's own static has already been torn
    // down.
    void clearOperationMap();

private:
    // addWorker() will spawn a new thread on run())
    void run(size_t workerId);
    void wait_for_worker_threads();

    // Why this is not a recursive lock: http://www.zaval.org/resources/library/butenhof1.html
    mutable std::mutex lock_;
    std::condition_variable condition_;
    std::atomic<bool> closed_;
    std::atomic<bool> finish_;
    AsyncRPCOperationMap operation_map_;
    std::queue <AsyncRPCOperationId> operation_id_queue_;
    std::vector<std::thread> workers_;
};

#endif


