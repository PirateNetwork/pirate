// Ironwood protocol module hub — mirrors the orchard_protocol pattern.
//
// SCAFFOLDING ONLY: nothing in this module is reachable from C++ yet. There is no v6
// transaction format, no IRONWOOD_VERSION_GROUP_ID/tx-version gate, and no consensus
// activation logic wired up on the C++ side (only the inert UPGRADE_IRONWOOD skeleton in
// consensus/upgrades.h). This exists so the FFI shape for the Ironwood pool can be built
// and compiled against incrementally, the same way orchard_protocol was originally built.
#![allow(unused_imports)]
#![allow(dead_code)]

pub(crate) mod ironwood_bundle;
pub(crate) mod ironwood_validator;

pub(crate) use ironwood_bundle::*;
pub(crate) use ironwood_validator::*;
