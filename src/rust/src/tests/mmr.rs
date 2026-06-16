use zcash_history::{Entry, EntryLink, NodeData, V1};

use crate::history::{append, remove};

const NODE_DATA_16L: &[u8] = include_bytes!("./res/tree16.dat");
const NODE_DATA_1023L: &[u8] = include_bytes!("./res/tree1023.dat");

struct TreeView {
    peaks: Vec<(u32, Entry<V1>)>,
    extra: Vec<(u32, Entry<V1>)>,
}

fn draft(into: &mut Vec<(u32, Entry<V1>)>, nodes: &[NodeData], peak_pos: usize, h: u32) {
    let node_data = nodes[peak_pos - 1].clone();
    let peak: Entry<V1> = match h {
        0 => Entry::new_leaf(node_data),
        _ => Entry::new(
            node_data,
            EntryLink::Stored((peak_pos - (1 << h) - 1) as u32),
            EntryLink::Stored((peak_pos - 2) as u32),
        ),
    };
    into.push(((peak_pos - 1) as u32, peak));
}

fn prepare_tree(nodes: &[NodeData]) -> TreeView {
    assert!(!nodes.is_empty());

    let mut h = (32 - ((nodes.len() + 1) as u32).leading_zeros() - 1) - 1;
    let mut peak_pos = (1 << (h + 1)) - 1;
    let mut peaks = Vec::new();

    let mut last_peak_pos = 0;
    let mut last_peak_h = 0;

    loop {
        if peak_pos > nodes.len() {
            peak_pos -= 1 << h;
            h -= 1;
        }

        if peak_pos <= nodes.len() {
            draft(&mut peaks, nodes, peak_pos, h);
            last_peak_pos = peak_pos;
            last_peak_h = h;
            peak_pos += (1 << (h + 1)) - 1;
        }

        if h == 0 {
            break;
        }
    }

    let mut extra = Vec::new();
    let mut h = last_peak_h;
    let mut peak_pos = last_peak_pos;

    while h > 0 {
        let left_pos = peak_pos - (1 << h);
        let right_pos = peak_pos - 1;
        h -= 1;
        draft(&mut extra, nodes, left_pos, h);
        draft(&mut extra, nodes, right_pos, h);
        peak_pos = right_pos;
    }

    TreeView { peaks, extra }
}

fn preload_tree_append(nodes: &[NodeData]) -> (Vec<u32>, Vec<[u8; zcash_history::MAX_ENTRY_SIZE]>) {
    let tree_view = prepare_tree(nodes);
    let mut indices = Vec::new();
    let mut bytes = Vec::new();
    for (idx, entry) in tree_view.peaks {
        let mut buf = [0u8; zcash_history::MAX_ENTRY_SIZE];
        entry.write(&mut &mut buf[..]).expect("write failed");
        indices.push(idx);
        bytes.push(buf);
    }
    (indices, bytes)
}

fn preload_tree_delete(
    nodes: &[NodeData],
) -> (Vec<u32>, Vec<[u8; zcash_history::MAX_ENTRY_SIZE]>, usize) {
    let tree_view = prepare_tree(nodes);
    let mut indices = Vec::new();
    let mut bytes = Vec::new();
    let peak_count = tree_view.peaks.len();
    for (idx, entry) in tree_view.peaks.into_iter().chain(tree_view.extra) {
        let mut buf = [0u8; zcash_history::MAX_ENTRY_SIZE];
        entry.write(&mut &mut buf[..]).expect("write failed");
        indices.push(idx);
        bytes.push(buf);
    }
    (indices, bytes, peak_count)
}

fn load_nodes(bytes: &'static [u8]) -> Vec<NodeData> {
    let mut res = Vec::new();
    let mut cursor = std::io::Cursor::new(bytes);
    while (cursor.position() as usize) < bytes.len() {
        res.push(
            zcash_history::NodeData::read(0, &mut cursor)
                .expect("Statically checked to be correct"),
        );
    }
    res
}

#[test]
fn append_test() {
    let nodes = load_nodes(NODE_DATA_16L);
    let (indices, peaks) = preload_tree_append(&nodes);

    let mut new_node_data = [0u8; zcash_history::MAX_NODE_DATA_SIZE];
    let new_node = NodeData {
        consensus_branch_id: 0,
        subtree_commitment: [0u8; 32],
        start_time: 101,
        end_time: 110,
        start_target: 190,
        end_target: 200,
        start_sapling_root: [0u8; 32],
        end_sapling_root: [0u8; 32],
        subtree_total_work: Default::default(),
        start_height: 10,
        end_height: 10,
        sapling_tx: 13,
    };
    new_node.write(&mut &mut new_node_data[..]).expect("write failed");

    let mut buf_ret = vec![[0u8; zcash_history::MAX_NODE_DATA_SIZE]; 32];

    let effect = append(0, nodes.len() as u32, &indices, &peaks, &new_node_data, &mut buf_ret)
        .expect("append should succeed");

    assert_eq!(effect.count, 2);

    let n1 = NodeData::from_bytes(0, &buf_ret[0][..]).expect("node #1");
    let n2 = NodeData::from_bytes(0, &buf_ret[1][..]).expect("node #2");

    assert_eq!(n1.start_height, 10);
    assert_eq!(n1.end_height, 10);
    assert_eq!(n2.start_height, 9);
    assert_eq!(n2.end_height, 10);
    assert_eq!(n2.sapling_tx, 27);
}

#[test]
fn delete_test() {
    let nodes = load_nodes(NODE_DATA_1023L);
    let (indices, node_bytes, peak_count) = preload_tree_delete(&nodes);

    let effect = remove(0, nodes.len() as u32, &indices, &node_bytes, peak_count)
        .expect("remove should succeed");

    // Deleting from a full tree of height 9 results in cascade-deleting 10 nodes.
    assert_eq!(effect.count, 10);
}
