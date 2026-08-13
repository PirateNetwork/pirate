Notable changes
===============

Ironwood: a new shielded pool
------------------------------

Version 6.0.0 introduces **Ironwood**, a new shielded transaction pool built
on the Halo2 proving system over the Pallas curve, with its own v6
transaction format and `UPGRADE_IRONWOOD` consensus activation. Ironwood
began life as an integration of Zcash's Orchard protocol and was
subsequently evolved into a distinct pool, collapsing what had briefly been
separate Orchard and Ironwood code paths into a single pool and renaming the
remaining Orchard-derived identifiers, persistence tags, and RPC surface to
Ironwood throughout.

Sapling and Ironwood payment disclosure are both supported: users can
disclose a key that decrypts a single Sapling output or Ironwood action
without exposing their full spending key. Sapling and Ironwood shielded
address-book handling was brought to parity, and nullifier computation was
aligned across both pools and moved into the Rust FFI bridge.

`GetFilteredNotes` — used by manual note selection as well as the
auto-consolidation and auto-sweep services — gained a bounded streaming
dual-heap selection algorithm, and this was subsequently extended to cover
Ironwood in addition to Sapling.

Underlying dependencies (`orchard`, `librustzcash`, `zcash_note_encryption`)
were updated to rebased forks tracking upstream, including a pull of the
fix for the Orchard double-spend exploit identified upstream in Zcash
0.14.0.

The Ironwood network upgrade's mainnet activation is now set for Sat Oct
3 2026, 12:00 PM PDT (19:00 UTC) — two weeks after the notary
`requiredSigs` update (see "Notary requiredSigs and Notaries RPC tools"
below). Unlike that height-based activation, Ironwood activates from a
real-world timestamp: each node independently derives the actual
activation height at runtime, from the first block whose timestamp
crosses this threshold, so no specific height needs to be agreed on in
advance.

Security hardening and DoS mitigation
--------------------------------------

A broad, dated pass of defensive fixes across the networking and parsing
layers:

- Configurable mempool size and time bounds.
- Rate limiting and size caps for `GETBLOCKS`/`GETHEADERS` locators, nSPV
  payloads, alert messages, and the orphan transaction pool.
- `MAX_INV_SZ` reduced from 50,000 to 5,000 and `MAX_INBOUND_FROMIP`
  reduced from 5 to 2.
- Fixed an addr-relay amplification issue in `SendMessages`, and capped
  `mapRelay` size to bound relay-cache memory.
- Peers sending malformed messages are now penalized.
- Bounds checks added for untrusted komodo opreturn/coinbase parsing,
  nSPV parsers, and `GetCryptoCondition` fulfillment length handling.
- Oversized bloom filter `vData` is rejected before allocation.
- `listtransactions` and `getblockhashes` RPCs hardened against bad
  parameters.
- Sapling zk-proof uniqueness is now enforced at the consensus/mempool
  layer, alongside a fix to the mempool index lifecycle, to close a
  duplicate-nullifier acceptance gap.
- Sapling anchor lookups are cached per transaction to reduce redundant
  validation work.
- Fixed a NULL-pointer-write crash in `init_string()` on allocation
  failure (mirroring an existing fix already applied to
  `accumulatebytes()` in the same file).
- P2P listen-socket binding now happens immediately after the datadir
  lock, before script-check threads, the scheduler, Sapling parameter
  loading, or the RPC/HTTP server are started. A bind failure (blocked
  port, bad `-bind`/`-whitebind`) now fails fast in milliseconds instead
  of after standing up — and then having to tear back down — the full
  service stack, avoiding spurious restart-timeout trips under an
  external process supervisor.

Wallet security
----------------

- The wallet master-key KDF was hardened with scrypt, with automatic
  upgrade of legacy wallets to the new KDF.
- The HD chain, destdata, and note-commitment trees are now encrypted at
  rest.
- Stale Sprout zkeymeta records are erased on wallet load.
- The wallet now automatically rescans on a transaction serialization
  error encountered while loading, ensuring notes are initialized before
  any `deletetx` is executed against them.

Bundled Tor and I2P networking
--------------------------------

Tor and I2P are now embedded and auto-managed as subprocesses of the
wallet, supervised by a new `pirate-networking` watchdog. The bundled
`tor`/`i2pd` binaries were renamed to avoid collisions with any
system-installed copies, and a header-sync stall caused by
`mapBlocksInFlight`-based throttling was fixed.

