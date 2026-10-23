#!/usr/bin/env python3
# E2E: Verifies that traffic the device originates prefers Ethernet over WiFi on a shared subnet.

"""End-to-end check of which interface the device uses for connections it opens.

Regression guard for GideonZ/1541ultimate#862. With Ethernet and WiFi up on the
same subnet, lwIP's route lookup returned the netif added last, which is WiFi,
so every connection the device opened to a host on that subnet left from the
WiFi address. The policy is that traffic the device starts, with no request to
answer, prefers Ethernet.

This host is the network peer. The C64 opens a TCP connection and a UDP socket
through the UCI network target to ports this host binds, and the suite reads the
address the device's packets come from. Both have to carry the Ethernet
address. Unpatched firmware with WiFi joined sends both from the WiFi address.

Which interface is which comes from the device: the network target numbers its
interfaces and reports each one's MAC, and GET /v1/info reports the Ethernet MAC
as `ethernet_mac`. The suite needs two interfaces with an address on one subnet
that also holds this host, or the route lookup is never ambiguous and a pass
would prove nothing; it skips otherwise, so a device with WiFi off, or a host on
another subnet, skips rather than passes.

It covers where the device's own traffic starts. It does not look at the
source MAC of replies, which needs a capture on the wire; the unit tests in
software/io/network/tests cover that selection, and the reply path on hardware
is checked with a capture on this host's wired port.

Supported on any Ultimate whose FPGA provides the command interface. The suite
enables the "Command Interface" setting and restores it on exit. Nothing else
on the device is changed.

    ./run-tests --suite uci-net-route u64
"""

import argparse
import ipaddress
import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import targets  # noqa: E402
from api import UltimateApi  # noqa: E402
from net_target_test import (CFG_CMD_IF, CONFIG_CATEGORY, STATUS_OK, Net, Peer,  # noqa: E402
                             local_address_towards, restore_settings)
from report import (Failure, check, check_ok, check_skip, check_start, detail,  # noqa: E402
                    format_exception, suite_fail, suite_ok, suite_skip)
from uci import TARGET_NETWORK, Uci, interface_present  # noqa: E402

SUITE = "net_route_test"

NET_CMD_GET_NETADDR = 0x04
NET_CMD_GET_IPADDR = 0x05
# The network target numbers its interfaces from 0; no device has more than a few.
MAX_INTERFACES = 4
CONNECTIONS = 3


def interface_addresses(net: Net) -> list[tuple[int, ipaddress.IPv4Interface]]:
    """(index, address/netmask) of every interface that has an address."""
    found = []
    for index in range(MAX_INTERFACES):
        reply = net.uci.transact(bytes([TARGET_NETWORK, NET_CMD_GET_IPADDR, index]),
                                 single_part=False)
        if reply.status_text != STATUS_OK or len(reply.data) < 8:
            break
        address = ipaddress.IPv4Address(bytes(reply.data[0:4]))
        netmask = ipaddress.IPv4Address(bytes(reply.data[4:8]))
        if not address.is_unspecified:
            found.append((index, ipaddress.IPv4Interface(f"{address}/{netmask}")))
    return found


def interface_mac(net: Net, index: int) -> str:
    """The MAC of interface `index`, as GET /v1/info writes it."""
    reply = net.uci.transact(bytes([TARGET_NETWORK, NET_CMD_GET_NETADDR, index]),
                             single_part=False)
    if reply.status_text != STATUS_OK or len(reply.data) < 6:
        raise Failure(f"GET_NETADDR {index} answered {reply.status_text!r} with {reply.data!r}")
    return ":".join(f"{b:02x}" for b in reply.data[:6])


def shared_subnet(addresses, host: str) -> list[tuple[int, ipaddress.IPv4Interface]] | None:
    """The interfaces whose subnet also holds `host`, when there are two or more."""
    on_link = [(i, a) for i, a in addresses if ipaddress.IPv4Address(host) in a.network]
    return on_link if len(on_link) >= 2 else None


