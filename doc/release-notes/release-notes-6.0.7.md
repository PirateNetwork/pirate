Notable changes
===============

Ironwood key derivation now follows ZIP-32
------------------------------------------

Releases 6.0.0 through 6.0.6 derived Ironwood account keys in a way that is not
ZIP-32. A developer building a compatible wallet reported it, and it was checked
against the official Orchard ZIP-32 test vectors. Two things differed from the
standard:

- The child index was hashed padded with 28 zero bytes (32 bytes) instead of as
  the 4-byte little-endian value ZIP-32 specifies.
- Each child's `parent_fvk_tag` was taken from the child's own full viewing key
  instead of from its parent's.

Both affect the key material or its serialized form, so the same seed produced
different Ironwood keys and addresses here than in any other ZIP-32
implementation. Sapling derivation was never affected.

This release derives Ironwood keys the standard way. Ironwood activates on
mainnet on October 3 2026, so this is fixed before most wallets hold any
Ironwood funds; nothing about the consensus rules changes.

**Existing wallets are not migrated.** A wallet keeps every key it already holds.
A wallet that created its primary Ironwood key under 6.0.0 - 6.0.6 keeps that
key, and `z_getnewaddress` keeps handing out addresses from it exactly as
before. Funds sent to those addresses stay spendable by the wallet that holds
their keys. Only keys created from now on follow ZIP-32:

- A new wallet, or a wallet that has no primary Ironwood key yet, gets standard
  ZIP-32 keys.
- `z_getnewaddresskey ironwood` derives the next ZIP-32 account.

New option: `legacy`
--------------------

`z_getnewaddress` and `z_getnewaddresskey` take an optional second argument,
`legacy` (boolean, default false), for Ironwood only. With `true` they derive
from the old scheme, so keys and addresses created by 6.0.0 - 6.0.6 can be
reached again, for example after restoring from a seed:

    pirate-cli z_getnewaddresskey ironwood true
    pirate-cli z_getnewaddress ironwood true

- `z_getnewaddresskey ironwood true` walks the legacy accounts in order (0, 1,
  2, ...), skipping any the wallet already holds. Call it once per legacy key the
  wallet used to have.
- `z_getnewaddress ironwood true` returns a diversified address of the first
  legacy account, adding that key to the wallet first if it is not held.
- The `legacy` option does not touch the ZIP-32 account counter or the primary
  key, so the standard and legacy sequences advance independently.
- Using it with `sapling` is an error.

Restoring from a seed
---------------------

The seed alone no longer implies the wallet's complete set of Ironwood keys. A
wallet restored from a seed on 6.0.7:

1. derives standard ZIP-32 keys by default, and
2. only regains legacy keys if `z_getnewaddresskey ironwood true` is called once
   per legacy account the old wallet used.

After restoring legacy keys, rescan (`-rescan`, or `rescanblockchain` from the
start of Ironwood activity); the wallet does not see funds sent to a key until it
has scanned the blocks that paid it.

If you created Ironwood addresses on 6.0.0 - 6.0.6, keep the wallet file backup
as well as the seed. If you are unsure whether a wallet holds legacy Ironwood
keys, `z_listaddresses` on the old wallet shows every Ironwood address it holds.

Writing a compatible wallet
---------------------------

For other wallet developers, an Ironwood account key is standard ZIP-32 Orchard
derivation with these parameters:

- Seed: the 64-byte BIP-39 seed (empty passphrase) of the wallet's mnemonic.
- Path: `m/32'/coin_type'/account'`, all hardened; coin type 141 on mainnet, 1 on
  testnet and regtest.
- The first address of account `n` is the default (diversifier index 0) external
  address of that account's key.
- `parent_fvk_tag` is the first 4 bytes of the parent's full viewing key
  fingerprint.

Wallets created by 6.0.0 - 6.0.6 do not follow this and cannot be recovered by a
standard implementation from the seed; the `legacy` option above is the way to
regain them in this wallet.

Tests
-----

- The official Orchard ZIP-32 vectors (`orchard/src/test_vectors/zip32.rs`) now
  run against the node's derivation: master key, every level of the m/1'/2'/3'
  vector, fingerprints and tags.
- Known-answer tests pin both the standard and the legacy account key, tag and
  child index for a fixed seed, including values computed by an independent
  implementation.
- RPC tests cover the `legacy` option on both RPCs, the independence of the two
  sequences, and a wallet whose primary key is already a legacy key.

Upgrade notes
-------------

This is a patch release. There are no consensus, wallet-format or database
changes and no rescan is needed for an in-place upgrade; existing keys are read
from the wallet as before. Upgrade before Ironwood activates on mainnet
(October 3 2026), so that no wallet creates Ironwood keys with the old
derivation.

Changelog
=========

Cryptoforge:
  Derive Ironwood keys per ZIP-32 (4-byte child index, parent FVK tag) and add
  the `legacy` option to z_getnewaddress / z_getnewaddresskey. (c3f4aa60b)
  Bump version to 6.0.7.50 (patch).
