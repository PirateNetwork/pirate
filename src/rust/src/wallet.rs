use bridgetree::BridgeTree;
use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use incrementalmerkletree::Position;
use libc::c_uchar;
use std::collections::{BTreeSet,BTreeMap};
use std::io;
use tracing::error;

use zcash_encoding::{Optional, Vector};
use zcash_primitives::{
    consensus::BlockHeight,
    merkle_tree::{read_position, write_position},
    sapling::{Node, NOTE_COMMITMENT_TREE_DEPTH},
    sapling::note::ExtractedNoteCommitment,
    transaction::TxId,
};

use crate::{
    incremental_merkle_tree::{read_tree, write_tree},
    streams_ffi::{CppStreamReader, CppStreamWriter, ReadCb, StreamObj, WriteCb},
    sapling::{Bundle, Output}
};

pub const MAX_CHECKPOINTS: usize = 100;
const NOTE_STATE_V1: u8 = 1;

/// A data structure tracking the last transaction whose notes
/// have been added to the wallet's note commitment tree.
#[derive(Debug, Clone)]
pub struct LastObserved {
    block_height: BlockHeight,
    block_tx_idx: Option<usize>,
    tx_output_idx: Option<usize>,
}

pub struct Wallet {
    /// The in-memory index from txid to note positions from the associated transaction.
    /// This map should always have a subset of the keys in `wallet_received_notes`.
    wallet_note_positions: BTreeMap<TxId, NotePositions>,
    /// The incremental Merkle tree used to track note commitments and witnesses for notes
    /// belonging to the wallet.
    commitment_tree: BridgeTree<Node, u32, NOTE_COMMITMENT_TREE_DEPTH>,
    /// The block height at which the last checkpoint was created, if any.
    last_checkpoint: Option<BlockHeight>,
    /// The block height and transaction index of the note most recently added to
    /// `commitment_tree`
    last_observed: Option<LastObserved>,
}

#[derive(Debug, Clone)]
pub enum WalletError {
    OutOfOrder(LastObserved, BlockHeight, usize, usize),
    NoteCommitmentTreeFull,
}

#[derive(Debug, Clone)]
pub enum RewindError {
    /// The note commitment tree does not contain enough checkpoints to
    /// rewind to the requested height. The number of blocks that
    /// it is possible to rewind is returned as the payload of
    /// this error.
    InsufficientCheckpoints(usize),
}

/// A data structure holding chain position information for a single transaction.
#[derive(Clone, Debug)]
struct NotePositions {
    /// The height of the block containing the transaction.
    tx_height: BlockHeight,
    /// A map from the index of an Orchard action tracked by this wallet, to the position
    /// of the output note's commitment within the global Merkle tree.
    note_positions: BTreeMap<usize, Position>,
}


impl Wallet {
    pub fn empty() -> Self {
        Wallet {
            wallet_note_positions: BTreeMap::new(),
            commitment_tree: BridgeTree::new(MAX_CHECKPOINTS),
            last_checkpoint: None,
            last_observed: None,
        }
    }

    /// Reset the state of the wallet to be suitable for rescan.
    /// This removes all witness from the wallet.
    pub fn reset(&mut self) {
        self.wallet_note_positions.clear();
        self.commitment_tree = BridgeTree::new(MAX_CHECKPOINTS);
        self.last_checkpoint = None;
        self.last_observed = None;
    }

    /// Checkpoints the note commitment tree. This returns `false` and leaves the note
    /// commitment tree unmodified if the block height does not immediately succeed
    /// the last checkpointed block height (unless the note commitment tree is empty,
    /// in which case it unconditionally succeeds). This must be called exactly once
    /// per block.
    #[tracing::instrument(level = "trace", skip(self))]
    pub fn checkpoint(&mut self, block_height: BlockHeight) -> bool {
        // checkpoints must be in order of sequential block height and every
        // block must be checkpointed
        if let Some(last_height) = self.last_checkpoint {
            let expected_height = last_height + 1;
            if block_height != expected_height {
                tracing::error!(
                    "Expected checkpoint height {}, given {}",
                    expected_height,
                    block_height
                );
                return false;
            }
        }

        self.commitment_tree.checkpoint(block_height.into());
        self.last_checkpoint = Some(block_height);
        true
    }

