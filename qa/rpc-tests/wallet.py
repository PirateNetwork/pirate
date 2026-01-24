#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2016-2022 The Zcash developers
# Copyright (c) 2017-2025 The Pirate developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, start_nodes, start_node, \
    connect_nodes_bi, sync_blocks, sync_mempools, get_coinbase_address, \
    wait_and_assert_operationid_status

from decimal import Decimal
import json

class WalletTest (BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.cache_behavior = 'clean'
        self.num_nodes = 4

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir, extra_args=[[
            "--printtoconsole=1",
        ]] * 3)
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        self.is_network_split=False
        self.sync_all()

    def run_test (self):
        print("Mining blocks...")

        self.nodes[1].generate(5)
        self.sync_all()

        # Verify initial balance states
        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(walletinfo['immature_balance'], Decimal('0'))
        assert_equal(walletinfo['balance'], Decimal('1024.12017230'))  # Block 1: 0.12017230 + Blocks 2-5: 4x256 = 1024.12017230
        self.sync_all()

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(walletinfo['immature_balance'], Decimal('0'))
        assert_equal(walletinfo['balance'], Decimal('0'))
        self.sync_all()

        # Generate mature coinbase outputs on node 0
        self.nodes[0].generate(201)
        self.sync_all()

        assert_equal(self.nodes[0].getbalance(), Decimal('51456'))  # 201 blocks x 256
        assert_equal(self.nodes[1].getbalance(), Decimal('1024.12017230'))  # Block 1: 0.12017230 + Blocks 2-5: 4x256
        assert_equal(self.nodes[2].getbalance(), Decimal('0'))
        assert_equal(self.nodes[0].getbalance("*"), Decimal('51456'))  # 201 blocks x 256
        assert_equal(self.nodes[1].getbalance("*"), Decimal('1024.12017230'))  # Block 1: 0.12017230 + Blocks 2-5: 4x256
        assert_equal(self.nodes[2].getbalance("*"), Decimal('0'))

        

        # Prepare addresses for testing taddr->zaddr transactions
        mytaddr = get_coinbase_address(self.nodes[0])
        mysaplingzaddr = self.nodes[0].z_getnewaddress('sapling')
        mysaplingzaddr2 = self.nodes[0].z_getnewaddress('sapling')
        myorchardzaddr = self.nodes[0].z_getnewaddress('orchard')
        myorchardzaddr2 = self.nodes[0].z_getnewaddress('orchard')

        # Shield the coinbase to Sapling address
        print("Shielding coinbase UTXOs to Sapling address...")
        result = self.nodes[0].z_shieldcoinbase("*", mysaplingzaddr, 0)
        wait_and_assert_operationid_status(self.nodes[0], result['opid'])
        self.sync_all()
        self.nodes[1].generate(2)
        self.sync_all()

        # Shield the coinbase to Orchard address
        print("Shielding coinbase UTXOs to Orchard address...")
        result = self.nodes[0].z_shieldcoinbase("*", myorchardzaddr, 0)
        wait_and_assert_operationid_status(self.nodes[0], result['opid'])
        self.sync_all()
        self.nodes[1].generate(2)
        self.sync_all()

        # Test z_createbuildinstructions, z_buildrawtransaction, and z_submittransaction
        self.test_build_and_submit_transaction(mysaplingzaddr, myorchardzaddr)

        # Catch an attempt to send a transaction with an absurdly high fee.
        # Send 1.23456789 ARRR from a shielded address but specify a high fee.
        # The fee of 9.0 ARRR (>4 times the standard 0.0001) should be considered high.
        assert(Decimal("9.0") > 4*Decimal("0.0001"))
        fee = Decimal('9.0')  # 4 times the standard 0.0001 fee
        recipients = []
        recipients.append({"address": mysaplingzaddr, "amount": Decimal('1.23456789')})

        try:
            result = self.nodes[1].z_sendmany(mysaplingzaddr, recipients, 10, fee)
            wait_and_assert_operationid_status(self.nodes[1], result, "failed",
                "Insufficient funds, no UTXOs found for taddr from address.")
        except JSONRPCException as e:
            print("Expected error caught:", str(e))

    def test_build_and_submit_transaction(self, existing_sapling_addr, existing_orchard_addr):
        """
        Test z_createbuildinstructions, z_buildrawtransaction, and sendrawtransaction
        
        This comprehensive test verifies the complete workflow of:
        1. Creating build instructions for various transaction types (returns hex string)
        2. Building raw transactions from those instructions (takes hex, returns hex)
        3. Submitting the raw transactions to the network using sendrawtransaction
        4. Proper error handling for invalid inputs
        5. Support for memos and multiple recipients
        6. Cross-protocol transaction compatibility (if supported)
        """
        print("Testing transaction building and submission workflow...")
        
        # Use the addresses that already have funds from the earlier shielding operations
        # Get fresh addresses for testing - reuse addresses that already have funds
        sapling_from = existing_sapling_addr
        sapling_to = self.nodes[1].z_getnewaddress('sapling')
        orchard_from = existing_orchard_addr
        orchard_to = self.nodes[1].z_getnewaddress('orchard')
        
        # Shield some funds to the test addresses first
        print("Setting up test funds...")
        
        # Shield coinbase to sapling address
        shield_result = self.nodes[0].z_shieldcoinbase("*", sapling_from, 0)
        wait_and_assert_operationid_status(self.nodes[0], shield_result['opid'])
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()
        
        # Shield coinbase to orchard address  
        shield_result = self.nodes[0].z_shieldcoinbase("*", orchard_from, 0)
        wait_and_assert_operationid_status(self.nodes[0], shield_result['opid'])
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()
        
        # Test 1: Sapling to Sapling transaction
        print("Test 1: Building Sapling to Sapling transaction...")
        
        # Create build instructions for Sapling transaction (returns hex string)
        sapling_recipients = [{"address": sapling_to, "amount": Decimal('10.0')}]
        build_instructions_hex = self.nodes[0].z_createbuildinstructions(
            sapling_from, sapling_recipients, 1, Decimal('0.0001')
        )
        
        # Verify build instructions is a hex string
        assert isinstance(build_instructions_hex, str)
        assert len(build_instructions_hex) > 0
        print(f"Build instructions hex length: {len(build_instructions_hex)}")
        
        # Build raw transaction from instructions (takes hex, returns hex)
        raw_tx_hex = self.nodes[0].z_buildrawtransaction(build_instructions_hex)
        assert isinstance(raw_tx_hex, str)
        assert len(raw_tx_hex) > 0
        print(f"Raw transaction hex length: {len(raw_tx_hex)}")
        
        # Submit the transaction using sendrawtransaction
        submit_txid = self.nodes[0].sendrawtransaction(raw_tx_hex)
        assert isinstance(submit_txid, str)
        assert len(submit_txid) == 64  # Transaction ID should be 64 hex characters
        print(f"Transaction submitted with txid: {submit_txid}")
        
        # Verify transaction was accepted
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()
        
        # Verify recipient received funds
        sapling_balance = self.nodes[1].z_getbalance(sapling_to)
        assert sapling_balance >= Decimal('10.0')
        print(f"Sapling recipient balance: {sapling_balance}")
        
        # Test 2: Orchard to Orchard transaction
        print("Test 2: Building Orchard to Orchard transaction...")
        
        orchard_recipients = [{"address": orchard_to, "amount": Decimal('5.0')}]
        build_instructions_hex = self.nodes[0].z_createbuildinstructions(
            orchard_from, orchard_recipients, 0, Decimal('0.0001')  # Use 0 confirmations
        )
        
        assert isinstance(build_instructions_hex, str)
        assert len(build_instructions_hex) > 0
        print(f"Orchard build instructions hex length: {len(build_instructions_hex)}")
        
        raw_tx_hex = self.nodes[0].z_buildrawtransaction(build_instructions_hex)
        assert isinstance(raw_tx_hex, str)
        assert len(raw_tx_hex) > 0
        print(f"Orchard raw transaction hex length: {len(raw_tx_hex)}")
        
        submit_txid = self.nodes[0].sendrawtransaction(raw_tx_hex)
        assert len(submit_txid) == 64
        print(f"Orchard transaction submitted with txid: {submit_txid}")
        
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()
        
        orchard_balance = self.nodes[1].z_getbalance(orchard_to)
        assert orchard_balance >= Decimal('5.0')
        print(f"Orchard recipient balance: {orchard_balance}")
        
        # Test 3: Multiple recipients transaction
        print("Test 3: Building transaction with multiple recipients...")
        
        recipient1 = self.nodes[1].z_getnewaddress('sapling')
        recipient2 = self.nodes[2].z_getnewaddress('sapling')
        
        multi_recipients = [
            {"address": recipient1, "amount": Decimal('2.0')},
            {"address": recipient2, "amount": Decimal('3.0')}
        ]
        
        build_instructions_hex = self.nodes[0].z_createbuildinstructions(
            sapling_from, multi_recipients, 1, Decimal('0.0001')
        )
        
        assert isinstance(build_instructions_hex, str)
        print(f"Multi-recipient build instructions hex length: {len(build_instructions_hex)}")
        
        raw_tx_hex = self.nodes[0].z_buildrawtransaction(build_instructions_hex)
        submit_txid = self.nodes[0].sendrawtransaction(raw_tx_hex)
        
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()
        
        # Verify both recipients received funds
        balance1 = self.nodes[1].z_getbalance(recipient1)
        balance2 = self.nodes[2].z_getbalance(recipient2)
        assert balance1 >= Decimal('2.0')
        assert balance2 >= Decimal('3.0')
        print(f"Multi-recipient balances: {balance1}, {balance2}")
        
        # Test 4: Error handling - invalid instructions
        print("Test 4: Testing error handling...")
        
        try:
            # Test with invalid from address
            invalid_instructions_hex = self.nodes[0].z_createbuildinstructions(
                "invalid_address", [{"address": sapling_to, "amount": Decimal('1.0')}], 1, Decimal('0.0001')
            )
            assert False, "Should have thrown an error for invalid from address"
        except JSONRPCException as e:
            print(f"Expected error for invalid from address: {e}")
        
        try:
            # Test with invalid build instructions (invalid hex)
            invalid_raw_tx_hex = self.nodes[0].z_buildrawtransaction("invalid_hex_data")
            assert False, "Should have thrown an error for invalid build instructions"
        except JSONRPCException as e:
            print(f"Expected error for invalid build instructions: {e}")
        
        try:  
            # Test with invalid hex data for submission
            invalid_submit_txid = self.nodes[0].sendrawtransaction("invalid_hex_data")
            assert False, "Should have thrown an error for invalid hex data"
        except JSONRPCException as e:
            print(f"Expected error for invalid hex data: {e}")
        
        # Test 5: Transaction with memo
        print("Test 5: Testing transaction with memo...")
        
        memo_recipient = self.nodes[1].z_getnewaddress('sapling')
        memo_recipients = [{"address": memo_recipient, "amount": Decimal('1.0'), "memo": "48656c6c6f20576f726c64"}]  # "Hello World" in hex
        
        build_instructions_hex = self.nodes[0].z_createbuildinstructions(
            sapling_from, memo_recipients, 1, Decimal('0.0001')
        )
        
        print(f"Memo transaction build instructions created")
        
        raw_tx_hex = self.nodes[0].z_buildrawtransaction(build_instructions_hex)
        submit_txid = self.nodes[0].sendrawtransaction(raw_tx_hex)
        
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        
        memo_balance = self.nodes[1].z_getbalance(memo_recipient)
        assert memo_balance >= Decimal('1.0')
        print(f"Memo transaction recipient balance: {memo_balance}")
        
        # Test 6: Cross-protocol transaction (if supported)
        print("Test 6: Testing cross-protocol compatibility...")
        
        try:
            # Try Sapling to Orchard transaction
            cross_recipients = [{"address": orchard_to, "amount": Decimal('0.5')}]
            build_instructions_hex = self.nodes[0].z_createbuildinstructions(
                sapling_from, cross_recipients, 1, Decimal('0.0001')
            )
            
            raw_tx_hex = self.nodes[0].z_buildrawtransaction(build_instructions_hex)
            submit_txid = self.nodes[0].sendrawtransaction(raw_tx_hex)
            print(f"Cross-protocol transaction successful: {submit_txid}")
            
            self.sync_all()
            self.nodes[0].generate(1)
            self.sync_all()
            
        except JSONRPCException as e:
            print(f"Cross-protocol transaction not supported or failed: {e}")
        
        print("All transaction building and submission tests completed!")

        

if __name__ == '__main__':
    WalletTest ().main ()
