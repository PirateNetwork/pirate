# Pirate Chain Manual Testing Guide

## Table of Contents

1. [Introduction](#introduction)
2. [Testing Environment Setup](#testing-environment-setup)
3. [Beginner Testing Instructions](#beginner-testing-instructions)
4. [Intermediate Testing Instructions](#intermediate-testing-instructions)
5. [Advanced Testing Instructions](#advanced-testing-instructions)
6. [Mining and Balance Testing with Testnet](#mining-and-balance-testing-with-testnet)
7. [Developer Testing Instructions](#developer-testing-instructions)
8. [Common Issues and Troubleshooting](#common-issues-and-troubleshooting)
9. [Reporting Issues](#reporting-issues)

## Introduction

This guide provides comprehensive manual testing instructions for Pirate Chain users across different skill levels. Whether you're a casual user or a developer, these instructions will help you thoroughly test Pirate Chain functionality and identify potential issues.

**IMPORTANT SAFETY NOTICE:**
- Always use test networks (regtest/testnet) for testing
- Never use real ARRR or mainnet for testing purposes
- Backup any important data before testing
- Testing may involve experimental features that could be unstable

## Testing Environment Setup

### Prerequisites

Before starting any testing, ensure you have:

1. **Hardware Requirements:**
   - Minimum 4GB RAM (8GB+ recommended)
   - 50GB+ free disk space
   - Stable internet connection

2. **Software Requirements:**
   - Latest Pirate Chain binaries (`pirated`, `pirate-cli`)
   - Text editor for configuration files
   - Terminal/command line access

### Basic Environment Setup

1. **Create Testing Directory:**
   ```bash
   mkdir ~/pirate-testing
   cd ~/pirate-testing
   ```

2. **Create Test Configuration:**
   ```bash
   mkdir -p ~/.komodo/PIRATE-TEST
   ```

3. **Create Test Config File (`~/.komodo/PIRATE-TEST/PIRATE.conf`):**
   ```
   server=1
   rpcuser=testuser
   rpcpassword=testpass123
   rpcallowip=127.0.0.1
   exportdir=/home/$(whoami)/pirate-testing/exports
   printtoconsole=1
   ```

4. **Start Test Node:**
   ```bash
   pirated -testnet=1 -datadir=~/.komodo/PIRATE-TEST -daemon
   ```
   
   **Note:** The `-testnet=1` parameter must be specified on the command line, not in the configuration file.

## Beginner Testing Instructions

### Level 1: Basic Wallet Operations

**Objective:** Test fundamental wallet functionality without advanced features.

#### Test 1: Node Startup and Basic Commands

1. **Start the node:**
   ```bash
   pirated -testnet=1 -datadir=~/.komodo/PIRATE-TEST
   ```

2. **Wait for startup (look for "Done loading" message)**

3. **Test basic connectivity:**
   ```bash
   pirate-cli -datadir=~/.komodo/PIRATE-TEST getinfo
   ```
   **Expected:** Node info including version, connections, and block count

4. **Check wallet status:**
   ```bash
   pirate-cli -datadir=~/.komodo/PIRATE-TEST getwalletinfo
   ```
   **Expected:** Wallet balance and transaction count (should be 0 for new wallet)

#### Test 2: Address Generation

1. **Generate transparent address:**
   ```bash
   pirate-cli -datadir=~/.komodo/PIRATE-TEST getnewaddress
   ```
   **Expected:** Returns a transparent address starting with 't'

2. **Generate Sapling address:**
   ```bash
   pirate-cli -datadir=~/.komodo/PIRATE-TEST z_getnewaddress sapling
   ```
   **Expected:** Returns a Sapling address starting with 'zs' or 'ztestsapling'

3. **List all addresses:**
   ```bash
   pirate-cli -datadir=~/.komodo/PIRATE-TEST z_listaddresses
   ```
   **Expected:** Shows all created shielded addresses

#### Test 3: Mining Test Coins (Regtest Mode)

**Switch to regtest for faster testing:**

1. **Stop testnet node and start regtest:**
   ```bash
   pirate-cli -datadir=~/.komodo/PIRATE-TEST stop
   # Wait for shutdown
   pirated -regtest=1 -datadir=~/.komodo/PIRATE-REGTEST
   ```

2. **Generate initial blocks:**
   ```bash
   pirate-cli -datadir=~/.komodo/PIRATE-REGTEST generate 101
   ```
   **Expected:** Generates 101 blocks, providing mature coinbase rewards

3. **Check balance:**
   ```bash
   pirate-cli -datadir=~/.komodo/PIRATE-REGTEST getbalance
   ```
   **Expected:** Shows transparent balance from mining rewards

#### Test 4: Basic Transactions

1. **Create Sapling address:**
   ```bash
   SAPLING_ADDR=$(pirate-cli -datadir=~/.komodo/PIRATE-REGTEST z_getnewaddress sapling)
   echo "Sapling address: $SAPLING_ADDR"
   ```

2. **Shield transparent coins:**
   ```bash
   pirate-cli -datadir=~/.komodo/PIRATE-REGTEST z_shieldcoinbase "*" $SAPLING_ADDR 0 1
   ```
   **Expected:** Returns operation ID

3. **Check operation status:**
   ```bash
   pirate-cli -datadir=~/.komodo/PIRATE-REGTEST z_getoperationstatus
   ```
   **Expected:** Shows "success" status when complete

4. **Generate block to confirm:**
   ```bash
   pirate-cli -datadir=~/.komodo/PIRATE-REGTEST generate 1
   ```

5. **Check shielded balance:**
   ```bash
   pirate-cli -datadir=~/.komodo/PIRATE-REGTEST z_getbalance $SAPLING_ADDR
   ```
   **Expected:** Shows shielded balance

**✅ Beginner Testing Complete:** If all tests pass, basic wallet functionality is working correctly.

## Intermediate Testing Instructions

### Level 2: Advanced Wallet Features

**Objective:** Test intermediate features including different address types and wallet backup/restore.

#### Test 5: Multiple Address Types

1. **Create different address types:**
   ```bash
   # Transparent
   T_ADDR=$(pirate-cli -regtest getnewaddress)
   
   # Sapling
   SAPLING_ADDR=$(pirate-cli -regtest z_getnewaddress sapling)
   
   # Orchard (if available)
   ORCHARD_ADDR=$(pirate-cli -regtest z_getnewaddress orchard)
   
   echo "Transparent: $T_ADDR"
   echo "Sapling: $SAPLING_ADDR"
   echo "Orchard: $ORCHARD_ADDR"
   ```

2. **Test address validation:**
   ```bash
   pirate-cli -regtest validateaddress $T_ADDR
   pirate-cli -regtest z_validateaddress $SAPLING_ADDR
   ```
   **Expected:** Each command returns detailed address information

#### Test 6: Cross-Pool Transactions

1. **Setup: Ensure you have funds in Sapling address from Test 4**

2. **Send from Sapling to Orchard (if available):**
   ```bash
   pirate-cli -regtest z_sendmany $SAPLING_ADDR '[{"address":"'$ORCHARD_ADDR'","amount":5.0}]' 1 0.0001
   ```

3. **Check operation and mine block:**
   ```bash
   pirate-cli -regtest z_getoperationstatus
   pirate-cli -regtest generate 1
   ```

4. **Verify balances:**
   ```bash
   pirate-cli -regtest z_getbalance $SAPLING_ADDR
   pirate-cli -regtest z_getbalance $ORCHARD_ADDR
   ```

#### Test 7: Wallet Backup and Recovery

1. **Create backup directory:**
   ```bash
   mkdir -p ~/pirate-testing/backups
   ```

2. **Export wallet:**
   ```bash
   pirate-cli -regtest z_exportwallet backup_test_$(date +%Y%m%d)
   ```
   **Expected:** Returns path to backup file

3. **List wallet contents:**
   ```bash
   ls -la ~/pirate-testing/exports/
   ```

4. **Create second wallet for import test:**
   ```bash
   pirate-cli -regtest stop
   # Backup current wallet
   cp ~/.komodo/PIRATE-REGTEST/wallet.dat ~/.komodo/PIRATE-REGTEST/wallet.dat.backup
   
   # Start with new wallet
   rm ~/.komodo/PIRATE-REGTEST/wallet.dat
   pirated -regtest=1 -datadir=~/.komodo/PIRATE-REGTEST
   ```

5. **Import previous wallet:**
   ```bash
   BACKUP_FILE=$(ls ~/pirate-testing/exports/backup_test_* | head -1)
   pirate-cli -regtest z_importwallet $BACKUP_FILE
   ```

6. **Verify addresses and balances restored:**
   ```bash
   pirate-cli -regtest z_listaddresses
   pirate-cli -regtest z_gettotalbalance
   ```

#### Test 8: Transaction History and Memo Fields

1. **Send transaction with memo:**
   ```bash
   NEW_SAPLING=$(pirate-cli -regtest z_getnewaddress sapling)
   pirate-cli -regtest z_sendmany $SAPLING_ADDR '[{"address":"'$NEW_SAPLING'","amount":1.0,"memo":"Test memo message"}]'
   ```

2. **Mine block and check received transaction:**
   ```bash
   pirate-cli -regtest generate 1
   pirate-cli -regtest z_listreceivedbyaddress $NEW_SAPLING
   ```
   **Expected:** Shows transaction with memo field

**✅ Intermediate Testing Complete:** Advanced wallet features are functioning correctly.

## Advanced Testing Instructions

### Level 3: Complex Scenarios and Edge Cases

**Objective:** Test complex scenarios, performance, and edge cases that may reveal subtle bugs.

#### Test 9: Large Transaction Testing

1. **Create multiple addresses:**
   ```bash
   for i in {1..10}; do
     ADDR=$(pirate-cli -regtest z_getnewaddress sapling)
     echo "Address $i: $ADDR" >> test_addresses.txt
   done
   ```

2. **Fund multiple addresses simultaneously:**
   ```bash
   RECIPIENTS='['
   while IFS= read -r line; do
     ADDR=$(echo $line | cut -d' ' -f3)
     RECIPIENTS+="{\"address\":\"$ADDR\",\"amount\":1.0},"
   done < test_addresses.txt
   RECIPIENTS="${RECIPIENTS%,}]"
   
   pirate-cli -regtest z_sendmany $SAPLING_ADDR "$RECIPIENTS"
   ```

3. **Monitor memory usage during operation:**
   ```bash
   watch -n 1 'ps aux | grep pirated | grep -v grep'
   ```

#### Test 10: Concurrent Operations Testing

1. **Start multiple operations simultaneously:**
   ```bash
   # Terminal 1
   pirate-cli -regtest z_sendmany $SAPLING_ADDR '[{"address":"'$NEW_SAPLING'","amount":0.5}]' &
   
   # Terminal 2
   pirate-cli -regtest z_shieldcoinbase "*" $ANOTHER_ADDR &
   
   # Terminal 3
   pirate-cli -regtest generate 10 &
   ```

2. **Monitor all operations:**
   ```bash
   watch -n 2 'pirate-cli -regtest z_getoperationstatus'
   ```

#### Test 11: Blockchain Reorganization Testing

1. **Create two nodes with different chains:**
   ```bash
   # Node 1 (existing)
   pirate-cli -regtest generate 5
   BLOCK1=$(pirate-cli -regtest getbestblockhash)
   
   # Start Node 2 with isolated network
   pirated -regtest=1 -datadir=~/.komodo/PIRATE-REGTEST2 -port=18844 -rpcport=18843
   pirate-cli -regtest -rpcport=18843 generate 10
   ```

2. **Connect nodes and observe reorganization:**
   ```bash
   pirate-cli -regtest addnode "127.0.0.1:18844" "add"
   ```

3. **Monitor chain tips:**
   ```bash
   pirate-cli -regtest getbestblockhash
   pirate-cli -regtest -rpcport=18843 getbestblockhash
   ```

#### Test 12: Performance and Stress Testing

1. **Large memo field test:**
   ```bash
   LARGE_MEMO=$(python3 -c "print('A' * 500)")
   pirate-cli -regtest z_sendmany $SAPLING_ADDR '[{"address":"'$NEW_SAPLING'","amount":0.1,"memo":"'$LARGE_MEMO'"}]'
   ```

2. **Rapid transaction generation:**
   ```bash
   for i in {1..20}; do
     pirate-cli -regtest z_sendmany $SAPLING_ADDR '[{"address":"'$NEW_SAPLING'","amount":0.01}]'
     sleep 1
   done
   ```

3. **Monitor node performance:**
   ```bash
   pirate-cli -regtest getmempoolinfo
   pirate-cli -regtest getnetworkinfo
   ```

**✅ Advanced Testing Complete:** Complex scenarios and edge cases tested successfully.

## Mining and Balance Testing with Testnet

### Level 3.5: Real Network Mining and Balance Verification

**Objective:** Test mining operations and balance management on the actual Pirate Chain testnet to simulate real-world conditions.

**⚠️ Important:** This section uses the real testnet network, which may be slower than regtest but provides realistic testing conditions.

#### Test 17: Testnet Setup and Initial Mining

1. **Setup testnet environment:**
   ```bash
   # Stop any existing nodes
   pirate-cli stop 2>/dev/null || true
   
   # Create testnet-specific directory
   mkdir -p ~/.komodo/PIRATE-TESTNET
   mkdir -p ~/pirate-testing/testnet-exports
   ```

2. **Create testnet configuration (`~/.komodo/PIRATE-TESTNET/PIRATE.conf`):**
   ```
   server=1
   rpcuser=testnetuser
   rpcpassword=testnetpass456
   rpcallowip=127.0.0.1
   rpcport=18232
   port=18233
   exportdir=/home/$(whoami)/pirate-testing/testnet-exports
   addnode=testnet.piratechain.com
   addnode=testnet2.piratechain.com
   printtoconsole=1
   debug=1
   ```

3. **Start testnet node:**
   ```bash
   pirated -testnet=1 -datadir=~/.komodo/PIRATE-TESTNET -daemon
   ```

4. **Wait for initial sync and check connection:**
   ```bash
   # Monitor sync progress
   while true; do
     BLOCKS=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getblockcount 2>/dev/null || echo "0")
     CONNECTIONS=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getconnectioncount 2>/dev/null || echo "0")
     echo "Blocks: $BLOCKS, Connections: $CONNECTIONS"
     sleep 30
     if [ "$CONNECTIONS" -gt 0 ] && [ "$BLOCKS" -gt 100 ]; then
       echo "✅ Testnet sync established"
       break
     fi
   done
   ```

5. **Test solo mining on testnet:**
   ```bash
   # Generate mining address
   MINING_ADDR=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getnewaddress)
   echo "Mining address: $MINING_ADDR"
   
   # Start mining (may take considerable time on testnet)
   echo "Starting testnet mining... This may take several minutes or hours."
   pirate-cli -datadir=~/.komodo/PIRATE-TESTNET generate 1
   ```

6. **Monitor mining progress:**
   ```bash
   # Check if block was mined
   BALANCE=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getbalance)
   echo "Current balance: $BALANCE ARRR"
   
   # Verify mining reward
   if [ "$(echo "$BALANCE > 0" | bc -l)" -eq 1 ]; then
     echo "✅ Successfully mined block and received reward"
   else
     echo "⏳ Mining in progress or no reward yet (coinbase maturity = 100 blocks)"
   fi
   ```

#### Test 18: Testnet Pool Mining Simulation

1. **Setup multiple addresses for pool-like testing:**
   ```bash
   # Create mining pool addresses
   for i in {1..5}; do
     ADDR=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getnewaddress)
     echo "Pool address $i: $ADDR" >> testnet_pool_addresses.txt
   done
   ```

2. **Simulate distributed mining rewards:**
   ```bash
   # If we have balance from previous mining
   BALANCE=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getbalance)
   if [ "$(echo "$BALANCE > 1" | bc -l)" -eq 1 ]; then
     # Distribute rewards to pool addresses
     while IFS= read -r line; do
       ADDR=$(echo $line | cut -d' ' -f4)
       AMOUNT="0.1"
       echo "Sending $AMOUNT ARRR to $ADDR"
       pirate-cli -datadir=~/.komodo/PIRATE-TESTNET sendtoaddress $ADDR $AMOUNT
     done < testnet_pool_addresses.txt
   else
     echo "⏳ Insufficient balance for pool distribution test"
   fi
   ```

3. **Monitor transaction confirmations:**
   ```bash
   # Check pending transactions
   pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getmempoolinfo
   
   # List recent transactions
   pirate-cli -datadir=~/.komodo/PIRATE-TESTNET listtransactions "*" 10
   ```

#### Test 19: Shielded Mining and Balance Management

1. **Create shielded addresses for mining rewards:**
   ```bash
   # Create Sapling address for shielded storage
   SAPLING_MINING=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_getnewaddress sapling)
   echo "Sapling mining address: $SAPLING_MINING"
   
   # Create Orchard address if available
   ORCHARD_MINING=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_getnewaddress orchard 2>/dev/null || echo "Orchard not available")
   echo "Orchard mining address: $ORCHARD_MINING"
   ```

2. **Shield all mining rewards:**
   ```bash
   # Check transparent balance
   T_BALANCE=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getbalance)
   echo "Transparent balance to shield: $T_BALANCE ARRR"
   
   if [ "$(echo "$T_BALANCE > 0.001" | bc -l)" -eq 1 ]; then
     # Shield all coinbase to Sapling
     echo "Shielding coinbase rewards to Sapling..."
     SHIELD_OP=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_shieldcoinbase "*" $SAPLING_MINING 0 0.0001)
     echo "Shield operation: $SHIELD_OP"
     
     # Monitor shielding operation
     while true; do
       STATUS=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_getoperationstatus)
       echo "Operation status: $STATUS"
       if echo "$STATUS" | grep -q "success"; then
         echo "✅ Shielding completed successfully"
         break
       elif echo "$STATUS" | grep -q "failed"; then
         echo "❌ Shielding failed"
         break
       fi
       sleep 10
     done
   fi
   ```

3. **Verify shielded balances:**
   ```bash
   # Check Sapling balance
   SAPLING_BAL=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_getbalance $SAPLING_MINING)
   echo "Sapling balance: $SAPLING_BAL ARRR"
   
   # Check total balances
   pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_gettotalbalance
   ```

#### Test 20: Cross-Pool Balance Transfers on Testnet

1. **Test Sapling to Orchard transfers (if available):**
   ```bash
   if [ "$ORCHARD_MINING" != "Orchard not available" ]; then
     TRANSFER_AMOUNT="0.5"
     echo "Transferring $TRANSFER_AMOUNT ARRR from Sapling to Orchard..."
     
     TRANSFER_OP=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_sendmany $SAPLING_MINING '[{"address":"'$ORCHARD_MINING'","amount":'$TRANSFER_AMOUNT',"memo":"Testnet cross-pool transfer"}]' 1 0.0001)
     echo "Transfer operation: $TRANSFER_OP"
     
     # Monitor transfer
     sleep 5
     pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_getoperationstatus
   else
     echo "⏳ Orchard not available, skipping cross-pool test"
   fi
   ```

2. **Test multiple simultaneous transfers:**
   ```bash
   # Create multiple destination addresses
   DEST1=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_getnewaddress sapling)
   DEST2=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_getnewaddress sapling)
   
   # Send to multiple addresses in one transaction
   MULTI_OP=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_sendmany $SAPLING_MINING '[{"address":"'$DEST1'","amount":0.1,"memo":"Multi-send test 1"},{"address":"'$DEST2'","amount":0.1,"memo":"Multi-send test 2"}]' 1 0.0001)
   echo "Multi-send operation: $MULTI_OP"
   ```

#### Test 21: Testnet Performance and Load Testing

1. **Test rapid transaction generation:**
   ```bash
   # Create test addresses for load testing
   for i in {1..10}; do
     LOAD_ADDR=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_getnewaddress sapling)
     echo $LOAD_ADDR >> testnet_load_addresses.txt
   done
   
   # Send small amounts rapidly
   echo "Starting load test with rapid transactions..."
   START_TIME=$(date +%s)
   
   while IFS= read -r addr; do
     pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_sendmany $SAPLING_MINING '[{"address":"'$addr'","amount":0.01}]' 1 0.0001 &
     sleep 2  # Prevent overwhelming the node
   done < testnet_load_addresses.txt
   
   END_TIME=$(date +%s)
   DURATION=$((END_TIME - START_TIME))
   echo "Load test duration: $DURATION seconds"
   ```

2. **Monitor network and node performance:**
   ```bash
   # Check memory usage
   echo "Node memory usage:"
   ps aux | grep pirated | grep -v grep
   
   # Check mempool status
   echo "Mempool information:"
   pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getmempoolinfo
   
   # Check network status
   echo "Network information:"
   pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getnetworkinfo | grep -E "(connections|version|subversion)"
   ```

#### Test 22: Testnet Wallet Backup and Recovery with Real Data

1. **Create comprehensive wallet backup:**
   ```bash
   # Export wallet with real testnet data
   BACKUP_DATE=$(date +%Y%m%d_%H%M%S)
   BACKUP_FILE="testnet_wallet_backup_$BACKUP_DATE"
   
   pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_exportwallet $BACKUP_FILE
   echo "Wallet backed up to: ~/pirate-testing/testnet-exports/$BACKUP_FILE"
   ```

2. **Test wallet recovery with transaction history:**
   ```bash
   # Stop node and backup current wallet
   pirate-cli -datadir=~/.komodo/PIRATE-TESTNET stop
   cp ~/.komodo/PIRATE-TESTNET/wallet.dat ~/.komodo/PIRATE-TESTNET/wallet.dat.testnet_backup
   
   # Start with fresh wallet
   rm ~/.komodo/PIRATE-TESTNET/wallet.dat
   pirated -testnet=1 -datadir=~/.komodo/PIRATE-TESTNET -daemon
   
   # Wait for startup
   sleep 10
   
   # Import backed up wallet
   LATEST_BACKUP=$(ls ~/pirate-testing/testnet-exports/testnet_wallet_backup_* | tail -1)
   pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_importwallet $LATEST_BACKUP
   ```

3. **Verify complete recovery:**
   ```bash
   # Check all addresses recovered
   echo "Recovered addresses:"
   pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_listaddresses
   
   # Check balances match
   echo "Recovered balances:"
   pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_gettotalbalance
   
   # Check transaction history
   echo "Recent transactions:"
   pirate-cli -datadir=~/.komodo/PIRATE-TESTNET z_listreceivedbyaddress $SAPLING_MINING 0
   ```

#### Test 23: Long-Term Testnet Stability Testing

1. **Setup monitoring script:**
   ```bash
   cat > testnet_monitor.sh << 'EOF'
   #!/bin/bash
   LOGFILE="testnet_stability_$(date +%Y%m%d).log"
   
   while true; do
     TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
     BLOCKS=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getblockcount 2>/dev/null || echo "ERROR")
     CONNECTIONS=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getconnectioncount 2>/dev/null || echo "ERROR")
     MEMPOOL=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getmempoolinfo 2>/dev/null | jq '.size' 2>/dev/null || echo "ERROR")
     
     echo "$TIMESTAMP - Blocks: $BLOCKS, Connections: $CONNECTIONS, Mempool: $MEMPOOL" >> $LOGFILE
     
     # Check for errors
     if [ "$BLOCKS" = "ERROR" ] || [ "$CONNECTIONS" = "ERROR" ]; then
       echo "$TIMESTAMP - NODE ERROR DETECTED" >> $LOGFILE
     fi
     
     sleep 300  # Check every 5 minutes
   done
   EOF
   
   chmod +x testnet_monitor.sh
   ```

2. **Run stability test:**
   ```bash
   echo "Starting 24-hour stability monitoring..."
   ./testnet_monitor.sh &
   MONITOR_PID=$!
   echo "Monitor PID: $MONITOR_PID"
   
   # Let it run for desired duration
   echo "Monitor running. Check testnet_stability_$(date +%Y%m%d).log for results."
   echo "To stop: kill $MONITOR_PID"
   ```

#### Test 24: Testnet Mining Profitability Analysis

1. **Calculate mining costs vs rewards:**
   ```bash
   cat > mining_analysis.sh << 'EOF'
   #!/bin/bash
   START_TIME=$(date +%s)
   START_BALANCE=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getbalance)
   START_BLOCKS=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getblockcount)
   
   echo "Starting mining analysis..."
   echo "Start time: $(date)"
   echo "Start balance: $START_BALANCE ARRR"
   echo "Start block: $START_BLOCKS"
   
   # Mine for specified duration (in seconds)
   DURATION=${1:-3600}  # Default 1 hour
   END_TIME=$((START_TIME + DURATION))
   
   while [ $(date +%s) -lt $END_TIME ]; do
     # Attempt to mine a block
     pirate-cli -datadir=~/.komodo/PIRATE-TESTNET generate 1 >/dev/null 2>&1
     
     CURRENT_BLOCKS=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getblockcount)
     if [ $CURRENT_BLOCKS -gt $START_BLOCKS ]; then
       echo "Mined block $CURRENT_BLOCKS"
       START_BLOCKS=$CURRENT_BLOCKS
     fi
     
     sleep 60  # Check every minute
   done
   
   # Calculate results
   END_BALANCE=$(pirate-cli -datadir=~/.komodo/PIRATE-TESTNET getbalance)
   DURATION_HOURS=$(echo "scale=2; $DURATION / 3600" | bc -l)
   REWARD_GAINED=$(echo "$END_BALANCE - $START_BALANCE" | bc -l)
   
   echo "Mining analysis complete:"
   echo "Duration: $DURATION_HOURS hours"
   echo "Reward gained: $REWARD_GAINED ARRR"
   echo "Hourly rate: $(echo "scale=4; $REWARD_GAINED / $DURATION_HOURS" | bc -l) ARRR/hour"
   EOF
   
   chmod +x mining_analysis.sh
   ```

2. **Run mining analysis:**
   ```bash
   # Run for 1 hour (3600 seconds)
   ./mining_analysis.sh 3600
   ```

**✅ Testnet Mining and Balance Testing Complete:** Real-world testnet mining and balance management tested successfully.

### Testnet Testing Summary

The testnet mining and balance testing provides:
- **Real Network Conditions:** Unlike regtest, testnet simulates actual network conditions
- **Mining Verification:** Tests both solo and pool-style mining scenarios
- **Balance Management:** Comprehensive testing of transparent and shielded balance operations
- **Performance Analysis:** Real-world performance and stability monitoring
- **Recovery Testing:** Wallet backup and recovery with actual transaction history

**Key Metrics to Monitor:**
- Block generation time and success rate
- Transaction confirmation times
- Memory usage during operations
- Network connection stability
- Balance accuracy across different address types

**⚠️ Notes for Testnet Testing:**
- Testnet mining may be significantly slower than regtest
- Network connectivity depends on testnet peer availability
- Some operations may timeout - this is normal for testnet
- Always backup wallet.dat before testing recovery procedures

## Developer Testing Instructions

### Level 4: Development and Debugging

**Objective:** Test development features, debugging capabilities, and integration testing.

#### Test 13: RPC Interface Testing

1. **Test all RPC commands:**
   ```bash
   pirate-cli -regtest help | grep -E "^[a-z]" > rpc_commands.txt
   
   # Test each command with help
   while read cmd; do
     echo "Testing: $cmd"
     pirate-cli -regtest help $cmd > /dev/null
     if [ $? -eq 0 ]; then
       echo "✅ $cmd help works"
     else
       echo "❌ $cmd help failed"
     fi
   done < rpc_commands.txt
   ```

2. **Test RPC error handling:**
   ```bash
   # Invalid commands
   pirate-cli -regtest invalidcommand 2>&1 | grep -q "Method not found" && echo "✅ Error handling works"
   
   # Invalid parameters
   pirate-cli -regtest getblock "invalid" 2>&1 | grep -q "Error" && echo "✅ Parameter validation works"
   ```

#### Test 14: Log Analysis and Debugging

1. **Enable detailed logging:**
   ```bash
   pirate-cli -regtest stop
   pirated -regtest=1 -datadir=~/.komodo/PIRATE-REGTEST -debug=all -printtoconsole=1 > debug.log 2>&1 &
   ```

2. **Generate transactions and analyze logs:**
   ```bash
   pirate-cli -regtest z_sendmany $SAPLING_ADDR '[{"address":"'$NEW_SAPLING'","amount":0.1}]'
   
   # Analyze logs for errors or warnings
   grep -i error debug.log
   grep -i warning debug.log
   ```

3. **Test log rotation:**
   ```bash
   # Check current log size
   ls -lh ~/.komodo/PIRATE-REGTEST/debug.log
   
   # Force log activity
   for i in {1..100}; do
     pirate-cli -regtest getinfo > /dev/null
   done
   ```

#### Test 15: Integration Testing

1. **Test with external tools:**
   ```bash
   # Test JSON-RPC directly
   curl -u testuser:testpass123 -X POST -H "Content-Type: application/json" \
     -d '{"method":"getinfo","params":[],"id":1}' \
     http://127.0.0.1:18232/
   ```

2. **Test wallet file integrity:**
   ```bash
   pirate-cli -regtest stop
   
   # Check wallet file
   file ~/.komodo/PIRATE-REGTEST/wallet.dat
   
   # Test wallet repair
   pirated -regtest=1 -datadir=~/.komodo/PIRATE-REGTEST -salvagewallet
   ```

#### Test 16: Automated Test Suite

1. **Run Python test framework:**
   ```bash
   cd ~/pirate-testing
   git clone https://github.com/PirateNetwork/pirate
   cd pirate/qa/rpc-tests
   
   # Run specific tests
   python3 wallet_addresses.py
   python3 wallet_backup.py
   python3 zkey_import_export.py
   ```

2. **Create custom test script:**
   ```bash
   cat > custom_test.py << 'EOF'
   #!/usr/bin/env python3
   from test_framework.test_framework import BitcoinTestFramework
   from test_framework.util import *
   
   class CustomTest(BitcoinTestFramework):
       def run_test(self):
           # Add your custom test logic here
           print("Custom test running...")
           
   if __name__ == '__main__':
       CustomTest().main()
   EOF
   
   python3 custom_test.py
   ```

**✅ Developer Testing Complete:** Development and debugging features tested thoroughly.

## Common Issues and Troubleshooting

### Node Startup Issues

**Problem:** Node fails to start
**Solutions:**
1. Check if ports are already in use: `netstat -tulpn | grep 18232`
2. Verify configuration file syntax
3. Check disk space: `df -h`
4. Review debug.log for specific errors

### Transaction Issues

**Problem:** Transactions fail or take too long
**Solutions:**
1. Check operation status: `pirate-cli z_getoperationstatus`
2. Verify sufficient balance including fees
3. Ensure addresses are valid: `pirate-cli z_validateaddress <address>`
4. Check mempool: `pirate-cli getmempoolinfo`

### Synchronization Issues

**Problem:** Nodes don't sync or sync slowly
**Solutions:**
1. Check network connectivity: `pirate-cli getnetworkinfo`
2. Add peers manually: `pirate-cli addnode <peer> add`
3. Restart with `-reindex` if blockchain is corrupted
4. Check system time accuracy

### Wallet Issues

**Problem:** Wallet corruption or missing transactions
**Solutions:**
1. Restore from backup: `cp wallet.dat.backup wallet.dat`
2. Rescan blockchain: `pirate-cli -rescan`
3. Import keys from backup: `pirate-cli z_importwallet <backup_file>`
4. Check address derivation: `pirate-cli z_listaddresses`

## Reporting Issues

### Information to Include

When reporting issues, please provide:

1. **Environment Information:**
   - Operating system and version
   - Pirate Chain version: `pirate-cli getinfo`
   - Hardware specifications

2. **Issue Details:**
   - Exact steps to reproduce
   - Expected vs actual behavior
   - Error messages (full text)
   - Screenshots if relevant

3. **Logs and Debug Information:**
   - Relevant portions of debug.log
   - RPC command outputs
   - System resource usage

4. **Test Results:**
   - Which tests from this guide pass/fail
   - Any modifications made to standard procedures

### Where to Report

- **GitHub Issues:** https://github.com/PirateNetwork/pirate/issues
- **Discord:** Pirate Chain development channels
- **Forum:** Official Pirate Chain forums

### Issue Templates

**Bug Report Template:**
```
**Environment:**
- OS: [e.g., Ubuntu 20.04]
- Pirate Version: [e.g., v5.9.0]
- Testing Level: [Beginner/Intermediate/Advanced/Developer]

**Description:**
Brief description of the issue

**Steps to Reproduce:**
1. Step one
2. Step two
3. Step three

**Expected Behavior:**
What should have happened

**Actual Behavior:**
What actually happened

**Additional Information:**
- Error messages
- Log excerpts
- Screenshots
```

## Conclusion

This comprehensive testing guide provides structured testing procedures for users of all skill levels. Regular testing helps ensure Pirate Chain remains stable, secure, and functional across different environments and use cases.

Remember to:
- Always test on test networks first
- Document any issues found
- Keep testing environments isolated from production
- Update testing procedures as new features are added

For questions about this testing guide or additional testing scenarios, please reach out to the Pirate Chain development community.

---

**Last Updated:** August 2025  
**Version:** 1.0  
**Compatible with:** Pirate Chain v5.9.0+
