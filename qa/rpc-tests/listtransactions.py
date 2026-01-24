#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2016-2022 The Zcash developers
# Copyright (c) 2019-2025 The Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

# Exercise the listtransactions RPC API
# 
# This test has been adapted for Pirate Chain's unique transaction behavior:
# - Uses transparent transactions (-ac_private=0) for compatibility
# - Accounts for Pirate Chain's multiple transaction entries per txid
# - Adjusted for smaller transaction amounts and specific validation patterns

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, start_nodes

from decimal import Decimal

def count_array_matches(object_array, to_match):
    num_matched = 0
    for item in object_array:
        if all((item[key] == value for key,value in to_match.items())):
            num_matched = num_matched+1
    return num_matched

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

class ListTransactionsTest(BitcoinTestFramework):
    """
    Test the listtransactions RPC method functionality.
    
    This test verifies:
    - Basic send/receive transaction listing
    - Transaction confirmation updates after mining
    - Send-to-self transaction handling
    - Multi-recipient sendmany operations
    
    Pirate Chain specific adaptations:
    - Uses transparent transactions for test compatibility
    - Validates specific transaction amounts due to multiple entries per txid
    - Handles Pirate Chain's unique transaction categorization
    """
    
    def __init__(self):
        super().__init__()
        self.cache_behavior = 'clean'  # Use clean cache to avoid CheckBlockIndex errors
        self.num_nodes = 2

    def setup_nodes(self):
        return start_nodes(self.num_nodes, self.options.tmpdir, extra_args=[[
            "-ac_private=0",  # Enable transparent transactions for testing
            "-ac_cc=0"        # Disable CryptoConditions
        ]] * self.num_nodes)

    def run_test(self):
        # Test 1: Basic send transaction from node 0 to node 1
        self.nodes[0].generate(10)
        self.sync_all()
        
        txid = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.1)
        self.sync_all()
        
        # Verify send transaction appears correctly on sender node
        # Note: Pirate Chain creates multiple entries per txid, so we specify exact amount
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid, "category":"send", "amount":Decimal("-0.1")},
                           {"category":"send","amount":Decimal("-0.1"),"confirmations":0})
        
        # Verify receive transaction appears correctly on recipient node
        check_array_result(self.nodes[1].listtransactions(),
                           {"txid":txid},
                           {"category":"receive","amount":Decimal("0.1"),"confirmations":0})

        # Test 2: Mine a block and verify confirmation count updates
        self.nodes[0].generate(1)
        self.sync_all()
        
        # Verify confirmation count increased to 1 for both nodes
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid, "category":"send", "amount":Decimal("-0.1")},
                           {"category":"send","amount":Decimal("-0.1"),"confirmations":1})
        check_array_result(self.nodes[1].listtransactions(),
                           {"txid":txid},
                           {"category":"receive","amount":Decimal("0.1"),"confirmations":1})

        # Test 3: Send-to-self transaction
        txid = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 0.2)
        
        # Verify both send and receive entries for self-transaction
        # Must specify exact amounts due to Pirate Chain's multiple transaction entries
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid, "category":"send", "amount":Decimal("-0.2")},
                           {"amount":Decimal("-0.2")})
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid, "category":"receive", "amount":Decimal("0.2")},
                           {"amount":Decimal("0.2")})

        # Test 4: Multi-recipient sendmany transaction
        # First, fund node 1 with sufficient balance for the test
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 5.0)
        self.nodes[0].generate(1)
        self.sync_all()
        
        # Create sendmany transaction: node1 sends to multiple addresses
        node_0_addr_0 = self.nodes[0].getnewaddress()
        node_0_addr_1 = self.nodes[0].getnewaddress()
        node_1_addr_0 = self.nodes[1].getnewaddress()
        node_1_addr_1 = self.nodes[1].getnewaddress()
        
        # Send different amounts to each address (mix of self and other node)
        send_to = { node_0_addr_0 : 0.11,   # to node 0
                    node_1_addr_0 : 0.22,   # to node 1 (self)
                    node_0_addr_1 : 0.33,   # to node 0
                    node_1_addr_1 : 0.44 }  # to node 1 (self)
        
        txid = self.nodes[1].sendmany("", send_to)
        self.sync_all()
        
        # Verify all send transactions appear on sender (node 1)
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"send","amount":Decimal("-0.11")},
                           {"txid":txid} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"send","amount":Decimal("-0.22")},
                           {"txid":txid} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"send","amount":Decimal("-0.33")},
                           {"txid":txid} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"send","amount":Decimal("-0.44")},
                           {"txid":txid} )
        
        # Verify corresponding receive transactions
        check_array_result(self.nodes[0].listtransactions(),
                           {"category":"receive","amount":Decimal("0.11")},
                           {"txid":txid} )
        check_array_result(self.nodes[0].listtransactions(),
                           {"category":"receive","amount":Decimal("0.33")},
                           {"txid":txid} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"receive","amount":Decimal("0.22")},
                           {"txid":txid} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"receive","amount":Decimal("0.44")},
                           {"txid":txid} )

if __name__ == '__main__':
    ListTransactionsTest().main()
