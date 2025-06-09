// This file ensures that all FFI functions are properly exported
// when building the Rust library for different architectures

// Re-export all modules with FFI functions
pub use crate::rustzcash::*;
pub use crate::orchard_actions::*;
pub use crate::transaction_ffi::*;
pub use crate::orchard_wallet::*;
pub use crate::orchard_keys_ffi::*;
pub use crate::orchard_ffi::*;
pub use crate::orchard_bundle::*;
pub use crate::builder_ffi::*;
pub use crate::bundlecache::*;
pub use crate::history::*;
pub use crate::incremental_merkle_tree::*;
pub use crate::merkle_frontier::*;
pub use crate::params::*;
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
#[path = "metrics_ffi.rs"]
mod metrics_ffi;
#[path = "streams_ffi.rs"]
mod streams_ffi;
#[path = "tracing_ffi.rs"]
mod tracing_ffi;
#[path = "zcashd_orchard.rs"]
mod zcashd_orchard;
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
#[path = "orchard_actions.rs"]
mod orchard_actions;
#[path = "orchard_bundle.rs"]
mod orchard_bundle;
#[path = "orchard_ffi.rs"]
mod orchard_ffi;
#[path = "orchard_keys_ffi.rs"]
mod orchard_keys_ffi;
#[path = "orchard_keys.rs"]
mod orchard_keys;
#[path = "params.rs"]
mod params;
#[path = "sapling.rs"]
mod sapling;
#[path = "sprout.rs"]
mod sprout;
#[path = "streams.rs"]
mod streams;
#[path = "transaction_ffi.rs"]
mod transaction_ffi;
#[path = "sapling_wallet.rs"]
mod sapling_wallet;
#[path = "orchard_wallet.rs"]
mod orchard_wallet;
#[path = "test_harness_ffi.rs"]
mod test_harness_ffi; 