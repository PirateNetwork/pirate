use std::convert::TryInto;

use rand_core::OsRng;
use tracing::{debug, error};

use crate::{
    bundlecache::{orchard_bundle_validity_cache, orchard_bundle_validity_cache_mut, CacheEntries},
    orchard_protocol::orchard_bundle::Bundle,
};

struct BatchValidatorInner {
    validator: orchard::bundle::BatchValidator<'static>,
    queued_entries: CacheEntries,
    /// Set if a bundle was rejected by `orchard::bundle::BatchValidator::add_bundle` (e.g. it
    /// disables cross-address transfers but `vk`'s circuit version can't enforce that). Once
    /// poisoned, `validate` must fail the whole batch even though the underlying
    /// `orchard::bundle::BatchValidator` doesn't know the bundle was dropped.
    poisoned: bool,
}

pub(crate) struct BatchValidator(Option<BatchValidatorInner>);

/// Creates an Orchard bundle batch validation context.
pub(crate) fn orchard_batch_validation_init(cache_store: bool) -> Box<BatchValidator> {
    let vk = (unsafe { crate::ORCHARD_VK.as_ref() })
        .expect("Parameters not loaded: ORCHARD_VK should have been initialized");
    assert!(
        vk.circuit_version() == orchard::circuit::OrchardCircuitVersion::FixedPostNu6_2,
        "ORCHARD_VK must use FixedPostNu6_2; InsecurePreNu6_2 is forbidden for live-network verification"
    );
    Box::new(BatchValidator(Some(BatchValidatorInner {
        validator: orchard::bundle::BatchValidator::new(vk),
        queued_entries: CacheEntries::new(cache_store),
        poisoned: false,
    })))
}

impl BatchValidator {
    /// Adds an Orchard bundle to this batch.
    pub(crate) fn add_bundle(&mut self, bundle: Box<Bundle>, sighash: [u8; 32]) {
        let batch = self.0.as_mut();
        let bundle = bundle.inner();

        // TreasureChest only builds/accepts v5 transactions today; there is no v6 (and
        // therefore no Ironwood-bundle) support yet. Revisit once v6 lands.
        let tx_version = orchard::bundle::TxVersion::V5;

        match (batch, bundle) {
            (Some(batch), Some(bundle)) => {
                let cache = orchard_bundle_validity_cache();

                // Compute the cache entry for this bundle.
                let cache_entry = {
                    let bundle_commitment = bundle
                        .commitment(tx_version)
                        .expect("bundle was already validated as v5-representable");
                    let bundle_authorizing_commitment = bundle
                        .authorizing_commitment(tx_version)
                        .expect("bundle was already validated as v5-representable");
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
                        error!("Orchard bundle rejected by batch validator: {}", e);
                        batch.poisoned = true;
                    }
                }
            }
            (Some(_), None) => debug!("Tx has no Orchard component"),
            (None, _) => error!("orchard::BatchValidator has already been used"),
        }
    }

    /// Validates this batch.
    ///
    /// - Returns `true` if `batch` is null.
    /// - Returns `false` if any item in the batch is invalid.
    ///
    /// The batch validation context is freed by this function.
    ///
    /// ## Consensus rules
    ///
    /// [§4.6](https://zips.z.cash/protocol/protocol.pdf#actiondesc):
    /// - Canonical element encodings are enforced by [`orchard_bundle_parse`].
    /// - SpendAuthSig^Orchard validity is enforced here.
    /// - Proof validity is enforced here.
    ///
    /// [§7.1](https://zips.z.cash/protocol/protocol.pdf#txnencodingandconsensus):
    /// - `bindingSigOrchard` validity is enforced here.
    pub(crate) fn validate(&mut self) -> bool {
        if let Some(inner) = self.0.take() {
            // `vk` was already validated (FixedPostNu6_2) and bound into `inner.validator`
            // at construction time (see `orchard_batch_validation_init`).
            if inner.poisoned {
                return false;
            }
            if inner.validator.validate(OsRng) {
                // `BatchValidator::validate()` is only called if every
                // `BatchValidator::check_bundle()` returned `true`, so at this point
                // every bundle that was added to `inner.queued_entries` has valid
                // authorization.
                orchard_bundle_validity_cache_mut().insert(inner.queued_entries);
                true
            } else {
                false
            }
        } else {
            error!("orchard::BatchValidator has already been used");
            false
        }
    }
}
