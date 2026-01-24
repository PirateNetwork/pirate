#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2016-2022 The Zcash developers
# Copyright (c) 2017-2025 The Pirate developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .


#
# Helpful routines for regression testing
#

import os
import sys

from binascii import hexlify, unhexlify
from base64 import b64encode
from decimal import Decimal, ROUND_DOWN
import json
import http.client
import random
import shutil
import subprocess
import tarfile
import tempfile
import time
import re
import errno

from . import coverage
from .authproxy import AuthServiceProxy, JSONRPCException

PIRATED_BINARY = os.path.join('src', 'pirated')

LEGACY_DEFAULT_FEE = Decimal('0.00001')

COVERAGE_DIR = None
PRE_BLOSSOM_BLOCK_TARGET_SPACING = 150
POST_BLOSSOM_BLOCK_TARGET_SPACING = 75

SPROUT_BRANCH_ID = 0x00000000
OVERWINTER_BRANCH_ID = 0x5BA81B19
SAPLING_BRANCH_ID = 0x76B809BB
BLOSSOM_BRANCH_ID = 0x2BB40E60
HEARTWOOD_BRANCH_ID = 0xF5B9230B
CANOPY_BRANCH_ID = 0xE9FF75A6
NU5_BRANCH_ID = 0xC2D6D0B4
ORCHARD_BRANCH_ID = 0xC2D6D0B4

# The maximum number of nodes a single test can spawn
MAX_NODES = 8
# Don't assign rpc or p2p ports lower than this
PORT_MIN = 11000
# The number of ports to "reserve" for p2p and rpc, each
PORT_RANGE = 5000


def pirated_binary():
    return os.getenv("PIRATED", PIRATED_BINARY)

class PortSeed:
    # Must be initialized with a unique integer for each process
    n = None

def enable_coverage(dirname):
    """Maintain a log of which RPC calls are made during testing."""
    global COVERAGE_DIR
    COVERAGE_DIR = dirname


def get_rpc_proxy(url, node_number, timeout=None):
    """
    Args:
        url (str): URL of the RPC server to call
        node_number (int): the node number (or id) that this calls to

    Kwargs:
        timeout (int): HTTP timeout in seconds

    Returns:
        AuthServiceProxy. convenience object for making RPC calls.

    """
    proxy_kwargs = {}
    if timeout is not None:
        proxy_kwargs['timeout'] = timeout

    proxy = AuthServiceProxy(url, **proxy_kwargs)
    proxy.url = url  # store URL on proxy for info

    coverage_logfile = coverage.get_filename(
        COVERAGE_DIR, node_number) if COVERAGE_DIR else None

    return coverage.AuthServiceProxyWrapper(proxy, coverage_logfile)


def p2p_port(n):
    assert(n <= MAX_NODES)
    return PORT_MIN + n + (MAX_NODES * PortSeed.n) % (PORT_RANGE - 1 - MAX_NODES)

def rpc_port(n):
    return PORT_MIN + PORT_RANGE + n + (MAX_NODES * PortSeed.n) % (PORT_RANGE - 1 - MAX_NODES)

def check_json_precision():
    """Make sure json library being used does not lose precision converting ZEC values"""
    n = Decimal("20000000.00000003")
    zatoshis = int(json.loads(json.dumps(float(n)))*1.0e8)
    if zatoshis != 2000000000000003:
        raise RuntimeError("JSON encode/decode loses precision")

def bytes_to_hex_str(byte_str):
    return hexlify(byte_str).decode('ascii')

def hex_str_to_bytes(hex_str):
    return unhexlify(hex_str.encode('ascii'))

def str_to_b64str(string):
    return b64encode(string.encode('utf-8')).decode('ascii')

def sync_blocks(rpc_connections, wait=0.125, timeout=600, allow_different_tips=False):
    """
    Wait until everybody has the same tip, and has notified
    all internal listeners of them.

    If allow_different_tips is True, waits until everyone has
    the same block count.
    """
    while timeout > 0:
        try:
            if allow_different_tips:
                tips = [ x.getblockcount() for x in rpc_connections ]
            else:
                tips = [ x.getbestblockhash() for x in rpc_connections ]
            if tips == [ tips[0] ]*len(tips):
                return True
        except JSONRPCException as e:
            # Handle the case where RPC interface is disabled while building witness cache
            if "RPC Interface disabled while builing witness cache" in str(e) or \
               "RPC interface disabled while building witness cache" in str(e):
                print("RPC interface disabled while building witness cache, waiting...")
                time.sleep(5)  # Wait longer when building cache
                #timeout -= 5
                continue
            elif "Activating best chain..." in str(e):
                # This can happen if the node is still processing a block
                # that was just generated, so we wait and try again.
                print("Activating best chain, waiting...")
                time.sleep(5)
            else:
                raise e
        time.sleep(wait)
        timeout -= wait

    raise AssertionError("Block sync failed")

