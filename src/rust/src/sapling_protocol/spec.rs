use incrementalmerkletree::Hashable;
use zcash_primitives::merkle_tree::HashSer;
use sapling_crypto::{
    merkle_hash,
    Node,
};

#[cxx::bridge]
mod ffi {
    #[namespace = "sapling::spec"]
    extern "Rust" {
        fn tree_uncommitted() -> [u8; 32];
        fn merkle_hash(depth: usize, lhs: &[u8; 32], rhs: &[u8; 32]) -> [u8; 32];
    }
}

/// Writes the "uncommitted" note value for empty leaves of the Merkle tree.
///
/// `result` must be a valid pointer to 32 bytes which will be written.
pub(crate) fn tree_uncommitted() -> [u8; 32] {
    // Should be okay, caller is responsible for ensuring the pointer
    // is a valid pointer to 32 bytes that can be mutated.
    let mut result = [0; 32];
    Node::empty_leaf()
        .write(&mut result[..])
        .expect("Sapling leaves are 32 bytes");
    result
}










