#!/usr/bin/env python3
#Copyright (c) 2022 The Zcash developers
#Copyright (c) 2022-2025 Pirate developers
#Distributed under the MIT software license, see the accompanying
#file COPYING or https://www.opensource.org/licenses/mit-license.php

"""
Final Root Test

Tests the 'hashFinalSaplingRoot' and 'hashFinalOrchardRoot' block header fields to ensure they update correctly
when transactions with outputs (commitments) are mined into blocks.
"""

from decimal import Decimal
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    NU5_BRANCH_ID,
    assert_equal,
    connect_nodes_bi,
    get_coinbase_address,
    nuparams,
    start_nodes,
    wait_and_assert_operationid_status,
)


# Constants for empty commitment tree roots
SPROUT_TREE_EMPTY_ROOT = "59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7"
SAPLING_TREE_EMPTY_ROOT = "3e49b5f954aa9d3545bc6c37744661eea48d7c34e3000d82b7f0010c30f4c2fb"
#ORCHARD_TREE_EMPTY_ROOT = "2fd8e51a03d9bbe2dd809831b1497aeb68a6e37ddf707ced4aa2d8dff13529ae"
NULL_FIELD = "0000000000000000000000000000000000000000000000000000000000000000"

#SPROUT_TREE_EMPTY_ROOT = "59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7"
#SAPLING_TREE_EMPTY_ROOT = "3e49b5f954aa9d3545bc6c37744661eea48d7c34e3000d82b7f0010c30f4c2fb"
ORCHARD_TREE_EMPTY_ROOT = "ae2935f1dfd8a24aed7c70df7de3a668eb7a49b1319880dde2bbd9031ae5d82f"
#NULL_FIELD = "0000000000000000000000000000000000000000000000000000000000000000"


