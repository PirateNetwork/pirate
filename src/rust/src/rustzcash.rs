#![allow(static_mut_refs)]
//! FFI between the C++ zcashd codebase and the Rust Zcash crates.
//!
//! This is internal to zcashd and is not an officially-supported API.

// Catch documentation errors caused by code changes.
#![deny(broken_intra_doc_links)]
// Clippy has a default-deny lint to prevent dereferencing raw pointer arguments
// in a non-unsafe function. However, declaring a function as unsafe has the
// side-effect that the entire function body is treated as an unsafe {} block,
// and rustc will not enforce full safety checks on the parts of the function
// that would otherwise be safe.
//
// The functions in this crate are all for FFI usage, so it's obvious to the
// caller (which is only ever zcashd) that the arguments must satisfy the
// necessary assumptions. We therefore ignore this lint to retain the benefit of
// explicitly annotating the parts of each function that must themselves satisfy
// assumptions of underlying code.
//
// See https://github.com/rust-lang/rfcs/pull/2585 for more background.
#![allow(clippy::not_unsafe_ptr_arg_deref)]


use bellman::groth16::{PreparedVerifyingKey, prepare_verifying_key, VerifyingKey};
use bls12_381::Bls12;
use tracing::info;
use libc::{c_uchar, size_t};
use std::path::{Path, PathBuf};
use std::slice;
use std::sync::Once;
use subtle::CtOption;

#[cfg(not(target_os = "windows"))]
use std::ffi::OsStr;
#[cfg(not(target_os = "windows"))]
use std::os::unix::ffi::OsStrExt;

#[cfg(target_os = "windows")]
use std::ffi::OsString;
#[cfg(target_os = "windows")]
use std::os::windows::ffi::OsStringExt;

use sapling_crypto::circuit::{OutputParameters, OutputVerifyingKey, SpendParameters, SpendVerifyingKey};

use sapling_crypto::merkle_hash;

use sapling_crypto::zip32 as sapling_zip32;
use ::zip32::{ChildIndex, DiversifierIndex};

#[path = "crypto/blake2b.rs"]
mod blake2b;
mod streams_ffi;
mod tracing_ffi;

mod bridge;
mod seed_bridge;

mod builder_ffi;
mod bundlecache;
mod history;
mod incremental_merkle_tree;
mod merkle_frontier;
#[path = "orchard_protocol/orchard_protocol.rs"]
mod orchard_protocol;
#[path = "ironwood_protocol/ironwood_protocol.rs"]
mod ironwood_protocol;
#[path = "sapling_protocol/sapling_protocol.rs"]
mod sapling_protocol;
#[path = "sprout_protocol/sprout.rs"]
mod sprout;
mod seed;
mod transparent;
mod streams;
mod transaction_ffi;

mod test_harness_ffi;

#[cfg(test)]
mod tests;

static PROOF_PARAMETERS_LOADED: Once = Once::new();
static mut SAPLING_SPEND_VK: Option<SpendVerifyingKey> = None;
static mut SAPLING_OUTPUT_VK: Option<OutputVerifyingKey> = None;
static mut SPROUT_GROTH16_VK: Option<PreparedVerifyingKey<Bls12>> = None;

static mut SAPLING_SPEND_PARAMS: Option<SpendParameters> = None;
static mut SAPLING_OUTPUT_PARAMS: Option<OutputParameters> = None;
static mut SPROUT_GROTH16_PARAMS_PATH: Option<PathBuf> = None;

static mut ORCHARD_PK: Option<orchard::circuit::ProvingKey> = None;
static mut ORCHARD_VK: Option<orchard::circuit::VerifyingKey> = None;

// SCAFFOLDING ONLY: loaded eagerly alongside ORCHARD_PK/VK for consistency, but nothing
// in this tree can construct or validate an Ironwood bundle yet (no v6 transaction
// support). The Ironwood pool requires PostNu6_3, a distinct circuit from Orchard's
// current FixedPostNu6_2 (see OrchardCircuitVersion::supports_cross_address_restriction),
// so it needs its own key pair rather than reusing ORCHARD_PK/VK.
static mut IRONWOOD_PK: Option<orchard::circuit::ProvingKey> = None;
static mut IRONWOOD_VK: Option<orchard::circuit::VerifyingKey> = None;

