Notable changes
===============

TLS is required for every peer connection
-----------------------------------------

Every P2P connection, inbound and outbound, in both `pirated` and the Qt
wallet, now has to complete a TLS handshake. There is no longer any way to
turn this off and no fallback to an unencrypted connection.

The `-tlsenforcement` and `-tlsfallbacknontls` options have been removed, along
with the "Only connect to encrypted peers" setting in the Qt wallet. **A node
whose configuration still sets either option will refuse to start** with an
error naming it; delete those lines from `PIRATE.conf` (or the command line)
before upgrading.

`-plaintextpeer=<ip>` remains as the one deliberate exception, for local
software such as mining-pool servers that cannot speak TLS. Three problems
with it are fixed:

- Tor delivers every inbound hidden-service connection from `127.0.0.1`, so
  allowing loopback for a local pool server also exempted every inbound Tor
  peer from TLS. Connections arriving through the onion listener are now never
  exempt.
- Each entry must now be a single IP address. A subnet was previously
  accepted, and logged as allowed, but never matched any connection.
- Addresses are compared in canonical form, so equivalent spellings of the
  same address (for example an IPv4-mapped IPv6 address) now match.

Peers older than v6.0.0 are disconnected
----------------------------------------

The protocol version is raised to 170015 and the minimum accepted peer version
to 170014. Nodes running v5.9.3 or earlier are disconnected; every 6.0.x
release is still accepted. The log line written when an obsolete peer is
disconnected now shows that peer's actual version instead of 0.

Network upgrade heights are fixed in the code
---------------------------------------------

The `-ac_sapling`, `-ac_overwinter` and `-ac_ironwood` options are removed. On
the PIRATE network, Overwinter and Sapling activate at block 152855 and
Ironwood at block 4161185, now compiled in rather than detected from block
timestamps. PIRATETST uses 50, 100 and 200. No other asset chain built from
this code activates these upgrades.

Checkpoints are added for the Sapling and Ironwood activation blocks and at
every 100,000 blocks up to block 4,168,315.

Shielded notes no longer lost by -deletetx during a reorg
---------------------------------------------------------

With `-deletetx` enabled (common on pool nodes), a wallet transaction that
was briefly in conflict during a chain reorganisation could be deleted from
the wallet even though one of its shielded notes was still unspent or being
spent. The note was also removed from the wallet's commitment tree, which
caused later spends to fail with "Merkle Path not found for Sapling note".
Conflicted transactions now go through the same checks as every other
deletion and are kept while they still hold an unspent note.

Qt wallet: crash when opening the address book
----------------------------------------------

The Qt wallet could crash at startup, or when opening a wallet, while sorting
the shielded address list. This is fixed. The same kind of fault, much less
likely to trigger, is also fixed in the transaction list's sorting.

The shielded address list also behaves better:

- A key group's total balance no longer includes the ungrouped addresses
  listed below it.
- The selected address stays selected when the list is re-sorted.
- The list keeps its sort order when an address is added or removed.
- Names containing numbers sort numerically, so "Key Group 2" comes before
  "Key Group 10".

Wallet files from newer versions
--------------------------------

When the wallet file contains a record type this version does not recognise,
for example one written by a future version, the record is now named in the
log and skipped, and the rest of the wallet loads normally. Previously such a
record was skipped without any log entry. The record is left untouched in the
file.

`debug=1` no longer includes lock tracing
-----------------------------------------

`debug=1` (or `debug=` with no value) enables every log category except
`threadlock`, which writes a line for every contended lock and is far more
verbose than any other category. Enable it by name: `debug=threadlock` on its
own, or alongside the blanket setting as `debug=1,threadlock`.

Upgrade notes
-------------

This is a patch release with no wallet-format or database changes. It is
safe to upgrade in place, and no reindex or rescan is needed.

Before starting the new version, remove any `tlsenforcement` or
`tlsfallbacknontls` lines from `PIRATE.conf`, or the node will not start.
`plaintextpeer` entries must name a single IP address rather than a subnet.

Changelog
=========

Cryptoforge:
  Hardcode network-upgrade activation heights; raise the peer protocol floor
  to v6.0.0; fix conflicted-tx deletion bug. (fb9c2f74a)
  Require TLS for every P2P connection, no toggle; keep -plaintextpeer as the
  sole per-IP exception. (ac07347c9)
  Exclude threadlock from blanket debug=1/debug=; require it explicitly.
  (a4e835ac3)
  Harden wallet.dat loading against unrecognized record types. (ed1fbced4)
  Fix z-address table sort crash and related ordering issues. (d23b866b0)
  Bump version to 6.0.9.50 (patch).
