#!/usr/bin/env python3
# Copyright (c) 2017 The Zcash developers
# Copyright (c) 2017-2025 The Pirate developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from decimal import Decimal
from functools import reduce
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, start_nodes, connect_nodes_bi, wait_and_assert_operationid_status

import logging
import sys
import time

logging.basicConfig(format='%(levelname)s: %(message)s', level=logging.INFO, stream=sys.stdout)


class KeyImportExportTest (BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.num_nodes = 3
        self.cache_behavior = 'clean'

    def setup_network(self, split=False):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, extra_args=[[
            '-minrelaytxfee=0',
            '-exportdir=' + self.options.tmpdir,
        ]] * self.num_nodes)
        # Connect all nodes to all other nodes (full mesh topology)
        for i in range(len(self.nodes)):
            for j in range(i + 1, len(self.nodes)):
                connect_nodes_bi(self.nodes, i, j)
        self.is_network_split=False
        self.safe_sync_all()

    def generate_blocks_with_delay(self, node, num_blocks):
        """Generate blocks with 1 second delay before each block"""
        for i in range(num_blocks):
            time.sleep(1)  # Wait 1 second before generating each block
            try:
                node.generate(1)
                if i < num_blocks - 1:  # Don't sleep after the last block
                    time.sleep(0.1)  # Small delay between blocks
            except Exception as e:
                print(f"Error generating block {i+1}/{num_blocks}: {e}")
                raise

    def safe_sync_all(self):
        """Sync all nodes with retry logic"""
        max_attempts = 10
        for attempt in range(max_attempts):
            try:
                self.sync_all()
                return
            except Exception as e:
                print(f"Sync attempt {attempt + 1} failed: {e}")
                if attempt == max_attempts - 1:
                    raise
                time.sleep(30)  # Wait before retry

    def run_test(self):
        [alice, bob, charlie] = self.nodes

        def create_and_test_transparent_key(node, label=""):
            """Create a transparent address and test key export/import without funding it"""
            addr = node.getnewaddress()
            if label:
                node.setaccount(addr, label)
            
            # Test that we can export the private key
            privkey = node.dumpprivkey(addr)
            assert len(privkey) > 0, "Should be able to export private key"
            
            return addr, privkey

        # Mine the first 10 blocks to get the chain started
        self.generate_blocks_with_delay(alice, 10)
        self.safe_sync_all()
        
        # Generate enough blocks to activate Sapling (block 100)
        self.generate_blocks_with_delay(alice, 90)
        self.safe_sync_all()
        
        # Generate more blocks for coinbase maturity
        self.generate_blocks_with_delay(alice, 110)
        self.safe_sync_all()

        logging.info("Testing transparent key creation and export...")
        
        # Create transparent addresses for testing key import/export (but don't fund them)
        addr1, privkey1 = create_and_test_transparent_key(bob, "test_addr_1")
        addr2, privkey2 = create_and_test_transparent_key(bob, "test_addr_2")
        
        # Verify all addresses are unique
        addresses = [addr1, addr2]
        assert len(set(addresses)) == 2, "All addresses should be unique"
        
        # Verify all private keys are unique
        privkeys = [privkey1, privkey2]
        assert len(set(privkeys)) == 2, "All private keys should be unique"
        
        logging.info("Testing transparent key import...")
        
        # Import the first key into charlie
        ipkaddr1 = charlie.importprivkey(privkey1, 'imported_addr_1', True)
        assert_equal(addr1, ipkaddr1, "Imported address should match original")
        
        # Verify charlie can export the same key
        charlie_privkey1 = charlie.dumpprivkey(addr1)
        assert_equal(privkey1, charlie_privkey1, "Exported key should match imported key")
        
        # Import the second key into charlie
        ipkaddr2 = charlie.importprivkey(privkey2, 'imported_addr_2', True)
        assert_equal(addr2, ipkaddr2, "Second imported address should match original")
        
        # Verify idempotent behavior - importing the same key again should work
        ipkaddr1_again = charlie.importprivkey(privkey1, 'imported_addr_1', True)
        assert_equal(addr1, ipkaddr1_again, "Re-importing should work and return same address")
        
        # Import the second key into alice for cross-node testing
        ipkaddr2_alice = alice.importprivkey(privkey2, 'alice_imported', True)
        assert_equal(addr2, ipkaddr2_alice, "Alice should be able to import the key")
        
        # Verify alice can export the key
        alice_privkey2 = alice.dumpprivkey(addr2)
        assert_equal(privkey2, alice_privkey2, "Alice should export the same key")
        
        # Test that charlie has the correct imported addresses
        charlie_addresses = charlie.getaddressesbyaccount('imported_addr_1')
        assert addr1 in charlie_addresses, "Charlie should have imported address 1"
        
        charlie_addresses2 = charlie.getaddressesbyaccount('imported_addr_2')
        assert addr2 in charlie_addresses2, "Charlie should have imported address 2"
        
        # Test that alice has the imported address
        alice_addresses = alice.getaddressesbyaccount('alice_imported')
        assert addr2 in alice_addresses, "Alice should have the imported address"
        
        logging.info("Testing wallet dump and import with transparent keys...")
        
        # Create additional transparent addresses in alice's wallet
        alice_addr1 = alice.getnewaddress()
        alice_addr2 = alice.getnewaddress()
        alice.setaccount(alice_addr1, "alice_addr_1")
        alice.setaccount(alice_addr2, "alice_addr_2")
        
        # Dump alice's wallet
        dump_file = "transparentkeytestdump"
        dump_path = alice.dumpwallet(dump_file)
        
        # Import alice's wallet into bob
        bob.importwallet(dump_path)
        
        # Verify bob can access alice's transparent keys
        alice_privkey1 = alice.dumpprivkey(alice_addr1)
        bob_privkey1 = bob.dumpprivkey(alice_addr1)
        assert_equal(alice_privkey1, bob_privkey1, "Bob should have alice's private key after import")
        
        # Verify account labels were imported
        bob_addresses1 = bob.getaddressesbyaccount('alice_addr_1')
        assert alice_addr1 in bob_addresses1, "Bob should have imported alice's labeled address"
        
        logging.info("All transparent key import/export tests passed!")
        logging.info("Note: No funds were sent to transparent addresses - only key management was tested.")


if __name__ == '__main__':
    KeyImportExportTest().main()
