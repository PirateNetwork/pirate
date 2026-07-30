use std::convert::TryFrom;
use group::{ff::Field, ff::PrimeField, Group, GroupEncoding};
use pasta_curves::pallas;
use rand::{thread_rng, Rng};
use zcash_note_encryption::EphemeralKeyBytes;
use redjubjub::{self as redjubjub, Binding, SpendAuth};
use sapling_crypto::{
    value::ValueCommitment,
    note::ExtractedNoteCommitment as SaplingExtractedNoteCommitment,
};
use sapling_crypto::bundle::{
    Authorized as SaplingSig, Bundle as SaplingBundle,
    SpendDescription as SaplingSpend, OutputDescription as SaplingOutput,
};
use zcash_protocol::value::ZatBalance as Amount;
use orchard::{
    bundle::Authorized as IronwoodSig,
    note::{ExtractedNoteCommitment as IronwoodCmx, Nullifier as IronwoodNullifier},
    primitives::redpallas::{
        SigningKey as IronwoodSigningKey, SpendAuth as IronwoodSpendAuth,
        VerificationKey as IronwoodVerificationKey,
    },
    value::ValueCommitment as IronwoodValueCommitment,
    Proof as IronwoodProof,
};

pub(crate) fn test_only_invalid_sapling_bundle(
    spends: usize,
    outputs: usize,
    value_balance: i64,
) -> Box<crate::sapling_protocol::Bundle> {
    let mut rng = thread_rng();

    fn gen_array<R: Rng, const N: usize>(mut rng: R) -> [u8; N] {
        let mut tmp = [0; N];
        rng.fill(&mut tmp[..]);
        tmp
    }

    let spends = (0..spends)
        .map(|_| {
            let cv = ValueCommitment::from_bytes_not_small_order(
                &jubjub::ExtendedPoint::random(&mut rng).to_bytes(),
            )
            .unwrap();
            let anchor = jubjub::Base::random(&mut rng);
            let nullifier = sapling_crypto::Nullifier(rng.gen());
            // Derive rk from a random signing key — always yields a valid VerificationKey.
            let rk = redjubjub::VerificationKey::<SpendAuth>::from(
                &redjubjub::SigningKey::<SpendAuth>::new(&mut rng)
            );
            let zkproof = gen_array(&mut rng);
            let spend_auth_sig: redjubjub::Signature<SpendAuth> = gen_array::<_, 64>(&mut rng).into();

            SaplingSpend::<SaplingSig>::from_parts(
                cv,
                anchor,
                nullifier,
                rk,
                zkproof,
                spend_auth_sig,
            )
        })
        .collect();

    let outputs = (0..outputs)
        .map(|_| {
            let cv = ValueCommitment::from_bytes_not_small_order(
                &jubjub::ExtendedPoint::random(&mut rng).to_bytes(),
            )
            .unwrap();
            let cmu = SaplingExtractedNoteCommitment::from_bytes(
                &jubjub::Base::random(&mut rng).to_bytes(),
            )
            .unwrap();
            let ephemeral_key =
                EphemeralKeyBytes(jubjub::ExtendedPoint::random(&mut rng).to_bytes());
            let enc_ciphertext = gen_array(&mut rng);
            let out_ciphertext = gen_array(&mut rng);
            let zkproof = gen_array(&mut rng);

            SaplingOutput::<[u8; 192]>::from_parts(
                cv,
                cmu,
                ephemeral_key,
                enc_ciphertext,
                out_ciphertext,
                zkproof,
            )
        })
        .collect();

    let binding_sig: redjubjub::Signature<Binding> = gen_array::<_, 64>(&mut rng).into();

    let bundle = SaplingBundle::<SaplingSig, Amount>::from_parts(
        spends,
        outputs,
        Amount::from_i64(value_balance).unwrap(),
        SaplingSig { binding_sig },
    );
    Box::new(crate::sapling_protocol::Bundle(bundle))
}

