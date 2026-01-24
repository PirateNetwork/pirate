#!/usr/bin/env python3
# Copyright (c) 2020 The Zcash developers
# Copyright (c) 2023-2025 The Pirate developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

"""
Test framework validation for Pirate network.

This test validates the functionality of the Pirate test framework by:
1. Starting multiple Pirate daemon nodes
2. Testing the check_node_log utility function
3. Verifying proper node startup and log output
4. Testing both positive and negative log assertions

The test ensures that the framework can properly:
- Initialize test nodes with correct parameters
- Find expected strings in node logs ("Pirate version")
- Handle assertions for missing strings
- Restart nodes after log checking
- Clean shutdown of test nodes
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_raises,
    connect_nodes,
    start_node,
    check_node_log,
)

class FrameworkTest(BitcoinTestFramework):
    """
    Test class for validating the Pirate network test framework functionality.
    
    This test ensures that the test framework utilities work correctly,
    particularly the log checking functionality and node management.
    """

    def __init__(self):
        super().__init__()
        self.num_nodes = 2
        self.cache_behavior = 'clean'

    def start_node_with(self, index, extra_args=[]):
        """
        Start a Pirate daemon node with specified index and additional arguments.
        
        Args:
            index (int): Node index number
            extra_args (list): Additional command line arguments for the node
            
        Returns:
            Node proxy object for RPC communication
        """
        args = []
        return start_node(index, self.options.tmpdir, args + extra_args)

    def wait_for_node_ready(self, index, timeout=30):
        """
        Wait for a node to be fully ready by checking if it responds to RPC calls.
        
        Args:
            index (int): Node index number
            timeout (int): Maximum time to wait in seconds
        """
        import time
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                # Try a simple RPC call to check if node is ready
                self.nodes[index].getblockcount()
                return True
            except Exception:
                time.sleep(0.5)
        raise Exception(f"Node {index} failed to become ready within {timeout} seconds")

    def setup_network(self, split=False):
        """
        Initialize the test network with two connected Pirate nodes.
        
        Args:
            split (bool): Whether to split the network (not used in this test)
        """
        self.nodes = []
        self.nodes.append(self.start_node_with(0))
        self.nodes.append(self.start_node_with(1))
        
        # Wait for nodes to be ready before connecting
        self.wait_for_node_ready(0)
        self.wait_for_node_ready(1)
        
        connect_nodes(self.nodes[1], 0)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        """
        Execute the main test logic for framework validation.
        
        This test performs the following validations:
        1. Tests check_node_log utility with a string that should be found
        2. Restarts the node after log checking
        3. Tests negative case with assert_raises for a string that won't be found
        4. Restarts the node again before test completion
        """
        
        # Test the check_node_log utility function with expected string
        # This should find "Pirate version" in the node startup logs
        string_to_find = "Pirate version"
        check_node_log(self, 1, string_to_find)

        # Node 1 was stopped to check the logs, need to be restarted
        self.nodes[1] = self.start_node_with(1, [])
        self.wait_for_node_ready(1)
        connect_nodes(self.nodes[1], 0)

        # Test negative case - this should raise AssertionError for non-existent string
        assert_raises(AssertionError, check_node_log, self, 1, "Will not be found")

        # Need to start node 1 before leaving the test to ensure clean shutdown
        self.nodes[1] = self.start_node_with(1, [])
        self.wait_for_node_ready(1)
        connect_nodes(self.nodes[1], 0)


if __name__ == '__main__':
    FrameworkTest().main()
