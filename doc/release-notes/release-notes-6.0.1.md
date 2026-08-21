Notable changes
===============

Wallet RPC fix: z_viewtransaction
----------------------------------

`z_viewtransaction` bounded its Sapling outputs loop with the wrong
count function, so any transaction with more Sapling outputs than
Sapling spends — most commonly a plain shielded receive with zero
spends — had those outputs silently dropped from the RPC's report.
The RPC also never reported Ironwood spends or outputs at all. Both
are fixed: `z_viewtransaction` now reports Sapling and Ironwood spends
and outputs correctly, and the long-dead Sprout code path has been
removed from it.

A related bug was found in the same audit: `zs_listtransactions`,
`zs_gettransaction`, `zs_listspentbyaddress`, `zs_listreceivedbyaddress`,
`zs_listsentbyaddress`, and `getalldata` share an internal helper that
resolves archived (pruned-from-wallet) transactions using a cached set
of viewing keys, refreshing that cache when it might be stale. The
refresh check ran against a transaction object before it had actually
been loaded, so it never fired — an already-archived transaction whose
cached key set predated a later-added key (e.g. a Sapling internal
change key) could permanently under-report its spends. Fixed, and the
refresh now also covers Ironwood keys, which it never did before.

Upgrade notes
-------------

This is a patch release. No consensus or wallet-format changes; safe to
upgrade in place. Node operators primarily affected by the
`z_viewtransaction`/`zs_*` RPC fixes above should re-query any
previously-incomplete results after upgrading.

Changelog
=========

Cryptoforge:
  Fix z_viewtransaction Sapling/Ironwood gaps and stale-IVK archive bug.
  (580795f8f)
  Bump version to 6.0.1.50 (hotfix). (d2cbda2ee)
  Rework release pipeline: auto-release on master, sign from release/,
  add macOS Intel builds. (0f1f91aa5)
  Build Linux-hosted CI targets inside pinned ubuntu:20.04 Docker
  images. (b0cc10b2c)
  Scope the Linux CI Docker image's test run to pirate-gtest only.
  (61f78055f)
  Fix macOS CLI build failures: skip gtest, exclude libpsl from
  vendored curl. (56b4fb08c)
  Fix Docker-built VERSION missing its git-sha suffix; pin macOS
  runners. (8997532de)