pub(crate) fn test_only_replace_sapling_nullifier(
    bundle: &mut crate::sapling_protocol::Bundle,
    spend_index: usize,
    nullifier: [u8; 32],
) {
    if let Some(bundle) = bundle.0.as_mut() {
        *bundle = SaplingBundle::<SaplingSig, Amount>::from_parts(
            bundle
                .shielded_spends()
                .iter()
                .enumerate()
                .map(|(i, spend)| {
                    if i == spend_index {
                        SaplingSpend::<SaplingSig>::from_parts(
                            spend.cv().clone(),
                            *spend.anchor(),
                            sapling_crypto::Nullifier(nullifier),
                            spend.rk().clone(),
                            *spend.zkproof(),
                            *spend.spend_auth_sig(),
                        )
                    } else {
                        spend.clone()
                    }
                })
                .collect(),
            bundle.shielded_outputs().to_vec(),
            *bundle.value_balance(),
            bundle.authorization().clone(),
        ).expect("bundle must have actions")
    }
}

/// Replaces one Sapling spend's value commitment (cv) with a fresh, independently-random
/// but valid one (same technique as `test_only_invalid_sapling_bundle`), leaving
/// anchor/nullifier/rk/zkproof/spend_auth_sig untouched. cv is a public input to the
/// spend circuit, so swapping it should be caught by proof verification the same way a
/// corrupted proof is - see `test_only_replace_sapling_spend_proof`'s doc comment for why
/// this can't just be arbitrary bytes (a uniformly-random *point*, not a uniformly-random
/// byte string, is what's needed - the latter is very unlikely to decode as one).
pub(crate) fn test_only_replace_sapling_spend_cv(
    bundle: &mut crate::sapling_protocol::Bundle,
    spend_index: usize,
) {
    if let Some(bundle) = bundle.0.as_mut() {
        let mut rng = thread_rng();
        let cv = ValueCommitment::from_bytes_not_small_order(
            &jubjub::ExtendedPoint::random(&mut rng).to_bytes(),
        ).unwrap();
        *bundle = SaplingBundle::<SaplingSig, Amount>::from_parts(
            bundle
                .shielded_spends()
                .iter()
                .enumerate()
                .map(|(i, spend)| {
                    if i == spend_index {
                        SaplingSpend::<SaplingSig>::from_parts(
                            cv.clone(),
                            *spend.anchor(),
                            *spend.nullifier(),
                            spend.rk().clone(),
                            *spend.zkproof(),
                            *spend.spend_auth_sig(),
                        )
                    } else {
                        spend.clone()
                    }
                })
                .collect(),
            bundle.shielded_outputs().to_vec(),
            *bundle.value_balance(),
            bundle.authorization().clone(),
        ).expect("bundle must have actions")
    }
}

/// Replaces one Sapling spend's randomized verification key (rk) with a fresh,
/// independently-random but valid one (same technique as
/// `test_only_invalid_sapling_bundle`: derived from a random signing key, which always
/// yields a valid, non-identity `VerificationKey`), leaving
/// cv/anchor/nullifier/zkproof/spend_auth_sig untouched. rk is a public input to the
/// spend circuit *and* the key spend_auth_sig is verified against, so swapping it should
/// be caught either way.
pub(crate) fn test_only_replace_sapling_spend_rk(
    bundle: &mut crate::sapling_protocol::Bundle,
    spend_index: usize,
) {
    if let Some(bundle) = bundle.0.as_mut() {
        let mut rng = thread_rng();
        let rk = redjubjub::VerificationKey::<SpendAuth>::from(
            &redjubjub::SigningKey::<SpendAuth>::new(&mut rng)
        );
        *bundle = SaplingBundle::<SaplingSig, Amount>::from_parts(
            bundle
                .shielded_spends()
                .iter()
                .enumerate()
                .map(|(i, spend)| {
                    if i == spend_index {
                        SaplingSpend::<SaplingSig>::from_parts(
                            spend.cv().clone(),
                            *spend.anchor(),
                            *spend.nullifier(),
                            rk.clone(),
                            *spend.zkproof(),
                            *spend.spend_auth_sig(),
                        )
                    } else {
                        spend.clone()
                    }
                })
                .collect(),
            bundle.shielded_outputs().to_vec(),
            *bundle.value_balance(),
            bundle.authorization().clone(),
        ).expect("bundle must have actions")
    }
}

