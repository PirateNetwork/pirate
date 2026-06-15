use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use std::collections::{BTreeMap, BTreeSet};
use std::io::{self, Read, Write};

use bridgetree::{BridgeTree, Checkpoint, MerkleBridge};
use incrementalmerkletree::{Address, Hashable, Level, Position};
use zcash_encoding::{Optional, Vector};
use zcash_primitives::merkle_tree::{
    read_address, read_leu64_usize, read_nonempty_frontier_v1, read_position, write_address,
    write_nonempty_frontier_v1, write_position, write_usize_leu64, HashSer,
};

/// Depth of both Sapling and Orchard note commitment trees (32 levels).
pub const NOTE_COMMITMENT_TREE_DEPTH: u8 = 32;

/// Maximum number of checkpoints retained in the in-memory tree.
pub const MAX_CHECKPOINTS: usize = 100;

pub const SER_V1: u8 = 1;
pub const SER_V2: u8 = 2;
pub const SER_V3: u8 = 3;
/// ShardTree serialization format — not supported by BridgeTree; triggers rescan.
pub const SER_V4: u8 = 4;

/// Reads part of the information required to construct a `bridgetree` version `0.3.0`
/// [`MerkleBridge`] as encoded from the `incrementalmerkletree` version `0.3.0` version of the
/// `AuthFragment` data structure.
#[allow(clippy::redundant_closure)]
pub fn read_auth_fragment_v1<H: HashSer, R: Read>(
    mut reader: R,
) -> io::Result<(Position, usize, Vec<H>)> {
    let position = read_position(&mut reader)?;
    let alts_observed = read_leu64_usize(&mut reader)?;
    let values = Vector::read(&mut reader, |r| H::read(r))?;

    Ok((position, alts_observed, values))
}

#[allow(clippy::needless_borrows_for_generic_args)]
pub fn read_bridge_v1<H: HashSer + Ord + Clone, R: Read>(
    mut reader: R,
) -> io::Result<MerkleBridge<H>> {
    fn levels_required(pos: Position) -> impl Iterator<Item = Level> {
        (0u8..64).filter_map(move |i| {
            if u64::from(pos) == 0 || u64::from(pos) & (1 << i) == 0 {
                Some(Level::from(i))
            } else {
                None
            }
        })
    }

    let prior_position = Optional::read(&mut reader, read_position)?;

    let fragments = Vector::read(&mut reader, |mut r| {
        let fragment_position = read_position(&mut r)?;
        let (pos, levels_observed, values) = read_auth_fragment_v1(r)?;

        if fragment_position == pos {
            Ok((pos, levels_observed, values))
        } else {
            Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "Auth fragment position mismatch: {:?} != {:?}",
                    fragment_position, pos
                ),
            ))
        }
    })?;

    let frontier = read_nonempty_frontier_v1(&mut reader)?;
    let mut tracking = BTreeSet::new();
    let mut ommers = BTreeMap::new();
    for (pos, levels_observed, values) in fragments.into_iter() {
        let levels = levels_required(pos)
            .take(levels_observed + 1)
            .collect::<Vec<_>>();

        tracking.insert(Address::above_position(*levels.last().unwrap(), pos));

        for (level, ommer_value) in levels
            .into_iter()
            .rev()
            .skip(1)
            .zip(values.into_iter().rev())
        {
            let ommer_address = Address::above_position(level, pos).sibling();
            ommers.insert(ommer_address, ommer_value);
        }
    }

    Ok(MerkleBridge::from_parts(
        prior_position,
        tracking,
        ommers,
        frontier,
    ))
}

