#include "assert.h"
#include "boost/variant/static_visitor.hpp"
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

CAmount fSweepTxFee = DEFAULT_SWEEP_FEE;
bool fSweepMapUsed = false;
const int SWEEP_EXPIRY_DELTA = 40;
boost::optional<libzcash::SaplingPaymentAddress> rpcSweepAddress;

AsyncRPCOperation_sweeptoaddress::AsyncRPCOperation_sweeptoaddress(int targetHeight, bool fromRpc) : targetHeight_(targetHeight), fromRPC_(fromRpc){}

AsyncRPCOperation_sweeptoaddress::~AsyncRPCOperation_sweeptoaddress() {}

void AsyncRPCOperation_sweeptoaddress::main() {
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

    std::string s = strprintf("%s: Sapling Sweep transaction created. (status=%s", getId(), getStateAsString());
    if (success) {
        s += strprintf(", success)\n");
    } else {
        s += strprintf(", error=%s)\n", getErrorMessage());
    }

    LogPrintf("%s", s);
}

bool AsyncRPCOperation_sweeptoaddress::main_impl() {
    LogPrint("zrpcunsafe", "%s: Beginning asyncrpcoperation_sweeptoaddress.\n", getId());
    auto consensusParams = Params().GetConsensus();
    auto nextActivationHeight = NextActivationHeight(targetHeight_, consensusParams);
    if (nextActivationHeight && targetHeight_ + SWEEP_EXPIRY_DELTA >= nextActivationHeight.get()) {
        LogPrint("zrpcunsafe", "%s: Sweep txs would be created before a NU activation but may expire after. Skipping this round.\n", getId());
        setSweepResult(0, 0, std::vector<std::string>());
        return true;
    }

    // Resolve sweep destination address — RPC-set address takes priority over config.
    libzcash::SaplingPaymentAddress sweepAddress;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        if (rpcSweepAddress.is_initialized()) {
            sweepAddress = *rpcSweepAddress;
        } else if (!fromRPC_ && fSweepMapUsed) {
            const vector<string>& v = mapMultiArgs["-sweepsaplingaddress"];
            bool found = false;
            for (int i = 0; i < (int)v.size(); i++) {
                auto zAddress = DecodePaymentAddress(v[i]);
                if (boost::get<libzcash::SaplingPaymentAddress>(&zAddress) != nullptr) {
                    sweepAddress = boost::get<libzcash::SaplingPaymentAddress>(zAddress);
                    found = true;
                }
            }
            if (!found) return false;
        } else {
            return false;
        }
    }

    // Read all wallet Sapling addresses from the keystore - no note data loaded.
    // GetSaplingPaymentAddresses is O(num_keys), not O(num_notes).
    std::set<libzcash::PaymentAddress> filterAddresses;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        std::set<libzcash::SaplingPaymentAddress> allAddrs;
        pwalletMain->GetSaplingPaymentAddresses(allAddrs);
        for (const auto& addr : allAddrs) {
            if (!(addr == sweepAddress)) {
                filterAddresses.insert(addr);
            }
        }
    }

    int numTxCreated = 0;
    std::vector<std::string> sweepTxIds;
    CAmount amountSwept = 0;
    const int maxQuantity = 50;

    struct AddressSweepWork {
        libzcash::SaplingPaymentAddress addr;
        libzcash::SaplingExtendedSpendingKey extsk;
        std::vector<SaplingNoteEntry> saplingEntries;
    };

    std::vector<AddressSweepWork> addressWork;

    std::vector<CSproutNotePlaintextEntry> sproutEntries;
    std::vector<SaplingNoteEntry> allSaplingEntries;
    if (!filterAddresses.empty()) {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->GetFilteredNotes(sproutEntries, allSaplingEntries,
                                      filterAddresses,
                                      11, INT_MAX, true, true, true);
    }

    std::map<libzcash::SaplingPaymentAddress, size_t> workIndexByAddress;
    for (const auto& paymentAddr : filterAddresses) {
        if (isCancelled() || ShutdownRequested())
            break;

        const auto& addr = boost::get<libzcash::SaplingPaymentAddress>(paymentAddr);

        // Skip watch-only addresses - spending key required to build spends.
        libzcash::SaplingExtendedSpendingKey extsk;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (!pwalletMain->GetSaplingExtendedSpendingKey(addr, extsk))
                continue;
        }

        workIndexByAddress[addr] = addressWork.size();
        addressWork.push_back({addr, extsk, {}});
    }

    for (const auto& entry : allSaplingEntries) {
        auto it = workIndexByAddress.find(entry.address);
        if (it != workIndexByAddress.end()) {
            addressWork[it->second].saplingEntries.push_back(entry);
        }
    }

    for (auto& work : addressWork) {
        if (work.saplingEntries.empty())
            continue;

        size_t cursor = 0;

        while (cursor < work.saplingEntries.size()) {
            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Stopping sweep inner loop (cancelled or shutdown).", getId());
                break;
            }
            CAmount fee = fSweepTxFee;
            std::vector<SaplingOutPoint> ops;
            std::vector<libzcash::SaplingNote> notes;
            CAmount amountToSend = 0;

            int selected = 0;
            while (cursor < work.saplingEntries.size() && selected < maxQuantity) {
                const auto& entry = work.saplingEntries[cursor++];
                ops.push_back(entry.op);
                notes.push_back(entry.note);
                amountToSend += CAmount(entry.note.value());
                selected++;
            }

            if (ops.empty())
                break;

            if (amountToSend <= fee)
                break;

            auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                builder.SetExpiryHeight(chainActive.Tip()->nHeight + SWEEP_EXPIRY_DELTA);
            }
            LogPrint("zrpcunsafe", "%s: Beginning creating transaction with Sapling output amount=%s\n", getId(), FormatMoney(amountToSend - fee));

            uint256 anchor;
            std::vector<libzcash::MerklePath> saplingMerklePaths;
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->GetSaplingNoteMerklePaths(ops, saplingMerklePaths, anchor)) {
                    LogPrint("zrpcunsafe", "%s: Merkle Path not found for Sapling note. Stopping.\n", getId());
                    break;
                }
            }

            for (size_t i = 0; i < notes.size(); i++)
                builder.AddSaplingSpend(work.extsk.expsk, notes[i], anchor, saplingMerklePaths[i]);

            builder.SetFee(fee);
            builder.AddSaplingOutput(work.extsk.expsk.ovk, sweepAddress, amountToSend - fee);

            auto tx = builder.Build().GetTxOrThrow();

            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Canceled or shutdown. Stopping.\n", getId());
                break;
            }

            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->CommitAutomatedTx(tx)) {
                    LogPrint("zrpcunsafe", "%s: Failed to commit sweep transaction, stopping.\n", getId());
                    break;
                }
            }
            LogPrint("zrpcunsafe", "%s: Committed sweep transaction with txid=%s\n", getId(), tx.GetHash().ToString());
            numTxCreated++;
            amountSwept += amountToSend - fee;
            sweepTxIds.push_back(tx.GetHash().ToString());
        }
    }

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->nextSweep = pwalletMain->sweepInterval + chainActive.Tip()->nHeight;
        pwalletMain->fSweepRunning = false;
    }

    LogPrint("zrpcunsafe", "%s: Created %d transactions with total Sapling output amount=%s\n", getId(), numTxCreated, FormatMoney(amountSwept));
    setSweepResult(numTxCreated, amountSwept, sweepTxIds);
    return true;

}

void AsyncRPCOperation_sweeptoaddress::setSweepResult(int numTxCreated, const CAmount& amountSwept, const std::vector<std::string>& sweepTxIds) {
    UniValue res(UniValue::VOBJ);
    res.push_back(Pair("num_tx_created", numTxCreated));
    res.push_back(Pair("amount_swept", FormatMoney(amountSwept)));
    UniValue txIds(UniValue::VARR);
    for (const std::string& txId : sweepTxIds) {
        txIds.push_back(txId);
    }
    res.push_back(Pair("sweep_txids", txIds));
    set_result(res);
}

void AsyncRPCOperation_sweeptoaddress::cancel() {
    set_state(OperationStatus::CANCELLED);
}

UniValue AsyncRPCOperation_sweeptoaddress::getStatus() const {
    UniValue v = AsyncRPCOperation::getStatus();
    UniValue obj = v.get_obj();
    obj.push_back(Pair("method", "sweeptoaddress"));
    obj.push_back(Pair("target_height", targetHeight_));
    return obj;
}
