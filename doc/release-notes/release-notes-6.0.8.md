Notable changes
===============

Offline signing no longer trusts an unverified change address
---------------------------------------------------------------

Cold-storage signing splits a spend across two machines: an online wallet
selects notes and builds an unsigned transaction, and an offline wallet
holding the spending key signs it. The offline wallet is the one meant to be
trusted with where the funds actually go.

`z_buildrawtransaction` read the change address for the transaction from the
build instructions supplied by the online wallet, and signed to it without
checking that the offline wallet held the key for that address. An online
wallet that was compromised, or a build-instructions file tampered with in
transit, could therefore select more input value than the visible payment
needed and send the difference to an address of its own choosing.
`decoderawtransaction`, and the sign dialog in the Qt wallet, also did not show
the change address or the fee, so a tampered file looked the same as a normal
one under review before signing.

`z_buildrawtransaction` now checks that the offline wallet holds the spending
key for the instructed change address, and refuses to sign if it does not - a
wallet that only holds a viewing key for that address (for example, one
imported to track someone else's funds) is rejected too, since a change note
it can see but not spend is lost just the same. `decoderawtransaction` and the
Qt sign dialog now also show the change address and the fee, so a tampered
build instructions file is visible even without hitting the new check.

Anyone who signs transactions on an offline machine should upgrade both the
online and offline wallets before their next spend.

z_getbalance and note-selection RPCs no longer stall for seconds
------------------------------------------------------------------

On a wallet with several thousand transactions, `z_getbalance` and every other
RPC that selects or filters shielded notes could hold the wallet locked for
over ten seconds, driven by three unrelated costs in the same code path: a
disk read to look up a transaction's confirming block that the wallet already
had in memory, decrypting every note in the wallet before checking whether it
even matched the caller's filters, and a full transaction copy for every note
considered.

All three are fixed: block height now comes from the wallet's own in-memory
index, a note is decrypted only if it survives the caller's filters first
(spending key, address, spent state), and a note that does match is
reconstructed from the wallet's existing cache with no decryption at all - the
memo field is the one exception, decrypted only when an RPC that actually
returns memos asks for one. Wallets with a large transaction history should
see markedly faster `z_getbalance`/`z_listunspent`-family calls and less lock
contention with other RPCs while they run.

Upgrade notes
-------------

This is a patch release. There are no consensus, wallet-format or database
changes; it is safe to upgrade in place, and no reindex or rescan is needed.

Changelog
=========

Cryptoforge:
  Reject offline-signing change addresses the wallet can't spend from.
  (57ae08728)
  Speed up GetFilteredNotes: skip disk reads, decryption, and copies.
  (f9c2fd253)
  Bump version to 6.0.8.50 (patch).
