Notable changes
===============

Block template crash at shielded subtree boundaries fixed
---------------------------------------------------------

Nodes that build block templates - solo miners, staking nodes, and mining
pool nodes serving `getblocktemplate` - could abort with the assertion
`CBlockIndex::GetBlockHash(): Assertion 'phashBlock' failed` (a crash to
desktop in the Qt wallet). It happened whenever a candidate block's Sapling or
Ironwood outputs completed a 65,536-output subtree, which is a boundary the
chain crosses periodically as shielded activity grows.

The node records subtree completion metadata (the subtree root, its height,
and the hash of the block that completed it). While validating a candidate
block, `ConnectBlock()` runs against a temporary block index that has no hash
pointer yet, and it read the completing block's hash through that index -
which trips the assertion. Since a candidate block is never persisted, the
crash happened even though nothing was being written.

`ConnectBlock()` now takes the hash directly from the block being validated,
for both Sapling and Ironwood. For blocks that are actually connected to the
chain this is the same value as before, so the subtree metadata recorded on
disk is unchanged. Validation of blocks received from peers was never
affected: those blocks always carry a real block index entry.

Regression tests were added for both Sapling and Ironwood: a real shielding
transaction that completes a subtree is connected through an unregistered
block index with a null hash pointer, exactly as template validation does,
and the tests check that the completed frontier, the subtree index, and the
best-block marker come out as expected.

Upgrade notes
-------------

This is a patch release. There are no consensus, wallet-format, or database
changes; it is safe to upgrade in place, and no reindex or rescan is needed.

Miners, stakers, and mining pool operators should upgrade promptly: any node
that builds a block template containing the shielded output that completes a
subtree will otherwise abort at that point, and will abort again on restart
whenever its next template contains one. Nodes that neither mine nor stake
nor serve `getblocktemplate` (including proposal mode) were not exposed to
this crash.

Changelog
=========

Øswald Kardingson:
  fix(consensus): use candidate block hash for completed subtrees.
  (57e95b76b)
  test(consensus): cover subtree boundaries with temporary block indexes.
  (bcb7d625b)

Cryptoforge:
  Bump version to 6.0.5.50 (patch).
