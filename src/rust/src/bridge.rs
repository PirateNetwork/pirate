use crate::{
    bundlecache::init as bundlecache_init,
    merkle_frontier::{
        new_orchard, new_sapling, orchard_empty_root, parse_orchard, parse_sapling,
        sapling_empty_root, OrchardFrontier, OrchardWallet, SaplingFrontier, SaplingWallet,
    },
    orchard_protocol::{
        orchard_bundle::{
            none_orchard_bundle, orchard_bundle_from_raw_box, parse_orchard_bundle,
            parse_orchard_bundle_v6, Action,
            Bundle as OrchardBundle,
        },
        orchard_actions::{
            compute_nullifier as compute_nullifier_orchard,
            derive_orchard_ock, try_orchard_decrypt_action_ock,
        },
        orchard_validator::{orchard_batch_validation_init, BatchValidator as OrchardBatchValidator},
    },
    ironwood_protocol::{
        ironwood_bundle::{
            none_ironwood_bundle, ironwood_bundle_from_raw_box, parse_ironwood_bundle,
            Bundle as IronwoodBundle,
        },
        ironwood_validator::{
            ironwood_batch_validation_init, BatchValidator as IronwoodBatchValidator,
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
        type IronwoodBundlePtr;
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
        fn parse(stream: &mut CppStream<'_>, consensus_branch_id: u32) -> Result<Box<OrchardBundle>>;
        fn serialize(self: &OrchardBundle, stream: &mut CppStream<'_>) -> Result<()>;
        // v6 (Ironwood-era) Orchard slot. SCAFFOLDING ONLY: unused until v6 exists.
        #[rust_name = "parse_orchard_bundle_v6"]
        fn parse_v6(stream: &mut CppStream<'_>, consensus_branch_id: u32) -> Result<Box<OrchardBundle>>;
        fn serialize_v6(self: &OrchardBundle, stream: &mut CppStream<'_>) -> Result<()>;
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

    // SCAFFOLDING ONLY: IronwoodBundle is not yet used by any C++ code (no v6 transaction
    // format exists in this tree). It reuses the `Action` type declared above in
    // `orchard_bundle` — Ironwood shares the Orchard Action circuit and note format,
    // distinguished only by BundleVersion.
    #[namespace = "ironwood_bundle"]
    extern "Rust" {
        type IronwoodBundle;

        #[rust_name = "none_ironwood_bundle"]
        fn none() -> Box<IronwoodBundle>;
        #[rust_name = "ironwood_bundle_from_raw_box"]
        unsafe fn from_raw_box(bundle: *mut IronwoodBundlePtr) -> Box<IronwoodBundle>;
        fn box_clone(self: &IronwoodBundle) -> Box<IronwoodBundle>;
        #[rust_name = "parse_ironwood_bundle"]
        fn parse(stream: &mut CppStream<'_>, consensus_branch_id: u32) -> Result<Box<IronwoodBundle>>;
        fn serialize(self: &IronwoodBundle, stream: &mut CppStream<'_>) -> Result<()>;
        fn as_ptr(self: &IronwoodBundle) -> *const IronwoodBundlePtr;
        fn recursive_dynamic_usage(self: &IronwoodBundle) -> usize;
        fn is_present(self: &IronwoodBundle) -> bool;
        fn actions(self: &IronwoodBundle) -> Vec<Action>;
        fn num_actions(self: &IronwoodBundle) -> usize;
        fn get_action(self: &IronwoodBundle, action_index: usize) -> Result<Box<Action>>;
        fn enable_spends(self: &IronwoodBundle) -> bool;
        fn enable_outputs(self: &IronwoodBundle) -> bool;
        fn value_balance_zat(self: &IronwoodBundle) -> i64;
        fn anchor(self: &IronwoodBundle) -> [u8; 32];
        fn proof(self: &IronwoodBundle) -> Vec<u8>;
        fn binding_sig(self: &IronwoodBundle) -> [u8; 64];
        fn coinbase_outputs_are_valid(self: &IronwoodBundle) -> bool;
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

    // SCAFFOLDING ONLY: not yet called by any C++ code (no v6 transaction support exists
    // in this tree).
    #[namespace = "ironwood"]
    extern "Rust" {
        #[cxx_name = "BatchValidator"]
        type IronwoodBatchValidator;
        #[cxx_name = "init_batch_validator"]
        fn ironwood_batch_validation_init(cache_store: bool) -> Box<IronwoodBatchValidator>;
        fn add_bundle(
            self: &mut IronwoodBatchValidator,
            bundle: Box<IronwoodBundle>,
            sighash: [u8; 32],
        );
        fn validate(self: &mut IronwoodBatchValidator) -> bool;
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
}