- The embedded daemons are now automatically restarted (with backoff) if
  either crashes or is OOM-killed mid-run. `pirate-networking`'s
  liveness check previously matched a tracked pid against
  `/proc/<pid>/comm`, which always failed for i2pd — it renames its own
  comm to `i2pd-daemon` at runtime — leaving a crashed i2pd as an
  unreaped zombie still holding the SAM port and blocking any restart.
  It now matches against `/proc/<pid>/exe` instead, which reflects the
  actually-exec'd binary regardless of any later self-renaming.
- Startup no longer blocks for up to 180 seconds per daemon waiting for
  Tor/I2P's control or SAM port to come up; that wait now runs on its
  own thread instead of stalling the entire node's initialization.
- Outbound I2P dialing now runs on its own dedicated thread. Previously
  it shared `ThreadOpenConnections()` with ordinary clearnet/Tor
  dialing, so a slow or unresponsive I2P client — each SAM round trip
  inside `Connect()` can block up to 3 minutes, and `Connect()` can make
  several in sequence — froze clearnet and Tor dialing for the same
  duration.
- Fixed a shutdown crash: an in-flight I2P SAM call could still be
  holding a `Session`'s lock at the exact moment shutdown destroyed that
  `Session`, aborting the process. The destructor now waits for any
  in-flight call to finish before letting member destruction proceed.
- Fixed a deadlock between the peer list and the I2P relay pool
  (`cs_vNodes`/`cs_i2p_relay_pool`, locked in opposite order by the
  socket handler's disconnect loop and the pool's own identity-retirement
  logic) that could freeze all network processing while leaving the node
  responsive only to a shutdown command.
- Fixed a use-after-free crash in I2P relay-pool identity rotation:
  routine rotation on a long-running node could free a slot's `Session`
  out from under another thread still using it. The slot now holds a
  `shared_ptr` instead of a `unique_ptr`, so a reader pins the object
  alive for as long as it's actually in use.
- `getnetworkinfo` gained a `torcontrol` field reporting the Tor control
  port actually in use — the embedded daemon reassigns it at startup if
  the configured port is taken, without ever updating `PIRATE.conf` — so
  a sibling process (e.g. lightwalletd, publishing its own hidden
  service via the same Tor daemon) can discover the real value instead
  of trusting a possibly-stale configured/default one.

Peer-to-peer networking and transaction privacy
---------------------------------------------------

- Locally-originated transactions (wallet sends, `sendrawtransaction`, the
  nSPV broadcaster, and CC module transactions) are now relayed only to
  privacy peers by default — outbound Tor, or inbound/outbound I2P — so a
  network of clearnet-monitoring nodes can no longer correlate a
  transaction back to the sending node's IP address by observing which
  peer first announced it. Transactions relayed on behalf of another peer
  are unaffected and continue going to all peers as before. Falls back to
  relaying via all peers if no privacy peer is currently connected, unless
  that fallback is explicitly disabled.
- ADDRv2 is now required from peers once Ironwood activates: a peer that
  requests addresses without ever having negotiated ADDRv2 support is
  disconnected instead of served a downgraded, pre-BIP155 response that
  can't represent Tor v3/I2P addresses.
- Tor hidden-service connections now use a dedicated internal listening
  socket, separate from the main clearnet listener, so inbound Tor peers
  can be reliably distinguished from other loopback-sourced connections.
  This fixed a latent bug where the per-source-IP inbound connection cap
  never actually worked (it compared against an uninitialized buffer) —
  fixing that in isolation would otherwise have collapsed every inbound
  Tor peer into "the same source" and capped the node to just two
  concurrent inbound Tor connections. Inbound connections from local
  addresses that aren't genuinely Tor-forwarded are now rejected outright.
- Outbound connection selection now targets a diversified spread across
  reachable networks (IPv4/IPv6/Tor/I2P/CJDNS) instead of letting
  whichever network is best represented in the address database claim
  every outbound slot.
- Fixed an address-manager bug that discarded every Tor/I2P/CJDNS address
  gossiped to a node before it was ever stored, regardless of the
  receiving node's own network configuration — breaking propagation of
  those addresses through the wider gossip network. Also fixed inverted
  reachability weighting in address selection, and a legacy `GETADDR`
  response leak that could return addresses on networks a pre-ADDRv2 peer
  could never use.
