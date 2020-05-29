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
const int CONSOLIDATION_EXPIRY_DELTA = 15;


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

    std::string s = strprintf("%s: Sapling Consolidation transaction created. (status=%s", getId(), getStateAsString());
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

    std::vector<CSproutNotePlaintextEntry> sproutEntries;
    std::vector<SaplingNoteEntry> saplingEntries;
    std::set<libzcash::SaplingPaymentAddress> addresses;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
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
                    addresses.insert(saplingAddress );
                }
            }
        } else {
            pwalletMain->GetSaplingPaymentAddresses(addresses);
        }
    }

    int numTxCreated = 0;
    std::vector<std::string> consolidationTxIds;
    CAmount amountConsolidated = 0;
    CCoinsViewCache coinsView(pcoinsTip);

    for (auto addr : addresses) {
        libzcash::SaplingExtendedSpendingKey extsk;
        if (pwalletMain->GetSaplingExtendedSpendingKey(addr, extsk)) {

            std::vector<SaplingNoteEntry> fromNotes;
            CAmount amountToSend = 0;
            int maxQuantity = rand() % 35 + 10;
            for (const SaplingNoteEntry& saplingEntry : saplingEntries) {

                libzcash::SaplingIncomingViewingKey ivk;
                pwalletMain->GetSaplingIncomingViewingKey(boost::get<libzcash::SaplingPaymentAddress>(saplingEntry.address), ivk);

                //Select Notes from that same address we will be sending to.
                if (ivk == extsk.expsk.full_viewing_key().in_viewing_key()) {
                  amountToSend += CAmount(saplingEntry.note.value());
                  fromNotes.push_back(saplingEntry);
                }

                //Only use a randomly determined number of notes between 10 and 45
                if (fromNotes.size() >= maxQuantity)
                  break;

            }

            //random minimum 2 - 12 required
            int minQuantity = rand() % 10 + 2;
            if (fromNotes.size() < minQuantity)
              continue;

            amountConsolidated += amountToSend;
            auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
            //builder.SetExpiryHeight(targetHeight_ + CONSOLIDATION_EXPIRY_DELTA);
            LogPrint("zrpcunsafe", "%s: Beginning creating transaction with Sapling output amount=%s\n", getId(), FormatMoney(amountToSend - fConsolidationTxFee));

            // Select Sapling notes
            std::vector<SaplingOutPoint> ops;
            std::vector<libzcash::SaplingNote> notes;
            for (auto fromNote : fromNotes) {
                ops.push_back(fromNote.op);
                notes.push_back(fromNote.note);
            }

            // Fetch Sapling anchor and witnesses
            uint256 anchor;
            std::vector<boost::optional<SaplingWitness>> witnesses;
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                pwalletMain->GetSaplingNoteWitnesses(ops, witnesses, anchor);
            }

            // Add Sapling spends
            for (size_t i = 0; i < notes.size(); i++) {
                if (!witnesses[i]) {
                    LogPrint("zrpcunsafe", "%s: Missing Witnesses. Stopping.\n", getId());
                    break;
                }
                builder.AddSaplingSpend(extsk.expsk, notes[i], anchor, witnesses[i].get());
            }

            builder.SetFee(fConsolidationTxFee);
            builder.AddSaplingOutput(extsk.expsk.ovk, addr, amountToSend - fConsolidationTxFee);
            //CTransaction tx = builder.Build();

            auto maybe_tx = builder.Build();
            if (!maybe_tx) {
                LogPrint("zrpcunsafe", "%s: Failed to build transaction.", getId());
                break;
            }
            CTransaction tx = maybe_tx.get();

            if (isCancelled()) {
                LogPrint("zrpcunsafe", "%s: Canceled. Stopping.\n", getId());
                break;
            }

            pwalletMain->CommitConsolidationTx(tx);
            LogPrint("zrpcunsafe", "%s: Committed consolidation transaction with txid=%s\n", getId(), tx.GetHash().ToString());
            amountConsolidated += amountToSend - fConsolidationTxFee;
            consolidationTxIds.push_back(tx.GetHash().ToString());

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
