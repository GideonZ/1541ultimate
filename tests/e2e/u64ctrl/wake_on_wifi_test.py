#!/usr/bin/env python3
"""E2E: waking the machine from off with a magic packet.

Supported target: a C64 Ultimate (or Ultimate 64 Elite II) carrying the "Wake
On Wi-Fi" setting. The suite skips, rather than fails, on firmware that does not
serve the item, so it is safe to name on an older build.

Two conditions of the setup this cannot check for itself, and both make every
check below fail if they do not hold:

- The device is on Wi-Fi. Over the wired jack the control module never sees the
  packet: the RMII PHY sits in the FPGA power domain, which is down while the
  machine is off, and the module has no wired path of its own. That is what the
  setting's help text in the menu says, and it is not a limitation this suite
  can work around.
- The harness is in the device's broadcast domain. A magic packet is sent to
  the broadcast address because a machine that is off answers no ARP for the
  application's address, and a limited broadcast does not cross a router. Give
  --broadcast to aim at a subnet's own broadcast address instead.

Unlike the power cycle suite this needs no mains interruption: the machine is
put into the off state over REST, which leaves the control module powered and
associated, which is exactly the state the feature is for. Only the last
scenario, in which the packet must be ignored, ends with a machine that nothing
on the network can revive -- hence `manual`, and hence --power-button-cmd if
the run is to be unattended.

    ./run-tests --suite wake-on-wifi --manual c64u
"""

import argparse
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from api import UltimateApi
from machine_power import (DEFAULT_SILENCE_SECONDS, DEFAULT_UP_TIMEOUT,
                           PowerButton, alive, stays_off, switch_machine_off,
                           wait_for_state)
from report import (Failure, check, check_ok, check_skip, check_start, detail,
                    format_exception, section, suite_fail, suite_ok)

SUITE = "wake_on_wifi_test"

ITEM = "Wake On Wi-Fi"
ENABLED = "Enabled"
DISABLED = "Disabled"

# Where a magic packet is sent. Port 9 (discard) is what wake tools use by
# default; the firmware matches on the pattern rather than on the port, so 7 or
# a raw frame would do as well.
DEFAULT_BROADCAST = "255.255.255.255"
DEFAULT_PORT = 9
# Wake tools send the packet several times over, on the grounds that a single
# lost datagram is not worth a failed wake. The firmware disarms on the first
# match, so the copies cost nothing.
COPIES = 3
COPY_PAUSE = 0.2


def magic_packet(mac: bytes) -> bytes:
    """The 102 bytes every wake tool sends: six 0xFF, then the MAC sixteen times."""
    return (b"\xff" * 6) + (mac * 16)


def send_magic(mac: bytes, broadcast: str, port: int) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        payload = magic_packet(mac)
        for _ in range(COPIES):
            sock.sendto(payload, (broadcast, port))
            time.sleep(COPY_PAUSE)
    detail(f"{COPIES} magic packets for {format_mac(mac)} to {broadcast}:{port}")


def format_mac(mac: bytes) -> str:
    return ":".join(f"{octet:02x}" for octet in mac)


def parse_mac(text: str) -> bytes:
    """A MAC from a command line or from an ARP table.

    macOS prints its ARP entries with the leading zero of an octet stripped
    ("24:6f:28:1:22:33"), so each octet is padded rather than taken as written.
    """
    parts = re.split(r"[:-]", text.strip())
    if len(parts) != 6:
        raise Failure(f"not a MAC address: {text!r}")
    try:
        return bytes(int(part, 16) for part in parts)
    except ValueError:
        raise Failure(f"not a MAC address: {text!r}") from None


def discover_mac(host: str) -> bytes:
    """The device's MAC, from the host's own ARP table.

    Asked of the operating system rather than of the device, which serves it
    nowhere, and read while the machine is still up so the entry is fresh: the
    REST calls of the preconditions have just been through it.
    """
    try:
        address = socket.gethostbyname(host)
    except OSError as exc:
        raise Failure(f"cannot resolve {host!r}: {exc}") from None
    result = subprocess.run(["arp", "-n", address],
                            capture_output=True, text=True)
    found = re.search(r"\b([0-9a-f]{1,2}(?::[0-9a-f]{1,2}){5})\b",
                      result.stdout, re.IGNORECASE)
    if not found:
        raise Failure(
            f"no MAC for {address} in the ARP table; pass --mac. "
            f"arp said: {(result.stdout or result.stderr).strip()[:200]!r}")
    return parse_mac(found.group(1))


def other_mac(mac: bytes) -> bytes:
    """A MAC that is not the device's, for the packet that must be ignored.

    The last octet is moved, so the address stays inside the same block and
    could plausibly be a neighbour of the device rather than something a
    matcher might reject for its own reasons.
    """
    return mac[:5] + bytes([mac[5] ^ 0x01])


def find_store(api: UltimateApi) -> str:
    """The config store serving the setting, or "" when this firmware has none.

    The store is looked up rather than named because it answers to "U64
    Specific Settings" on an Ultimate 64 and to "C64U Specific Settings" on a
    Commodore machine, and because its absence is exactly how firmware without
    the feature presents itself.
    """
    names = api.configs.categories().get("categories")
    if not isinstance(names, list):
        raise Failure(f"configs: no category list in the answer: {names!r}")
    for category in names:
        if isinstance(category, str) and ITEM in api.configs.category(category):
            return category
    return ""