#[allow(clippy::needless_borrows_for_generic_args)]
pub fn read_bridge_v2<H: HashSer + Ord + Clone, R: Read>(
    mut reader: R,
) -> io::Result<MerkleBridge<H>> {
    let prior_position = Optional::read(&mut reader, read_position)?;
    let tracking = Vector::read_collected(&mut reader, |r| read_address(r))?;
    let ommers = Vector::read_collected(&mut reader, |mut r| {
        let addr = read_address(&mut r)?;
        let value = H::read(&mut r)?;
        Ok((addr, value))
    })?;
    let frontier = read_nonempty_frontier_v1(&mut reader)?;

    Ok(MerkleBridge::from_parts(
        prior_position,
        tracking,
        ommers,
        frontier,
    ))
}

pub fn read_bridge<H: HashSer + Ord + Clone, R: Read>(
    mut reader: R,
    tree_version: u8,
) -> io::Result<MerkleBridge<H>> {
    match tree_version {
        SER_V2 => read_bridge_v1(&mut reader),
        SER_V3 => match reader.read_u8()? {
            SER_V2 => read_bridge_v2(&mut reader),
            flag => Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("Unrecognized bridge serialization version: {:?}", flag),
            )),
        },
        other => Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("Unrecognized tree serialization version: {:?}", other),
        )),
    }
}

#[allow(clippy::needless_borrows_for_generic_args)]
pub fn write_bridge_v2<H: HashSer + Ord, W: Write>(
    mut writer: W,
    bridge: &MerkleBridge<H>,
) -> io::Result<()> {
    Optional::write(&mut writer, bridge.prior_position(), |w, pos| {
        write_position(w, pos)
    })?;
    Vector::write_sized(&mut writer, bridge.tracking().iter(), |w, addr| {
        write_address(w, *addr)
    })?;
    Vector::write_sized(
        &mut writer,
        bridge.ommers().iter(),
        |mut w, (addr, value)| {
            write_address(&mut w, *addr)?;
            value.write(&mut w)
        },
    )?;
    write_nonempty_frontier_v1(&mut writer, bridge.frontier())?;

    Ok(())
}

pub fn write_bridge<H: HashSer + Ord, W: Write>(
    mut writer: W,
    bridge: &MerkleBridge<H>,
) -> io::Result<()> {
    writer.write_u8(SER_V2)?;
    write_bridge_v2(writer, bridge)
}

/// Reads a [`bridgetree::Checkpoint`] as encoded from the `incrementalmerkletree` version `0.3.0`
/// version of the data structure.
#[allow(clippy::needless_borrows_for_generic_args)]
pub fn read_checkpoint_v2<R: Read>(
    mut reader: R,
    checkpoint_id: u32,
) -> io::Result<Checkpoint<u32>> {
    let bridges_len = read_leu64_usize(&mut reader)?;
    let _ = reader.read_u8()? == 1;
    let marked = Vector::read_collected(&mut reader, |r| read_position(r))?;
    let forgotten = Vector::read_collected(&mut reader, |mut r| {
        let pos = read_position(&mut r)?;
        let _ = read_leu64_usize(&mut r)?;
        Ok(pos)
    })?;

    Ok(Checkpoint::from_parts(
        checkpoint_id,
        bridges_len,
        marked,
        forgotten,
    ))
}

/// Reads a [`bridgetree::Checkpoint`] as encoded from the `bridgetree` version `0.2.0`
/// version of the data structure.
pub fn read_checkpoint_v3<R: Read>(mut reader: R) -> io::Result<Checkpoint<u32>> {
    Ok(Checkpoint::from_parts(
        reader.read_u32::<LittleEndian>()?,
        read_leu64_usize(&mut reader)?,
        Vector::read_collected(&mut reader, |r| read_position(r))?,
        Vector::read_collected(&mut reader, |r| read_position(r))?,
    ))
}

pub fn write_checkpoint_v3<W: Write>(
    mut writer: W,
    checkpoint: &Checkpoint<u32>,
) -> io::Result<()> {
    writer.write_u32::<LittleEndian>(*checkpoint.id())?;
    write_usize_leu64(&mut writer, checkpoint.bridges_len())?;
    Vector::write_sized(&mut writer, checkpoint.marked().iter(), |w, p| {
        write_position(w, *p)
    })?;
    Vector::write_sized(&mut writer, checkpoint.forgotten().iter(), |w, pos| {
        write_position(w, *pos)
    })?;

    Ok(())
}

