// Copyright (c) 2021-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef PIRATE_RUST_INCLUDE_RUST_ORCHARD_PIRATE_KEYS_H
#define PIRATE_RUST_INCLUDE_RUST_ORCHARD_PIRATE_KEYS_H

#include "rust/streams.h"

#ifdef __cplusplus
extern "C" {
#endif

bool orchard_ivk_to_address(const unsigned char *ivk_bytes, const unsigned char *diversifier_index, unsigned char *out_bytes);

bool orchard_fvk_to_ovk(const unsigned char *fvk_bytes, unsigned char *out_bytes);
bool orchard_fvk_to_ovk_internal(const unsigned char *fvk_bytes, unsigned char *out_bytes);
bool orchard_fvk_to_ivk(const unsigned char *fvk_bytes, unsigned char *out_bytes);
bool orchard_fvk_to_ivk_internal(const unsigned char *fvk_bytes, unsigned char *out_bytes);
bool orchard_fvk_to_default_address(const unsigned char *fvk_bytes, unsigned char *out_bytes);
bool orchard_fvk_to_default_address_internal(const unsigned char *fvk_bytes, unsigned char *out_bytes);
bool orchard_fvk_to_address(const unsigned char *fvk_bytes, const unsigned char *diversifier_index, unsigned char *out_bytes);
bool orchard_fvk_to_address_internal(const unsigned char *fvk_bytes, const unsigned char *diversifier_index, unsigned char *out_bytes);


bool orchard_sk_is_valid(const unsigned char *sk_bytes);
bool orchard_sk_to_fvk(const unsigned char *sk_bytes, unsigned char *out_bytes);
bool orchard_sk_to_default_address(const unsigned char *sk_bytes, unsigned char *out_bytes);
bool orchard_sk_to_default_address_internal(const unsigned char *sk_bytes, unsigned char *out_bytes);

/// Derive the Zip32 Master ExtendedSpendingKey from a seed.
bool orchard_derive_master_key(const unsigned char* seed, size_t seed_len, unsigned char *bytes_out);
bool orchard_derive_child_key(const unsigned char* xsk_bytes, uint32_t coin_type, uint32_t account, unsigned char *bytes_out);




#ifdef __cplusplus
}
#endif

#endif // PIRATE_RUST_INCLUDE_RUST_ORCHARD_KEYS_H
