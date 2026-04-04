Notable changes
===============

Auto-Consolidation Improvements
---------------------------------

The automatic Sapling note consolidation service has been significantly enhanced
with new runtime controls, replacing parameters that previously required a wallet
restart.

### New RPC Commands

- **`enableconsolidation "1"|"0"`** — Enable or disable the auto-consolidation
  background service at runtime. A fixed bug previously caused the enable/disable
  flag to always evaluate as enabled regardless of the argument passed.

- **`consolidationstatus`** — Returns the current consolidation configuration and
  runtime state, including whether consolidation is active, the next scheduled
  run block, interval, target note quantity, transaction fee, and the configured
  address filter.

- **`setconsolidationtarget <n>`** — Set the per-address note count threshold
  that triggers consolidation (minimum 2). Previously only configurable via
  `-consolidationtargetqty` at startup.

- **`setconsolidationfee <amount>`** — Set the transaction fee used for
  consolidation transactions. A fee of `0` is permitted for use with fee-free
  configurations.

- **`setconsolidationinterval <n>`** — Set the number of blocks between
  consolidation runs (minimum 1). Previously only configurable via
  `-consolidationinterval` at startup.

- **`consolidationaddresses "list"|"add"|"remove"|"clear" ("address")`** —
  Manage the runtime address filter. When the filter is active, consolidation
  only processes addresses on the list. Supports `list`, `add`, `remove`, and
  `clear` actions. The filter list is backed by the `-consolidatesaplingaddress`
  config key and is persisted across calls within a session.

### New Config Options

- **`-consolidationtargetqty=<n>`** — Set at startup the per-address note count
  threshold (minimum 2, default 100).

- **`-consolidationinterval=<n>`** — Set at startup the block interval between
  consolidation runs (minimum 1).

Auto-Sweep Improvements
------------------------

The automatic sweep-to-address service now has a full set of runtime controls
mirroring those available for auto-consolidation.

### New RPC Commands

- **`enablesweep "1"|"0"`** — Enable or disable the auto-sweep background
  service at runtime. Enabling sweep requires a sweep destination address to be
  configured (via `setsweepaddress` or `-sweepsaplingaddress` config); attempting
  to enable without one returns an error.

- **`sweepstatus`** — Returns the current sweep configuration and runtime state,
  including whether sweep is active, the next scheduled run block, interval,
  transaction fee, and the configured destination address.

- **`setsweepaddress "saplingaddress"`** — Set the sweep destination address at
  runtime. The address must be a valid Sapling payment address and the wallet
  must hold the corresponding spending key. This RPC-set address takes priority
  over any `-sweepsaplingaddress` config value in both automatic and
  RPC-triggered sweeps.

- **`setsweepfee <amount>`** — Set the transaction fee for sweep transactions. A
  fee of `0` is permitted.

- **`setsweepinterval <n>`** — Set the number of blocks between sweep runs
  (minimum 1).

### New Config Option

- **`-sweepinterval=<n>`** — Set at startup the block interval between sweep
  runs (minimum 1).

Sweep-Aware Consolidation Address Selection
-------------------------------------------

When auto-sweep is enabled, the automatic consolidation service now restricts its
candidate address set to the configured sweep destination only. This prevents
consolidation from fragmenting notes at other addresses while sweep is actively
moving funds. Priority order for candidate selection:

1. If auto-sweep is active → sweep address only.
2. Else if the consolidation address filter (`consolidationaddresses`) is active
   → filter list only.
3. Otherwise → all wallet Sapling addresses.

`GetFilteredNotes` Performance Improvement
-------------------------------------------

`GetFilteredNotes` (used across multiple RPC commands and background services)
now supports a bounded streaming dual-heap selection algorithm, significantly
reducing the time the wallet lock is held when callers only need a limited number
of notes.

The algorithm maintains two disjoint heaps — a max-heap of the N/2 smallest
notes and a min-heap of the N/2 largest notes — and exits early as soon as both
heaps are full and the aggregate value of the working set meets the caller's
minimum value requirement. This avoids scanning the entire wallet when the
requirement can be satisfied partway through.

New optional parameters were added to `GetFilteredNotes`:
- **`maxNotes`** — Maximum number of notes to collect. When `0` (default), the
  original unbounded path is used, preserving backward-compatible behaviour for
  all existing callers.
- **`minAggregateValue`** — Early-exit target: once both heaps are full and the
  combined value of selected notes reaches this threshold, scanning stops. Pass
  `0` to disable early exit (filling both heaps from the full wallet scan before
  returning).

Shutdown Safety in Background Operations
-----------------------------------------

All background operation loops in the consolidation and sweep services now check
`ShutdownRequested()` in addition to the existing `isCancelled()` check. This
ensures that in-progress consolidation or sweep operations terminate promptly
when the node is stopping, preventing unnecessary delays during shutdown.

The check is applied at:
- The top of the outer per-address loop.
- The top of the inner transaction-building loop.
- After each transaction is built.

This affects `asyncrpcoperation_saplingconsolidation`,
`asyncrpcoperation_saplingconsolidation_address`, and
`asyncrpcoperation_sweeptoaddress`.

Init-Time Validation for Consolidation Addresses
--------------------------------------------------

At startup, each address supplied via `-consolidatesaplingaddress` is now
validated both for address correctness and for the presence of a corresponding
spending key in the wallet. Addresses that fail either check are skipped with a
warning log message rather than aborting startup.

If both consolidation addresses and a sweep address are configured and they do
not match, a warning is logged and startup continues. At runtime the
sweep-priority logic (above) resolves the conflict automatically.

Documentation
-------------

Three documentation files have been added or updated under `doc/`:

- **`consolidation-auto-rpc.md`** (new) — Full reference for the auto-
  consolidation system: all RPCs with example calls and return shapes, all
  config options, the address selection algorithm, sweep interaction, stopping
  conditions, and a note on encrypted-wallet compatibility.

- **`sweep-auto-rpc.md`** (new) — Full reference for the auto-sweep system:
  all RPCs with examples, all config options, how sweep is triggered (block
  interval with a 15-block consolidation-yield window), per-address loop and
  50-note batches, stopping conditions, and encrypted-wallet compatibility.

- **`consolidateaddress-rpc.md`** (updated) — Corrected parameter ranges
  (`maxnotes` 1–100, `maxtransactions` 1–50), added node-shutdown as a stopping
  condition, and updated performance recommendations to reflect the actual limits.

Changelog
=========

Cryptoforge:
      Update consolidation and sweep. Update GetFilteredNote to allow for early
      exit when enough notes have been found.
      Update version to 5.9.2
