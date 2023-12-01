// Copyright (c) 2021-2023 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef PIRATE_WALLET_SAPLING_H
#define PIRATE_WALLET_SAPLING_H

#include <array>

#include "primitives/block.h"
#include "primitives/sapling.h"

#include "streams.h"
#include "streams_rust.h"
#include "rust/bridge.h"
#include "rust/sapling/wallet.h"
#include "zcash/IncrementalMerkleTree.hpp"

#include "util.h"

class SaplingWallet;
class SaplingWalletNoteCommitmentTreeWriter;
class SaplingWalletNoteCommitmentTreeLoader;

class SaplingWallet
{
private:
    std::unique_ptr<SaplingWalletPtr, decltype(&sapling_wallet_free)> inner;

    friend class SaplingWalletNoteCommitmentTreeWriter;
    friend class SaplingWalletNoteCommitmentTreeLoader;
public:
    SaplingWallet() : inner(sapling_wallet_new(), sapling_wallet_free) {}
    SaplingWallet(SaplingWallet&& wallet_data) : inner(std::move(wallet_data.inner)) {}
    SaplingWallet& operator=(SaplingWallet&& wallet)
    {
        if (this != &wallet) {
            inner = std::move(wallet.inner);
        }
        return *this;
    }

    // SaplingWallet should never be copied
    SaplingWallet(const SaplingWallet&) = delete;
    SaplingWallet& operator=(const SaplingWallet&) = delete;

    /**
     * Reset the state of the wallet to be suitable for rescan from the NU5 activation
     * height.  This removes all witness and spentness information from the wallet. The
     * keystore is unmodified and decrypted note, nullifier, and conflict data are left
     * in place with the expectation that they will be overwritten and/or updated in the
     * rescan process.
     */
    void Reset() {
        sapling_wallet_reset(inner.get());
    }

    /**
     * Overwrite the first bridge of the Sapling note commitment tree to have the
     * provided frontier as its latest state. This will fail with an assertion error
     * if any checkpoints exist in the tree.
     */
    void InitNoteCommitmentTree(const SaplingMerkleFrontier& frontier) {
        assert(!GetLastCheckpointHeight() >= 0);

        // LogPrintf("\nCommitment Tree LastCheckpointHeight %i\n", GetLastCheckpointHeight());
        //
        // auto walletPtr = reinterpret_cast<merkle_frontier::SaplingWallet*>(inner.get());
        //
        // auto initWallet = frontier.inner->init_wallet(walletPtr);
        //
        // LogPrintf("\nInit Commitment Tree %s\n", initWallet? "success":"fail");

        assert(frontier.inner->init_wallet(
            reinterpret_cast<merkle_frontier::SaplingWallet*>(inner.get())));
    }

    /**
     * Checkpoint the note commitment tree. This returns `false` and leaves the note
     * commitment tree unmodified if the block height specified is not the successor
     * to the last block height checkpointed.
     */
    bool CheckpointNoteCommitmentTree(int nBlockHeight) {
        assert(nBlockHeight >= 0);
        return sapling_wallet_checkpoint(inner.get(), (uint32_t) nBlockHeight);
    }

    /**
     * Return whether the sapling note commitment tree contains any checkpoints.
     */
    int GetLastCheckpointHeight() const {
        uint32_t lastHeight{0};
        if (sapling_wallet_get_last_checkpoint(inner.get(), &lastHeight)) {
            return (int) lastHeight;
        } else {
            return -1;
        }
    }

    /**
     * Rewinds to the most recent checkpoint, and marks as unspent any notes
     * previously identified as having been spent by transactions in the
     * latest block.
     */
    bool Rewind(int nBlockHeight, uint32_t& uResultHeight) {
        assert(nBlockHeight >= 0);
        return sapling_wallet_rewind(inner.get(), (uint32_t) nBlockHeight, &uResultHeight);
    }

