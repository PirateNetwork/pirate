use libc::c_uchar;
use zcash_primitives::transaction::components::GROTH_PROOF_SIZE;
use zcash_proofs::sprout as old_sprout;

use super::{prove, verify};

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
    let proof = prove(
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
    );

    unsafe { *proof_out = proof };
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
    verify(
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
    )
}
