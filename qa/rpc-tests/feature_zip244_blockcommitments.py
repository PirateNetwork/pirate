#!/usr/bin/env python3
# Copyright (c) 2020 The Zcash developers
# Copyright (c) 2022-2025 The Pirate developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .


from test_framework.blocktools import derive_block_commitments_hash
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    bytes_to_hex_str,
    hex_str_to_bytes,
    nuparams,
    start_nodes,
)

TERMINATOR = b'\x00' * 32

"""
ZIP 244 Block Commitments Test

This test validates the correct implementation of ZIP 244 block commitments in Pirate Chain.
It verifies that the 'hashBlockCommitments' block header field transitions correctly between
pre-Orchard and post-Orchard activation:

1. Pre-Orchard (before block 200): hashBlockCommitments equals finalsaplingroot, 
   and chainhistoryroot is null
2. Post-Orchard (block 200+): hashBlockCommitments contains ZIP 244 commitment hash
   derived from chainhistoryroot and authdataroot

The test ensures proper network upgrade behavior at Orchard activation height.
"""

class AuthDataRootTest(BitcoinTestFramework):
    """
    Test ZIP 244 block commitments functionality in Pirate Chain.
    
    Validates the hashBlockCommitments field behavior before and after 
    Orchard/NU5 network upgrade activation.
    """

    def __init__(self):
        super().__init__()
        self.num_nodes = 4
        self.cache_behavior = 'clean'  # Start with fresh blockchain state

    def setup_nodes(self):
        return start_nodes(self.num_nodes, self.options.tmpdir, extra_args=[[
        ]] * self.num_nodes)

    def run_test(self):
        """
        Main test execution:
        1. Generate blocks up to height 197 (pre-Orchard)
        2. Test blocks 197-199 for pre-Orchard behavior
        3. Generate blocks to reach Orchard activation (block 200+)
        4. Test blocks 200-201 for post-Orchard ZIP 244 behavior
        """
        print("Starting ZIP 244 Block Commitments test...")
        
        # Generate blocks up to block 197 to test pre-Orchard behavior (blocks 197, 198, 199)
        # Orchard/NU5 activates at block 200 on regtest
        self.nodes[0].generate(197)
        self.sync_all()
        
        current_height = self.nodes[0].getblockcount()
        print(f"Generated blocks up to height: {current_height}")

        # Test Phase 1: Pre-Orchard blocks (197, 198, 199 - before block 200)
        # Expected behavior: blockcommitments = finalsaplingroot, chainhistoryroot = null
        print("\n=== Phase 1: Testing Pre-Orchard Blocks (before block 200) ===")
        for i in range(3):
            block_height = current_height
            if block_height >= 200:
                break  # Don't test blocks at or after Orchard activation
                
            block_before = self.nodes[0].getblock('%d' % block_height)
            print(f"Pre-Orchard Block {block_height}:")
            print(f"  blockcommitments: {block_before['blockcommitments']}")
            print(f"  chainhistoryroot: {block_before['chainhistoryroot']}")
            
            # Check what other fields are available to understand the structure
            relevant_fields = ['finalsaplingroot', 'finalorchardroot', 'authdataroot']
            for field in relevant_fields:
                if field in block_before:
                    print(f"  {field}: {block_before[field]}")
            
            # Prior to Orchard: blockcommitments equals finalsaplingroot, chainhistoryroot is null
            print(f"  Expected: blockcommitments == finalsaplingroot, chainhistoryroot == null")
            
            # Verify chainhistoryroot is null (all zeros)
            null_hash = "0000000000000000000000000000000000000000000000000000000000000000"
            assert_equal(block_before['chainhistoryroot'], null_hash)
            print("  ✓ chainhistoryroot is null as expected")
            
            # Verify blockcommitments equals finalsaplingroot
            if 'finalsaplingroot' in block_before:
                assert_equal(block_before['blockcommitments'], block_before['finalsaplingroot'])
                print("  ✓ blockcommitments equals finalsaplingroot as expected")
            else:
                print("  ✗ No finalsaplingroot field found!")

            self.nodes[0].generate(1)
            self.sync_all()
            current_height = self.nodes[0].getblockcount()

        # Generate additional blocks to reach Orchard activation (block 200+)
        while current_height < 202:
            self.nodes[0].generate(1)
            self.sync_all()
            current_height = self.nodes[0].getblockcount()

        # Test Phase 2: Post-Orchard blocks (200, 201)
        # Expected behavior: blockcommitments = ZIP 244 hash of (chainhistoryroot + authdataroot)
        print("\n=== Phase 2: Testing Post-Orchard Blocks (block 200+) ===")
        for i in range(2):
            block_height = 200 + i
            block_after = self.nodes[0].getblock('%d' % block_height)
            print(f"Post-Orchard Block {block_height}:")
            print(f"  blockcommitments: {block_after['blockcommitments']}")
            print(f"  chainhistoryroot: {block_after['chainhistoryroot']}")
            
            # Calculate ZIP 244 block commitments hash
            block_commitments = bytes_to_hex_str(derive_block_commitments_hash(
                hex_str_to_bytes(block_after['chainhistoryroot'])[::-1],
                hex_str_to_bytes(block_after['authdataroot'])[::-1],
            )[::-1])
            print(f"  calculated ZIP 244 hash: {block_commitments}")
            
            # Verify that blockcommitments field matches the calculated ZIP 244 hash
            assert_equal(block_after['blockcommitments'], block_commitments)
            print(f"  ✓ blockcommitments matches ZIP 244 hash as expected")

            if i == 0:  # Generate one more block to test block 201
                self.nodes[0].generate(1)
                self.sync_all()
        
        print("\n✅ ZIP 244 Block Commitments test completed successfully!")
        print("   - Pre-Orchard: blockcommitments = finalsaplingroot ✓")
        print("   - Post-Orchard: blockcommitments = ZIP 244 hash ✓")


if __name__ == '__main__':
    AuthDataRootTest().main()
