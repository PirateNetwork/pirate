# TOR SUPPORT IN PIRATE

It is possible to run Pirate as a Tor onion service, and connect to such services.

The following directions assume you have a Tor proxy running on port 9050. Many distributions default to having a SOCKS proxy listening on port 9050, but others may not. In particular, the Tor Browser Bundle defaults to listening on port 9150. See [Tor Project FAQ:TBBSocksPort](https://www.torproject.org/docs/faq.html.en#TBBSocksPort) for how to properly
configure Tor.

## Compatibility

- Starting with version 5.6.0, Pirate only supports Tor version 3 hidden
  services (Tor v3). Tor v2 addresses are ignored by Pirate Core and neither
  relayed nor stored.

- Tor removed v2 support beginning with version 0.4.6.

## How to see information about your Tor configuration via Pirate

There are several ways to see your local onion address in Pirate:
- in the debug log (grep for "tor:" or "AddLocal")
- in the output of RPC `getnetworkinfo` in the "localaddresses" section
- in the output of the CLI `-netinfo` peer connections dashboard

You may set the `-debug=tor` config logging option to have additional
information in the debug log about your Tor configuration.

CLI `-addrinfo` returns the number of addresses known to your node per network
type, including Tor v2 and v3. This is useful to see how many onion addresses
are known to your node for `-onlynet=onion` and how many Tor v3 addresses it
knows when upgrading to Pirate v5.4.3 and up that supports Tor v3 only.

## 1. Run Pirate behind a Tor proxy

The first step is running Pirate behind a Tor proxy. This will already anonymize all
outgoing connections, but more is possible.

    -proxy=ip:port  Set the proxy server. If SOCKS5 is selected (default), this proxy
                    server will be used to try to reach .onion addresses as well.
                    You need to use -noonion or -onion=0 to explicitly disable
                    outbound access to onion services.

    -onion=ip:port  Set the proxy server to use for Tor onion services. You do not
                    need to set this if it's the same as -proxy. You can use -onion=0
                    to explicitly disable access to onion services.
                    Note: Only the -proxy option sets the proxy for DNS requests;
                    with -onion they will not route over Tor, so use -proxy if you
                    have privacy concerns.

    -listen         When using -proxy, listening is disabled by default. If you want
                    to manually configure an onion service (see section 3), you'll
                    need to enable it explicitly.

    -connect=X      When behind a Tor proxy, you can specify .onion addresses instead
    -addnode=X      of IP addresses or hostnames in these parameters. It requires
    -seednode=X     SOCKS5. In Tor mode, such addresses can also be exchanged with
                    other P2P nodes.

    -onlynet=onion  Make outgoing connections only to .onion addresses. Incoming
                    connections are not affected by this option. This option can be
                    specified multiple times to allow multiple network types, e.g.
                    ipv4, ipv6 or onion. If you use this option with values other
                    than onion you *cannot* disable onion connections; outgoing onion
                    connections will be enabled when you use -proxy or -onion. Use
                    -noonion or -onion=0 if you want to be sure there are no outbound
                    onion connections over the default proxy or your defined -proxy.

In a typical situation, this suffices to run behind a Tor proxy:

    ./pirated -proxy=127.0.0.1:9050

## 2. Automatically create a Pirate onion service

Pirate makes use of Tor's control socket API to create and destroy
ephemeral onion services programmatically. This means that if Tor is running and
proper authentication has been configured, Pirate automatically creates an
onion service to listen on. The goal is to increase the number of available
onion nodes.

This feature is enabled by default if Pirate is listening (`-listen`) and
it requires a Tor connection to work. It can be explicitly disabled with
`-listenonion=0`. If it is not disabled, it can be configured using the
`-torcontrol` and `-torpassword` settings.

To see verbose Tor information in the pirated debug log, pass `-debug=tor`.

### Embedded Tor daemon (no system Tor install required)

Builds with embedded onion routing support (the default; disable at build
time with `--disable-embedded-onion-routing`) don't need a separately
installed and configured system Tor at all. `pirated`/`pirate-qt` bundle
their own copy of tor, installed as `pirate-tor` (not the upstream `tor`
filename) precisely so it can never collide with a real system Tor package
that happens to live in the same directory, and launch and manage it
automatically - so the "Control Port" and "Authentication" instructions below
are only relevant if you disable this and fall back to an externally-managed
Tor.

Relevant options:

    -torautostart=1  Automatically launch and manage the bundled pirate-tor daemon
                      (default: 1). Set to 0 to disable and use an externally
                      managed Tor instead - the "Control Port" / "Authentication"
                      setup below then applies.

    -torpath=<path>   Use a specific tor binary instead of the bundled/
                      auto-detected one.

The embedded daemon is only started if `-listenonion` is also enabled
(the default) - it exists to serve the automatic onion service feature
described above, not as a general-purpose SOCKS proxy.

Binary discovery order, if `-torpath` isn't set: a `pirate-tor` binary sitting
next to the running `pirated`/`pirate-qt` executable (checked against the
SHA256 recorded at build time, so a tampered-with sibling binary is rejected
rather than silently used) is tried first, then a plain `tor` on `$PATH` (an
externally-managed system install, which was never going to be named
`pirate-tor`).

Its own torrc, data directory, and logs (`tor.stdout.log`/`tor.stderr.log`)
live under `<datadir>/tor/`. Startup waits up to 180 seconds for the Control
Port to come up; if it doesn't, pirated logs a warning and continues (the
control connection logic below has its own retry logic and may still pick it
up later).

### Control Port

You may need to set up the Tor Control Port. On Linux distributions there may be
some or all of the following settings in `/etc/tor/torrc`, generally commented
out by default (if not, add them):

