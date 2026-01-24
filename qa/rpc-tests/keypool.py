#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2016-2022 The Zcash developers
# Copyright (c) 2017-2025 The Pirate developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

# Exercise the wallet keypool, and interaction with wallet encryption/locking

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, \
    start_nodes, start_node, stop_nodes, bitcoind_processes, wait_bitcoinds
import time

def check_array_result(object_array, to_match, expected):
    """
    Pass in array of JSON objects, a dictionary with key/value pairs
    to match against, and another dictionary with expected key/value
    pairs.
    """
    num_matched = 0
    for item in object_array:
        all_match = True
        for key,value in to_match.items():
            if item[key] != value:
                all_match = False
        if not all_match:
            continue
        for key,value in expected.items():
            if item[key] != value:
                raise AssertionError("%s : expected %s=%s"%(str(item), str(key), str(value)))
            num_matched = num_matched+1
    if num_matched == 0:
        raise AssertionError("No objects matched %s"%(str(to_match)))

class KeyPoolTest(BitcoinTestFramework):

    def run_test(self):
        nodes = self.nodes
        
        # Test keypool functionality
        print("Testing keypool functionality...")
        
        # Check initial keypool size using new RPC method
        nodes[0].keypoolrefill()  # Set keypool to default size 100
        initial_keypool_size = nodes[0].getkeypoolsize()
        print(f"Initial keypool size: {initial_keypool_size}")
        assert_equal(initial_keypool_size, 101, f"Default keypool size should be 101 (100+1), but got {initial_keypool_size}")
        
        # Generate some blocks and verify keypool size decrements by 1 each time
        starting_balance = nodes[0].getbalance()
        print("Generating blocks and monitoring keypool size...")
        for i in range(10):
            nodes[0].generate(1)
            current_keypool_size = nodes[0].getkeypoolsize()
            print(f"After block {i+1}: keypool size = {current_keypool_size}")
            
            #check balance - should be 256 * blocks generated + starting balance
            expected_balance = 256 * (i + 1)
            actual_balance = nodes[0].getbalance()
            assert_equal(actual_balance, expected_balance + starting_balance, f"Balance should be {expected_balance + starting_balance} after block {i+1}, but got {actual_balance}")
            
            # Keypool should be 100 after each block (decremented by 1 from initial 101)
            expected_keypool = 100
            assert_equal(current_keypool_size, expected_keypool, 
                        f"Keypool size should be {expected_keypool} but got {current_keypool_size} after generating block {i+1}")
        
        print("Keypool size correctly decrements during block generation - SUCCESS!")
        
        # Test keypool refill functionality
        print("\nTesting keypool refill...")
        nodes[0].keypoolrefill(150)  # Set keypool size to 150 + 1 = 151 total
        new_keypool_size = nodes[0].getkeypoolsize()
        print(f"After keypoolrefill(150): keypool size = {new_keypool_size}")
        # keypoolrefill sets the keypool to the requested amount + 1
        expected_size = 150 + 1  # 151
        assert_equal(new_keypool_size, expected_size, f"Keypool size should be {expected_size} after refill, but got {new_keypool_size}")
        
        # Generate blocks and verify keypool decrements back to default size
        print("Generating blocks with larger keypool - should decrement to default...")

        starting_balance = nodes[0].getbalance()
        # After refill to 151, each block will decrement by 1. We need to get down to 100
        blocks_to_generate = 55  # 151 - 100 = 51, but allow some extra
        for i in range(blocks_to_generate):
            nodes[0].generate(1)
            print("Block generated, checking keypool size...")
            current_keypool_size = nodes[0].getkeypoolsize()

            #Check Coin Balance - Should be 256 * Blocks Generated + Starting Balance
            expected_balance = 256 * (i + 1)
            actual_balance = nodes[0].getbalance()
            assert_equal(actual_balance, expected_balance + starting_balance, f"Balance should be {expected_balance + starting_balance} after block {i+1}, but got {actual_balance}")
            
            if i < 10 or i % 10 == 0:  # Print every 10th iteration to reduce spam
                print(f"After block {i+1}: keypool size = {current_keypool_size}")
            
            # After using the extra keys, should be back to default 100 (not 101)
            if current_keypool_size == 100:
                print(f"Keypool decremented back to default size 100 after {i+1} blocks")
                break
        
        # Verify we're back to default size of 100 (after block generation)
        final_keypool_size = nodes[0].getkeypoolsize()
        assert_equal(final_keypool_size, 100, f"Keypool should have decremented back to 100, but got {final_keypool_size}")
        
        # Test that keypool refill with smaller size than default n performs no action
        print("\nTesting keypool refill with smaller size than default...")
        nodes[0].keypoolrefill(50)  # Try to set keypool to 50 + 1 = 51 keys (smaller than default 100)
        after_small_refill = nodes[0].getkeypoolsize()
        print(f"After keypoolrefill(50): keypool size = {after_small_refill}")
        # Should remain at 100 since refill with value < n performs no action
        assert_equal(after_small_refill, 100, f"Keypool should remain at 100 (no action for refill < n), but got {after_small_refill}")
        
        # Generate a block to verify keypool behavior remains normal
        nodes[0].generate(1)
        after_block_keypool = nodes[0].getkeypoolsize()
        print(f"After generating 1 block: keypool size = {after_block_keypool}")
        # Should be 100 (normal behavior after block generation)
        assert_equal(after_block_keypool, 100, f"Keypool should be 100 after block generation, but got {after_block_keypool}")
        
        print("Keypool refill behavior verified - SUCCESS!")
        print("Keypool test completed successfully")

    def __init__(self):
        super().__init__()
        self.num_nodes = 1

    def setup_network(self):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, extra_args=[['-experimentalfeatures', '-developerencryptwallet']])

if __name__ == '__main__':
    KeyPoolTest().main()
