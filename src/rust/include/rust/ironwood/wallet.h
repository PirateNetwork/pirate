// Copyright (c) 2021-2023 The Zcash developers
// Copyright (c) 2021-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef ZCASH_RUST_INCLUDE_RUST_IRONWOOD_WALLET_H
#define ZCASH_RUST_INCLUDE_RUST_IRONWOOD_WALLET_H

#include "rust/builder.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A type-safe pointer type for an Ironwood wallet.
 */
struct IronwoodWalletPtr;
typedef struct IronwoodWalletPtr IronwoodWalletPtr;

/**
 * Constructs a new empty Ironwood wallet and return a pointer to it.
 * Memory is allocated by Rust and must be manually freed using
 * `ironwood_wallet_free`.
 */
IronwoodWalletPtr* ironwood_wallet_new();

/**
 * Frees the memory associated with an Ironwood wallet that was allocated
 * by Rust.
 */
void ironwood_wallet_free(IronwoodWalletPtr* wallet);

/**
 * Reset the state of the wallet to be suitable for rescan from the NU5 activation
 * height.  This removes all witness and spentness information from the wallet. The
 * keystore is unmodified and decrypted note, nullifier, and conflict data are left
 * in place with the expectation that they will be overwritten and/or updated in
 * the rescan process.
 */
void ironwood_wallet_reset(IronwoodWalletPtr* wallet);

/**
 * Checkpoint the note commitment tree. This returns `false` and leaves the note
 * commitment tree unmodified if the block height specified is not the successor
 * to the last block height checkpointed.
 */
bool ironwood_wallet_checkpoint(
    IronwoodWalletPtr* wallet,
    uint32_t blockHeight);

/**
 * Returns whether or not the wallet has any checkpointed state to which it can rewind.
 * If so, `blockHeightRet` will be modified to contain the last block height at which a
 * checkpoint was created.
 */
bool ironwood_wallet_get_last_checkpoint(
    const IronwoodWalletPtr* wallet,
    uint32_t* blockHeightRet);

/**
 * Rewinds to the most recent checkpoint, and marks as unspent any notes previously
 * identified as having been spent by transactions in the latest block.
 *
 * The `blockHeight` argument provides the height to which the witness tree should be
 * rewound, such that after the rewind this height corresponds to the latest block
 * appended to the tree.
 *
 * Returns `true` if the rewind is successful, in which case `resultHeight` will contain
 * the height to which the tree has been rewound; otherwise, this returns `false` and
 * leaves `resultHeight` unmodified.
 */
bool ironwood_wallet_rewind(
    IronwoodWalletPtr* wallet,
    uint32_t blockHeight,
    uint32_t* resultHeight);

/**
 * Add the note commitment values for the specified bundle to the wallet's note
 * commitment tree, and mark any Ironwood notes that belong to the wallet so
 * that we can construct authentication paths to these notes in the future.
 *
 * This requires the block height and the index of the block within the
 * transaction in order to guarantee that note commitments are appended in the
 * correct order. Returns `false` if the provided bundle is not in the correct
 * position to have its note commitments appended to the note commitment tree.
 */
bool ironwood_wallet_append_bundle_commitments(
       IronwoodWalletPtr* wallet,
       const uint32_t block_height,
       const size_t block_tx_idx,
       const IronwoodBundlePtr* bundle
       );

bool clear_ironwood_note_positions_for_txid(
   IronwoodWalletPtr* wallet,
   const unsigned char txid[32]
);

bool create_ironwood_single_txid_positions(
   IronwoodWalletPtr* wallet,
   const uint32_t block_height,
   const unsigned char txid[32]
);

bool ironwood_wallet_append_single_commitment(
    IronwoodWalletPtr* wallet,
    const uint32_t block_height,
    const unsigned char txid[32],
    const size_t block_tx_idx,
    const size_t tx_output_idx,
    const ActionPtr* action,
    const bool is_mine
);

/**
 * Obtains the root of the wallet's Ironwood note commitment tree at the given
 * checkpoint depth, copying it to `root_ret` which must point to a 32-byte
 * array. As a consequence of how checkpoints are created by the `zcashd`
 * embedded wallet, a `checkpoint_depth` of `0` corresponds to the tree state
 * as of the block most recently appended to the chain, a depth of `1`
 * corresponds to the end of the previous block, and so forth.
 *
 * Returns `true` if it is possible to compute a valid note commitment tree
 * root at the given depth, otherwise `false`.
 */
bool ironwood_wallet_commitment_tree_root(
    const IronwoodWalletPtr* wallet,
    const size_t checkpoint_depth,
    unsigned char* root_ret);

/**
 * Run the garbage collection operation on the wallet's note commitment
 * tree.
 */
void ironwood_wallet_gc_note_commitment_tree(IronwoodWalletPtr* wallet);

/**
 * Write the wallet's note commitment tree to the provided stream.
 */
bool ironwood_wallet_write_note_commitment_tree(
    const IronwoodWalletPtr* wallet,
    void* stream,
    write_callback_t write_cb);

/**
 * Read a note commitment tree from the provided stream, and update the wallet's internal
 * note commitment tree state to equal the value that was read.
 */
bool ironwood_wallet_load_note_commitment_tree(
    IronwoodWalletPtr* wallet,
    void* stream,
    read_callback_t read_cb);

bool ironwood_wallet_unmark_transaction_notes(
    IronwoodWalletPtr* wallet,
    const unsigned char txid[32]
);

bool ironwood_is_note_tracked(
  IronwoodWalletPtr* wallet,
  const unsigned char txid[32],
  const size_t tx_output_idx,
  uint64_t *position_out
);

bool ironwood_wallet_get_path_for_note(
    IronwoodWalletPtr* wallet,
    const unsigned char txid[32],
    const size_t tx_output_idx,
    unsigned char *path_ret
);

bool get_ironwood_path_root_with_cm(
    const unsigned char *merkle_path,
    const unsigned char *cm,
    unsigned char *anchor_out
);

#ifdef __cplusplus
}
#endif

#endif // ZCASH_RUST_INCLUDE_RUST_IRONWOOD_WALLET_H