def sync_mempools(rpc_connections, wait=0.5, timeout=120):
    """
    Wait until everybody has the same transactions in their memory
    pools, and has notified all internal listeners of them
    """
    while timeout > 0:
        pool = set(rpc_connections[0].getrawmempool())
        num_match = 1
        for i in range(1, len(rpc_connections)):
            if set(rpc_connections[i].getrawmempool()) == pool:
                num_match = num_match+1
        if num_match == len(rpc_connections):
            break
        time.sleep(wait)
        timeout -= wait

    # Now that the mempools are in sync, wait for the internal
    # notifications to finish
    while timeout > 0:
        notified = [ x.getmempoolinfo()['fullyNotified'] for x in rpc_connections ]
        if notified == [ True ] * len(notified):
            return True
        time.sleep(wait)
        timeout -= wait

    raise AssertionError("Mempool sync failed")

bitcoind_processes = {}

def initialize_datadir(dirname, n, clock_offset=0, addnodes=None, p2p_port_override=None, rpc_port_override=None):
    """
    Initialize a node's data directory with proper configuration.
    
    Args:
        dirname: Base directory for node data
        n: Node number (used for port assignment when ports not overridden)
        clock_offset: Clock offset in seconds (default: 0)
        addnodes: List of full address strings to connect to (default: None)
                 Should be in format "127.0.0.1:port" with predetermined ports
                 If None and n > 0, connects to node 0 using p2p_port(0)
        p2p_port_override: Predetermined P2P port for this node (default: None)
                          Only valid when addnodes is provided
        rpc_port_override: Predetermined RPC port for this node (default: None)
                          Only valid when addnodes is provided
    """
    # Port overrides only allowed when addnodes is provided
    if (p2p_port_override is not None or rpc_port_override is not None) and addnodes is None:
        raise ValueError("Port overrides can only be used when addnodes is provided")
    
    datadir = os.path.join(dirname, "node"+str(n))
    if not os.path.isdir(datadir):
        os.makedirs(datadir)
    rpc_u, rpc_p = rpc_auth_pair(n)
    
    # Use predetermined ports if provided (only when addnodes is used), otherwise calculate them
    if addnodes is not None:
        node_p2p_port = p2p_port_override if p2p_port_override is not None else p2p_port(n)
        node_rpc_port = rpc_port_override if rpc_port_override is not None else rpc_port(n)
    else:
        # Default behavior: always use calculated ports
        node_p2p_port = p2p_port(n)
        node_rpc_port = rpc_port(n)
    
    with open(os.path.join(datadir, "PIRATETST.conf"), 'w', encoding='utf8') as f:
        f.write("showmetrics=0\n")
        f.write("rpcuser=" + rpc_u + "\n")
        f.write("rpcpassword=" + rpc_p + "\n")
        f.write("port="+str(node_p2p_port)+"\n")
        f.write("rpcport="+str(node_rpc_port)+"\n")
        f.write("listenonion=0\n")
        f.write("allowlocaladdnode=1\n")
        f.write("addressindex=1\n")
        f.write("spentindex=1\n")
        f.write("timestampindex=1\n")
        f.write("ac_private=0\n")
        f.write("ac_cc=0\n")
        f.write("debug=net\n")
        
        # Handle addnode connections
        if addnodes is not None:
            # Use custom addnode list - accept as full addresses (already calculated)
            for addnode_addr in addnodes:
                f.write("addnode="+str(addnode_addr)+"\n")
        elif n > 0:
            # Default behavior: connect to node 0
            f.write("addnode=127.0.0.1:"+str(p2p_port(0))+"\n")
            
        if clock_offset != 0: 
            f.write('clockoffset='+str(clock_offset)+'\n')

    return datadir

def rpc_auth_pair(n):
    return 'rpcuser💻' + str(n), 'rpcpass🔑' + str(n)

def rpc_url(i, rpchost=None):
    rpc_u, rpc_p = rpc_auth_pair(i)
    host = '127.0.0.1'
    port = rpc_port(i)
    if rpchost:
        parts = rpchost.split(':')
        if len(parts) == 2:
            host, port = parts
        else:
            host = rpchost
    return "http://%s:%s@%s:%d" % (rpc_u, rpc_p, host, int(port))

def wait_for_bitcoind_start(process, url, i):
    '''
    Wait for bitcoind to start. This means that RPC is accessible and fully initialized.
    Raise an exception if bitcoind exits during initialization.
    '''
    while True:
        if process.poll() is not None:
            raise Exception('%s node %d exited with status %i during initialization' % (pirated_binary(), i, process.returncode))
        try:
            rpc = get_rpc_proxy(url, i)
            rpc.getblockcount()
            break # break out of loop on success
        except IOError as e:
            if e.errno != errno.ECONNREFUSED: # Port not yet open?
                raise # unknown IO error
        except JSONRPCException as e: # Initialization phase
            if e.error['code'] != -28: # RPC in warmup?
                raise # unknown JSON RPC exception
        time.sleep(0.25)

