use crate::sapling_protocol::sapling_keys::{
    check_diversifier,
    ivk_to_address as sapling_keys_ivk_to_address,
    ivk_to_address_from_index as sapling_keys_ivk_to_address_from_index,
    sk_to_expsk as sapling_keys_sk_to_expsk,
    expsk_to_fvk as sapling_keys_expsk_to_fvk,
    expsk_to_default_address as sapling_keys_expsk_to_default_address,
    expsk_to_default_address_internal as sapling_keys_expsk_to_default_address_internal,
    fvk_to_ivk as sapling_keys_fvk_to_ivk,
    fvk_to_ivk_internal as sapling_keys_fvk_to_ivk_internal,
    fvk_to_default_address as sapling_keys_fvk_to_default_address,
    fvk_to_default_address_internal as sapling_keys_fvk_to_default_address_internal,
    dfvk_to_change_address as sapling_keys_dfvk_to_change_address,
    dfvk_to_ivk_internal as sapling_keys_dfvk_to_ivk_internal,
    dfvk_to_nk_internal as sapling_keys_dfvk_to_nk_internal,
    dfvk_to_ovk_internal as sapling_keys_dfvk_to_ovk_internal,
    dfvk_to_address_internal as sapling_keys_dfvk_to_address_internal,
    dfvk_to_address_from_index_internal as sapling_keys_dfvk_to_address_from_index_internal,
    fvk_to_address as sapling_keys_fvk_to_address,
    fvk_to_address_internal as sapling_keys_fvk_to_address_internal,
    fvk_to_address_from_index as sapling_keys_fvk_to_address_from_index,
    fvk_to_address_from_index_internal as sapling_keys_fvk_to_address_from_index_internal,
    xsk_derive_internal as sapling_keys_xsk_derive_internal,
};

#[cxx::bridge]
pub(crate) mod ffi {
    #[namespace = "sapling_keys"]
    extern "Rust" {
        fn check_diversifier(diversifier: &[u8; 11]) -> bool;
        #[cxx_name = "ivk_to_address"]
        fn sapling_keys_ivk_to_address(ivk: &[u8; 32], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "ivk_to_address_from_index"]
        fn sapling_keys_ivk_to_address_from_index(ivk: &[u8; 32], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "sk_to_expsk"]
        fn sapling_keys_sk_to_expsk(sk: &[u8; 32], out: &mut [u8; 96]) -> bool;
        #[cxx_name = "expsk_to_fvk"]
        fn sapling_keys_expsk_to_fvk(expsk: &[u8; 96], out: &mut [u8; 96]) -> bool;
        #[cxx_name = "expsk_to_default_address"]
        fn sapling_keys_expsk_to_default_address(expsk: &[u8; 96], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "expsk_to_default_address_internal"]
        fn sapling_keys_expsk_to_default_address_internal(expsk: &[u8; 96], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "fvk_to_ivk"]
        fn sapling_keys_fvk_to_ivk(fvk: &[u8; 96], out: &mut [u8; 32]) -> bool;
        #[cxx_name = "fvk_to_ivk_internal"]
        fn sapling_keys_fvk_to_ivk_internal(fvk: &[u8; 96], out: &mut [u8; 32]) -> bool;
        #[cxx_name = "fvk_to_default_address"]
        fn sapling_keys_fvk_to_default_address(fvk: &[u8; 96], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "fvk_to_default_address_internal"]
        fn sapling_keys_fvk_to_default_address_internal(fvk: &[u8; 96], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "dfvk_to_change_address"]
        fn sapling_keys_dfvk_to_change_address(dfvk: &[u8; 128], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "dfvk_to_ivk_internal"]
        fn sapling_keys_dfvk_to_ivk_internal(dfvk: &[u8; 128], out: &mut [u8; 32]) -> bool;
        #[cxx_name = "dfvk_to_nk_internal"]
        fn sapling_keys_dfvk_to_nk_internal(dfvk: &[u8; 128], out: &mut [u8; 32]) -> bool;
        #[cxx_name = "dfvk_to_ovk_internal"]
        fn sapling_keys_dfvk_to_ovk_internal(dfvk: &[u8; 128], out: &mut [u8; 32]) -> bool;
        #[cxx_name = "dfvk_to_address_internal"]
        fn sapling_keys_dfvk_to_address_internal(dfvk: &[u8; 128], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "dfvk_to_address_from_index_internal"]
        fn sapling_keys_dfvk_to_address_from_index_internal(dfvk: &[u8; 128], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "fvk_to_address"]
        fn sapling_keys_fvk_to_address(fvk: &[u8; 96], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "fvk_to_address_internal"]
        fn sapling_keys_fvk_to_address_internal(fvk: &[u8; 96], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "fvk_to_address_from_index"]
        fn sapling_keys_fvk_to_address_from_index(fvk: &[u8; 96], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "fvk_to_address_from_index_internal"]
        fn sapling_keys_fvk_to_address_from_index_internal(fvk: &[u8; 96], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "xsk_derive_internal"]
        fn sapling_keys_xsk_derive_internal(xsk: &[u8; 169]) -> [u8; 169];
    }
}
