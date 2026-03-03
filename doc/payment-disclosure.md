# Payment Disclosure

## Summary

Payment disclosure allows the sender of a shielded transaction to reveal the details of a specific output or action they created. The disclosure is a decryption key that allows anyone to decrypt and view a single shielded output/action without compromising any other transaction data. This feature supports both **Sapling** and **Orchard** shielded pools.

Use RPC calls `z_exportsaplingdisclosure`, `z_exportorcharddisclosure`, and `z_verifydisclosure` to generate and verify payment disclosures. A graphical interface is also available in the wallet GUI.

## Who Should Read This Document

- Frequent users of shielded transactions
- Payment processors and exchanges
- Merchants accepting shielded payments
- Anyone needing proof of payment for shielded transactions

---

## Background

Payment disclosure is a decryption key that allows a sender to reveal the details of a specific shielded output they created. By sharing this key, the sender can demonstrate that they transferred funds to a recipient's shielded address. This is particularly useful when:

- A merchant claims they didn't receive payment
- You need to demonstrate payment to a third party (e.g., dispute resolution)
- Regulatory or compliance requirements demand proof of payment
- Auditing or record-keeping purposes

The payment disclosure reveals:
- The transaction ID
- The specific output/action index
- The recipient's address
- The amount sent
- The memo field contents

**Important:** Payment disclosure does NOT compromise the privacy of other outputs in the transaction or any future transactions.

---

## Use Cases

### Example 1: Merchant Payment Verification

Alice sends 100 ARRR to Bob's shielded address for goods. Bob claims he never received the payment. Alice generates a payment disclosure (a decryption key for that specific output) and provides it to Bob, who can decrypt and view the payment details to confirm the payment was made to his address.

### Example 2: Dispute Resolution

If Bob continues to dispute the payment, Alice can provide the payment disclosure to a third party mediator or platform administrator, allowing them to decrypt and view the payment details to confirm the payment was sent.

### Example 3: Tax Records

A business needs to maintain proof of payments made to contractors using shielded addresses for tax reporting purposes.

---

## Technical Details

### Encoding Format

Payment disclosures are encoded using Bech32 format with network-specific prefixes:

#### Mainnet
- **Sapling:** `pirate-sapling-payment-disclosure`
- **Orchard:** `pirate-orchard-payment-disclosure`

#### Testnet
- **Sapling:** `zdisctest`
- **Orchard:** `odisctest`

#### Regtest
- **Sapling:** `zdiscregtest`
- **Orchard:** `odiscregtest`

### Supported Scenarios

The payment disclosure system automatically handles various transaction types:

- **Sapling → Sapling**: Uses sender's Sapling OVK
- **Sapling → Orchard**: Uses sender's Sapling OVK
- **Orchard → Orchard**: Uses sender's Orchard OVK
- **Orchard → Sapling**: Uses sender's Orchard OVK
- **Transparent → Sapling**: Uses derived transparent OVK
- **Transparent → Orchard**: Uses derived transparent OVK

---

## Using Payment Disclosure (GUI)

### Creating a Payment Disclosure

1. **Open the Transactions Tab**
   - Navigate to the "Transactions" tab in your wallet

2. **Locate Your Transaction**
   - Find the transaction for which you want to create a disclosure
   - Right-click on the transaction or double-click to open details

3. **View Transaction Details**
   - In the transaction details dialog, check "Show Payment Disclosure Keys"
   - The dialog will display all outgoing shielded payments in the transaction

4. **Copy the Disclosure Key**
   - Click the "Copy" button next to the disclosure key you want
   - The disclosure key is now in your clipboard
   - Share this key with the recipient or third party

### Verifying a Payment Disclosure

1. **Open Verification Dialog**
   - Go to **Tools → Verify Payment Disclosure** in the menu bar
   - Or use the keyboard shortcut (if configured)

2. **Enter the Disclosure Key**
   - Paste the payment disclosure key into the input box
   - The format will be detected automatically (Sapling or Orchard)

3. **Verify**
   - Click "Verify Disclosure"
   - If valid, you'll see:
     - Disclosure type (Sapling or Orchard)
     - Transaction ID
     - Output/Action index
     - Value sent
     - Recipient address
     - Memo contents (in hex format)