    /// Returns the last checkpoint if any. If no checkpoint exists, the wallet has not
    /// yet observed any blocks.
    pub fn last_checkpoint(&self) -> Option<BlockHeight> {
        self.last_checkpoint
    }

    /// Rewinds the note commitment tree to the given height, removes notes and spentness
    /// information for transactions mined in the removed blocks, and returns the height to which
    /// the tree has been rewound if successful. Returns  `RewindError` if not enough checkpoints
    /// exist to execute the full rewind requested and the wallet has witness information that
    /// would be invalidated by the rewind. If the requested height is greater than or equal to the
    /// height of the latest checkpoint, this returns a successful result containing the height of
    /// the last checkpoint.
    ///
    /// In the case that no checkpoints exist but the note commitment tree also records no witness
    /// information, we allow the wallet to continue to rewind, under the assumption that the state
    /// of the note commitment tree will be overwritten prior to the next append.
    #[tracing::instrument(level = "trace", skip(self))]
    pub fn rewind(&mut self, to_height: BlockHeight) -> Result<BlockHeight, RewindError> {
        if let Some(checkpoint_height) = self.last_checkpoint {
            if to_height >= checkpoint_height {
                tracing::trace!("Last checkpoint is before the rewind height, nothing to do.");
                return Ok(checkpoint_height);
            }

            tracing::trace!("Rewinding note commitment tree");
            let blocks_to_rewind = <u32>::from(checkpoint_height) - <u32>::from(to_height);
            let checkpoint_count = self.commitment_tree.checkpoints().len();
            for _ in 0..blocks_to_rewind {
                // If the rewind fails, we have no more checkpoints. This is fine in the
                // case that we have a recently-initialized tree, so long as we have no
                // witnessed indices. In the case that we have any witnessed notes, we
                // have hit the maximum rewind limit, and this is an error.
                if !self.commitment_tree.rewind() {
                    assert!(self.commitment_tree.checkpoints().is_empty());
                    if !self.commitment_tree.marked_indices().is_empty() {
                        return Err(RewindError::InsufficientCheckpoints(checkpoint_count));
                    }
                }
            }

            // retain notes that correspond to transactions that are not "un-mined" after
            // the rewind
            let to_retain: BTreeSet<_> = self
                .wallet_note_positions
                .iter()
                .filter_map(|(txid, n)| {
                    if n.tx_height <= to_height {
                        Some(*txid)
                    } else {
                        None
                    }
                })
                .collect();
            tracing::trace!("Retaining notes in transactions {:?}", to_retain);

            // self.mined_notes.retain(|_, v| to_retain.contains(&v.txid));

            // nullifier and received note data are retained, because these values are stable
            // once we've observed a note for the first time. The block height at which we
            // observed the note is removed along with the note positions, because the
            // transaction will no longer have been observed as having been mined.
            self.wallet_note_positions
                .retain(|txid, _| to_retain.contains(txid));

            // reset our last observed height to ensure that notes added in the future are
            // from a new block
            self.last_observed = Some(LastObserved {
                block_height: to_height,
                block_tx_idx: None,
                tx_output_idx: None,
            });

            self.last_checkpoint = if checkpoint_count > blocks_to_rewind as usize {
                Some(to_height)
            } else {
                // checkpoint_count <= blocks_to_rewind
                None
            };

            Ok(to_height)
        } else if self.commitment_tree.marked_indices().is_empty() {
            tracing::trace!("No witnessed notes in tree, allowing rewind without checkpoints");

            // If we have no witnessed notes, it's okay to keep "rewinding" even though
            // we have no checkpoints. We then allow last_observed to assume the height
            // to which we have reset the tree state.
            self.last_observed = Some(LastObserved {
                block_height: to_height,
                block_tx_idx: None,
                tx_output_idx: None,
            });

            Ok(to_height)
        } else {
            Err(RewindError::InsufficientCheckpoints(0))
        }
    }