def initialize_chain(test_dir, num_nodes, cachedir, cache_behavior='current'):
    """
    Create a set of node datadirs in `test_dir`, based upon the specified
    `cache_behavior` value. The following values are recognized for 
    `cache_behavior`:

    * 'current': create a 200-block-long chain (with wallet) for MAX_NODES
      in `cachedir` if necessary. Afterward, create num_nodes copies in 
      `test_dir` from the cache. The resulting nodes will be configured to
      use the -clockoffset config argument when starting to ensure that 
      the cached chain is not treated as being excessively out-of-date.
    * 'sprout': use persisted chain data containing known amounts of Sprout
      funds from the files in `qa/rpc-tests/cache/sprout`. This allows 
      testing of Sprout spends even though Sprout outputs can no longer
      be created by pirated software. The resulting nodes will be configured to
      use the -clockoffset config argument when starting to ensure that 
      the cached chain is not treated as being excessively out-of-date.
    * 'fresh': force re-creation of the cache, and then start as for `current`.
    * 'clean': start the nodes without cached chain data, allowing the test
      to take full control of chain setup.
    """
    assert num_nodes <= MAX_NODES

    def rebuild_cache():
        #find and delete old cache directories if any exist
        for i in range(MAX_NODES):
            if os.path.isdir(os.path.join(cachedir,"node"+str(i))):
                shutil.rmtree(os.path.join(cachedir,"node"+str(i)))

        # Create cache directories, run bitcoinds:
        block_time = int(time.time()) - (800 * PRE_BLOSSOM_BLOCK_TARGET_SPACING)
        for i in range(MAX_NODES):
            datadir = initialize_datadir(cachedir, i)
            args = [ pirated_binary() ]
            args.extend([
                '-regtest=1',
                '-datadir='+datadir,
                '-keypool=100',
                '-discover=0',
                #'-nuparams=5ba81b19:1', # Overwinter
                #'-nuparams=76b809bb:1', # Sapling
                '-mocktime=%d' % block_time
            ])
            if i > 0:
                args.append("-connect=127.0.0.1:"+str(p2p_port(0)))
            bitcoind_processes[i] = subprocess.Popen(args)
            if os.getenv("PYTHON_DEBUG", ""):
                print("initialize_chain: %s started, waiting for RPC to come up" % (pirated_binary(),))
            wait_for_bitcoind_start(bitcoind_processes[i], rpc_url(i), i)
            if os.getenv("PYTHON_DEBUG", ""):
                print("initialize_chain: RPC successfully started")

        rpcs = []
        for i in range(MAX_NODES):
            try:
                rpcs.append(get_rpc_proxy(rpc_url(i), i))
            except:
                sys.stderr.write("Error connecting to "+rpc_url(i)+"\n")
                sys.exit(1)

        # Create a 200-block-long chain; each of the 4 first nodes
        # gets 25 mature blocks and 25 immature.
        # Note: To preserve compatibility with older versions of
        # initialize_chain, only 4 nodes will generate coins.
        #
        # Blocks are created with timestamps 2.5 minutes apart (matching the
        # chain defaulting above to Sapling active), starting 200 * 2.5 minutes
        # before the current time.
        for i in range(2):
            for peer in range(4):
                for j in range(25):
                    set_node_times(rpcs, block_time)
                    rpcs[peer].generate(1)
                    block_time += PRE_BLOSSOM_BLOCK_TARGET_SPACING
                # Must sync before next peer starts generating blocks
                sync_blocks(rpcs)
        # Check that local time isn't going backwards
        assert_greater_than(time.time() + 1, block_time)

        # Shut them down, and clean up cache directories:
        stop_nodes(rpcs)
        wait_bitcoinds()
        for i in range(MAX_NODES):
            # record the system time at which the cache was regenerated
            with open(node_file(cachedir, i, 'cache_config.json'), "w", encoding="utf8") as cache_conf_file:
                cache_config = { "cache_time": time.time() }
                cache_conf_json = json.dumps(cache_config, indent=4)
                cache_conf_file.write(cache_conf_json)

            os.remove(node_file(cachedir, i, "debug.log"))
            os.remove(node_file(cachedir, i, "db.log"))
            os.remove(node_file(cachedir, i, "peers.dat"))

    def init_from_cache():
        for i in range(num_nodes):
            from_dir = os.path.join(cachedir, "node"+str(i))
            to_dir = os.path.join(test_dir,  "node"+str(i))
            shutil.copytree(from_dir, to_dir)
            with open(os.path.join(to_dir, 'regtest', 'cache_config.json'), "r", encoding="utf8") as cache_conf_file:
                cache_conf = json.load(cache_conf_file)
                # obtain the clock offset as a negative number of seconds
                offset = round(cache_conf['cache_time']) - round(time.time())
                # overwrite port/rpcport and clock offset in PIRATETST.conf
                initialize_datadir(test_dir, i, clock_offset=offset) 

    def init_persistent(cache_behavior):
        assert num_nodes <= 4 # only 4 nodes with Sprout funds are supported
        cache_path = persistent_cache_path(cache_behavior)
        if not os.path.isdir(cache_path):
            raise Exception('No cache available for cache behavior %s' % cache_behavior)

        chain_cache_filename = os.path.join(cache_path, "chain_cache.tar.gz")
        if not os.path.exists(chain_cache_filename):
            raise Exception('Chain cache missing for cache behavior %s' % cache_behavior)

        for i in range(num_nodes):
            to_dir = os.path.join(test_dir, "node"+str(i), "regtest")
            os.makedirs(to_dir)

            # Copy the same chain data to all nodes
            with tarfile.open(chain_cache_filename, "r:gz") as chain_cache_file:
                chain_cache_file.extractall(path = to_dir)

            # Copy in per-node wallet data
            wallet_tgz_filename = os.path.join(cache_path, "node"+str(i)+"_wallet.tar.gz")
            if not os.path.exists(wallet_tgz_filename):
                raise Exception('Wallet cache missing for cache behavior %s, node %d' % (cache_behavior, i))
            with tarfile.open(wallet_tgz_filename, "r:gz") as wallet_tgz_file:
                wallet_tgz_file.extractall(path = os.path.join(to_dir, "wallet.dat"))

            # Copy in per-node wallet config and update PIRATETST.conf to set the
            # clock offsets correctly.
            cache_conf_filename = os.path.join(to_dir, 'cache_config.json')
            if not os.path.exists(cache_conf_filename):
                raise Exception('Cache config missing for cache behavior %s, node %d' % (cache_behavior, i))
            with open(cache_conf_filename, "r", encoding="utf8") as cache_conf_file:
                cache_conf = json.load(cache_conf_file)
                # obtain the clock offset as a negative number of seconds
                offset = round(cache_conf['cache_time']) - round(time.time())
                # overwrite port/rpcport and clock offset in PIRATETST.conf
                initialize_datadir(test_dir, i, clock_offset=offset) 

    def cache_rebuild_required():
        for i in range(MAX_NODES):
            node_path = os.path.join(cachedir, 'node'+str(i))
            if os.path.isdir(node_path):
                if not os.path.isfile(node_file(cachedir, i, 'cache_config.json')):
                    return True
            else:
                return True
        return False

    if cache_behavior == 'current':
        if cache_rebuild_required(): rebuild_cache() 
        init_from_cache()
    elif cache_behavior == 'fresh':
        rebuild_cache()
        init_from_cache()
    elif cache_behavior == 'clean':
        initialize_chain_clean(test_dir, num_nodes)
    else:
        init_persistent(cache_behavior)

