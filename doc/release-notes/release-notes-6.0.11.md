Recommended update
==================

6.0.11 is a recommended networking and connectivity release. It improves how a
node builds and keeps a healthy, diverse set of outbound peer connections, and
resolves cases where a node could end up stuck with only a handful of peers.

This release makes no consensus changes and does not change the block-index
format, so **no reindex is required** — it is a drop-in upgrade over 6.0.10.

6.0.10 remains a mandatory security release. A node must be on 6.0.10 or later
for the corrected consensus rules; 6.0.11 does not relax that requirement, it
only builds on top of it. Upgrading is strongly recommended for every node, and
especially for nodes helping the network reconverge, since the connectivity
fixes below directly affect how reliably a node finds and stays connected to
good peers.

Notable changes
===============

Healthier spread of outbound connections across networks
--------------------------------------------------------

A node keeps a minimum number of outbound connections on each network it can
reach, so its peers are not all drawn from whichever network happens to be best
represented in its address book. IPv4, the network nearly every peer can reach,
now has a higher floor of its own: a node keeps building out IPv4 peers until it
has a solid backbone before settling for the smaller per-network minimum on the
other networks.

A network that has no working route on a given host (most commonly IPv6 on a
host with no IPv6 connectivity) no longer monopolizes the node's connection
attempts. Such a network is backed off after its dials fail and is set aside
from the connection-diversity target, so the node spends its attempts on the
networks it can actually reach instead of repeatedly failing against one it
cannot. The backed-off network is still retried periodically and immediately
regains its place the moment a connection to it succeeds, so a network that
later becomes reachable recovers on its own. IPv4 is never backed off.

I2P no longer throttles clearnet peer discovery
-----------------------------------------------

The regular connection dialer never dials I2P peers — those have their own
dedicated dialer — but it was still counting I2P toward its connection-diversity
target, so whenever the node was short on I2P peers the dialer spent its search
budget looking for I2P peers it would never open, starving normal IPv4/IPv6 peer
discovery. This left some nodes stuck with very few peers. The regular dialer no
longer steers toward networks it does not serve, so clearnet (and Tor) peer
discovery proceeds normally regardless of I2P peer counts.

Peer selection no longer stops at an already-connected network group
--------------------------------------------------------------------

While searching for a new peer, encountering a candidate in a network group the
node is already connected to no longer ends the search for that pass. The node
keeps looking through the remaining candidates, so a node does not repeatedly
give up early and under-connect.

Explicitly specified privacy peers connect correctly
-----------------------------------------------------

A peer given explicitly (for example through addnode or connect) as an .onion or
.i2p destination is now resolved to its network before address validation,
rather than being rejected as an unroutable address. Explicitly specified
privacy peers connect as intended.

Build script changes (headless builds)
--------------------------------------

The headless build script (zcutil/build.sh) now packages the finished
daemon/CLI binaries into a repository-local artifacts folder and a .zip bundle
as part of a normal build. The test suite is no longer built by default; pass
--enable-tests to build it (--enable-lcov still builds the tests, since coverage
is measured by running them). An inversion in the --disable-tests handling was
also corrected.

Upgrading
=========

Stop the old node, install 6.0.11, and start it again. No reindex and no
configuration change is required.