    /// Add note commitments for the Orchard components of a transaction to the note
    /// commitment tree, and mark the tree at the notes decryptable by this wallet so that
    /// in the future we can produce authentication paths to those notes.
    ///
    /// * `block_height` - Height of the block containing the transaction that provided
    ///   this bundle.
    /// * `block_tx_idx` - Index of the transaction within the block
    /// * `txid` - Identifier of the transaction.
    /// * `bundle` - Orchard component of the transaction.
    /// #[tracing::instrument(level = "trace", skip(self))]
    pub fn sapling_append_commitments(
        &mut self,
        block_height: BlockHeight,
        block_tx_idx: usize,
        sapling_bundle: &Bundle,
    ) -> Result<(), WalletError> {
        // Check that the wallet is in the correct state to update the note commitment tree with
        // new outputs.
        if let Some(last) = &self.last_observed {
            if !(
                // we are observing a subsequent transaction in the same block
                (block_height == last.block_height && last.block_tx_idx.map_or(false, |idx| idx < block_tx_idx))
                // or we are observing a new block
                || block_height > last.block_height
            ) {
                return Err(WalletError::OutOfOrder(
                    last.clone(),
                    block_height,
                    block_tx_idx,
                    0,
                ));
            }
        }

        self.last_observed = Some(LastObserved {
            block_height,
            block_tx_idx: Some(block_tx_idx),
            tx_output_idx: None,
        });

        // update the block height recorded for the transaction
        // let my_notes_for_tx = self.wallet_received_notes.get(txid);
        // if my_notes_for_tx.is_some() {
        //     tracing::trace!("Tx is ours, marking as mined");
        //     assert!(self
        //         .wallet_note_positions
        //         .insert(
        //             *txid,
        //             NotePositions {
        //                 tx_height: block_height,
        //                 note_positions: BTreeMap::default(),
        //             },
        //         )
        //         .is_none());
        // }
        if let Some(sapling_bundle) = sapling_bundle.inner() {

            for shielded_output in sapling_bundle.shielded_outputs().iter() {
                // append the note commitment for each action to the note commitment tree
                if !self
                    .commitment_tree
                    .append(Node::from_cmu(shielded_output.cmu()))
                {
                    return Err(WalletError::NoteCommitmentTreeFull);
                }

                // for notes that are ours, mark the current state of the tree
                // if my_notes_for_tx
                //     .as_ref()
                //     .and_then(|n| n.decrypted_notes.get(&action_idx))
                //     .is_some()
                // {
                //     tracing::trace!("Witnessing Orchard note ({}, {})", txid, action_idx);
                //     let pos = self.commitment_tree.mark().expect("tree is not empty");
                //     assert!(self
                //         .wallet_note_positions
                //         .get_mut(txid)
                //         .expect("We created this above")
                //         .note_positions
                //         .insert(action_idx, pos)
                //         .is_none());
                // }

                // For nullifiers that are ours that we detect as spent by this action,
                // we will record that input as being mined.
                // if let Some(outpoint) = self.nullifiers.get(action.nullifier()) {
                //     assert!(self
                //         .mined_notes
                //         .insert(
                //             *outpoint,
                //             InPoint {
                //                 txid: *txid,
                //                 action_idx,
                //             },
                //         )
                //         .is_none());
                // }
            }
        }

        Ok(())
    }

    pub fn clear_single_txid_postions(
        &mut self,
        txid: &TxId,
    ) -> Result<(), WalletError>  {

        //Check if txid already exists
        match self.wallet_note_positions.get(txid) {
           Some(_) => {
               self.wallet_note_positions.remove(txid);
               ()
           },
           None => ()
        }

        Ok(())

    }

    pub fn create_empty_txid_positions(
        &mut self,
        block_height: BlockHeight,
        txid: &TxId,
    ) -> Result<(), WalletError>  {

        //Check if txid already exists
        match self.wallet_note_positions.get(txid) {
           Some(_) => {
               self.wallet_note_positions.remove(txid);
               ()
           },
           None => ()
        }

        self.wallet_note_positions
            .insert(
                *txid,
                NotePositions {
                    tx_height: block_height,
                    note_positions: BTreeMap::default(),
                },
            );


        Ok(())

    }

