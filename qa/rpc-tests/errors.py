#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2018-2023 The Zcash developers
# Copyright (c) 2024 The Pirate Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

"""
Test RPC error handling for Pirate Chain.

This test validates how Pirate Chain handles various RPC error conditions,
including invalid methods, wrong parameters, and missing required parameters.

Key behavioral differences from Bitcoin Core:
- Pirate shows comprehensive help text instead of throwing parameter count errors
- This provides a more user-friendly experience for developers
- All error conditions are properly handled and documented

The test covers:
1. Non-existent RPC methods (should return "Method not found")
2. Invalid parameter counts (should display help documentation)
3. Missing required parameters (should show command help)
4. Extra unwanted parameters (should trigger help display)

This comprehensive error handling ensures that users get helpful feedback
instead of cryptic error messages when using RPC commands incorrectly.
"""

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import start_nodes

class ErrorsTest(BitcoinTestFramework):
    """
    Comprehensive test suite for RPC error handling in Pirate Chain.
    
    This test validates that Pirate Chain's RPC implementation properly handles
    various error conditions in a user-friendly manner. Unlike Bitcoin Core,
    which throws cryptic parameter errors, Pirate Chain displays comprehensive
    help documentation when commands are used incorrectly.
    
    Test Coverage:
    - Invalid method names
    - Incorrect parameter counts
    - Missing required parameters  
    - Extra unnecessary parameters
    
    Expected Behavior:
    - Non-existent methods: "Method not found" error
    - Parameter issues: Full help documentation display
    - Consistent error handling across all RPC commands
    
    This approach significantly improves the developer experience by providing
    immediate, contextual help instead of requiring separate help lookups.
    """

    def __init__(self):
        super().__init__()
        self.num_nodes = 1  # Only need one node for error testing

    def setup_network(self, split=False):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir)
        self.is_network_split = False

    def run_test(self):
        """
        Execute comprehensive RPC error handling tests.
        
        This method systematically tests different types of RPC errors
        to ensure Pirate Chain's error handling is both robust and user-friendly.
        """
        node = self.nodes[0]

        print("Testing RPC error handling...")

        # Test 1: Non-existent method should fail with "Method not found"
        # This validates basic RPC method validation
        try:
            node.nonexistentmethod()
            assert False, "Should have thrown an exception for non-existent method"
        except JSONRPCException as e:
            errorString = e.error['message']
            assert "method not found" in errorString.lower(), f"Expected 'method not found', got: {errorString}"
            print("✓ Non-existent method correctly returns 'Method not found'")

        # Test 2: Wrong parameters should trigger help display (Pirate-specific behavior)
        # This demonstrates Pirate's user-friendly approach to parameter errors
        try:
            node.gettxoutsetinfo(1)  # gettxoutsetinfo takes no parameters
            assert False, "Should have thrown an exception for invalid parameters"
        except JSONRPCException as e:
            errorString = e.error['message']
            # In Pirate, wrong parameters show help text instead of parameter count errors
            assert "returns comprehensive statistics" in errorString.lower(), \
                f"Expected help text for gettxoutsetinfo, got: {errorString[:100]}..."
            print("✓ Invalid parameters correctly trigger help display")

        # Test 3: Missing required parameters should also show help
        # Validates that required parameter validation works properly
        try:
            node.getblock()  # Missing required hash parameter
            assert False, "Should have thrown an exception for missing required parameters"
        except JSONRPCException as e:
            errorString = e.error['message']
            # Should show help text for getblock
            assert "retrieves detailed information" in errorString.lower(), \
                f"Expected help text for getblock, got: {errorString[:100]}..."
            print("✓ Missing required parameters correctly trigger help display")

        # Test 4: Verify help is comprehensive for extra parameters
        # Ensures consistent behavior across different RPC commands
        try:
            node.getinfo("too", "many", "params")  # getinfo takes no parameters
            assert False, "Should have thrown an exception for too many parameters"
        except JSONRPCException as e:
            errorString = e.error['message']
            # Should show help text for getinfo
            assert "returns an object containing" in errorString.lower(), \
                f"Expected help text for getinfo, got: {errorString[:100]}..."
            print("✓ Extra parameters correctly trigger help display")

        print("All RPC error handling tests passed!")
        print("Note: Pirate shows helpful documentation instead of cryptic parameter errors")
        print("This user-friendly approach improves the developer experience significantly")

if __name__ == '__main__':
    ErrorsTest().main()
