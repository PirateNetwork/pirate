<!--
Copyright (c) 2025 Pirate Chain Development Team
Distributed under the MIT software license, see the accompanying
file COPYING or http://www.opensource.org/licenses/mit-license.php.
-->

# Sapling Address Consolidation RPC

## Overview

The `consolidateaddress` RPC command provides an efficient way to consolidate multiple small Sapling notes at a specific address into fewer, larger notes. This helps reduce wallet fragmentation and improves transaction performance by reducing the number of notes that need to be processed in future transactions.

## Command Syntax

```bash
consolidateaddress "saplingaddress" ( fee ) ( maxnotes ) ( maxtransactions )
```

### Parameters

1. **saplingaddress** (string, required)
   - The Sapling address to consolidate notes for
   - Must be a valid Sapling payment address (starts with `zs`)

2. **fee** (numeric, optional, default=0.0001)
   - The fee amount in ARRR to pay per consolidation transaction
   - Must be sufficient for network acceptance (minimum 0.0001 ARRR recommended)

3. **maxnotes** (numeric, optional, default=50)
   - Maximum number of notes to include in a single consolidation transaction
   - Higher values increase transaction size but improve consolidation efficiency
   - Range: 1-100 (enforced by the RPC)

4. **maxtransactions** (numeric, optional, default=10)
   - Maximum number of consolidation transactions to create
   - Helps prevent excessive resource usage for addresses with many notes
   - Range: 1-50 (enforced by the RPC)

### Return Value

```json
{
  "opid": "operation-id-string"
}
```

The operation ID can be used with `z_getoperationstatus` and `z_getoperationresult` to monitor progress and retrieve results.

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

### Basic Consolidation
```bash
# Consolidate with default parameters (0.0001 ARRR fee, 50 max notes, 10 max transactions)
pirate-cli consolidateaddress "zs14d8tc0hl9q0vg5l28uec5vk6sk34fkj2n8s7jalvw5fxpy6v39yn4s2ga082lymrkjk0x2nqg37"
```

### Custom Fee
```bash
# Consolidate with higher fee for faster confirmation
pirate-cli consolidateaddress "zs14d8tc0hl9q0vg5l28uec5vk6sk34fkj2n8s7jalvw5fxpy6v39yn4s2ga082lymrkjk0x2nqg37" 0.0002
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
pirate-cli z_getoperationstatus '["operation-id"]'
```

### Get Final Results
```bash
pirate-cli z_getoperationresult '["operation-id"]'
```

### Sample Result
```json
{
  "id": "operation-id",
  "status": "success",
  "creation_time": 1640995200,
  "method": "saplingconsolidation_address",
  "result": {
    "num_tx_created": 3,
    "amount_consolidated": "1.50000000",
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

## Error Handling

### Common Errors

- **RPC_INVALID_ADDRESS_OR_KEY**: Invalid Sapling address provided
- **RPC_WALLET_UNLOCK_NEEDED**: Wallet must be unlocked to access spending keys
- **RPC_WALLET_ERROR**: Insufficient notes or other wallet-related issues

### Automatic Stopping Conditions

- Fewer than 2 notes remain for consolidation
- Remaining notes insufficient to cover transaction fees
- Maximum transaction limit reached
- Network upgrade activation detected within expiry window
- Operation cancelled or node shutdown requested

## Performance Considerations

### Optimal Parameters

- **Fee**: Use 0.0001 ARRR for standard confirmation time
- **MaxNotes**: Use 30-100 for balance between efficiency and transaction size
- **MaxTransactions**: Use 5-20 to prevent excessive resource usage (max 50)

### When to Consolidate

- After receiving many small payments
- Before making large transactions (reduces note selection complexity)
- During periods of low network activity (lower fees, faster confirmation)

## Implementation Files

- **RPC Handler**: `src/wallet/rpcwallet.cpp` - `consolidateaddress()` function
- **Operation Class**: `src/wallet/asyncrpcoperation_saplingconsolidation_address.h/cpp`
- **Client Interface**: `src/rpc/client.cpp` - parameter conversion table

## Related Commands

- `z_listreceivedbyaddress`: List notes available for consolidation
- `z_getoperationstatus`: Monitor consolidation progress
- `z_getoperationresult`: Retrieve consolidation results
- `z_listoperationids`: List all active operations

## Version History

- **v5.9.0**: Initial implementation with intelligent two-step selection algorithm
- **v5.9.0**: Added optional fee parameter with sensible defaults
- **v5.9.0**: Enhanced documentation and error handling