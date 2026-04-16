<!--
Copyright (c) 2025 Pirate Chain Development Team
Distributed under the MIT software license, see the accompanying
file COPYING or http://www.opensource.org/licenses/mit-license.php.
-->

# Automatic Sapling Note Sweep

## Overview

Automatic Sapling note sweep is a background wallet service that periodically moves all
funds from every other wallet address into a single configured destination address. This
is useful for consolidating funds from many addresses into one for security, privacy, or
operational simplicity.

Unlike auto-consolidation (which merges notes *within* each address), sweep moves funds
*across* addresses — draining each source address completely into the sweep destination.

The service runs at a configurable block interval and processes each eligible wallet address
independently.

---

## Wallet Encryption Compatibility

**Automatic sweep is not compatible with an encrypted (passphrase-locked) wallet.**

Sweep transactions require access to Sapling spending keys for each source address. When
the wallet is locked, spending keys are inaccessible and sweep runs will fail silently.

**If your wallet is encrypted, do not use auto-sweep.** Use
`z_sendmany` manually while the wallet is temporarily unlocked instead.

---

## Configuration (PIRATE.conf / Command Line)

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `-sweep` | bool | `false` | Enable automatic sweep at startup |
| `-sweepsaplingaddress=<zaddr>` | string | (required) | Destination address to sweep all funds to |
| `-sweeptxfee` | satoshis | `10000` | Fee per sweep transaction |
| `-sweepinterval` | blocks | ~15 minutes | Blocks between sweep runs |

### Examples

```ini
# PIRATE.conf
sweep=1
sweepsaplingaddress=zs1abc...
sweeptxfee=10000
sweepinterval=144
```

### Notes

- `-sweepsaplingaddress` is **required** when `-sweep=1`. Exactly one address must be specified.
- The sweep address must be a Sapling address and the wallet must hold its spending key.
- The sweep address is **excluded** from being swept — only other wallet addresses are drained.
- Use `setsweepaddress` at runtime to change the destination without restarting.
- Use the RPC commands below to change all other settings in a running node.

---

## RPC Commands

### `enablesweep true|false`

Enable or disable automatic sweep in a running node.

```bash
pirate-cli enablesweep true
pirate-cli enablesweep false
```

- Enabling sweep requires a sweep address to have been configured (via `setsweepaddress`
  or `-sweepsaplingaddress`). Returns an error if none is set.

**Result:**
```json
{ "sweepEnabled": true }
```

---

### `sweepstatus`

Return the current state of the automatic sweep service.

```bash
pirate-cli sweepstatus
```

**Result:**
```json
{
  "sweepEnabled": true,
  "isRunning": false,
  "nextSweep": 1042500,
  "sweepInterval": 15,
  "sweepTxFee": 10000,
  "sweepAddress": "zs1abc..."
}
```

| Field | Description |
|-------|-------------|
| `sweepEnabled` | Whether auto-sweep is active |
| `isRunning` | Whether a sweep operation is currently executing |
| `nextSweep` | Block height at which the next run will be triggered |
| `sweepInterval` | Configured blocks between runs |
| `sweepTxFee` | Fee in satoshis used per transaction |
| `sweepAddress` | Active sweep destination address, or `(not set)` |

---

### `setsweepaddress "zaddr"`

Set the Sapling destination address that all funds will be swept to.

```bash
pirate-cli setsweepaddress "zs1abc..."
```

- The address must be a Sapling address (`zs1...`).
- The wallet must hold the spending key for this address.
- Only one sweep address may be active at a time — calling this again replaces the previous address.
- The new address takes effect on the next sweep run.

**Result:**
```json
{ "sweepAddress": "zs1abc..." }
```

---

### `setsweepfee <satoshis>`

Set the fee used per automatic sweep transaction.

```bash
pirate-cli setsweepfee 10000
pirate-cli setsweepfee 0
```

- Minimum value: `0` (zero fee allowed)
- Value is in satoshis (1 ARRR = 100,000,000 satoshis).

**Result:**
```json
{ "sweepTxFee": 10000 }
```

---

### `setsweepinterval <blocks>`

Set the number of blocks between automatic sweep runs.

```bash
pirate-cli setsweepinterval 144
```

- Minimum value: `1`
- Common values: `15` (~15 minutes), `144` (~1 day), `1008` (~1 week)

**Result:**
```json
{ "sweepInterval": 144 }
```

---

## How Auto-Sweep Works

1. At every block, the wallet checks whether `blockHeight >= nextSweep`.
2. A sweep run is skipped if:
   - Auto-consolidation is enabled and its next run is within 15 blocks (consolidation runs first).
   - A consolidation operation is currently in progress.
3. If the sweep is due, a background async operation is launched.
4. For each candidate address (all wallet Sapling addresses **except** the sweep destination):
   - Addresses without a spending key (watch-only) are skipped.
   - Up to 50 confirmed notes (minimum 11-block depth) are fetched per batch.
   - A transaction is built spending those notes to the sweep address, minus the fee.
   - The inner loop repeats until no more eligible notes remain at that address.
5. After all addresses are processed, `nextSweep` is advanced by `sweepInterval` blocks.

---

## Address Selection

The sweep destination address is resolved using the following priority:

1. **RPC-set address** (`setsweepaddress`) — takes priority over everything.
2. **Config address** (`-sweepsaplingaddress`) — used if no RPC address is set.
3. If neither is configured the run is skipped.

All other wallet Sapling addresses that have at least one confirmed note above the fee
threshold are eligible source addresses.

---

## Interaction with Consolidation

When sweep is active, auto-consolidation is **automatically restricted to the sweep
destination address only**. This pre-consolidates notes at the destination before the
sweep service moves them, improving sweep efficiency.

Any consolidation address list configured via `consolidationaddresses` or
`-consolidatesaplingaddress` is ignored while sweep is enabled. A warning is logged at
startup if a conflicting list is present.

Sweep will not run while a consolidation operation is in progress, and will also yield
to an imminent consolidation run (within 15 blocks).

---

## Stopping Conditions

A sweep run stops early if any of the following occur:

- No notes remain above the fee threshold at a source address.
- The operation is cancelled.
- Node shutdown is requested.
- The Merkle path for a note cannot be retrieved.
- A transaction fails to commit to the wallet.

---

## Related Commands

- `consolidationstatus` / `enableconsolidation` — Consolidation service status and control
- `consolidationaddresses` — Manage the consolidation address filter list
- `z_getoperationstatus` — Monitor background sweep operation progress
- `z_listunspent` — Inspect notes before/after a sweep run
- See [consolidation-auto-rpc.md](consolidation-auto-rpc.md) for auto-consolidation documentation
