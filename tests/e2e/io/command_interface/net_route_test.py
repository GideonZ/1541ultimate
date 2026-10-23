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
another subnet, skips rather than passes. Both interfaces also have to have their
link up: an address stays configured when its link goes down, and then every
connection leaves from Ethernet on unpatched firmware too. Nothing but the menu
shows live link state, so the suite reads the interface rows of the root menu over
GET /v1/machine:menu_screen (opening the menu and closing it again if it was
closed) and skips when it cannot read them or either link is down.

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
import re
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import pacing  # noqa: E402
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


# What the device's root menu draws for each interface, from getDisplayString() in
# network_interface.cc and network_esp32.cc: "Net0    IP: 10.0.0.5   Link Up" or
# "Net0    MAC 02:15:41:..  Link Down" for the wired one, the same with "WiFi" for the
# wireless one, which also has states with no link wording ("Disabled", "Scanning").
LINK_ROW = re.compile(r"(?:^|\s)(Net\d+|WiFi)\s+(?:IP:\s*(\d+\.\d+\.\d+\.\d+)|MAC\s+[0-9A-Fa-f:]{17})?"
                      r".*?\b(Link Up|Link Down)\s*$")
MENU_OPEN_SECONDS = 5.0


def link_rows(rows: list[str]) -> list[tuple[str, str, bool]]:
    """(row name, address or "", link up) for every interface row on the menu screen."""
    found = []
    for row in rows:
        match = LINK_ROW.search(row.rstrip())
        if match:
            found.append((match.group(1), match.group(2) or "", match.group(3) == "Link Up"))
    return found


def link_problem(rows: list[str], wired: ipaddress.IPv4Address,
                 wireless: list[ipaddress.IPv4Address]) -> str:
    """Why the suite cannot tell a routing fault from a missing link; "" when both are up.

    GET_IPADDR returns the stored address, which a static configuration keeps with the
    link down, so link state comes from the menu: the wired row reads Link Up once the
    PHY has linked and the netif is up (rmii_interface.cc, NetworkInterface::link_up),
    the WiFi row once the module reports Connected, which is when it brings its netif
    up (wifi.cc). Each Link Up row carries the interface's address, which is how it is
    matched to the UCI interface; a Link Down row does not.
    """
    found = link_rows(rows)
    if not found:
        return "the menu screen shows no Net or WiFi row, so link state cannot be read"
    up = {address for _, address, is_up in found if is_up}
    for name, address in [("Ethernet", wired)] + [("WiFi", a) for a in wireless]:
        if str(address) in up:
            continue
        down = [row for row, _, is_up in found
                if not is_up and (row == "WiFi") == (name == "WiFi")]
        if down:
            return f"the {name} link is down ({address} is still configured)"
        return f"no menu row shows {address} ({name}) with Link Up"
    return ""


def read_link_rows(machine) -> list[str]:
    """The root menu's rows, opening the menu if it is closed and closing it again.

    An empty list when the menu will not open. A menu that was already open is left open
    and read where it is: if it is not at the root there are no interface rows, which the
    caller reports as unreadable.
    """
    rows = machine.menu_rows()
    if rows:
        return rows
    machine.menu_button()
    try:
        deadline = time.monotonic() + MENU_OPEN_SECONDS
        while not rows and time.monotonic() < deadline:
            time.sleep(pacing.MENU_TOGGLE_SETTLE_SECONDS)
            rows = machine.menu_rows()
        return rows
    finally:
        if rows:
            machine.close_menu_from_anywhere()


class Run:
    """What the cleanup has to undo, whatever stopped the run."""

    def __init__(self) -> None:
        self.original: dict[str, str] = {}
        # Whether $DF1B-$DF1F answer, which is not whether the setting was changed: a C64
        # Ultimate reads the setting as Enabled with the whole window at $FF.
        self.present = False
        self.peer: Peer | None = None


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

    run = Run()
    failure = ""
    skipped = ""
    try:
        skipped = run_checks(target, device, uci, run)
    except Failure as exc:
        failure = str(exc)
    except Exception as exc:  # noqa: BLE001
        failure = format_exception(exc)
    finally:
        # Also for KeyboardInterrupt and SIGTERM-style exits, which skip the handlers above.
        failure = failure or hand_back(device, uci, run)
    if failure:
        suite_fail(SUITE, failure)
        return 1
    if skipped:
        suite_skip(SUITE, skipped)
        return 0
    suite_ok(SUITE)
    return 0


