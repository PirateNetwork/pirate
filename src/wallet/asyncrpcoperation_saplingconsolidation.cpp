#include "assert.h"
#include "boost/variant/static_visitor.hpp"
#include "asyncrpcoperation_saplingconsolidation.h"
#include "init.h"
#include "key_io.h"
#include "rpc/protocol.h"
#include "random.h"
#include "sync.h"
#include "tinyformat.h"
#include "transaction_builder.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet.h"

CAmount fConsolidationTxFee = DEFAULT_CONSOLIDATION_FEE;
bool fConsolidationMapUsed = false;
const int CONSOLIDATION_EXPIRY_DELTA = 40;


AsyncRPCOperation_saplingconsolidation::AsyncRPCOperation_saplingconsolidation(int targetHeight) : targetHeight_(targetHeight) {}

AsyncRPCOperation_saplingconsolidation::~AsyncRPCOperation_saplingconsolidation() {}

void AsyncRPCOperation_saplingconsolidation::main() {
    if (isCancelled())
        return;

    set_state(OperationStatus::EXECUTING);
    start_execution_clock();

    bool success = false;

    try {
        success = main_impl();
    } catch (const UniValue& objError) {
        int code = find_value(objError, "code").get_int();
        std::string message = find_value(objError, "message").get_str();
        set_error_code(code);
        set_error_message(message);
    } catch (const runtime_error& e) {
        set_error_code(-1);
        set_error_message("runtime error: " + string(e.what()));
    } catch (const logic_error& e) {
        set_error_code(-1);
        set_error_message("logic error: " + string(e.what()));
    } catch (const exception& e) {
        set_error_code(-1);
        set_error_message("general exception: " + string(e.what()));
    } catch (...) {
        set_error_code(-2);
        set_error_message("unknown error");
    }

    stop_execution_clock();

    if (success) {
        set_state(OperationStatus::SUCCESS);
    } else {
        set_state(OperationStatus::FAILED);
    }

    std::string s = strprintf("%s: Sapling Consolidation routine complete. (status=%s", getId(), getStateAsString());
    if (success) {
        s += strprintf(", success)\n");
    } else {
        s += strprintf(", error=%s)\n", getErrorMessage());
    }

    LogPrintf("%s", s);
}