    pub fn sapling_append_single_commitment(
        &mut self,
        block_height: BlockHeight,
        txid: &TxId,
        block_tx_idx: usize,
        tx_output_idx: usize,
        sapling_output: &Output,
        is_mine: bool,
    ) -> Result<(), WalletError> {
        if let Some(last) = &self.last_observed {
            if !(
                //We are observing a subsequent output in the same transaction
                (block_height == last.block_height && last.block_tx_idx.map_or(false, |idx| idx == block_tx_idx) && last.tx_output_idx.map_or(false, |idx| idx < tx_output_idx))
                // we are observing a subsequent transaction in the same block
                || (block_height == last.block_height && last.block_tx_idx.map_or(false, |idx| idx < block_tx_idx))
                // or we are observing a new block
                || block_height > last.block_height
            ) {
                return Err(WalletError::OutOfOrder(
                    last.clone(),
                    block_height,
                    block_tx_idx,
                    tx_output_idx,
                ));
            }
        }

        self.last_observed = Some(LastObserved {
            block_height,
            block_tx_idx: Some(block_tx_idx),
            tx_output_idx: Some(tx_output_idx),
        });

        if !self.commitment_tree.append(Node::from_cmu(
            &ExtractedNoteCommitment::from_bytes(&sapling_output.cmu()).unwrap()
            ))
        {
            return Err(WalletError::NoteCommitmentTreeFull);
        }

        if is_mine {
            let pos = self.commitment_tree.mark().expect("tree is not empty");
            assert!(self
                    .wallet_note_positions
                    .get_mut(txid)
                    .expect("This should already be created")
                    .note_positions
                    .insert(tx_output_idx, pos)
                    .is_none());

        }


        Ok(())

    }

    /// Returns the root of the Orchard note commitment tree, as of the specified checkpoint
    /// depth. A depth of 0 corresponds to the chain tip.
    pub fn note_commitment_tree_root(&self, checkpoint_depth: usize) -> Option<Node> {
        self.commitment_tree.root(checkpoint_depth)
    }

}

#[no_mangle]
pub extern "C" fn sapling_wallet_new() -> *mut Wallet {
    let empty_wallet = Wallet::empty();
    Box::into_raw(Box::new(empty_wallet))
}

#[no_mangle]
pub extern "C" fn sapling_wallet_free(wallet: *mut Wallet) {
    if !wallet.is_null() {
        drop(unsafe { Box::from_raw(wallet) });
    }
}

#[no_mangle]
pub extern "C" fn sapling_wallet_reset(wallet: *mut Wallet) {
    let wallet = unsafe { wallet.as_mut() }.expect("Wallet pointer may not be null");
    wallet.reset();
}

#[no_mangle]
pub extern "C" fn sapling_wallet_checkpoint(
    wallet: *mut Wallet,
    block_height: BlockHeight,
) -> bool {
    let wallet = unsafe { wallet.as_mut() }.expect("Wallet pointer may not be null");
    wallet.checkpoint(block_height)
}

#[no_mangle]
pub extern "C" fn sapling_wallet_get_last_checkpoint(
    wallet: *const Wallet,
    block_height_ret: *mut u32,
) -> bool {
    let wallet = unsafe { wallet.as_ref() }.expect("Wallet pointer may not be null");
    let block_height_ret =
        unsafe { block_height_ret.as_mut() }.expect("Block height return pointer may not be null");
    if let Some(height) = wallet.last_checkpoint() {
        *block_height_ret = height.into();
        true
    } else {
        false
    }
}