def expect_origin(what: str, seen: str, wired: ipaddress.IPv4Address,
                  wireless: list[ipaddress.IPv4Address]) -> None:
    detail(f"{what} arrived from {seen}")
    if seen != str(wired):
        which = " (the WiFi address)" if ipaddress.IPv4Address(seen) in wireless else ""
        raise Failure(f"{what} came from {seen}{which}, expected the Ethernet address {wired}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    cli.add_device_arguments(parser, password=None, timeout=30.0, colour=False)
    parser.add_argument("-b", "--busy-timeout", type=float, default=15.0,
                        help="How long one command may stay in Command Busy before it "
                             "counts as wedged.")
    args = parser.parse_args()

    target = targets.parse(args.host)
    device = UltimateApi(target.device, args.password, args.timeout)
    computer = UltimateApi(target.computer, args.password, args.timeout)
    uci = Uci(computer.machine, args.busy_timeout)

    original: dict[str, str] = {}
    interface_enabled = False
    peer: Peer | None = None
    failure = ""
    try:
        with check(f"read {CONFIG_CATEGORY!r}"):
            config = device.configs.category(CONFIG_CATEGORY)
            if CFG_CMD_IF not in config:
                raise Failure(f"this device has no {CFG_CMD_IF!r} setting; it cannot run this suite")
            original = {CFG_CMD_IF: str(config[CFG_CMD_IF])}
        with check("enable the Command Interface registers at $DF1B-$DF1F"):
            device.configs.set(CONFIG_CATEGORY, CFG_CMD_IF, "Enabled")
            interface_enabled = True
            uci.release()

        label = "the device originates traffic from its Ethernet address"
        if not interface_present(uci):
            check_start(label)
            reason = "$DF1B-$DF1F all read $FF, so no command interface is present"
            check_skip(reason)
            return finish(device, uci, original, interface_enabled, peer, failure, reason)

        net = Net(uci)
        host = local_address_towards(target.device)
        with check("the device reports its interfaces"):
            addresses = interface_addresses(net)
            detail(", ".join(f"{i}: {a}" for i, a in addresses) or "none")
        shared = shared_subnet(addresses, host)
        if shared is None:
            check_start(label)
            reason = (f"needs two interfaces with an address on the subnet of this host "
                      f"({host}); the device has {[str(a) for _, a in addresses]}")
            check_skip(reason)
            return finish(device, uci, original, interface_enabled, peer, failure, reason)

        wired_mac = str(device.info().extra.get("ethernet_mac", "")).lower()
        wired_entry = next(((i, a) for i, a in shared
                            if wired_mac and interface_mac(net, i) == wired_mac), None)
        if wired_entry is None:
            check_start(label)
            reason = (f"none of the interfaces on this subnet ({[str(a) for _, a in shared]}) "
                      f"has the Ethernet MAC GET /v1/info reports ({wired_mac or 'none'})")
            check_skip(reason)
            return finish(device, uci, original, interface_enabled, peer, failure, reason)
        wired = wired_entry[1]
        others = [(i, a) for i, a in shared if i != wired_entry[0]]
        wireless = [a.ip for _, a in others]
        detail(f"Ethernet {wired.ip}, other {[str(a) for a in wireless]}, this host {host}")

        check_start("this host can act as the device's network peer")
        peer = Peer(host)
        check_ok(f"{peer.ip} UDP {peer.udp_port}, TCP {peer.tcp_port}")

        with check(f"{CONNECTIONS} TCP connections the device opens come from its Ethernet address"):
            for _ in range(CONNECTIONS):
                handle = net.open_tcp(peer.ip, peer.tcp_port)
                try:
                    conn = peer.accept_tcp()
                    expect_origin("a TCP connection", conn.getpeername()[0], wired.ip, wireless)
                    conn.close()
                finally:
                    net.close(handle)

        with check("a UDP datagram the device sends comes from its Ethernet address"):
            handle = net.open_udp(peer.ip, peer.udp_port)
            try:
                source, _ = peer.learn_udp_peer(net, handle)
                expect_origin("a UDP datagram", source, wired.ip, wireless)
            finally:
                net.close(handle)
    except Failure as exc:
        failure = str(exc)
    except Exception as exc:  # noqa: BLE001
        failure = format_exception(exc)
    return finish(device, uci, original, interface_enabled, peer, failure)


def finish(device, uci, original, interface_enabled, peer, failure: str,
           skipped: str = "") -> int:
    """Hand the interface and the setting back, then report. `skipped` is why the
    suite could not run; it ends the suite as skipped, never as passed."""
    if peer is not None:
        peer.close()
    released = uci.release() if interface_enabled else True
    try:
        restore_settings(device, original, False)
    except Failure as exc:
        failure = failure or str(exc)
    if not released:
        failure = failure or "the command interface did not return to Idle"
    if failure:
        suite_fail(SUITE, failure)
        return 1
    if skipped:
        suite_skip(SUITE, skipped)
        return 0
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
