#!/usr/bin/env python3
# Copyright (c) 2016 The Zcash developers
# Copyright (c) 2022-2025 Pirate developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

"""
Wallet Shielding Coinbase Test

This test verifies that coinbase UTXOs must be shielded in regtest mode when
-regtestshieldcoinbase is enabled. It tests various scenarios including:
- Proper shielding to Sapling and Orchard addresses
- Error handling for transparent change during coinbase shielding
- Watch-only address validation
- Zero-confirmation note verification
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import COIN
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, wait_and_assert_operationid_status, \
    wait_and_assert_operationid_status_result, get_coinbase_address, \
    LEGACY_DEFAULT_FEE
from decimal import Decimal

def check_value_pool(node, name, total):
    """
    Verify that a specific value pool has the expected total value.
    
    Args:
        node: The node to query for blockchain info
        name (str): The name of the value pool ('sapling' or 'orchard')
        total (Decimal): The expected total value in the pool
        
    Raises:
        AssertionError: If the pool is not found or values don't match
    """
    value_pools = node.getblockchaininfo()['valuePools']
    found = False
    for pool in value_pools:
        if pool['id'] == name:
            found = True
            assert_equal(pool['monitored'], True)
            assert_equal(pool['chainValue'], total)
            assert_equal(pool['chainValueZat'], total * COIN)
    assert(found)

class WalletShieldingCoinbaseTest(BitcoinTestFramework):
    """
    Test class for verifying coinbase shielding behavior in regtest mode.
    
    This test ensures that when -regtestshieldcoinbase is enabled, coinbase
    UTXOs are properly shielded and transparent change is not allowed during
    the shielding process.
    """

    def setup_chain(self):
        """Initialize a clean test directory with 4 nodes."""
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)

    def setup_network(self, split=False):
        """
        Start nodes with -regtestshieldcoinbase to set fCoinbaseMustBeShielded to true.
        
        Args:
            split (bool): Whether to split the network (unused in this test)
        """
        self.nodes = start_nodes(4, self.options.tmpdir, extra_args=[[
            '-minrelaytxfee=0',
            '-regtestshieldcoinbase',
            '-debug=zrpcunsafe',
            '-allowdeprecated=getnewaddress',
            '-allowdeprecated=legacy_privacy',
            '-allowdeprecated=z_getnewaddress',
            '-allowdeprecated=z_getbalance',
            '-allowdeprecated=z_gettotalbalance',
        ]] * 4)
        
        # Connect all nodes in a mesh topology
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 0, 2)
        connect_nodes_bi(self.nodes, 0, 3)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        """
        Main test execution method.
        
        Tests various aspects of coinbase shielding including:
        - Initial balance verification
        - Watch-only address handling
        - Error conditions for transparent change
        - Successful shielding to both Sapling and Orchard addresses
        - Zero-confirmation note verification
        """
        print("Mining blocks...")

        # =================================================================
        # INITIAL SETUP: Generate blocks and verify initial state
        # =================================================================
        
        self.nodes[1].generate(5)
        self.sync_all()

        # Verify initial balance states
        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(walletinfo['immature_balance'], Decimal('0'))
        assert_equal(walletinfo['balance'], Decimal('1024.12017230'))
        self.sync_all()

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(walletinfo['immature_balance'], Decimal('0'))
        assert_equal(walletinfo['balance'], Decimal('0'))
        self.sync_all()

        # Generate mature coinbase outputs on node 0
        self.nodes[0].generate(201)
        self.sync_all()

        # =================================================================
        # BALANCE VERIFICATION: Ensure expected balance distribution
        # =================================================================
        
        assert_equal(self.nodes[0].getbalance(), Decimal('51456'))
        assert_equal(self.nodes[1].getbalance(), Decimal('1024.12017230'))
        assert_equal(self.nodes[2].getbalance(), Decimal('0'))
        assert_equal(self.nodes[3].getbalance(), Decimal('0'))

        # Verify that both Sapling and Orchard value pools are empty initially
        for node_idx in range(4):
            check_value_pool(self.nodes[node_idx], 'sapling', 0)
            check_value_pool(self.nodes[node_idx], 'orchard', 0)

        # =================================================================
        # ADDRESS SETUP: Create addresses for testing
        # =================================================================
        
        # Prepare addresses for testing taddr->zaddr transactions
        mytaddr = get_coinbase_address(self.nodes[0])
        mysaplingzaddr = self.nodes[0].z_getnewaddress('sapling')
        myorchardzaddr = self.nodes[0].z_getnewaddress('orchard')

        # =================================================================
        # WATCH-ONLY ADDRESS TESTING: Verify UTXOs are not selected
        # =================================================================
        
        # Test that watch-only address UTXOs are not selected for Sapling
        self.nodes[3].importaddress(mytaddr)
        recipients = [{"address": mysaplingzaddr, "amount": Decimal('1')}]

        result = self.nodes[3].z_sendmany(mytaddr, recipients)
        wait_and_assert_operationid_status(self.nodes[3], result, "failed", 
            "Insufficient funds, no UTXOs found for taddr from address.")

        # Test that watch-only address UTXOs are not selected for Orchard
        recipients = [{"address": myorchardzaddr, "amount": Decimal('1')}]

        result = self.nodes[3].z_sendmany(mytaddr, recipients)
        wait_and_assert_operationid_status(self.nodes[3], result, "failed", 
            "Insufficient funds, no UTXOs found for taddr from address.")

        # =================================================================
        # ERROR CONDITION TESTING: Transparent change not allowed
        # =================================================================

        # Test Sapling: This send will fail because consensus does not allow 
        # transparent change when shielding a coinbase UTXO
        recipients = []
        recipients.append({"address": mysaplingzaddr, "amount": Decimal('1.23456789')})

        myopid = self.nodes[0].z_sendmany(mytaddr, recipients, 10, LEGACY_DEFAULT_FEE)
        error_result = wait_and_assert_operationid_status_result(
                self.nodes[0],
                myopid, "failed",
                "16: bad-txns-acprivacy-chain",
                10)

        # Verify that the returned status object contains operation parameters
        assert_equal(error_result["method"], "z_sendmany")
        params = error_result["params"]
        assert_equal(params["fee"], LEGACY_DEFAULT_FEE)  # default
        assert_equal(params["minconf"], Decimal('10'))  # default
        assert_equal(params["fromaddress"], mytaddr)
        assert_equal(params["amounts"][0]["address"], mysaplingzaddr)
        assert_equal(params["amounts"][0]["amount"], Decimal('1.23456789'))

        # Test Orchard: This send will fail because consensus does not allow 
        # transparent change when shielding a coinbase UTXO
        recipients = []
        recipients.append({"address": myorchardzaddr, "amount": Decimal('1.23456789')})

        myopid = self.nodes[0].z_sendmany(mytaddr, recipients, 10, LEGACY_DEFAULT_FEE)
        error_result = wait_and_assert_operationid_status_result(
                self.nodes[0],
                myopid, "failed",
                "16: bad-txns-acprivacy-chain",
                10)

        # Verify that the returned status object contains operation parameters
        assert_equal(error_result["method"], "z_sendmany")
        params = error_result["params"]
        assert_equal(params["fee"], LEGACY_DEFAULT_FEE)  # default
        assert_equal(params["minconf"], Decimal('10'))  # default
        assert_equal(params["fromaddress"], mytaddr)
        assert_equal(params["amounts"][0]["address"], myorchardzaddr)
        assert_equal(params["amounts"][0]["amount"], Decimal('1.23456789'))

        # Test Multioutput: This send will fail because consensus does not allow 
        # transparent change when shielding a coinbase UTXO
        recipients = []
        recipients.append({"address": mysaplingzaddr, "amount": Decimal('1.23456789')})
        recipients.append({"address": myorchardzaddr, "amount": Decimal('1.23456789')})

        myopid = self.nodes[0].z_sendmany(mytaddr, recipients, 10, LEGACY_DEFAULT_FEE)
        error_result = wait_and_assert_operationid_status_result(
                self.nodes[0],
                myopid, "failed",
                "16: bad-txns-acprivacy-chain",
                10)

        # Verify that the returned status object contains operation parameters
        assert_equal(error_result["method"], "z_sendmany")
        params = error_result["params"]
        assert_equal(params["fee"], LEGACY_DEFAULT_FEE)  # default
        assert_equal(params["minconf"], Decimal('10'))  # default
        assert_equal(params["fromaddress"], mytaddr)
        assert_equal(params["amounts"][0]["address"], mysaplingzaddr)
        assert_equal(params["amounts"][0]["amount"], Decimal('1.23456789'))
        assert_equal(params["amounts"][1]["address"], myorchardzaddr)
        assert_equal(params["amounts"][1]["amount"], Decimal('1.23456789'))

        # =================================================================
        # VIEWING KEY SETUP: Prepare Node 3 for balance verification
        # =================================================================
        
        # Add viewing keys to Node 3 for verification purposes
        mysaplingviewingkey = self.nodes[0].z_exportviewingkey(mysaplingzaddr)
        self.nodes[3].z_importviewingkey(mysaplingviewingkey, "no")

        myorchardviewingkey = self.nodes[0].z_exportviewingkey(myorchardzaddr)
        self.nodes[3].z_importviewingkey(myorchardviewingkey, "no")

        # =================================================================
        # SUCCESSFUL SHIELDING TESTS: Valid coinbase to shielded transfers
        # =================================================================

        # Shield coinbase to Sapling: This will succeed with exact amount and no change
        # We send one coinbase UTXOs totalling 256 less a default fee, with no change
        # (This tx fits within the block unpaid action limit.)
        mytaddr = get_coinbase_address(self.nodes[0])
        shieldvalue = Decimal('256.0') - LEGACY_DEFAULT_FEE
        recipients = []
        recipients.append({"address": mysaplingzaddr, "amount": shieldvalue})
        myopid = self.nodes[0].z_sendmany(mytaddr, recipients, 10, LEGACY_DEFAULT_FEE)
        mytxid = wait_and_assert_operationid_status(self.nodes[0], myopid)
        self.sync_all()

        # Verify that z_listunspent can return a note that has zero confirmations
        results = self.nodes[0].z_listunspent()
        assert(len(results) == 0)
        results = self.nodes[0].z_listunspent(0)  # set minconf to zero
        assert(len(results) == 1)
        assert_equal(results[0]["address"], mysaplingzaddr)
        assert_equal(results[0]["amount"], shieldvalue)
        assert_equal(results[0]["confirmations"], 0)
        assert_equal(results[0]["spendable"], True)

        # Mine the transaction to confirm it
        self.nodes[1].generate(1)
        self.sync_all()

        # Shield coinbase to Orchard: This will succeed with exact amount and no change
        # We send two coinbase UTXOs totalling 256.0 less a default fee, with no change
        # (This tx fits within the block unpaid action limit.)
        mytaddr = get_coinbase_address(self.nodes[0])
        shieldvalue = Decimal('256.0') - LEGACY_DEFAULT_FEE
        recipients = []
        recipients.append({"address": myorchardzaddr, "amount": shieldvalue})
        myopid = self.nodes[0].z_sendmany(mytaddr, recipients, 10, LEGACY_DEFAULT_FEE)
        mytxid = wait_and_assert_operationid_status(self.nodes[0], myopid)
        self.sync_all()

        # Mine the transaction to confirm it
        self.nodes[1].generate(1)
        self.sync_all()

        # =================================================================
        # MULTI-OUTPUT SHIELDING: Send to both Sapling and Orchard
        # =================================================================
        
        # Shield coinbase to both Sapling and Orchard addresses simultaneously
        # We send coinbase UTXOs with half the fee for each output
        mytaddr = get_coinbase_address(self.nodes[0])
        shieldvalue = Decimal('256.0')/2 - LEGACY_DEFAULT_FEE/2
        recipients = []
        recipients.append({"address": mysaplingzaddr, "amount": shieldvalue})
        recipients.append({"address": myorchardzaddr, "amount": shieldvalue})
        myopid = self.nodes[0].z_sendmany(mytaddr, recipients, 10, LEGACY_DEFAULT_FEE)
        mytxid = wait_and_assert_operationid_status(self.nodes[0], myopid)
        self.sync_all()

        # Mine the final transaction
        self.nodes[1].generate(1)
        self.sync_all()

        print("✓ All coinbase shielding tests completed successfully!")

if __name__ == '__main__':
    WalletShieldingCoinbaseTest().main()
