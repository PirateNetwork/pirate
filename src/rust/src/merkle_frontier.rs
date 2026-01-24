use core::mem::size_of_val;

use incrementalmerkletree::{
    frontier::{CommitmentTree, Frontier},
    Hashable, Level,
};
use orchard::{
    tree::MerkleHashOrchard,
    note::ExtractedNoteCommitment as OrchardExtractedNoteCommitment,
};

use zcash_primitives::{
    merkle_tree::{read_frontier_v1, write_commitment_tree, write_frontier_v1, HashSer},
    sapling::{NOTE_COMMITMENT_TREE_DEPTH, Node, note::ExtractedNoteCommitment as SaplingExtractedNoteCommitment},
};

// use crate::{bridge::ffi, orchard_bundle, streams::CppStream, wallet::Wallet};
use crate::{bridge::ffi, sapling::Bundle as SaplingBundle, orchard_bundle, streams::CppStream, orchard_wallet::Wallet as OrchardWalletInternal, sapling_wallet::Wallet as SaplingWalletInternal};

use crate::de_ct;

// This is also defined in `IncrementalMerkleTree.hpp`
pub const TRACKED_SUBTREE_HEIGHT: u8 = 16;

type Inner<H> = Frontier<H, NOTE_COMMITMENT_TREE_DEPTH>;

/// An incremental Merkle frontier.
#[derive(Clone)]
pub(crate) struct MerkleFrontier<H>(Inner<H>);

impl<H: Copy + Hashable + HashSer> MerkleFrontier<H> {
    /// Returns a copy of the value.
    pub(crate) fn box_clone(&self) -> Box<Self> {
        Box::new(self.clone())
    }

    /// Attempts to parse a Merkle frontier from the given C++ stream.
    pub(crate) fn parse(reader: &mut CppStream<'_>) -> Result<Box<Self>, String> {
        match read_frontier_v1(reader) {
            Ok(parsed) => Ok(Box::new(MerkleFrontier(parsed))),
            Err(e) => Err(format!("Failed to parse v5 Merkle frontier: {}", e)),
        }
    }

    /// Serializes the frontier to the given C++ stream.
    pub(crate) fn serialize(&self, writer: &mut CppStream<'_>) -> Result<(), String> {
        write_frontier_v1(writer, &self.0)
            .map_err(|e| format!("Failed to serialize v5 Merkle frontier: {}", e))
    }

    /// Serializes the frontier to the given C++ stream in the legacy frontier encoding.
    pub(crate) fn serialize_legacy(&self, writer: &mut CppStream<'_>) -> Result<(), String> {
        let commitment_tree = CommitmentTree::from_frontier(&self.0);
        write_commitment_tree(&commitment_tree, writer).map_err(|e| {
            format!(
                "Failed to serialize Merkle frontier in legacy format: {}",
                e,
            )
        })
    }

    /// Returns the amount of memory dynamically allocated for the frontier.
    ///
    /// Includes `self` because this type is stored on the heap when passed to C++.
    pub(crate) fn dynamic_memory_usage(&self) -> usize {
        size_of_val(&self.0) + self.0.dynamic_memory_usage()
    }

    /// Obtains the current root of this Merkle frontier by hashing against empty nodes up
    /// to the maximum height of the pruned tree that the frontier represents.
    pub(crate) fn root(&self) -> [u8; 32] {
        let mut root = [0; 32];
        self.0
            .root()
            .write(&mut root[..])
            .expect("root is 32 bytes");
        root
    }

    /// Returns the number of leaves in the Merkle tree for which this is the frontier.
    pub(crate) fn size(&self) -> u64 {
        self.0.value().map_or(0, |f| u64::from(f.position()) + 1)
    }
}

/// Returns the root of an empty Orchard Merkle tree.
pub(crate) fn orchard_empty_root() -> [u8; 32] {
    let level = Level::from(NOTE_COMMITMENT_TREE_DEPTH);
    MerkleHashOrchard::empty_root(level).to_bytes()
}

/// An Orchard incremental Merkle frontier.
pub(crate) type OrchardFrontier = MerkleFrontier<MerkleHashOrchard>;


/// Constructs a new empty Orchard Merkle frontier.
pub(crate) fn new_orchard() -> Box<OrchardFrontier> {
    Box::new(MerkleFrontier(Inner::empty()))
}

/// Attempts to parse an Orchard Merkle frontier from the given C++ stream.
pub(crate) fn parse_orchard(reader: &mut CppStream<'_>) -> Result<Box<OrchardFrontier>, String> {
    OrchardFrontier::parse(reader)
}