def run_checks(target, device, uci, run: Run) -> str:
    """Run the checks; returns why the suite could not run, "" when it did."""
    with check(f"read {CONFIG_CATEGORY!r}"):
        config = device.configs.category(CONFIG_CATEGORY)
        if CFG_CMD_IF not in config:
            raise Failure(f"this device has no {CFG_CMD_IF!r} setting; it cannot run this suite")
        run.original = {CFG_CMD_IF: str(config[CFG_CMD_IF])}
    with check("enable the Command Interface registers at $DF1B-$DF1F"):
        device.configs.set(CONFIG_CATEGORY, CFG_CMD_IF, "Enabled")
        run.present = interface_present(uci)
        if run.present:
            uci.release()

    label = "the device originates traffic from its Ethernet address"
    if not run.present:
        check_start(label)
        reason = "$DF1B-$DF1F all read $FF, so no command interface is present"
        check_skip(reason)
        return reason

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
        return reason

    wired_mac = str(device.info().extra.get("ethernet_mac", "")).lower()
    wired_entry = next(((i, a) for i, a in shared
                        if wired_mac and interface_mac(net, i) == wired_mac), None)
    if wired_entry is None:
        check_start(label)
        if not wired_mac:
            reason = ("GET /v1/info reports no ethernet_mac, which firmware before v3.15a "
                      "does not, so the wired interface cannot be told from the WiFi one")
        else:
            reason = (f"none of the interfaces on this subnet ({[str(a) for _, a in shared]}) "
                      f"has the Ethernet MAC GET /v1/info reports ({wired_mac})")
        check_skip(reason)
        return reason
    wired = wired_entry[1]
    others = [(i, a) for i, a in shared if i != wired_entry[0]]
    wireless = [a.ip for _, a in others]
    detail(f"Ethernet {wired.ip}, other {[str(a) for a in wireless]}, this host {host}")

    # An address stays configured when its link goes down, so having two addresses on the
    # subnet proves nothing about the route: with WiFi down every connection leaves from
    # Ethernet on unpatched firmware too, and with Ethernet down it cannot be the winner.
    problem = link_problem(read_link_rows(device.machine), wired.ip, wireless)
    if problem:
        check_start(label)
        check_skip(problem)
        return problem
    detail("the menu shows Link Up for both interfaces")

    check_start("this host can act as the device's network peer")
    run.peer = Peer(host)
    check_ok(f"{run.peer.ip} UDP {run.peer.udp_port}, TCP {run.peer.tcp_port}")

    with check(f"{CONNECTIONS} TCP connections the device opens come from its Ethernet address"):
        for _ in range(CONNECTIONS):
            handle = net.open_tcp(run.peer.ip, run.peer.tcp_port)
            try:
                conn = run.peer.accept_tcp()
                expect_origin("a TCP connection", conn.getpeername()[0], wired.ip, wireless)
                conn.close()
            finally:
                net.close(handle)

    with check("a UDP datagram the device sends comes from its Ethernet address"):
        handle = net.open_udp(run.peer.ip, run.peer.udp_port)
        try:
            source, _ = run.peer.learn_udp_peer(net, handle)
            expect_origin("a UDP datagram", source, wired.ip, wireless)
        finally:
            net.close(handle)
    return ""


def hand_back(device, uci, run: Run) -> str:
    """Put the setting back first, then close the peer and hand the interface back.

    Each step runs whatever the one before it did. Returns the first problem, "" if none.
    """
    problems = []
    try:
        restore_settings(device, run.original, False)
    except Failure as exc:
        problems.append(str(exc))
    except Exception as exc:  # noqa: BLE001
        problems.append(format_exception(exc))
    if run.peer is not None:
        try:
            run.peer.close()
        except Exception as exc:  # noqa: BLE001
            problems.append(format_exception(exc))
    # An interface that is not there cannot return to Idle: release() reads $FF as Data More.
    if run.present:
        try:
            if not uci.release():
                problems.append("the command interface did not return to Idle")
        except Exception as exc:  # noqa: BLE001
            problems.append(format_exception(exc))
    return problems[0] if problems else ""


if __name__ == "__main__":
    raise SystemExit(main())
