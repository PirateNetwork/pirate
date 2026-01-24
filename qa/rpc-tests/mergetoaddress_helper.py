#!/usr/bin/env python3
# Copyright (c) 2018 The Zcash developers
# Copyright (c) 2022-2025 Pirate developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

"""
Pirate Chain z_mergetoaddress RPC Test Helper

This module provides common test functionality for testing the z_mergetoaddress RPC
method before and after Sapling activation. It handles the setup and execution of
comprehensive test scenarios for merging notes between different shielded address
types (Sapling and Orchard).

The helper class manages:
- Test environment initialization
- Network setup with multiple nodes
- Address generation and funding
- Merge operation testing with various parameters
- Balance verification and error condition testing
"""

from test_framework.authproxy import JSONRPCException
from test_framework.util import (
    assert_equal, connect_nodes_bi, fail,
    initialize_chain_clean, start_node,
    wait_and_assert_operationid_status, LEGACY_DEFAULT_FEE,
    generate_with_delay
)
from decimal import Decimal
import time

# Default fee for shielding transactions
DEFAULT_FEE = Decimal('0.0001')


def assert_mergetoaddress_exception(expected_error_msg, merge_to_address_lambda):
    """
    Assert that a merge-to-address operation raises the expected exception.
    
    This utility function tests error conditions by executing a lambda function
    that should raise a JSONRPCException with a specific error message. It's used
    to verify that the z_mergetoaddress RPC properly validates parameters and
    rejects invalid operations.
    
    Args:
        expected_error_msg (str): The expected error message from the RPC call
        merge_to_address_lambda (callable): Lambda function that calls z_mergetoaddress
        
    Raises:
        AssertionError: If the operation doesn't fail with the expected error message
        Exception: If an unexpected exception type is raised
    """
    try:
        merge_to_address_lambda()
    except JSONRPCException as e:
        assert_equal(expected_error_msg, e.error['message'])
    except Exception as e:
        fail("Expected JSONRPCException. Found %s" % repr(e))
    else:
        fail("Expected exception: “%s”, but didn’t fail" % expected_error_msg)