- Fixed I2P proxy-error misclassification that caused genuine SAM-proxy
  failures to be reported as ordinary peer-unreachable errors.
- `getpeerinfo` gained `network`, `addrfrompeer`, and `inbound_onion`
  fields for diagnosing peer connectivity and Tor/I2P status directly, and
  `getnetworkinfo` gained `privatetxrelay`, `privatetxrelayfallback`, and
  `privacypeers` fields.
- Locally-originated transactions relayed over I2P now use a rotating pool
  of short-lived, burn-after-use identities separate from the node's
  permanent I2P identity, so a colluding I2P peer can no longer link
  multiple transactions to the same persistent destination over time. Each
  identity is warmed (tunnels built, peers accumulated) before it's ever
  selected, is retired shortly after relaying one transaction, and carries
  a staggered backstop expiry so it isn't kept alive indefinitely if never
  used. The node's existing permanent I2P identity is untouched and
  continues to serve general connectivity; it is never used to relay a
  locally-originated transaction. See
  `doc/privacy-network-layer-whitepaper.md` for the full design and a
  comparison against how other privacy-focused projects address the same
  problem.
- Fixed the per-source-IP inbound connection cap to be scoped per local
  I2P identity rather than shared globally across all of a node's I2P
  identities — a remote peer connecting to both the primary identity and
  a pool identity is expected multi-identity behavior, not abuse, and no
  longer counts against itself.
- `getpeerinfo` and the Qt Peers detail pane gained an `i2p_identity`
  field (`primary` or `pool-N`) showing which local I2P identity a
  connection belongs to.
- Fixed the I2P relay pool's outbound dialing being starved on
  well-connected nodes: it previously shared the regular outbound
  connection semaphore with ordinary peer discovery, which polls far
  more aggressively and could win every freed connection slot first,
  even when plenty of overall outbound capacity was available. The
  pool now has its own dedicated reservation, sized for two
  connections per configured identity rather than one, kept additive
  alongside the regular outbound budget so it can't come at the cost
  of general peer connectivity.
- I2P relay-pool outbound dialing no longer excludes a candidate
  address just because a different local identity (the primary
  identity, or another pool slot) is already connected to it — a
  remote peer connecting to several of a node's local identities is
  the intended multi-identity behavior, and the old global check
  needlessly compounded I2P address scarcity. Since the same address
  can now legitimately be selected by more than one pool identity in
  a given cycle, a randomized 1-6 second delay was added before each
  dial attempt so they don't all connect to that peer back-to-back.
- Address-manager entries that are effectively unusable (never seen
  within a 30-day horizon, a zero or bogus timestamp, or repeated
  connection failures with no success) are no longer selected as
  outbound dial candidates at anywhere near the same rate as good
  ones, and are now actively swept out of the address table on an
  hourly schedule instead of only being evicted lazily on a bucket
  collision that might never happen.
- Orphaned `peers.dat.XXXX` temporary files (left behind if the
  process is killed or crashes mid-write to the address database) are
  now cleaned up automatically on startup.
- Default DNS seeds and fallback `-addnode` peers were updated to the
  new `dnsseed1-2.cryptoforge.cc` pirate-seeder instances, replacing
  hostnames that predated the pirate-seeder rewrite; the fallback
  addnode list was also pruned to hosts confirmed reachable.

Test suite consolidation
--------------------------

The three legacy test runners have been merged into a single `pirate-gtest`
binary: the Bitcoin Core Boost.Test suite and the ktest suite were both
folded into gtest, retiring `pirate-test` and `pirate-ktest` respectively.
The consolidated suite was hardened with bug fixes, closed Sapling/Ironwood
coverage gaps, and gained new coverage for shielded duplicate-nullifier
rejection and real Sapling/Ironwood proof verification across the mempool
and `ConnectBlock` paths. A link failure in the consolidated suite (an
out-of-line template instantiation dropped by inlining, breaking only
the test binary) was also fixed.

Wallet UI improvements
------------------------

- The transaction view and transaction history were reworked, including
  rescan/sync progress dialogs.
- The Z-address table now displays addresses grouped by spending key, adds
  a Type (Scope) column, and tracks extended IVKs.
- Fixed crashes and out-of-bounds issues in the address table, including
  key export.
- Added BIP39 wordlist language options.
- The new-address dialog was updated, and transaction detail popup
  contents are now shown before confirmation.