pub(crate) struct OrchardWallet;

impl OrchardFrontier {
    /// Appends the note commitments in the given bundle to this frontier.
    pub(crate) fn append_bundle(
        &mut self,
        bundle: &orchard_bundle::Bundle,
    ) -> Result<ffi::OrchardAppendResult, &'static str> {
        if let Some(bundle) = bundle.inner() {
            // A single bundle can't contain 2^TRACKED_SUBTREE_HEIGHT actions, so we'll never cross
            // more than one subtree boundary while processing that bundle. This means we only need
            // to find a single subtree root while processing an individual bundle, so `Option` is
            // sufficient; we don't need a `Vec`.
            let mut tracked_root: Option<MerkleHashOrchard> = None;
            for action in bundle.actions().iter() {
                if !self.0.append(MerkleHashOrchard::from_cmx(action.cmx())) {
                    return Err("Orchard note commitment tree is full.");
                }

                if let Some(non_empty_frontier) = self.0.value() {
                    let level = Level::from(TRACKED_SUBTREE_HEIGHT);
                    let pos = non_empty_frontier.position();
                    if pos.is_complete_subtree(level) {
                        assert_eq!(tracked_root, None);
                        tracked_root = Some(non_empty_frontier.root(Some(level)))
                    }
                }
            }

            Ok(if let Some(root_hash) = tracked_root {
                ffi::OrchardAppendResult {
                    has_subtree_boundary: true,
                    completed_subtree_root: root_hash.to_bytes(),
                }
            } else {
                ffi::OrchardAppendResult {
                    has_subtree_boundary: false,
                    completed_subtree_root: [0u8; 32],
                }
            })
        } else {
            Err("null Orchard bundle pointer")
        }
    }

        /// Appends a single cmx this frontier.
        pub(crate) fn append(
            &mut self,
            orchard_cmx: [u8; 32],
        ) -> Result<ffi::OrchardAppendResult, &'static str> {
            // A single bundle can't contain 2^TRACKED_SUBTREE_HEIGHT actions, so we'll never cross
            // more than one subtree boundary while processing that bundle. This means we only need
            // to find a single subtree root while processing an individual bundle, so `Option` is
            // sufficient; we don't need a `Vec`.
            let mut tracked_root: Option<MerkleHashOrchard> = None;

            // Deserialize the extracted note commitment.
            let cmx = match de_ct(OrchardExtractedNoteCommitment::from_bytes(&orchard_cmx)) {
                Some(a) => a,
                None => return Err("Orchard CMX is invalid."),
            };

            //Append the note commitment to the frontier.
            if !self.0.append(MerkleHashOrchard::from_cmx(&cmx)) {
                return Err("Orchard note commitment tree is full.");
            }

            // Check if the frontier has a complete subtree.
            if let Some(non_empty_frontier) = self.0.value() {
                let level = Level::from(TRACKED_SUBTREE_HEIGHT);
                let pos = non_empty_frontier.position();
                if pos.is_complete_subtree(level) {
                    assert_eq!(tracked_root, None);
                    tracked_root = Some(non_empty_frontier.root(Some(level)))
                }
            }
            
            // Return the result.
            Ok(if let Some(root_hash) = tracked_root {
                ffi::OrchardAppendResult {
                    has_subtree_boundary: true,
                    completed_subtree_root: root_hash.to_bytes(),
                }
            } else {
                ffi::OrchardAppendResult {
                    has_subtree_boundary: false,
                    completed_subtree_root: [0u8; 32],
                }
            })
        }

    /// Overwrites the first bridge of the Orchard wallet's note commitment tree to have
    /// `self` as its latest state.
    ///
    /// This will fail with an assertion error if any checkpoints exist in the tree.
    ///
    /// TODO: Remove once `crate::wallet` is migrated to `cxx`.
    pub(crate) fn init_wallet(&self, wallet: *mut OrchardWallet) -> bool {
        crate::orchard_wallet::orchard_wallet_init_from_frontier(wallet as *mut OrchardWalletInternal, &self.0)
    }
}

/// Returns the root of an empty Sapling Merkle tree.
pub(crate) fn sapling_empty_root() -> [u8; 32] {
    let level = Level::from(NOTE_COMMITMENT_TREE_DEPTH);
    let mut root = [0; 32];
    Node::empty_root(level)
            .write(&mut root[..])
            .expect("root is 32 bytes");
    root
}

/// An Sapling incremental Merkle frontier.
pub(crate) type SaplingFrontier = MerkleFrontier<Node>;