bool AsyncRPCOperation_saplingconsolidation::main_impl() {

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        if (fCleanUpMode && pwalletMain->fCleanupRoundComplete) {
            //update the mode statuses and recheck next round
            checkCleanUpConfirmedOrExpired();
            LogPrint("zrpcunsafe", "%s: Cleanup in round complete status, skipping this cycle.\n", getId());
            return true;
        }
    }

    LogPrint("zrpcunsafe", "%s: Beginning AsyncRPCOperation_saplingconsolidation.\n", getId());
    auto consensusParams = Params().GetConsensus();
    auto nextActivationHeight = NextActivationHeight(targetHeight_, consensusParams);
    if (nextActivationHeight && targetHeight_ + CONSOLIDATION_EXPIRY_DELTA >= nextActivationHeight.get()) {
        LogPrint("zrpcunsafe", "%s: Consolidation txs would be created before a NU activation but may expire after. Skipping this round.\n", getId());
        setConsolidationResult(0, 0, std::vector<std::string>());
        return true;
    }


    int64_t nNow = GetTime();
    bool roundComplete = false;
    bool foundAddressWithMultipleNotes = false;
    bool consolidationComplete = true;
    int numTxCreated = 0;
    std::vector<std::string> consolidationTxIds;
    CAmount amountConsolidated = 0;
    int consolidationTarget = 0;

    for (int i = 0; i < 50; i++) {

        std::vector<CSproutNotePlaintextEntry> sproutEntries;
        std::vector<SaplingNoteEntry> saplingEntries;
        std::set<libzcash::SaplingPaymentAddress> addresses;
        std::map<libzcash::SaplingPaymentAddress, std::vector<SaplingNoteEntry>> mapAddresses;
        std::map<std::pair<int,int>, SaplingNoteEntry> mapsortedEntries;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            consolidationTarget = pwalletMain->targetConsolidationQty;
            // We set minDepth to 11 to avoid unconfirmed notes and in anticipation of specifying
            // an anchor at height N-10 for each Sprout JoinSplit description
            // Consider, should notes be sorted?
            pwalletMain->GetFilteredNotes(sproutEntries, saplingEntries, "", 11);
            if (fConsolidationMapUsed) {
                const vector<string>& v = mapMultiArgs["-consolidatesaplingaddress"];
                for(int i = 0; i < v.size(); i++) {
                    auto zAddress = DecodePaymentAddress(v[i]);
                    if (boost::get<libzcash::SaplingPaymentAddress>(&zAddress) != nullptr) {
                        libzcash::SaplingPaymentAddress saplingAddress = boost::get<libzcash::SaplingPaymentAddress>(zAddress);
                        addresses.insert(saplingAddress);
                    }
                }
            }

            //Sort Entries
            for (auto & entry : saplingEntries) {
                  auto entryIndex = std::make_pair(entry.confirmations, entry.op.n);
                  mapsortedEntries.insert({entryIndex, entry});
            }

            //Store unspent note size for reporting
            if (fCleanUpMode) {
                int nNoteCount = saplingEntries.size();
                pwalletMain->cleanupCurrentRoundUnspent = nNoteCount;
            }

            for (std::map<std::pair<int,int>, SaplingNoteEntry>::reverse_iterator rit = mapsortedEntries.rbegin(); rit != mapsortedEntries.rend(); rit++) {
                SaplingNoteEntry entry = (*rit).second;
                //Map all notes by address
                if (addresses.count(entry.address) > 0 || !fConsolidationMapUsed) {
                    std::map<libzcash::SaplingPaymentAddress, std::vector<SaplingNoteEntry>>::iterator it;
                    it = mapAddresses.find(entry.address);
                    if (it != mapAddresses.end()) {
                        it->second.push_back(entry);
                    } else {
                        std::vector<SaplingNoteEntry> entries;
                        entries.push_back(entry);
                        mapAddresses[entry.address] = entries;
                    }
                }
            }
        }

        CCoinsViewCache coinsView(pcoinsTip);
        consolidationComplete = true;

        //Don't consolidate if under the threshold
        if (saplingEntries.size() < consolidationTarget){
            roundComplete = true;
        } else {
            //if we make it here then we need to consolidate and the routine is considered incomplete
            consolidationComplete = false;

            for (std::map<libzcash::SaplingPaymentAddress, std::vector<SaplingNoteEntry>>::iterator it = mapAddresses.begin(); it != mapAddresses.end(); it++) {
                auto addr = (*it).first;
                auto addrSaplingEntries = (*it).second;

                libzcash::SaplingExtendedSpendingKey extsk;
                if (pwalletMain->GetSaplingExtendedSpendingKey(addr, extsk)) {

                    std::vector<SaplingNoteEntry> fromNotes;
                    CAmount amountToSend = 0;

                    int minQuantity = rand() % 10 + 2;
                    int maxQuantity = rand() % 35 + 10;
                    if (fCleanUpMode) {
                        minQuantity = 2;
                        maxQuantity = 25;
                    }

                    if (addrSaplingEntries.size() > 1) {
                        foundAddressWithMultipleNotes = true;
                    }

                    for (const SaplingNoteEntry& saplingEntry : addrSaplingEntries) {

                        libzcash::SaplingIncomingViewingKey ivk;
                        pwalletMain->GetSaplingIncomingViewingKey(boost::get<libzcash::SaplingPaymentAddress>(saplingEntry.address), ivk);

                        //Select Notes from that same address we will be sending to.
                        if (ivk == extsk.expsk.full_viewing_key().in_viewing_key() && saplingEntry.address == addr) {
                          amountToSend += CAmount(saplingEntry.note.value());
                          fromNotes.push_back(saplingEntry);
                        }

                        //Only use a randomly determined number of notes between 10 and 45
                        if (fromNotes.size() >= maxQuantity)
                            break;

                    }

                    //random minimum 2 - 12 required
                    if (fromNotes.size() < minQuantity)
                        continue;

                    // Select Sapling notes
                    std::vector<SaplingOutPoint> ops;
                    std::vector<libzcash::SaplingNote> notes;
                    for (auto fromNote : fromNotes) {
                        ops.push_back(fromNote.op);
                        notes.push_back(fromNote.note);
                    }

                    // Fetch Sapling anchor and merkle paths
                    uint256 anchor;
                    std::vector<libzcash::MerklePath> saplingMerklePaths;
                    {
                        LOCK2(cs_main, pwalletMain->cs_wallet);
                        if (!pwalletMain->GetSaplingNoteMerklePaths(ops, saplingMerklePaths, anchor)) {
                            LogPrint("zrpcunsafe", "%s: Merkle Path not found for Sapling note. Stopping.\n", getId());
                        }
                    }

                    CAmount fee = fConsolidationTxFee;
                    if (amountToSend <= fConsolidationTxFee) {
                      fee = 0;
                    }
                    amountConsolidated += amountToSend;
                    auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
                    int nExpires = 0;
                    {
                        LOCK2(cs_main, pwalletMain->cs_wallet);
                        nExpires = chainActive.Tip()->nHeight + CONSOLIDATION_EXPIRY_DELTA;
                        builder.SetExpiryHeight(nExpires);
                    }
                    LogPrint("zrpcunsafe", "%s: Beginning creating transaction with Sapling output amount=%s\n", getId(), FormatMoney(amountToSend - fee));

                    // Add Sapling spends
                    for (size_t i = 0; i < notes.size(); i++) {
                        builder.AddSaplingSpend(extsk.expsk, notes[i], anchor, saplingMerklePaths[i]);
                    }


                    builder.SetFee(fee);
                    builder.AddSaplingOutput(extsk.expsk.ovk, addr, amountToSend - fee);

                    auto tx = builder.Build().GetTxOrThrow();

                    if (isCancelled()) {
                        LogPrint("zrpcunsafe", "%s: Canceled. Stopping.\n", getId());
                        break;
                    }

                    pwalletMain->CommitAutomatedTx(tx);
                    LogPrint("zrpcunsafe", "%s: Committed consolidation transaction with txid=%s\n", getId(), tx.GetHash().ToString());
                    amountConsolidated += amountToSend - fee;
                    numTxCreated++;
                    consolidationTxIds.push_back(tx.GetHash().ToString());

                    //Gather up txids until the round is complete
                    if (fCleanUpMode) {
                        {
                            LOCK2(cs_main, pwalletMain->cs_wallet);
                            pwalletMain->cleanupCurrentRoundUnspent = pwalletMain->cleanupCurrentRoundUnspent - fromNotes.size();
                            pwalletMain->cleanUpUnconfirmed++;
                            pwalletMain->vCleanUpTxids.push_back(tx.GetHash());
                            pwalletMain->cleanupMaxExpirationHieght = std::max(pwalletMain->cleanupMaxExpirationHieght, nExpires);
                        }
                    }
                }

                if (fCleanUpMode && nNow + 180 < GetTime()) {
                    LogPrint("zrpcunsafe", "%s: Exiting inner loop, long running.\n", getId());
                    break;
                }
            }
        }

        if (fCleanUpMode && nNow + 180 < GetTime()) {
            LogPrint("zrpcunsafe", "%s: Exiting outer loop, long running.\n", getId());
            break;
        }

        if (roundComplete) {
            break;
        }

        if (!foundAddressWithMultipleNotes) {
            break;
        }
    }

    updateCleanupMetrics();

    if (roundComplete) {
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            pwalletMain->fCleanupRoundComplete = true;
        }
    }

    if (!foundAddressWithMultipleNotes) {
        //Exit Cleanup mode. All Addresses only have 1 note
        //no more consolidation possible
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            pwalletMain->fCleanupRoundComplete = true;
        }
        //also consider the routine complete
        consolidationComplete = true;
    }

    if (consolidationComplete || fCleanUpMode) {
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (fCleanUpMode) {
                //Let the wallet catchup if the process ran long
                auto waitBlocks = ((GetTime() - nNow)/60);
                pwalletMain->nextConsolidation = chainActive.Tip()->nHeight + waitBlocks;
            } else {
                pwalletMain->nextConsolidation = pwalletMain->initializeConsolidationInterval + chainActive.Tip()->nHeight;
                pwalletMain->fConsolidationRunning = false;
            }
        }
    }

    LogPrint("zrpcunsafe", "%s: Created %d transactions with total Sapling output amount=%s\n", getId(), numTxCreated, FormatMoney(amountConsolidated));
    setConsolidationResult(numTxCreated, amountConsolidated, consolidationTxIds);
    return true;

}

