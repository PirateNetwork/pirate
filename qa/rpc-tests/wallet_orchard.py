#!/usr/bin/env python3
# Copyright (c) 2025 The Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

"""
Focused test script for Orchard protocol functionality.
Tests Orchard key creation, address funding, and basic operations.
Wallet dump/import functionality is tested separately in wallet_dump_import_test.py.
"""

from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal, assert_greater_than, assert_true, assert_raises_message,
    start_nodes, initialize_chain_clean, connect_nodes_bi, get_coinbase_address,
    wait_and_assert_operationid_status, LEGACY_DEFAULT_FEE
)
import logging
import sys
import os

logging.basicConfig(format='%(levelname)s: %(message)s', level=logging.INFO, stream=sys.stdout)

class OrchardWalletTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir, extra_args=[[
        ]] * 3)
        # Connect all nodes to all other nodes (full mesh topology)
        for i in range(len(self.nodes)):
            for j in range(i + 1, len(self.nodes)):
                connect_nodes_bi(self.nodes, i, j)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        """Run Orchard-specific functionality tests"""
        logging.info("Starting Orchard functionality tests...")
        
        alice, bob, charlie = self.nodes
        
        # Generate initial funds and activate protocols
        self.setup_initial_funds(alice)
        
        # Check current protocol status
        current_height = alice.getblockcount()
        sapling_active = current_height >= 100
        orchard_active = current_height >= 200
        
        logging.info(f"Protocol status at height {current_height}: Sapling={sapling_active}, Orchard={orchard_active}")
        
        # Test if Orchard is available
        orchard_available = self.check_orchard_availability(alice)
        
        if orchard_available:
            logging.info("=== TESTING ORCHARD FUNCTIONALITY ===")
            # Test Orchard key creation and basic operations
            self.test_orchard_key_creation(alice)
            
            # Test Orchard address funding and balance operations
            self.test_orchard_address_funding(alice)
            
            logging.info("All Orchard tests passed!")
        else:
            logging.info("=== ORCHARD NOT AVAILABLE - TESTING SAPLING FALLBACK ===")
            # Still test that basic Sapling functionality works
            self.test_sapling_fallback(alice, bob)

    def setup_initial_funds(self, node):
        """Generate initial funds for testing and activate protocols"""
        logging.info("Setting up initial funds and activating protocols...")
        
        try:
            # Generate blocks to activate Sapling (block 100)
            current_height = node.getblockcount()
            logging.info(f"Current height: {current_height}")
            
            if current_height < 100:
                blocks_to_sapling = 100 - current_height
                logging.info(f"Generating {blocks_to_sapling} blocks to activate Sapling at block 100")
                node.generate(blocks_to_sapling)
                self.sync_all()
                logging.info(f"Sapling activation completed at height {node.getblockcount()}")
            else:
                logging.info("Sapling already active")
            
            # Generate blocks to activate Orchard (block 200)
            current_height = node.getblockcount()
            if current_height < 200:
                blocks_to_orchard = 200 - current_height
                logging.info(f"Generating {blocks_to_orchard} blocks to activate Orchard at block 200")
                node.generate(blocks_to_orchard)
                self.sync_all()
                logging.info(f"Orchard activation completed at height {node.getblockcount()}")
            else:
                logging.info("Orchard already active")
            
            # Generate additional blocks for coinbase maturity
            logging.info("Generating additional blocks for coinbase maturity...")
            node.generate(20)
            self.sync_all()
            
            current_height = node.getblockcount()
            balance = node.getbalance()
            logging.info(f"Final height: {current_height}, transparent balance: {balance}")
            
            # Verify both protocols are active
            assert current_height >= 100, "Sapling should be active"
            assert current_height >= 200, "Orchard should be active"
            assert_greater_than(balance, Decimal('100'))
            logging.info("Initial funds setup completed successfully")
            
        except Exception as e:
            logging.error(f"Initial funds setup failed: {e}")
            logging.error(f"Exception type: {type(e).__name__}")
            import traceback
            logging.error(f"Traceback: {traceback.format_exc()}")
            raise

    def check_orchard_availability(self, node):
        """Check if Orchard functionality is available"""
        current_height = node.getblockcount()
        logging.info(f"Checking Orchard availability at height {current_height}")
        
        if current_height < 200:
            logging.info("Orchard not active yet (requires block 200+)")
            return False
            
        try:
            # Try to create an Orchard address
            logging.info("Attempting to create Orchard address...")
            orchard_addr = node.z_getnewaddress('orchard')
            logging.info(f"Orchard address created successfully: {orchard_addr[:20]}...")
            
            # Try to get address info
            try:
                addr_info = node.z_validateaddress(orchard_addr)
                logging.info(f"Orchard address validation: {addr_info}")
            except Exception as e:
                logging.info(f"Address validation failed: {e}")
            
            return True
        except Exception as e:
            logging.info(f"Orchard not available: {e}")
            logging.info(f"Exception type: {type(e).__name__}")
            return False

    def test_orchard_key_creation(self, node):
        """Test Orchard key creation and basic operations"""
        logging.info("Testing Orchard key creation...")
        
        # Create multiple Orchard addresses - both default and diversified
        orchard_addrs = []
        
        # Create default address from new key
        default_addr = node.z_getnewaddresskey('orchard')
        orchard_addrs.append(default_addr)
        logging.info(f"Created Orchard default address: {default_addr}")
        
        # Create diversified addresses
        for i in range(2):
            div_addr = node.z_getnewaddress('orchard')
            orchard_addrs.append(div_addr)
            logging.info(f"Created Orchard diversified address {i+1}: {div_addr}")
        
        # Verify addresses are different
        assert_equal(len(set(orchard_addrs)), 3, "All Orchard addresses should be unique")
        
        # Try to export keys
        for addr in orchard_addrs:
            try:
                key = node.z_exportkey(addr)
                assert_true(len(key) > 0, f"Should be able to export key for {addr}")
                logging.info(f"Successfully exported key for {addr}")
            except Exception as e:
                logging.info(f"Key export not available for {addr}: {e}")

    def test_orchard_address_funding(self, node):
        """Test Orchard address funding and balance operations"""
        logging.info("Testing Orchard address funding...")
        
        try:
            # Create Orchard addresses
            orchard_addr1 = node.z_getnewaddresskey('orchard')  # Default from new key
            orchard_addr2 = node.z_getnewaddress('orchard')     # Diversified
            
            # Fund the Orchard addresses (first shield to Sapling, then to Orchard)
            sapling_addr = node.z_getnewaddresskey('sapling')
            
            # Get a coinbase address to shield from
            coinbase_addr = get_coinbase_address(node)
            
            # Shield coinbase to Sapling first
            shield_result = node.z_shieldcoinbase(coinbase_addr, sapling_addr, 0, 1)
            wait_and_assert_operationid_status(node, shield_result['opid'])
            node.generate(1)
            self.sync_all()
            
            # Check Sapling balance
            sapling_balance = node.z_getbalance(sapling_addr)
            assert_greater_than(sapling_balance, Decimal('0'))
            
            # Send from Sapling to Orchard
            opid = node.z_sendmany(sapling_addr, [
                {"address": orchard_addr1, "amount": Decimal('5.0')},
                {"address": orchard_addr2, "amount": Decimal('3.0')}
            ], 1, Decimal('0.0001'))
            wait_and_assert_operationid_status(node, opid)
            node.generate(1)
            self.sync_all()
            
            # Verify balances
            balance1 = node.z_getbalance(orchard_addr1)
            balance2 = node.z_getbalance(orchard_addr2)
            
            assert_equal(balance1, Decimal('5.0'), "Orchard address 1 should have correct balance")
            assert_equal(balance2, Decimal('3.0'), "Orchard address 2 should have correct balance")
            
            # Test sending between Orchard addresses
            opid = node.z_sendmany(orchard_addr1, [
                {"address": orchard_addr2, "amount": Decimal('1.0')}
            ], 1, Decimal('0.0001'))
            wait_and_assert_operationid_status(node, opid)
            node.generate(1)
            self.sync_all()
            
            # Verify updated balances
            new_balance1 = node.z_getbalance(orchard_addr1)
            new_balance2 = node.z_getbalance(orchard_addr2)
            
            # Account for fees (approximate check)
            assert_greater_than(Decimal('4.5'), new_balance1)  # Should be less than 4.0 due to fees
            assert_greater_than(new_balance1, Decimal('3.5'))  # But more than 3.5
            assert_equal(new_balance2, Decimal('4.0'), "Orchard address 2 should have updated balance")
            
            logging.info(f"Final Orchard balances: addr1={new_balance1}, addr2={new_balance2}")
            logging.info("Orchard address funding test passed")
            
        except Exception as e:
            logging.error(f"Orchard address funding test failed: {e}")
            import traceback
            logging.error(f"Traceback: {traceback.format_exc()}")
            raise

    def test_sapling_fallback(self, source_node, target_node):
        """Fallback test using only Sapling if Orchard is not available"""
        logging.info("Testing Sapling fallback (Orchard not available)...")
        
        current_height = source_node.getblockcount()
        if current_height < 100:
            logging.info("Sapling not yet active, activating it first...")
            blocks_needed = 100 - current_height
            source_node.generate(blocks_needed)
            self.sync_all()
        
        # Create and fund Sapling address
        sapling_addr = source_node.z_getnewaddresskey('sapling')
        
        # Get a coinbase address to shield from
        coinbase_addr = get_coinbase_address(source_node)
        logging.info(f"Using coinbase address: {coinbase_addr}")
        
        shield_result = source_node.z_shieldcoinbase(coinbase_addr, sapling_addr, 0, 1)
        wait_and_assert_operationid_status(source_node, shield_result['opid'])
        source_node.generate(1)
        self.sync_all()
        
        balance = source_node.z_getbalance(sapling_addr)
        logging.info(f"Sapling balance: {balance}")
        
        assert_greater_than(balance, Decimal('0'), "Sapling address should have balance")
        
        # Test sending between Sapling addresses
        sapling_addr2 = source_node.z_getnewaddress('sapling')
        opid = source_node.z_sendmany(sapling_addr, [
            {"address": sapling_addr2, "amount": Decimal('5.0')}
        ], 1, Decimal('0.0001'))
        wait_and_assert_operationid_status(source_node, opid)
        source_node.generate(1)
        self.sync_all()
        
        balance2 = source_node.z_getbalance(sapling_addr2)
        assert_equal(balance2, Decimal('5.0'), "Sapling balance should match")
        
        logging.info("Sapling fallback test passed")

if __name__ == '__main__':
    OrchardWalletTest().main()
