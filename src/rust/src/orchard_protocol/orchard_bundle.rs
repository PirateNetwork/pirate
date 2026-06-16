use std::{mem, ptr};

use libc::c_uchar;
use memuse::DynamicUsage;
use orchard::{
    bundle::Authorized,
    keys::{FullViewingKey, OutgoingViewingKey, PreparedIncomingViewingKey},
    note_encryption::OrchardDomain,
    primitives::redpallas::{Signature, SpendAuth},
};
use zcash_note_encryption::{try_note_decryption, try_output_recovery_with_ovk};
use zcash_primitives::transaction::components::orchard as orchard_serialization;
use zcash_protocol::value::ZatBalance as Amount;

use crate::{bridge::ffi, streams::CppStream};

pub struct Action(orchard::Action<Signature<SpendAuth>>);

impl Action {
    pub(crate) fn cv(&self) -> [u8; 32] {
        self.0.cv_net().to_bytes()
    }

    pub(crate) fn nullifier(&self) -> [u8; 32] {
        self.0.nullifier().to_bytes()
    }

    pub(crate) fn rk(&self) -> [u8; 32] {
        self.0.rk().into()
    }

    pub(crate) fn cmx(&self) -> [u8; 32] {
        self.0.cmx().to_bytes()
    }

    pub(crate) fn ephemeral_key(&self) -> [u8; 32] {
        self.0.encrypted_note().epk_bytes
    }

    pub(crate) fn enc_ciphertext(&self) -> [u8; 580] {
        self.0.encrypted_note().enc_ciphertext
    }

    pub(crate) fn out_ciphertext(&self) -> [u8; 80] {
        self.0.encrypted_note().out_ciphertext
    }

    pub(crate) fn spend_auth_sig(&self) -> [u8; 64] {
        self.0.authorization().into()
    }

    /// Compute nullifier for this action using provided viewing key components
    /// 
    /// Decrypts the action with the IVK derived from the FVK to obtain the note,
    /// then computes the nullifier. Using the FVK as the single source of truth
    /// ensures the IVK and nullifier derivation are always consistent.
    pub(crate) fn compute_nullifier(
        &self,
        fvk_bytes: &[c_uchar; 96],
        result: &mut [u8; 32],
    ) -> bool {
        // Deserialize FVK — single source of truth for both IVK derivation and nullifier
        let fvk = match FullViewingKey::from_bytes(fvk_bytes) {
            Some(k) => k,
            None => return false,
        };

        // Derive IVK from FVK (try external scope first, then internal)
        let ivk = fvk.to_ivk(orchard::keys::Scope::External);
        let prepared_ivk = PreparedIncomingViewingKey::new(&ivk);

        // Create the domain for decryption
        let domain = OrchardDomain::for_action(&self.0);
        
        // Try external scope decryption
        let decrypted = if let Some(r) = try_note_decryption(&domain, &prepared_ivk, &self.0) {
            r
        } else {
            // Try internal scope (change)
            let ivk_internal = fvk.to_ivk(orchard::keys::Scope::Internal);
            let prepared_ivk_internal = PreparedIncomingViewingKey::new(&ivk_internal);
            match try_note_decryption(&domain, &prepared_ivk_internal, &self.0) {
                Some(r) => r,
                None => return false,
            }
        };

        // Compute the nullifier from the decrypted note using the FVK
        let nullifier = decrypted.0.nullifier(&fvk);
        *result = nullifier.to_bytes();
        true
    }

    pub(crate) fn as_ptr(&self) -> *const ffi::ActionPtr {
        let ret: *const orchard::Action<Signature<SpendAuth>> = &self.0;
        ret.cast()
    }

    pub(crate) fn inner(&self) -> orchard::Action<Signature<SpendAuth>> {
        self.0.clone()
    }
}

#[derive(Clone)]
pub struct Bundle(Option<orchard::Bundle<Authorized, Amount>>);

pub(crate) fn none_orchard_bundle() -> Box<Bundle> {
    Box::new(Bundle(None))
}

pub(crate) unsafe fn orchard_bundle_from_raw_box(
    bundle: *mut ffi::OrchardBundlePtr,
) -> Box<Bundle> {
    Bundle::from_raw_box(bundle)
}

/// Parses an authorized Orchard bundle from the given stream.
pub(crate) fn parse_orchard_bundle(reader: &mut CppStream<'_>) -> Result<Box<Bundle>, String> {
    Bundle::parse(reader)
}

impl Bundle {
    pub(crate) unsafe fn from_raw_box(bundle: *mut ffi::OrchardBundlePtr) -> Box<Self> {
        Box::new(Bundle(if bundle.is_null() {
            None
        } else {
            let bundle: *mut orchard::Bundle<Authorized, Amount> = bundle.cast();
            Some(*Box::from_raw(bundle))
        }))
    }

    /// Returns a copy of the value.
    pub(crate) fn box_clone(&self) -> Box<Self> {
        Box::new(self.clone())
    }