class MergeToAddressHelper:
    """
    Test helper class for z_mergetoaddress RPC functionality.
    
    This class provides a comprehensive test suite for the z_mergetoaddress RPC method,
    which allows merging multiple UTXOs and/or notes from different pools (transparent,
    Sapling shielded) into a single output. The helper manages the test environment,
    node setup, and various test scenarios.
    
    Key functionalities tested:
    - Merging transparent UTXOs to shielded addresses
    - Merging shielded notes between different pools
    - Error handling for invalid parameters
    - Fee calculation and validation
    - Balance verification after merge operations
    """

    def setup_chain(self, test):
        """
        Initialize the blockchain test environment.
        
        Sets up a clean blockchain environment for testing by initializing
        the test directory and preparing the necessary data structures.
        
        Args:
            test: The test framework instance containing configuration options
        """
        print("Initializing test directory " + test.options.tmpdir)
        initialize_chain_clean(test.options.tmpdir, 4)

    def setup_network(self, test, additional_args=[]):
        """
        Initialize the test network with multiple nodes.
        
        Creates and configures a network of three interconnected nodes with
        appropriate parameters for testing z_mergetoaddress functionality.
        All nodes are fully connected to each other (mesh topology).
        
        Args:
            test: The test framework instance
            additional_args (list): Additional command-line arguments for node startup
            
        Node Configuration:
            - Minimum relay transaction fee set to 0 for testing
            - Debug logging enabled for RPC operations
            - Deprecated RPC methods allowed for compatibility
        """
        args = [
        ]
        args += additional_args
        test.nodes = []
        test.nodes.append(start_node(0, test.options.tmpdir, args))
        test.nodes.append(start_node(1, test.options.tmpdir, args))
        test.nodes.append(start_node(2, test.options.tmpdir, args))
        connect_nodes_bi(test.nodes, 0, 1)
        connect_nodes_bi(test.nodes, 1, 2)
        connect_nodes_bi(test.nodes, 0, 2)
        test.is_network_split = False
        test.sync_all()

    def run_test(self, test):
        """
        Execute the main test suite for z_mergetoaddress functionality.
        
        This method runs comprehensive tests for the z_mergetoaddress RPC,
        including setup of test addresses, funding operations, merge operations,
        and validation of results. Tests cover both successful operations and
        error conditions.
        
        Args:
            test: The test framework instance with initialized nodes
            
        Test Coverage:
            - Address generation for different pool types
            - Transaction mining and synchronization  
            - Balance verification across different address types
            - Error handling for invalid operations
            - Fee calculation validation
        """
        def generate_and_check(node, expected_transactions):
            """
            Generate a block and verify the expected number of transactions.
            
            Args:
                node: The node to generate the block on
                expected_transactions (int): Expected number of transactions in the block
            """
            [blockhash] = generate_with_delay(node, 1)
            assert_equal(len(node.getblock(blockhash)['tx']), expected_transactions)


        # =================================================================
        # WALLET BALANCE VERIFICATION: Check initial wallet states
        # =================================================================
        
        generate_with_delay(test.nodes[0], 5)
        test.sync_all()
        walletinfo = test.nodes[0].getwalletinfo()

        # Verify initial balance states
        assert_equal(walletinfo['immature_balance'], Decimal('0'))
        assert_equal(walletinfo['balance'], Decimal('1024.12017230'))  # Block 1: 0.12017230 + Blocks 2-5: 4×256 = 1024.12017230
        test.sync_all()
        
        # Generate additional blocks for mature coinbase outputs
        generate_with_delay(test.nodes[2], 1)
        test.nodes[2].getnewaddress()
        generate_with_delay(test.nodes[2], 1)
        test.nodes[2].getnewaddress()
        generate_with_delay(test.nodes[2], 1)
        test.sync_all()
        generate_with_delay(test.nodes[1], 201)
        test.sync_all()

        # Re-verify wallet balances after maturation
        walletinfo = test.nodes[0].getwalletinfo()
        walletinfo2 = test.nodes[1].getwalletinfo()
        walletinfo3 = test.nodes[2].getwalletinfo()

        # Verify expected balance distributions
        assert_equal(test.nodes[0].getbalance(), Decimal('1024.12017230'))  # Block 1: 0.12017230 + Blocks 2-5: 4×256 = 1024.12017230
        assert_equal(test.nodes[1].getbalance(), Decimal('51456'))  # 201 blocks × 256
        assert_equal(test.nodes[2].getbalance(), Decimal('768'))  # 3 blocks × 256

        # =================================================================
        # ADDRESS CREATION AND FUNDING: Generate and fund addresses
        # =================================================================

        mysaplingaddr = test.nodes[0].z_getnewaddress('sapling')
        myorchardaddr = test.nodes[0].z_getnewaddress('orchard')
        zerrortestaddr = test.nodes[0].z_getnewaddress('sapling')

        # =================================================================
        # SHIELDING OPERATIONS: Move coinbase UTXOs to shielded pools
        # =================================================================
        
        # Shield the coinbase to Sapling address
        print("Shielding coinbase UTXOs to Sapling address...")
        result = test.nodes[0].z_shieldcoinbase("*", mysaplingaddr, 0)
        wait_and_assert_operationid_status(test.nodes[0], result['opid'])
        time.sleep(1)
        test.sync_all()
        time.sleep(1)
        generate_and_check(test.nodes[1], 2)
        time.sleep(1)
        test.sync_all()

        # Shield the coinbase to Orchard address
        print("Shielding coinbase UTXOs to Orchard address...")
        result = test.nodes[1].z_shieldcoinbase("*", myorchardaddr, 0)
        wait_and_assert_operationid_status(test.nodes[1], result['opid'])
        time.sleep(1)
        test.sync_all()
        time.sleep(1)
        generate_and_check(test.nodes[0], 2)
        time.sleep(1)
        test.sync_all()

        # =================================================================
        # TEST DATA PREPARATION: Create multiple addresses and fund them
        # =================================================================
        
        # Prepare some UTXOs and notes for merging
        zsaplingaddr1 = test.nodes[0].z_getnewaddress('sapling')
        zsaplingaddr2 = test.nodes[0].z_getnewaddress('sapling')
        zsaplingaddr3 = test.nodes[0].z_getnewaddress('sapling')
        zorchardaddr1 = test.nodes[0].z_getnewaddress('orchard')
        zorchardaddr2 = test.nodes[0].z_getnewaddress('orchard')
        zorchardaddr3 = test.nodes[0].z_getnewaddress('orchard')

        # Send some funds from Sapling to Sapling & Orchard addresses
        result = test.nodes[0].z_sendmany(mysaplingaddr, [
            {'address': zsaplingaddr1, 'amount': Decimal('100')},
            {'address': zsaplingaddr2, 'amount': Decimal('100')},
            {'address': zsaplingaddr3, 'amount': Decimal('100')},
            {'address': zorchardaddr1, 'amount': Decimal('100')},
            {'address': zorchardaddr2, 'amount': Decimal('100')},
            {'address': zorchardaddr3, 'amount': Decimal('100')},
            ], 1, Decimal('0.0001'))
        wait_and_assert_operationid_status(test.nodes[0], result)
        time.sleep(1)
        test.sync_all()
        time.sleep(1)
        generate_and_check(test.nodes[1], 2)
        time.sleep(1)
        test.sync_all()

        # =================================================================
        # ERROR CONDITION TESTING: Verify proper validation and error handling
        # =================================================================

        # Test 1: Invalid array parameter - should fail with proper error message
        print("Merging will fail because from arguments need to be in an array...")
        assert_mergetoaddress_exception(
            "JSON value is not an array as expected",
            lambda: test.nodes[0].z_mergetoaddress("notanarray", mysaplingaddr))

        # Test 2: Negative fee validation - should reject negative fees
        print("Merging will fail because fee is negative...")
        assert_mergetoaddress_exception(
            "Amount out of range",
            lambda: test.nodes[0].z_mergetoaddress([zsaplingaddr1, zsaplingaddr2, zsaplingaddr3], mysaplingaddr, -1))

        # Test 3: Fee exceeding MAX_MONEY - should reject excessively large fees
        print("Merging will fail because fee is larger than MAX_MONEY...")
        assert_mergetoaddress_exception(
            "Amount out of range",
            lambda: test.nodes[0].z_mergetoaddress([zsaplingaddr1, zsaplingaddr2, zsaplingaddr3], mysaplingaddr, Decimal('200000000.00000001')))

        # Merging will fail because fee is larger than `-maxtxfee`
        print("Merging will fail because fee is larger than `-maxtxfee`...")
        assert_mergetoaddress_exception(
            "Fee (9.00 ARRR) is greater than the maximum fee allowed by this instance (0.10 ARRR). Run pirated with `-maxtxfee` to adjust this limit.",
            lambda: test.nodes[0].z_mergetoaddress([zsaplingaddr1, zsaplingaddr2, zsaplingaddr3], mysaplingaddr, 9))

        # Merging will fail because transparent limit parameter must be at least 0
        print("Merging will fail because transparent limit parameter must be at least 0...")
        assert_mergetoaddress_exception(
            "Limit on maximum number of UTXOs cannot be negative",
            lambda: test.nodes[0].z_mergetoaddress([zsaplingaddr1, zsaplingaddr2, zsaplingaddr3], mysaplingaddr, Decimal('0.0001'), -1))

        # Merging will fail because transparent limit parameter is absurdly large
        print("Merging will fail because transparent limit parameter is absurdly large...")
        assert_mergetoaddress_exception(
            "JSON integer out of range",
            lambda: test.nodes[0].z_mergetoaddress([zsaplingaddr1, zsaplingaddr2, zsaplingaddr3], mysaplingaddr, Decimal('0.0001'), 99999999999999))

        # Merging will fail because shielded limit parameter must be at least 0
        print("Merging will fail because shielded limit parameter must be at least 0...")
        assert_mergetoaddress_exception(
            "Limit on maximum number of notes cannot be negative",
            lambda: test.nodes[0].z_mergetoaddress([zsaplingaddr1, zsaplingaddr2, zsaplingaddr3], mysaplingaddr, Decimal('0.0001'), 50, -1))

        # Merging will fail because shielded limit parameter is absurdly large
        print("Merging will fail because shielded limit parameter is absurdly large...")
        assert_mergetoaddress_exception(
            "JSON integer out of range",
            lambda: test.nodes[0].z_mergetoaddress([zsaplingaddr1, zsaplingaddr2, zsaplingaddr3], mysaplingaddr, Decimal('0.0001'), 50, 99999999999999))


        # Merge all sapling notes to a sapling z-addr, and set fee to 0.0001
        result = test.nodes[0].z_mergetoaddress([zsaplingaddr1, zsaplingaddr2, zsaplingaddr3], mysaplingaddr, Decimal('0.0001'))
        wait_and_assert_operationid_status(test.nodes[0], result['opid'])
        time.sleep(1)
        test.sync_all()
        time.sleep(1)
        generate_and_check(test.nodes[1], 2)
        time.sleep(1)
        test.sync_all()

        # Confirm balances of destination address and source addresses
        assert_equal(test.nodes[0].z_getbalance(mysaplingaddr), Decimal('724.1199723'))  # 1024.12017230 - 600 (sent) - 0.0001 (sendmany fee) + 300 (merged) - 0.0001 (merge fee) = 724.1199723
        assert_equal(test.nodes[0].z_getbalance(myorchardaddr), Decimal('12800'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr1), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr2), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr3), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr1), Decimal('100'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr2), Decimal('100'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr3), Decimal('100'))

        # Merge all orchard notes to a sapling z-addr, and set fee to 0.0001
        result = test.nodes[0].z_mergetoaddress([zorchardaddr1, zorchardaddr2, zorchardaddr3], mysaplingaddr, Decimal('0.0001'))
        wait_and_assert_operationid_status(test.nodes[0], result['opid'])
        time.sleep(1)
        test.sync_all()
        time.sleep(1)
        generate_and_check(test.nodes[1], 2)
        time.sleep(1)
        test.sync_all()

        # Confirm balances of destination address and source addresses
        assert_equal(test.nodes[0].z_getbalance(mysaplingaddr), Decimal('1024.1198723'))  # 724.1199723 + 300 (merged) - 0.0001 (merge fee) = 1024.1198723
        assert_equal(test.nodes[0].z_getbalance(myorchardaddr), Decimal('12800'))  # unchanged from previous step
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr1), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr2), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr3), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr1), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr2), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr3), Decimal('0'))


        # Send some funds from Orchard to Sapling & Orchard addresses
        print("Sending funds from Orchard to Sapling & Orchard addresses...")
        result = test.nodes[0].z_sendmany(myorchardaddr, [
            {'address': zsaplingaddr1, 'amount': Decimal('100')},
            {'address': zsaplingaddr2, 'amount': Decimal('100')},
            {'address': zsaplingaddr3, 'amount': Decimal('100')},
            {'address': zorchardaddr1, 'amount': Decimal('100')},
            {'address': zorchardaddr2, 'amount': Decimal('100')},
            {'address': zorchardaddr3, 'amount': Decimal('100')},
            ], 1, Decimal('0.0001'))
        wait_and_assert_operationid_status(test.nodes[0], result)
        time.sleep(1)
        test.sync_all()
        time.sleep(1)
        generate_and_check(test.nodes[1], 2)
        time.sleep(1)
        test.sync_all()

        # Confirm balances of destination address and source addresses
        assert_equal(test.nodes[0].z_getbalance(mysaplingaddr), Decimal('1024.1198723'))  # unchanged from previous step
        assert_equal(test.nodes[0].z_getbalance(myorchardaddr), Decimal('12199.9999'))  # 12800.0 - 600 (sent) - 0.0001 (sendmany fee) = 12199.9999
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr1), Decimal('100'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr2), Decimal('100'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr3), Decimal('100'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr1), Decimal('100'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr2), Decimal('100'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr3), Decimal('100'))

        # Merge all sapling and orchard notes to a sapling z-addr, and set fee to 0.0001
        result = test.nodes[0].z_mergetoaddress([zsaplingaddr1, zsaplingaddr2, zsaplingaddr3,zorchardaddr1, zorchardaddr2, zorchardaddr3], mysaplingaddr, Decimal('0.0001'))
        wait_and_assert_operationid_status(test.nodes[0], result['opid'])
        time.sleep(1)
        test.sync_all()
        time.sleep(1)
        generate_and_check(test.nodes[1], 2)
        time.sleep(1)
        test.sync_all()
        
        # Confirm balances of destination address and source addresses
        assert_equal(test.nodes[0].z_getbalance(mysaplingaddr), Decimal('1624.1197723'))  # 1024.1198723 + 600 (merged) - 0.0001 (merge fee) = 1624.1197723
        assert_equal(test.nodes[0].z_getbalance(myorchardaddr), Decimal('12199.9999'))  # unchanged from previous step
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr1), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr2), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr3), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr1), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr2), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr3), Decimal('0'))

        # Send some funds from Orchard to Sapling & Orchard addresses
        result = test.nodes[0].z_sendmany(myorchardaddr, [
            {'address': zsaplingaddr1, 'amount': Decimal('100')},
            {'address': zsaplingaddr2, 'amount': Decimal('100')},
            {'address': zsaplingaddr3, 'amount': Decimal('100')},
            {'address': zorchardaddr1, 'amount': Decimal('100')},
            {'address': zorchardaddr2, 'amount': Decimal('100')},
            {'address': zorchardaddr3, 'amount': Decimal('100')},
            ], 1, Decimal('0.0001'))
        wait_and_assert_operationid_status(test.nodes[0], result)
        time.sleep(1)
        test.sync_all()
        time.sleep(1)
        generate_and_check(test.nodes[1], 2)
        time.sleep(1)
        test.sync_all()
        
        # Verify balances before the final merge operation
        assert_equal(test.nodes[0].z_getbalance(mysaplingaddr), Decimal('1624.1197723'))  # unchanged from previous step
        assert_equal(test.nodes[0].z_getbalance(myorchardaddr), Decimal('11599.9998'))  # 12199.9999 - 600 (sent) - 0.0001 (sendmany fee) = 11599.9998
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr1), Decimal('100'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr2), Decimal('100'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr3), Decimal('100'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr1), Decimal('100'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr2), Decimal('100'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr3), Decimal('100'))
        
        # Merge all sapling and orchard notes to an orchard z-addr, and set fee to 0.0001
        result = test.nodes[0].z_mergetoaddress([zsaplingaddr1, zsaplingaddr2, zsaplingaddr3, zorchardaddr1, zorchardaddr2, zorchardaddr3], myorchardaddr, Decimal('0.0001'))
        wait_and_assert_operationid_status(test.nodes[0], result['opid'])
        time.sleep(1)
        test.sync_all()
        time.sleep(1)
        generate_and_check(test.nodes[1], 2)
        time.sleep(1)
        test.sync_all()
        
        # Confirm final balances after the merge operation
        assert_equal(test.nodes[0].z_getbalance(mysaplingaddr), Decimal('1624.1197723'))  # unchanged from previous step
        assert_equal(test.nodes[0].z_getbalance(myorchardaddr), Decimal('12199.9997'))  # 11599.9998 + 600 (merged) - 0.0001 (merge fee) = 11599.9997
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr1), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr2), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zsaplingaddr3), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr1), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr2), Decimal('0'))
        assert_equal(test.nodes[0].z_getbalance(zorchardaddr3), Decimal('0'))

        # =================================================================
        # TEST COMPLETION: All merge operations successful
        # =================================================================
        
        print("✓ All z_mergetoaddress tests completed successfully!")
