# I2P support in Treasure Chest

It is possible to run Treasure Chest as an
[I2P (Invisible Internet Project)](https://en.wikipedia.org/wiki/I2P)
service and connect to such services.

This [glossary](https://geti2p.net/en/about/glossary) may be useful to get
started with I2P terminology.

## Run Treasure Chest with an I2P router (proxy)

A running I2P router (proxy) with [SAM](https://geti2p.net/en/docs/api/samv3)
enabled is required (there is an [official one](https://geti2p.net) and
[a few alternatives](https://en.wikipedia.org/wiki/I2P#Routers)). Notice the IP
address and port the SAM proxy is listening to; usually, it is
`127.0.0.1:7656`. Once it is up and running with SAM enabled, use the following
Treasure Chest options:

```
-i2psam=<ip:port>
     I2P SAM proxy to reach I2P peers and accept I2P connections (default:
     none)

-i2pacceptincoming
     If set and -i2psam is also set then incoming I2P connections are
     accepted via the SAM proxy. If this is not set but -i2psam is set
     then only outgoing connections will be made to the I2P network.
     Ignored if -i2psam is not set. Listening for incoming I2P
     connections is done through the SAM proxy, not by binding to a
     local address and port (default: 1)
```

In a typical situation, this suffices:

```
pirated -i2psam=127.0.0.1:7656
```

The first time Treasure Chest connects to the I2P router, its I2P address (and
corresponding private key) will be automatically generated and saved in a file
named `i2p_private_key` in the Treasure Chest data directory.

## Embedded i2pd daemon (no separate I2P router required)

Builds with embedded onion routing support (the default; disable at build
time with `--disable-embedded-onion-routing`) don't need a separately
installed and configured I2P router at all. `pirated`/`pirate-qt` bundle their
own copy of i2pd, installed as `pirate-i2pd` (not the upstream `i2pd`
filename) precisely so it can never collide with a real system i2pd package
that happens to live in the same directory, launch it with its SAM API
enabled, and manage it automatically.

Relevant options:

    -i2pdautostart=1  Automatically launch and manage the bundled pirate-i2pd daemon
                      (default: 1). When enabled, `-i2psam` defaults to
                      `127.0.0.1:7656` automatically if you haven't set it
                      yourself, so I2P works out of the box. Set to 0 to
                      disable and use an externally managed I2P router instead
                      (set `-i2psam` yourself, as described above).

    -i2pdpath=<path>  Use a specific i2pd binary instead of the bundled/
                      auto-detected one.

Binary discovery order, if `-i2pdpath` isn't set: a `pirate-i2pd` binary
sitting next to the running `pirated`/`pirate-qt` executable (checked against
the SHA256 recorded at build time, so a tampered-with sibling binary is
rejected rather than silently used) is tried first, then a plain `i2pd` on
`$PATH` (an externally-managed system install, which was never going to be
named `pirate-i2pd`).

Its own i2pd.conf, data directory, and logs (`i2pd.stdout.log`/
`i2pd.stderr.log`) live under `<datadir>/i2pd/`. Startup waits up to 180
seconds for the SAM port to come up; if it doesn't, pirated logs a warning and
continues (I2P session creation has its own retry logic and may still pick it
up later).

## Additional configuration options related to I2P

You may set the `debug=i2p` config logging option to have additional
information in the debug log about your I2P configuration and connections. Run
`pirate-cli help logging` for more information.

It is possible to restrict outgoing connections in the usual way with
`onlynet=i2p`. I2P support was added to Treasure Chest in version 22.0 (mid 2021)
and there may be fewer I2P peers than Tor or IP ones. Therefore, using
`onlynet=i2p` alone (without other `onlynet=`) may make a node more susceptible
to [Sybil attacks](https://en.bitcoin.it/wiki/Weaknesses#Sybil_attack). Use
`pirate-cli -addrinfo` to see the number of I2P addresses known to your node.

## I2P related information in Treasure Chest

There are several ways to see your I2P address in Treasure Chest:
- in the debug log (grep for `AddLocal`, the I2P address ends in `.b32.i2p`)
- in the output of the `getnetworkinfo` RPC in the "localaddresses" section
- in the output of `pirate-cli -netinfo` peer connections dashboard

To see which I2P peers your node is connected to, use `pirate-cli -getpeerinfo` RPC.

## Compatibility

Treasure Chest uses the [SAM v3.1](https://geti2p.net/en/docs/api/samv3) protocol
to connect to the I2P network. Any I2P router that supports it can be used.

## Crash safety of the embedded i2pd daemon

Since the embedded i2pd daemon runs as its own OS process, a normal graceful
shutdown of `pirated`/`pirate-qt` stops it along the way. But if the node
crashes, is `kill -9`'d, or gets OOM-killed, that shutdown code never runs -
without anything else in place, i2pd would be left running, orphaned, forever.

To prevent this, builds with embedded onion routing support also launch a
small companion process, `pirate-networking`, alongside `pirated`/`pirate-qt`.
Its only job is to watch for the node disappearing without a clean shutdown
and terminate the embedded i2pd (and tor, if enabled) daemon in that case. No
configuration is required - it's started and stopped automatically together
with the embedded daemons, and exits on its own once the node has shut down
cleanly. You can see it running alongside your node (e.g. `ps aux | grep
pirate-networking`), and its own logs live at
`<datadir>/networking/networking.stdout.log` and
`networking.stderr.log`.

Like `pirate-tor`/`pirate-i2pd`, the `pirate-networking` sibling binary is
itself SHA256-verified against the exact copy this build shipped before
`pirated`/`pirate-qt` will launch it, for the same reason: it's handed a live
control channel over the embedded daemons, so a tampered-with same-named file
in the install directory is rejected rather than silently run.