/// Converts CtOption<T> into Option<T>
fn de_ct<T>(ct: CtOption<T>) -> Option<T> {
    if ct.is_some().into() {
        Some(ct.unwrap())
    } else {
        None
    }
}

/// Loads the zk-SNARK parameters into memory and saves paths as necessary.
/// Only called once.
///
/// If `load_proving_keys` is `false`, the proving keys will not be loaded, making it
/// impossible to create proofs. This flag is for the Boost test suite, which never
/// creates shielded transactions, but exercises code that requires the verifying keys to
/// be present even if there are no shielded components to verify.
#[no_mangle]
pub extern "C" fn librustzcash_init_zksnark_params(
    #[cfg(not(target_os = "windows"))] sprout_path: *const u8,
    #[cfg(target_os = "windows")] sprout_path: *const u16,
    sprout_path_len: usize,
    load_proving_keys: bool,
) {
    PROOF_PARAMETERS_LOADED.call_once(|| {
        #[cfg(not(target_os = "windows"))]
        let sprout_path = if sprout_path.is_null() {
            None
        } else {
            Some(OsStr::from_bytes(unsafe {
                slice::from_raw_parts(sprout_path, sprout_path_len)
            }))
        };

        #[cfg(target_os = "windows")]
        let sprout_path = if sprout_path.is_null() {
            None
        } else {
            Some(OsString::from_wide(unsafe {
                slice::from_raw_parts(sprout_path, sprout_path_len)
            }))
        };

        let sprout_path = sprout_path.as_ref().map(Path::new);

        let sprout_vk = {
            let sprout_vk_bytes = include_bytes!("sprout_protocol/sprout-groth16.vk");
            let vk = VerifyingKey::<Bls12>::read(&sprout_vk_bytes[..])
                .expect("should be able to parse Sprout verification key");
            prepare_verifying_key(&vk)
        };

        // Load params
        let (sapling_spend_params, sapling_output_params) = {
            let (spend_buf, output_buf) = wagyu_zcash_parameters::load_sapling_parameters();
            let spend_params = SpendParameters::read(&spend_buf[..], false)
                .expect("couldn't deserialize Sapling spend parameters");
            let output_params = OutputParameters::read(&output_buf[..], false)
                .expect("couldn't deserialize Sapling output parameters");
            (spend_params, output_params)
        };

        let sapling_spend_vk = sapling_spend_params.verifying_key();
        let sapling_output_vk = sapling_output_params.verifying_key();

        // Generate Orchard parameters.
        info!(target: "main", "Loading Orchard parameters");
        let orchard_pk = load_proving_keys.then(|| {
            orchard::circuit::ProvingKey::build(orchard::circuit::OrchardCircuitVersion::FixedPostNu6_2)
        });
        let orchard_vk =
            orchard::circuit::VerifyingKey::build(orchard::circuit::OrchardCircuitVersion::FixedPostNu6_2);

        // Generate Ironwood parameters. SCAFFOLDING ONLY: see IRONWOOD_PK/VK above.
        info!(target: "main", "Loading Ironwood parameters");
        let ironwood_pk = load_proving_keys.then(|| {
            orchard::circuit::ProvingKey::build(orchard::circuit::OrchardCircuitVersion::PostNu6_3)
        });
        let ironwood_vk =
            orchard::circuit::VerifyingKey::build(orchard::circuit::OrchardCircuitVersion::PostNu6_3);

        // Caller is responsible for calling this function once, so
        // these global mutations are safe.
        unsafe {
            SAPLING_SPEND_PARAMS = load_proving_keys.then_some(sapling_spend_params);
            SAPLING_OUTPUT_PARAMS = load_proving_keys.then_some(sapling_output_params);
            SPROUT_GROTH16_PARAMS_PATH = sprout_path.map(|p| p.to_owned());

            SAPLING_SPEND_VK = Some(sapling_spend_vk);
            SAPLING_OUTPUT_VK = Some(sapling_output_vk);
            SPROUT_GROTH16_VK = Some(sprout_vk);

            ORCHARD_PK = orchard_pk;
            ORCHARD_VK = Some(orchard_vk);

            IRONWOOD_PK = ironwood_pk;
            IRONWOOD_VK = Some(ironwood_vk);
        }
    });
}