/// Constructs a new empty Sapling Merkle frontier.
pub(crate) fn new_sapling() -> Box<SaplingFrontier> {
    Box::new(MerkleFrontier(Inner::empty()))
}

/// Attempts to parse an Orchard Merkle frontier from the given C++ stream.
pub(crate) fn parse_sapling(reader: &mut CppStream<'_>) -> Result<Box<SaplingFrontier>, String> {
    SaplingFrontier::parse(reader)
}

pub(crate) struct SaplingWallet;

impl SaplingFrontier {
    /// Appends the note commitments in the given bundle to this frontier.
    pub(crate) fn append_bundle(
        &mut self,
        sapling_bundle: &SaplingBundle,
    ) -> Result<ffi::SaplingAppendResult, &'static str> {
        if let Some(sapling_bundle) = sapling_bundle.inner() {
            // A single bundle can't contain 2^TRACKED_SUBTREE_HEIGHT actions, so we'll never cross
            // more than one subtree boundary while processing that bundle. This means we only need
            // to find a single subtree root while processing an individual bundle, so `Option` is
            // sufficient; we don't need a `Vec`.
            let mut tracked_root: Option<Node> = None;
            for shielded_output in sapling_bundle.shielded_outputs().iter() {
                if !self.0.append(Node::from_cmu(shielded_output.cmu())) {
                    return Err("Sapling note commitment tree is full.");
                }

                if let Some(non_empty_frontier) = self.0.value() {
                    let level = Level::from(TRACKED_SUBTREE_HEIGHT);
                    let pos = non_empty_frontier.position();
                    if pos.is_complete_subtree(level) {
                        assert_eq!(tracked_root, None);
                        tracked_root = Some(non_empty_frontier.root(Some(level)))
                    }
                }
            }

            Ok(if let Some(root_hash) = tracked_root {

                let mut root = [0; 32];
                root_hash.write(&mut root[..])
                        .expect("root is 32 bytes");

                ffi::SaplingAppendResult {
                    has_subtree_boundary: true,
                    completed_subtree_root: root,
                }
            } else {
                ffi::SaplingAppendResult {
                    has_subtree_boundary: false,
                    completed_subtree_root: [0u8; 32],
                }
            })
        } else {
            Err("null Sapling bundle pointer")
        }
    }

    pub(crate) fn append(
        &mut self,
        sapling_cmu: [u8; 32],
    ) -> Result<ffi::SaplingAppendResult, &'static str> {
        // A single bundle can't contain 2^TRACKED_SUBTREE_HEIGHT actions, so we'll never cross
        // more than one subtree boundary while processing that bundle. This means we only need
        // to find a single subtree root while processing an individual bundle, so `Option` is
        // sufficient; we don't need a `Vec`.
        let mut tracked_root: Option<Node> = None;

        // Deserialize the extracted note commitment.
        let cmu = match de_ct(SaplingExtractedNoteCommitment::from_bytes(&sapling_cmu)) {
            Some(a) => a,
            None => return Err("Sapling CMU is invalid."),
        };

        //Append the note commitment to the frontier.
        if !self.0.append(Node::from_cmu(&cmu)) {
            return Err("Sapling note commitment tree is full.");
        }

        // Check if the frontier has a complete subtree.
        if let Some(non_empty_frontier) = self.0.value() {
            let level = Level::from(TRACKED_SUBTREE_HEIGHT);
            let pos = non_empty_frontier.position();
            if pos.is_complete_subtree(level) {
                assert_eq!(tracked_root, None);
                tracked_root = Some(non_empty_frontier.root(Some(level)))
            }
        }
        
        // Return the result.
        Ok(if let Some(root_hash) = tracked_root {

            let mut root = [0; 32];
            root_hash.write(&mut root[..])
                    .expect("root is 32 bytes");

            ffi::SaplingAppendResult {
                has_subtree_boundary: true,
                completed_subtree_root: root,
            }
        } else {
            ffi::SaplingAppendResult {
                has_subtree_boundary: false,
                completed_subtree_root: [0u8; 32],
            }
        })

    }

    /// Overwrites the first bridge of the SApling wallet's note commitment tree to have
    /// `self` as its latest state.
    ///
    /// This will fail with an assertion error if any checkpoints exist in the tree.
    ///
    /// TODO: Remove once `crate::wallet` is migrated to `cxx`.
    pub(crate) fn init_wallet(&self, wallet: *mut SaplingWallet) -> bool {
        crate::sapling_wallet::sapling_wallet_init_from_frontier(wallet as *mut SaplingWalletInternal, &self.0)
    }
}
