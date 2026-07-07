use std::convert::TryInto;

use rand_core::OsRng;
use tracing::{debug, error};

use crate::{
    bundlecache::{ironwood_bundle_validity_cache, ironwood_bundle_validity_cache_mut, CacheEntries},
    ironwood_protocol::ironwood_bundle::Bundle,
};

struct BatchValidatorInner {
    validator: orchard::bundle::BatchValidator<'static>,
    queued_entries: CacheEntries,
    poisoned: bool,
}

pub(crate) struct BatchValidator(Option<BatchValidatorInner>);

/// Creates an Ironwood bundle batch validation context.
pub(crate) fn ironwood_batch_validation_init(cache_store: bool) -> Box<BatchValidator> {
    let vk = (unsafe { crate::IRONWOOD_VK.as_ref() })
        .expect("Parameters not loaded: IRONWOOD_VK should have been initialized");
    assert!(
        vk.circuit_version() == orchard::circuit::OrchardCircuitVersion::PostNu6_3,
        "IRONWOOD_VK must use PostNu6_3"
    );
    Box::new(BatchValidator(Some(BatchValidatorInner {
        validator: orchard::bundle::BatchValidator::new(vk),
        queued_entries: CacheEntries::new(cache_store),
        poisoned: false,
    })))
}

impl BatchValidator {
    /// Adds an Ironwood bundle to this batch.
    pub(crate) fn add_bundle(&mut self, bundle: Box<Bundle>, sighash: [u8; 32]) {
        let batch = self.0.as_mut();
        let bundle = bundle.inner();

        // Ironwood bundles exist only in v6 transactions.
        let tx_version = orchard::bundle::TxVersion::V6;

        match (batch, bundle) {
            (Some(batch), Some(bundle)) => {
                let cache = ironwood_bundle_validity_cache();

                // Compute the cache entry for this bundle.
                let cache_entry = {
                    let bundle_commitment = bundle
                        .commitment(tx_version)
                        .expect("bundle was already validated as v6-representable");
                    let bundle_authorizing_commitment = bundle
                        .authorizing_commitment(tx_version)
                        .expect("bundle was already validated as v6-representable");
                    cache.compute_entry(
                        bundle_commitment.0.as_bytes().try_into().unwrap(),
                        bundle_authorizing_commitment
                            .0
                            .as_bytes()
                            .try_into()
                            .unwrap(),
                        &sighash,
                    )
                };

                // Check if this bundle's validation result exists in the cache.
                if !cache.contains(cache_entry, &mut batch.queued_entries) {
                    // The bundle has been added to `inner.queued_entries` because it was not
                    // in the cache. We now add its authorization to the validation batch.
                    if let Err(e) = batch.validator.add_bundle(bundle, sighash) {
                        error!("Ironwood bundle rejected by batch validator: {}", e);
                        batch.poisoned = true;
                    }
                }
            }
            (Some(_), None) => debug!("Tx has no Ironwood component"),
            (None, _) => error!("ironwood::BatchValidator has already been used"),
        }
    }

    /// Validates this batch.
    pub(crate) fn validate(&mut self) -> bool {
        if let Some(inner) = self.0.take() {
            // `vk` was already validated (PostNu6_3) and bound into `inner.validator` at
            // construction time (see `ironwood_batch_validation_init`).
            if inner.poisoned {
                return false;
            }
            if inner.validator.validate(OsRng) {
                ironwood_bundle_validity_cache_mut().insert(inner.queued_entries);
                true
            } else {
                false
            }
        } else {
            error!("ironwood::BatchValidator has already been used");
            false
        }
    }
}