/// Output analogue of [`test_only_replace_sapling_spend_cv`]; see its doc comment for why
/// the replacement is a freshly-generated random point rather than random bytes.
pub(crate) fn test_only_replace_sapling_output_cv(
    bundle: &mut crate::sapling_protocol::Bundle,
    output_index: usize,
) {
    if let Some(bundle) = bundle.0.as_mut() {
        let mut rng = thread_rng();
        let cv = ValueCommitment::from_bytes_not_small_order(
            &jubjub::ExtendedPoint::random(&mut rng).to_bytes(),
        ).unwrap();
        *bundle = SaplingBundle::<SaplingSig, Amount>::from_parts(
            bundle.shielded_spends().to_vec(),
            bundle
                .shielded_outputs()
                .iter()
                .enumerate()
                .map(|(i, output)| {
                    if i == output_index {
                        SaplingOutput::<[u8; 192]>::from_parts(
                            cv.clone(),
                            output.cmu().clone(),
                            output.ephemeral_key().clone(),
                            *output.enc_ciphertext(),
                            *output.out_ciphertext(),
                            *output.zkproof(),
                        )
                    } else {
                        output.clone()
                    }
                })
                .collect(),
            *bundle.value_balance(),
            bundle.authorization().clone(),
        ).expect("bundle must have actions")
    }
}

/// Replaces one Sapling spend's zk-proof with caller-supplied bytes, leaving its
/// cv/anchor/nullifier/rk/spend_auth_sig untouched. Used to prove that the real Groth16
/// batch validator (`sapling::BatchValidator`, main.cpp's `AcceptToMemoryPool`/
/// `ConnectBlock`) actually rejects an otherwise well-formed, correctly-signed spend once
/// its proof no longer attests to the statement.
///
/// The replacement must itself be a well-formed (curve-point-encoded) Groth16 proof, or
/// `sapling::BatchValidator::check_bundle`'s cheap structural check rejects it before the
/// bundle is ever queued for the real (deferred, pairing-check) batch verification this
/// helper exists to exercise - e.g. pass another real spend's or output's zkproof bytes
/// (valid encoding, wrong statement), not arbitrary random bytes.
pub(crate) fn test_only_replace_sapling_spend_proof(
    bundle: &mut crate::sapling_protocol::Bundle,
    spend_index: usize,
    proof: [u8; 192],
) {
    if let Some(bundle) = bundle.0.as_mut() {
        *bundle = SaplingBundle::<SaplingSig, Amount>::from_parts(
            bundle
                .shielded_spends()
                .iter()
                .enumerate()
                .map(|(i, spend)| {
                    if i == spend_index {
                        SaplingSpend::<SaplingSig>::from_parts(
                            spend.cv().clone(),
                            *spend.anchor(),
                            *spend.nullifier(),
                            spend.rk().clone(),
                            proof,
                            *spend.spend_auth_sig(),
                        )
                    } else {
                        spend.clone()
                    }
                })
                .collect(),
            bundle.shielded_outputs().to_vec(),
            *bundle.value_balance(),
            bundle.authorization().clone(),
        ).expect("bundle must have actions")
    }
}