4. **Review Results**
   - Green text indicates successful verification
   - Red text indicates an error (invalid key, transaction not found, etc.)

---

## Using Payment Disclosure (RPC)

### Prerequisites

Your node must have the transaction indexed to create or verify payment disclosures:
```bash
treasurechest-cli gettransaction <txid>
```

If the transaction is not found, you may need to run with `-txindex=1` enabled.

### Creating a Sapling Payment Disclosure

**Command:**
```bash
treasurechest-cli z_exportsaplingdisclosure "txid" output_index
```

**Parameters:**
- `txid` (string, required): The transaction ID
- `output_index` (numeric, required): The Sapling output index (starts from 0)

**Example:**
```bash
treasurechest-cli z_exportsaplingdisclosure "8d3c9f1e2a5b7d4c6e8f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d" 0
```

**Result:**
```
pirate-sapling-payment-disclosure1qqqq...
```

### Creating an Orchard Payment Disclosure

**Command:**
```bash
treasurechest-cli z_exportorcharddisclosure "txid" action_index
```

**Parameters:**
- `txid` (string, required): The transaction ID
- `action_index` (numeric, required): The Orchard action index (starts from 0)

**Example:**
```bash
treasurechest-cli z_exportorcharddisclosure "403fee829dac18435fd575807ead7ef627767f5ebe5074214136b34de8968cdf" 1
```

**Result:**
```
pirate-orchard-payment-disclosure1qqqq...
```

### Verifying a Payment Disclosure (Any Type)

**Command:**
```bash
treasurechest-cli z_verifydisclosure "disclosure_key"
```

**Parameters:**
- `disclosure_key` (string, required): The payment disclosure key (Sapling or Orchard)

The command automatically detects whether the disclosure is for Sapling or Orchard.

**Example (Sapling):**
```bash
treasurechest-cli z_verifydisclosure "pirate-sapling-payment-disclosure1qqqq..."
```

**Result:**
```json
{
  "disclosure_type": "Sapling",
  "txid": "8d3c9f1e2a5b7d4c6e8f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d",
  "output_index": 0,
  "value": 100.00000000,
  "address": "pirate1xyz...",
  "memo": "f60000000000..."
}
```

**Example (Orchard):**
```bash
treasurechest-cli z_verifydisclosure "pirate-orchard-payment-disclosure1qqqq..."
```

**Result:**
```json
{
  "disclosure_type": "Orchard",
  "txid": "403fee829dac18435fd575807ead7ef627767f5ebe5074214136b34de8968cdf",
  "action_index": 1,
  "value": 3285777.99098000,
  "address": "pirate1v2pxvcxp3gq2adjy58m9n4hg0z3p0afn44can393pt69nf8q4djy6dl0g2qy548jq4fgxqc49gx",
  "memo": "f60000000000..."
}
```

**Note:** The key difference is:
- Sapling disclosures use `output_index`
- Orchard disclosures use `action_index`

---

## Identifying Output/Action Indices

When creating a payment disclosure, you need to know which output or action index corresponds to the payment you want to prove.

### Method 1: Using z_listunspent

If the transaction is relatively recent and outputs are still unspent:

```bash
treasurechest-cli z_listunspent 0 999999 false '["your_address"]'
```

Look for your transaction ID in the results to find the output indices.

### Method 2: Using getrawtransaction

```bash
treasurechest-cli getrawtransaction "txid" 1
```

This shows the full transaction structure including all outputs. Count the Sapling or Orchard outputs to determine indices (starting from 0).

### Method 3: Transaction Details in GUI

The transaction details dialog in the GUI shows all outputs when "Show Payment Disclosure Keys" is checked, making it easy to identify which output corresponds to which recipient.

---

## Security Considerations

### What Payment Disclosure Reveals

✅ **Disclosed Information:**
- That YOU sent the payment (proves sender identity)
- The recipient's address
- The amount sent
- The transaction ID and output/action index
- The memo contents

❌ **NOT Disclosed:**
- Other outputs in the same transaction
- Any inputs used in the transaction
- Information about any other transactions
- The recipient's balance or transaction history

### Best Practices

1. **Only share disclosures when necessary**: Payment disclosures reveal sensitive information about a specific payment.

