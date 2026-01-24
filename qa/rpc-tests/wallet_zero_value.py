#!/usr/bin/env python3
# Copyright (c) 2020 The Zcash developers
# Copyright (c) 2017-2025 The Pirate developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, start_nodes
from decimal import Decimal

# Test wallet address behaviour across network upgrades
class WalletZeroValueTest(BitcoinTestFramework):
    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir, extra_args=[[
            '-allowdeprecated=getnewaddress',
            '-ac_private=0', # Disable private coins to test zero value outputs transparent outputs
            '-ac_cc=0',      # Disable cryptoconditions to allow transparent transactions
        ]] * 2)
        connect_nodes_bi(self.nodes,0,1)
        self.is_network_split=False
        self.sync_all()

    def run_test(self):
        # First, generate some blocks to ensure we have UTXOs
        self.nodes[1].generate(110)
        self.sync_all()
        
        #check if we can list zero value tx as available coins
        #1. create rawtx
        #2. hex-changed one output to 0.0
        #3. sign and send
        #4. check if recipient (node0) can list the zero value tx
        usp = self.nodes[1].listunspent()
        print(f"Available UTXO: {usp[0]['amount']} PIRATE")
        
        # Create a transaction where we can set one output to zero without excessive fees
        inputs = [{"txid":usp[0]['txid'], "vout":usp[0]['vout']}]
        input_amount = usp[0]['amount']
        # Calculate outputs to leave reasonable fee (0.0001 PIRATE)
        change_amount = input_amount - Decimal('0.0001')  # Leave 0.0001 for fee
        outputs = {
            self.nodes[1].getnewaddress(): change_amount,
            self.nodes[0].getnewaddress(): Decimal('0.0')  # This will be our zero value output
        }

        rawTx = self.nodes[1].createrawtransaction(inputs, outputs)
        print(f"Created raw transaction: {rawTx}")
        
        decRawTx = self.nodes[1].decoderawtransaction(rawTx)
        print(f"Transaction outputs: {decRawTx['vout']}")
        
        signedRawTx = self.nodes[1].signrawtransaction(rawTx)
        decRawTx = self.nodes[1].decoderawtransaction(signedRawTx['hex'])
        zeroValueTxid= decRawTx['txid']
        print(f"Zero value transaction ID: {zeroValueTxid}")
        
        self.nodes[1].sendrawtransaction(signedRawTx['hex'])

        self.sync_all()
        self.nodes[1].generate(1) #mine a block
        self.sync_all()

        unspentTxs = self.nodes[0].listunspent() #zero value tx must be in listunspents output
        print(f"Node 0 has {len(unspentTxs)} unspent transactions")
        
        found = False
        for uTx in unspentTxs:
            print(f"Checking UTXO: {uTx['txid'][:16]}... amount: {uTx['amount']}")
            if uTx['txid'] == zeroValueTxid:
                found = True
                print(f"Found zero value transaction! UTXO fields: {list(uTx.keys())}")
                assert_equal(uTx['amount'], Decimal('0.00000000'))
                # Check for amountZat field if it exists
                if 'amountZat' in uTx:
                    assert_equal(uTx['amountZat'], 0)
                else:
                    print("amountZat field not present in UTXO output")
                break
                
        assert found, f"Zero value transaction {zeroValueTxid} not found in listunspent output"
        print("SUCCESS: Zero value transaction found and validated!")

if __name__ == '__main__':
    WalletZeroValueTest().main()
