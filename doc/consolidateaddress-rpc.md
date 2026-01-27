<!--
Copyright (c) 2025 Pirate Chain Development Team
Distributed under the MIT software license, see the accompanying
file COPYING or http://www.opensource.org/licenses/mit-license.php.
-->

# Shielded Address Consolidation RPC

## Overview

The `consolidateaddress` RPC command provides an efficient way to consolidate multiple small Sapling or Orchard notes at a specific address into fewer, larger notes. This helps reduce wallet fragmentation and improves transaction performance by reducing the number of notes that need to be processed in future transactions.

The command automatically detects whether the provided address is a Sapling (zs) or Orchard (pirate1) address and routes to the appropriate consolidation implementation.

## Command Syntax

```bash
consolidateaddress "address" ( fee ) ( maxnotes ) ( maxtransactions )
```

### Parameters

1. **address** (string, required)
   - The shielded address to consolidate notes for
   - Must be a valid Sapling payment address (starts with `zs`) or Orchard address (starts with `pirate1`)

2. **fee** (numeric, optional, default=0.0001)
   - The fee amount in ARRR to pay per consolidation transaction
   - Must be sufficient for network acceptance (minimum 0.0001 ARRR recommended)

3. **maxnotes** (numeric, optional, default=50)
   - Maximum number of notes to include in a single consolidation transaction
   - Higher values increase transaction size but improve consolidation efficiency
   - Range: 2-200 (practical limit based on transaction size constraints)

4. **maxtransactions** (numeric, optional, default=10)
   - Maximum number of consolidation transactions to create
   - Helps prevent excessive resource usage for addresses with many notes
   - Range: 1-100

### Return Value (Immediate)

```json
{
  "opid": "operation-id-string",
  "unspent_notes_before": 150
}
```

The operation ID can be used with `z_getoperationstatus` and `z_getoperationresult` to monitor progress and retrieve results. The `unspent_notes_before` field shows the total number of unspent notes at the address before consolidation begins.

### Operation Result

Once the consolidation operation completes, the full result can be retrieved using `z_getoperationresult`. The result includes:

```json
{
  "num_tx_created": 3,
  "amount_consolidated": "1.50000000",
  "notes_consolidated": 145,
  "notes_remaining": 5,
  "address": "zs14d8tc0hl9q0vg5l28uec5vk6sk34fkj2n8s7jalvw5fxpy6v39yn4s2ga082lymrkjk0x2nqg37",
  "fee_per_tx": "0.00010000",
  "max_notes_per_tx": 50,
  "max_transactions": 10,
  "consolidation_txids": [
    "txid1...",
    "txid2...",
    "txid3..."
  ]
}
```

**New Fields in v6.0.0+:**
- **notes_consolidated**: Number of notes that were successfully consolidated into the transaction outputs
- **notes_remaining**: Number of unspent notes still remaining at the address after consolidation completes

## Algorithm

The consolidation uses a sophisticated two-step intelligent note selection algorithm:

### Step 1: Fee Coverage Phase
- Selects the smallest notes first
- Continues until the total value exceeds the required fee
- Ensures every consolidation transaction can pay its fee

### Step 2: Optimization Phase
- Adds additional notes up to the `maxnotes` limit
- Continues with smallest-first selection for optimal dust cleanup
- Maximizes consolidation efficiency within transaction constraints

### Key Features

- **Intelligent Selection**: Prioritizes small "dust" notes for cleanup
- **Fee Safety**: Guarantees each transaction can pay its required fee
- **Resource Control**: Respects note and transaction limits to prevent resource exhaustion
- **Security**: Maintains at least one unconsolidated note for immediate spending
- **Wallet Locking**: Captures spending keys during operation creation, allowing execution even if wallet is locked afterward

## Security Considerations

### Wallet Requirements
- Wallet must be unlocked during command invocation to extract spending keys
- Spending keys are held in memory only during operation execution
- Operation can proceed even if wallet is locked after creation

### Note Selection
- Uses minimum 11-block confirmation depth for note selection
- Validates all cryptographic proofs before transaction commitment
- Maintains proper Merkle path validation for spent notes

### Network Safety
- Respects network upgrade boundaries to prevent transaction expiry issues
- Uses 40-block expiry delta for reliable confirmation

## Usage Examples

### Basic Consolidation (Sapling)
```bash
# Consolidate Sapling address with default parameters (0.0001 ARRR fee, 50 max notes, 10 max transactions)
pirate-cli consolidateaddress "zs14d8tc0hl9q0vg5l28uec5vk6sk34fkj2n8s7jalvw5fxpy6v39yn4s2ga082lymrkjk0x2nqg37"
```

### Basic Consolidation (Orchard)
```bash
# Consolidate Orchard address with default parameters
pirate-cli consolidateaddress "pirate1example..."
```