/// Output analogue of [`test_only_replace_sapling_spend_proof`]; see its doc comment for
/// why the replacement must be another real, well-formed proof rather than random bytes.
pub(crate) fn test_only_replace_sapling_output_proof(
    bundle: &mut crate::sapling_protocol::Bundle,
    output_index: usize,
    proof: [u8; 192],
) {
    if let Some(bundle) = bundle.0.as_mut() {
        *bundle = SaplingBundle::<SaplingSig, Amount>::from_parts(
            bundle.shielded_spends().to_vec(),
            bundle
                .shielded_outputs()
                .iter()
                .enumerate()
                .map(|(i, output)| {
                    if i == output_index {
                        SaplingOutput::<[u8; 192]>::from_parts(
                            output.cv().clone(),
                            output.cmu().clone(),
                            output.ephemeral_key().clone(),
                            *output.enc_ciphertext(),
                            *output.out_ciphertext(),
                            proof,
                        )
                    } else {
                        output.clone()
                    }
                })
                .collect(),
            *bundle.value_balance(),
            bundle.authorization().clone(),
        ).expect("bundle must have actions")
    }
}

/// Replaces one Sapling output's note commitment (cmu) with a fresh, independently-random
/// but valid field element, leaving cv/ephemeral_key/ciphertexts/zkproof untouched. cmu is
/// a public input to the output circuit, so swapping it should be caught by proof
/// verification the same way a corrupted proof is - see
/// `test_only_replace_sapling_spend_cv`'s doc comment for why the replacement is generated
/// as a field element rather than sampled as random bytes.
pub(crate) fn test_only_replace_sapling_output_cmu(
    bundle: &mut crate::sapling_protocol::Bundle,
    output_index: usize,
) {
    if let Some(bundle) = bundle.0.as_mut() {
        let mut rng = thread_rng();
        let cmu = SaplingExtractedNoteCommitment::from_bytes(
            &jubjub::Base::random(&mut rng).to_bytes(),
        ).unwrap();
        *bundle = SaplingBundle::<SaplingSig, Amount>::from_parts(
            bundle.shielded_spends().to_vec(),
            bundle
                .shielded_outputs()
                .iter()
                .enumerate()
                .map(|(i, output)| {
                    if i == output_index {
                        SaplingOutput::<[u8; 192]>::from_parts(
                            output.cv().clone(),
                            cmu,
                            output.ephemeral_key().clone(),
                            *output.enc_ciphertext(),
                            *output.out_ciphertext(),
                            *output.zkproof(),
                        )
                    } else {
                        output.clone()
                    }
                })
                .collect(),
            *bundle.value_balance(),
            bundle.authorization().clone(),
        ).expect("bundle must have actions")
    }
}

pub(crate) fn test_only_replace_sapling_output_parts(
    bundle: &mut crate::sapling_protocol::Bundle,
    output_index: usize,
    cmu: [u8; 32],
    enc_ciphertext: [u8; 580],
    out_ciphertext: [u8; 80],
) {
    if let Some(bundle) = bundle.0.as_mut() {
        *bundle = SaplingBundle::<SaplingSig, Amount>::from_parts(
            bundle.shielded_spends().to_vec(),
            bundle
                .shielded_outputs()
                .iter()
                .enumerate()
                .map(|(i, output)| {
                    if i == output_index {
                        SaplingOutput::<[u8; 192]>::from_parts(
                            output.cv().clone(),
                            SaplingExtractedNoteCommitment::from_bytes(&cmu).unwrap(),
                            output.ephemeral_key().clone(),
                            enc_ciphertext,
                            out_ciphertext,
                            *output.zkproof(),
                        )
                    } else {
                        output.clone()
                    }
                })
                .collect(),
            *bundle.value_balance(),
            bundle.authorization().clone(),
        ).expect("bundle must have actions")
    }
}

