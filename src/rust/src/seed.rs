//! HD seed generation, mnemonic phrase encoding/decoding, and BIP-39 seed derivation.
//!
//! Provides the four operations needed by the C++ HDSeed and key-derivation layer:
//!   - Cryptographically secure random bytes for seed generation.
//!   - BIP-39 entropy → mnemonic phrase conversion (12 / 18 / 24 words).
//!   - BIP-39 mnemonic phrase → entropy restoration.
//!   - BIP-39 entropy → 64-byte PBKDF2 seed (used as input to zip32 master key derivation).
//!
//! All functions are exposed to C++ through the `hd_seed` bridge namespace defined in bridge.rs.
//! Language-sensitive functions accept a `bip39::Language`; the bridge wrappers in this module
//! map from the `hd_seed::MnemonicLanguage` cxx shared enum (defined in seed_bridge.rs) via
//! `lang_to_bip39`.

use bip39::{Language, Mnemonic, Seed};
use rand_core::{OsRng, RngCore};
use crate::seed_bridge::ffi::MnemonicLanguage;

/// Fills `out` with 32 cryptographically secure random bytes via the OS CSPRNG.
///
/// Backed by Rust's `OsRng` / the `getrandom` crate. The first call may block
/// briefly until the OS entropy pool is ready.
pub fn random_bytes(out: &mut [u8; 32]) {
    OsRng.fill_bytes(out);
}

/// Derives the BIP-39 mnemonic phrase from raw entropy bytes in the requested language.
///
/// `entropy` must be 16, 24, or 32 bytes, corresponding to 12-, 18-, or 24-word
/// mnemonics respectively. Returns an error string if the entropy length is invalid.
pub fn entropy_to_phrase(entropy: &[u8], lang: Language) -> String {
    match Mnemonic::from_entropy(entropy, lang) {
        Ok(m) => m.phrase().to_string(),
        Err(_) => "Internal error: The HDseed length is invalid.".to_string(),
    }
}

/// Restores raw entropy from a BIP-39 mnemonic phrase into `out`.
///
/// `lang` must match the language the phrase was originally generated with.
/// `out` must be exactly 16, 24, or 32 bytes (matching the word count of the phrase).
/// Returns `true` on success, `false` if the phrase is invalid or the recovered
/// entropy length does not match `out.len()`.
pub fn phrase_to_entropy(phrase: &str, lang: Language, out: &mut [u8]) -> bool {
    match Mnemonic::from_phrase(phrase, lang) {
        Ok(m) => {
            let entropy = m.entropy();
            if entropy.len() != out.len() {
                return false;
            }
            out.copy_from_slice(entropy);
            true
        }
        Err(_) => false,
    }
}

/// Derives the 64-byte BIP-39 PBKDF2 seed from raw entropy bytes (empty passphrase).
///
/// `entropy` must be 16, 24, or 32 bytes. The resulting 64-byte buffer is used as
/// input to zip32 master spending key derivation when BIP-39 mode is enabled.
/// Language does not affect PBKDF2 derivation — only the entropy and passphrase matter.
/// Returns `true` on success, `false` if the entropy length is invalid.
pub fn entropy_to_bip39_seed(entropy: &[u8], out: &mut [u8; 64]) -> bool {
    // Language::English is used here only to reconstruct the Mnemonic object for
    // the Seed::new call; the PBKDF2 output is independent of the word-list language.
    match Mnemonic::from_entropy(entropy, Language::English) {
        Ok(m) => {
            let seed = Seed::new(&m, "");
            out.copy_from_slice(seed.as_bytes());
            true
        }
        Err(_) => false,
    }
}

/// Maps the cxx-bridge `hd_seed::MnemonicLanguage` shared enum to a `bip39::Language`.
pub(crate) fn lang_to_bip39(lang: MnemonicLanguage) -> Language {
    match lang {
        MnemonicLanguage::ChineseSimplified  => Language::ChineseSimplified,
        MnemonicLanguage::ChineseTraditional => Language::ChineseTraditional,
        MnemonicLanguage::French             => Language::French,
        MnemonicLanguage::Italian            => Language::Italian,
        MnemonicLanguage::Japanese           => Language::Japanese,
        MnemonicLanguage::Korean             => Language::Korean,
        MnemonicLanguage::Spanish            => Language::Spanish,
        _                                    => Language::English,
    }
}

/// Bridge wrapper: derives the mnemonic phrase from entropy in the requested language.
pub(crate) fn hd_seed_entropy_to_phrase(entropy: &[u8], lang: MnemonicLanguage) -> String {
    entropy_to_phrase(entropy, lang_to_bip39(lang))
}

/// Bridge wrapper: restores entropy from a mnemonic phrase (must match the original language).
pub(crate) fn hd_seed_phrase_to_entropy(phrase: &str, lang: MnemonicLanguage, out: &mut [u8]) -> bool {
    phrase_to_entropy(phrase, lang_to_bip39(lang), out)
}