class FinalOrchardRootTest(BitcoinTestFramework):
    """
    Test final Orchard root updates in block headers.
    
    Verifies that the 'hashFinalOrchardRoot' field (returned as 'finalorchardroot' in RPC)
    is correctly updated when Orchard transactions with outputs are mined into blocks.
    """

    def __init__(self):
        """Initialize test with 2 nodes and clean cache behavior."""
        super().__init__()
        self.num_nodes = 2
        self.cache_behavior = 'clean'

    def setup_network(self, split=False):
        """
        Set up test network with required node configurations.
        
        Args:
            split (bool): Whether to split the network (unused in this test)
        """
        node_args = [
            '-minrelaytxfee=0',
            '-txindex',  # Avoid JSONRPC error: No information available about transaction
            '-reindex',  # Required due to enabling -txindex
            '-debug',
            '-allowdeprecated=getnewaddress',
            '-allowdeprecated=z_getnewaddress',
            '-allowdeprecated=z_getbalance',
            '-experimentalfeatures',
            '-lightwalletd'
        ]
        
        self.nodes = start_nodes(
            self.num_nodes, 
            self.options.tmpdir, 
            extra_args=[node_args] * self.num_nodes
        )
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        """
        Main test execution covering multiple phases:
        1. Initial blockchain state verification
        2. Sapling transaction testing (pre-Orchard)
        3. Orchard activation and testing
        4. Combined Sapling and Orchard transaction testing
        """
        self._setup_initial_blocks()
        self._create_test_addresses()
        self._verify_initial_tree_states()
        self._test_sapling_transactions_pre_orchard()
        self._activate_orchard_and_test()
        self._test_combined_transactions()

    def _setup_initial_blocks(self):
        """Generate initial blocks if needed to create coinbase outputs."""
        blockcount = self.nodes[0].getblockcount()
        
        if blockcount < 110:
            # Ensure we have addresses to generate blocks to
            self.nodes[0].keypoolrefill(1000)
            self.nodes[1].keypoolrefill(1000)
            self.nodes[0].generate(110 - blockcount)
            self.sync_all()
            blockcount = self.nodes[0].getblockcount()
            assert_equal(blockcount, 110)

    def _create_test_addresses(self):
        """Create transparent, Sapling, and Orchard addresses for testing."""
        self.mytaddr = get_coinbase_address(self.nodes[0])

    def _verify_initial_tree_states(self):
        """Verify tree states for blocks before Sapling and Orchard activation."""
        blockcount = self.nodes[0].getblockcount()
        
        for height in range(0, blockcount + 1):
            blk = self.nodes[0].getblock(str(height))
            treestate = self.nodes[0].z_gettreestate(str(height))

            assert_equal(treestate["height"], height)
            assert_equal(treestate["hash"], self.nodes[0].getblockhash(height))

            # Phase 1: Neither Sapling nor Orchard are active (height < 100)
            if height < 100:
                self._verify_pre_sapling_state(blk, treestate)

            # Phase 2: Sapling active, Orchard not active (100 <= height < 200)
            elif height < 200:
                self._verify_sapling_only_state(blk, treestate)

            self.treestate = treestate

        self.sync_all()

    def _verify_pre_sapling_state(self, blk, treestate):
        """Verify tree state when neither Sapling nor Orchard are active."""
        # Orchard should not exist in block header
        assert "finalorchardroot" not in blk
        if "trees" in blk:
            assert "orchard" not in blk["trees"]

        # Sprout tree should be empty
        assert "skipHash" not in treestate["sprout"]
        assert_equal(treestate["sprout"]["commitments"]["finalRoot"], SPROUT_TREE_EMPTY_ROOT)
        assert_equal(treestate["sprout"]["commitments"]["finalState"], "000000")

        # Sapling should be null/inactive
        assert "skipHash" not in treestate["sapling"]
        assert_equal(treestate["sapling"]["commitments"]["finalRoot"], NULL_FIELD)
        assert "finalState" not in treestate["sapling"]

        # Orchard should be null/inactive
        assert "skipHash" not in treestate["orchard"]
        assert_equal(treestate["orchard"]["commitments"]["finalRoot"], NULL_FIELD)
        assert "finalState" not in treestate["orchard"]

    def _verify_sapling_only_state(self, blk, treestate):
        """Verify tree state when Sapling is active but Orchard is not."""
        # Orchard should not exist in block header
        assert "finalorchardroot" not in blk
        if "trees" in blk:
            assert "orchard" not in blk["trees"]

        # Sprout tree should remain empty
        assert_equal(treestate["sprout"]["commitments"]["finalRoot"], SPROUT_TREE_EMPTY_ROOT)
        assert_equal(treestate["sprout"]["commitments"]["finalState"], "000000")

        # Sapling should be empty but active
        assert_equal(treestate["sapling"]["commitments"]["finalRoot"], SAPLING_TREE_EMPTY_ROOT)
        assert_equal(treestate["sapling"]["commitments"]["finalState"], "000000")
        assert "skipHash" not in treestate["sapling"]

        # Orchard should still be null/inactive
        assert "skipHash" not in treestate["orchard"]
        assert_equal(treestate["orchard"]["commitments"]["finalRoot"], NULL_FIELD)
        assert "finalState" not in treestate["orchard"]

    def _test_sapling_transactions_pre_orchard(self):
        """Test Sapling transactions before Orchard activation."""
        # Create Sapling address
        self.mysaplingaddr = self.nodes[0].z_getnewaddress('sapling')

        #update taddr
        self.mytaddr = get_coinbase_address(self.nodes[0])

        # Send funds to Sapling address
        result = self.nodes[0].z_shieldcoinbase(self.mytaddr, self.mysaplingaddr, 0, 1)
        mytxid = wait_and_assert_operationid_status(self.nodes[0], result['opid'])

        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        # Verify transaction was mined and Sapling tree was updated
        current_block_height = self.nodes[0].getblockcount()
        blk = self.nodes[0].getblock(str(current_block_height))

        # Verify Sapling root changed from empty state
        new_sapling_root = blk["finalsaplingroot"]
        assert new_sapling_root != SAPLING_TREE_EMPTY_ROOT
        assert new_sapling_root != NULL_FIELD
        self.sapling_root = new_sapling_root

        # Verify transaction has Sapling output
        tx_result = self.nodes[0].getrawtransaction(mytxid, 1)
        assert_equal(len(tx_result["vShieldedOutput"]), 1)

        # Verify tree size in block
        if "trees" in blk and "sapling" in blk["trees"]:
            assert_equal(blk["trees"]["sapling"]["size"], 1)

        # Orchard should still not exist
        assert "finalorchardroot" not in blk
        if "trees" in blk:
            assert "orchard" not in blk["trees"]

        # Verify tree state changes
        self._verify_sapling_tree_update()

    def _verify_sapling_tree_update(self):
        """Verify Sapling tree state was updated correctly."""
        current_block_height = self.nodes[0].getblockcount()
        new_treestate = self.nodes[0].z_gettreestate(str(current_block_height))

        # Verify Sapling tree changes
        if "sapling" in new_treestate and "sapling" in self.treestate:
            assert_equal(new_treestate["sapling"]["commitments"]["finalRoot"], self.sapling_root)
            assert new_treestate["sapling"]["commitments"]["finalRoot"] != self.treestate["sapling"]["commitments"]["finalRoot"]
            assert new_treestate["sapling"]["commitments"]["finalState"] != self.treestate["sapling"]["commitments"]["finalState"]
            assert_equal(len(new_treestate["sapling"]["commitments"]["finalRoot"]), 64)
            assert_equal(len(new_treestate["sapling"]["commitments"]["finalState"]), 132)

        # Verify Orchard tree unchanged (still null)
        if "orchard" in new_treestate and "orchard" in self.treestate:
            assert_equal(new_treestate["orchard"]["commitments"]["finalRoot"], NULL_FIELD)
            assert new_treestate["orchard"]["commitments"]["finalRoot"] == self.treestate["orchard"]["commitments"]["finalRoot"]
            assert_equal(len(new_treestate["orchard"]["commitments"]["finalRoot"]), 64)

        # Verify Sprout tree unchanged
        assert_equal(new_treestate["sprout"], self.treestate["sprout"])
        self.treestate = new_treestate

    def _activate_orchard_and_test(self):
        """Activate Orchard and test basic functionality."""
        # Generate blocks to activate Orchard
        starting_height = self.nodes[0].getblockcount()
        self.nodes[0].generate(110)
        self.sync_all()
        blockcount = self.nodes[0].getblockcount()
        print(f"Chain Height after Orchard activation: {blockcount}")

        # Verify Orchard activation in generated blocks
        self._verify_orchard_activation_blocks(starting_height, blockcount)

        # Test first Orchard transaction
        self._test_first_orchard_transaction()

    def _verify_orchard_activation_blocks(self, starting_height, blockcount):
        """Verify tree states during and after Orchard activation."""
        for height in range(starting_height, blockcount + 1):
            blk = self.nodes[0].getblock(str(height))
            new_treestate = self.nodes[0].z_gettreestate(str(height))

            print(f"\nVerifying block height {height}")

            assert_equal(new_treestate["height"], height)
            assert_equal(new_treestate["hash"], self.nodes[0].getblockhash(height))

            # Pre-activation blocks (height < 200)
            if height < 200:
                self._verify_pre_orchard_activation(new_treestate)
            else:
                # Post-activation blocks (height >= 200)
                self._verify_post_orchard_activation(new_treestate)

            # Sprout should remain unchanged
            assert_equal(new_treestate["sprout"], self.treestate["sprout"])
            self.treestate = new_treestate

    def _verify_pre_orchard_activation(self, treestate):
        """Verify tree state before Orchard activation."""
        # Sprout tree remains empty
        assert_equal(treestate["sprout"]["commitments"]["finalRoot"], SPROUT_TREE_EMPTY_ROOT)
        assert_equal(treestate["sprout"]["commitments"]["finalState"], "000000")

        # Sapling tree unchanged from previous state
        assert treestate["sapling"]["commitments"]["finalRoot"] == self.treestate["sapling"]["commitments"]["finalRoot"]
        assert treestate["sapling"]["commitments"]["finalState"] == self.treestate["sapling"]["commitments"]["finalState"]

        # Orchard not yet active
        assert "skipHash" not in treestate["orchard"]
        assert_equal(treestate["orchard"]["commitments"]["finalRoot"], NULL_FIELD)
        assert "finalState" not in treestate["orchard"]

    def _verify_post_orchard_activation(self, treestate):
        """Verify tree state after Orchard activation."""
        # Sprout tree remains empty
        assert_equal(treestate["sprout"]["commitments"]["finalRoot"], SPROUT_TREE_EMPTY_ROOT)
        assert_equal(treestate["sprout"]["commitments"]["finalState"], "000000")

        # Sapling tree unchanged from previous state
        assert treestate["sapling"]["commitments"]["finalRoot"] == self.treestate["sapling"]["commitments"]["finalRoot"]
        assert treestate["sapling"]["commitments"]["finalState"] == self.treestate["sapling"]["commitments"]["finalState"]

        # Orchard now active with empty tree
        assert_equal(treestate["orchard"]["commitments"]["finalRoot"], ORCHARD_TREE_EMPTY_ROOT)
        assert_equal(treestate["orchard"]["commitments"]["finalState"], "000000")
        assert "skipHash" not in treestate["orchard"]

    def _test_first_orchard_transaction(self):
        """Test the first Orchard transaction and verify tree updates."""
        # Create Orchard address
        self.myorchardaddr = self.nodes[0].z_getnewaddress('orchard')

        #update taddr
        self.mytaddr = get_coinbase_address(self.nodes[0])

        # Send funds to Orchard address
        result = self.nodes[0].z_shieldcoinbase(self.mytaddr, self.myorchardaddr, 0, 1)
        mytxid = wait_and_assert_operationid_status(self.nodes[0], result['opid'])

        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        # Verify Orchard root changed
        current_block_height = self.nodes[0].getblockcount()
        blk = self.nodes[0].getblock(str(current_block_height))
        new_orchard_root = blk["finalorchardroot"]
        assert new_orchard_root != ORCHARD_TREE_EMPTY_ROOT
        assert new_orchard_root != NULL_FIELD
        self.orchard_root = new_orchard_root

        # Verify transaction has Orchard actions
        tx_result = self.nodes[0].getrawtransaction(mytxid, 1)
        assert_equal(len(tx_result["orchard"]["actions"]), 2)

        # Verify tree sizes in block
        if "trees" in blk and "sapling" in blk["trees"]:
            assert_equal(blk["trees"]["sapling"]["size"], 1)
        if "trees" in blk and "orchard" in blk["trees"]:
            assert_equal(blk["trees"]["orchard"]["size"], 2)

        # Verify tree state changes
        self._verify_first_orchard_tree_update()

    def _verify_first_orchard_tree_update(self):
        """Verify tree state after first Orchard transaction."""
        current_block_height = self.nodes[0].getblockcount()
        new_treestate = self.nodes[0].z_gettreestate(str(current_block_height))

        # Verify Sapling tree unchanged
        if "sapling" in new_treestate and "sapling" in self.treestate:
            assert_equal(new_treestate["sapling"]["commitments"]["finalRoot"], self.sapling_root)
            assert new_treestate["sapling"]["commitments"]["finalRoot"] == self.treestate["sapling"]["commitments"]["finalRoot"]
            assert new_treestate["sapling"]["commitments"]["finalState"] == self.treestate["sapling"]["commitments"]["finalState"]
            assert_equal(len(new_treestate["sapling"]["commitments"]["finalRoot"]), 64)
            assert_equal(len(new_treestate["sapling"]["commitments"]["finalState"]), 132)

        # Verify Orchard tree changed
        if "orchard" in new_treestate and "orchard" in self.treestate:
            assert_equal(new_treestate["orchard"]["commitments"]["finalRoot"], self.orchard_root)
            assert new_treestate["orchard"]["commitments"]["finalRoot"] != self.treestate["orchard"]["commitments"]["finalRoot"]
            assert new_treestate["orchard"]["commitments"]["finalState"] != self.treestate["orchard"]["commitments"]["finalState"]
            assert_equal(len(new_treestate["orchard"]["commitments"]["finalRoot"]), 64)
            assert_equal(len(new_treestate["orchard"]["commitments"]["finalState"]), 196)

        # Verify Sprout tree unchanged
        assert_equal(new_treestate["sprout"], self.treestate["sprout"])
        self.treestate = new_treestate

    def _test_combined_transactions(self):
        """Test alternating Sapling and Orchard transactions."""
        # Test sequence: Sapling -> Orchard -> Sapling
        self._test_second_sapling_transaction()
        self._test_second_orchard_transaction()
        self._test_third_sapling_transaction()

    def _test_second_sapling_transaction(self):
        """Test second Sapling transaction after Orchard activation."""

        #update taddr
        self.mytaddr = get_coinbase_address(self.nodes[0])

        # Send funds to Sapling address
        result = self.nodes[0].z_shieldcoinbase(self.mytaddr, self.mysaplingaddr, 0, 1)
        mytxid = wait_and_assert_operationid_status(self.nodes[0], result['opid'])

        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        # Verify Orchard root unchanged, Sapling root changed
        current_block_height = self.nodes[0].getblockcount()
        blk = self.nodes[0].getblock(str(current_block_height))
        
        new_orchard_root = blk["finalorchardroot"]
        assert new_orchard_root == self.orchard_root
        assert new_orchard_root != ORCHARD_TREE_EMPTY_ROOT
        assert new_orchard_root != NULL_FIELD

        new_sapling_root = blk["finalsaplingroot"]
        assert new_sapling_root != self.sapling_root
        assert new_sapling_root != SAPLING_TREE_EMPTY_ROOT
        assert new_sapling_root != NULL_FIELD
        self.sapling_root = new_sapling_root

        # Verify transaction has Sapling output
        tx_result = self.nodes[0].getrawtransaction(mytxid, 1)
        assert_equal(len(tx_result["vShieldedOutput"]), 1)

        # Verify tree sizes
        if "trees" in blk and "orchard" in blk["trees"]:
            assert_equal(blk["trees"]["orchard"]["size"], 2)
        if "trees" in blk and "sapling" in blk["trees"]:
            assert_equal(blk["trees"]["sapling"]["size"], 2)

        self._verify_sapling_update_orchard_unchanged()

    def _verify_sapling_update_orchard_unchanged(self):
        """Verify Sapling tree updated while Orchard remained unchanged."""
        current_block_height = self.nodes[0].getblockcount()
        new_treestate = self.nodes[0].z_gettreestate(str(current_block_height))

        # Verify Sapling tree changed
        if "sapling" in new_treestate and "sapling" in self.treestate:
            assert_equal(new_treestate["sapling"]["commitments"]["finalRoot"], self.sapling_root)
            assert new_treestate["sapling"]["commitments"]["finalRoot"] != self.treestate["sapling"]["commitments"]["finalRoot"]
            assert new_treestate["sapling"]["commitments"]["finalState"] != self.treestate["sapling"]["commitments"]["finalState"]
            assert_equal(len(new_treestate["sapling"]["commitments"]["finalRoot"]), 64)
            assert_equal(len(new_treestate["sapling"]["commitments"]["finalState"]), 196)

        # Verify Orchard tree unchanged
        if "orchard" in new_treestate and "orchard" in self.treestate:
            assert_equal(new_treestate["orchard"]["commitments"]["finalRoot"], self.orchard_root)
            assert new_treestate["orchard"]["commitments"]["finalRoot"] == self.treestate["orchard"]["commitments"]["finalRoot"]
            assert new_treestate["orchard"]["commitments"]["finalState"] == self.treestate["orchard"]["commitments"]["finalState"]
            assert_equal(len(new_treestate["orchard"]["commitments"]["finalRoot"]), 64)
            assert_equal(len(new_treestate["orchard"]["commitments"]["finalState"]), 196)

        # Verify Sprout tree unchanged
        assert_equal(new_treestate["sprout"], self.treestate["sprout"])
        self.treestate = new_treestate

    def _test_second_orchard_transaction(self):
        """Test second Orchard transaction."""

        #update taddr
        self.mytaddr = get_coinbase_address(self.nodes[0])

        result = self.nodes[0].z_shieldcoinbase(self.mytaddr, self.myorchardaddr, 0, 1)
        mytxid = wait_and_assert_operationid_status(self.nodes[0], result['opid'])

        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        # Verify Orchard root changed, Sapling root unchanged
        current_block_height = self.nodes[0].getblockcount()
        blk = self.nodes[0].getblock(str(current_block_height))
        
        new_orchard_root = blk["finalorchardroot"]
        assert new_orchard_root != self.orchard_root
        assert new_orchard_root != ORCHARD_TREE_EMPTY_ROOT
        assert new_orchard_root != NULL_FIELD
        self.orchard_root = new_orchard_root

        new_sapling_root = blk["finalsaplingroot"]
        assert new_sapling_root == self.sapling_root
        assert new_sapling_root != SAPLING_TREE_EMPTY_ROOT
        assert new_sapling_root != NULL_FIELD

        # Verify transaction has Orchard actions
        tx_result = self.nodes[0].getrawtransaction(mytxid, 1)
        assert_equal(len(tx_result["orchard"]["actions"]), 2)

        # Verify tree sizes
        if "trees" in blk and "orchard" in blk["trees"]:
            assert_equal(blk["trees"]["orchard"]["size"], 4)
        if "trees" in blk and "sapling" in blk["trees"]:
            assert_equal(blk["trees"]["sapling"]["size"], 2)

        self._verify_orchard_update_sapling_unchanged()

    def _verify_orchard_update_sapling_unchanged(self):
        """Verify Orchard tree updated while Sapling remained unchanged."""
        current_block_height = self.nodes[0].getblockcount()
        new_treestate = self.nodes[0].z_gettreestate(str(current_block_height))

        # Verify Sapling tree unchanged
        if "sapling" in new_treestate and "sapling" in self.treestate:
            assert_equal(new_treestate["sapling"]["commitments"]["finalRoot"], self.sapling_root)
            assert new_treestate["sapling"]["commitments"]["finalRoot"] == self.treestate["sapling"]["commitments"]["finalRoot"]
            assert new_treestate["sapling"]["commitments"]["finalState"] == self.treestate["sapling"]["commitments"]["finalState"]
            assert_equal(len(new_treestate["sapling"]["commitments"]["finalRoot"]), 64)
            assert_equal(len(new_treestate["sapling"]["commitments"]["finalState"]), 196)

        # Verify Orchard tree changed
        if "orchard" in new_treestate and "orchard" in self.treestate:
            assert_equal(new_treestate["orchard"]["commitments"]["finalRoot"], self.orchard_root)
            assert new_treestate["orchard"]["commitments"]["finalRoot"] != self.treestate["orchard"]["commitments"]["finalRoot"]
            assert new_treestate["orchard"]["commitments"]["finalState"] != self.treestate["orchard"]["commitments"]["finalState"]
            assert_equal(len(new_treestate["orchard"]["commitments"]["finalRoot"]), 64)
            assert_equal(len(new_treestate["orchard"]["commitments"]["finalState"]), 260)

        # Verify Sprout tree unchanged
        assert_equal(new_treestate["sprout"], self.treestate["sprout"])
        self.treestate = new_treestate

    def _test_third_sapling_transaction(self):
        """Test third Sapling transaction."""

        #update taddr
        self.mytaddr = get_coinbase_address(self.nodes[0])
        
        result = self.nodes[0].z_shieldcoinbase(self.mytaddr, self.mysaplingaddr, 0, 1)
        mytxid = wait_and_assert_operationid_status(self.nodes[0], result['opid'])

        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        # Verify Orchard root unchanged, Sapling root changed
        current_block_height = self.nodes[0].getblockcount()
        blk = self.nodes[0].getblock(str(current_block_height))
        
        new_orchard_root = blk["finalorchardroot"]
        assert new_orchard_root == self.orchard_root
        assert new_orchard_root != ORCHARD_TREE_EMPTY_ROOT
        assert new_orchard_root != NULL_FIELD

        new_sapling_root = blk["finalsaplingroot"]
        assert new_sapling_root != self.sapling_root
        assert new_sapling_root != SAPLING_TREE_EMPTY_ROOT
        assert new_sapling_root != NULL_FIELD
        self.sapling_root = new_sapling_root

        # Verify transaction has Sapling output
        tx_result = self.nodes[0].getrawtransaction(mytxid, 1)
        assert_equal(len(tx_result["vShieldedOutput"]), 1)

        # Verify tree sizes
        if "trees" in blk and "orchard" in blk["trees"]:
            assert_equal(blk["trees"]["orchard"]["size"], 4)
        if "trees" in blk and "sapling" in blk["trees"]:
            assert_equal(blk["trees"]["sapling"]["size"], 3)

        self._verify_final_sapling_update()

    def _verify_final_sapling_update(self):
        """Verify final Sapling tree update while Orchard remained unchanged."""
        current_block_height = self.nodes[0].getblockcount()
        new_treestate = self.nodes[0].z_gettreestate(str(current_block_height))

        # Verify Sapling tree changed
        if "sapling" in new_treestate and "sapling" in self.treestate:
            assert_equal(new_treestate["sapling"]["commitments"]["finalRoot"], self.sapling_root)
            assert new_treestate["sapling"]["commitments"]["finalRoot"] != self.treestate["sapling"]["commitments"]["finalRoot"]
            assert new_treestate["sapling"]["commitments"]["finalState"] != self.treestate["sapling"]["commitments"]["finalState"]
            assert_equal(len(new_treestate["sapling"]["commitments"]["finalRoot"]), 64)
            assert_equal(len(new_treestate["sapling"]["commitments"]["finalState"]), 196)

        # Verify Orchard tree unchanged
        if "orchard" in new_treestate and "orchard" in self.treestate:
            assert_equal(new_treestate["orchard"]["commitments"]["finalRoot"], self.orchard_root)
            assert new_treestate["orchard"]["commitments"]["finalRoot"] == self.treestate["orchard"]["commitments"]["finalRoot"]
            assert new_treestate["orchard"]["commitments"]["finalState"] == self.treestate["orchard"]["commitments"]["finalState"]
            assert_equal(len(new_treestate["orchard"]["commitments"]["finalRoot"]), 64)
            assert_equal(len(new_treestate["orchard"]["commitments"]["finalState"]), 260)

        # Verify Sprout tree unchanged
        assert_equal(new_treestate["sprout"], self.treestate["sprout"])


if __name__ == '__main__':
    FinalOrchardRootTest().main()