def set_item(api: UltimateApi, store: str, value: str) -> None:
    """Set the setting and read it back, so a silent refusal is not a pass."""
    api.configs.set(store, ITEM, value)
    current = api.configs.current(store, ITEM)
    if current != value:
        raise Failure(f"setting {ITEM!r} to {value!r} left it at {current!r}")
    detail(f"{ITEM} is {value!r}")


def wake_hint(args: argparse.Namespace) -> str:
    """What to look at when a wake that should have happened did not."""
    return (f"no wake: check that the device is associated over Wi-Fi rather "
            f"than plugged into the wired jack, and that this harness shares "
            f"its broadcast domain (sent to {args.broadcast}:{args.port})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "c64u"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "5.0")))
    parser.add_argument("--mac", default="",
                        help="The device's MAC address. Discovered from the "
                             "host's ARP table when not given.")
    parser.add_argument("--broadcast", default=DEFAULT_BROADCAST,
                        help=f"Where to send the packet (default: {DEFAULT_BROADCAST}). "
                             "A subnet's own broadcast address, such as "
                             "192.168.1.255, for a harness with more than one "
                             "interface.")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"UDP port to send to (default: {DEFAULT_PORT}).")
    parser.add_argument("--power-button-cmd", default="",
                        help="Shell command that presses the machine's power "
                             "button. Without it the operator is asked, which "
                             "needs a terminal: the last scenario ends with the "
                             "machine off and no packet may revive it.")
    parser.add_argument("--up-timeout", type=float, default=DEFAULT_UP_TIMEOUT,
                        help=f"How long a wake may take (default: {DEFAULT_UP_TIMEOUT:.0f}).")
    parser.add_argument("--silence-seconds", type=float, default=DEFAULT_SILENCE_SECONDS,
                        help="How long a machine that should stay off must stay "
                             f"silent (default: {DEFAULT_SILENCE_SECONDS:.0f}).")
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    original = ""
    store = ""
    try:
        section("1. Preconditions")
        check_start("the machine answers")
        if not alive(api):
            check_skip(f"nothing answers on {args.host}; switch the machine on first")
            suite_ok(SUITE)
            return 0
        # api.version() is the REST API's version, not the machine's; the
        # firmware and the product come from /v1/info.
        info = api.info()
        detail(f"{info.product}, firmware {info.firmware_version} "
               f"(REST API {api.version()})")
        store = find_store(api)
        if not store:
            check_skip(f"no config store serves {ITEM!r}; "
                       "this firmware predates the setting")
            suite_ok(SUITE)
            return 0
        original = api.configs.current(store, ITEM)
        detail(f"store {store!r}, currently {original!r}")
        mac = parse_mac(args.mac) if args.mac else discover_mac(args.host)
        detail(f"device MAC {format_mac(mac)}"
               f"{'' if args.mac else ' (from the ARP table)'}")
        # Closes the check the two skips above would have closed. An open check
        # makes report.py treat every check that follows as nested inside it,
        # which prints no verdict and counts nothing.
        check_ok()
        # After the skips and before anything that switches the machine off: a
        # run that cannot be completed says so now, and a run that was going to
        # skip is not asked for a power button it never needs.
        button = PowerButton(args.power_button_cmd)

        # The negative case first: it ends with the machine off, and the
        # positive case that follows is what brings it back, so the operator is
        # left out of it entirely.
        section("2. Enabled, a magic packet for another station")
        with check("is ignored"):
            set_item(api, store, ENABLED)
            switch_machine_off(api, args.up_timeout)
            send_magic(other_mac(mac), args.broadcast, args.port)
            if not stays_off(api, args.silence_seconds):
                raise Failure("came up on a packet addressed to another station")

        section("3. Enabled, a magic packet for this station")
        with check("wakes the machine"):
            send_magic(mac, args.broadcast, args.port)
            if not wait_for_state(api, True, args.up_timeout):
                raise Failure(wake_hint(args))

        # Last, because nothing on the network can revive what it leaves off.
        section("4. Disabled")
        with check("a magic packet is ignored"):
            set_item(api, store, DISABLED)
            switch_machine_off(api, args.up_timeout)
            send_magic(mac, args.broadcast, args.port)
            if not stays_off(api, args.silence_seconds):
                raise Failure(f"came up with {ITEM!r} at {DISABLED!r}")
        button.press(api, args.up_timeout)

        suite_ok(SUITE)
        return 0
    except Failure as exc:
        suite_fail(SUITE, str(exc))
        return 1
    except Exception as exc:  # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        # Put the setting back if it was read and the machine is there to take
        # it. A machine left off by a failed scenario cannot be written to, and
        # saying so is better than a traceback out of the cleanup path.
        if store and original:
            try:
                api.configs.set(store, ITEM, original)
            except Exception:  # noqa: BLE001
                detail(f"could not restore {ITEM!r} to {original!r}")


if __name__ == "__main__":
    raise SystemExit(main())