    /// Parses an authorized Orchard bundle from the given stream.
    pub(crate) fn parse(reader: &mut CppStream<'_>) -> Result<Box<Self>, String> {
        match orchard_serialization::read_v5_bundle(reader) {
            Ok(parsed) => Ok(Box::new(Bundle(parsed))),
            Err(e) => Err(format!("Failed to parse Orchard bundle: {}", e)),
        }
    }

    /// Serializes an authorized Orchard bundle to the given stream.
    ///
    /// If `bundle == None`, this serializes `nActionsOrchard = 0`.
    pub(crate) fn serialize(&self, writer: &mut CppStream<'_>) -> Result<(), String> {
        orchard_serialization::write_v5_bundle(self.inner(), writer)
            .map_err(|e| format!("Failed to serialize Orchard bundle: {}", e))
    }

    pub(crate) fn inner(&self) -> Option<&orchard::Bundle<Authorized, Amount>> {
        self.0.as_ref()
    }

    pub(crate) fn as_ptr(&self) -> *const ffi::OrchardBundlePtr {
        if let Some(bundle) = self.inner() {
            let ret: *const orchard::Bundle<Authorized, Amount> = bundle;
            ret.cast()
        } else {
            ptr::null()
        }
    }

    /// Returns the amount of dynamically-allocated memory used by this bundle.
    pub(crate) fn recursive_dynamic_usage(&self) -> usize {
        self.inner()
            // Bundles are boxed on the heap, so we count their own size as well as the size
            // of `Vec`s they allocate.
            .map(|bundle| mem::size_of_val(bundle) + bundle.dynamic_usage())
            // If the transaction has no Orchard component, nothing is allocated for it.
            .unwrap_or(0)
    }

    /// Returns whether the Orchard bundle is present.
    pub(crate) fn is_present(&self) -> bool {
        self.0.is_some()
    }

    pub(crate) fn actions(&self) -> Vec<Action> {
        self.0
            .iter()
            .flat_map(|b| b.actions().iter())
            .cloned()
            .map(Action)
            .collect()
    }

    pub(crate) fn num_actions(&self) -> usize {
        self.inner().map(|b| b.actions().len()).unwrap_or(0)
    }

    /// Returns the action at the given index, or an error if out of range.
    pub(crate) fn get_action(&self, action_index: usize) -> Result<Box<Action>, String> {
        self.inner()
            .and_then(|b| b.actions().get(action_index).map(|a| Box::new(Action(a.clone()))))
            .ok_or_else(|| format!("No Orchard action at index {}", action_index))
    }

    /// Returns whether the Orchard bundle is present and spends are enabled.
    pub(crate) fn enable_spends(&self) -> bool {
        self.inner()
            .map(|b| b.flags().spends_enabled())
            .unwrap_or(false)
    }

    /// Returns whether the Orchard bundle is present and outputs are enabled.
    pub(crate) fn enable_outputs(&self) -> bool {
        self.inner()
            .map(|b| b.flags().outputs_enabled())
            .unwrap_or(false)
    }

    /// Returns the value balance for this Orchard bundle.
    ///
    /// A transaction with no Orchard component has a value balance of zero.
    pub(crate) fn value_balance_zat(&self) -> i64 {
        self.inner()
            .map(|b| b.value_balance().into())
            // From section 7.1 of the Zcash prototol spec:
            // If valueBalanceOrchard is not present, then v^balanceOrchard is defined to be 0.
            .unwrap_or(0)
    }

    /// Returns the anchor for the bundle.
    ///
    /// # Panics
    ///
    /// Panics if the bundle is not present.
    pub(crate) fn anchor(&self) -> [u8; 32] {
        self.inner()
            .expect("Bundle actions should have been checked to be non-empty")
            .anchor()
            .to_bytes()
    }

    /// Returns the proof for the bundle.
    ///
    /// # Panics
    ///
    /// Panics if the bundle is not present.
    pub(crate) fn proof(&self) -> Vec<u8> {
        self.inner()
            .expect("Bundle actions should have been checked to be non-empty")
            .authorization()
            .proof()
            .as_ref()
            .to_vec()
    }

    /// Returns the binding signature for the bundle.
    ///
    /// # Panics
    ///
    /// Panics if the bundle is not present.
    pub(crate) fn binding_sig(&self) -> [u8; 64] {
        self.inner()
            .expect("Bundle actions should have been checked to be non-empty")
            .authorization()
            .binding_signature()
            .into()
    }

    /// Returns whether all actions contained in the Orchard bundle can be decrypted with
    /// the all-zeros OVK.
    ///
    /// Returns `true` if no Orchard actions are present.
    ///
    /// This should only be called on an Orchard bundle that is an element of a coinbase
    /// transaction.
    pub(crate) fn coinbase_outputs_are_valid(&self) -> bool {
        if let Some(bundle) = self.inner() {
            for act in bundle.actions() {
                if try_output_recovery_with_ovk(
                    &OrchardDomain::for_action(act),
                    &OutgoingViewingKey::from([0u8; 32]),
                    act,
                    act.cv_net(),
                    &act.encrypted_note().out_ciphertext,
                )
                .is_none()
                {
                    return false;
                }
            }
        }

        // Either there are no Orchard actions, or all of the outputs
        // are decryptable with the all-zeros OVK.
        true
    }
}
