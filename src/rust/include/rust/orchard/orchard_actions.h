// Copyright (c) 2021-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef PIRATE_RUST_INCLUDE_RUST_ORCHARD_ACTIONS_H
#define PIRATE_RUST_INCLUDE_RUST_ORCHARD_ACTIONS_H


#ifdef __cplusplus
extern "C" {
#endif

bool try_orchard_decrypt_action_ivk(
    const ActionPtr* orchard_action,
    const unsigned char* ivk_bytes,
    uint64_t *value_out,
    unsigned char *address_out,
    unsigned char *memo_out,
    unsigned char *rho_out,
    unsigned char *rseed_out
);

bool try_orchard_decrypt_action_fvk(
    const ActionPtr* orchard_action,
    const unsigned char* fvk_bytes,
    uint64_t *value_out,
    unsigned char *address_out,
    unsigned char *memo_out,
    unsigned char *rho_out,
    unsigned char *rseed_out,
    unsigned char *nullifier_out
);

bool get_nullifer_from_parts(
    const unsigned char* fvk_bytes,
    const unsigned char* address_bytes,
    const uint64_t u64,
    const unsigned char* rho_bytes,
    const unsigned char* rseed_bytes,
    unsigned char *nullifier_out
);

#ifdef __cplusplus
}
#endif

#endif // PIRATE_RUST_INCLUDE_RUST_ORCHARD_ACTIONS_H
