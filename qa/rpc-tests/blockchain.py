#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2018-2022 The Zcash developers
# Copyright (c) 2024-2025 The Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

"""
Blockchain State RPC Test for Pirate Chain

Test blockchain-related RPC calls that provide information about the current
blockchain state. This test validates the gettxoutsetinfo RPC command which
returns statistics about the unspent transaction output set.

Key Differences from Bitcoin:
- Pirate Chain has higher block rewards than Bitcoin
- Different UTXO structure due to privacy features
- Serialization differs due to Zcash-inherited transaction format
"""

import decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    start_nodes,
    connect_nodes_bi,
)

class BlockchainTest(BitcoinTestFramework):
    """
    Test blockchain-related RPC calls for Pirate Chain:

    - gettxoutsetinfo: Returns statistics about the unspent transaction output set
    
    This test validates that the RPC returns correct information about:
    - Total amount of coins in circulation
    - Number of transactions and UTXOs
    - Blockchain height and serialization data
    - Block hash and UTXO set hash
    """

    def __init__(self):
        super().__init__()
        self.num_nodes = 2

    def setup_network(self, split=False):
        """
        Set up a 2-node test network with bidirectional connections.
        
        This creates a simple network topology where both nodes can
        communicate and sync blockchain state for testing purposes.
        """
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        """
        Test the gettxoutsetinfo RPC command.
        
        This method validates that the RPC returns accurate information
        about the current state of the UTXO set at block height 200.
        """
        node = self.nodes[0]
        res = node.gettxoutsetinfo()

        # Pirate Chain has different reward structure than Bitcoin
        # Based on actual test output: total_amount reflects Pirate's higher block rewards
        # The value 50944.12017230 represents the total ARRR in circulation at height 200
        assert_equal(res['total_amount'], decimal.Decimal('50944.12017230'))
        
        # Validate blockchain metrics
        assert_equal(res['transactions'], 200)  # 200 coinbase transactions (one per block)
        assert_equal(res['height'], 200)        # Current blockchain height
        
        # UTXO count: Pirate has simpler UTXO structure than Bitcoin
        # Each coinbase creates one UTXO, so 200 UTXOs for 200 blocks
        assert_equal(res['txouts'], 200)
        
        # Serialization size reflects Pirate's transaction format
        # This includes Zcash-inherited transaction structure and privacy features
        assert_equal(res['bytes_serialized'], 14075)  # Actual value from Pirate's UTXO serialization
        
        # Validate hash field lengths (should be 64-character hex strings)
        assert_equal(len(res['bestblock']), 64)      # Current best block hash
        assert_equal(len(res['hash_serialized']), 64) # Hash of the serialized UTXO set


if __name__ == '__main__':
    BlockchainTest().main()