/// Replaces an Ironwood bundle's zk-proof with random bytes of the same (canonical) length,
/// leaving every action (cv/nullifier/rk/cmx/ciphertexts/spend_auth_sig) and the binding
/// signature untouched. Orchard/Ironwood proves the whole bundle with a single Halo2 proof
/// (unlike Sapling's per-spend/output Groth16 proofs), so there is only one proof to corrupt
/// here. Used to prove that the real Halo2 batch validator (`ironwood::BatchValidator`,
/// main.cpp's `AcceptToMemoryPool`/`ConnectBlock`) actually rejects an otherwise well-formed,
/// correctly-signed, correctly-anchored bundle once its proof no longer attests to the
/// statement.
pub(crate) fn test_only_corrupt_ironwood_proof(
    bundle: &mut crate::ironwood_protocol::ironwood_bundle::Bundle,
) {
    if let Some(inner) = bundle.0.as_mut() {
        let mut rng = thread_rng();
        let num_actions = inner.actions().len();
        let mut garbage = vec![0u8; orchard::Proof::expected_proof_size(num_actions)];
        rng.fill(&mut garbage[..]);
        let corrupted_auth = IronwoodSig::from_parts(
            IronwoodProof::new(garbage),
            inner.authorization().binding_signature().clone(),
        );
        *inner = orchard::Bundle::try_from_parts(
            inner.actions().clone(),
            *inner.flags(),
            *inner.value_balance(),
            *inner.anchor(),
            corrupted_auth,
            inner.bundle_version(),
        ).expect("proof length matches num_actions; flags/version unchanged");
    }
}

/// Replaces one Ironwood action's nullifier with a fresh, independently-random but valid
/// Pallas base-field element, leaving rk/cmx/ciphertexts/cv_net/the aggregate proof/
/// binding signature untouched. Orchard proves the whole bundle with a single Halo2 proof
/// over every action's public inputs (nullifier included), so swapping just one action's
/// nullifier should be caught by proof verification the same way a corrupted proof is -
/// see `test_only_corrupt_ironwood_proof`'s doc comment for why. Generated as an actual
/// random field element (not sampled as random bytes) so it's guaranteed to decode
/// canonically - see `test_only_replace_sapling_spend_cv`'s doc comment for the same
/// reasoning on the Sapling side.
pub(crate) fn test_only_replace_ironwood_nullifier(
    bundle: &mut crate::ironwood_protocol::ironwood_bundle::Bundle,
    action_index: usize,
) {
    if let Some(inner) = bundle.0.as_mut() {
        let mut rng = thread_rng();
        let nf = IronwoodNullifier::from_bytes(&pallas::Base::random(&mut rng).to_repr()).unwrap();
        let mut actions = inner.actions().clone();
        if let Some(action) = actions.iter_mut().nth(action_index) {
            *action = orchard::Action::from_parts(
                nf,
                action.rk().clone(),
                action.cmx().clone(),
                action.encrypted_note().clone(),
                action.cv_net().clone(),
                action.authorization().clone(),
            ).expect("rk/epk unchanged, still non-identity");
        }
        *inner = orchard::Bundle::try_from_parts(
            actions,
            *inner.flags(),
            *inner.value_balance(),
            *inner.anchor(),
            inner.authorization().clone(),
            inner.bundle_version(),
        ).expect("proof length/flags/version unchanged");
    }
}

/// Replaces one Ironwood action's note commitment (cmx) with a fresh, independently-random
/// but valid Pallas base-field element, leaving nullifier/rk/ciphertexts/cv_net/the
/// aggregate proof/binding signature untouched. cmx is a public input to the action
/// circuit, so swapping it should be caught the same way as a swapped nullifier - see
/// `test_only_replace_ironwood_nullifier`'s doc comment for why the replacement is
/// generated as a field element.
pub(crate) fn test_only_replace_ironwood_cmx(
    bundle: &mut crate::ironwood_protocol::ironwood_bundle::Bundle,
    action_index: usize,
) {
    if let Some(inner) = bundle.0.as_mut() {
        let mut rng = thread_rng();
        let cmx = IronwoodCmx::from_bytes(&pallas::Base::random(&mut rng).to_repr()).unwrap();
        let mut actions = inner.actions().clone();
        if let Some(action) = actions.iter_mut().nth(action_index) {
            *action = orchard::Action::from_parts(
                *action.nullifier(),
                action.rk().clone(),
                cmx,
                action.encrypted_note().clone(),
                action.cv_net().clone(),
                action.authorization().clone(),
            ).expect("rk/epk unchanged, still non-identity");
        }
        *inner = orchard::Bundle::try_from_parts(
            actions,
            *inner.flags(),
            *inner.value_balance(),
            *inner.anchor(),
            inner.authorization().clone(),
            inner.bundle_version(),
        ).expect("proof length/flags/version unchanged");
    }
}