def initialize_chain_clean(test_dir, num_nodes):
    """
    Create an empty blockchain and num_nodes wallets.
    Useful if a test case wants complete control over initialization.
    """
    for i in range(num_nodes):
        initialize_datadir(test_dir, i)

def persistent_cache_path(cache_behavior):
    return os.path.join(
        os.path.dirname(os.path.dirname(os.path.realpath(__file__))), 
        'cache', 
        cache_behavior
    )

def persistent_cache_exists(cache_behavior):
    cache_path = persistent_cache_path(cache_behavior)
    return os.path.isdir(cache_path)

# Clean up, zip, and persist the generated datadirs. Record the generation
# time so that we can correctly set the system clock offset in tests that
# restore their node states using the resulting files.
def persist_node_caches(tmpdir, cache_behavior, num_nodes):
    cache_path = persistent_cache_path(cache_behavior)
    if os.path.exists(cache_path):
        raise Exception('Cache already exists for cache behavior %s' % cache_behavior)
    os.mkdir(cache_path)

    for i in range(num_nodes):
        node_path = os.path.join(tmpdir, 'node' + str(i), 'regtest')

        # Clean up the files that we don't want to persist
        os.remove(os.path.join(node_path, 'debug.log'))
        os.remove(os.path.join(node_path, 'db.log'))
        os.remove(os.path.join(node_path, 'peers.dat'))

        # Persist the wallet file for the node to the cache
        wallet_tgz_filename = os.path.join(cache_path, 'node' + str(i) + '_wallet.tar.gz')
        with tarfile.open(wallet_tgz_filename, "w:gz") as wallet_tgz_file:
            wallet_tgz_file.add(os.path.join(node_path, 'wallet.dat'), arcname="")

        # Persist the chain data and cache config just once; it will be reused
        # for all of the nodes when loading from the cache.
        if i == 0:
            # Move the wallet.dat file out of the way so that it doesn't
            # pollute the chain cache tarfile
            shutil.move(
                    os.path.join(node_path, 'wallet.dat'),
                    os.path.join(tmpdir, 'wallet.dat.0'))

            # Store the current time so that we can correctly set the clock
            # offset when restoring from the cache.
            cache_config = { "cache_time": time.time() }
            cache_conf_filename = os.path.join(cache_path, 'cache_config.json')
            with open(cache_conf_filename, "w", encoding="utf8") as cache_conf_file:
                cache_conf_json = json.dumps(cache_config, indent=4)
                cache_conf_file.write(cache_conf_json)

            # Persist the chain data.
            chain_cache_filename = os.path.join(cache_path, 'chain_cache.tar.gz')
            with tarfile.open(chain_cache_filename, "w:gz") as chain_cache_file:
                chain_cache_file.add(node_path, arcname="")

            # Move the wallet file back into place
            shutil.move(
                    os.path.join(tmpdir, 'wallet.dat.0'),
                    os.path.join(node_path, 'wallet.dat'))


