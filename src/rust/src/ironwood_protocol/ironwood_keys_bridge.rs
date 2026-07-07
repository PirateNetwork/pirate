use crate::ironwood_protocol::ironwood_keys::{
    ivk_to_address as ironwood_keys_ivk_to_address,
    ivk_to_address_from_index as ironwood_keys_ivk_to_address_from_index,
    fvk_to_ovk, fvk_to_ovk_internal,
    fvk_to_ivk as ironwood_keys_fvk_to_ivk,
    fvk_to_ivk_internal as ironwood_keys_fvk_to_ivk_internal,
    fvk_to_default_address, fvk_to_default_address_internal,
    fvk_to_address, fvk_to_address_internal,
    fvk_to_address_from_index, fvk_to_address_from_index_internal,
    sk_is_valid, sk_to_fvk,
    sk_to_default_address, sk_to_default_address_internal,
    derive_master_key, derive_child_key,
};

#[cxx::bridge]
pub(crate) mod ffi {
    #[namespace = "ironwood_keys"]
    extern "Rust" {
        #[cxx_name = "ivk_to_address"]
        fn ironwood_keys_ivk_to_address(ivk: &[u8; 64], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "ivk_to_address_from_index"]
        fn ironwood_keys_ivk_to_address_from_index(ivk: &[u8; 64], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool;
        fn fvk_to_ovk(fvk: &[u8; 96], out: &mut [u8; 32]) -> bool;
        fn fvk_to_ovk_internal(fvk: &[u8; 96], out: &mut [u8; 32]) -> bool;
        #[cxx_name = "fvk_to_ivk"]
        fn ironwood_keys_fvk_to_ivk(fvk: &[u8; 96], out: &mut [u8; 64]) -> bool;
        #[cxx_name = "fvk_to_ivk_internal"]
        fn ironwood_keys_fvk_to_ivk_internal(fvk: &[u8; 96], out: &mut [u8; 64]) -> bool;
        fn fvk_to_default_address(fvk: &[u8; 96], out: &mut [u8; 43]) -> bool;
        fn fvk_to_default_address_internal(fvk: &[u8; 96], out: &mut [u8; 43]) -> bool;
        fn fvk_to_address(fvk: &[u8; 96], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool;
        fn fvk_to_address_internal(fvk: &[u8; 96], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool;
        fn fvk_to_address_from_index(fvk: &[u8; 96], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool;
        fn fvk_to_address_from_index_internal(fvk: &[u8; 96], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool;
        fn sk_is_valid(sk: &[u8; 32]) -> bool;
        fn sk_to_fvk(sk: &[u8; 32], out: &mut [u8; 96]) -> bool;
        fn sk_to_default_address(sk: &[u8; 32], out: &mut [u8; 43]) -> bool;
        fn sk_to_default_address_internal(sk: &[u8; 32], out: &mut [u8; 43]) -> bool;
        fn derive_master_key(seed: &[u8], out: &mut [u8; 73]) -> bool;
        fn derive_child_key(xsk: &[u8; 73], coin_type: u32, account: u32, out: &mut [u8; 73]) -> bool;
    }
}
