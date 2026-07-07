// Copyright (c) 2022-2023 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef ZCASH_RUST_INCLUDE_RUST_BUILDER_H
#define ZCASH_RUST_INCLUDE_RUST_BUILDER_H

#include "rust/pointers.h"
#include "rust/transaction.h"

#ifdef __cplusplus
extern "C" {
#endif

/// A type-safe pointer to a Rust-allocated struct containing the information
/// needed to spend an Ironwood note.
struct IronwoodSpendInfoPtr;
typedef struct IronwoodSpendInfoPtr IronwoodSpendInfoPtr;

/// Pointer to Rust-allocated Ironwood bundle builder.
struct IronwoodBuilderPtr;
typedef struct IronwoodBuilderPtr IronwoodBuilderPtr;

/// Pointer to Rust-allocated Ironwood bundle without proofs
/// or authorizing data.
struct IronwoodUnauthorizedBundlePtr;
typedef struct IronwoodUnauthorizedBundlePtr IronwoodUnauthorizedBundlePtr;

/// Frees the memory associated with an Ironwood spend info struct that was
/// allocated by Rust.
void ironwood_spend_info_free(IronwoodSpendInfoPtr* ptr);

/// Construct a new Ironwood transaction builder.
///
/// If `anchor` is `null`, the root of the empty Ironwood commitment tree is used.
IronwoodBuilderPtr* ironwood_builder_new(
    bool spends_enabled,
    bool outputs_enabled,
    const unsigned char* anchor);

/// Frees an Ironwood builder returned from `ironwood_builder_new`.
void ironwood_builder_free(IronwoodBuilderPtr* ptr);

/// Adds a note to be spent in this bundle.
///
/// Returns `false` if the Merkle path in `spend_info` does not have the
/// required anchor.
///
/// `spend_info` is always freed by this method, whether or not it succeeds.
bool ironwood_builder_add_spend(
    IronwoodBuilderPtr* ptr,
    const unsigned char* fvk_bytes,
    const ActionPtr* ironwood_action,
    const unsigned char *merkle_path);

/// Adds a note to be spent in this bundle, building the note from parts.
///
/// Returns `false` if the Merkle path in `spend_info` does not have the
/// required anchor.
///
/// `spend_info` is always freed by this method, whether or not it succeeds.
bool ironwood_builder_add_spend_from_parts(
    IronwoodBuilderPtr* ptr,
    const unsigned char* fvk_bytes,
    const unsigned char* note_address_bytes,
    uint64_t value,
    const unsigned char* rho_bytes,
    const unsigned char* rseed_bytes,
    const unsigned char *merkle_path);

/// Adds an address which will receive funds in this bundle.
///
/// `ovk` is a pointer to the outgoing viewing key to make this recipient recoverable by,
/// or `null` to make the recipient unrecoverable by the sender.
///
/// `memo` is a pointer to the 512-byte memo field encoding, or `null` for "no memo".
bool ironwood_builder_add_recipient(
    IronwoodBuilderPtr* ptr,
    const unsigned char* ovk,
    const unsigned char* recipient,
    uint64_t value,
    const unsigned char* memo);

/// Builds a bundle containing the given spent notes and recipients.
///
/// Returns `null` if an error occurs.
///
/// `builder` is always freed by this method.
IronwoodUnauthorizedBundlePtr* ironwood_builder_build(IronwoodBuilderPtr* builder);

/// Frees an Ironwood bundle returned from `ironwood_bundle_build`.
void ironwood_unauthorized_bundle_free(IronwoodUnauthorizedBundlePtr* bundle);

/// Adds proofs and signatures to the bundle.
///
/// Returns `null` if an error occurs.
///
/// `bundle` is always freed by this method.
IronwoodBundlePtr* ironwood_unauthorized_bundle_prove_and_sign(
    IronwoodUnauthorizedBundlePtr* bundle,
    const unsigned char* skbytes,
    size_t keycount,
    const unsigned char* sighash);

#ifdef __cplusplus
}
#endif

#endif // ZCASH_RUST_INCLUDE_RUST_BUILDER_H
