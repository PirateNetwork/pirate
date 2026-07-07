#![allow(dead_code)]
use blake2b_simd::Params;

const PRF_EXPAND_PERSONALIZATION: &[u8; 16] = b"Zcash_ExpandSeed";

/// The set of domains in which $PRF^\mathsf{expand}$ is defined.
///
/// The `Ironwood*` variant names reflect this pool's naming; the underlying protocol-spec
/// domain-separator byte tags are the same ones the Orchard Action circuit already used
/// (see the rename-exception note in this session's plan - these are cryptographic
/// constants, not cosmetic names, and there is no spec basis to invent different ones).
pub(crate) enum PrfExpand {
    Esk,
    Rcm,
    IronwoodAsk,
    IronwoodNk,
    IronwoodRivk,
    Psi,
    IronwoodZip32Child,
    IronwoodDkOvk,
    IronwoodRivkInternal,
}

impl PrfExpand {
    fn domain_separator(&self) -> u8 {
        match self {
            Self::Esk => 0x04,
            Self::Rcm => 0x05,
            Self::IronwoodAsk => 0x06,
            Self::IronwoodNk => 0x07,
            Self::IronwoodRivk => 0x08,
            Self::Psi => 0x09,
            Self::IronwoodZip32Child => 0x81,
            Self::IronwoodDkOvk => 0x82,
            Self::IronwoodRivkInternal => 0x83,
        }
    }

    /// Expands the given secret key in this domain, with no additional data.
    ///
    /// $PRF^\mathsf{expand}(sk, dst) := BLAKE2b-512("Zcash_ExpandSeed", sk || dst)$
    ///
    /// Defined in [Zcash Protocol Spec § 5.4.2: Pseudo Random Functions][concreteprfs].
    ///
    /// [concreteprfs]: https://zips.z.cash/protocol/nu5.pdf#concreteprfs
    pub(crate) fn expand(self, sk: &[u8]) -> [u8; 64] {
        self.with_ad_slices(sk, &[])
    }

    /// Expands the given secret key in this domain, with the given additional data.
    ///
    /// $PRF^\mathsf{expand}(sk, dst, t) := BLAKE2b-512("Zcash_ExpandSeed", sk || dst || t)$
    ///
    /// Defined in [Zcash Protocol Spec § 5.4.2: Pseudo Random Functions][concreteprfs].
    ///
    /// [concreteprfs]: https://zips.z.cash/protocol/nu5.pdf#concreteprfs
    pub(crate) fn with_ad(self, sk: &[u8], t: &[u8]) -> [u8; 64] {
        self.with_ad_slices(sk, &[t])
    }

    /// Expands the given secret key in this domain, with additional data concatenated
    /// from the given slices.
    ///
    /// $PRF^\mathsf{expand}(sk, dst, a, b, ...) := BLAKE2b-512("Zcash_ExpandSeed", sk || dst || a || b || ...)$
    ///
    /// Defined in [Zcash Protocol Spec § 5.4.2: Pseudo Random Functions][concreteprfs].
    ///
    /// [concreteprfs]: https://zips.z.cash/protocol/nu5.pdf#concreteprfs
    pub(crate) fn with_ad_slices(self, sk: &[u8], ts: &[&[u8]]) -> [u8; 64] {
        let mut h = Params::new()
            .hash_length(64)
            .personal(PRF_EXPAND_PERSONALIZATION)
            .to_state();
        h.update(sk);
        h.update(&[self.domain_separator()]);
        for t in ts {
            h.update(t);
        }
        *h.finalize().as_array()
    }
}