### Custom Fee
```bash
# Consolidate Sapling address with higher fee for faster confirmation
pirate-cli consolidateaddress "zs14d8tc0hl9q0vg5l28uec5vk6sk34fkj2n8s7jalvw5fxpy6v39yn4s2ga082lymrkjk0x2nqg37" 0.0002

# Consolidate Orchard address with custom fee
pirate-cli consolidateaddress "pirate1example..." 0.0002
```

### Optimized Parameters
```bash
# Consolidate with custom parameters for maximum efficiency
pirate-cli consolidateaddress "zs14d8tc0hl9q0vg5l28uec5vk6sk34fkj2n8s7jalvw5fxpy6v39yn4s2ga082lymrkjk0x2nqg37" 0.0001 30 5
```

### Via JSON-RPC
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"method":"consolidateaddress","params":["zs14d8tc0hl9q0vg5l28uec5vk6sk34fkj2n8s7jalvw5fxpy6v39yn4s2ga082lymrkjk0x2nqg37",0.0001,30,5]}' \
  http://localhost:45453/
```

## Monitoring Progress

### Check Operation Status
```bash
# Using single operation ID (string)
pirate-cli z_getoperationstatus "operation-id"

# Using array of operation IDs
pirate-cli z_getoperationstatus '["operation-id"]'
```

### Get Final Results
```bash
pirate-cli z_getoperationresult '["operation-id"]'
```

### Sample Result (Sapling)
```json
{
  "id": "operation-id",
  "status": "success",
  "creation_time": 1640995200,
  "method": "saplingconsolidation_address",
  "result": {
    "num_tx_created": 3,
    "amount_consolidated": "1.50000000",
    "notes_consolidated": 145,
    "notes_remaining": 5,
    "address": "zs14d8tc0hl9q0vg5l28uec5vk6sk34fkj2n8s7jalvw5fxpy6v39yn4s2ga082lymrkjk0x2nqg37",
    "fee_per_tx": "0.00010000",
    "max_notes_per_tx": 50,
    "max_transactions": 10,
    "consolidation_txids": [
      "txid1...",
      "txid2...",
      "txid3..."
    ]
  }
}
```

### Sample Result (Orchard)
```json
{
  "id": "operation-id",
  "status": "success",
  "creation_time": 1640995200,
  "method": "orchardconsolidation_address",
  "result": {
    "num_tx_created": 2,
    "amount_consolidated": "0.75000000",
    "notes_consolidated": 82,
    "notes_remaining": 3,
    "address": "pirate1example...",
    "fee_per_tx": "0.00010000",
    "max_notes_per_tx": 50,
    "max_transactions": 10,
    "consolidation_txids": [
      "txid1...",
      "txid2..."
    ]
  }
}
```

## Error Handling

### Common Errors

- **RPC_INVALID_ADDRESS_OR_KEY**: Invalid shielded address provided or address type not supported
- **RPC_WALLET_UNLOCK_NEEDED**: Wallet must be unlocked to access spending keys
- **RPC_WALLET_ERROR**: Insufficient notes, missing spending key, or other wallet-related issues
- **RPC_INVALID_PARAMETER**: Network upgrade not yet active for the specified address type (Sapling or Orchard)

### Automatic Stopping Conditions

- Fewer than 2 notes remain for consolidation
- Remaining notes insufficient to cover transaction fees
- Maximum transaction limit reached
- Network upgrade activation detected within expiry window

## Performance Considerations

### Optimal Parameters

- **Fee**: Use 0.0001 ARRR for standard confirmation time
- **MaxNotes**: Use 30-50 for balance between efficiency and transaction size
- **MaxTransactions**: Use 5-10 to prevent excessive resource usage

### When to Consolidate

- After receiving many small payments
- Before making large transactions (reduces note selection complexity)
- During periods of low network activity (lower fees, faster confirmation)

## Implementation Files

- **RPC Handler**: `src/wallet/rpcwallet.cpp` - `consolidateaddress()` function with address type detection
- **Sapling Operation Class**: `src/wallet/asyncrpcoperation_saplingconsolidation_address.h/cpp`
- **Orchard Operation Class**: `src/wallet/asyncrpcoperation_orchardconsolidation_address.h/cpp`
- **Client Interface**: `src/rpc/client.cpp` - parameter conversion table
- **Build Configuration**: `src/Makefile.am` - source file declarations

## Related Commands

- `z_listreceivedbyaddress`: List notes available for consolidation
- `z_getoperationstatus`: Monitor consolidation progress
- `z_getoperationresult`: Retrieve consolidation results
- `z_listoperationids`: List all active operations

## Version History

### v5.9.0
- Initial Sapling implementation with intelligent two-step selection algorithm
- Added optional fee parameter with sensible defaults
- Enhanced documentation and error handling

### v6.0.0
- **Added Orchard address support (pirate1 prefix) with automatic type detection**
- **Added `unspent_notes_before` field to immediate RPC response**
- **Added `notes_consolidated` and `notes_remaining` fields to operation result**
- **Enhanced documentation and error handling for both Sapling and Orchard pools**