def _rpchost_to_args(rpchost):
    '''Convert optional IP:port spec to rpcconnect/rpcport args'''
    if rpchost is None:
        return []

    match = re.match('(\[[0-9a-fA-f:]+\]|[^:]+)(?::([0-9]+))?$', rpchost)
    if not match:
        raise ValueError('Invalid RPC host spec ' + rpchost)

    rpcconnect = match.group(1)
    rpcport = match.group(2)

    if rpcconnect.startswith('['): # remove IPv6 [...] wrapping
        rpcconnect = rpcconnect[1:-1]

    rv = ['-rpcconnect=' + rpcconnect]
    if rpcport:
        rv += ['-rpcport=' + rpcport]
    return rv

def start_node(i, dirname, extra_args=None, rpchost=None, timewait=None, binary=None, stderr=None, use_gdb=False):
    """
    Start a bitcoind and return RPC connection to it
    """
    datadir = os.path.join(dirname, "node"+str(i))
    if binary is None:
        binary = pirated_binary()
    
    # Build the command arguments for the daemon
    daemon_args = [
        '-regtest=1',
        '-datadir='+datadir,
        '-keypool=100',
        '-discover=0',
        '-rest',
        #'-nuparams=5ba81b19:1', # Overwinter
        #'-nuparams=76b809bb:1', # Sapling
    ]
    if extra_args is not None: 
        daemon_args.extend(extra_args)
    
    # Choose between gdb and direct execution
    if use_gdb:
        # Create persistent log directory for GDB logs
        gdb_logs_dir = os.path.join(os.getcwd(), "gdb_logs")
        os.makedirs(gdb_logs_dir, exist_ok=True)
        
        # Create a gdb script for better debugging
        gdb_script = os.path.join(datadir, f"node{i}_gdb.script")
        gdb_log = os.path.join(gdb_logs_dir, f"node{i}_gdb.log")
        gdb_core_dir = os.path.join(gdb_logs_dir, f"node{i}_cores")
        
        os.makedirs(datadir, exist_ok=True)
        os.makedirs(gdb_core_dir, exist_ok=True)
        
        with open(gdb_script, 'w') as f:
            f.write("set confirm off\n")
            f.write("set pagination off\n")
            f.write("set print pretty on\n")
            f.write("set logging file " + gdb_log + "\n")
            f.write("set logging on\n")
            f.write("handle SIGPIPE nostop noprint pass\n")  # Ignore broken pipe signals
            # Catch assertion failures and treat them as crashes
            f.write("handle SIGABRT stop print\n")  # Stop on assertion failures (SIGABRT)
            f.write("catch signal SIGABRT\n")  # Catch SIGABRT signals
            f.write("break abort\n")  # Break on abort() calls
            f.write("break __assert_fail\n")  # Break on assertion failures (glibc)
            f.write("break __assertion_failed\n")  # Break on assertion failures (some systems)
            # Create command to execute when assertion breakpoints are hit
            f.write("commands\n")
            f.write("  echo \\n*** ASSERTION FAILURE DETECTED ***\\n\n")
            f.write("  echo Function call that triggered the assertion:\\n\n")
            f.write("  bt 10\n")
            f.write("  echo \\n*** Full stack trace ***\\n\n")
            f.write("  bt full\n")
            f.write("  echo \\n*** Assertion details ***\\n\n")
            f.write("  info frame\n")
            f.write("  info locals\n")
            f.write("  info args\n")
            f.write("  echo \\n*** Generating core dump for assertion failure ***\\n\n")
            f.write("  shell cd " + gdb_core_dir + "\n")
            f.write("  generate-core-file\n")
            f.write("  echo *** Assertion core dump saved to " + gdb_core_dir + " ***\\n\n")
            f.write("  echo *** EXITING DUE TO ASSERTION FAILURE ***\\n\n")
            f.write("  quit 1\n")
            f.write("end\n")
            f.write("echo *** Starting GDB session for node " + str(i) + " ***\\n\n")
            f.write("echo Binary: " + binary + "\\n\n")
            f.write("echo Arguments: " + " ".join(daemon_args) + "\\n\n")
            f.write("echo Timestamp: ")
            f.write("shell date\n")
            f.write("echo \\n*** Running process ***\\n\n")
            f.write("run\n")
            f.write("echo \\n*** Process finished with exit code: \n")
            f.write("if $_exitcode == 0\n")
            f.write("  echo *** NORMAL EXIT - No crash detected ***\\n\n")
            f.write("else\n")
            f.write("  echo *** ABNORMAL EXIT - Process crashed with code: \n")
            f.write("  print $_exitcode\n")
            f.write("  echo \\n*** CRASH DETECTED - Generating backtrace ***\\n\n")
            f.write("  bt full\n")
            f.write("  echo \\n*** Printing registers ***\\n\n")
            f.write("  info registers\n")
            f.write("  echo \\n*** Examining stack frame ***\\n\n")
            f.write("  info frame\n")
            f.write("  echo \\n*** Local variables ***\\n\n")
            f.write("  info locals\n")
            f.write("  echo \\n*** Function arguments ***\\n\n")
            f.write("  info args\n")
            f.write("  echo \\n*** Generating core dump ***\\n\n")
            f.write("  shell cd " + gdb_core_dir + "\n")
            f.write("  generate-core-file\n")
            f.write("  echo *** Core dump saved to " + gdb_core_dir + " ***\\n\n")
            f.write("end\n")
            f.write("echo *** GDB session completed ***\\n\n")
            f.write("shell date\n")
            f.write("quit\n")
        
        # Run under gdb with the script and redirect output
        args = ['gdb', '--batch', '--command', gdb_script, '--args', binary]
        args.extend(daemon_args)
        print(f"Starting node {i} under GDB")
        print(f"  GDB script: {gdb_script}")
        print(f"  GDB log: {gdb_log}")
        print(f"  Core dumps: {gdb_core_dir}")
        print(f"  Command: gdb --batch --command {gdb_script} --args {binary} {' '.join(daemon_args)}")
    else:
        # Direct execution (original behavior)
        args = [binary]
        args.extend(daemon_args)
    
    bitcoind_processes[i] = subprocess.Popen(args, stderr=stderr)
    if os.getenv("PYTHON_DEBUG", ""):
        print("start_node: bitcoind started, waiting for RPC to come up")
    url = rpc_url(i, rpchost)
    wait_for_bitcoind_start(bitcoind_processes[i], url, i)
    if os.getenv("PYTHON_DEBUG", ""):
        print("start_node: RPC successfully started")
    proxy = get_rpc_proxy(url, i, timeout=timewait)

    if COVERAGE_DIR:
        coverage.write_all_rpc_commands(COVERAGE_DIR, proxy)

    return proxy

