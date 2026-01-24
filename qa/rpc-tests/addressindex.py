#!/usr/bin/env python3
# Copyright (c) 2019 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .
#
# Test addressindex generation and fetching for insightexplorer or lightwalletd
# 
# RPCs tested here:
#
#   getaddresstxids
#   getaddressbalance
#   getaddressdeltas
#   getaddressutxos
#   getaddressmempool

from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework

from test_framework.util import (
    assert_equal,
    start_nodes,
    stop_nodes,
    connect_nodes_bi,
    wait_bitcoinds,
    initialize_chain_clean
)

from test_framework.script import (
    CScript,
    OP_HASH160,
    OP_EQUAL,
    OP_DUP,
    OP_DROP,
)

from test_framework.mininode import (
    COIN,
    CTransaction,
    CTxIn, CTxOut, COutPoint,
)

from binascii import hexlify, unhexlify


class AddressIndexTest(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.num_nodes = 3
        self.cache_behavior = 'clean'

    def setup_chain(self):
        """
        Initialize the blockchain test environment.
        
        Sets up a clean blockchain environment for testing by initializing
        the test directory and preparing the necessary data structures.
        """
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self):
        # Simplified args for stable testing
        base_args = [
            '-minrelaytxfee=0',
            '-txindex',
            '-experimentalfeatures',
            '-allowdeprecated=getnewaddress',
            '-allowdeprecated=z_getnewaddress',
            '-ac_private=0',
            '-ac_cc=0',
        ]
        
        # Node configurations
        args_with_indexing = []
        for i in range(self.num_nodes):
            if i < 2:
                # First two nodes use insightexplorer for address indexing
                args_with_indexing.append(base_args + ['-insightexplorer'])
            else:
                # Third node uses lightwalletd for address indexing
                args_with_indexing.append(base_args + ['-lightwalletd'])
        
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, args_with_indexing)

        # Connect the nodes using the standard method
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 0, 2)

        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        # helper functions

        def getaddresstxids(node_index, addresses, start, end):
            return self.nodes[node_index].getaddresstxids({
                'addresses': addresses,
                'start': start,
                'end': end
            })

        def getaddressdeltas(node_index, addresses, start, end, chainInfo=None):
            params = {
                'addresses': addresses,
                'start': start,
                'end': end,
            }
            if chainInfo is not None:
                params.update({'chainInfo': chainInfo})
            return self.nodes[node_index].getaddressdeltas(params)

        # default received value is the balance value
        def check_balance(node_index, address, expected_balance, expected_received=None):
            if isinstance(address, list):
                bal = self.nodes[node_index].getaddressbalance({'addresses': address})
            else:
                bal = self.nodes[node_index].getaddressbalance(address)
            assert_equal(bal['balance'], expected_balance)
            if expected_received is None:
                expected_received = expected_balance
            assert_equal(bal['received'], expected_received)

        # Begin test
        print("Starting address index test...")
        
        # Generate blocks and sync all nodes
        self.nodes[0].generate(105)
        self.sync_all()
        
        # Check that all nodes have the expected balance
        balance = self.nodes[0].getbalance()
        assert(balance > 26620 and balance < 26630)  # ~104 blocks x 256 ARRR
        
        # Get coinbase transactions for address testing
        unspent_txids = [u['txid'] for u in self.nodes[0].listunspent()]
        
        # Pick first mining address from first coinbase transaction  
        tx = self.nodes[0].getrawtransaction(unspent_txids[0], 1)
        mining_addr = tx['vout'][0]['scriptPubKey']['addresses'][0]
        
        # Test address has at least one transaction
        sample_txids = self.nodes[0].getaddresstxids(mining_addr)
        assert(len(sample_txids) >= 1)
        
        # Create test address and send transactions
        # Use regular getnewaddress with ac_private=0 and ac_cc=0 configuration
        addr1 = self.nodes[0].getnewaddress()
        expected = 0
        txids_a1 = []
        
        for i in range(5):
            amount = i + 1
            txid = self.nodes[0].sendtoaddress(addr1, amount)
            txids_a1.append(txid)
            self.nodes[0].generate(1)
            self.sync_all()
            expected += amount
        
        # Test address balance
        balance_info = self.nodes[0].getaddressbalance(addr1)
        assert_equal(balance_info['received'], expected * COIN)
        
        # Test transaction retrieval
        retrieved_txids = self.nodes[0].getaddresstxids(addr1)
        assert_equal(sorted(retrieved_txids), sorted(txids_a1))
        
        # Test address deltas
        deltas = self.nodes[0].getaddressdeltas({'addresses': [addr1]})
        positive_deltas = [d for d in deltas if d['satoshis'] > 0]
        assert_equal(len(positive_deltas), 5)
        
        # Test address UTXOs
        utxos = self.nodes[0].getaddressutxos(addr1)
        assert(len(utxos) > 0)
        
        for utxo in utxos:
            assert_equal(utxo['address'], addr1)
            assert(utxo['satoshis'] > 0)
        
        print("Address indexing test completed successfully!")


if __name__ == '__main__':
    AddressIndexTest().main()