/// Replaces one Ironwood action's value commitment (cv_net) with a fresh,
/// independently-random but valid curve point, leaving nullifier/rk/cmx/ciphertexts/the
/// aggregate proof/binding signature untouched. cv_net is a public input to the action
/// circuit *and* is bound by the binding signature to the bundle's declared value
/// balance, so swapping it should be caught either way - see
/// `test_only_replace_ironwood_nullifier`'s doc comment for why the replacement is
/// generated as a real point.
pub(crate) fn test_only_replace_ironwood_cv(
    bundle: &mut crate::ironwood_protocol::ironwood_bundle::Bundle,
    action_index: usize,
) {
    if let Some(inner) = bundle.0.as_mut() {
        let mut rng = thread_rng();
        let cv = IronwoodValueCommitment::from_bytes(&pallas::Point::random(&mut rng).to_bytes()).unwrap();
        let mut actions = inner.actions().clone();
        if let Some(action) = actions.iter_mut().nth(action_index) {
            *action = orchard::Action::from_parts(
                *action.nullifier(),
                action.rk().clone(),
                action.cmx().clone(),
                action.encrypted_note().clone(),
                cv,
                action.authorization().clone(),
            ).expect("rk/epk unchanged, still non-identity");
        }
        *inner = orchard::Bundle::try_from_parts(
            actions,
            *inner.flags(),
            *inner.value_balance(),
            *inner.anchor(),
            inner.authorization().clone(),
            inner.bundle_version(),
        ).expect("proof length/flags/version unchanged");
    }
}

/// Replaces one Ironwood action's randomized verification key (rk) with a fresh,
/// independently-random but valid one (derived from a random signing key, which always
/// yields a valid, non-identity `VerificationKey`), leaving nullifier/cmx/ciphertexts/
/// cv_net/the aggregate proof/binding signature untouched. rk is a public input to the
/// action circuit *and* the key spend_auth_sig is verified against, so swapping it should
/// be caught either way.
pub(crate) fn test_only_replace_ironwood_rk(
    bundle: &mut crate::ironwood_protocol::ironwood_bundle::Bundle,
    action_index: usize,
) {
    if let Some(inner) = bundle.0.as_mut() {
        let mut rng = thread_rng();
        // SigningKey::try_from rejects a small fraction of byte strings (non-canonical
        // scalar encoding); retry with fresh randomness until one decodes.
        let signing_key = loop {
            let bytes: [u8; 32] = rng.gen();
            if let Ok(sk) = IronwoodSigningKey::<IronwoodSpendAuth>::try_from(bytes) {
                break sk;
            }
        };
        let rk = IronwoodVerificationKey::<IronwoodSpendAuth>::from(&signing_key);
        let mut actions = inner.actions().clone();
        if let Some(action) = actions.iter_mut().nth(action_index) {
            *action = orchard::Action::from_parts(
                *action.nullifier(),
                rk.clone(),
                action.cmx().clone(),
                action.encrypted_note().clone(),
                action.cv_net().clone(),
                action.authorization().clone(),
            ).expect("rk is a real, non-identity key");
        }
        *inner = orchard::Bundle::try_from_parts(
            actions,
            *inner.flags(),
            *inner.value_balance(),
            *inner.anchor(),
            inner.authorization().clone(),
            inner.bundle_version(),
        ).expect("proof length/flags/version unchanged");
    }
}
