use crate::{
    bundlecache::init as bundlecache_init,
    merkle_frontier::{new_sapling, sapling_empty_root, parse_sapling, SaplingFrontier, SaplingWallet},
    params::{network, Network},
    sapling::{
        apply_sapling_bundle_signatures, build_sapling_bundle, finish_bundle_assembly,
        init_batch_validator as init_sapling_batch_validator, init_verifier, new_bundle_assembler,
        new_sapling_builder, none_sapling_bundle, parse_v4_sapling_components,
        parse_v4_sapling_output, parse_v4_sapling_spend, parse_v5_sapling_bundle,
        BatchValidator as SaplingBatchValidator, Bundle as SaplingBundle,
        BundleAssembler as SaplingBundleAssembler, Output, SaplingBuilder,
        SaplingUnauthorizedBundle, Spend, Verifier,
    },
    streams::{
        from_auto_file, from_blake2b_writer, from_buffered_file, from_data, from_hash_writer,
        from_size_computer, CppStream,
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
        fn from_auto_file(file: Pin<&mut CAutoFile>) -> Box<CppStream<'_>>;
        fn from_buffered_file(file: Pin<&mut CBufferedFile>) -> Box<CppStream<'_>>;
        fn from_hash_writer(writer: Pin<&mut CHashWriter>) -> Box<CppStream<'_>>;
        fn from_blake2b_writer(writer: Pin<&mut CBLAKE2bWriter>) -> Box<CppStream<'_>>;
        fn from_size_computer(sc: Pin<&mut CSizeComputer>) -> Box<CppStream<'_>>;
    }

    #[namespace = "consensus"]
    extern "Rust" {
        type Network;

        fn network(
            network: &str,
            overwinter: i32,
            sapling: i32,
            blossom: i32,
            heartwood: i32,
            canopy: i32,
            nu5: i32,
        ) -> Result<Box<Network>>;
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
        include!("rust/sapling.h");
        type SaplingBundlePtr;
        type OutputPtr;
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

        #[cxx_name = "Bundle"]
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
        fn outputs(self: &SaplingBundle) -> Vec<Output>;
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
        fn new_sapling_builder(network: &Network, height: u32) -> Box<SaplingBuilder>;
        fn add_spend(
            self: &mut SaplingBuilder,
            extsk: &[u8],
            diversifier: [u8; 11],
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
            target_height: u32,
        ) -> Result<Box<SaplingUnauthorizedBundle>>;

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

    #[namespace = "merkle_frontier"]
    struct SaplingAppendResult {
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
        fn append_bundle(self: &mut SaplingFrontier, sapling_bundle: &SaplingBundle) -> Result<SaplingAppendResult>;
        unsafe fn init_wallet(self: &SaplingFrontier, wallet: *mut SaplingWallet) -> bool;
    }
}
