Notable changes
===============

Wallet RPC fix: z_shieldcoinbase to a Sapling address
------------------------------------------------------

Every `z_shieldcoinbase` call targeting a Sapling z-address failed with:

    Runtime error: TransactionBuilder cannot add Sapling output without
    Sapling builder (call InitializeSapling first)

The Sapling handler in `AsyncRPCOperation_shieldcoinbase` built its
transaction from transparent coinbase inputs straight into
`SendChangeTo()` without first calling `InitializeSapling()`. That call is
normally made implicitly when a Sapling spend is added, but shielding is an
outputs-only case — transparent inputs, zero Sapling spends — so nothing
else on that path initialized the builder. The equivalent Ironwood handler
already called `InitializeIronwood()` in the same spot, which is what
existing coverage never caught: no test exercised the Sapling handler's
actual transaction-building path, only its constructor-level parameter
validation.

Fixed by initializing the Sapling builder before adding the change output,
matching the pattern already used for the same outputs-only case in
`z_mergetoaddress`. Also added a regression test that builds a real
Sapling shield-coinbase transaction end-to-end.

Upgrade notes
-------------

This is a patch release. No consensus or wallet-format changes; safe to
upgrade in place. Node/wallet operators who saw `z_shieldcoinbase` fail
with the "InitializeSapling" runtime error above should upgrade and
retry — Ironwood-destined shielding was never affected.

Changelog
=========

Cryptoforge:
  Fix z_shieldcoinbase to a Sapling address always throwing "cannot add
  Sapling output without Sapling builder"; add end-to-end regression test.
  (d5a1fe4b5)