- Fixed the Overview tab showing a transaction's fee instead of the
  actual amount deducted from the wallet balance for outbound sends,
  and a related bug where the transaction list's minimum-amount filter
  compared against the fee instead of the real transaction size.

RPC and wallet API changes
-----------------------------

- `z_sendmany` no longer supports transparent transactions.
- Sapling change addresses are now supported.
- Consolidation and Sweep now have Orchard/Ironwood variants alongside the
  existing Sapling implementations.
- Added a `-changeaddress` configuration option letting a user route all
  shielded change to a chosen Sapling or Ironwood address instead of the
  auto-derived internal one, regardless of which pool a send actually
  spent from — there is no consensus rule tying a change output's pool
  to the spend's pool. Applies to `z_sendmany` and to
  `z_createbuildinstructions`/`z_createbuildinstructionscoincontrol`'s
  offline builder-instruction workflow alike.

Notary requiredSigs and Notaries RPC tools
---------------------------------------------

Notarization `requiredSigs` is now a constant, active from a fixed block
height, rather than a runtime-computed value: `requiredSigs` is 6,
active from height 4141650 (~Sat Sep 19 2026 12:00 PM PDT / 19:00 UTC,
estimated from the chain tip at the 60s/block target spacing). New
`nn_getwalletinfo` and `nn_split` Notaries RPC tools were added, along
with an `nn_makenota` test RPC for exercising notarization end-to-end.

Build and packaging infrastructure
-------------------------------------

- `pirate-qt` and `pirate-cli` builds are now packaged as `.deb` and `.zip`
  release artifacts across Linux, aarch64, Windows, and macOS, with
  binaries stripped in the packaged artifacts while unstripped copies
  remain where they are built.
- Release-artifact versions, `Info.plist`, and `signbinaries.sh` now all
  derive from `configure.ac`'s `_CLIENT_VERSION_*` macros instead of being
  hand-copied in multiple places.
- CI dependency lists were fixed to include `cmake`, required by the i2pd
  and Qt depends packages.