def assert_start_raises_init_error(i, dirname, extra_args=None, expected_msg=None):
    with tempfile.SpooledTemporaryFile(max_size=2**16) as log_stderr:
        try:
            node = start_node(i, dirname, extra_args, stderr=log_stderr)
            stop_node(node, i)
        except Exception as e:
            assert ("%s node %d exited" % (pirated_binary(), i)) in str(e) # node must have shutdown
            if expected_msg is not None:
                log_stderr.seek(0)
                stderr = log_stderr.read().decode('utf-8')
                if expected_msg not in stderr:
                    raise AssertionError("Expected error \"" + expected_msg + "\" not found in:\n" + stderr)
        else:
            if expected_msg is None:
                assert_msg = "%s should have exited with an error" % (pirated_binary(),)
            else:
                assert_msg = "%s should have exited with expected error %r" % (pirated_binary(), expected_msg)
            raise AssertionError(assert_msg)

def start_nodes(num_nodes, dirname, extra_args=None, rpchost=None, binary=None, use_gdb=False):
    """
    Start multiple bitcoinds, return RPC connections to them
    """
    if extra_args is None: extra_args = [ None for _ in range(num_nodes) ]
    if binary is None: binary = [ None for _ in range(num_nodes) ]
    rpcs = []
    try:
        for i in range(num_nodes):
            rpcs.append(start_node(i, dirname, extra_args[i], rpchost, binary=binary[i], use_gdb=use_gdb))
    except: # If one node failed to start, stop the others
        stop_nodes(rpcs)
        raise
    return rpcs

def node_file(dirname, n_node, filename):
    return os.path.join(dirname, "node"+str(n_node), "regtest", filename)

def check_node(i):
    bitcoind_processes[i].poll()
    return bitcoind_processes[i].returncode

def stop_node(node, i):
    # Be defensive: node may be None if start failed or returned nothing
    if node is None:
        # If the process exists, wait for it and remove from tracking
        if i in bitcoind_processes and bitcoind_processes[i] is not None:
            try:
                bitcoind_processes[i].wait()
            except Exception:
                pass
            try:
                del bitcoind_processes[i]
            except KeyError:
                pass
        return
    try:
        node.stop()
    except http.client.CannotSendRequest as e:
        print("WARN: Unable to stop node: " + repr(e))
    # Wait for the process to exit and remove from tracking if present
    if i in bitcoind_processes and bitcoind_processes[i] is not None:
        try:
            bitcoind_processes[i].wait()
        except Exception:
            pass
        try:
            del bitcoind_processes[i]
        except KeyError:
            pass

def stop_nodes(nodes):
    # Defensive: skip None entries (nodes that failed to start)
    for idx, node in enumerate(list(nodes)):
        if node is None:
            # If a process with this index exists, wait for it and drop it
            if idx in bitcoind_processes and bitcoind_processes[idx] is not None:
                try:
                    bitcoind_processes[idx].wait()
                except Exception:
                    pass
                try:
                    del bitcoind_processes[idx]
                except KeyError:
                    pass
            continue
        try:
            node.stop()
        except http.client.CannotSendRequest as e:
            print("WARN: Unable to stop node: " + repr(e))
    del nodes[:] # Emptying array closes connections as a side effect

def set_node_times(nodes, t):
    for node in nodes:
        node.setmocktime(t)

def wait_bitcoinds():
    # Wait for all bitcoinds to cleanly exit
    for bitcoind in list(bitcoind_processes.values()):
        bitcoind.wait()
    bitcoind_processes.clear()

def connect_nodes(from_connection, node_num):
    ip_port = "127.0.0.1:"+str(p2p_port(node_num))
    try:
        from_connection.addnode(ip_port, "add")
    except JSONRPCException as e:
        # Ignore "Node already added" error - this is normal networking behavior
        if "Node already added" in str(e):
            pass
        else:
            raise e
    # poll until version handshake complete to avoid race conditions
    # with transaction relaying
    while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
        time.sleep(0.1)

