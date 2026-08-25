Notable changes
===============

Bootstrap downloads: multi-source verification
------------------------------------------------

The blockchain bootstrap download previously fetched a single hardcoded
tarball over plaintext HTTP with no integrity check at all before
extracting it straight over the local datadir. `getBootstrap()` now tries
three independent HTTPS mirrors in random order (spreading load instead of
always hitting the same host first), fetching and checking a published
sha256 against each download before ever extracting it - a source with no
published hash is rejected immediately, before any of its (multi-GB)
tarball is even downloaded, rather than after wasting the download.

Two longstanding bugs in the same code path are also fixed: a headless
`-bootstrap=2` run used to wipe `blocks/`/`chainstate/` and then never
actually redownload them, because the download call was gated behind a
GUI-only check - it now downloads regardless of whether a GUI is present.
And the legacy raw `bootstrap.dat` importer, which nothing has produced
for this chain in a long time, has been removed.

-disablewallet crash on shutdown
-----------------------------------

Stopping a node started with `-disablewallet` via the `stop` RPC crashed
with a null-pointer dereference. `StartShutdown()` locked
`pwalletMain->cs_wallet` to flush the wallet on shutdown, checking only
whether initialization had completed and the node was online - never
whether a wallet actually existed. With `-disablewallet`, `pwalletMain`
stays null for the life of the process, so any shutdown reaching this
path dereferenced it. A plain `SIGTERM` doesn't go through this function,
which is why the crash only ever showed up via `stop`. Fixed, with a
regression test.

Optional -blocknotify/-alertnotify command execution
-------------------------------------------------------

Running an arbitrary system command from `-blocknotify`/`-alertnotify`
now requires an explicit build-time opt-in: every platform build script
(`build.sh` and all its platform-specific variants) accepts a new
`--enable-system-command` flag, without which those options are silently
inert. This only affects anyone building from source themselves - it
does not change behavior for prebuilt binaries, which have never had
this flag enabled.

Upgrade notes
-------------

This is a patch release. No consensus or wallet-format changes; safe to
upgrade in place. Node operators running with `-disablewallet` who saw a
crash on `stop` should upgrade. Anyone relying on `-blocknotify`/
`-alertnotify` actually running their configured command in a
self-built binary needs to add `--enable-system-command` to their build
invocation going forward.

Changelog
=========

Cryptoforge:
  Verify and diversify bootstrap downloads, fix headless bootstrap bug.
  (036dd2dd1)
  Fix null-deref crash in StartShutdown() with -disablewallet.
  (8e1e725db)
  Check bootstrap sha256 before downloading the tarball, not after.
  (2b7f3153e)
  Add --enable-system-command opt-in build flag across all build
  scripts. (e027b0424)
  Bump version to 6.0.3.50 (patch).
