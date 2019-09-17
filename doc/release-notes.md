(note: this is a temporary file, to be added-to by anybody, and moved to
release-notes at release time)

Notable changes
===============

Low-level RPC changes
---------------------

- Bare multisig outputs to our keys are no longer automatically treated as
  incoming payments. As this feature was only available for multisig outputs for
  which you had all private keys in your wallet, there was generally no use for
  them compared to single-key schemes. Furthermore, no address format for such
  outputs is defined, and wallet software can't easily send to it. These outputs
  will no longer show up in `listtransactions`, `listunspent`, or contribute to
  your balance, unless they are explicitly watched (using `importaddress` or
  `importmulti` with hex script argument). `signrawtransaction*` also still
  works for them.

Disabling old Sprout proofs
---------------------------

As part of our ongoing work to clean up the codebase and minimise the security
surface of `komodod`, we are removing `libsnark` from the codebase, and dropping
support for creating and verifying old Sprout proofs. Funds stored in Sprout
addresses are not affected, as they are spent using the hybrid Sprout circuit
(built using `bellman`) that was deployed during the Sapling network upgrade.

This change has several implications:

- `komodod` no longer verifies old Sprout proofs, and will instead assume they
  are valid. This has a minor implication for nodes: during initial block
  download, an adversary could feed the node fake blocks containing invalid old
  Sprout proofs, and the node would accept the fake chain as valid. However,
  `komodod` internally contains checkpoints after Sapling activation for both
  block heights and cumulative chain work, and does not exit the initial block
  download phase until the active chain contains at least as much work as the
  checkpointed chain work. The node would therefore be non-functional (and would
  not broadcast the fake chain to other peers) until the fake chain contained as
  much work as the main chain, making this a 50% + 1 attack, which the current
  consensus rules already does not protect against.

- Shielded transactions can no longer be created before Sapling has activated.
  This does not affect Komodo itself, but will affect assetchains / ACs that
  have not yet activated Sapling (or that start a new chain after this point and
  do not activate Sapling from launch). Note that the old Sprout circuit is
  [vulnerable to counterfeiting](https://z.cash/support/security/announcements/security-announcement-2019-02-05-cve-2019-7167/)
  and should not be used in current deployments.

- Starting from this release, the circuit parameters from the original Sprout
  MPC are no longer required to start `komodod`, and will not be downloaded by
  `fetch-params.sh`. They are not being automatically deleted at this time.

We would like to take a moment to thank the `libsnark` authors and contributors.
It was vital to the success of Komodo, and the development of zero-knowledge
proofs in general, to have this code available and usable.