#[no_mangle]
pub extern "C" fn sapling_wallet_rewind(
    wallet: *mut Wallet,
    to_height: BlockHeight,
    result_height: *mut BlockHeight,
) -> bool {
    let wallet = unsafe { wallet.as_mut() }.expect("Wallet pointer may not be null");
    let result_height =
        unsafe { result_height.as_mut() }.expect("Return value pointer may not be null.");
    match wallet.rewind(to_height) {
        Ok(result) => {
            *result_height = result;
            true
        }
        Err(e) => {
            error!(
                "Unable to rewind the wallet to height {:?}: {:?}",
                to_height, e
            );
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn sapling_wallet_append_bundle_commitments(
    wallet: *mut Wallet,
    block_height: u32,
    block_tx_idx: usize,
    sapling_bundle: *const Bundle,
) -> bool {
    let wallet = unsafe { wallet.as_mut() }.expect("Wallet pointer may not be null");
    // let txid = TxId::from_bytes(*unsafe { txid.as_ref() }.expect("txid may not be null."));
    if let Some(sapling_bundle) = unsafe { sapling_bundle.as_ref() } {
        if let Err(e) =
            wallet.sapling_append_commitments(block_height.into(), block_tx_idx, sapling_bundle)
        {
            error!("An error occurred adding the Orchard bundle's notes to the note commitment tree: {:?}", e);
            return false;
        }
    }

    true
}

#[no_mangle]
pub extern "C" fn clear_note_positions_for_txid(
    wallet: *mut Wallet,
    txid: *const [c_uchar; 32],
) -> bool {
    let wallet = unsafe { wallet.as_mut() }.expect("Wallet pointer may not be null");
    let txid = TxId::from_bytes(*unsafe { txid.as_ref() }.expect("txid may not be null."));
    if let Err(e) =
        wallet.clear_single_txid_postions(&txid)
    {
        error!("An error occurred clearing txid postions: {:?}", e);
        return false;
    }

    true
}

#[no_mangle]
pub extern "C" fn create_single_txid_positions(
    wallet: *mut Wallet,
    block_height: u32,
    txid: *const [c_uchar; 32],
) -> bool {
    let wallet = unsafe { wallet.as_mut() }.expect("Wallet pointer may not be null");
    let txid = TxId::from_bytes(*unsafe { txid.as_ref() }.expect("txid may not be null."));
    if let Err(e) =
        wallet.create_empty_txid_positions(block_height.into(), &txid)
    {
        error!("An error occurred createing empty txid postions: {:?}", e);
        return false;
    }

    true
}

#[no_mangle]
pub extern "C" fn sapling_wallet_append_single_commitment(
    wallet: *mut Wallet,
    block_height: u32,
    txid: *const [c_uchar; 32],
    block_tx_idx: usize,
    tx_output_idx: usize,
    sapling_output: *const Output,
    is_mine: bool,
) -> bool {
    let wallet = unsafe { wallet.as_mut() }.expect("Wallet pointer may not be null");
    let txid = TxId::from_bytes(*unsafe { txid.as_ref() }.expect("txid may not be null."));
    if let Some(sapling_output) = unsafe { sapling_output.as_ref() } {
        if let Err(e) =
            wallet.sapling_append_single_commitment(block_height.into(), &txid, block_tx_idx, tx_output_idx, sapling_output, is_mine)
        {
            error!("An error occurred adding this Sapling output to the note commitment tree: {:?}", e);
            return false;
        }
    }

    true
}

#[no_mangle]
pub extern "C" fn sapling_wallet_commitment_tree_root(
    wallet: *const Wallet,
    checkpoint_depth: usize,
    root_ret: *mut [u8; 32],
) -> bool {
    let wallet = unsafe { wallet.as_ref() }.expect("Wallet pointer may not be null");
    let root_ret = unsafe { root_ret.as_mut() }.expect("Cannot return to the null pointer.");

    // there is always a valid note commitment tree root at depth 0
    // (it may be the empty root)
    if let Some(root) = wallet.note_commitment_tree_root(checkpoint_depth) {
        *root_ret = root.to_bytes();
        true
    } else {
        false
    }
}

#[no_mangle]
pub extern "C" fn sapling_wallet_gc_note_commitment_tree(wallet: *mut Wallet) {
    let wallet = unsafe { wallet.as_mut() }.expect("Wallet pointer may not be null.");
    wallet.commitment_tree.garbage_collect();
}

#[no_mangle]
pub extern "C" fn sapling_wallet_write_note_commitment_tree(
    wallet: *const Wallet,
    stream: Option<StreamObj>,
    write_cb: Option<WriteCb>,
) -> bool {
    let wallet = unsafe { wallet.as_ref() }.expect("Wallet pointer may not be null.");
    let mut writer = CppStreamWriter::from_raw_parts(stream, write_cb.unwrap());

    let write_v1 = move |mut writer: CppStreamWriter| -> io::Result<()> {
        Optional::write(&mut writer, wallet.last_checkpoint, |w, h| {
            w.write_u32::<LittleEndian>(h.into())
        })?;
        write_tree(&mut writer, &wallet.commitment_tree)?;

        // Write note positions.
        Vector::write_sized(
            &mut writer,
            wallet.wallet_note_positions.iter(),
            |mut w, (txid, tx_notes)| {
                txid.write(&mut w)?;
                w.write_u32::<LittleEndian>(tx_notes.tx_height.into())?;
                Vector::write_sized(
                    w,
                    tx_notes.note_positions.iter(),
                    |w, (action_idx, position)| {
                        w.write_u32::<LittleEndian>(*action_idx as u32)?;
                        write_position(w, *position)
                    },
                )
            },
        )?;

        Ok(())
    };

    match writer
        .write_u8(NOTE_STATE_V1)
        .and_then(|()| write_v1(writer))
    {
        Ok(()) => true,
        Err(e) => {
            error!("Failure in writing Orchard note commitment tree: {}", e);
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn sapling_wallet_load_note_commitment_tree(
    wallet: *mut Wallet,
    stream: Option<StreamObj>,
    read_cb: Option<ReadCb>,
) -> bool {
    let wallet = unsafe { wallet.as_mut() }.expect("Wallet pointer may not be null.");
    let mut reader = CppStreamReader::from_raw_parts(stream, read_cb.unwrap());

    let mut read_v1 = move |mut reader: CppStreamReader| -> io::Result<()> {
        let last_checkpoint = Optional::read(&mut reader, |r| {
            r.read_u32::<LittleEndian>().map(BlockHeight::from)
        })?;
        let commitment_tree = read_tree(&mut reader)?;

        // Read note positions.
        wallet.wallet_note_positions = Vector::read_collected(&mut reader, |mut r| {
            Ok((
                TxId::read(&mut r)?,
                NotePositions {
                    tx_height: r.read_u32::<LittleEndian>().map(BlockHeight::from)?,
                    note_positions: Vector::read_collected(r, |r| {
                        Ok((
                            r.read_u32::<LittleEndian>().map(|idx| idx as usize)?,
                            read_position(r)?,
                        ))
                    })?,
                },
            ))
        })?;

        wallet.last_checkpoint = last_checkpoint;
        wallet.commitment_tree = commitment_tree;
        Ok(())
    };

    match reader.read_u8() {
        Err(e) => {
            error!(
                "Failed to read Orchard note position serialization flag: {}",
                e
            );
            false
        }
        Ok(NOTE_STATE_V1) => match read_v1(reader) {
            Ok(_) => true,
            Err(e) => {
                error!(
                    "Failed to read Orchard note commitment or last checkpoint height: {}",
                    e
                );
                false
            }
        },
        Ok(flag) => {
            error!(
                "Unrecognized Orchard note position serialization version: {}",
                flag
            );
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn sapling_wallet_init_from_frontier(
    wallet: *mut Wallet,
    frontier: *const bridgetree::Frontier<Node, NOTE_COMMITMENT_TREE_DEPTH>,
) -> bool {
    let wallet = unsafe { wallet.as_mut() }.expect("Wallet pointer may not be null.");
    let frontier = unsafe { frontier.as_ref() }.expect("Wallet pointer may not be null.");

    if wallet.commitment_tree.checkpoints().is_empty()
        && wallet.commitment_tree.marked_indices().is_empty()
    {
        wallet.commitment_tree = frontier.value().map_or_else(
            || BridgeTree::new(MAX_CHECKPOINTS),
            |nonempty_frontier| {
                BridgeTree::from_frontier(MAX_CHECKPOINTS, nonempty_frontier.clone())
            },
        );
        true
    } else {
        // if we have any checkpoints in the tree, or if we have any witnessed notes,
        // don't allow reinitialization
        error!(
            "Invalid attempt to reinitialize note commitment tree: {} checkpoints present.",
            wallet.commitment_tree.checkpoints().len()
        );
        false
    }
}
