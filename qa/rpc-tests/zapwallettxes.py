#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2016-2022 The Zcash developers
# Copyright (c) 2017-2025 The Pirate developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    start_nodes, start_node, connect_nodes_bi, bitcoind_processes, wait_and_assert_operationid_status


class ZapWalletTXesTest (BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.cache_behavior = 'clean'
        self.num_nodes = 3

    def setup_network(self, split=False):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, extra_args=[[
            '-allowdeprecated=getnewaddress',
        ]] * self.num_nodes)
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        self.is_network_split=False
        self.sync_all()

    def run_test (self):
        print("Mining blocks...")
        self.nodes[0].generate(4)
        self.sync_all()
        self.nodes[1].generate(101)
        self.sync_all()

        # Check that we have some balance for z_shieldcoinbase operations
        balance = self.nodes[0].getbalance()
        print(f"Node 0 balance: {balance}")
        assert balance > 0, "Node 0 should have some balance for z_shieldcoinbase operations"

        # Create Sapling addresses for shielded transactions
        zaddr0 = self.nodes[0].z_getnewaddress('sapling')
        zaddr1 = self.nodes[0].z_getnewaddress('sapling')
        zaddr2 = self.nodes[0].z_getnewaddress('sapling')
        zaddr3 = self.nodes[0].z_getnewaddress('sapling')

        # Create z_shieldcoinbase transactions instead of sendtoaddress
        # Need to space these out to avoid mempool conflicts
        result0 = self.nodes[0].z_shieldcoinbase("*", zaddr0, 0)
        opid0 = result0['opid']
        txid0 = wait_and_assert_operationid_status(self.nodes[0], opid0)
        print(f"Completed z_shieldcoinbase 0: {txid0}")

        # Generate a block to confirm the first transaction and free up UTXOs
        self.nodes[0].generate(1)
        self.sync_all()

        result1 = self.nodes[0].z_shieldcoinbase("*", zaddr1, 0)
        opid1 = result1['opid']
        txid1 = wait_and_assert_operationid_status(self.nodes[0], opid1)
        print(f"Completed z_shieldcoinbase 1: {txid1}")
        
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        # Now create unconfirmed transactions
        result2 = self.nodes[0].z_shieldcoinbase("*", zaddr2, 0)
        opid2 = result2['opid']
        txid2 = wait_and_assert_operationid_status(self.nodes[0], opid2)
        print(f"Completed z_shieldcoinbase 2: {txid2}")

        # Generate a block to free up more UTXOs for the last transaction
        self.nodes[0].generate(1)
        self.sync_all()

        result3 = self.nodes[0].z_shieldcoinbase("*", zaddr3, 0)
        opid3 = result3['opid']
        txid3 = wait_and_assert_operationid_status(self.nodes[0], opid3)
        print(f"Completed z_shieldcoinbase 3: {txid3}")

        tx0 = self.nodes[0].gettransaction(txid0)
        assert_equal(tx0['txid'], txid0) # tx0 must be available (confirmed)

        tx1 = self.nodes[0].gettransaction(txid1)
        assert_equal(tx1['txid'], txid1) # tx1 must be available (confirmed)

        tx2 = self.nodes[0].gettransaction(txid2)
        assert_equal(tx2['txid'], txid2) # tx2 must be available (unconfirmed)

        tx3 = self.nodes[0].gettransaction(txid3)
        assert_equal(tx3['txid'], txid3) # tx3 must be available (unconfirmed)

        # restart pirated
        self.nodes[0].stop()
        bitcoind_processes[0].wait()
        self.nodes[0] = start_node(0,self.options.tmpdir)

        tx3 = self.nodes[0].gettransaction(txid3)
        assert_equal(tx3['txid'], txid3) # tx must be available (unconfirmed)

        self.nodes[0].stop()
        bitcoind_processes[0].wait()

        # restart pirated with zapwallettxes
        self.nodes[0] = start_node(0,self.options.tmpdir, ["-zapwallettxes=1"])

        aException = False
        try:
            tx3 = self.nodes[0].gettransaction(txid3)
        except JSONRPCException as e:
            print(e)
            aException = True

        assert_equal(aException, True) # there must be an exception because the unconfirmed wallet tx0 must be gone by now

        tx0 = self.nodes[0].gettransaction(txid0)
        assert_equal(tx0['txid'], txid0) # tx0 (confirmed) must still be available because it was confirmed


if __name__ == '__main__':
    ZapWalletTXesTest().main()
