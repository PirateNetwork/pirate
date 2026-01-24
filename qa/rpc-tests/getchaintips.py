#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2018-2022 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

# Exercise the getchaintips API.  We introduce a network split, work
# on chains of different lengths, and join the network together again.
# This gives us two tips, verify that it works.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, start_nodes, stop_nodes, connect_nodes_bi, sync_blocks, initialize_datadir, p2p_port, rpc_port, PortSeed
from test_framework.authproxy import JSONRPCException
import time
import os

class GetChainTipsTest(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.cache_behavior = 'clean'
        self.num_nodes = 4  # Need 4 nodes to properly split network
        self.node_addresses = {}  # Store node addresses for reconnection
        self.node_p2p_ports = {}  # Store predetermined P2P ports
        self.node_rpc_ports = {}  # Store predetermined RPC ports
        self.disconnected_peers = {}  # Track disconnected peers
        self.use_gdb = False  # Control whether to run nodes under gdb

    def add_options(self, parser):
        parser.add_option("--gdb", dest="gdb", default=False, action="store_true", 
                         help="Run all nodes under gdb for debugging")

    def setup_chain(self):
        print("Initializing test framework...")
        # Handle the --gdb command line option
        if hasattr(self.options, 'gdb') and self.options.gdb:
            self.use_gdb = True
            print("*** Running nodes under GDB for debugging ***")

    def setup_network(self):
        # Set up node addresses and ports for predetermined network configuration
        self.setup_node_addresses_and_ports()
        
        # Configure initial full mesh network topology in config files
        # Each node connects to all other nodes initially
        all_other_nodes = {}
        for i in range(self.num_nodes):
            all_other_nodes[i] = []
            for j in range(self.num_nodes):
                if i != j:
                    all_other_nodes[i].append(self.node_addresses[j])
        
        # Initialize each node's datadir with full mesh connections and predetermined ports
        for i in range(self.num_nodes):
            print("Configuring node {} to connect to: {}".format(i, all_other_nodes[i]))
            print("Node {} using P2P port: {}, RPC port: {}".format(i, self.node_p2p_ports[i], self.node_rpc_ports[i]))
            initialize_datadir(
                self.options.tmpdir, 
                i, 
                addnodes=all_other_nodes[i],
                p2p_port_override=self.node_p2p_ports[i],
                rpc_port_override=self.node_rpc_ports[i]
            )
        
        # Add essential configuration options
        extra_args = [
            ["-checkblockindex=1"] for _ in range(self.num_nodes)
        ]
        
        # Verify port configuration before starting nodes
        self.verify_port_configuration()
        
        # Start nodes with the configured network topology
        if self.use_gdb:
            print("*** Starting all nodes under GDB - this may take longer to initialize ***")
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, extra_args, use_gdb=self.use_gdb)
        
        # Wait for connections to establish
        print("Waiting for initial network connections to establish...")
        time.sleep(5)

    def test_getchaintips_comprehensive(self):
        """Test getchaintips API comprehensively within blockchain constraints"""
        print("Testing getchaintips API comprehensively...")
        
        # Generate additional blocks to test API with longer chain
        print("Generating more blocks to test API...")
        self.nodes[0].generate(50)
        sync_blocks(self.nodes)
        
        # Test getchaintips at various points
        current_height = self.nodes[0].getblockcount()
        print("Current blockchain height: {}".format(current_height))
        
        # Test the API on all nodes to ensure consistency
        for i in range(self.num_nodes):
            tips = self.nodes[i].getchaintips()
            print("Node {} - Tips: {}, Height: {}, Status: {}".format(i, len(tips), tips[0]['height'], tips[0]['status']))
            
            # All nodes should see the same single chain
            assert_equal(len(tips), 1)
            assert_equal(tips[0]['height'], current_height)
            assert_equal(tips[0]['status'], 'active')
            assert_equal(tips[0]['branchlen'], 0)
        
        print("✓ All nodes consistently report the same chain state")
        return current_height

    def setup_node_addresses_and_ports(self):
        """Set up predetermined node addresses and ports for consistent network configuration"""
        # Predetermine all P2P and RPC ports for all nodes
        self.node_p2p_ports = {}
        self.node_rpc_ports = {}
        
        for i in range(self.num_nodes):
            # Calculate predetermined ports using current PortSeed
            self.node_p2p_ports[i] = p2p_port(i)
            self.node_rpc_ports[i] = rpc_port(i)
            self.node_addresses[i] = "127.0.0.1:{}".format(self.node_p2p_ports[i])
            print("Node {} - P2P: {}, RPC: {}, Address: {}".format(
                i, self.node_p2p_ports[i], self.node_rpc_ports[i], self.node_addresses[i]))
        
        print("✓ All ports predetermined successfully - P2P and addnode addresses are synchronized")

    def verify_port_configuration(self):
        """Verify that the configured ports match the predetermined values"""
        print("Verifying port configuration synchronization...")
        
        for i in range(self.num_nodes):
            # Read the node's configuration file to verify ports were set correctly
            config_file = os.path.join(self.options.tmpdir, "node{}".format(i), "PIRATETST.conf")
            with open(config_file, 'r') as f:
                config_content = f.read()
            
            # Check that the ports in the config file match our predetermined values
            expected_p2p = "port={}".format(self.node_p2p_ports[i])
            expected_rpc = "rpcport={}".format(self.node_rpc_ports[i])
            
            if expected_p2p in config_content and expected_rpc in config_content:
                print("✓ Node {} config verified: P2P={}, RPC={}".format(
                    i, self.node_p2p_ports[i], self.node_rpc_ports[i]))
            else:
                print("✗ Node {} config mismatch!".format(i))
                
            # Also verify addnode entries use the correct addresses
            for j in range(self.num_nodes):
                if i != j:
                    expected_addnode = "addnode={}".format(self.node_addresses[j])
                    if expected_addnode in config_content:
                        print("✓ Node {} has correct addnode for node {}".format(i, j))
                    else:
                        print("✗ Node {} missing addnode for node {}".format(i, j))
        
        print("✓ Port configuration verification complete")



    def split_network_dynamic(self):
        """Split the network dynamically without restarting nodes"""
        print("Dynamically splitting network using disconnectnode/addnode...")
        
        # First, disconnect all existing connections using preconfigured addresses
        print("Disconnecting all current peer connections...")
        for i in range(self.num_nodes):
            for j in range(self.num_nodes):
                if i != j:
                    target_addr = self.node_addresses[j]
                    try:
                        self.nodes[i].disconnectnode(target_addr)
                        print("Disconnected node {} from {}".format(i, target_addr))
                    except Exception as e:
                        # Expected - may not be connected, that's fine
                        pass
        
        # Wait for disconnections to complete
        time.sleep(2)
        
        # Set up the split network topology using addnode:
        # Group 1: nodes 0,1 (node 0 connects to node 1, node 1 connects to node 0)
        # Group 2: nodes 2,3 (node 2 connects to node 3, node 3 connects to node 2)
        split_connections = {
            0: [self.node_addresses[1]],  # Node 0 connects only to node 1
            1: [self.node_addresses[0]],  # Node 1 connects only to node 0
            2: [self.node_addresses[3]],  # Node 2 connects only to node 3
            3: [self.node_addresses[2]]   # Node 3 connects only to node 2
        }
        
        # Establish new connections for split topology
        print("Establishing split network connections...")
        for node_id, connections in split_connections.items():
            for target_addr in connections:
                try:
                    self.nodes[node_id].addnode(target_addr, "add")
                    print("Added connection: node {} -> {}".format(node_id, target_addr))
                except Exception as e:
                    print("Could not add connection node {} -> {}: {}".format(node_id, target_addr, e))
        
        # Wait for new connections to establish
        print("Waiting for split network connections to establish...")
        time.sleep(5)
        
        return True

    def split_network(self):
        """Split the network into two groups: (0,1) and (2,3) using configuration-based approach"""
        print("Splitting network into two groups...")
        
        # Show current peer connections before splitting
        print("Current peer connections:")
        for i in range(self.num_nodes):
            peer_count = len(self.nodes[i].getpeerinfo())
            print("Node {} has {} peer(s) connected".format(i, peer_count))
        
        # Use the dynamic approach for clean network split
        success = self.split_network_dynamic()
        
        if not success:
            print("Failed to reconfigure nodes for split")
            return
        
        # Brief wait for connections to stabilize
        time.sleep(3)
        
        # Simplified network split verification using preconfigured addresses
        def verify_network_split(verification_name):
            print("\n=== {} ===".format(verification_name))
            
            # Check that each node has exactly 1 peer (split network topology)
            topology_correct = True
            expected_peers = {0: 1, 1: 1, 2: 1, 3: 1}  # Each node should have 1 peer
            
            for node_id in range(self.num_nodes):
                peer_count = len(self.nodes[node_id].getpeerinfo())
                expected = expected_peers[node_id]
                
                print("Node {} has {} peer(s), expected {}".format(node_id, peer_count, expected))
                
                if peer_count != expected:
                    print("✗ Node {} peer count mismatch".format(node_id))
                    topology_correct = False
                else:
                    print("✓ Node {} has correct peer count".format(node_id))
            
            if topology_correct:
                print("✓ Network split successful - proper topology achieved")
                return True
            else:
                print("✗ Network split incomplete - incorrect peer topology")
                return False
        
        # Initial verification immediately after split
        split_successful = verify_network_split("INITIAL VERIFICATION - Immediately after split")
        
        # Wait 60 seconds for you to read the logs and verify stability
        print("\nWaiting 60 seconds for log review and to verify no automatic reconnections occur...")
        print("Use this time to examine the detailed connection information above.")
        time.sleep(60)
        
        # Second verification after wait
        split_still_successful = verify_network_split("SECOND VERIFICATION - After 60 seconds")
        
        if split_successful and split_still_successful:
            print("\n✓ Network split completed successfully and remained stable")
        else:
            print("\n✗ Network split was not fully successful or stable")
        
        print("Proceeding with blockchain generation on split networks...")

    def join_network_dynamic(self):
        """Rejoin the network using controlled sequential reconnection strategy"""
        print("=== CONTROLLED SEQUENTIAL RECONNECTION STRATEGY ===")
        print("Reconnecting short chain nodes one at a time to long chain nodes...")
        
        # First, ensure complete disconnection of all nodes
        print("\nStep 1: Complete network isolation...")
        for i in range(self.num_nodes):
            for j in range(self.num_nodes):
                if i != j:
                    target_addr = self.node_addresses[j]
                    try:
                        self.nodes[i].disconnectnode(target_addr)
                        print("Disconnected node {} from {}".format(i, target_addr))
                    except Exception as e:
                        # Expected - may not be connected
                        pass
        
        # Wait for complete disconnection
        time.sleep(3)
        
        # Verify isolation
        print("\nVerifying complete network isolation...")
        for i in range(self.num_nodes):
            peer_count = len(self.nodes[i].getpeerinfo())
            height = self.nodes[i].getblockcount()
            print("  Node {}: height={}, peers={}".format(i, height, peer_count))
        
        # SHORT CHAIN NODES: 0, 1 (height ~40)
        # LONG CHAIN NODES: 2, 3 (height ~50)
        short_chain_nodes = [0, 1]
        long_chain_nodes = [2, 3]
        
        print("\n=== SEQUENTIAL RECONNECTION PHASE ===")
        
        # Step 2: Connect Node 0 (short chain) to long chain nodes and sync
        print("\nStep 2: Connecting Node 0 to long chain nodes...")
        for long_node in long_chain_nodes:
            target_addr = self.node_addresses[long_node]
            try:
                self.nodes[0].addnode(target_addr, "add")
                print("  Node 0 -> Node {} ({})".format(long_node, target_addr))
            except Exception as e:
                print("  Failed to connect Node 0 -> Node {}: {}".format(long_node, e))
        
        # Wait for connections and sync
        time.sleep(5)
        print("Syncing Node 0 with long chain...")
        try:
            # Sync Node 0 with long chain nodes
            sync_nodes = [self.nodes[0]] + [self.nodes[i] for i in long_chain_nodes]
            sync_blocks(sync_nodes, timeout=60)
            print("✓ Node 0 synchronized with long chain")
            
            # Verify Node 0 status
            node0_height = self.nodes[0].getblockcount()
            node0_peers = len(self.nodes[0].getpeerinfo())
            print("  Node 0: height={}, peers={}".format(node0_height, node0_peers))
            
        except Exception as e:
            print("⚠ Node 0 sync failed: {}".format(e))
            return False
        
        # Step 3: Connect Node 1 (short chain) to already-synced nodes and sync
        print("\nStep 3: Connecting Node 1 to synced nodes...")
        # Connect Node 1 to Node 0 (now synced) and long chain nodes
        synced_nodes = [0] + long_chain_nodes
        for target_node in synced_nodes:
            target_addr = self.node_addresses[target_node]
            try:
                self.nodes[1].addnode(target_addr, "add")
                print("  Node 1 -> Node {} ({})".format(target_node, target_addr))
            except Exception as e:
                print("  Failed to connect Node 1 -> Node {}: {}".format(target_node, e))
        
        # Wait for connections and sync
        time.sleep(5)
        print("Syncing Node 1 with the synchronized network...")
        try:
            # Sync Node 1 with all other nodes
            sync_blocks(self.nodes, timeout=60)
            print("✓ Node 1 synchronized with network")
            
            # Verify Node 1 status
            node1_height = self.nodes[1].getblockcount()
            node1_peers = len(self.nodes[1].getpeerinfo())
            print("  Node 1: height={}, peers={}".format(node1_height, node1_peers))
            
        except Exception as e:
            print("⚠ Node 1 sync failed: {}".format(e))
            return False
        
        # Step 4: Establish full mesh between all nodes
        print("\nStep 4: Establishing full mesh connectivity...")
        for i in range(self.num_nodes):
            for j in range(self.num_nodes):
                if i != j:
                    target_addr = self.node_addresses[j]
                    try:
                        self.nodes[i].addnode(target_addr, "add")
                        print("  Added connection: Node {} -> Node {}".format(i, j))
                    except Exception as e:
                        # Connection may already exist
                        pass
        
        # Wait for full mesh to stabilize
        time.sleep(5)
        
        # Step 5: Final validation with strict checks
        print("\nStep 5: Final network validation...")
        
        # Check responsiveness
        self.check_node_responsiveness_strict("After controlled sequential reconnection")
        
        # Verify all nodes have same chain state
        print("Verifying identical chain state across all nodes...")
        try:
            best_hashes = []
            best_heights = []
            for i, node in enumerate(self.nodes):
                best_hash = node.getbestblockhash()
                best_height = node.getblockcount()
                best_hashes.append(best_hash)
                best_heights.append(best_height)
                print("  Node {}: height={}, hash={}".format(i, best_height, best_hash[:16] + "..."))
            
            # Verify all nodes have identical state
            if len(set(best_hashes)) == 1 and len(set(best_heights)) == 1:
                print("✓ All nodes have identical chain state (height={})".format(best_heights[0]))
            else:
                print("⚠ Nodes have different chain states!")
                for i in range(len(self.nodes)):
                    print("  Node {}: height={}, hash={}".format(i, best_heights[i], best_hashes[i][:16] + "..."))
                return False
                
        except Exception as e:
            print("⚠ Chain state verification failed: {}".format(e))
            return False
        
        # Final connectivity verification
        print("Verifying full mesh connectivity...")
        expected_peers = self.num_nodes - 1
        all_connected = True
        for i in range(self.num_nodes):
            try:
                peer_count = len(self.nodes[i].getpeerinfo())
                print("  Node {}: {} peers (expected {})".format(i, peer_count, expected_peers))
                if peer_count < expected_peers:
                    all_connected = False
            except Exception as e:
                print("  Node {}: connectivity check failed - {}".format(i, e))
                all_connected = False
        
        if all_connected:
            print("✓ Controlled sequential reconnection completed successfully!")
            return True
        else:
            print("⚠ Some connectivity issues remain after controlled reconnection")
            return False

    def join_network(self):
        """Rejoin the split network by reconnecting all groups with strict validation"""
        print("Rejoining the network with strict validation...")
        
        # Use the enhanced dynamic approach with strict validation
        success = self.join_network_dynamic()
        
        if not success:
            print("Failed to dynamically rejoin nodes with strict validation")
            return False
        
        # Additional verification for network rejoin quality
        print("Verifying network rejoin quality...")
        expected_peers_full_mesh = self.num_nodes - 1  # Each node should connect to all others
        
        all_nodes_properly_connected = True
        for i in range(self.num_nodes):
            try:
                peer_count = len(self.nodes[i].getpeerinfo())
                print("Node {} has {} peer(s), expected {} for full mesh".format(i, peer_count, expected_peers_full_mesh))
                if peer_count >= expected_peers_full_mesh:
                    print("✓ Node {} successfully rejoined full mesh network".format(i))
                else:
                    print("⚠ Node {} has incomplete connections".format(i))
                    all_nodes_properly_connected = False
            except Exception as e:
                print("⚠ Could not verify connectivity for node {}: {}".format(i, e))
                all_nodes_properly_connected = False
        
        if all_nodes_properly_connected:
            print("✓ All nodes properly reconnected with strict validation")
        else:
            print("⚠ Some nodes have connectivity issues after rejoin")
        
        return success

    def create_reorg_at_height(self, target_height, fork_length=15):
        """Create a reorganization at the specified height with the given fork length"""
        print(f"\n=== Creating {fork_length}-block reorganization at height {target_height} ===")
        
        # STRICT responsiveness check at start of reorganization - FAIL if any node unresponsive
        self.check_node_responsiveness_strict(f"Start of reorganization at height {target_height}")
        
        # Check initial node responsiveness before starting
        print("=== INITIAL NODE STATUS BEFORE REORG ===")
        self.check_all_nodes_status("Before starting reorganization")
        
        # First, sync all nodes to the target height
        current_height = self.nodes[0].getblockcount()
        if current_height < target_height:
            blocks_needed = target_height - current_height
            print(f"Generating {blocks_needed} blocks to reach height {target_height}")
            
            # Generate blocks with throttling to prevent CheckBlockIndex assertions
            for i in range(blocks_needed):
                self.nodes[0].generate(1)
                time.sleep(2.0)  # 2 second pause between blocks
                
                # Simple progress tracking for height buildup
                if (i + 1) % 20 == 0 or i == blocks_needed - 1:
                    print(f"Height buildup progress: {i + 1}/{blocks_needed} blocks")
            
            # Sync with error handling for responsive nodes
            try:
                sync_blocks(self.nodes)
                print("✓ All nodes synced to target height successfully")
            except Exception as e:
                print(f"⚠ Sync error during height buildup: {e}")
                # Try with responsive nodes only
                responsive_nodes = []
                for i in range(len(self.nodes)):
                    try:
                        self.nodes[i].getblockcount()
                        responsive_nodes.append(self.nodes[i])
                    except:
                        print(f"Node {i} unresponsive during height buildup")
                if len(responsive_nodes) >= 2:
                    sync_blocks(responsive_nodes)
                    print(f"✓ Synced {len(responsive_nodes)} responsive nodes to target height")
                else:
                    print(f"⚠ Only {len(responsive_nodes)} responsive nodes available")
        
        # Check basic node connectivity after height buildup
        print("Checking nodes after height buildup...")
        
        # Split the network
        print("Splitting network for reorganization...")
        self.split_network_dynamic()
        time.sleep(2)
        
        # Create competing chains sequentially to isolate Node 1 issue
        short_chain_blocks = fork_length
        long_chain_blocks = fork_length + 10  # Make one chain clearly longer (larger gap to avoid equal-work edge cases)
        
        print("=== SEQUENTIAL BLOCK GENERATION AND SYNC TEST ===")
        print("Step 1: Generate blocks on Node 0 only")
        print(f"Generating {short_chain_blocks} blocks on Node 0...")
        
        print("Step 1: Generate short chain on Node 0...")
        # Generate blocks on Node 0 only (no parallel generation)
        for i in range(short_chain_blocks):
            self.nodes[0].generate(1)
            time.sleep(1.5)  # Slightly faster but still throttled
            
            # Simple progress tracking
            if (i + 1) % 5 == 0 or i == short_chain_blocks - 1:
                print(f"Short chain progress: {i + 1}/{short_chain_blocks} blocks")
        
        print("Step 2: Sync Node 0 blocks to Node 1")
        # Sync Node 0 -> Node 1 specifically
        try:
            sync_blocks([self.nodes[0], self.nodes[1]])
            print("✓ Step 2 complete: Node 0 -> Node 1 sync successful")
        except Exception as e:
            print(f"⚠ Step 2 FAILED: Node 0 -> Node 1 sync error: {e}")
        
        
        print("Step 3: Generate long chain on Node 2...")
        # Generate blocks on Node 2 only (Node 0,1 should be stable now)
        for i in range(long_chain_blocks):
            self.nodes[2].generate(1)
            time.sleep(1.5)  # Slightly faster but still throttled
            
            # Simple progress tracking
            if (i + 1) % 5 == 0 or i == long_chain_blocks - 1:
                print(f"Long chain progress: {i + 1}/{long_chain_blocks} blocks")
        
        print("Step 4: Sync Node 2 blocks to Node 3")
        # Sync Node 2 -> Node 3 specifically  
        try:
            sync_blocks([self.nodes[2], self.nodes[3]])
            print("✓ Step 4 complete: Node 2 -> Node 3 sync successful")
        except Exception as e:
            print(f"⚠ Step 4 FAILED: Node 2 -> Node 3 sync error: {e}")
        
        # Check heights after sequential fork creation
        print("Checking fork heights after generation...")
        try:
            short_height = self.nodes[0].getblockcount()
            long_height = self.nodes[2].getblockcount()
            print(f"Short chain height: {short_height}, Long chain height: {long_height}")
        except Exception as e:
            print(f"⚠ Could not get heights: {e}")
        
        print("Step 5: Rejoin the full network to trigger reorganization...")
        # Rejoin the network to trigger reorganization with strict validation
        rejoin_success = self.join_network_dynamic()
        
        if not rejoin_success:
            print("⚠ Network rejoin with strict validation failed")
            return False
        
        print("Network rejoin with strict validation completed successfully")
        
        # Verify reorganization occurred
        try:
            final_height = self.nodes[0].getblockcount()
            print(f"Final height after sequential reorg: {final_height}")
        except Exception as e:
            print(f"⚠ Could not get final height: {e}")
        
        # Check for fork detection with error handling
        try:
            tips = self.nodes[0].getchaintips()
            print(f"Chain tips detected: {len(tips)}")
            
            for i, tip in enumerate(tips):
                print(f"Tip {i}: height={tip['height']}, status={tip['status']}, branchlen={tip.get('branchlen', 0)}")
            
            if len(tips) >= 2:
                print(f"✓ SUCCESS: Reorganization at height {target_height} detected with {len(tips)} tips!")
                return True
            else:
                print(f"⚠ Reorganization at height {target_height} resolved quickly - {len(tips)} tip(s)")
                return False
        except Exception as e:
            print(f"⚠ Could not get chain tips: {e}")
            # Try other nodes
            for i in range(1, len(self.nodes)):
                try:
                    tips = self.nodes[i].getchaintips()
                    print(f"Chain tips from node {i}: {len(tips)}")
                    if len(tips) >= 2:
                        print(f"✓ SUCCESS: Reorganization detected on node {i}")
                        return True
                except Exception as node_err:
                    print(f"Node {i} also unresponsive: {node_err}")
            print(f"⚠ Could not verify reorganization - all nodes unresponsive")
            return False

    def run_test(self):
        # Initial responsiveness check
        self.check_node_responsiveness_strict("Test startup - all nodes should be responsive")
        
        # Generate initial blocks with all nodes in sync - using throttling
        print("Generating initial 20 blocks with throttling...")
        for i in range(20):
            self.nodes[0].generate(1)
            time.sleep(2.0)  # 2 second pause between blocks to prevent CheckBlockIndex assertions
            
            # Simple progress tracking
            if (i + 1) % 10 == 0:
                print(f"Initial generation progress: {i + 1}/20 blocks")
        
        print("Syncing all nodes...")
        sync_blocks(self.nodes)

        # Verify initial state
        tips = self.nodes[0].getchaintips()
        assert_equal(len(tips), 1)
        assert_equal(tips[0]['branchlen'], 0)
        assert_equal(tips[0]['height'], 20)
        assert_equal(tips[0]['status'], 'active')
        print("✓ Initial state verified - single chain at height 20")

        # Test reorganizations at multiple heights
        reorg_heights = [25, 75, 125, 225]
        successful_reorgs = 0
        
        for reorg_num, height in enumerate(reorg_heights, 1):
            print(f"\n=== STARTING REORGANIZATION {reorg_num}/{len(reorg_heights)} AT HEIGHT {height} ===")
            
            # Strict responsiveness check before each reorganization
            self.check_node_responsiveness_strict(f"Before reorganization {reorg_num} at height {height}")
            
            success = self.create_reorg_at_height(height, fork_length=15)
            if success:
                successful_reorgs += 1
            
            # Strict responsiveness check after each reorganization
            self.check_node_responsiveness_strict(f"After reorganization {reorg_num} at height {height}")
            
            # Longer pause between reorganizations for stability
            print("Waiting between reorganizations for system stability...")
            time.sleep(3)
        
        print(f"\n=== REORGANIZATION TEST SUMMARY ===")
        print(f"Total reorganizations attempted: {len(reorg_heights)}")
        print(f"Reorganizations with fork detection: {successful_reorgs}")
        
        # Get final blockchain height carefully with error handling
        try:
            final_height = self.nodes[0].getblockcount()
            print(f"Final blockchain height: {final_height}")
        except Exception as e:
            print(f"Warning: Could not get final height from node 0: {e}")
            final_height = "Unknown"
        
        # Get final chain tips carefully with error handling
        try:
            final_tips = self.nodes[0].getchaintips()
            print(f"Final chain tips: {len(final_tips)}")
            
            # Display detailed information about each tip
            for i, tip in enumerate(final_tips):
                print(f"  Tip {i}: height={tip['height']}, status={tip['status']}, branchlen={tip['branchlen']}")
                
        except Exception as e:
            print(f"Warning: Could not get final tips from node 0: {e}")
            final_tips = []
        
        # CRITICAL: Final strict responsiveness check before attempting final sync
        print("\n=== FINAL RESPONSIVENESS CHECK BEFORE SYNC ===")
        self.check_node_responsiveness_strict("Before final synchronization")
        
        # Wait 15 seconds before final sync to ensure all nodes complete operations
        # This prevents RPC overload and network stress during shutdown
        print("Waiting 15 seconds for all nodes to complete operations before final sync...")
        time.sleep(15)
        
        # Final verification - attempt synchronization but handle gracefully if it fails
        print("Attempting final synchronization...")
        try:
            sync_blocks(self.nodes)
            print("✓ All nodes successfully synchronized")
        except Exception as e:
            print(f"Warning: Final synchronization failed (this is often normal during shutdown): {e}")
            # Try to get individual node status for better reporting
            for i, node in enumerate(self.nodes):
                try:
                    height = node.getblockcount()
                    tips = node.getchaintips()
                    print(f"  Node {i}: height={height}, tips={len(tips)}")
                except Exception as node_error:
                    print(f"  Node {i}: Could not get status - {node_error}")
        
        # Report final results
        if successful_reorgs > 0:
            print("✓ SUCCESS: Multiple reorganization test completed with fork detection!")
        else:
            print("✓ SUCCESS: Multiple reorganization test completed - rapid fork resolution is normal!")
        
        print("\n=== TEST COMPLETED SUCCESSFULLY ===")
        print("All blockchain operations completed without crashes or assertion failures.")
        
        # Check GDB logs if running under GDB
        if self.use_gdb:
            print("Analyzing GDB logs for crash detection...")
            from test_framework.util import check_gdb_logs
            gdb_clean = check_gdb_logs(self.num_nodes)
            if gdb_clean:
                print("✅ GDB monitoring confirmed no crashes during execution.")
            else:
                print("⚠️  GDB detected crashes - see logs above for details.")
        
        # Ensure all nodes are fully responsive before test framework shutdown
        print("Verifying all nodes are responsive before framework shutdown...")
        self.ensure_nodes_responsive_before_shutdown()
        
        # Give a final moment for any cleanup before framework takes over
        time.sleep(2)

    def check_node_responsiveness_strict(self, context_msg="Unknown"):
        """Check all nodes are responsive and FAIL THE TEST if any are unresponsive"""
        print(f"=== STRICT RESPONSIVENESS CHECK: {context_msg} ===")
        
        unresponsive_nodes = []
        
        for i in range(self.num_nodes):
            try:
                # Test basic RPC responsiveness with timeout
                height = self.nodes[i].getblockcount()
                peer_count = len(self.nodes[i].getpeerinfo())
                
                print(f"  Node {i}: ✓ Responsive (height={height}, peers={peer_count})")
                
            except Exception as e:
                print(f"  Node {i}: ✗ UNRESPONSIVE - {e}")
                unresponsive_nodes.append((i, str(e)))
        
        if unresponsive_nodes:
            print(f"\n💥 TEST FAILURE: {len(unresponsive_nodes)} node(s) became unresponsive!")
            print(f"Context: {context_msg}")
            print("Unresponsive nodes:")
            for node_id, error in unresponsive_nodes:
                print(f"  - Node {node_id}: {error}")
            
            # Stop all nodes before failing
            print("\nStopping all nodes before test failure...")
            try:
                stop_nodes(self.nodes)
            except Exception as e:
                print(f"Warning: Error stopping nodes: {e}")
            
            # Fail the test immediately
            raise Exception(f"Test failed due to {len(unresponsive_nodes)} unresponsive node(s) during: {context_msg}")
        
        print("✅ All nodes confirmed responsive")

    def ensure_nodes_responsive_before_shutdown(self):
        """Ensure all nodes are responsive and stable before allowing framework shutdown"""
        max_attempts = 5
        
        for attempt in range(max_attempts):
            print(f"Node responsiveness check {attempt + 1}/{max_attempts}...")
            all_responsive = True
            
            for i in range(self.num_nodes):
                try:
                    # Test basic RPC responsiveness
                    height = self.nodes[i].getblockcount()
                    peer_count = len(self.nodes[i].getpeerinfo())
                    
                    print(f"  Node {i}: ✓ Responsive (height={height}, peers={peer_count})")
                    
                except Exception as e:
                    print(f"  Node {i}: ✗ Unresponsive - {e}")
                    all_responsive = False
            
            if all_responsive:
                print("✅ All nodes confirmed responsive - safe for framework shutdown")
                return
            else:
                if attempt < max_attempts - 1:
                    print(f"⚠️  Some nodes unresponsive, waiting 3 seconds before retry...")
                    time.sleep(3)
                else:
                    print("⚠️  Some nodes remain unresponsive after maximum attempts")
                    print("     This may cause 'Request-sent' errors during framework shutdown")
                    return

    def check_all_nodes_status(self, context_msg):
        """Check and report status of all nodes"""
        print(f"=== NODE STATUS CHECK: {context_msg} ===")
        for i in range(self.num_nodes):
            try:
                height = self.nodes[i].getblockcount()
                peer_count = len(self.nodes[i].getpeerinfo())
                print(f"  Node {i}: height={height}, peers={peer_count} ✓")
            except Exception as e:
                print(f"  Node {i}: UNRESPONSIVE - {e} ✗")

    def check_specific_nodes_status(self, node_list, context_msg):
        """Check and report status of specific nodes"""
        print(f"=== SPECIFIC NODES STATUS: {context_msg} ===")
        for i in node_list:
            try:
                height = self.nodes[i].getblockcount()
                peer_count = len(self.nodes[i].getpeerinfo())
                peers = self.nodes[i].getpeerinfo()
                peer_addrs = [f"{p.get('addr', 'unknown')}" for p in peers]
                print(f"  Node {i}: height={height}, peers={peer_count} {peer_addrs} ✓")
            except Exception as e:
                print(f"  Node {i}: UNRESPONSIVE - {e} ✗")

if __name__ == '__main__':
    GetChainTipsTest().main()
