<!--
Copyright (c) 2025 Pirate Chain Development Team
Distributed under the MIT software license, see the accompanying
file COPYING or http://www.opensource.org/licenses/mit-license.php.
-->

# Automatic Sapling Note Consolidation

## Overview

Automatic Sapling note consolidation is a background wallet service that periodically merges
multiple small notes at each Sapling address into fewer, larger notes. This reduces wallet
fragmentation, lowers future transaction sizes, and improves overall wallet performance.

The service runs at a configurable block interval and processes each eligible address
independently, using random batch sizes to spread load and avoid predictable on-chain patterns.

---

## Wallet Encryption Compatibility

**Automatic consolidation is not compatible with an encrypted (passphrase-locked) wallet.**

Consolidation transactions require access to Sapling spending keys to build cryptographic
proofs. When a wallet is encrypted and locked, spending keys are not accessible and
consolidation runs will fail silently — the threshold probe will succeed but the spend
build step will be unable to extract keys.

**If your wallet is encrypted, do not use auto-consolidation.** Use the `consolidateaddress`
RPC instead. That command captures spending keys at invocation time while the wallet is
temporarily unlocked, then executes the async operation independently — the wallet can be
re-locked immediately after issuing the command without interrupting the operation.

```bash
# Unlock wallet just long enough to issue the command (60 seconds is sufficient)
pirate-cli walletpassphrase "your passphrase" 60
pirate-cli consolidateaddress "zs1abc..." 0.0001 50 10
pirate-cli walletlock
# The consolidation operation continues running in the background
pirate-cli z_getoperationstatus '["opid-..."]'
```

See [consolidateaddress-rpc.md](consolidateaddress-rpc.md) for full details on the manual
consolidation command.

> **Note:** Auto-consolidation does **not** benefit from the same key-capture behaviour
> because it re-reads spending keys fresh on every scheduled run. A locked wallet will
> cause each run to fail silently.

---

## Configuration (PIRATE.conf / Command Line)

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `-consolidation` | bool | `false` | Enable automatic consolidation at startup |
| `-consolidationtxfee` | satoshis | `10000` | Fee per consolidation transaction |
| `-consolidationtargetqty` | integer | `100` | Min notes per address before consolidation triggers |
| `-consolidationinterval` | blocks | ~1 week | Blocks between consolidation runs |
| `-consolidatesaplingaddress=<zaddr>` | string | (all) | Restrict consolidation to this address (repeatable) |

### Examples

```ini
# PIRATE.conf
consolidation=1
consolidationtxfee=10000
consolidationtargetqty=50
consolidationinterval=1008
consolidatesaplingaddress=zs1abc...
consolidatesaplingaddress=zs1def...
```

### Notes

- `-consolidatesaplingaddress` may be specified multiple times to build an address filter list.
  All listed addresses must have their spending key in the wallet.
- When `-consolidation=0` (default), none of the other options have any effect at startup.
- Use the RPC commands below to change settings in a running node without restarting.

---

## RPC Commands

### `enableconsolidation true|false`

Enable or disable automatic consolidation in a running node.

```bash
pirate-cli enableconsolidation true
pirate-cli enableconsolidation false
```

**Result:**
```json
{ "consolidationEnabled": true }
```

---

### `consolidationstatus`

Return the current state of the automatic consolidation service.

```bash
pirate-cli consolidationstatus
```

**Result:**
```json
{
  "consolidationEnabled": true,
  "isRunning": false,
  "nextConsolidation": 1042800,
  "consolidationInterval": 10080,
  "targetQty": 100,
  "consolidationTxFee": 10000,
  "addressFilterEnabled": true,
  "consolidationAddresses": [
    "zs1abc...",
    "zs1def..."
  ]
}
```

| Field | Description |
|-------|-------------|
| `consolidationEnabled` | Whether auto-consolidation is active |
| `isRunning` | Whether a consolidation operation is currently executing |
| `nextConsolidation` | Block height at which the next run will be triggered |
| `consolidationInterval` | Configured blocks between runs |
| `targetQty` | Minimum note count required per address to trigger consolidation |
| `consolidationTxFee` | Fee in satoshis used per transaction |
| `addressFilterEnabled` | Whether consolidation is restricted to an explicit address list |
| `consolidationAddresses` | The explicit address list (empty when filter is off) |

---

### `setconsolidationtarget <qty>`

Set the minimum number of notes an address must have before auto-consolidation will process it.

```bash
pirate-cli setconsolidationtarget 50
```