/// Writes the "uncommitted" note value for empty leaves of the Merkle tree.
///
/// `result` must be a valid pointer to 32 bytes which will be written.
#[no_mangle]
pub extern "C" fn librustzcash_tree_uncommitted(result: *mut [c_uchar; 32]) {
    unsafe { *result = sapling_protocol::spec::tree_uncommitted(); }
}

/// Computes a merkle tree hash for a given depth. The `depth` parameter should
/// not be larger than 62.
///
/// `a` and `b` each must be of length 32, and must each be scalars of BLS12-381.
///
/// The result of the merkle tree hash is placed in `result`, which must also be
/// of length 32.
#[no_mangle]
pub extern "C" fn librustzcash_merkle_hash(
    depth: size_t,
    a: *const [c_uchar; 32],
    b: *const [c_uchar; 32],
    result: *mut [c_uchar; 32],
) {
    // Should be okay, because caller is responsible for ensuring
    // the pointers are valid pointers to 32 bytes.
    let tmp = merkle_hash(depth, unsafe { &*a }, unsafe { &*b });

    // Should be okay, caller is responsible for ensuring the pointer
    // is a valid pointer to 32 bytes that can be mutated.
    let result = unsafe { &mut *result };
    *result = tmp;
}

/// Derive the master ExtendedSpendingKey from a seed.
#[no_mangle]
pub extern "C" fn librustzcash_zip32_xsk_master(
    seed: *const c_uchar,
    seedlen: size_t,
    xsk_master: *mut [c_uchar; 169],
) {
    let seed = unsafe { std::slice::from_raw_parts(seed, seedlen) };

    let xsk = sapling_zip32::ExtendedSpendingKey::master(seed);

    xsk.write(&mut (unsafe { &mut *xsk_master })[..])
        .expect("should be able to serialize an ExtendedSpendingKey");
}

/// Derive a child ExtendedSpendingKey from a parent.
#[no_mangle]
pub extern "C" fn librustzcash_zip32_xsk_derive(
    xsk_parent: *const [c_uchar; 169],
    i: u32,
    xsk_i: *mut [c_uchar; 169],
) {
    let xsk_parent = sapling_zip32::ExtendedSpendingKey::read(&unsafe { *xsk_parent }[..])
        .expect("valid ExtendedSpendingKey");
    let i = ChildIndex::from_index(i).expect("expected hardened child index");

    let xsk = xsk_parent.derive_child(i);

    xsk.write(&mut (unsafe { &mut *xsk_i })[..])
        .expect("should be able to serialize an ExtendedSpendingKey");
}

/// Derive a child ExtendedFullViewingKey from a parent.
#[no_mangle]
pub extern "C" fn librustzcash_zip32_xfvk_derive(
    xfvk_parent: *const [c_uchar; 169],
    i: u32,
    _xfvk_i: *mut [c_uchar; 169],
) -> bool {
    let xfvk_parent = sapling_zip32::ExtendedFullViewingKey::read(&unsafe { *xfvk_parent }[..])
        .expect("valid ExtendedFullViewingKey");
    let _ = xfvk_parent;
    let _ = i;
    // XFVK child derivation is not supported in sapling-crypto 0.7
    false
}

/// Derive a PaymentAddress from an ExtendedFullViewingKey.
#[no_mangle]
pub extern "C" fn librustzcash_zip32_xfvk_address(
    xfvk: *const [c_uchar; 169],
    j: *const [c_uchar; 11],
    j_ret: *mut [c_uchar; 11],
    addr_ret: *mut [c_uchar; 43],
) -> bool {
    let xfvk = match sapling_zip32::ExtendedFullViewingKey::read(&unsafe { *xfvk }[..]) {
        Ok(xfvk) => xfvk,
        Err(_) => return false,
    };
    let j = DiversifierIndex::from(unsafe { *j });

    let addr = match xfvk.find_address(j) {
        Some(addr) => addr,
        None => return false,
    };

    let j_ret = unsafe { &mut *j_ret };
    let addr_ret = unsafe { &mut *addr_ret };

    j_ret.copy_from_slice(addr.0.as_bytes());
    addr_ret.copy_from_slice(&addr.1.to_bytes());

    true
}

// Seed generation and mnemonic phrase functions have been moved to seed.rs
// and are now exposed via the hd_seed bridge namespace.
