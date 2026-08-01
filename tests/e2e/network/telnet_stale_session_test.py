#!/usr/bin/env python3
# E2E: Verifies half-open Telnet sessions are reaped and listener capacity recovers.

"""Red/green E2E for the Telnet half-open session leak.

A telnet peer that vanishes at the network level (WiFi drop, powered-off phone,
AP roam) sends no FIN/RST and never ACKs the device's keepalive probes. Without
the fix its session slot leaks and the 4-slot listener wedges; with the fix TCP
keepalive reaps the dead session (~35s) and capacity recovers.

The vanished peer is faked by opening the victims from a throwaway IP alias,
then deleting it so the host stops answering ARP/keepalive. Managing the alias
needs root, so the script elevates only `ip addr add/del` via `sudo -n` (grant
passwordless sudo for `ip`, or run under sudo).

The interface and the throwaway address are derived from the route this host
uses to reach the device, so no interface name or subnet is hard coded. Both
can still be overridden with --iface and --victim-ip. The device must be on the
same subnet as this host: a peer behind a router keeps being answered by that
router, so deleting the alias would not make the peer vanish.

Exit 0 = GREEN (all slots reaped, full capacity recovers); non-zero = RED / setup error.
"""

import argparse
import ipaddress
import json
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

TELNET_PORT = 23
DEFAULT_MAX_SESSIONS = 4  # mirrors TELNET_MAX_SESSIONS in software/network/socket_gui.cc
BUSY_MARKER = b"Too many connections"
FREE_CONFIRM_BYTES = 64  # larger than any busy reply; excess with no marker = banner
IP_CMD = ["sudo", "-n", "ip"]  # only ip needs root
VICTIM_SEARCH_DEPTH = 16  # highest host addresses of the subnet to consider
FALLBACK_PREFIX_LEN = 24  # only used when detection failed and both overrides were given


def log(msg: str) -> None:
    print(f"[telnet-stale-e2e] {msg}", flush=True)


