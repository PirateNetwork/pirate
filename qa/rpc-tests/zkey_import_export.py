#!/usr/bin/env python3
# Copyright (c) 2017 The Zcash developers
# Copyright (c) 2017-2025 The Pirate developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, start_nodes,\
    initialize_chain_clean, connect_nodes_bi, wait_and_assert_operationid_status, \
    get_coinbase_address
import logging
import sys
import time

logging.basicConfig(format='%(levelname)s: %(message)s', level=logging.INFO, stream=sys.stdout)

class ZkeyImportExportTest (BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 6)

    def setup_network(self, split=False):
        self.nodes = start_nodes(6, self.options.tmpdir, extra_args=[[
        ]] * 6)
        # Connect all nodes to all other nodes (full mesh topology)
        for i in range(len(self.nodes)):
            for j in range(i + 1, len(self.nodes)):
                connect_nodes_bi(self.nodes, i, j)
        self.is_network_split=False
        self.sync_all()

    def run_test(self):
        [alice, bob, charlie, david, miner, lisa] = self.nodes

        def generate_blocks_with_delay(node, num_blocks):
            """Generate blocks with 1 second delay before each block"""
            for i in range(num_blocks):
                time.sleep(1)  # Wait 1 second before generating each block
                try:
                    node.generate(1)
                    # Add a small delay to ensure block propagation
                    if i < num_blocks - 1:  # Don't sleep after the last block
                        time.sleep(0.1)  # Small delay between blocks
                except Exception as e:
                    print(f"Error generating block {i+1}/{num_blocks}: {e}")
                    raise

        def safe_sync_all():
            """Sync all nodes with retry logic"""
            max_attempts = 3
            for attempt in range(max_attempts):
                try:
                    self.sync_all()
                    return
                except Exception as e:
                    print(f"Sync attempt {attempt + 1} failed: {e}")
                    if attempt == max_attempts - 1:
                        raise
                    time.sleep(2)  # Wait before retry

        # the sender loses 'amount' plus fee; to_addr receives exactly 'amount'
        def z_send(from_node, from_addr, to_addr, amount):
            print("Sending amount from %s to %s: %s (fee: 0.0001)" % (from_addr, to_addr, amount))

            opid = from_node.z_sendmany(from_addr,
                [{"address": to_addr, "amount": Decimal(amount)}], 1, Decimal('0.0001'))
            wait_and_assert_operationid_status(from_node, opid)
            safe_sync_all()
            generate_blocks_with_delay(miner, 1)
            safe_sync_all()

        def verify_utxos(node, amts, zaddr):
            amts.sort(reverse=True)
            txs = node.z_listreceivedbyaddress(zaddr)
            txs.sort(key=lambda x: x["amount"], reverse=True)
            print("\nSorted txs (txid, amount, rawconfirmations):", [(tx["txid"], tx["amount"], tx["rawconfirmations"]) for tx in txs], "\n")
            print("Expected amounts:", amts, "\n")

            try:
                assert_equal(amts, [tx["amount"] for tx in txs])
                for tx in txs:
                    # make sure Sapling outputs exist and have valid values
                    assert_equal("outindex" in tx, True)
                    assert_greater_than(tx["outindex"], -1)
            except AssertionError:
                logging.error(
                    'Expected amounts: %r; txs: %r',
                    amts, txs)
                raise

        def get_private_balance(node):
            balance = node.z_gettotalbalance()
            print("z_gettotalbalance:", balance)
            return balance['private']

        # Mine the first 10 blocks to get the chain started
        # Add initial wait to ensure all nodes are properly connected and ready
        time.sleep(2)
        generate_blocks_with_delay(alice, 10)
        safe_sync_all()

        # Seed Alice with some funds
        generate_blocks_with_delay(alice, 10)
        safe_sync_all()
        generate_blocks_with_delay(miner, 100)
        safe_sync_all()

        # Shield Alice's coinbase funds to her zaddr
        alice_taddr = get_coinbase_address(alice)
        alice_zaddr = alice.z_getnewaddresskey('sapling')  # Explicitly use Sapling since we're after Sapling activation
        print("Alice's taddr:", alice_taddr)
        print("Alice's zaddr:", alice_zaddr)
        res = alice.z_shieldcoinbase(alice_taddr, alice_zaddr, 0, 1)  # Shield to alice_zaddr
        wait_and_assert_operationid_status(alice, res['opid'])

        safe_sync_all()
        generate_blocks_with_delay(miner, 1)
        safe_sync_all()

        # Now get a pristine z-address for receiving transfers:
        bob_zaddr = bob.z_getnewaddresskey('sapling')  # Explicitly use Sapling since we're after Sapling activation
        verify_utxos(bob, [], bob_zaddr)
        # TODO: Verify that charlie doesn't have funds in addr
        # verify_utxos(charlie, [])

        # Internal test consistency assertion:
        # Alice will send 6 transactions with 0.0001 fee each = 0.0006 total fees
        # Plus the amounts she'll send: 2.3 + 3.7 + 0.1 + 0.5 + 1.0 + 0.19 = 7.79
        # Total Alice needs: 7.79 + 0.0006 = 7.7906
        total_amounts = Decimal('2.3') + Decimal('3.7') + Decimal('0.1') + Decimal('0.5') + Decimal('1.0') + Decimal('0.19')
        alice_fees = Decimal('0.0006')  # 6 transactions * 0.0001 fee each
        total_needed = total_amounts + alice_fees
        assert_greater_than(
            Decimal(get_private_balance(alice)),
            total_needed)

        logging.info("Sending pre-export txns...")
        z_send(alice, alice_zaddr, bob_zaddr, Decimal('2.3'))
        z_send(alice, alice_zaddr, bob_zaddr, Decimal('3.7'))
        safe_sync_all()
        generate_blocks_with_delay(miner, 1)
        safe_sync_all()

        logging.info("Exporting privkey from bob...")
        bob_privkey = bob.z_exportkey(bob_zaddr)
        print("Bob's private key: %s" % bob_privkey)

        logging.info("Sending post-export txns...")
        z_send(alice, alice_zaddr, bob_zaddr, Decimal('0.1'))
        z_send(alice, alice_zaddr, bob_zaddr, Decimal('0.5'))
        safe_sync_all()
        generate_blocks_with_delay(miner, 1)
        safe_sync_all()

        verify_utxos(bob, [Decimal('2.3'), Decimal('3.7'), Decimal('0.1'), Decimal('0.5')], bob_zaddr)
        # verify_utxos(charlie, [])

        logging.info("Importing bob_privkey into charlie...")
        # z_importkey rescan defaults to "whenkeyisnew", so should rescan here
        # z_importkey returns None, so we need to get the address separately
        charlie.z_importkey(bob_privkey)
        
        # Get the imported address - it should be the same as bob_zaddr since we imported bob's key
        imported_addresses = charlie.z_listaddresses()
        # Find the Sapling address that matches bob_zaddr
        ipk_zaddr_str = None
        for addr in imported_addresses:
            if addr == bob_zaddr:
                ipk_zaddr_str = addr
                break
        
        if ipk_zaddr_str is None:
            raise AssertionError("Failed to find imported address")
            
        print("Imported z-address: %s" % ipk_zaddr_str)

        # z_importkey should have rescanned for new key, so this should pass:
        verify_utxos(charlie, [Decimal('2.3'), Decimal('3.7'), Decimal('0.1'), Decimal('0.5')], ipk_zaddr_str)

        # Verify the address type by checking if it's a Sapling address
        # Sapling addresses start with "zs" in mainnet or "zregtestsapling" in regtest
        assert ipk_zaddr_str.startswith("zregtestsapling"), "Address should be Sapling format"

        # Verify idempotent behavior:
        charlie.z_importkey(bob_privkey)  # This should not fail
        
        # Verify the address is still there
        imported_addresses_2 = charlie.z_listaddresses()
        assert ipk_zaddr_str in imported_addresses_2, "Address should still be in wallet after re-import"

        # amounts should be unchanged
        verify_utxos(charlie, [Decimal('2.3'), Decimal('3.7'), Decimal('0.1'), Decimal('0.5')], ipk_zaddr_str)

        logging.info("Sending post-import txns...")
        z_send(alice, alice_zaddr, bob_zaddr, Decimal('1.0'))
        z_send(alice, alice_zaddr, bob_zaddr, Decimal('0.19'))
        safe_sync_all()
        generate_blocks_with_delay(miner, 1)
        safe_sync_all()

        verify_utxos(bob, [Decimal('2.3'), Decimal('3.7'), Decimal('0.1'), Decimal('0.5'), Decimal('1.0'), Decimal('0.19')], bob_zaddr)
        verify_utxos(charlie, [Decimal('2.3'), Decimal('3.7'), Decimal('0.1'), Decimal('0.5'), Decimal('1.0'), Decimal('0.19')], ipk_zaddr_str)

        # keep track of the fees incurred by bob (his sends)
        bob_fee = Decimal('0.0002')  # Bob will send 2 transactions with 0.0001 fee each

        # Try to reproduce zombie balance reported in #1936
        # At generated zaddr, receive ZEC, and send ZEC back out. bob -> alice
        print("Sending amount from bob to alice: ", Decimal('2.3'))
        z_send(bob, bob_zaddr, alice_zaddr, Decimal('2.3'))
        print("Sending amount from bob to alice: ", Decimal('3.7'))
        z_send(bob, bob_zaddr, alice_zaddr, Decimal('3.7'))
        safe_sync_all()
        generate_blocks_with_delay(miner, 25)
        safe_sync_all()
        
        # Bob's remaining balance calculation:
        # Bob received all amounts: 2.3 + 3.7 + 0.1 + 0.5 + 1.0 + 0.19 = 7.79
        # Bob sent back amounts: 2.3 + 3.7 = 6.0  
        # Bob paid fees: 0.0001 + 0.0001 = 0.0002
        # Bob's final balance: 7.79 - 6.0 - 0.0002 = 1.7898
        total_received = Decimal('2.3') + Decimal('3.7') + Decimal('0.1') + Decimal('0.5') + Decimal('1.0') + Decimal('0.19')  # All amounts Bob received
        total_sent = Decimal('2.3') + Decimal('3.7')  # amounts that Bob sent back to Alice
        bob_balance = total_received - total_sent - bob_fee
        
        assert_equal(bob.z_getbalance(bob_zaddr), bob_balance)

        # z_import onto new node "david" (blockchain rescan, default or True?)
        # Force a rescan when importing the key to ensure proper balance calculation
        david.z_importkey(bob_privkey, "yes")  # Force rescan
        
        # Ensure all nodes are synced before checking balances
        safe_sync_all()
        generate_blocks_with_delay(miner, 1)
        safe_sync_all()
        
        # Find the imported address in david's wallet
        david_addresses = david.z_listaddresses()
        david_imported_addr = None
        for addr in david_addresses:
            if addr == bob_zaddr:
                david_imported_addr = addr
                break
        
        if david_imported_addr is None:
            raise AssertionError("Failed to find imported address in david's wallet")

        # Debug: Print balances to understand the discrepancy
        print(f"Bob's balance: {bob.z_getbalance(bob_zaddr)}")
        print(f"Bob's z-address: {bob_zaddr}")
        print(f"Charlie's balance: {charlie.z_getbalance(ipk_zaddr_str)}")
        print(f"Charlie's z-address: {ipk_zaddr_str}")
        print(f"David's balance: {david.z_getbalance(david_imported_addr)}")
        print(f"David's imported address: {david_imported_addr}")       
        print(f"Expected balance: {bob_balance}")

        # Check if amt bob spent is deducted for charlie and david
        assert_equal(charlie.z_getbalance(ipk_zaddr_str), bob_balance)
        assert_equal(david.z_getbalance(david_imported_addr), bob_balance)

if __name__ == '__main__':
    ZkeyImportExportTest().main()
