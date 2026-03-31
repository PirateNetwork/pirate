#include "assert.h"
#include "boost/variant/static_visitor.hpp"
#include "asyncrpcoperation_saplingconsolidation.h"
#include "asyncrpcoperation_sweeptoaddress.h"
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
    LogPrint("zrpcunsafe", "%s: Beginning AsyncRPCOperation_saplingconsolidation.\n", getId());
    auto consensusParams = Params().GetConsensus();
    auto nextActivationHeight = NextActivationHeight(targetHeight_, consensusParams);
    if (nextActivationHeight && targetHeight_ + CONSOLIDATION_EXPIRY_DELTA >= nextActivationHeight.get()) {
        LogPrint("zrpcunsafe", "%s: Consolidation txs would be created before a NU activation but may expire after. Skipping this round.\n", getId());
        setConsolidationResult(0, 0, std::vector<std::string>());
        return true;
    }

    int numTxCreated = 0;
    std::vector<std::string> consolidationTxIds;
    CAmount amountConsolidated = 0;

    // STEP 1: Read wallet addresses and config from keystore only - no note data loaded.
    // GetSaplingPaymentAddresses reads key metadata only, O(num_keys) not O(num_notes).
    int consolidationTarget = 0;
    std::vector<libzcash::SaplingPaymentAddress> candidateAddresses;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        consolidationTarget = pwalletMain->targetConsolidationQty;

        if (pwalletMain->fSaplingSweepEnabled) {
            // When sweep is active, consolidation is restricted to the sweep destination
            // address only - consolidating into it prepares funds for the sweep.
            if (rpcSweepAddress.is_initialized()) {
                candidateAddresses.push_back(*rpcSweepAddress);
            } else if (fSweepMapUsed) {
                const vector<string>& v = mapMultiArgs["-sweepsaplingaddress"];
                for (int i = 0; i < (int)v.size(); i++) {
                    auto zAddress = DecodePaymentAddress(v[i]);
                    if (boost::get<libzcash::SaplingPaymentAddress>(&zAddress) != nullptr)
                        candidateAddresses.push_back(boost::get<libzcash::SaplingPaymentAddress>(zAddress));
                }
            }
            // If sweep is enabled but no sweep address is configured, skip consolidation.
        } else if (fConsolidationMapUsed) {
            // Sweep not active: use the explicit consolidation address filter list.
            const vector<string>& v = mapMultiArgs["-consolidatesaplingaddress"];
            for (int i = 0; i < (int)v.size(); i++) {
                auto zAddress = DecodePaymentAddress(v[i]);
                if (boost::get<libzcash::SaplingPaymentAddress>(&zAddress) != nullptr)
                    candidateAddresses.push_back(boost::get<libzcash::SaplingPaymentAddress>(zAddress));
            }
        } else {
            // No filter active: consolidate all wallet Sapling addresses.
            // GetSaplingPaymentAddresses reads key metadata only, O(num_keys) not O(num_notes).
            std::set<libzcash::SaplingPaymentAddress> allAddrs;
            pwalletMain->GetSaplingPaymentAddresses(allAddrs);
            candidateAddresses.assign(allAddrs.begin(), allAddrs.end());
        }
    }

    // STEP 2: Per-address: check spending key, probe for threshold, then consolidate.
    for (const auto& addr : candidateAddresses) {
        if (isCancelled() || ShutdownRequested())
            break;
        // Spending key check first - skip watch-only addresses immediately.
        libzcash::SaplingExtendedSpendingKey extsk;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (!pwalletMain->GetSaplingExtendedSpendingKey(addr, extsk))
                continue;
        }

        // Threshold probe: fetch at most consolidationTarget notes under a brief lock.
        // GetFilteredNotes exits early once maxNotes is reached, so lock hold is bounded.
        {
            std::vector<CSproutNotePlaintextEntry> sproutProbe;
            std::vector<SaplingNoteEntry> saplingProbe;
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                std::set<libzcash::PaymentAddress> filterAddr;
                filterAddr.insert(addr);
                pwalletMain->GetFilteredNotes(sproutProbe, saplingProbe, filterAddr,
                                              11, INT_MAX, true, true, true,
                                              consolidationTarget, 0);
            }
            if ((int)saplingProbe.size() < consolidationTarget)
                continue;
        }

        // Random note count bounds chosen once per address.
        // maxQuantity must be chosen first so minQuantity can be clamped below it.
        const int maxQuantity = rand() % 35 + 10;          // [10, 44]
        const int minQuantity = rand() % std::min(9, maxQuantity - 1) + 2;  // [2, min(10, maxQuantity-1)]

        // Per-address inner loop: keep consolidating until fewer than minQuantity
        // unspent notes remain or they cannot cover the fee.
        // Committed notes are excluded by ignoreSpent=true on the next call.
        while (true) {
            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Stopping consolidation inner loop (cancelled or shutdown).", getId());
                break;
            }
            std::vector<CSproutNotePlaintextEntry> sproutEntries;
            std::vector<SaplingNoteEntry> saplingEntries;
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                std::set<libzcash::PaymentAddress> filterAddresses;
                filterAddresses.insert(addr);
                pwalletMain->GetFilteredNotes(sproutEntries, saplingEntries, filterAddresses,
                                              11, INT_MAX, true, true, true,
                                              maxQuantity, fConsolidationTxFee + 1);
            }

            if ((int)saplingEntries.size() < minQuantity)
                break;

            CAmount amountToSend = 0;
            for (const auto& e : saplingEntries)
                amountToSend += CAmount(e.note.value());

            if (amountToSend <= fConsolidationTxFee)
                break;

            std::vector<SaplingOutPoint> ops;
            std::vector<libzcash::SaplingNote> notes;
            for (const auto& entry : saplingEntries) {
                ops.push_back(entry.op);
                notes.push_back(entry.note);
            }

            uint256 anchor;
            std::vector<libzcash::MerklePath> saplingMerklePaths;
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->GetSaplingNoteMerklePaths(ops, saplingMerklePaths, anchor)) {
                    LogPrint("zrpcunsafe", "%s: Merkle Path not found for Sapling note. Stopping.\n", getId());
                    break;
                }
            }

            auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                builder.SetExpiryHeight(chainActive.Tip()->nHeight + CONSOLIDATION_EXPIRY_DELTA);
            }
            LogPrint("zrpcunsafe", "%s: Beginning creating transaction with Sapling output amount=%s\n", getId(), FormatMoney(amountToSend - fConsolidationTxFee));

            for (size_t i = 0; i < notes.size(); i++)
                builder.AddSaplingSpend(extsk.expsk, notes[i], anchor, saplingMerklePaths[i]);

            builder.SetFee(fConsolidationTxFee);
            builder.AddSaplingOutput(extsk.expsk.ovk, addr, amountToSend - fConsolidationTxFee);

            auto tx = builder.Build().GetTxOrThrow();

            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Canceled. Stopping.\n", getId());
                break;
            }

            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->CommitAutomatedTx(tx)) {
                    LogPrint("zrpcunsafe", "%s: Failed to commit consolidation transaction, stopping.\n", getId());
                    break;
                }
            }
            LogPrint("zrpcunsafe", "%s: Committed consolidation transaction with txid=%s\n", getId(), tx.GetHash().ToString());
            amountConsolidated += amountToSend - fConsolidationTxFee;
            numTxCreated++;
            consolidationTxIds.push_back(tx.GetHash().ToString());
        }
    }

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->nextConsolidation = pwalletMain->initializeConsolidationInterval + chainActive.Tip()->nHeight;
        pwalletMain->fConsolidationRunning = false;
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


