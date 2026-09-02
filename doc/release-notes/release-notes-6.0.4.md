Notable changes
===============

Offline shielded signing flow (z_createbuildinstructions*/z_buildrawtransaction) fixed
----------------------------------------------------------------------------------------

`z_createbuildinstructionscoincontrol` never called `SetChecksum()` before
returning its hex blob, so `z_buildrawtransaction`'s `ValidateChecksum()` (a
CRC-16 recomputed over the serialized bytes) would only ever pass by a
1-in-65536 coincidence - the offline round trip through this RPC was broken
end-to-end. Fixed with the missing checksum call.

Both `z_createbuildinstructions` and `z_createbuildinstructionscoincontrol`
gained full Ironwood support (coincontrol inputs take an optional
`"type": "sapling"|"ironwood"` field, defaulting to `"sapling"` for backward
compatibility; Ironwood output addresses are now accepted, cleanly gated on
network upgrade activation instead of throwing an uncaught internal error),
and now build against the correct next-block height instead of a value that
computed the wrong consensus branch ID whenever a network upgrade activated
within the window, while also silently doubling the intended transaction
expiry delta.

Change handling in this offline flow was architecturally wrong: the
*signing* side used to decide where change went, using whichever wallet
happened to be doing the signing - incorrect whenever that's a different,
dedicated spending-key-only wallet with no business deciding where the
*originating* wallet's change should land. The originating wallet
(`z_createbuildinstructions`/`z_createbuildinstructionscoincontrol`) now
resolves and bakes its own change destination (its `-changeaddress`
override, or its ZIP-32 default internal address derived from the source's
full viewing key) directly into the instructions at creation time;
`z_buildrawtransaction` now honors that baked-in choice instead of
consulting its own wallet's configuration.

`z_createbuildinstructions` also gained a check for one further bug: its
note-selection loop only ever stopped early once it had gathered enough
value, and never failed if it ran out of notes without reaching the
requested amount (for example, if funds moved or got locked between a
caller's `z_listunspent` snapshot and the RPC call actually running). This
silently produced a checksum-valid but unspendable blob, whose failure only
surfaced later and confusingly as `Change cannot be negative` from
`z_buildrawtransaction`. It now fails immediately with a clear
insufficient-funds error instead.

Finally, transparent addresses are no longer accepted as an output type by
either RPC (they were already rejected as a *source*), matching each
other's behavior.

z_listaddresses: change addresses are now labeled
----------------------------------------------------

Every Sapling and Ironwood account has an auto-derived internal (ZIP-32)
change address in addition to its normal external one - the wallet owns and
can spend from either, and both have always appeared in `z_listaddresses`'
output with no way to tell them apart. Picking the internal one as a
`fromaddress` by mistake is easy to do and easy to misdiagnose, since any
change from a spend against it has nowhere sensible to go but back to
itself.

`z_listaddresses` gained an optional `verbose` parameter (default `false`,
existing output format unchanged) that, when set, returns each address with
its `type` (`sprout`/`sapling`/`ironwood`) and `source`
(`normal`/`change`) instead of a bare string - using the same scope
tracking the Qt wallet's address list already relies on. Ironwood addresses
are also now included in this RPC's output at all (previously
Sprout/Sapling only).

Change-handling documentation corrected
------------------------------------------

`z_sendmany` and `z_sendmany_prepare_offline` both claimed "change
generated from a zaddr returns to itself" - inaccurate independently of
this release's other changes, since change actually goes to the sending
account's auto-derived internal address, or wherever `-changeaddress`
points if configured. Every RPC that consults `-changeaddress` now
describes its change behavior accurately, and `-changeaddress`'s own help
text names every RPC that reads it.

Upgrade notes
-------------

This is a patch release. No consensus or wallet-format changes; safe to
upgrade in place. Anyone using the offline `z_createbuildinstructions*` /
`z_buildrawtransaction` signing flow should upgrade - the coincontrol
variant was non-functional before this release. If change from a shielded
send is unexpectedly returning to the same address it was sent from, check
whether `-changeaddress` is set in your config before assuming a bug; run
`z_listaddresses true true` to see which of your addresses are internal
change addresses versus normal ones.

Changelog
=========

Cryptoforge:
  Fix broken z_createbuildinstructionscoincontrol offline-signing flow.
  (c5c2c78d5)
  Fix z_createbuildinstructions insufficient-funds gap, correct
  change-handling docs, add z_listaddresses source labeling.
  (88aeac054)
  Bump version to 6.0.4.50 (patch).
