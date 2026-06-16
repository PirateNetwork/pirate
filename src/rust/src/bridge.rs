use crate::{
    bundlecache::init as bundlecache_init,
    merkle_frontier::{
        new_orchard, new_sapling, orchard_empty_root, parse_orchard, parse_sapling,
        sapling_empty_root, OrchardFrontier, OrchardWallet, SaplingFrontier, SaplingWallet,
    },
    orchard_protocol::{
        orchard_bundle::{
            none_orchard_bundle, orchard_bundle_from_raw_box, parse_orchard_bundle, Action,
            Bundle as OrchardBundle,
        },
        orchard_actions::{
            compute_nullifier as compute_nullifier_orchard,
            derive_orchard_ock, try_orchard_decrypt_action_ock,
        },
        orchard_validator::{orchard_batch_validation_init, BatchValidator as OrchardBatchValidator},
        orchard_keys::{
            ivk_to_address as orchard_keys_ivk_to_address,
            ivk_to_address_from_index as orchard_keys_ivk_to_address_from_index,
            fvk_to_ovk, fvk_to_ovk_internal,
            fvk_to_ivk as orchard_keys_fvk_to_ivk, fvk_to_ivk_internal as orchard_keys_fvk_to_ivk_internal,
            fvk_to_default_address, fvk_to_default_address_internal,
            fvk_to_address, fvk_to_address_internal,
            fvk_to_address_from_index, fvk_to_address_from_index_internal,
            sk_is_valid, sk_to_fvk,
            sk_to_default_address, sk_to_default_address_internal,
            derive_master_key, derive_child_key,
        },
    },
    builder_ffi::shielded_signature_digest,
    sapling_protocol::{
        apply_sapling_bundle_signatures, build_sapling_bundle, compute_nullifier,
        derive_sapling_ock,
        finish_bundle_assembly, new_bundle_assembler, new_sapling_builder, none_sapling_bundle,
        parse_v4_sapling_components, parse_v4_sapling_output, parse_v4_sapling_spend,
        parse_v5_sapling_bundle,
        sapling_validator::{init_batch_validator as init_sapling_batch_validator, BatchValidator as SaplingBatchValidator},
        init_verifier, Bundle as SaplingBundle, BundleAssembler as SaplingBundleAssembler, Output,
        SaplingBuilder, SaplingUnauthorizedBundle, Spend, Verifier,
    },
    sapling_protocol::sapling_keys::{
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
    },
    transparent::{
        ovk_for_shielding_from_taddr as transparent_keys_ovk_for_shielding_from_taddr,
    },
    seed::{
        random_bytes as hd_seed_random_bytes,
        entropy_to_bip39_seed as hd_seed_entropy_to_bip39_seed,
        hd_seed_entropy_to_phrase,
        hd_seed_phrase_to_entropy,
    },
    streams::{
        from_auto_file, from_blake2b_writer, from_buffered_file, from_data, from_hash_writer,
        from_secure_data, from_size_computer, CppStream,
    },
    test_harness_ffi::{
        test_only_invalid_sapling_bundle, test_only_replace_sapling_nullifier,
        test_only_replace_sapling_output_parts,
    },
};

#[allow(clippy::needless_lifetimes)]
#[cxx::bridge]
pub(crate) mod ffi {

    extern "C++" {
        include!("hash.h");
        include!("streams.h");

        #[cxx_name = "RustDataStream"]
        type RustStream = crate::streams::ffi::RustStream;
        #[cxx_name = "SecureRustDataStream"]
        type SecureRustStream = crate::streams::ffi::SecureRustStream;
        type CAutoFile = crate::streams::ffi::CAutoFile;
        type CBufferedFile = crate::streams::ffi::CBufferedFile;
        type CHashWriter = crate::streams::ffi::CHashWriter;
        type CBLAKE2bWriter = crate::streams::ffi::CBLAKE2bWriter;
        type CSizeComputer = crate::streams::ffi::CSizeComputer;
    }
    #[namespace = "stream"]
    extern "Rust" {
        type CppStream<'a>;

        fn from_data(stream: Pin<&mut RustStream>) -> Box<CppStream<'_>>;
        fn from_secure_data(stream: Pin<&mut SecureRustStream>) -> Box<CppStream<'_>>;
        fn from_auto_file(file: Pin<&mut CAutoFile>) -> Box<CppStream<'_>>;
        fn from_buffered_file(file: Pin<&mut CBufferedFile>) -> Box<CppStream<'_>>;
        fn from_hash_writer(writer: Pin<&mut CHashWriter>) -> Box<CppStream<'_>>;
        fn from_blake2b_writer(writer: Pin<&mut CBLAKE2bWriter>) -> Box<CppStream<'_>>;
        fn from_size_computer(sc: Pin<&mut CSizeComputer>) -> Box<CppStream<'_>>;
    }

