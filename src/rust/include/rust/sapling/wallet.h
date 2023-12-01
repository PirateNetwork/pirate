// Copyright (c) 2021-2023 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef ZCASH_RUST_INCLUDE_RUST_SAPLING_WALLET_H
#define ZCASH_RUST_INCLUDE_RUST_SAPLING_WALLET_H

//#include "rust/sapling/keys.h"
//#include "rust/builder.h"
#include "rust/streams.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A type-safe pointer type for an Sapling wallet.
 */
struct SaplingWalletPtr;
typedef struct SaplingWalletPtr SaplingWalletPtr;

/// Pointer to an Sapling incremental merkle tree frontier
struct SaplingMerkleFrontierPtr;
typedef struct SaplingMerkleFrontierPtr SaplingMerkleFrontierPtr;

/**
 * Constructs a new empty Sapling wallet and return a pointer to it.
 * Memory is allocated by Rust and must be manually freed using
 * `sapling_wallet_free`.
 */
SaplingWalletPtr* sapling_wallet_new();

/**
 * Frees the memory associated with an Sapling wallet that was allocated
 * by Rust.
 */
void sapling_wallet_free(SaplingWalletPtr* wallet);

/**
 * Reset the state of the wallet to be suitable for rescan from the NU5 activation
 * height.  This removes all witness and spentness information from the wallet. The
 * keystore is unmodified and decrypted note, nullifier, and conflict data are left
 * in place with the expectation that they will be overwritten and/or updated in
 * the rescan process.
 */
void sapling_wallet_reset(SaplingWalletPtr* wallet);

/**
 * Checkpoint the note commitment tree. This returns `false` and leaves the note
 * commitment tree unmodified if the block height specified is not the successor
 * to the last block height checkpointed.
 */
bool sapling_wallet_checkpoint(
        SaplingWalletPtr* wallet,
        uint32_t blockHeight
        );

/**
 * Returns whether or not the wallet has any checkpointed state to which it can rewind.
 * If so, `blockHeightRet` will be modified to contain the last block height at which a
 * checkpoint was created.
 */
