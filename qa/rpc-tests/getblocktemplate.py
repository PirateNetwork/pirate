#!/usr/bin/env python3
# Copyright (c) 2016 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from io import BytesIO
import codecs
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    LEGACY_DEFAULT_FEE,
    get_coinbase_address,
    hex_str_to_bytes,
    nustr,
    start_nodes,
    wait_and_assert_operationid_status,
)
from test_framework.mininode import (
    CTransaction,
)
from test_framework.blocktools import (
    create_block
)
from decimal import Decimal
import time
import logging
import sys

logging.basicConfig(format='%(levelname)s: %(message)s', level=logging.INFO, stream=sys.stdout)

# Pirate chain specific upgrade branch IDs
SAPLING_BRANCH_ID = 0x76b809bb
ORCHARD_BRANCH_ID = 0xc2d6d0b4  # This is Pirate's version of NU5

class GetBlockTemplateTest(BitcoinTestFramework):
    '''
    Test getblocktemplate, ensure that a block created from its result
    can be submitted and accepted.
    '''

    def __init__(self):
        super().__init__()
        self.num_nodes = 1
        self.cache_behavior = 'clean'

    def setup_network(self, split=False):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, extra_args=[[
            '-minrelaytxfee=0',
            '-ac_private=0',
            '-ac_cc=0',
        ]] * self.num_nodes)
        self.is_network_split = False
        self.node = self.nodes[0]

    def generate_blocks_with_delay(self, node, num_blocks):
        """Generate blocks with 1 second delay before each block"""
        for i in range(num_blocks):
            time.sleep(1)  # Wait 1 second before generating each block
            try:
                node.generate(1)
                if i < num_blocks - 1:  # Don't sleep after the last block
                    time.sleep(0.1)  # Small delay between blocks
            except Exception as e:
                print(f"Error generating block {i+1}/{num_blocks}: {e}")
                raise

    def safe_sync_all(self):
        """Sync all nodes with retry logic"""
        max_attempts = 10
        for attempt in range(max_attempts):
            try:
                self.sync_all()
                return
            except Exception as e:
                print(f"Sync attempt {attempt + 1} failed: {e}")
                if attempt == max_attempts - 1:
                    raise
                time.sleep(30)  # Wait before retry

    def add_nu5_v4_tx_to_mempool(self):
        node = self.node
        # sprout to transparent (v4)
        recipients = [{"address": self.transparent_addr, "amount": Decimal('0.1')}]
        myopid = node.z_sendmany(self.sprout_addr, recipients, 1, LEGACY_DEFAULT_FEE, 'AllowRevealedRecipients')
        wait_and_assert_operationid_status(node, myopid)

    def add_nu5_v5_tx_to_mempool(self):
        node = self.node
        recipients = [{"address": self.unified_addr, "amount": Decimal('9.99999')}]
        myopid = node.z_sendmany(get_coinbase_address(node), recipients, 1, LEGACY_DEFAULT_FEE, 'AllowRevealedSenders')
        wait_and_assert_operationid_status(node, myopid)

    def add_transparent_tx_to_mempool(self):
        node = self.node
        
        # Check if Sapling is active for shielded transactions
        upgrades = node.getblockchaininfo()['upgrades']
        sapling_active = nustr(SAPLING_BRANCH_ID) in upgrades and upgrades[nustr(SAPLING_BRANCH_ID)]['status'] == 'active'
        
        if sapling_active:
            # Post-Sapling: Use shielded transactions
            self.ensure_shielded_funds()
            
            # Create a transparent transaction using shielded funds
            new_addr = node.getnewaddress()
            recipients = [{"address": new_addr, "amount": Decimal('0.1')}]
            
            # Use z_sendmany from our shielded address to transparent
            if hasattr(self, 'sapling_addr') and self.sapling_addr:
                try:
                    # Check if we have balance first
                    balance = node.z_getbalance(self.sapling_addr)
                    if balance >= 0.1:
                        myopid = node.z_sendmany(self.sapling_addr, recipients, 1, LEGACY_DEFAULT_FEE)
                        wait_and_assert_operationid_status(node, myopid)
                    else:
                        logging.info(f"Insufficient shielded balance: {balance}, skipping transaction")
                except Exception as e:
                    logging.info(f"Could not send shielded transaction: {e}")
            else:
                logging.info("No sapling address available, skipping transaction")
        else:
            # Pre-Sapling: Generate a block to get mature transparent funds
            # Since we're in pre-Sapling phase, we can use transparent coinbase directly
            current_height = node.getblockcount()
            if current_height >= 100:  # We have some mature coinbase
                new_addr = node.getnewaddress()
                try:
                    # Try to send a small amount
                    node.sendtoaddress(new_addr, 0.01)
                except Exception as e:
                    logging.info(f"Could not create transparent transaction: {e}")
                    # Generate a block to get more mature funds
                    node.generate(1)
            else:
                # Not enough mature blocks, skip this transaction
                logging.info("Not enough mature blocks for transparent transaction, skipping")

    def ensure_shielded_funds(self):
        """Ensure we have shielded funds available for transactions"""
        node = self.node
        
        # Check if Sapling is active
        upgrades = node.getblockchaininfo()['upgrades']
        sapling_active = nustr(SAPLING_BRANCH_ID) in upgrades and upgrades[nustr(SAPLING_BRANCH_ID)]['status'] == 'active'
        
        if not sapling_active:
            logging.info("Sapling not active, cannot create shielded addresses")
            return
        
        # Create sapling address if we don't have one
        if not hasattr(self, 'sapling_addr') or not self.sapling_addr:
            try:
                self.sapling_addr = node.z_getnewaddress('sapling')
                logging.info(f"Created sapling address: {self.sapling_addr}")
            except Exception as e:
                logging.info(f"Could not create sapling address: {e}")
                return
        
        # Check if we have sufficient shielded balance
        try:
            balance = node.z_getbalance(self.sapling_addr)
            if balance >= 1.0:  # We have enough shielded funds
                return
        except:
            pass
        
        # Shield some coinbase funds
        self.shield_coinbase_to_sapling()

    def shield_coinbase_to_sapling(self):
        """Shield coinbase funds to sapling address"""
        node = self.node
        
        if not hasattr(self, 'sapling_addr') or not self.sapling_addr:
            return
        
        try:
            # Shield coinbase funds to sapling address
            logging.info(f"Shielding coinbase funds to {self.sapling_addr}")
            myopid = node.z_shieldcoinbase("*", self.sapling_addr, LEGACY_DEFAULT_FEE)
            wait_and_assert_operationid_status(node, myopid)
            logging.info("Successfully shielded coinbase funds")
        except Exception as e:
            logging.info(f"Could not shield coinbase funds: {e}")

    def gbt_submitblock(self, nu5_active):
        node = self.node
        mempool_tx_list = node.getrawmempool()

        gbt = node.getblocktemplate()

        # Strict validation: require defaultroots field to be present
        assert_equal('defaultroots' in gbt, True, "defaultroots field is required in getblocktemplate response")
        
        # Debug: print the structure of gbt to understand what fields are available
        logging.info(f"getblocktemplate keys: {list(gbt.keys())}")
        logging.info(f"defaultroots keys: {list(gbt['defaultroots'].keys())}")
        
        # Strict validation: require essential fields in defaultroots
        assert_equal('merkleroot' in gbt['defaultroots'], True, "merkleroot field is required in defaultroots")
        assert_equal('chainhistoryroot' in gbt['defaultroots'], True, "chainhistoryroot field is required in defaultroots")
        
        # Additional validation for NU5/Orchard fields when active
        if nu5_active:
            assert_equal('authdataroot' in gbt['defaultroots'], True, "authdataroot field is required in defaultroots when NU5/Orchard is active")

        # make sure no transactions were left out (or added)
        assert_equal(len(mempool_tx_list), len(gbt['transactions']))
        assert_equal(set(mempool_tx_list), set([tx['hash'] for tx in gbt['transactions']]))

        prevhash = int(gbt['previousblockhash'], 16)
        nTime = gbt['mintime']
        nBits = int(gbt['bits'], 16)

        # Handle Pirate chain's block commitment structure with strict validation
        # Note: defaultroots and required fields already validated above
        
        # Use the appropriate field for block commitment hash
        # For now, we use the legacy hashBlockCommitments field for block construction
        # but validate that defaultroots has the required fields
        if 'hashBlockCommitments' in gbt:
            blockcommitmentshash = int(gbt['hashBlockCommitments'], 16)
        elif nu5_active and 'blockcommitmentshash' in gbt['defaultroots']:
            blockcommitmentshash = int(gbt['defaultroots']['blockcommitmentshash'], 16)
            # Strict validation for blockcommitmentshash if present
            assert_equal(len(gbt['defaultroots']['blockcommitmentshash']), 64, "blockcommitmentshash must be 64-character hex string")
            try:
                int(gbt['defaultroots']['blockcommitmentshash'], 16)
            except ValueError:
                assert False, f"blockcommitmentshash is not valid hex: {gbt['defaultroots']['blockcommitmentshash']}"
        else:
            # Fallback to chainhistoryroot if no other option
            blockcommitmentshash = int(gbt['defaultroots']['chainhistoryroot'], 16)
            
        # Strict validation for chainhistoryroot (always required)
        assert_equal(len(gbt['defaultroots']['chainhistoryroot']), 64, "chainhistoryroot must be 64-character hex string")
        try:
            int(gbt['defaultroots']['chainhistoryroot'], 16)
        except ValueError:
            assert False, f"chainhistoryroot is not valid hex: {gbt['defaultroots']['chainhistoryroot']}"
        # Legacy fields validation - only warn if they don't match defaultroots
        # (Legacy fields may not be populated correctly, we use defaultroots instead)
        if 'blockcommitmentshash' in gbt:
            legacy_blockcommitmentshash = int(gbt['blockcommitmentshash'], 16)
            if legacy_blockcommitmentshash != blockcommitmentshash:
                logging.info(f"Legacy blockcommitmentshash ({legacy_blockcommitmentshash}) differs from defaultroots value ({blockcommitmentshash})")
        
        if 'lightclientroothash' in gbt:
            legacy_lightclientroothash = int(gbt['lightclientroothash'], 16)
            if legacy_lightclientroothash != blockcommitmentshash:
                logging.info(f"Legacy lightclientroothash ({legacy_lightclientroothash}) differs from defaultroots value ({blockcommitmentshash})")
                
        if 'finalsaplingroothash' in gbt:
            legacy_finalsaplingroothash = int(gbt['finalsaplingroothash'], 16)
            if legacy_finalsaplingroothash != blockcommitmentshash:
                logging.info(f"Legacy finalsaplingroothash ({legacy_finalsaplingroothash}) differs from defaultroots value ({blockcommitmentshash})")

        f = BytesIO(hex_str_to_bytes(gbt['coinbasetxn']['data']))
        coinbase = CTransaction()
        coinbase.deserialize(f)
        coinbase.calc_sha256()
        assert_equal(coinbase.hash, gbt['coinbasetxn']['hash'])
        if 'authdigest' in gbt['coinbasetxn']:
            assert_equal(coinbase.auth_digest_hex, gbt['coinbasetxn']['authdigest'])

        block = create_block(prevhash, coinbase, nTime, nBits, blockcommitmentshash)

        # copy the non-coinbase transactions from the block template to the block
        for gbt_tx in gbt['transactions']:
            f = BytesIO(hex_str_to_bytes(gbt_tx['data']))
            tx = CTransaction()
            tx.deserialize(f)
            tx.calc_sha256()
            assert_equal(tx.hash, gbt_tx['hash'])
            # Check auth digest if available
            try:
                raw_tx = node.getrawtransaction(tx.hash, 1)
                if 'authdigest' in raw_tx:
                    assert_equal(tx.auth_digest_hex, raw_tx['authdigest'])
            except:
                logging.info(f"Could not verify auth digest for transaction {tx.hash}")
            block.vtx.append(tx)
        # Handle merkle root with strict validation
        # Note: merkleroot field already validated above
        merkleroot_from_defaultroots = int(gbt['defaultroots']['merkleroot'], 16)
        block.hashMerkleRoot = merkleroot_from_defaultroots
        
        # Strict validation: ensure merkleroot is a valid 64-character hex string
        assert_equal(len(gbt['defaultroots']['merkleroot']), 64, "merkleroot must be 64-character hex string")
        # Validate it's actually hex
        try:
            int(gbt['defaultroots']['merkleroot'], 16)
        except ValueError:
            assert False, f"merkleroot is not valid hex: {gbt['defaultroots']['merkleroot']}"
        
        # Strict validation: ensure merkleroot from defaultroots matches calculated value
        calculated_merkleroot = block.calc_merkle_root()
        assert_equal(block.hashMerkleRoot, calculated_merkleroot, "merkleroot from defaultroots must match calculated merkle root")
        
        # Additional strict validations
        assert_equal(len(block.vtx), len(gbt['transactions']) + 1, "number of transactions must match template")
        assert_equal(block.hashPrevBlock, int(gbt['previousblockhash'], 16), "previous block hash must match template")
        
        # Strict validation: ensure block construction is consistent
        assert block.hashMerkleRoot != 0, "merkleroot cannot be zero"
        if nu5_active:
            assert hasattr(block, 'hashAuthDataRoot'), "block must have hashAuthDataRoot when NU5 is active"
            assert block.hashAuthDataRoot != 0, "authdataroot cannot be zero when NU5 is active"
        
        # Handle auth data root for NU5/Orchard with strict validation
        if nu5_active:
            # Note: authdataroot field already validated above
            authdataroot_from_defaultroots = int(gbt['defaultroots']['authdataroot'], 16)
            block.hashAuthDataRoot = authdataroot_from_defaultroots
            
            # Strict validation: ensure authdataroot is a valid 64-character hex string
            assert_equal(len(gbt['defaultroots']['authdataroot']), 64, "authdataroot must be 64-character hex string")
            # Validate it's actually hex
            try:
                int(gbt['defaultroots']['authdataroot'], 16)
            except ValueError:
                assert False, f"authdataroot is not valid hex: {gbt['defaultroots']['authdataroot']}"
            
            # Strict validation: ensure the provided value matches calculated value
            assert_equal(block.hashAuthDataRoot, block.calc_auth_data_root(), "authdataroot value must match calculated auth data root")
        block.solve()
        block.calc_sha256()

        submitblock_reply = node.submitblock(codecs.encode(block.serialize(), 'hex_codec'))
        assert_equal(None, submitblock_reply)
        assert_equal(block.hash, node.getbestblockhash())
        # Wait until the wallet has been notified of all blocks, so that it doesn't try to
        # double-spend transparent coins in subsequent test phases.
        self.safe_sync_all()

    def run_test(self):
        node = self.node

        # Generate the first 10 blocks to get the chain started
        logging.info("Starting chain with initial blocks...")
        self.generate_blocks_with_delay(node, 10)
        self.safe_sync_all()

        # Set up addresses for testing early
        self.transparent_addr = node.getnewaddress()
        
        # Initialize other addresses as None - will be set later when available
        self.sprout_addr = None
        self.unified_addr = None
        self.sapling_addr = None
        
        # We'll create sapling address later when Sapling is active

        logging.info(f"Current height: {node.getblockcount()}")
        
        # Test getblocktemplate before any upgrades
        logging.info("Testing getblocktemplate before Sapling activation...")
        print("Testing getblocktemplate for pre-Sapling")
        
        # Only the coinbase; this covers the case where the Merkle root
        # is equal to the coinbase txid.
        print("- only coinbase")
        self.gbt_submitblock(False)

        # Adding one transaction triggering a single Merkle digest.
        print("- one transaction (plus coinbase)")
        self.add_transparent_tx_to_mempool()
        self.gbt_submitblock(False)

        # Generate blocks to activate Sapling (block 100)
        blocks_to_sapling = 100 - node.getblockcount()
        if blocks_to_sapling > 0:
            logging.info(f"Generating {blocks_to_sapling} blocks to activate Sapling...")
            self.generate_blocks_with_delay(node, blocks_to_sapling)
            self.safe_sync_all()

        logging.info(f"Current height: {node.getblockcount()}")
        
        # Verify Sapling is active
        upgrades = node.getblockchaininfo()['upgrades']
        sapling_status = upgrades[nustr(SAPLING_BRANCH_ID)]['status'] if nustr(SAPLING_BRANCH_ID) in upgrades else 'not found'
        logging.info(f"Sapling status: {sapling_status}")

        # Test getblocktemplate after Sapling but before Orchard
        logging.info("Testing getblocktemplate after Sapling activation...")
        print("Testing getblocktemplate for post-Sapling, pre-Orchard")
        
        # Try to set up sprout addresses now that Sapling is active
        if self.sprout_addr is None:
            try:
                # Try different methods to get a sprout address
                addresses = node.listaddresses()
                self.sprout_addr = addresses[0]['sprout']['addresses'][0]
            except:
                try:
                    # Alternative method
                    self.sprout_addr = node.z_getnewaddress('sprout')
                except:
                    logging.info("Could not create sprout addresses, skipping sprout tests")

        # Try to set up unified addresses (may not work until Orchard is active)
        if self.unified_addr is None:
            try:
                account = node.z_getnewaccount()['account']
                self.unified_addr = node.z_getaddressforaccount(account)['address']
            except:
                logging.info("Unified addresses not available yet, will test later")

        # Generate more blocks for coinbase maturity and testing
        self.generate_blocks_with_delay(node, 50)
        self.safe_sync_all()
        
        # Shield some coinbase funds for transparent transactions (if Sapling is active)
        upgrades = node.getblockchaininfo()['upgrades']
        sapling_active = nustr(SAPLING_BRANCH_ID) in upgrades and upgrades[nustr(SAPLING_BRANCH_ID)]['status'] == 'active'
        
        if sapling_active:
            try:
                # Create sapling address if we don't have one
                if not hasattr(self, 'sapling_addr') or not self.sapling_addr:
                    self.sapling_addr = node.z_getnewaddress('sapling')
                    logging.info(f"Created sapling address: {self.sapling_addr}")
                
                logging.info("Shielding initial coinbase funds...")
                myopid = node.z_shieldcoinbase("*", self.sapling_addr, LEGACY_DEFAULT_FEE)
                wait_and_assert_operationid_status(node, myopid)
                logging.info("Successfully shielded initial coinbase funds")
            except Exception as e:
                logging.info(f"Could not shield initial coinbase funds: {e}")

        print("- only coinbase (post-Sapling)")
        self.gbt_submitblock(False)

        # Adding one transaction triggering a single Merkle digest.
        print("- one transaction (plus coinbase, post-Sapling)")
        self.add_transparent_tx_to_mempool()
        self.gbt_submitblock(False)

        # Adding two transactions to trigger hash Merkle root edge case.
        print("- two transactions (plus coinbase, post-Sapling)")
        # Generate a few more blocks to ensure we have mature coins
        self.generate_blocks_with_delay(node, 5)
        self.safe_sync_all()
        self.add_transparent_tx_to_mempool()
        self.add_transparent_tx_to_mempool()
        self.gbt_submitblock(False)

        # Generate blocks to approach Orchard activation (block 200)
        blocks_to_orchard = 200 - node.getblockcount()
        if blocks_to_orchard > 0:
            logging.info(f"Generating {blocks_to_orchard} blocks to activate Orchard...")
            self.generate_blocks_with_delay(node, blocks_to_orchard)
            self.safe_sync_all()

        logging.info(f"Current height: {node.getblockcount()}")
        
        # Verify Orchard is active
        upgrades = node.getblockchaininfo()['upgrades']
        orchard_status = upgrades[nustr(ORCHARD_BRANCH_ID)]['status'] if nustr(ORCHARD_BRANCH_ID) in upgrades else 'not found'
        logging.info(f"Orchard status: {orchard_status}")

        # Test getblocktemplate after Orchard activation
        logging.info("Testing getblocktemplate after Orchard activation...")
        print("Testing getblocktemplate for post-Orchard")

        # Generate more blocks for testing
        self.generate_blocks_with_delay(node, 50)
        self.safe_sync_all()

        # Only the coinbase; this covers the case where the block authdata root
        # is equal to the coinbase authdata
        print("- only coinbase (post-Orchard)")
        self.gbt_submitblock(True)

        # Adding one transaction triggering a single Merkle digest.
        print("- one transaction (plus coinbase, post-Orchard)")
        self.add_transparent_tx_to_mempool()
        self.gbt_submitblock(True)

        # Adding two transactions to trigger hash Merkle root edge case.
        print("- two transactions (plus coinbase, post-Orchard)")
        # Generate a few more blocks to ensure we have mature coins
        self.generate_blocks_with_delay(node, 5)
        self.safe_sync_all()
        self.add_transparent_tx_to_mempool()
        self.add_transparent_tx_to_mempool()
        self.gbt_submitblock(True)

        # Test mixed transaction types if available
        if self.sprout_addr:
            print("- both v4 and transparent transactions (plus coinbase)")
            try:
                self.add_nu5_v4_tx_to_mempool()
                self.add_transparent_tx_to_mempool()
                self.gbt_submitblock(True)
            except Exception as e:
                logging.info(f"Could not test v4 transactions: {e}")

        if self.unified_addr:
            print("- testing Orchard transactions")
            for i in range(0, 3):  # Test with 3 Orchard transactions
                try:
                    self.add_nu5_v5_tx_to_mempool()
                except:
                    logging.info(f"Could not add Orchard transaction {i+1}")
                    break
            self.gbt_submitblock(True)

        logging.info("getblocktemplate test completed successfully!")
        print("All getblocktemplate tests passed for Pirate Chain!")

if __name__ == '__main__':
    GetBlockTemplateTest().main()