/// Reads a [`BridgeTree`] value from its serialized form.
///
/// Recognizes SER_V2 and SER_V3 (BridgeTree formats). SER_V4 returns an error
/// with a "rescan required" message so the caller can reset the tree gracefully.
#[allow(clippy::needless_borrows_for_generic_args)]
#[allow(clippy::redundant_closure)]
pub fn read_tree<H: Hashable + HashSer + Ord + Clone, const DEPTH: u8, R: Read>(
    mut reader: R,
) -> io::Result<BridgeTree<H, u32, DEPTH>> {
    let tree_version = reader.read_u8()?;

    if tree_version == SER_V4 {
        // ShardTree serialization — unreadable by BridgeTree.
        // Drain the stream so the caller stays aligned, then signal a rescan.
        let mut discard = Vec::new();
        let _ = reader.read_to_end(&mut discard);
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "ShardTree wallet format (SER_V4) detected; rescan required to rebuild witness data",
        ));
    }

    if tree_version == SER_V1 {
        // SER_V1 checkpoint data (pre-NU5 testnet) is not supported.
        // Drain the stream so the caller stays aligned, then signal a rescan.
        let mut discard = Vec::new();
        let _ = reader.read_to_end(&mut discard);
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Reading version 1 checkpoint data is not supported; rescan required",
        ));
    }

    let prior_bridges = Vector::read(&mut reader, |r| read_bridge(r, tree_version))?;
    let current_bridge = Optional::read(&mut reader, |r| read_bridge(r, tree_version))?;
    let saved: BTreeMap<Position, usize> = Vector::read_collected(&mut reader, |mut r| {
        Ok((read_position(&mut r)?, read_leu64_usize(&mut r)?))
    })?;

    let checkpoints = match tree_version {
        SER_V2 => {
            let mut fake_checkpoint_id = 0u32;
            Vector::read_collected_mut(&mut reader, |r| {
                fake_checkpoint_id += 1;
                read_checkpoint_v2(r, fake_checkpoint_id)
            })
        }
        SER_V3 => Vector::read_collected(&mut reader, |r| read_checkpoint_v3(r)),
        flag => Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("Unrecognized tree serialization version: {:?}", flag),
        )),
    }?;
    let max_checkpoints = read_leu64_usize(&mut reader)?;

    BridgeTree::from_parts(
        prior_bridges,
        current_bridge,
        saved,
        checkpoints,
        max_checkpoints,
    )
    .map_err(|err| {
        io::Error::new(
            io::ErrorKind::InvalidData,
            format!(
                "Consistency violation found when attempting to deserialize Merkle tree: {:?}",
                err
            ),
        )
    })
}

#[allow(clippy::needless_borrows_for_generic_args)]
pub fn write_tree<H: Hashable + HashSer + Ord, const DEPTH: u8, W: Write>(
    mut writer: W,
    tree: &BridgeTree<H, u32, DEPTH>,
) -> io::Result<()> {
    writer.write_u8(SER_V3)?;
    Vector::write(&mut writer, tree.prior_bridges(), |w, b| write_bridge(w, b))?;
    Optional::write(&mut writer, tree.current_bridge().as_ref(), |w, b| {
        write_bridge(w, b)
    })?;
    Vector::write_sized(
        &mut writer,
        tree.marked_indices().iter(),
        |mut w, (pos, i)| {
            write_position(&mut w, *pos)?;
            write_usize_leu64(&mut w, *i)
        },
    )?;
    Vector::write_sized(&mut writer, tree.checkpoints().iter(), |w, c| {
        write_checkpoint_v3(w, c)
    })?;
    write_usize_leu64(&mut writer, tree.max_checkpoints())?;

    Ok(())
}