```
ControlPort 9051
CookieAuthentication 1
CookieAuthFileGroupReadable 1
```

Add or uncomment those, save, and restart Tor (usually `systemctl restart tor`
or `sudo systemctl restart tor` on most systemd-based systems, including recent
Debian and Ubuntu, or just restart the computer).

On some systems (such as Arch Linux), you may also need to add the following
line:

```
DataDirectoryGroupReadable 1
```

### Authentication

Connecting to Tor's control socket API requires one of two authentication
methods to be configured: cookie authentication or pirated's `-torpassword`
configuration option.

#### Cookie authentication

For cookie authentication, the user running pirated must have read access to
the `CookieAuthFile` specified in the Tor configuration. In some cases this is
preconfigured and the creation of an onion service is automatic. Don't forget to
use the `-debug=tor` pirated configuration option to enable Tor debug logging.

If a permissions problem is seen in the debug log, e.g. `tor: Authentication
cookie /run/tor/control.authcookie could not be opened (check permissions)`, it
can be resolved by adding both the user running Tor and the user running
pirated to the same Tor group and setting permissions appropriately.

On Debian-derived systems, the Tor group will likely be `debian-tor` and one way
to verify could be to list the groups and grep for a "tor" group name:

```
getent group | cut -d: -f1 | grep -i tor
```

You can also check the group of the cookie file. On most Linux systems, the Tor
auth cookie will usually be `/run/tor/control.authcookie`:

```
stat -c '%G' /run/tor/control.authcookie
```

Once you have determined the `${TORGROUP}` and selected the `${USER}` that will
run pirated, run this as root:

```
usermod -a -G ${TORGROUP} ${USER}
```

Then restart the computer (or log out) and log in as the `${USER}` that will run
pirated.

#### `torpassword` authentication

For the `-torpassword=password` option, the password is the clear text form that
was used when generating the hashed password for the `HashedControlPassword`
option in the Tor configuration file.

The hashed password can be obtained with the command `tor --hash-password
password` (refer to the [Tor Dev
Manual](https://2019.www.torproject.org/docs/tor-manual.html.en) for more
details).


## 3. Manually create a Pirate onion service

You can also manually configure your node to be reachable from the Tor network.
Add these lines to your `/etc/tor/torrc` (or equivalent config file):

    HiddenServiceDir /var/lib/tor/pirate-service/
    HiddenServicePort 45452 127.0.0.1:45454

The directory can be different of course, but virtual port numbers should be equal to
your pirated's P2P listen port (45452 by default), and target addresses and ports
should be equal to binding address and port for inbound Tor connections (127.0.0.1:45454 by default).

    -externalip=X   You can tell pirate about its publicly reachable addresses using
                    this option, and this can be an onion address. Given the above
                    configuration, you can find your onion address in
                    /var/lib/tor/pirate-service/hostname. For connections
                    coming from unroutable addresses (such as 127.0.0.1, where the
                    Tor proxy typically runs), onion addresses are given
                    preference for your node to advertise itself with.

                    You can set multiple local addresses with -externalip. The
                    one that will be rumoured to a particular peer is the most
                    compatible one and also using heuristics, e.g. the address
                    with the most incoming connections, etc.

    -listen         You'll need to enable listening for incoming connections, as this
                    is off by default behind a proxy.

    -discover       When -externalip is specified, no attempt is made to discover local
                    IPv4 or IPv6 addresses. If you want to run a dual stack, reachable
                    from both Tor and IPv4 (or IPv6), you'll need to either pass your
                    other addresses using -externalip, or explicitly enable -discover.
                    Note that both addresses of a dual-stack system may be easily
                    linkable using traffic analysis.

In a typical situation, where you're only reachable via Tor, this should suffice:

    ./pirated -proxy=127.0.0.1:9050 -externalip=7zvj7a2imdgkdbg4f2dryd5rgtrn7upivr5eeij4cicjh65pooxeshid.onion -listen

(obviously, replace the .onion address with your own). It should be noted that you still
listen on all devices and another node could establish a clearnet connection, when knowing
your address. To mitigate this, additionally bind the address of your Tor proxy:

    ./pirated ... -bind=127.0.0.1

If you don't care too much about hiding your node, and want to be reachable on IPv4
as well, use `discover` instead:

    ./pirated ... -discover

and open port 45452 on your firewall (or use port mapping, i.e., `-upnp` or `-natpmp`).

If you only want to use Tor to reach .onion addresses, but not use it as a proxy
for normal IPv4/IPv6 communication, use:

    ./pirated -onion=127.0.0.1:9050 -externalip=7zvj7a2imdgkdbg4f2dryd5rgtrn7upivr5eeij4cicjh65pooxeshid.onion -discover

## 4. Privacy recommendations

- Do not add anything but Pirate ports to the onion service created in section 3.
  If you run a web service too, create a new onion service for that.
  Otherwise it is trivial to link them, which may reduce privacy. Onion
  services created automatically (as in section 2) always have only one port
  open.

## 5. Crash safety of the embedded Tor daemon

Since the embedded tor daemon runs as its own OS process, a normal graceful
shutdown of `pirated`/`pirate-qt` stops it along the way. But if the node
crashes, is `kill -9`'d, or gets OOM-killed, that shutdown code never runs -
without anything else in place, tor would be left running, orphaned, forever.

To prevent this, builds with embedded onion routing support also launch a
small companion process, `pirate-networking`, alongside `pirated`/`pirate-qt`.
Its only job is to watch for the node disappearing without a clean shutdown
and terminate the embedded tor (and i2pd, if enabled) daemon in that case. No
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