def reset_machine(host: str) -> None:
    for attempt in range(12):
        try:
            request = urllib.request.Request(
                f"http://{host}/v1/machine:menu_screen", method="GET"
            )
            with urllib.request.urlopen(request, timeout=5.0):
                pass
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                break
            raise
        # F8 leaves the menu from any depth; RUN/STOP is the fallback for the
        # editors it does not reach. Never send RETURN blind: in a browser it
        # activates the entry under the cursor, and on the Assembly 64 entry that
        # opens a network-backed form whose edit field parks the UI task.
        keys = ["left_shift", "f7"] if attempt < 8 else ["run_stop"]
        body = json.dumps({
            "events": [{"kind": "keyboard", "inputs": keys, "transition": "tap"}]
        }).encode("utf-8")
        request = urllib.request.Request(
            f"http://{host}/v1/machine:input",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(request, timeout=5.0):
            pass
        time.sleep(0.25)
    else:
        request = urllib.request.Request(
            f"http://{host}/v1/machine:menu_button", data=b"", method="PUT"
        )
        with urllib.request.urlopen(request, timeout=5.0):
            pass
        time.sleep(0.5)

    request = urllib.request.Request(
        f"http://{host}/v1/machine:reset", data=b"", method="PUT"
    )
    with urllib.request.urlopen(request, timeout=5.0):
        pass
    time.sleep(1.0)


def _connect(host: str, *, source_ip: str | None = None, timeout: float = 4.0) -> socket.socket:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    if source_ip:
        s.bind((source_ip, 0))
    s.connect((host, TELNET_PORT))
    return s


def probe_is_free(host: str) -> bool:
    """Fresh connection returned a banner, not the busy reply (split-segment safe)."""
    try:
        s = _connect(host)
    except OSError as exc:
        log(f"probe connect failed: {exc}")
        return False
    try:
        deadline = time.time() + 1.5
        buf = b""
        while time.time() < deadline and len(buf) < 4096:
            s.settimeout(max(0.05, deadline - time.time()))
            try:
                chunk = s.recv(256)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            if BUSY_MARKER in buf:
                return False
            if len(buf) >= FREE_CONFIRM_BYTES:
                return True
        return len(buf) > 0 and BUSY_MARKER not in buf
    finally:
        s.close()


def measure_capacity(host: str, cap: int) -> int:
    """Count how many concurrent sessions the listener will currently accept."""
    conns = []
    free = 0
    for _ in range(cap + 1):
        try:
            s = _connect(host)
            time.sleep(0.15)
            data = s.recv(64)
            if BUSY_MARKER in data:
                s.close()
            else:
                conns.append(s)
                free += 1
        except OSError:
            break
    for s in conns:
        s.close()
    time.sleep(2.0)  # let the clean closes reap first
    return free


def run_cmd(args: list[str]) -> None:
    subprocess.run(args, check=True, capture_output=True, text=True)


class SetupError(RuntimeError):
    pass


def query_ip(args: list[str]) -> list:
    """Run a read-only `ip -j` query. These need no root, unlike `ip addr add/del`."""
    result = subprocess.run(["ip", "-j"] + args, capture_output=True, text=True)
    if result.returncode != 0:
        raise SetupError(f"`ip -j {' '.join(args)}` failed: {(result.stderr or result.stdout).strip()}")
    try:
        return json.loads(result.stdout or "[]")
    except json.JSONDecodeError as exc:
        raise SetupError(f"`ip -j {' '.join(args)}` returned unparseable output: {exc}") from exc


def reserved_ipv4_addresses() -> set[str]:
    """Addresses this host already owns, plus every gateway it routes through.

    A gateway that drops ICMP would otherwise survive the ping check below and be
    picked as the victim address, which would break the LAN rather than the test.
    """
    reserved = set()
    for link in query_ip(["addr"]):
        for info in link.get("addr_info", []):
            if info.get("family") == "inet" and info.get("local"):
                reserved.add(info["local"])
    for route in query_ip(["route", "show"]):
        if route.get("gateway"):
            reserved.add(route["gateway"])
    return reserved


def detect_lan_path(device_ip: str) -> tuple[str, str, int]:
    """Return the interface, source address and prefix length used to reach the device."""
    routes = query_ip(["route", "get", device_ip])
    if not routes:
        raise SetupError(f"no route to {device_ip}")
    route = routes[0]
    if route.get("gateway"):
        raise SetupError(
            f"{device_ip} is reached through gateway {route['gateway']}. The vanishing-peer "
            "trick needs the device on the same subnet as this host, because a router would "
            "keep answering for the deleted address."
        )
    iface = route.get("dev")
    source = route.get("prefsrc")
    if not iface or not source:
        raise SetupError(f"route to {device_ip} names no interface and source address: {route}")
    for link in query_ip(["addr", "show", "dev", iface]):
        for info in link.get("addr_info", []):
            if info.get("family") == "inet" and info.get("local") == source:
                return iface, source, int(info["prefixlen"])
    raise SetupError(f"source address {source} is not configured on {iface}")


def answers_ping(address: str) -> bool:
    result = subprocess.run(
        ["ping", "-c", "1", "-W", "1", address], capture_output=True, text=True
    )
    return result.returncode == 0


def pick_victim_ip(source: str, prefix_len: int, device_ip: str) -> str:
    """Highest address in the local subnet that neither this host nor any peer claims."""
    network = ipaddress.ip_network(f"{source}/{prefix_len}", strict=False)
    if network.num_addresses < 8:
        raise SetupError(f"subnet {network} is too small to spare a throwaway address")
    taken = reserved_ipv4_addresses() | {device_ip}
    for candidate in reversed(list(network.hosts())[-VICTIM_SEARCH_DEPTH:]):
        address = str(candidate)
        if address in taken or answers_ping(address):
            continue
        return address
    raise SetupError(
        f"every one of the last {VICTIM_SEARCH_DEPTH} addresses in {network} is in use; "
        "pass a free one with --victim-ip"
    )


def add_ip_alias(iface: str, victim_ip: str, prefix_len: int) -> None:
    run_cmd(IP_CMD + ["addr", "add", f"{victim_ip}/{prefix_len}", "dev", iface])


def del_ip_alias(iface: str, victim_ip: str, prefix_len: int) -> bool:
    """Remove the throwaway victim IP; return True on success. Never raises."""
    result = subprocess.run(
        IP_CMD + ["addr", "del", f"{victim_ip}/{prefix_len}", "dev", iface],
        check=False, capture_output=True, text=True,
    )
    if result.returncode != 0:
        log(f"ip addr del {victim_ip} failed: {(result.stderr or result.stdout).strip()}")
    return result.returncode == 0


def open_half_open_victims(host: str, victim_ip: str, count: int) -> list[socket.socket]:
    victims = []
    for i in range(count):
        s = _connect(host, source_ip=victim_ip)
        try:
            s.settimeout(1.0)
            s.recv(64)  # drain banner so the session is fully live
        except OSError:
            pass
        victims.append(s)
        log(f"  opened half-open victim {i + 1}/{count} from {victim_ip}")
        time.sleep(0.2)
    return victims


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Red/green E2E for the telnet half-open session leak "
                    "(needs sudo on ip for the vanishing-peer alias).")
    parser.add_argument("-H", "--host", default="u64",
                        help="device hostname or IP (default u64)")
    parser.add_argument("--iface", default=None,
                        help="LAN interface for the throwaway victim IP "
                             "(default: the interface that routes to the device)")
    parser.add_argument("--victim-ip", default=None,
                        help="unused LAN IP to source the vanishing connections from "
                             "(default: a free address in this host's subnet)")
    parser.add_argument("--sessions", type=int, default=DEFAULT_MAX_SESSIONS,
                        help="half-open sessions to leak (= TELNET_MAX_SESSIONS)")
    parser.add_argument("--reap-timeout", type=float, default=75.0,
                        help="seconds to wait for keepalive reaping before declaring RED")
    parser.add_argument("--poll-interval", type=float, default=3.0,
                        help="seconds between recovery polls")
    args = parser.parse_args()

    # Preflight 1: adding and deleting the alias needs passwordless sudo for `ip`.
    check = subprocess.run(IP_CMD + ["addr", "show"], capture_output=True, text=True)
    if check.returncode != 0:
        log("ERROR: cannot run `sudo -n ip` - grant passwordless sudo for `ip` or "
            f"run under sudo. ({(check.stderr or check.stdout).strip()})")
        return 2

    # Preflight 2: work out which interface and address the vanishing peer should use.
    try:
        device_ip = socket.gethostbyname(args.host)
    except OSError as exc:
        log(f"ERROR: cannot resolve device host {args.host!r}: {exc}")
        return 2

    prefix_len = FALLBACK_PREFIX_LEN
    try:
        iface, source_ip, prefix_len = detect_lan_path(device_ip)
        if args.iface:
            iface = args.iface
        victim_ip = args.victim_ip or pick_victim_ip(source_ip, prefix_len, device_ip)
    except SetupError as exc:
        if not (args.iface and args.victim_ip):
            log(f"ERROR: {exc}")
            log("Pass both --iface and --victim-ip to skip this detection.")
            return 2
        log(f"WARNING: {exc}")
        log(f"Using --iface {args.iface} and --victim-ip {args.victim_ip} with a "
            f"/{FALLBACK_PREFIX_LEN} prefix.")
        iface, victim_ip = args.iface, args.victim_ip

    log(f"using interface {iface}, victim address {victim_ip}/{prefix_len}")

    reset_machine(args.host)

    victims: list[socket.socket] = []
    alias_added = False
    try:
        log(f"target telnet {args.host}:{TELNET_PORT}, victim source {victim_ip} on {iface}")

        # Table must be fully free at baseline, else another client is using it.
        free = measure_capacity(args.host, args.sessions)
        log(f"baseline free session slots: {free} (expected {args.sessions})")
        if free < args.sessions:
            log("ERROR: listener not fully free at baseline - is another telnet client connected?")
            return 3

        # Fill every slot with a half-open victim from the throwaway IP.
        add_ip_alias(iface, victim_ip, prefix_len)
        alias_added = True
        log(f"added victim IP alias {victim_ip}/{prefix_len} on {iface}")
        victims = open_half_open_victims(args.host, victim_ip, args.sessions)

        time.sleep(1.0)
        if probe_is_free(args.host):
            log("ERROR: listener still free after filling every slot - could not saturate.")
            return 4
        log("listener saturated: fresh connections are refused (as expected).")

        # Required: if this del fails the peers never vanish (fake RED) - abort.
        if not del_ip_alias(iface, victim_ip, prefix_len):
            log("ERROR: could not delete victim IP alias - peers would not truly vanish; aborting.")
            return 5
        alias_added = False
        log(f"deleted victim IP {victim_ip}: {args.sessions} sessions are now half-open.")

        # Require the FULL table back, not one slot (guards a partial reap).
        deadline = time.time() + args.reap_timeout
        start = time.time()
        recovered_at = None
        while time.time() < deadline:
            free = measure_capacity(args.host, args.sessions)
            if free >= args.sessions:
                recovered_at = time.time() - start
                break
            log(f"  recovery poll: {free}/{args.sessions} slots free")
            time.sleep(args.poll_interval)

        if recovered_at is not None:
            log(f"GREEN: all {args.sessions} leaked slots recovered after ~{recovered_at:.0f}s "
                "(keepalive reaped the half-open sessions).")
            return 0

        log(f"RED: listener still wedged {args.reap_timeout:.0f}s after the peers vanished - "
            "half-open sessions were never reaped (fix absent or ineffective).")
        return 1

    finally:
        for s in victims:
            try:
                s.close()
            except OSError:
                pass
        if alias_added:
            del_ip_alias(iface, victim_ip, prefix_len)
            log(f"cleanup: removed victim IP alias {victim_ip}")


if __name__ == "__main__":
    sys.exit(main())