void AsyncRPCOperation_saplingconsolidation::setConsolidationResult(int numTxCreated, const CAmount& amountConsolidated, const std::vector<std::string>& consolidationTxIds) {
    UniValue res(UniValue::VOBJ);
    res.push_back(Pair("num_tx_created", numTxCreated));
    res.push_back(Pair("amount_consolidated", FormatMoney(amountConsolidated)));
    UniValue txIds(UniValue::VARR);
    for (const std::string& txId : consolidationTxIds) {
        txIds.push_back(txId);
    }
    res.push_back(Pair("consolidation_txids", txIds));
    set_result(res);
}

void AsyncRPCOperation_saplingconsolidation::cancel() {
    set_state(OperationStatus::CANCELLED);
}

UniValue AsyncRPCOperation_saplingconsolidation::getStatus() const {
    UniValue v = AsyncRPCOperation::getStatus();
    UniValue obj = v.get_obj();
    obj.push_back(Pair("method", "saplingconsolidation"));
    obj.push_back(Pair("target_height", targetHeight_));
    return obj;
}

void AsyncRPCOperation_saplingconsolidation::updateCleanupMetrics() {

    LOCK2(cs_main, pwalletMain->cs_wallet);

    int expiredTxs = 0;
    int unconfirmedTxs = 0;
    int confirmedTxs = 0;

    for (int i = 0; i < pwalletMain->vCleanUpTxids.size(); i++) {
        std::map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.find(pwalletMain->vCleanUpTxids[i]);
        if (it != pwalletMain->mapWallet.end()) {
            CWalletTx *pwtx = &(*it).second;

            if (pwtx->GetDepthInMainChain() < 0) {
                expiredTxs++;
            }

            if (pwtx->GetDepthInMainChain() == 0) {
                unconfirmedTxs++;
            }

        } else {
            expiredTxs++;
        }
    }

    pwalletMain->cleanUpConfirmed = pwalletMain->vCleanUpTxids.size() - expiredTxs - unconfirmedTxs;
    pwalletMain->cleanUpConflicted = expiredTxs;
    pwalletMain->cleanUpUnconfirmed = unconfirmedTxs;

    return;

}

