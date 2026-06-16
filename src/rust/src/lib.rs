// This file ensures that all FFI functions are properly exported
// when building the Rust library for different architectures

// Re-export all modules with FFI functions
pub use crate::rustzcash::*;
pub use crate::equihash::*;
pub use crate::orchard_actions::*;
pub use crate::transaction_ffi::*;
pub use crate::orchard_wallet::*;
pub use crate::orchard_ffi::*;
pub use crate::orchard_bundle::*;
pub use crate::builder_ffi::*;
pub use crate::bundlecache::*;
pub use crate::history::*;
pub use crate::incremental_merkle_tree::*;
pub use crate::merkle_frontier::*;
pub use crate::sapling::*;
pub use crate::sprout::*;
pub use crate::streams::*;
pub use crate::sapling_wallet::*;

// Include the main rustzcash module
#[path = "rustzcash.rs"]
mod rustzcash;

// Include all other modules
#[path = "blake2b.rs"]
mod blake2b;
#[path = "ed25519.rs"]
mod ed25519;
#[path = "equihash.rs"]
mod equihash;
#[path = "streams_ffi.rs"]
mod streams_ffi;
#[path = "tracing_ffi.rs"]
mod tracing_ffi;
#[path = "bridge.rs"]
mod bridge;
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
#[path = "orchard_protocol/orchard_actions.rs"]
mod orchard_actions;
#[path = "orchard_protocol/orchard_bundle.rs"]
mod orchard_bundle;
#[path = "orchard_protocol/orchard_validator.rs"]
mod orchard_ffi;
#[path = "orchard_protocol/orchard_keys.rs"]
mod orchard_keys;
#[path = "sapling_protocol/sapling_keys.rs"]
mod sapling_keys;
#[path = "sapling_protocol/sapling.rs"]
mod sapling;
#[path = "sprout_protocol/sprout.rs"]
mod sprout;
#[path = "streams.rs"]
mod streams;
#[path = "transaction_ffi.rs"]
mod transaction_ffi;
#[path = "sapling_protocol/sapling_wallet.rs"]
mod sapling_wallet;
#[path = "orchard_protocol/orchard_wallet.rs"]
mod orchard_wallet;
#[path = "test_harness_ffi.rs"]
mod test_harness_ffi; 