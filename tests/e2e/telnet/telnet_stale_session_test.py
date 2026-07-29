#!/usr/bin/env python3
"""Red/green E2E for the telnet half-open session leak.

A telnet peer that vanishes at the network level (WiFi drop, powered-off phone,
AP roam) sends no FIN/RST and never ACKs the device's keepalive probes. Without
the fix its session slot leaks and the 4-slot listener wedges; with the fix TCP
keepalive reaps the dead session (~35s) and capacity recovers.

The vanished peer is faked by opening the victims from a throwaway IP alias,
then deleting it so the host stops answering ARP/keepalive. Managing the alias
needs root, so the script elevates only `ip addr add/del` via `sudo -n` (grant
passwordless sudo for `ip`, or run under sudo).

Exit 0 = GREEN (all slots reaped, full capacity recovers); non-zero = RED / setup error.
"""

import argparse
import socket
import subprocess
import sys
import time

TELNET_PORT = 23
DEFAULT_MAX_SESSIONS = 4  # mirrors TELNET_MAX_SESSIONS in software/network/socket_gui.cc
BUSY_MARKER = b"Too many connections"
FREE_CONFIRM_BYTES = 64  # larger than any busy reply; excess with no marker = banner
IP_CMD = ["sudo", "-n", "ip"]  # only ip needs root


def log(msg: str) -> None:
    print(f"[telnet-stale-e2e] {msg}", flush=True)


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


def add_ip_alias(iface: str, victim_ip: str) -> None:
    run_cmd(IP_CMD + ["addr", "add", f"{victim_ip}/24", "dev", iface])


def del_ip_alias(iface: str, victim_ip: str) -> bool:
    """Remove the throwaway victim IP; return True on success. Never raises."""
    result = subprocess.run(
        IP_CMD + ["addr", "del", f"{victim_ip}/24", "dev", iface],
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
    parser.add_argument("--iface", default="eth0",
                        help="LAN interface for the throwaway victim IP")
    parser.add_argument("--victim-ip", default="192.168.1.240",
                        help="unused LAN IP to source the vanishing connections from")
    parser.add_argument("--sessions", type=int, default=DEFAULT_MAX_SESSIONS,
                        help="half-open sessions to leak (= TELNET_MAX_SESSIONS)")
    parser.add_argument("--reap-timeout", type=float, default=75.0,
                        help="seconds to wait for keepalive reaping before declaring RED")
    parser.add_argument("--poll-interval", type=float, default=3.0,
                        help="seconds between recovery polls")
    args = parser.parse_args()

    # Preflight: can we manage the alias? (needs sudo on ip)
    check = subprocess.run(IP_CMD + ["addr", "show", "dev", args.iface],
                           capture_output=True, text=True)
    if check.returncode != 0:
        log("ERROR: cannot run `sudo -n ip` - grant passwordless sudo for `ip` or "
            f"run under sudo. ({(check.stderr or check.stdout).strip()})")
        return 2

    victims: list[socket.socket] = []
    alias_added = False
    try:
        log(f"target telnet {args.host}:{TELNET_PORT}, victim source {args.victim_ip} on {args.iface}")

        # Table must be fully free at baseline, else another client is using it.
        free = measure_capacity(args.host, args.sessions)
        log(f"baseline free session slots: {free} (expected {args.sessions})")
        if free < args.sessions:
            log("ERROR: listener not fully free at baseline - is another telnet client connected?")
            return 3

        # Fill every slot with a half-open victim from the throwaway IP.
        add_ip_alias(args.iface, args.victim_ip)
        alias_added = True
        log(f"added victim IP alias {args.victim_ip}/24 on {args.iface}")
        victims = open_half_open_victims(args.host, args.victim_ip, args.sessions)

        time.sleep(1.0)
        if probe_is_free(args.host):
            log("ERROR: listener still free after filling every slot - could not saturate.")
            return 4
        log("listener saturated: fresh connections are refused (as expected).")

        # Required: if this del fails the peers never vanish (fake RED) - abort.
        if not del_ip_alias(args.iface, args.victim_ip):
            log("ERROR: could not delete victim IP alias - peers would not truly vanish; aborting.")
            return 5
        alias_added = False
        log(f"deleted victim IP {args.victim_ip}: {args.sessions} sessions are now half-open.")

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
            del_ip_alias(args.iface, args.victim_ip)
            log(f"cleanup: removed victim IP alias {args.victim_ip}")


if __name__ == "__main__":
    sys.exit(main())