void AsyncRPCOperation_saplingconsolidation::checkCleanUpConfirmedOrExpired() {

    LOCK2(cs_main, pwalletMain->cs_wallet);

    bool foundExpired = false;
    bool foundUnconfirmed = false;

    int expiredTxs = 0;
    int unconfirmedTxs = 0;
    int confirmedTxs = 0;

    for (int i = 0; i < pwalletMain->vCleanUpTxids.size(); i++) {

        std::map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.find(pwalletMain->vCleanUpTxids[i]);
        if (it != pwalletMain->mapWallet.end()) {
            CWalletTx *pwtx = &(*it).second;

            if (pwtx->GetDepthInMainChain() < 0) {
                foundExpired = true;
                expiredTxs++;
            }

            if (pwtx->GetDepthInMainChain() == 0) {
                foundUnconfirmed = true;
                unconfirmedTxs++;
            }

        } else {
            foundExpired = true;
            expiredTxs++;
        }

    }

    pwalletMain->cleanUpConfirmed = pwalletMain->vCleanUpTxids.size() - expiredTxs - unconfirmedTxs;
    pwalletMain->cleanUpConflicted = expiredTxs;
    pwalletMain->cleanUpUnconfirmed = unconfirmedTxs;

    //All cleanup transactions have been confirmed
    if (!foundExpired && !foundUnconfirmed) {
        LogPrintf("Cleanup mode complete, resuming normal operations.\n");
        fCleanUpMode = false;
        pwalletMain->strCleanUpStatus = "Complete";
        pwalletMain->vCleanUpTxids.resize(0);
        pwalletMain->cleanUpConfirmed = 0;
        pwalletMain->cleanUpConflicted = 0;
        pwalletMain->cleanUpUnconfirmed = 0;
        pwalletMain->cleanupMaxExpirationHieght = 0;
        pwalletMain->cleanupCurrentRoundUnspent = 0;
        pwalletMain->nextConsolidation = pwalletMain->initializeConsolidationInterval + chainActive.Tip()->nHeight;
        pwalletMain->fConsolidationRunning = false;
        return;
    }

    //Cleanup transactions are either  confirmed or expired.
    //Reset and process another round.
    if (foundExpired && !foundUnconfirmed) {
        pwalletMain->vCleanUpTxids.resize(0);
        pwalletMain->cleanUpConfirmed = 0;
        pwalletMain->cleanUpConflicted = 0;
        pwalletMain->cleanUpUnconfirmed = 0;
        pwalletMain->cleanupMaxExpirationHieght = 0;
        pwalletMain->cleanupCurrentRoundUnspent = 0;
        pwalletMain->fCleanupRoundComplete = false;
        pwalletMain->strCleanUpStatus = "Creating cleanup transactions.";
        return;
    }

    //Else there are still some uncofirmed transactions, wcontinue to wait.
    pwalletMain->strCleanUpStatus = "Waiting for cleanup transactions to confirm.";
    return;

}