    /**
     * Append each Sapling note commitment from the specified block to the
     * wallet's note commitment tree. Does not mark any not for tracking.
     *
     * Returns `false` if the caller attempts to insert a block out-of-order.
     */
    bool AppendNoteCommitments(const int nBlockHeight, const CBlock& block) {
        assert(nBlockHeight >= 0);
        for (int txidx = 0; txidx < block.vtx.size(); txidx++) {
            CTransaction tx = block.vtx[txidx];

            if(tx.vShieldedOutput.size()>0) {

                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss << tx;
                CRustTransaction rTx;
                ss >> rTx;
                SaplingBundle saplingBundle = rTx.GetSaplingBundle();

                LogPrintf("Appending commitments for block %i\n", GetLastCheckpointHeight());

                if (!sapling_wallet_append_bundle_commitments(
                        inner.get(),
                        (uint32_t) nBlockHeight,
                        txidx,
                        saplingBundle.GetDetails().as_ptr()
                        )) {
                    return false;
                }
            }
        }

        return true;
    }

    /**
     * Append a specific Sapling note commitment from the specified block/tx/output to the
     * wallet's note commitment tree. Marks the note for tracking if it is flagged as isMine
     *
     * Returns `false` if the caller attempts to insert a block out-of-order.
     */
    bool AppendNoteCommitment(const int nBlockHeight, int txidx, int outidx, const OutputDescription output, bool isMine) {
        assert(nBlockHeight >= 0);

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << output;

        rust::box<sapling::Output> rustOutput = sapling::parse_v4_output({reinterpret_cast<uint8_t*>(ss.data()), ss.size()});
        if (!sapling_wallet_append_single_commitment(
                inner.get(),
                (uint32_t) nBlockHeight,
                txidx,
                outidx,
                (*rustOutput).as_ptr(),
                isMine)) {
            return false;
        }

        return true;
    }


    uint256 GetLatestAnchor() const {
        uint256 value;
        // there is always a valid note commitment tree root at depth 0
        assert(sapling_wallet_commitment_tree_root(inner.get(), 0, value.begin()));
        return value;
    }

    /**
     * Return the root of the Sapling note commitment tree having the specified number
     * of confirmations. `confirmations` must be a value in the range `1..=100`; it is
     * not possible to spend shielded notes with 0 confirmations.
     */
    // std::optional<uint256> GetAnchorWithConfirmations(unsigned int confirmations) const {
    //     // the checkpoint depth is equal to the number of confirmations - 1
    //     assert(confirmations > 0);
    //     uint256 value;
    //     if (sapling_wallet_commitment_tree_root(inner.get(), (size_t) confirmations - 1, value.begin())) {
    //         return value;
    //     } else {
    //         return std::nullopt;
    //     }
    // }

    /**
     * Return the witness and other information required to spend a given note.
     * `anchorConfirmations` must be a value in the range `1..=100`; it is not
     * possible to spend shielded notes with 0 confirmations.
     *
     * This method checks the root of the wallet's note commitment tree having
     * the specified `anchorConfirmations` to ensure that it corresponds to the
     * specified anchor and will panic if this check fails.
     */
    // std::vector<std::pair<libzcash::SaplingSpendingKey, sapling::SpendInfo>> GetSpendInfo(
    //     const std::vector<SaplingNoteMetadata>& noteMetadata,
    //     unsigned int anchorConfirmations,
    //     const uint256& anchor) const;

    void GarbageCollect() {
        sapling_wallet_gc_note_commitment_tree(inner.get());
    }

};

class SaplingWalletNoteCommitmentTreeWriter
{
private:
    const SaplingWallet& wallet;
public:
    SaplingWalletNoteCommitmentTreeWriter(const SaplingWallet& wallet): wallet(wallet) {}

    template<typename Stream>
    void Serialize(Stream& s) const {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            ::Serialize(s, nVersion);
        }
        RustStream <Stream>rs(s);
        if (!sapling_wallet_write_note_commitment_tree(
                    wallet.inner.get(),
                    &rs, RustStream<Stream>::write_callback)) {
            throw std::ios_base::failure("Failed to serialize Sapling note commitment tree.");
        }
    }
};

class SaplingWalletNoteCommitmentTreeLoader
{
private:
    SaplingWallet& wallet;
public:
    SaplingWalletNoteCommitmentTreeLoader(SaplingWallet& wallet): wallet(wallet) {}

    template<typename Stream>
    void Unserialize(Stream& s) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            ::Unserialize(s, nVersion);
        }
        RustStream <Stream>rs(s);
        if (!sapling_wallet_load_note_commitment_tree(
                    wallet.inner.get(),
                    &rs, RustStream<Stream>::read_callback)) {
            throw std::ios_base::failure("Failed to load Sapling note commitment tree.");
        }
    }
};

#endif // PIRATE_WALLET_SAPLING_H
