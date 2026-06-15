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


use bellman::groth16::{PreparedVerifyingKey, prepare_verifying_key, VerifyingKey, Parameters};
use bls12_381::Bls12;
use tracing::info;
use group::GroupEncoding;
use libc::{c_uchar, size_t};
use std::fs::File;
use std::io::BufReader;
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

use zcash_primitives::{
    merkle_tree::HashSer,
    transaction::components::GROTH_PROOF_SIZE,
};
use sapling_crypto::{
    merkle_hash,
    Diversifier, Node,
};
use zcash_proofs::sprout as old_sprout;

use incrementalmerkletree::Hashable;
use sapling_crypto::zip32 as sapling_zip32;
use ::zip32::{ChildIndex, DiversifierIndex};

mod blake2b;
mod ed25519;
mod streams_ffi;
mod tracing_ffi;

mod bridge;

mod builder_ffi;
mod bundlecache;
mod history;
mod incremental_merkle_tree;
mod merkle_frontier;
mod orchard_actions;
mod orchard_bundle;
mod orchard_ffi;
mod orchard_keys;
mod sapling_keys;
mod seed;
mod transparent;
mod sapling;
mod sprout;
mod streams;
mod transaction_ffi;
mod sapling_wallet;
mod orchard_wallet;

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
            let sprout_vk_bytes = include_bytes!("sprout-groth16.vk");
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
        let orchard_pk = load_proving_keys.then(orchard::circuit::ProvingKey::build);
        let orchard_vk = orchard::circuit::VerifyingKey::build();

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
        }
    });
}

/// Writes the "uncommitted" note value for empty leaves of the Merkle tree.
///
/// `result` must be a valid pointer to 32 bytes which will be written.
#[no_mangle]
pub extern "C" fn librustzcash_tree_uncommitted(result: *mut [c_uchar; 32]) {
    // Should be okay, caller is responsible for ensuring the pointer
    // is a valid pointer to 32 bytes that can be mutated.
    let result = unsafe { &mut *result };
    Node::empty_leaf()
        .write(&mut result[..])
        .expect("Sapling leaves are 32 bytes");
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

#[no_mangle]
pub extern "C" fn librustzcash_ivk_to_pkd(
    ivk: *const [c_uchar; 32],
    diversifier: *const [c_uchar; 11],
    result: *mut [c_uchar; 32],
) -> bool {
    let ivk = de_ct(jubjub::Scalar::from_bytes(unsafe { &*ivk }));
    let diversifier = Diversifier(unsafe { *diversifier });
    if let (Some(ivk), Some(g_d)) = (ivk, diversifier.g_d()) {
        let pk_d = g_d * ivk;

        let result = unsafe { &mut *result };

        *result = pk_d.to_bytes();

        true
    } else {
        false
    }
}

/// Sprout JoinSplit proof generation.
#[no_mangle]
pub extern "C" fn librustzcash_sprout_prove(
    proof_out: *mut [c_uchar; GROTH_PROOF_SIZE],

    phi: *const [c_uchar; 32],
    rt: *const [c_uchar; 32],
    h_sig: *const [c_uchar; 32],

    // First input
    in_sk1: *const [c_uchar; 32],
    in_value1: u64,
    in_rho1: *const [c_uchar; 32],
    in_r1: *const [c_uchar; 32],
    in_auth1: *const [c_uchar; old_sprout::WITNESS_PATH_SIZE],

    // Second input
    in_sk2: *const [c_uchar; 32],
    in_value2: u64,
    in_rho2: *const [c_uchar; 32],
    in_r2: *const [c_uchar; 32],
    in_auth2: *const [c_uchar; old_sprout::WITNESS_PATH_SIZE],

    // First output
    out_pk1: *const [c_uchar; 32],
    out_value1: u64,
    out_r1: *const [c_uchar; 32],

    // Second output
    out_pk2: *const [c_uchar; 32],
    out_value2: u64,
    out_r2: *const [c_uchar; 32],

    // Public value
    vpub_old: u64,
    vpub_new: u64,
) {
    // Load parameters from disk
    let sprout_fs = File::open(
        unsafe { &SPROUT_GROTH16_PARAMS_PATH }
            .as_ref()
            .expect("parameters should have been initialized"),
    )
    .expect("couldn't load Sprout groth16 parameters file");

    let mut sprout_fs = BufReader::with_capacity(1024 * 1024, sprout_fs);

    let params = Parameters::read(&mut sprout_fs, false)
        .expect("couldn't deserialize Sprout JoinSplit parameters file");

    drop(sprout_fs);

    let proof = old_sprout::create_proof(
        unsafe { *phi },
        unsafe { *rt },
        unsafe { *h_sig },
        unsafe { *in_sk1 },
        in_value1,
        unsafe { *in_rho1 },
        unsafe { *in_r1 },
        unsafe { &*in_auth1 },
        unsafe { *in_sk2 },
        in_value2,
        unsafe { *in_rho2 },
        unsafe { *in_r2 },
        unsafe { &*in_auth2 },
        unsafe { *out_pk1 },
        out_value1,
        unsafe { *out_r1 },
        unsafe { *out_pk2 },
        out_value2,
        unsafe { *out_r2 },
        vpub_old,
        vpub_new,
        &params,
    );

    proof
        .write(&mut (unsafe { &mut *proof_out })[..])
        .expect("should be able to serialize a proof");
}

/// Sprout JoinSplit proof verification.
#[no_mangle]
pub extern "C" fn librustzcash_sprout_verify(
    proof: *const [c_uchar; GROTH_PROOF_SIZE],
    rt: *const [c_uchar; 32],
    h_sig: *const [c_uchar; 32],
    mac1: *const [c_uchar; 32],
    mac2: *const [c_uchar; 32],
    nf1: *const [c_uchar; 32],
    nf2: *const [c_uchar; 32],
    cm1: *const [c_uchar; 32],
    cm2: *const [c_uchar; 32],
    vpub_old: u64,
    vpub_new: u64,
) -> bool {
    old_sprout::verify_proof(
        unsafe { &*proof },
        unsafe { &*rt },
        unsafe { &*h_sig },
        unsafe { &*mac1 },
        unsafe { &*mac2 },
        unsafe { &*nf1 },
        unsafe { &*nf2 },
        unsafe { &*cm1 },
        unsafe { &*cm2 },
        vpub_old,
        vpub_new,
        unsafe { SPROUT_GROTH16_VK.as_ref() }.expect("parameters should have been initialized"),
    )
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
