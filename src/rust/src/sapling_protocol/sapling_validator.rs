use std::convert::TryInto;
use rand_core::OsRng;
use sapling_crypto::BatchValidator as SaplingBatchValidator;
use zcash_primitives::transaction::txid::{BlockTxCommitmentDigester, TxIdDigester};

use crate::{
    SAPLING_SPEND_VK, SAPLING_OUTPUT_VK,
    bundlecache::{sapling_bundle_validity_cache, sapling_bundle_validity_cache_mut, CacheEntries},
};
use super::sapling_bundle::Bundle;

struct BatchValidatorInner {
    validator: SaplingBatchValidator,
    queued_entries: CacheEntries,
}

pub(crate) struct BatchValidator(Option<BatchValidatorInner>);

pub(crate) fn init_batch_validator(cache_store: bool) -> Box<BatchValidator> {
    Box::new(BatchValidator(Some(BatchValidatorInner {
        validator: SaplingBatchValidator::new(),
        queued_entries: CacheEntries::new(cache_store),
    })))
}

impl BatchValidator {
    /// Checks the bundle against Sapling-specific consensus rules, and queues its
    /// authorization for validation.
    ///
    /// Returns `false` if the bundle doesn't satisfy the checked consensus rules. This
    /// `BatchValidator` can continue to be used regardless, but some or all of the proofs
    /// and signatures from this bundle may have already been added to the batch even if
    /// it fails other consensus rules.
    ///
    /// `sighash` must be for the transaction this bundle is within.
    ///
    /// If this batch was configured to not cache the results, then if the bundle was in
    /// the global bundle validity cache, it will have been removed (and this method will
    /// return `true`).
    #[allow(clippy::boxed_local)]
    pub(crate) fn check_bundle(&mut self, bundle: Box<Bundle>, sighash: [u8; 32]) -> bool {
        match (&mut self.0, bundle.inner()) {
            (Some(inner), Some(_)) => {
                let cache = sapling_bundle_validity_cache();

                // Compute the cache entry for this bundle.
                let cache_entry = {
                    let bundle_commitment = bundle.commitment(TxIdDigester).unwrap();
                    let bundle_authorizing_commitment =
                        bundle.commitment(BlockTxCommitmentDigester);
                    cache.compute_entry(
                        bundle_commitment.as_bytes().try_into().unwrap(),
                        bundle_authorizing_commitment.as_bytes().try_into().unwrap(),
                        &sighash,
                    )
                };

                // Check if this bundle's validation result exists in the cache.
                if cache.contains(cache_entry, &mut inner.queued_entries) {
                    true
                } else {
                    // The bundle has been added to `inner.queued_entries` because it was not
                    // in the cache. We now check the bundle against the Sapling-specific
                    // consensus rules, and add its authorization to the validation batch.
                    inner.validator.check_bundle(bundle.0.unwrap(), sighash)
                }
            }
            (Some(_), None) => {
                tracing::debug!("Tx has no Sapling component");
                true
            }
            (None, _) => {
                tracing::error!("sapling::BatchValidator has already been used");
                false
            }
        }
    }

    /// Batch-validates the accumulated bundles.
    ///
    /// Returns `true` if every proof and signature in every bundle added to the batch
    /// validator is valid, or `false` if one or more are invalid. No attempt is made to
    /// figure out which of the accumulated bundles might be invalid; if that information
    /// is desired, construct separate [`BatchValidator`]s for sub-batches of the bundles.
    ///
    /// This method MUST NOT be called if any prior call to `Self::check_bundle` returned
    /// `false`.
    ///
    /// If this batch was configured to cache the results, then if this method returns
    /// `true` every bundle added to the batch will have also been added to the global
    /// bundle validity cache.
    pub(crate) fn validate(&mut self) -> bool {
        if let Some(inner) = self.0.take() {
            if inner.validator.validate(
                unsafe { SAPLING_SPEND_VK.as_ref() }
                    .expect("Parameters not loaded: SAPLING_SPEND_VK should have been initialized"),
                unsafe { SAPLING_OUTPUT_VK.as_ref() }.expect(
                    "Parameters not loaded: SAPLING_OUTPUT_VK should have been initialized",
                ),
                OsRng,
            ) {
                // `Self::validate()` is only called if every `Self::check_bundle()`
                // returned `true`, so at this point every bundle that was added to
                // `inner.queued_entries` has valid authorization and satisfies the
                // Sapling-specific consensus rules.
                sapling_bundle_validity_cache_mut().insert(inner.queued_entries);
                true
            } else {
                false
            }
        } else {
            tracing::error!("sapling::BatchValidator has already been used");
            false
        }
    }
}
