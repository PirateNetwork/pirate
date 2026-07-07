// This file ensures that all FFI functions are properly exported
// when building the Rust library for different architectures

// Re-export all modules with FFI functions
pub use crate::rustzcash::*;
pub use crate::ironwood_protocol::*;
pub use crate::transaction_ffi::*;
pub use crate::builder_ffi::*;
pub use crate::bundlecache::*;
pub use crate::history::*;
pub use crate::incremental_merkle_tree::*;
pub use crate::merkle_frontier::*;
pub use crate::sapling_protocol::*;
pub use crate::sprout::*;
pub use crate::streams::*;

// Include the main rustzcash module
#[path = "rustzcash.rs"]
mod rustzcash;

// Include all other modules
#[path = "crypto/blake2b.rs"]
mod blake2b;
#[path = "streams_ffi.rs"]
mod streams_ffi;
#[path = "tracing_ffi.rs"]
mod tracing_ffi;
#[path = "bridge.rs"]
mod bridge;
#[path = "seed_bridge.rs"]
mod seed_bridge;
#[path = "builder_ffi.rs"]
mod builder_ffi;
#[path = "bundlecache.rs"]
mod bundlecache;
#[path = "history.rs"]
mod history;
#[path = "incremental_merkle_tree.rs"]
mod incremental_merkle_tree;
#[path = "merkle_frontier.rs"]
mod merkle_frontier;
#[path = "ironwood_protocol/ironwood_protocol.rs"]
mod ironwood_protocol;
#[path = "sapling_protocol/sapling_protocol.rs"]
mod sapling_protocol;
#[path = "sprout_protocol/sprout.rs"]
mod sprout;
#[path = "streams.rs"]
mod streams;
#[path = "transaction_ffi.rs"]
mod transaction_ffi;
#[path = "test_harness_ffi.rs"]
mod test_harness_ffi; 