bool sapling_wallet_get_last_checkpoint(
        const SaplingWalletPtr* wallet,
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
bool sapling_wallet_rewind(
        SaplingWalletPtr* wallet,
        uint32_t blockHeight,
        uint32_t* resultHeight
        );

/**
 * Initialize the wallet's note commitment tree to the empty tree starting from the
 * specified Merkle frontier. This will return `false` and leave the wallet unmodified if
 * it would cause any checkpoint or witness state to be invalidated.
 */
bool sapling_wallet_init_from_frontier(
        SaplingWalletPtr* wallet,
        const SaplingMerkleFrontierPtr* frontier);

/**
 * A C struct used to transfer action_idx/IVK pairs back from Rust across the FFI
 * boundary. This must have the same in-memory representation as the `FFIActionIVK` type
 * in sapling_ffi/wallet.rs.
 *
 * Values of the `ivk` pointer must be freed manually; the best way to do this is to
 * wrap this pointer in an `SaplingIncomingViewingKey` which handles deallocation
 * in the object destructor.
 */
// struct RawSaplingActionIVK {
//     uint64_t actionIdx;
//     SaplingIncomingViewingKeyPtr* ivk;
// };
// static_assert(
//     sizeof(RawSaplingActionIVK) == 16,
//     "RawSaplingActionIVK struct should have exactly a 128-bit in-memory representation.");
// static_assert(alignof(RawSaplingActionIVK) == 8, "RawSaplingActionIVK struct alignment is not 64 bits.");
//
// typedef void (*push_action_ivk_callback_t)(void* rec, const RawSaplingActionIVK actionIvk);
//
// typedef void (*push_spend_action_idx_callback_t)(void* rec, uint32_t actionIdx);

/**
 * Searches the provided bundle for notes that are visible to the specified wallet's
 * incoming viewing keys, and adds those notes to the wallet.  For each note decryptable
 * by one of the wallet's keys, this method will insert a `RawSaplingActionIVK` value into
 * the provided `callbackReceiver` referent using the `push_cb` callback. Note that
 * this callback can perform transformations on the provided RawSaplingActionIVK in this
 * process.  For each action spending one of the wallet's notes, this method will pass
 * a `uint32_t` action index corresponding to that action to the `callbackReceiver` referent;
 * using the specified callback; usually, this will push the value into a result vector owned
 * by the caller.
 *
 * The provided bundle must be a component of the transaction from which `txid` was
 * derived.
 *
 * Returns `true` if the bundle is involved with the wallet; i.e. if it contains
 * notes spendable by the wallet, or spends any of the wallet's notes.
 */
// bool sapling_wallet_add_notes_from_bundle(
//         SaplingWalletPtr* wallet,
//         const unsigned char txid[32],
//         const SaplingBundlePtr* bundle,
//         void* callbackReceiver,
//         push_action_ivk_callback_t push_cb,
//         push_spend_action_idx_callback_t spend_cb
//         );

/**
 * Decrypts a selection of notes from the bundle with specified incoming viewing
 * keys, and adds those notes to the wallet.
 *
 * The provided bundle must be a component of the transaction from which
 * `txid` was derived.
 */
// bool sapling_wallet_load_bundle(
//         SaplingWalletPtr* wallet,
//         const unsigned char txid[32],
//         const SaplingBundlePtr* bundle,
//         const RawSaplingActionIVK* actionIvks,
//         size_t actionIvksLen,
//         const uint32_t* actionsSpendingWalletNotes,
//         size_t actionsSpendingWalletNotesLen
//         );

/**
 * Add the note commitment values for the specified bundle to the wallet's note
 * commitment tree, and mark any Sapling notes that belong to the wallet so
 * that we can construct authentication paths to these notes in the future.
 *
 * This requires the block height and the index of the block within the
 * transaction in order to guarantee that note commitments are appended in the
 * correct order. Returns `false` if the provided bundle is not in the correct
 * position to have its note commitments appended to the note commitment tree.
 */
bool sapling_wallet_append_bundle_commitments(
        SaplingWalletPtr* wallet,
        const uint32_t block_height,
        const size_t block_tx_idx,
        const SaplingBundlePtr* bundle
        );

bool sapling_wallet_append_single_commitment(
    SaplingWalletPtr* wallet,
    const uint32_t block_height,
    const size_t block_tx_idx,
    const size_t tx_output_idx,
    const OutputPtr* output,
    const bool is_mine
);

/**
 * Obtains the root of the wallet's Sapling note commitment tree at the given
 * checkpoint depth, copying it to `root_ret` which must point to a 32-byte
 * array. As a consequence of how checkpoints are created by the `zcashd`
 * embedded wallet, a `checkpoint_depth` of `0` corresponds to the tree state
 * as of the block most recently appended to the chain, a depth of `1`
 * corresponds to the end of the previous block, and so forth.
 *
 * Returns `true` if it is possible to compute a valid note commitment tree
 * root at the given depth, otherwise `false`.
 */
bool sapling_wallet_commitment_tree_root(
        const SaplingWalletPtr* wallet,
        const size_t checkpoint_depth,
        unsigned char* root_ret);

/**
 * Returns whether the specified transaction involves any Sapling notes that belong to
 * this wallet.
 */
// bool sapling_wallet_tx_involves_my_notes(
//         const SaplingWalletPtr* wallet,
//         const unsigned char txid[32]);

/**
 * Add the specified spending key to the wallet's key store.  This will also compute and
 * add the associated full and incoming viewing keys.
 */
// void sapling_wallet_add_spending_key(
//         SaplingWalletPtr* wallet,
//         const SaplingSpendingKeyPtr* sk);

/**
 * Add the specified full viewing key to the wallet's key store.  This will also compute
 * and add the associated incoming viewing key.
 */
// void sapling_wallet_add_full_viewing_key(
//         SaplingWalletPtr* wallet,
//         const SaplingFullViewingKeyPtr* fvk);

/**
 * Add the specified raw address to the wallet's key store, associated with the incoming
 * viewing key from which that address was derived.
 */
// bool sapling_wallet_add_raw_address(
//         SaplingWalletPtr* wallet,
//         const SaplingRawAddressPtr* addr,
//         const SaplingIncomingViewingKeyPtr* ivk);

/**
 * Returns a pointer to the Sapling spending key corresponding to the specified raw
 * address, if it is known to the wallet, or `nullptr` otherwise.
 *
 * Memory is allocated by Rust and must be manually freed using
 * `sapling_spending_key_free`.
 */
// SaplingSpendingKeyPtr* sapling_wallet_get_spending_key_for_address(
//         const SaplingWalletPtr* wallet,
//         const SaplingRawAddressPtr* addr);

/**
 * Returns a pointer to the Sapling incoming viewing key corresponding to the specified
 * raw address, if it is known to the wallet, or `nullptr` otherwise.
 *
 * Memory is allocated by Rust and must be manually freed using
 * `sapling_incoming_viewing_key_free`.
 */
// SaplingIncomingViewingKeyPtr* sapling_wallet_get_ivk_for_address(
//         const SaplingWalletPtr* wallet,
//         const SaplingRawAddressPtr* addr);

/**
 * A C struct used to transfer note metadata information across the Rust FFI boundary.
 * This must have the same in-memory representation as the `FFINoteMetadata` type in
 * sapling_ffi/wallet.rs.
 */
// struct RawSaplingNoteMetadata {
//     unsigned char txid[32];
//     uint32_t actionIdx;
//     SaplingRawAddressPtr* addr;
//     CAmount noteValue;
//     unsigned char memo[512];
// };

// typedef void (*push_note_callback_t)(void* resultVector, const RawSaplingNoteMetadata noteMeta);

/**
 * Finds notes that belong to the wallet that were sent to addresses derived from the
 * specified incoming viewing key, subject to the specified flags, and uses the provided
 * callback to push RawSaplingNoteMetadata values corresponding to those notes on to the
 * provided result vector.  Note that the push_cb callback can perform any necessary
 * conversion from a RawSaplingNoteMetadata value in addition to modifying the provided
 * result vector.
 *
 * If `ivk` is null, all notes belonging to the wallet will be returned.  The
 * `RawSaplingNoteMetadata::addr` pointers for values provided to the callback must be
 * manually freed by the caller.
 */
// void sapling_wallet_get_filtered_notes(
//         const SaplingWalletPtr* wallet,
//         const SaplingIncomingViewingKeyPtr* ivk,
//         bool ignoreMined,
//         bool requireSpendingKey,
//         void* resultVector,
//         push_note_callback_t push_cb
//         );

/**
 * A C struct used to transfer Sapling action spend information across the FFI boundary.
 * This must have the same in-memory representation as the `FFIActionSpend` type in
 * sapling_ffi/wallet.rs.
 */
// struct RawSaplingActionSpend {
//     uint32_t spendActionIdx;
//     unsigned char outpointTxId[32];
//     uint32_t outpointActionIdx;
//     SaplingRawAddressPtr* receivedAt;
//     CAmount noteValue;
// };

/**
 * A C struct used to transfer Sapling action output information across the FFI boundary.
 * This must have the same in-memory representation as the `FFIActionOutput` type in
 * sapling_ffi/wallet.rs.
 */
// struct RawSaplingActionOutput {
//     uint32_t outputActionIdx;
//     SaplingRawAddressPtr* recipient;
//     CAmount noteValue;
//     unsigned char memo[512];
//     bool isOutgoing;
// };
//
// typedef void (*push_spend_t)(void* callbackReceiver, const RawSaplingActionSpend data);
//
// typedef void (*push_output_t)(void* callbackReceiver, const RawSaplingActionOutput data);

/**
 * Trial-decrypts the specified Sapling bundle, and uses the provided callbacks to pass
 * `RawSaplingActionSpend` and `RawSaplingActionOutput` values (corresponding to the
 * actions of that bundle) to the provided result receiver.
 *
 * Note that the callbacks can perform any necessary conversion from a
 * `RawSaplingActionSpend` or `RawSaplingActionOutput` value in addition to modifying the
 * provided result receiver.
 *
 * `raw_ovks` must be a pointer to an array of `unsigned char[32]`.
 *
 * The `recipient` pointer for each `RawSaplingActionOutput` value, and the `receivedAt`
 * pointer for each `RawSaplingActionSpend` value, must be freed using
 * `sapling_address_free`.
 */
// bool sapling_wallet_get_txdata(
//         const SaplingWalletPtr* wallet,
//         const SaplingBundlePtr* bundle,
//         const unsigned char* raw_ovks,
//         size_t raw_ovks_len,
//         void* callbackReceiver,
//         push_spend_t push_spend_cb,
//         push_output_t push_output_cb
//         );
//
// typedef void (*push_txid_callback_t)(void* resultVector, unsigned char txid[32]);

/**
 * Returns a vector of transaction IDs for transactions that have been observed as
 * spending the given outpoint (transaction ID and action index) by using the `push_cb`
 * callback to push transaction IDs onto the provided result vector.
 */
// void sapling_wallet_get_potential_spends(
//         const SaplingWalletPtr* wallet,
//         const unsigned char txid[32],
//         const uint32_t action_idx,
//         void* resultVector,
//         push_txid_callback_t push_cb
//         );

/**
 * Returns a vector of transaction IDs for transactions that have been observed as
 * spending the given nullifier by using the `push_cb` callback to push transaction
 * IDs onto the provided result vector.
 */
// void sapling_wallet_get_potential_spends_from_nullifier(
//         const SaplingWalletPtr* wallet,
//         const unsigned char *nullifier,
//         void* resultVector,
//         push_txid_callback_t push_cb
//         );

/**
 * Fetches the information needed to spend the wallet note at the given
 * outpoint, as of the state of the note commitment tree at the given
 * checkpoint depth. As a consequence of how checkpoints are created by the
 * `zcashd` embedded wallet, a `checkpoint_depth` of `0` corresponds to the
 * tree state as of the block most recently appended to the chain, a depth of
 * `1` corresponds to the end of the previous block, and so forth.
 *
 * Returns `null` if the outpoint is not known to the wallet, or the checkpoint
 * depth exceeds the maximum number of checkpoints retained by the wallet.
 */
// SaplingSpendInfoPtr* sapling_wallet_get_spend_info(
//         const SaplingWalletPtr* wallet,
//         const unsigned char txid[32],
//         uint32_t action_idx,
//         size_t checkpoint_depth);

/**
 * Run the garbage collection operation on the wallet's note commitment
 * tree.
 */
void sapling_wallet_gc_note_commitment_tree(SaplingWalletPtr* wallet);

/**
 * Write the wallet's note commitment tree to the provided stream.
 */
bool sapling_wallet_write_note_commitment_tree(
        const SaplingWalletPtr* wallet,
        void* stream,
        write_callback_t write_cb);

/**
 * Read a note commitment tree from the provided stream, and update the wallet's internal
 * note commitment tree state to equal the value that was read.
 */
bool sapling_wallet_load_note_commitment_tree(
        SaplingWalletPtr* wallet,
        void* stream,
        read_callback_t read_cb);

/**
 * Returns whether the Sapling wallet's note commitment tree contains witness information
 * for all unspent notes.
 */
// bool sapling_wallet_unspent_notes_are_spendable(
//         const SaplingWalletPtr* wallet);

#ifdef __cplusplus
}
#endif

#endif // ZCASH_RUST_INCLUDE_RUST_SAPLING_WALLET_H
