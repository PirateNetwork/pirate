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

    std::vector<CSproutNotePlaintextEntry> sproutEntries;
    std::vector<SaplingNoteEntry> saplingEntries;
    libzcash::SaplingPaymentAddress sweepAddress;
    std::map<libzcash::SaplingPaymentAddress, std::vector<SaplingNoteEntry>> mapAddresses;

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        // We set minDepth to 11 to avoid unconfirmed notes and in anticipation of specifying
        // an anchor at height N-10 for each Sprout JoinSplit description
        // Consider, should notes be sorted?
        pwalletMain->GetFilteredNotes(sproutEntries, saplingEntries, "", 11);
        if (!fromRPC_) {
            if (fSweepMapUsed) {
                const vector<string>& v = mapMultiArgs["-sweepsaplingaddress"];
                for(int i = 0; i < v.size(); i++) {
                    auto zAddress = DecodePaymentAddress(v[i]);
                    if (boost::get<libzcash::SaplingPaymentAddress>(&zAddress) != nullptr) {
                        sweepAddress = boost::get<libzcash::SaplingPaymentAddress>(zAddress);
                    }
                }
            } else {
                return false;
            }
        } else {
            if (boost::get<libzcash::SaplingPaymentAddress>(&rpcSweepAddress) != nullptr) {
                sweepAddress = boost::get<libzcash::SaplingPaymentAddress>(rpcSweepAddress);
            } else {
                return false;
            }
        }

        for (auto & entry : saplingEntries) {
            //Map all notes by address
            if (sweepAddress == entry.address) {
                continue;
            } else {
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

    int numTxCreated = 0;
    std::vector<std::string> sweepTxIds;
    CAmount amountSwept = 0;
    CCoinsViewCache coinsView(pcoinsTip);
    bool sweepComplete = true;

    for (std::map<libzcash::SaplingPaymentAddress, std::vector<SaplingNoteEntry>>::iterator it = mapAddresses.begin(); it != mapAddresses.end(); it++) {
        auto addr = (*it).first;
        auto saplingEntries = (*it).second;

        libzcash::SaplingExtendedSpendingKey extsk;
        if (pwalletMain->GetSaplingExtendedSpendingKey(addr, extsk)) {

            std::vector<SaplingNoteEntry> fromNotes;
            CAmount amountToSend = pwalletMain->targetSweepQty;
            int maxQuantity = 50;

            //Count Notes availiable for this address
            int targetCount = 0;
            int noteCount = 0;
            for (const SaplingNoteEntry& saplingEntry : saplingEntries) {

              libzcash::SaplingIncomingViewingKey ivk;
              pwalletMain->GetSaplingIncomingViewingKey(boost::get<libzcash::SaplingPaymentAddress>(saplingEntry.address), ivk);

              if (ivk == extsk.expsk.full_viewing_key().in_viewing_key() && saplingEntry.address == addr) {
                noteCount ++;
              }
            }

            //Don't sweep if under the threshold
            if (noteCount <= targetCount){
                continue;
            }

            //if we make it here then we need to sweep and the routine is considered incomplete
            sweepComplete = false;

            for (const SaplingNoteEntry& saplingEntry : saplingEntries) {

                libzcash::SaplingIncomingViewingKey ivk;
                pwalletMain->GetSaplingIncomingViewingKey(boost::get<libzcash::SaplingPaymentAddress>(saplingEntry.address), ivk);

                //Select Notes from that same address we will be sending to.
                if (ivk == extsk.expsk.full_viewing_key().in_viewing_key() && saplingEntry.address == addr) {
                  amountToSend += CAmount(saplingEntry.note.value());
                  fromNotes.push_back(saplingEntry);
                }

                if (fromNotes.size() >= maxQuantity)
                  break;

            }

            int minQuantity = 1;
            if (fromNotes.size() < minQuantity)
              continue;

            CAmount fee = fSweepTxFee;
            if (amountToSend <= fSweepTxFee) {
              fee = 0;
            }
            amountSwept += amountToSend;
            auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                builder.SetExpiryHeight(chainActive.Tip()->nHeight+ SWEEP_EXPIRY_DELTA);
            }
            LogPrint("zrpcunsafe", "%s: Beginning creating transaction with Sapling output amount=%s\n", getId(), FormatMoney(amountToSend - fee));

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

            // Add Sapling spends
            for (size_t i = 0; i < notes.size(); i++) {
                builder.AddSaplingSpend(extsk.expsk, notes[i], anchor, saplingMerklePaths[i]);
            }

            builder.SetFee(fee);
            builder.AddSaplingOutput(extsk.expsk.ovk, sweepAddress, amountToSend - fee);

            auto tx = builder.Build().GetTxOrThrow();

            if (isCancelled()) {
                LogPrint("zrpcunsafe", "%s: Canceled. Stopping.\n", getId());
                break;
            }

            pwalletMain->CommitAutomatedTx(tx);
            LogPrint("zrpcunsafe", "%s: Committed sweep transaction with txid=%s\n", getId(), tx.GetHash().ToString());
            amountSwept += amountToSend - fee;
            sweepTxIds.push_back(tx.GetHash().ToString());

        }
    }

    if (sweepComplete) {
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
