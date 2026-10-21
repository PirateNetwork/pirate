Recommended update
==================

6.0.12 locks in the restarted chain with new checkpoints and improves block
download reliability. It is recommended for every node, and important for any
node still catching up after the restart.

This release makes no block-index format change, so **no reindex is required**
over 6.0.10 or 6.0.11 — it is a drop-in upgrade. A node upgrading from a version
older than 6.0.10 still performs the one-time reindex that 6.0.10 introduced,
and the checkpoints below ensure that reindex settles on the correct chain.

6.0.10 remains the mandatory security release; 6.0.12 builds on it.

Notable changes
===============

Checkpoints across the restart
------------------------------

Checkpoints have been added at the blocks around the point where the chain was
restarted: the last two blocks before the stall, the block where the chain
resumed, and a later block to lock the resumed chain in. A block offered at any
of these heights with a different hash is now rejected when it is received, so a
node building or syncing the chain cannot follow the branch that diverged at the
restart. A node syncing from scratch, or performing the 6.0.10 reindex, settles
directly on the restarted chain.

Block download timeouts are no longer inflated by disconnected peers
--------------------------------------------------------------------

When a peer with blocks still in flight disconnected, its outstanding requests
were removed but their count was not subtracted from the global in-flight total.
That phantom count stretched the download-timeout calculation for other peers,
in some cases by hours, slowing block download when peers churned. The count is
now corrected on disconnect, so download timeouts stay accurate.

Upgrading
=========

Stop the old node, install 6.0.12, and start it again. No reindex and no
configuration change is required when upgrading from 6.0.10 or 6.0.11.
