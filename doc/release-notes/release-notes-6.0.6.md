Notable changes
===============

RPC stalls after startup fixed
------------------------------

After startup, RPCs that need the main chain lock - `getblockchaininfo`,
`z_getbalance` and most other calls - could stall for tens of seconds, and in
one report for about two minutes, while the node otherwise kept running.
Monitoring tools and pools polling those RPCs saw a node that looked hung.

The cause was the peer address database. When a peer finished its handshake or
disconnected, the node updated that database while still holding the main chain
lock, so every other thread queued behind it whenever the database was busy.
The network threads kept it busy: three of them selected addresses to dial in
loops, and each selection could sleep 100ms per 1000 probes with the database
locked - up to about 20 seconds per call on a node that knew only a few peers it had
successfully connected to. The node this was diagnosed on also had no I2P router
running, so every I2P dial failed at once and was retried, which kept the
database busy.

- The address database is now updated after the main chain lock is released, in
  both the handshake and disconnect paths.
- Address selection no longer sleeps while holding the database lock.
- The I2P dial threads look up I2P candidates in a single pass over the address
  database instead of calling the general selector up to 50 times and discarding
  most of the results.
- When the I2P router cannot be reached, dialing through it backs off (5 seconds,
  doubling to 60) instead of retrying every second. Only a failed handshake with
  the router counts, so a stale or unknown I2P address does not pause dialing
  through a healthy router. The backoff clears as soon as the router answers, so
  outbound I2P dialing resumes within about a minute of the router returning; the
  threads that keep the I2P session and listener up retry on their own schedule
  (up to every 5 minutes) as before.
- `getpeerlist` read the address database without its lock, racing with the
  threads that modify it; it now takes the lock, and no longer holds the main
  chain lock while it waits.

Tor peers get their own connection slots
----------------------------------------

Nodes kept only about 2 outbound Tor peers. The regular dialer steers toward a
network only until it has 2 outbound peers, then goes back to picking addresses
in proportion to what it knows, and Tor is a small share of that. This matters
because locally-originated transactions with `-privatetxrelay` are relayed only
to outbound Tor peers (plus one burn-after-use I2P identity), so that number is
the size of the relay set.

Tor now has a dedicated set of outbound slots and its own dial thread, the way
the I2P relay pool does. The new option `-toroutbound=<n>` sets how many
(default 8, range 0-16). They are in addition to the regular outbound
connections, and Tor peers no longer compete with clearnet peers for a slot.
As with I2P, dialing through the Tor daemon backs off while its SOCKS port
cannot be reached (daemon not started or crashed), and dead onion peers do not
count toward that.

- Nodes that can only reach Tor (`-onlynet=onion`) keep dialing Tor from the
  regular connection slots as before, so they do not lose capacity.
- `-connect` still means exactly the peers listed: no Tor slots are reserved.
- `-toroutbound=0` restores the previous behavior.
- Tor that is running but still bootstrapping is not detected; dials made in
  that state fail quickly and are retried normally.

Privacy trade-off: every outbound Tor peer receives locally-originated
transactions first and learns this node's onion address, so more Tor slots mean
a larger relay set and better resilience, but also more peers that see your own
transactions before anyone else. Lower `-toroutbound` if you would rather keep
that set small.

`-onlynet` now keeps Tor and I2P off when it excludes them
-----------------------------------------------------------

`-onlynet` switched off the networks it excluded, but several things then turned
Tor or I2P back on because a proxy or daemon for them was available: `-proxy`,
`-onion=<address>`, `-i2psam`, and the Tor control code once it connected to the
Tor daemon. A node started with, say, `-onlynet=ipv4 -proxy=127.0.0.1:9050`
still connected to onion peers. Each of those now checks `-onlynet` first.

This predates this release, but the Tor slots above would have grown its effect
from about 2 Tor connections to 8. The hidden service is still created
when `-onlynet` excludes onion; `-onlynet` restricts outbound connections only.

Upgrade notes
-------------

This is a patch release. There are no consensus, wallet-format or database
changes; it is safe to upgrade in place, and no reindex or rescan is needed.

Nodes that run Tor will hold up to 8 extra outbound connections by default, so
expect a few more Tor circuits and open sockets. The file descriptor limit
requested at startup includes them. Set `-toroutbound=0` to keep the previous
behavior, or a lower number to keep the privacy exposure described above small.

If you use `-onlynet` together with `-proxy`, `-onion` or `-i2psam`, check that
the networks you expect are listed: networks `-onlynet` excludes now stay
excluded, where previously one of those options could re-enable them.

Anyone whose monitoring or pool software polls RPCs frequently should upgrade;
that is where the stalls were visible.

Changelog
=========

Cryptoforge:
  Fix RPC stalls: stop holding cs_main across addrman, make Select()
  non-blocking. (9bcd8aa0b)
  Give Tor its own outbound slots and dial thread; honor -onlynet for
  Tor/I2P. (64e972bfa)
  Bump version to 6.0.6.50 (patch).
