#!/usr/bin/env python
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

# Exercise the listtransactions API

import sys; assert sys.version_info < (3,), ur"This script does not run under Python 3. Please use Python 2.7.x."

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, start_nodes, \
    connect_nodes_bi

from decimal import Decimal

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

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir, extra_args=[['-debug=zrpc']] * 3 )
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        self.is_network_split=False
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()
        self.nodes[2].generate(720)
        self.sync_all()

    def run_test(self):
        # Simple send, 0 to 1:
        txid = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.1)
        self.sync_all()
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid},
                           {"category":"send","account":"","amount":Decimal("-0.1"),"confirmations":0})
        check_array_result(self.nodes[1].listtransactions(),
                           {"txid":txid},
                           {"category":"receive","account":"","amount":Decimal("0.1"),"confirmations":0})
        # mine a block, confirmations should change:
        self.nodes[0].generate(1)
        self.sync_all()
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid},
                           {"category":"send","account":"","amount":Decimal("-0.1"),"confirmations":1})
        check_array_result(self.nodes[1].listtransactions(),
                           {"txid":txid},
                           {"category":"receive","account":"","amount":Decimal("0.1"),"confirmations":1})

        # send-to-self:
        txid = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 0.2)

        # mine a block, confirmations should change:
        self.nodes[0].generate(1)
        self.sync_all()
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid, "category":"send"},
                           {"amount":Decimal("-0.2")})
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid, "category":"receive"},
                           {"amount":Decimal("0.2")})

        # sendmany from node1: twice to self, twice to node2:
        send_to = { self.nodes[0].getnewaddress() : 0.11,
                    self.nodes[1].getnewaddress() : 0.22,
                    self.nodes[0].getaccountaddress("") : 0.33,
                    self.nodes[1].getaccountaddress("") : 0.44 }
        txid = self.nodes[1].sendmany("", send_to)

        # mine a block, confirmations should change:
        self.nodes[0].generate(1)
        self.sync_all()
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"send","amount":Decimal("-0.11")},
                           {"txid":txid} )
        check_array_result(self.nodes[0].listtransactions(),
                           {"category":"receive","amount":Decimal("0.11")},
                           {"txid":txid} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"send","amount":Decimal("-0.22")},
                           {"txid":txid} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"receive","amount":Decimal("0.22")},
                           {"txid":txid} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"send","amount":Decimal("-0.33")},
                           {"txid":txid} )
        check_array_result(self.nodes[0].listtransactions(),
                           {"category":"receive","amount":Decimal("0.33")},
                           {"txid":txid, "account" : ""} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"send","amount":Decimal("-0.44")},
                           {"txid":txid, "account" : ""} )
        check_array_result(self.nodes[1].listtransactions(),
                           {"category":"receive","amount":Decimal("0.44")},
                           {"txid":txid, "account" : ""} )

if __name__ == '__main__':
    ListTransactionsTest().main()
