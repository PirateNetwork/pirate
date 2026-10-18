Mandatory update
================

6.0.10 is a mandatory security release. Every node — wallets, full nodes,
miners, pool operators and exchanges — must upgrade. Versions 6.0.9 and earlier
contain consensus flaws that allow invalid transactions to be accepted into a
block; a node still running an older version can follow an invalid chain.

On first start this release rebuilds its block index (a one-time reindex,
triggered automatically) so the corrected consensus rules are applied to the
existing chain. The reindex can take a while on a full node; do not interrupt
it.

Notable changes
===============

Coin-import transactions are fully validated again
--------------------------------------------------

Block validation no longer skips the input and burn-proof checks for
import-style transactions. Previously such a transaction could be included in a
block without proving a corresponding burn, which allowed coins to be created
without backing. Import transactions are now verified in block connection
exactly as they are in the mempool, and a block containing an unproven import is
rejected.

Output address classification can no longer be spoofed
------------------------------------------------------

The code that determines a transparent output's address treated certain
non-standard scripts as time-locked (CLTV) outputs and read the address from the
wrong place, so a script could be made to report an address it does not actually
pay. On Pirate, where transparent outputs are restricted by address, this could
be used to place funds in an output that passed the restriction while remaining
spendable by the author. Script classification now requires the exact,
canonical time-lock layout, so an output's reported address always matches what
the script actually requires.

Hardening of attacker-supplied parsing
---------------------------------------

Values read from a coin import's embedded burn data are now range-checked before
use, and the notarization-script decoder is bounded against its buffer. Neither
was reachable to exploit on Pirate, but both read untrusted input and are now
defensive.

Wallet: note locking across all shielded operations
----------------------------------------------------

Sweep, consolidation, z_sendmany and z_mergetoaddress now lock the notes they
select for the whole operation and release them reliably, and a committed
transaction is recorded in the wallet immediately instead of up to a second
later. This removes a race in which two concurrent operations could select the
same note — the cause of sweep/consolidation transactions being rejected as
duplicate spends. Sweep now reports an error when a transaction it builds is
rejected, rather than reporting success. A shutdown use-after-free in a queued,
never-run z_mergetoaddress was also fixed.

Diagnostics
-----------

The daemon now logs the stored block-index version against the client and
minimum versions on startup, and whether checkpoints are enabled. Ironwood
double-spend detections are logged by default, matching Sapling.

Upgrade notes
-------------

This upgrade is mandatory and cannot be skipped. There are no wallet-format
changes, but the block index is rebuilt automatically on first start because the
minimum index version was raised; no action is required beyond allowing the
reindex to finish.

Miners and pool operators should upgrade before resuming block production:
blocks that an older node would accept are rejected by 6.0.10.

Changelog
=========

Cryptoforge:
  Require proof of burn for coin imports in ConnectBlock. (da4e6252a)
  Reject non-canonical CLTV scripts so output addresses can't be spoofed.
  (3913cee40)
  Clamp attacker-supplied values in coin-import and notarization parsing.
  (051003ebe)
  Lock notes for every async transaction builder; record spends at commit.
  (3c14cd9bd)
  Force reindex on upgrade past 6.0.10.00 and log the block-index version.
  (04e2e00fa)
  Handle dependency HTTP errors and canonical mirror filenames. (481dcfe2a)
  Fetch UTF-CPP directly from GitHub's archive service. (9816cf380)
  Bump version to 6.0.10.50.