2. **Verify the recipient**: When sharing a disclosure, ensure you're sending it to the correct party.

3. **Store disclosures securely**: If keeping records of payment disclosures, store them securely.

4. **Transaction must be confirmed**: Payment disclosures can only be generated for confirmed transactions.

5. **Backup considerations**: Payment disclosures are generated on-demand from your wallet keys and the blockchain. No special backup is needed beyond your wallet backup.

---

## Troubleshooting

### Error: "Transaction not found"

- Ensure the transaction is confirmed and on the blockchain
- If running a pruned node, you may need `-txindex=1`
- Try `treasurechest-cli gettransaction <txid>` to verify the transaction exists

### Error: "Output index out of range"

- Verify the correct output/action index
- Use `getrawtransaction` to see how many outputs exist
- Remember indices start from 0

### Empty/Blank disclosure key returned

This usually means:
- The wallet doesn't have the necessary keys to generate the disclosure
- The output was not sent by this wallet
- You're trying to generate a disclosure for a received payment (only senders can generate disclosures)

### Error: "Invalid disclosure encoding"

- Ensure you copied the complete disclosure key
- Check for any extra characters or truncation
- Verify you're using the correct network (mainnet vs testnet)

### GUI shows "Failed to generate disclosure"

- The transaction may not be fully confirmed yet
- The output may be a received payment rather than a sent payment
- Your wallet may not have the spending authority for the sending address

---

## FAQ

**Q: Can the recipient generate a payment disclosure?**  
A: No. Only the sender who has the outgoing viewing key (OVK) can generate a payment disclosure.

**Q: Does payment disclosure work for transparent addresses?**  
A: Payment disclosures are only for shielded (Sapling and Orchard) payments. Transparent transactions are already public on the blockchain.

**Q: Can I generate a disclosure for a payment I received?**  
A: No. You can only generate disclosures for payments you sent.

**Q: Will this compromise my privacy?**  
A: Payment disclosures only reveal information about the specific output you disclose. Other outputs in the transaction and all other transactions remain private.

**Q: How long does a disclosure key last?**  
A: Payment disclosure keys are derived from cryptographic information in the transaction and your wallet. They remain valid as long as the transaction exists on the blockchain.

**Q: Can I generate a disclosure for change outputs?**  
A: Yes, but this is rarely useful since change outputs return to your own address.

**Q: What if I lose my wallet?**  
A: If you restore your wallet from seed phrase, you can regenerate payment disclosures for any transactions you sent, as long as the transactions are on the blockchain.

**Q: Do I need any special configuration?**  
A: No special configuration is required. The feature works with standard wallet configuration.

---

## Technical Implementation Notes

### Cryptographic Components

A payment disclosure is essentially a packaged decryption key:
- **Outgoing Cipher Key (OCK)**: Derived from the sender's OVK and transaction data; this is the actual decryption key
- **Bech32 Encoding**: For human-readable and error-detecting encoding
- **Output/Action Decryption**: Uses the OCK to decrypt a single shielded output/action

### Data Flow

1. **Generation**: Wallet uses sender's OVK + transaction data → derives OCK → encodes as Bech32
2. **Verification**: Parse Bech32 → extract OCK + transaction reference → decrypt the specific output/action → display details

The payment disclosure itself is NOT a cryptographic proof. It is a key that allows single output/action decryption, making the encrypted payment details readable.

### Network Support

- ✅ Mainnet
- ✅ Testnet  
- ✅ Regtest

All networks use different Bech32 prefixes to prevent cross-network confusion.

---

## Additional Resources

- **RPC Documentation**: Use `treasurechest-cli help z_exportsaplingdisclosure`
- **GUI Tutorial**: Available in the Help menu
- **Community Support**: Visit the Pirate Chain community forums
- **Source Code**: Available in the TreasureChest repository

---

## Version History

- **v6.0.0**: Initial implementation of Sapling and Orchard payment disclosure
  - Unified verification command supporting both protocols
  - GUI integration
  - Support for all transaction types (including cross-pool)
  - Automatic OVK selection based on transaction type

---

*For questions or issues, please contact the Pirate Chain development team or open an issue on GitHub.*
