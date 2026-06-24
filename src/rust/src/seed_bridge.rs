use crate::{
    seed::{
        random_bytes as hd_seed_random_bytes,
        entropy_to_bip39_seed as hd_seed_entropy_to_bip39_seed,
        hd_seed_entropy_to_phrase,
        hd_seed_phrase_to_entropy,
    },
    transparent::{
        ovk_for_shielding_from_taddr as transparent_keys_ovk_for_shielding_from_taddr,
    },
};

#[cxx::bridge]
pub(crate) mod ffi {
    #[namespace = "hd_seed"]
    /// Selects the BIP-39 word list language for mnemonic phrase generation and restoration.
    enum MnemonicLanguage {
        English = 0,
        ChineseSimplified = 1,
        ChineseTraditional = 2,
        French = 3,
        Italian = 4,
        Japanese = 5,
        Korean = 6,
        Spanish = 7,
    }

    #[namespace = "transparent_keys"]
    extern "Rust" {
        #[cxx_name = "ovk_for_shielding_from_taddr"]
        fn transparent_keys_ovk_for_shielding_from_taddr(seed: &[u8], out: &mut [u8; 32]) -> bool;
    }

    #[namespace = "hd_seed"]
    extern "Rust" {
        #[cxx_name = "random_bytes"]
        fn hd_seed_random_bytes(out: &mut [u8; 32]);
        #[cxx_name = "entropy_to_phrase"]
        fn hd_seed_entropy_to_phrase(entropy: &[u8], lang: MnemonicLanguage) -> String;
        #[cxx_name = "phrase_to_entropy"]
        fn hd_seed_phrase_to_entropy(phrase: &str, lang: MnemonicLanguage, out: &mut [u8]) -> bool;
        #[cxx_name = "entropy_to_bip39_seed"]
        fn hd_seed_entropy_to_bip39_seed(entropy: &[u8], out: &mut [u8; 64]) -> bool;
    }
}