def connect_nodes_bi(nodes, a, b):
    connect_nodes(nodes[a], b)
    connect_nodes(nodes[b], a)

def find_output(node, txid, amount):
    """
    Return index to output of txid with value amount
    Raises exception if there is none.
    """
    txdata = node.getrawtransaction(txid, 1)
    for i in range(len(txdata["vout"])):
        if txdata["vout"][i]["value"] == amount:
            return i
    raise RuntimeError("find_output txid %s : %s not found"%(txid,str(amount)))


def gather_inputs(from_node, amount_needed, confirmations_required=1):
    """
    Return a random set of unspent txouts that are enough to pay amount_needed
    """
    assert(confirmations_required >=0)
    utxo = from_node.listunspent(confirmations_required)
    random.shuffle(utxo)
    inputs = []
    total_in = Decimal("0.00000000")
    while total_in < amount_needed and len(utxo) > 0:
        t = utxo.pop()
        total_in += t["amount"]
        inputs.append({ "txid" : t["txid"], "vout" : t["vout"], "address" : t["address"] } )
    if total_in < amount_needed:
        raise RuntimeError("Insufficient funds: need %d, have %d"%(amount_needed, total_in))
    return (total_in, inputs)

def make_change(from_node, amount_in, amount_out, fee):
    """
    Create change output(s), return them
    """
    outputs = {}
    amount = amount_out+fee
    change = amount_in - amount
    if change > amount*2:
        # Create an extra change output to break up big inputs
        change_address = from_node.getnewaddress()
        # Split change in two, being careful of rounding:
        outputs[change_address] = Decimal(change/2).quantize(Decimal('0.00000001'), rounding=ROUND_DOWN)
        change = amount_in - amount - outputs[change_address]
    if change > 0:
        outputs[from_node.getnewaddress()] = change
    return outputs

def random_transaction(nodes, amount, min_fee, fee_increment, fee_variants):
    """
    Create a random transaction.
    Returns (txid, hex-encoded-transaction-data, fee)
    """
    from_node = random.choice(nodes)
    to_node = random.choice(nodes)
    fee = min_fee + fee_increment*random.randint(0,fee_variants)

    (total_in, inputs) = gather_inputs(from_node, amount+fee)
    outputs = make_change(from_node, total_in, amount, fee)
    outputs[to_node.getnewaddress()] = float(amount)

    rawtx = from_node.createrawtransaction(inputs, outputs)
    signresult = from_node.signrawtransaction(rawtx)
    txid = from_node.sendrawtransaction(signresult["hex"], True)

    return (txid, signresult["hex"], fee)

def assert_equal(expected, actual, message=""):
    if expected != actual:
        if message:
            message = "; %s" % message
        raise AssertionError("(left == right)%s\n  left: <%s>\n right: <%s>" % (message, str(expected), str(actual)))

def assert_true(condition, message = ""):
    if not condition:
        raise AssertionError(message)

def assert_false(condition, message = ""):
    assert_true(not condition, message)

def assert_greater_than(thing1, thing2):
    if thing1 <= thing2:
        raise AssertionError("%s <= %s"%(str(thing1),str(thing2)))

def assert_raises(exc, fun, *args, **kwds):
    assert_raises_message(exc, None, fun, *args, **kwds)

def assert_raises_message(ExceptionType, errstr, func, *args, **kwargs):
    """
    Asserts that func throws and that the exception contains 'errstr'
    in its message.
    """
    try:
        func(*args, **kwargs)
    except ExceptionType as e:
        if errstr is not None and errstr not in str(e):
            raise AssertionError("Invalid exception string: Couldn't find %r in %r" % (
                errstr, str(e)))
    except Exception as e:
        raise AssertionError("Unexpected exception raised: " + type(e).__name__)
    else:
        raise AssertionError("No exception raised")

def fail(message=""):
    raise AssertionError(message)


# Returns an async operation result
def wait_and_assert_operationid_status_result(node, myopid, in_status='success', in_errormsg=None, timeout=300):
    print('waiting for async operation {}'.format(myopid))
    result = None
    for _ in range(1, timeout):
        results = node.z_getoperationresult([myopid])
        if len(results) > 0:
            result = results[0]
            break
        time.sleep(1)

    assert_true(result is not None, "timeout occurred")
    status = result['status']

    debug = os.getenv("PYTHON_DEBUG", "")
    if debug:
        print('...returned status: {}'.format(status))

    errormsg = None
    if status == "failed":
        errormsg = result['error']['message']
        if debug:
            print('...returned error: {}'.format(errormsg))
        assert_equal(in_errormsg, errormsg)

    print('\nIn message: {}\n, Out message: {}\n'.format(in_errormsg, errormsg))
    assert_equal(in_status, status, "Operation returned mismatched status. Error Message: {}".format(errormsg))

    return result


# Returns txid if operation was a success or None
def wait_and_assert_operationid_status(node, myopid, in_status='success', in_errormsg=None, timeout=300):
    result = wait_and_assert_operationid_status_result(node, myopid, in_status, in_errormsg, timeout)
    if result['status'] == "success":
        return result['result']['txid']
    else:
        return result