- Minimum value: `2`
- Changes take effect on the next consolidation run.

**Result:**
```json
{ "targetQty": 50 }
```

---

### `setconsolidationfee <satoshis>`

Set the fee used per automatic consolidation transaction.

```bash
pirate-cli setconsolidationfee 10000
pirate-cli setconsolidationfee 0
```

- Minimum value: `0` (zero fee allowed)
- Value is in satoshis (1 ARRR = 100,000,000 satoshis).

**Result:**
```json
{ "consolidationTxFee": 10000 }
```

---

### `setconsolidationinterval <blocks>`

Set the number of blocks between automatic consolidation runs.

```bash
pirate-cli setconsolidationinterval 1008
```

- Minimum value: `1`
- Common values: `144` (~1 day), `1008` (~1 week), `4320` (~1 month)

**Result:**
```json
{ "consolidationInterval": 1008 }
```

---

### `consolidationaddresses <action> ("zaddr")`

Manage the explicit list of Sapling addresses that auto-consolidation will process.

When the list is **non-empty** only those addresses are consolidated.
When the list is **empty** (cleared) all wallet Sapling addresses are eligible.

All addresses must be Sapling addresses whose spending key is held by this wallet.

#### `list`

```bash
pirate-cli consolidationaddresses list
```

**Result:**
```json
{
  "filterEnabled": true,
  "addresses": [
    "zs1abc...",
    "zs1def..."
  ]
}
```

#### `add "zaddr"`

```bash
pirate-cli consolidationaddresses add "zs1abc..."
```

- Validates the address is Sapling and the wallet holds its spending key.
- Returns an error if the address is already in the list.

**Result:**
```json
{ "added": "zs1abc...", "totalAddresses": 2 }
```

#### `remove "zaddr"`

```bash
pirate-cli consolidationaddresses remove "zs1abc..."
```

- Returns an error if the address is not in the list.
- If the list becomes empty after removal, the filter is automatically disabled.

**Result:**
```json
{ "removed": "zs1abc...", "totalAddresses": 1, "filterEnabled": true }
```

#### `clear`

```bash
pirate-cli consolidationaddresses clear
```

- Clears all addresses and disables the filter (all wallet addresses become eligible).

**Result:**
```json
{ "cleared": true, "filterEnabled": false }
```

---

## Address Selection Logic

Each consolidation run selects candidate addresses using the following priority order:

1. **Sweep is active** — Only the configured sweep destination address is consolidated.
   The consolidation address list is ignored. This prepares funds for the sweep service.
2. **Consolidation address filter is active** — Only addresses in the explicit list are processed.
3. **No filter** — All wallet Sapling addresses are candidates.

In all cases, an address is skipped if:
- The wallet does not hold its spending key (watch-only).
- The address has fewer notes than `targetQty` (threshold probe).

---

## How Auto-Consolidation Works

1. At every block, the wallet checks whether `blockHeight >= nextConsolidation`.
2. If consolidation is due, a background async operation is launched.
3. For each candidate address:
   - A fast threshold probe fetches at most `targetQty` notes. If fewer are found the address is skipped.
   - Random batch bounds are chosen: `maxQuantity` in [10, 44], `minQuantity` in [2, 10].
   - An inner loop repeatedly fetches up to `maxQuantity` notes and builds a consolidation transaction, until fewer than `minQuantity` notes remain or the value no longer covers the fee.
4. After all addresses are processed `nextConsolidation` is advanced by `consolidationInterval` blocks.

The randomised batch sizes spread consolidation work across multiple runs and avoid predictable transaction patterns.

---

## Interaction with Sweep

When automatic sweep (`-sweep` / `enablesweep`) is active:

- Consolidation is **restricted to the sweep address only**.
- Any configured consolidation address list is **ignored**.
- This ensures funds are pre-consolidated at the sweep destination before the sweep service moves them.
- A warning is logged at startup if a conflicting consolidation address list is configured.

---

## Stopping Conditions

A consolidation run stops early if any of the following occur:

- The operation is cancelled (e.g. via `z_getoperationstatus` cancel).
- Node shutdown is requested.
- The Merkle path for a note cannot be retrieved.
- A transaction fails to commit to the wallet.

---

## Related Commands

- `consolidateaddress` — Manually consolidate a specific address (see [consolidateaddress-rpc.md](consolidateaddress-rpc.md))
- `sweepstatus` / `enablesweep` — Sweep service status and control
- `z_getoperationstatus` — Monitor background operation progress
- `z_listunspent` — Inspect individual notes before/after consolidation