- Fixed a macOS build failure (`pipe2()` doesn't exist on Darwin), a mingw
  cross-build failure (`_WIN32_WINNT` must be defined before any header),
  and a mingw32 i2pd CMake configuration issue pointing at the wrong
  windres.
- Patched i2pd's `Mapping::Contains()` for libc++'s incomplete
  `map::contains()`.
- `fetch-params.sh`/`fetch-params.bat` now pull the Sapling/Sprout
  trusted-setup parameters from a GitHub release instead of
  `bootstrap.arrr.black`.

Dependency updates
---------------------

- Updated `libsodium` to 1.0.22.
- Updated the `cxx` crate and regenerated `native_cxxbridge`.

New configuration flags
---------------------------

The following flags are new relative to the master branch.

**Peer-to-peer networking and privacy:**

- `-privatetxrelay` (default: 1) — relay locally-originated transactions
  only to privacy peers (outbound Tor, or inbound/outbound I2P).
- `-privatetxrelayfallback` (default: 1) — when `-privatetxrelay` is
  enabled and no privacy peer is connected, fall back to relaying via all
  peers instead of not relaying that round. Disable for strict,
  fail-closed behavior.
- `-maxinboundfromip=<n>` (default: 2) — maximum inbound connections
  accepted from a single source IP; previously a hardcoded constant, and
  does not apply to peers connecting via the dedicated Tor listener.
- `-allowlocalip` (default: 0) — allow `-addnode` connections to local
  addresses (127.x.x.x, ::1), and allow inbound connections from local
  addresses other than via a Tor hidden service. For testing only.
- `-i2pidentityrotation` (default: 1) — maintain a pool of short-lived,
  burn-after-use I2P identities for relaying locally-originated
  transactions, separate from the node's normal permanent I2P identity.
- `-i2pidentitypoolmin=<n>` (default: 12) — minimum number of warmed,
  ready I2P relay identities to keep in reserve.
- `-i2pidentitypoolmax=<n>` (default: 16) — maximum number of concurrent
  I2P relay identities.

**Bundled Tor and I2P process management:**

- `-torautostart` (default: 1) — automatically launch and manage a
  bundled Tor daemon so Tor works without a separately running instance.
- `-torpath=<path>` — path to the `tor` binary to use instead of
  auto-detection.
- `-i2pdautostart` (default: 1) — automatically launch and manage a
  bundled I2P (`i2pd`) daemon so I2P works without a separately running
  router.
- `-i2pdpath=<path>` — path to the `i2pd` binary to use instead of
  auto-detection.

**Mempool tuning:**

- `-maxmempool=<n>` (default: 300) — keep the transaction mempool below
  `<n>` megabytes.
- `-mempoolexpiry=<n>` (default: 1) — do not keep transactions in the
  mempool longer than `<n>` hours.
- `-maxorphanperpeer=<n>` (default: 5) — maximum orphan transactions
  accepted from a single peer.

**Sapling/Ironwood consolidation and sweep**, replacing the old
pool-agnostic `-consolidation*`/`-sweep*` options. This is a breaking
change for existing configs: `-consolidation`, `-consolidationtxfee`,
`-consolidateaddress`, `-sweepsaplingaddress`, and `-sweepironwoodaddress`
are no longer accepted at all — the node will refuse to start with an
error naming the replacement option, rather than silently aliasing to it:

- `-saplingconsolidation`, `-saplingconsolidationtxfee` (default: 10000
  satoshis), `-saplingconsolidationinterval=<n>` (default: 10080 blocks,
  ~1 week) — replace `-consolidation`/`-consolidationtxfee`.
- `-ironwoodconsolidation`, `-ironwoodconsolidationtxfee` (default: 10000
  satoshis), `-ironwoodconsolidationinterval=<n>` (default: 10080 blocks,
  ~1 week)
- `-consolidatesaplingaddress=<zaddr>` and
  `-consolidateironwoodaddress=<zaddr>` — per-protocol address to
  consolidate (default: all), replacing `-consolidateaddress`; must match
  the sweep address when sweep is enabled.
- `-sweepaddress=<zaddr>` — sweep target address, supports both Sapling
  and Ironwood addresses, replacing `-sweepsaplingaddress`/
  `-sweepironwoodaddress`.

**Wallet:**

- `-changeaddress=<zaddr>` — route all shielded change to this Sapling or
  Ironwood address instead of the auto-derived internal address,
  regardless of which pool a send spent from. Must be an address this
  wallet holds the spending key for.

**Other:**

- `-usedpowconfs` (default: 1) — use dPoW confirmation count instead of
  raw chain depth when filtering notes.

Upgrade notes
-------------

This release activates the Ironwood shielded pool under a new consensus
upgrade and bumps `MIN_INDEX_VERSION`, forcing a reindex on upgrade. All
node operators should upgrade and allow the reindex to complete before
resuming normal operation.

Ironwood mainnet activation is targeted for Sat Oct 3 2026, 12:00 PM PDT
(19:00 UTC); the notary `requiredSigs` update activates two weeks
earlier, at height 4141650 (~Sat Sep 19 2026 12:00 PM PDT / 19:00 UTC).
Upgrade before both dates.

Changelog
=========

Cryptoforge:
  Set PIRATE dPoW requiredSigs, activation height, and Ironwood date.
  (a923ed6ec)
  Isolate PIRATETST/other asset chains from PIRATE's mainnet peer
  sources. (cbf91de83)
  Add -changeaddress override for z_sendmany and offline builder
  instructions. (de33e1284)
  Replace stale DNS seeds with pirate-seeder hosts, prune dead
  addnodes. (f9fe33fb7)
  Update Cargo.toml comments to reflect librustzcash/orchard main.
  (812506a0c)
  Fix deadlock between cs_vNodes and cs_i2p_relay_pool. (4ea6c6676)
  Fix stack buffer overflow in Ironwood DeriveFVK's degenerate-IVK
  guard. (dd81bd4c1)
  Fix undefined reference to SerializeForEncryptionInput<CHDChain> in
  pirate-gtest. (467fbd2fa)
  Auto-restart embedded i2pd/tor if either daemon dies mid-run.
  (3581dc678)
  Fix deprecated boost/bind.hpp warning in tor_process/i2pd_process.
  (be1d5f98b)
  Expose actual Tor control port via getnetworkinfo. (ae525e189)
  Fix use-after-free crash in I2P relay-pool identity rotation.
  (332daa36d)
  Move P2P bind to run before script-check threads, scheduler, and RPC
  server. (049466bb2)
  Fix Overview tab showing fee instead of net amount for outbound
  transactions. (fc0c53289)
  Fix I2P/Tor startup stalls, dialing hangs, watchdog reaping, and a
  shutdown crash. (9fbef780e)
  Fix NULL-deref crash in init_string() on allocation failure.
  (492dabe7e)
  Stop selecting and stop keeping terrible addrman entries.
  (01a7ac72e)
  Fix I2P relay-pool outbound dialing being starved. (61875fafa)
  Fix NULL-deref crash on realloc failure in accumulatebytes.
  (ef6df9c4a)
  Add rotating burn-after-use I2P identities for local tx relay.
  (b80ebffb3)
  Default -usedpowconfs to true. (a65769b05)
  Restrict locally-originated transaction relay to privacy peers.
  (c89cea655)
  Fix inbound Tor connection handling and add related diagnostics.
  (7c640f14f)
  Disconnect legacy (non-ADDRv2) peers once Ironwood activates.
  (b1664e9c5)
  Add outbound network diversity, fix ADDR handler address-storage bug.
  (ca6d04515)
  Remove ineffective addnode-priority throttle in ThreadOpenConnections.
  (cd0363a75)
  Fix addrman: inverted reachability weighting, legacy GetAddr leak.
  (4127f0873)
  Enforce strict same-network reachability, fix I2P error classification.
  (1ac884bc1)
  Fix signbinaries.sh: sign from artifacts/release, not artifacts.
  (181b15117)
  Fix notaries.cpp build: CTxDestination is std::variant, not
  boost::variant. (3ac1addc0)
  Fix CI: missing cmake, qt-macos breakage, switch qt-* jobs to zcutil
  packaging. (6571b106f)
  Derive release-artifact versions from configure.ac instead of
  hand-copying. (0edf628d8)
  Align Windows/macOS packaging with Linux/ARM, rename build-qt-arm.sh.
  (181735e71)
  Package pirate-qt/pirate-cli builds as .deb and .tar.gz artifacts.
  (472e656eb)
  Add gtest coverage for shielded duplicate-nullifier rejection and real
  proof verification. (43ad4142e)
  Ignore more generated build artifacts. (7e780fa92)
  Remove generated tests_config.ini from tracking. (2a579230f)
  Fix mingw32 build: point i2pd's CMake config at the cross windres.
  (79eb3d183)
  Fix macOS build: pipe2() doesn't exist on Darwin. (c293ad1fb)
  Patch i2pd's Mapping::Contains() for libc++'s incomplete
  map::contains(). (cca08ad9c)
  Fix mingw build: define _WIN32_WINNT before any header, not just
  before windows.h. (de01948d6)
  Remove PIRATETST checkpoint data from chainparams.cpp. (ea6290b35)
  Show transaction detail popup contents before confirmation. (09c03f01b)
  Fix Sapling/Ironwood shielded address-book parity gaps. (44f287ea2)
  Reset nChainIronwoodValue alongside nChainSaplingValue in
  LoadBlockIndexDB fallback. (fb89e1ce7)
  Add pirate-networking watchdog; rename bundled tor/i2pd to avoid
  collisions. (530876cda)
  Harden pirate-gtest: fix bugs, close Sapling/Ironwood coverage gaps,
  split consolidation RPCs. (b5fbc1b9d)
  Merge the Bitcoin Core Boost.Test suite into gtest, retiring
  pirate-test. (4b8fc6a04)
  Merge the ktest suite into gtest, retiring pirate-ktest. (e298b921f)
  Re-enable and fix the pirate-gtest suite (disabled since Aug 2025).
  (581d34e1c)
  net: fix header-sync stalls from mapBlocksInFlight-based throttling.
  (ca5d9d0bf)
  Update orchard/librustzcash/zcash_note_encryption pins to rebased
  forks. (ae396f16c)
  clientversion: bump MIN_INDEX_VERSION to force reindex for Ironwood
  upgrade. (3a08d79ea)
  Cargo.toml: pull orchard/librustzcash patches from their pushed
  branches. (9a92fdb4c)
  rust: decrypt Ironwood notes with IronwoodDomain, not OrchardDomain.
  (e5dfbde4f)
  wallet: rename abbreviated Orchard persistence tags and stray
  identifiers. (708c504bb)
  rename remaining Orchard identifiers, types, and RPC surface to
  Ironwood. (eb3a4bfdd)
  consolidate Orchard+Ironwood into a single collapsed Ironwood pool.
  (7e5db88fd)
  rust: add ironwood_validator (BatchValidator) and IRONWOOD_PK/VK.
  (5494a882f)
  primitives: add v6 (Ironwood) transaction format scaffolding.
  (4df95aa09)
  primitives: add IronwoodBundle C++ wrapper. (46215e56e)
  rust: add ironwood_protocol scaffolding (IronwoodBundle FFI).
  (2e52f7616)
  consensus: add UPGRADE_IRONWOOD skeleton. (ae667e118)
  rust: fix Orchard bundle parsing to select the correct BundleVersion.
  (3c54c9ce9)
  txdb: read pre-Ironwood history nodes at their actual on-disk length.
  (a002212ba)
  rust: update Orchard/Sapling FFI for upstream API changes. (ce3c6d251)
  consensus: enforce ZIP 209 turnstile checks for Sapling/Orchard pools.
  (67ae8fae8)
  Add .graphifyignore to exclude build artifacts from graph scans.
  (8fa322e05)
  net: embed Tor and I2P as auto-managed subprocesses. (014c3ce5b)
  net/nspv: bound nSPV address length to coinaddr buffer size.
  (030a54981)
  wallet: encrypt HD chain, destdata, and verify note-commitment trees
  at rest. (2d1ba3d8f)
  wallet: erase Sprout zkeymeta records on load. (3abb09e5c)
  wallet: harden master-key KDF with scrypt and auto-upgrade legacy
  wallets. (773089c48)
  consensus: pass tx by const ref in ContextualCheckTransaction.
  (e5dccfb33)
  consensus: cache Sapling anchor lookups per transaction. (b4a5ae75b)
  rpc: harden listtransactions and getblockhashes against bad params.
  (cadf45b17)
  init: warn when -rest is combined with -rpcallowip. (8fcfee14e)
  mempool: remove dead Sprout JoinSplit fee-bypass branch. (55bc746dc)
  net: cap mapRelay size to bound relay-cache memory. (1138e982d)
  komodo: bounds-check untrusted opreturn/coinbase parsing. (1288b97d2)
  bloom: reject oversized filter vData before allocation. (b4ac53d8c)
  net: cap inv/getdata/addr counts before allocation. (d0b248d09)
  net: penalize peers sending malformed messages. (874227e68)
  cc: prevent fulfillment length underflow in GetCryptoCondition.
  (73756364a)
  net/nspv: validate attacker-controlled lengths in nSPV parsers.
  (90649c5ca)
  net: fix addr-relay amplification in SendMessages. (39925a9fa)
  consensus/mempool: enforce Sapling zk-proof uniqueness and fix mempool
  index lifecycle. (3920ca2ca)
  net: reduce MAX_INBOUND_FROMIP from 5 to 2. (cda9d0222)
  net: DoS hardening — alert invalid penalty and relay rate limit.
  (6ab16c167)
  net: DoS hardening — nSPV payload size cap, memcpy bounds checks, rate
  limit. (c3c8cb04f)
  net: reduce MAX_INV_SZ from 50,000 to 5,000. (bf8ea9eec)
  net: DoS hardening — GETBLOCKS/GETHEADERS locator size cap and rate
  limit. (2959751d0)
  net: DoS hardening — orphan pool per-peer slot limit. (ab17f12b3)
  DoS attack prevention, mempool size. Added configurable mempool size
  and time bounds. (c007e8922)
  increment the HDChain version to handle orchard. (397910841)
  delete auto generated rust cxx files on make clean. (2c9f5f9e1)
  update cxx version. (11fb6df94)
  add a PIRATETST checkpoint. (217963099)
  Update native_cxxbridge. (240c31204)
  Version 6.0.0-rc2. (3a9a28465)
  more rust refactor. (637c831e2)
  continue refactoring rust code. (1cc1c71d2)
  reorganize rust code. (8197badc2)
  remove dead code. (83b20bdfe)
  Update to the newest librustzcash and orchard 0.14.0 to patch the
  Orchard doublepend exploit identified upstream in Zcash. (d7b460100)
  Version 6.0.0-rc1. (8ea6ccc57)
  Update transaction table. (2ed7dc609)
  Update libsodium to 1.0.22. (a327da754)
  Auto rescan on transaction serialization error when loading the
  wallet, make sure notes are initialized before executing deletetx on
  them. (623e0f38b)
  Reverted size_t change and removed counters from serialization. The
  builder object is only serialized before committing any inputs or
  outputs. (d26a3aac9)
  use uint64_t instead of size_t for MACOS compatibility. (5372ace8c)
  Version 6.0.0-Beta4. (37aefa066)
  Implement Sapling Change Address. (a1428bb77)
  Update z_sendmany, remove support for transparent transactions.
  (5a4e3315a)
  Apply upstream updates to Orchard variants of Consolidation and Sweep
  functions. (fb6722c50)
  Update GetFilteredNotes logic to apply dual heap selection code to
  Orchard. (3a935fb69)
  Update Bip39 Language options. (d929aba8e)
  Add BIP39 Language options. (a9455b652)
  refactor sapling and orchard keys. (082c49c95)
  consolidate constants and type defs into Zcash.h, add uint256_t and
  uchar256_t to uint256.h, update payment disclosure with new type defs.
  (110287bb0)
  rename pirate_orchard to orchard. (7aaf0e228)
  Send Orchard change to default internal address. (e5a4e53d3)
  Remove old Sprout payment disclosure code. (58edafb7b)
  Implement Sapling and Orchard payment disclosure. Provides users the
  ability to disclose a key that will decrypt a single orchard output or
  action. (a2127db3d)
  Trim whitespace when copying. (e960755d0)
  Fix out of bounds segfault. (b6b11e840)
  Create a payment disclosure key and RPCs for Sapling and Orchard that
  can decrypt a single output or action. (7b449b085)
  Fix key export from address table. (fa52a57f0)
  fix segfault on address table. (3800f1d78)
  Version 6.0.0-Beta3. (354b82115)
  Update the zaddresstablemodel to display addresses grouped by spending
  key. (06bf7c1e3)
  Add Type (Scope) to ZAddressTable. (a81d661da)
  Add support for extra IVK and scope tracking. (78d133f91)
  Fix rescan, update progress dialogs, update transaction table for
  rescan and sync. (8178a81ab)
  Update Transactions history, document code. (4172fb96d)
  revert to BATCH_SIZE = 50. (086f6460d)
  Update new address dialog. (0d116fe77)
  Fix Transaction history. (e9d47d4cd)
  typedef uint256_t for transfer 32 byte object to rust FFI.
  (09bac1df2)
  align nullifier computation logic across sapling and orchard, move to
  the bridge and compute the nullifier from the rust output or action
  instead of passing serialized parts. (102801712)
  Cleanup rust functions that are no longer needed. (fdf212036)
  clean up rustzcash.rs. (c2f3fbb78)
  Migrate sapling decryption to rust. (b8e059e77)
  dont use precompiled headers. (e6ec26e40)
  fix vendored sources directory. (4e0066d6d)
  remove protobuf dependency. (7b7004c37)
  MacOS Compile fixes. (1390c914f)
  Version 6.0.0-Beta2. (db5e62c56)
  Rework Transaction View. (f7fd23bc7)
  update gitignore. (418846614)
  remove unneeded file. (950f6f429)
  update z_getbalances to return unspent note quantities. (d810cf94d)
  define != operator for orchard payment addresses. (dcafa70a4)
  update consolidate address for orchard. (18b7243f2)
  set the chain back to PIRATE and deactivate orchard. (7a235688e)

Øswald Kardingson:
  Use GitHub release for Zcash parameters. (733ad676a)
  Update security.md email. (1fc1c1ec9)
  Point CI at dev branch. (2263697a6)
  Fix bundled Tor builds across CI targets. (6c35fc56a)

DeckerSU:
  Add reference to KomodoOcean in README.md for Qt wallet integration.
  (a4f3ad5b8)
  Enhance nn_makenota to include a recognizable "DECKER" tag in
  desttxid. (0130d28ac)
  Add Decker to copyright and link it in About dialog. (f0772c34d)
  Fix Pirate compatibility in notaries.cpp. (698ba3ac7)
  Add nn_makenota test RPC for notarization testing. (99d711485)
  Add Notaries RPC tools (nn_getwalletinfo, nn_split). (faa837c75)
  PIRATE: make notarization requiredSigs a constant, active from height
  N. (6b18da79e)

Olexandr88:
  Update README.md. (eb3c68e38)

riskrole:
  chore: fix some comments. (9c33682c7)