# Find a coinbase address on the node, filtering by the number of UTXOs it has.
# If no filter is provided, returns the coinbase address on the node containing
# the greatest number of spendable UTXOs.
# The default cached chain has one address per coinbase output.
def get_coinbase_address(node, expected_utxos=None):
    addrs = [utxo['address'] for utxo in node.listunspent() if utxo['generated']]
    assert(len(set(addrs)) > 0)

    if expected_utxos is None:
        addrs = [(addrs.count(a), a) for a in set(addrs)]
        return sorted(addrs, reverse=True)[0][1]

    addrs = [a for a in set(addrs) if addrs.count(a) == expected_utxos]
    assert(len(addrs) > 0)
    return addrs[0]

def check_node_log(self, node_number, line_to_check, stop_node = True):
    print("Checking node " + str(node_number) + " logs")
    if stop_node:
        self.nodes[node_number].stop()
        bitcoind_processes[node_number].wait()
    logpath = self.options.tmpdir + "/node" + str(node_number) + "/regtest/debug.log"
    with open(logpath, "r", encoding="utf8") as myfile:
        logdata = myfile.readlines()
    for (n, logline) in enumerate(logdata):
        if line_to_check in logline:
            return n
    raise AssertionError(repr(line_to_check) + " not found")

def nustr(branch_id):
    return '%08x' % branch_id

def nuparams(branch_id, height):
    return '-nuparams=%s:%d' % (nustr(branch_id), height)

def generate_with_delay(node, num_blocks, delay=1.0):
    """
    Generate blocks with a delay between each block.
    
    Args:
        node: The RPC connection to the node
        num_blocks (int): Number of blocks to generate
        delay (float): Delay in seconds between blocks (default: 1.0)
        
    Returns:
        list: List of generated block hashes
    """
    block_hashes = []
    for i in range(num_blocks):
        if i > 0:  # No delay before the first block
            time.sleep(delay)
        hashes = node.generate(1)
        block_hashes.extend(hashes)
    return block_hashes

def check_gdb_logs(num_nodes):
    """
    Check and display GDB logs after test completion
    """
    import glob
    
    gdb_logs_dir = os.path.join(os.getcwd(), "gdb_logs")
    if not os.path.exists(gdb_logs_dir):
        print("No GDB logs directory found")
        return
    
    print(f"\n=== GDB LOGS SUMMARY ===")
    print(f"GDB logs directory: {gdb_logs_dir}")
    
    crashes_found = 0
    normal_exits = 0
    
    for i in range(num_nodes):
        gdb_log = os.path.join(gdb_logs_dir, f"node{i}_gdb.log")
        gdb_core_dir = os.path.join(gdb_logs_dir, f"node{i}_cores")
        
        if os.path.exists(gdb_log):
            print(f"\nNode {i} GDB log: {gdb_log}")
            
            # Read and analyze the log
            try:
                with open(gdb_log, 'r') as f:
                    log_content = f.read()
                
                if "NORMAL EXIT - No crash detected" in log_content:
                    print(f"  ✓ Node {i}: Normal exit - no crashes")
                    normal_exits += 1
                elif "ABNORMAL EXIT - Process crashed" in log_content:
                    print(f"  ✗ Node {i}: CRASH DETECTED!")
                    crashes_found += 1
                    
                    # Show relevant crash information
                    lines = log_content.split('\n')
                    in_backtrace = False
                    backtrace_lines = []
                    
                    for line in lines:
                        if "Generating backtrace" in line:
                            in_backtrace = True
                            continue
                        elif "Printing registers" in line:
                            in_backtrace = False
                            break
                        elif in_backtrace and line.strip():
                            backtrace_lines.append(line)
                            if len(backtrace_lines) >= 10:  # Limit to first 10 lines
                                break
                    
                    if backtrace_lines:
                        print(f"    Crash backtrace (first 10 lines):")
                        for bt_line in backtrace_lines:
                            print(f"      {bt_line}")
                    
                    # Check for core dumps
                    if os.path.exists(gdb_core_dir):
                        core_files = glob.glob(os.path.join(gdb_core_dir, "core*"))
                        if core_files:
                            print(f"    Core dumps generated: {len(core_files)} files in {gdb_core_dir}")
                        else:
                            print(f"    No core dumps found in {gdb_core_dir}")
                else:
                    print(f"  ? Node {i}: Indeterminate status (check log manually)")
                    
            except Exception as e:
                print(f"  ! Node {i}: Error reading log - {e}")
        else:
            print(f"  - Node {i}: No GDB log found")
    
    # Summary
    print(f"\n=== GDB ANALYSIS SUMMARY ===")
    print(f"Normal exits: {normal_exits}")
    print(f"Crashes detected: {crashes_found}")
    
    if crashes_found > 0:
        print(f"⚠️  WARNING: {crashes_found} node(s) crashed during execution!")
        print(f"   Check the individual log files in {gdb_logs_dir} for details")
    else:
        print("✅ All nodes exited normally - no crashes detected")
    
    return crashes_found == 0