    #[namespace = "libzcash"]
    unsafe extern "C++" {
        include!("zcash/cache.h");

        type BundleValidityCache;

        fn NewBundleValidityCache(kind: &str, bytes: usize) -> UniquePtr<BundleValidityCache>;
        fn insert(self: Pin<&mut BundleValidityCache>, entry: [u8; 32]);
        fn contains(&self, entry: &[u8; 32], erase: bool) -> bool;
    }
    #[namespace = "bundlecache"]
    extern "Rust" {
        #[rust_name = "bundlecache_init"]
        fn init(cache_bytes: usize);
    }

    unsafe extern "C++" {
        include!("rust/pointers.h");
        type OrchardBundlePtr;
        type SaplingBundlePtr;
        type OutputPtr;
        type ActionPtr;
    }

    #[namespace = "sapling"]
    extern "Rust" {
        type Spend;

        #[cxx_name = "parse_v4_spend"]
        fn parse_v4_sapling_spend(bytes: &[u8]) -> Result<Box<Spend>>;
        fn cv(self: &Spend) -> [u8; 32];
        fn anchor(self: &Spend) -> [u8; 32];
        fn nullifier(self: &Spend) -> [u8; 32];
        fn rk(self: &Spend) -> [u8; 32];
        fn zkproof(self: &Spend) -> [u8; 192];
        fn spend_auth_sig(self: &Spend) -> [u8; 64];

        type Output;

        #[cxx_name = "parse_v4_output"]
        fn parse_v4_sapling_output(bytes: &[u8]) -> Result<Box<Output>>;
        fn as_ptr(self: &Output) -> *const OutputPtr;
        fn cv(self: &Output) -> [u8; 32];
        fn cmu(self: &Output) -> [u8; 32];
        fn ephemeral_key(self: &Output) -> [u8; 32];
        fn enc_ciphertext(self: &Output) -> [u8; 580];
        fn out_ciphertext(self: &Output) -> [u8; 80];
        fn zkproof(self: &Output) -> [u8; 192];
        fn serialize_v4(self: &Output, stream: &mut CppStream<'_>) -> Result<()>;
        
        fn compute_nullifier(
            self: &Output,
            ivk: &[u8; 32],
            ak: &[u8; 32],
            nk: &[u8; 32],
            position: u64,
            result: &mut [u8; 32],
        ) -> bool;
        
        fn try_decrypt_output_ivk(
            self: &Output,
            ivk_bytes: &[u8; 32],

            value_out: &mut u64,
            diversifier_out: &mut [u8; 11],
            pk_d_out: &mut [u8; 32],
            memo_out: &mut [u8; 512],
            rseed_out: &mut [u8; 32],
            leadbyte_out: &mut u8,
            cmu_out: &mut [u8; 32],
            rcm_out: &mut [u8; 32],
        ) -> bool;
        
        fn try_decrypt_output_ovk(
            self: &Output,
            ovk_bytes: &[u8; 32],

            value_out: &mut u64,
            diversifier_out: &mut [u8; 11],
            pk_d_out: &mut [u8; 32],
            memo_out: &mut [u8; 512],
            rseed_out: &mut [u8; 32],
            leadbyte_out: &mut u8,
            cmu_out: &mut [u8; 32],
            rcm_out: &mut [u8; 32],
        ) -> bool;

        fn try_decrypt_output_ock(
            self: &Output,
            ock: &[u8; 32],

            value_out: &mut u64,
            diversifier_out: &mut [u8; 11],
            pk_d_out: &mut [u8; 32],
            memo_out: &mut [u8; 512],
            rseed_out: &mut [u8; 32],
            leadbyte_out: &mut u8,
            cmu_out: &mut [u8; 32],
            rcm_out: &mut [u8; 32],
        ) -> bool;

        #[cxx_name = "SaplingBundle"]
        type SaplingBundle;

        #[cxx_name = "none_bundle"]
        fn none_sapling_bundle() -> Box<SaplingBundle>;
        fn box_clone(self: &SaplingBundle) -> Box<SaplingBundle>;
        #[cxx_name = "parse_v5_bundle"]
        fn parse_v5_sapling_bundle(stream: &mut CppStream<'_>) -> Result<Box<SaplingBundle>>;
        fn serialize_v4_components(
            self: &SaplingBundle,
            stream: &mut CppStream<'_>,
            has_sapling: bool,
        ) -> Result<()>;
        fn serialize_v5(self: &SaplingBundle, stream: &mut CppStream<'_>) -> Result<()>;
        fn as_ptr(self: &SaplingBundle) -> *const SaplingBundlePtr;
        fn recursive_dynamic_usage(self: &SaplingBundle) -> usize;
        fn is_present(self: &SaplingBundle) -> bool;
        fn spends(self: &SaplingBundle) -> Vec<Spend>;
        fn get_spend(self: &SaplingBundle, spend_index: usize) -> Result<Box<Spend>>;
        fn outputs(self: &SaplingBundle) -> Vec<Output>;
        fn get_output(self: &SaplingBundle, out_index: usize) -> Result<Box<Output>>;
        fn num_spends(self: &SaplingBundle) -> usize;
        fn num_outputs(self: &SaplingBundle) -> usize;
        fn value_balance_zat(self: &SaplingBundle) -> i64;
        fn binding_sig(self: &SaplingBundle) -> [u8; 64];

        #[cxx_name = "test_only_invalid_bundle"]
        fn test_only_invalid_sapling_bundle(
            spends: usize,
            outputs: usize,
            value_balance: i64,
        ) -> Box<SaplingBundle>;
        #[cxx_name = "test_only_replace_nullifier"]
        fn test_only_replace_sapling_nullifier(
            bundle: &mut SaplingBundle,
            spend_index: usize,
            nullifier: [u8; 32],
        );
        #[cxx_name = "test_only_replace_output_parts"]
        fn test_only_replace_sapling_output_parts(
            bundle: &mut SaplingBundle,
            output_index: usize,
            cmu: [u8; 32],
            enc_ciphertext: [u8; 580],
            out_ciphertext: [u8; 80],
        );

        #[rust_name = "SaplingBundleAssembler"]
        type BundleAssembler;

        fn new_bundle_assembler() -> Box<SaplingBundleAssembler>;
        fn add_spend_to_assembler(self: &mut SaplingBundleAssembler, spend_bytes: [u8; 384]) -> bool;
        fn add_output_to_assembler(self: &mut SaplingBundleAssembler, output_bytes: [u8; 948]) -> bool;
        fn add_value_balance_to_assembler(self: &mut SaplingBundleAssembler, value_balance: i64) -> bool;
        #[cxx_name = "parse_v4_components"]
        fn parse_v4_sapling_components(
            stream: &mut CppStream<'_>,
            has_sapling: bool,
        ) -> Result<Box<SaplingBundleAssembler>>;
        fn have_actions(self: &SaplingBundleAssembler) -> bool;
        fn finish_bundle_assembly(
            assembler: Box<SaplingBundleAssembler>,
            binding_sig: [u8; 64],
        ) -> Box<SaplingBundle>;

        #[cxx_name = "Builder"]
        type SaplingBuilder;

        #[cxx_name = "new_builder"]
        fn new_sapling_builder(anchor: [u8; 32], coinbase: bool) -> Result<Box<SaplingBuilder>>;
        fn add_spend(
            self: &mut SaplingBuilder,
            extsk: &[u8],
            recipient: [u8; 43],
            value: u64,
            rcm: [u8; 32],
            merkle_path: [u8; 1065],
        ) -> Result<()>;
        fn add_recipient(
            self: &mut SaplingBuilder,
            ovk: [u8; 32],
            to: [u8; 43],
            value: u64,
            memo: [u8; 512],
        ) -> Result<()>;
        #[cxx_name = "build_bundle"]
        fn build_sapling_bundle(
            builder: Box<SaplingBuilder>,
        ) -> Result<Box<SaplingUnauthorizedBundle>>;

        fn compute_nullifier(
            diversifier: &[u8; 11],
            pk_d: &[u8; 32],
            value: u64,
            rcm: &[u8; 32],
            ak: &[u8; 32],
            nk: &[u8; 32],
            position: u64,
            result: &mut [u8; 32],
        ) -> bool;

        fn derive_sapling_ock(
            ovk: &[u8; 32],
            cv: &[u8; 32],
            cmu: &[u8; 32],
            epk: &[u8; 32],
            ock_out: &mut [u8; 32],
        ) -> bool;

        #[cxx_name = "UnauthorizedBundle"]
        type SaplingUnauthorizedBundle;

        #[cxx_name = "apply_bundle_signatures"]
        fn apply_sapling_bundle_signatures(
            bundle: Box<SaplingUnauthorizedBundle>,
            sighash_bytes: [u8; 32],
        ) -> Result<Box<SaplingBundle>>;

        type Verifier;

        fn init_verifier() -> Box<Verifier>;
        #[allow(clippy::too_many_arguments)]
        fn check_spend(
            self: &mut Verifier,
            cv: &[u8; 32],
            anchor: &[u8; 32],
            nullifier: &[u8; 32],
            rk: &[u8; 32],
            zkproof: &[u8; 192], // GROTH_PROOF_SIZE
            spend_auth_sig: &[u8; 64],
            sighash_value: &[u8; 32],
        ) -> bool;
        fn check_output(
            self: &mut Verifier,
            cv: &[u8; 32],
            cm: &[u8; 32],
            ephemeral_key: &[u8; 32],
            zkproof: &[u8; 192], // GROTH_PROOF_SIZE
        ) -> bool;
        fn final_check(
            self: &Verifier,
            value_balance: i64,
            binding_sig: &[u8; 64],
            sighash_value: &[u8; 32],
        ) -> bool;

        #[cxx_name = "BatchValidator"]
        type SaplingBatchValidator;
        #[cxx_name = "init_batch_validator"]
        fn init_sapling_batch_validator(cache_store: bool) -> Box<SaplingBatchValidator>;
        fn check_bundle(
            self: &mut SaplingBatchValidator,
            bundle: Box<SaplingBundle>,
            sighash: [u8; 32],
        ) -> bool;
        fn validate(self: &mut SaplingBatchValidator) -> bool;
    }

    #[namespace = "orchard_bundle"]
    extern "Rust" {
        type Action;
        type OrchardBundle;

        fn cv(self: &Action) -> [u8; 32];
        fn nullifier(self: &Action) -> [u8; 32];
        fn rk(self: &Action) -> [u8; 32];
        fn cmx(self: &Action) -> [u8; 32];
        fn ephemeral_key(self: &Action) -> [u8; 32];
        fn enc_ciphertext(self: &Action) -> [u8; 580];
        fn out_ciphertext(self: &Action) -> [u8; 80];
        fn spend_auth_sig(self: &Action) -> [u8; 64];
        fn as_ptr(self: &Action) -> *const ActionPtr;
        
        // Compute nullifier directly from encrypted action (decrypts first).
        // FVK is the single source of truth — IVK is derived internally from the FVK.
        fn compute_nullifier(
            self: &Action,
            fvk_bytes: &[u8; 96],
            result: &mut [u8; 32],
        ) -> bool;

        #[rust_name = "none_orchard_bundle"]
        fn none() -> Box<OrchardBundle>;
        #[rust_name = "orchard_bundle_from_raw_box"]
        unsafe fn from_raw_box(bundle: *mut OrchardBundlePtr) -> Box<OrchardBundle>;
        fn box_clone(self: &OrchardBundle) -> Box<OrchardBundle>;
        #[rust_name = "parse_orchard_bundle"]
        fn parse(stream: &mut CppStream<'_>) -> Result<Box<OrchardBundle>>;
        fn serialize(self: &OrchardBundle, stream: &mut CppStream<'_>) -> Result<()>;
        fn as_ptr(self: &OrchardBundle) -> *const OrchardBundlePtr;
        fn recursive_dynamic_usage(self: &OrchardBundle) -> usize;
        fn is_present(self: &OrchardBundle) -> bool;
        fn actions(self: &OrchardBundle) -> Vec<Action>;
        fn num_actions(self: &OrchardBundle) -> usize;
        fn get_action(self: &OrchardBundle, action_index: usize) -> Result<Box<Action>>;
        fn enable_spends(self: &OrchardBundle) -> bool;
        fn enable_outputs(self: &OrchardBundle) -> bool;
        fn value_balance_zat(self: &OrchardBundle) -> i64;
        fn anchor(self: &OrchardBundle) -> [u8; 32];
        fn proof(self: &OrchardBundle) -> Vec<u8>;
        fn binding_sig(self: &OrchardBundle) -> [u8; 64];
        fn coinbase_outputs_are_valid(self: &OrchardBundle) -> bool;
    }

    #[namespace = "orchard"]
    extern "Rust" {
        #[cxx_name = "BatchValidator"]
        type OrchardBatchValidator;
        #[cxx_name = "init_batch_validator"]
        fn orchard_batch_validation_init(cache_store: bool) -> Box<OrchardBatchValidator>;
        fn add_bundle(
            self: &mut OrchardBatchValidator,
            bundle: Box<OrchardBundle>,
            sighash: [u8; 32],
        );
        fn validate(self: &mut OrchardBatchValidator) -> bool;
        
        // Compute nullifier from note parts (already decrypted)
        // Similar to sapling::compute_nullifier
        #[rust_name = "compute_nullifier_orchard"]
        fn compute_nullifier(
            fvk_bytes: &[u8; 96],
            address_bytes: &[u8; 43],
            value: u64,
            rho_bytes: &[u8; 32],
            rseed_bytes: &[u8; 32],
            result: &mut [u8; 32],
        ) -> bool;

        // Derive Orchard Outgoing Cipher Key for a specific action
        fn derive_orchard_ock(
            orchard_action: &Action,
            ovk_bytes: &[u8; 32],
            ock_out: &mut [u8; 32],
        ) -> bool;

        // Decrypt Orchard action output using OCK
        fn try_orchard_decrypt_action_ock(
            orchard_action: &Action,
            ock_bytes: &[u8; 32],
            value_out: &mut u64,
            address_out: &mut [u8; 43],
            memo_out: &mut [u8; 512],
            rho_out: &mut [u8; 32],
            rseed_out: &mut [u8; 32],
        ) -> bool;
    }

    #[namespace = "merkle_frontier"]
    struct SaplingAppendResult {
        has_subtree_boundary: bool,
        completed_subtree_root: [u8; 32],
    }

    #[namespace = "merkle_frontier"]
    struct OrchardAppendResult {
        has_subtree_boundary: bool,
        completed_subtree_root: [u8; 32],
    }

    #[namespace = "merkle_frontier"]
    extern "Rust" {

        type SaplingFrontier;
        type SaplingWallet;

        fn sapling_empty_root() -> [u8; 32];
        fn new_sapling() -> Box<SaplingFrontier>;
        fn box_clone(self: &SaplingFrontier) -> Box<SaplingFrontier>;
        fn parse_sapling(reader: &mut CppStream<'_>) -> Result<Box<SaplingFrontier>>;
        fn serialize(self: &SaplingFrontier, writer: &mut CppStream<'_>) -> Result<()>;
        fn serialize_legacy(self: &SaplingFrontier, writer: &mut CppStream<'_>) -> Result<()>;
        fn dynamic_memory_usage(self: &SaplingFrontier) -> usize;
        fn root(self: &SaplingFrontier) -> [u8; 32];
        fn size(self: &SaplingFrontier) -> u64;
        fn append_bundle(
            self: &mut SaplingFrontier,
            sapling_bundle: &SaplingBundle,
        ) -> Result<SaplingAppendResult>;
        fn append(
            self: &mut SaplingFrontier,
            sapling_cmu: [u8; 32],
        ) -> Result<SaplingAppendResult>;
        unsafe fn init_wallet(self: &SaplingFrontier, wallet: *mut SaplingWallet) -> bool;
    }

    #[namespace = "merkle_frontier"]
    extern "Rust" {
        type OrchardFrontier;
        type OrchardWallet;

        fn orchard_empty_root() -> [u8; 32];
        fn new_orchard() -> Box<OrchardFrontier>;
        fn box_clone(self: &OrchardFrontier) -> Box<OrchardFrontier>;
        fn parse_orchard(reader: &mut CppStream<'_>) -> Result<Box<OrchardFrontier>>;
        fn serialize(self: &OrchardFrontier, writer: &mut CppStream<'_>) -> Result<()>;
        fn serialize_legacy(self: &OrchardFrontier, writer: &mut CppStream<'_>) -> Result<()>;
        fn dynamic_memory_usage(self: &OrchardFrontier) -> usize;
        fn root(self: &OrchardFrontier) -> [u8; 32];
        fn size(self: &OrchardFrontier) -> u64;
        fn append_bundle(
            self: &mut OrchardFrontier,
            bundle: &OrchardBundle,
        ) -> Result<OrchardAppendResult>;
        fn append(
            self: &mut OrchardFrontier,
            orchard_cmx: [u8; 32],
        ) -> Result<OrchardAppendResult>;
        unsafe fn init_wallet(self: &OrchardFrontier, wallet: *mut OrchardWallet) -> bool;
    }

    unsafe extern "C++" {
        include!("rust/builder.h");
        type OrchardUnauthorizedBundlePtr;
    }
    #[namespace = "builder"]
    extern "Rust" {
        unsafe fn shielded_signature_digest(
            consensus_branch_id: u32,
            tx_bytes: &[u8],
            all_prev_outputs: &[u8],
            sapling_bundle: &SaplingUnauthorizedBundle,
            orchard_bundle: *const OrchardUnauthorizedBundlePtr,
        ) -> Result<[u8; 32]>;
    }

    #[namespace = "orchard_keys"]
    extern "Rust" {
        #[cxx_name = "ivk_to_address"]
        fn orchard_keys_ivk_to_address(ivk: &[u8; 64], diversifier: &[u8; 11], out: &mut [u8; 43]) -> bool;
        #[cxx_name = "ivk_to_address_from_index"]
        fn orchard_keys_ivk_to_address_from_index(ivk: &[u8; 64], diversifier_index: &[u8; 11], out: &mut [u8; 43]) -> bool;
        fn fvk_to_ovk(fvk: &[u8; 96], out: &mut [u8; 32]) -> bool;
        fn fvk_to_ovk_internal(fvk: &[u8; 96], out: &mut [u8; 32]) -> bool;
        #[cxx_name = "fvk_to_ivk"]
        fn orchard_keys_fvk_to_ivk(fvk: &[u8; 96], out: &mut [u8; 64]) -> bool;
        #[cxx_name = "fvk_to_ivk_internal"]
        fn orchard_keys_fvk_to_ivk_internal(fvk: &[u8; 96], out: &mut [u8; 64]) -> bool;
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
        /// Derives the internal (change) ExtendedSpendingKey from the external 169-byte XSK.
        /// The result contains nsk_internal, required for zk-SNARK proofs over internal notes.
        #[cxx_name = "xsk_derive_internal"]
        fn sapling_keys_xsk_derive_internal(xsk: &[u8; 169]) -> [u8; 169];
    }

    #[namespace = "hd_seed"]
    /// Selects the BIP-39 word list language for mnemonic phrase generation and restoration.
    /// Word lists for all variants are compiled into the binary.
    enum MnemonicLanguage {
        /// BIP-39 English word list (default, always available).
        English = 0,
        /// BIP-39 Chinese Simplified word list.
        ChineseSimplified = 1,
        /// BIP-39 Chinese Traditional word list.
        ChineseTraditional = 2,
        /// BIP-39 French word list.
        French = 3,
        /// BIP-39 Italian word list.
        Italian = 4,
        /// BIP-39 Japanese word list.
        Japanese = 5,
        /// BIP-39 Korean word list.
        Korean = 6,
        /// BIP-39 Spanish word list.
        Spanish = 7,
    }

    #[namespace = "transparent_keys"]
    extern "Rust" {
        /// Derives the 32-byte OVK used when shielding funds from a transparent address.
        /// `seed` is the raw HD seed bytes. Always returns true.
        #[cxx_name = "ovk_for_shielding_from_taddr"]
        fn transparent_keys_ovk_for_shielding_from_taddr(seed: &[u8], out: &mut [u8; 32]) -> bool;
    }

    #[namespace = "hd_seed"]
    extern "Rust" {
        /// Fills `out` with 32 cryptographically secure random bytes via the OS CSPRNG.
        #[cxx_name = "random_bytes"]
        fn hd_seed_random_bytes(out: &mut [u8; 32]);

        /// Derives the BIP-39 mnemonic phrase from raw entropy (16, 24, or 32 bytes)
        /// using the specified word-list language.
        /// Returns an error string if the entropy length is invalid.
        #[cxx_name = "entropy_to_phrase"]
        fn hd_seed_entropy_to_phrase(entropy: &[u8], lang: MnemonicLanguage) -> String;

        /// Restores entropy from a BIP-39 mnemonic phrase into `out` (16, 24, or 32 bytes).
        /// `lang` must match the language used when the phrase was generated.
        /// Returns true on success.
        #[cxx_name = "phrase_to_entropy"]
        fn hd_seed_phrase_to_entropy(phrase: &str, lang: MnemonicLanguage, out: &mut [u8]) -> bool;

        /// Derives the 64-byte BIP-39 PBKDF2 seed from raw entropy (no passphrase).
        /// Language does not affect this derivation.
        /// Returns true on success.
        #[cxx_name = "entropy_to_bip39_seed"]
        fn hd_seed_entropy_to_bip39_seed(entropy: &[u8], out: &mut [u8; 64]) -> bool;
    }
}
