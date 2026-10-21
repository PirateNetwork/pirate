Recommended update
==================

6.0.14 improves how reliably a node keeps its header chain in sync and fixes the
blockchain bootstrap's wallet reset. It is recommended for every node.

This release makes no consensus change and no block-index format change, so
**no reindex is required** over 6.0.10 through 6.0.12 — it is a drop-in upgrade.
6.0.10 remains the mandatory security release; 6.0.14 builds on it.

Notable changes
===============

Header synchronization no longer stalls on a discarded chain
------------------------------------------------------------

Outbound header requests are now managed per peer: a single request is in flight
at a time, continuations are paced to respect peers' request limits, and an
unanswered request is retried on a steady timer. A peer that only ever replies
with headers for a block the node has already rejected (for example a peer still
on a chain the node has discarded) no longer counts as making progress, so it is
no longer able to hold the one header-sync slot and stall the node. Header-sync
progress is now recorded only when a batch is actually accepted, and the block
download bookkeeping no longer repeatedly re-walks a deep fork.

The node also keeps a lower-work announced chain as a fallback when a peer's
higher-work chain turns out to be invalid, so a discarded branch can no longer
hide another peer's valid blocks from download.

Recovering a tip that falls behind
-----------------------------------

Once a node is past initial sync it normally learns new blocks only from peer
announcements. If those announcements stop arriving — a missed message, or peers
that never announce to it — the node now notices when its tip has not advanced
for several block intervals and actively re-asks its peers for newer headers,
instead of waiting indefinitely. This is dormant while the tip is current, so it
adds no traffic on a healthy, synced node.

Blockchain bootstrap now resets the wallet correctly
----------------------------------------------------

Downloading the blockchain bootstrap is meant to clear the wallet's transactions
and rebuild them from the bootstrapped chain. A mistyped internal option left
that wipe disabled, so the bootstrap relied only on the rescan it performs and
could leave stale transaction data from the previous chain behind. The wallet is
now properly cleared (including the shielded Sapling and Ironwood state) and
rebuilt from the bootstrapped chain, both from the command line and from the GUI
bootstrap option.

Upgrading
=========

Stop the old node, install 6.0.14, and start it again. No reindex and no
configuration change is required.
