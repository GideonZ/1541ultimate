#!/usr/bin/env python3
"""E2E: waking the machine from off with a magic packet.

Supported target: a C64 Ultimate or Ultimate 64 Elite II carrying the "Wake On
Wi-Fi" setting. Firmware that does not serve the item skips rather than fails.

Three conditions this cannot check for itself, each of which fails every check
below:

- The control module is 1.14 or newer. The REST store serves the item whether
  or not the module can store it, so an older module takes the value and
  ignores it. Check the module version before suspecting the firmware.
- The device is on Wi-Fi. The wired PHY is powered down with the machine, so
  the module never sees the packet.
- The harness is in the device's broadcast domain. A machine that is off
  answers no ARP, so the packet goes to the broadcast address, which does not
  cross a router. Give --broadcast to aim at a subnet's own instead.

Scenarios 2, 3 and 5 put the machine off over REST, which leaves the control
module powered and associated. Scenario 5 asserts that a packet is ignored, so
it ends with a machine only its power button can revive: hence `manual`, and
hence --power-button-cmd. Scenario 4, the cold start, covers the watcher armed
from the module's own start rather than from a power transition; it needs
--power-off-cmd and --power-on-cmd and is skipped without them.

Every option can also come from the environment: U64_WOL_MAC, U64_WOL_BROADCAST,
U64_WOL_PORT, U64_POWER_BUTTON_CMD, U64_POWER_OFF_CMD and U64_POWER_ON_CMD.

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

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
sys.path.insert(0, bootstrap.directory("e2e", "u64ctrl"))

from api import UltimateApi
from machine_power import (DEFAULT_OFF_SECONDS, DEFAULT_SILENCE_SECONDS,
                           DEFAULT_UP_TIMEOUT, Mains, PowerButton, alive,
                           recover_if_off, stays_off, switch_machine_off,
                           wait_for_state)
from report import (Failure, check, check_ok, check_skip, check_start, detail,
                    format_exception, section, suite_fail, suite_ok)

SUITE = "wake_on_wifi_test"

ITEM = "Wake On Wi-Fi"
ENABLED = "Enabled"
DISABLED = "Disabled"

# Needed by the cold start scenario: a machine may only be woken from off, so
# the input power has to come back without switching it on.
MODE_ITEM = "Power On After Power Loss"
MODE_OFF = "Off"

# Port 9 is what wake tools use by default. The firmware matches on the pattern
# rather than on the port, so 7 or a raw frame would do as well.
DEFAULT_BROADCAST = "255.255.255.255"
DEFAULT_PORT = 9
# Wake tools repeat the packet, since a lost datagram is not worth a failed
# wake. The firmware disarms on the first match, so the copies cost nothing.
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
    """A MAC from a command line or from an ARP table. macOS strips the leading
    zero of an octet ("24:6f:28:1:22:33"), so each octet is padded."""
    parts = re.split(r"[:-]", text.strip())
    if len(parts) != 6:
        raise Failure(f"not a MAC address: {text!r}")
    try:
        return bytes(int(part, 16) for part in parts)
    except ValueError:
        raise Failure(f"not a MAC address: {text!r}") from None


def discover_mac(host: str) -> bytes:
    """The device's MAC, from the host's own neighbour table.

    The device serves it nowhere, so the operating system is asked instead,
    while the machine is still up and the entry is fresh. `ip neigh` first,
    `arp` second: net-tools is no longer installed by default.
    """
    try:
        address = socket.gethostbyname(host)
    except OSError as exc:
        raise Failure(f"cannot resolve {host!r}: {exc}") from None
    tried = []
    for argv in (["ip", "neigh", "show", address], ["arp", "-n", address]):
        try:
            result = subprocess.run(argv, capture_output=True, text=True, check=False)
        except OSError as exc:
            tried.append(f"{argv[0]}: {exc.strerror or exc}")
            continue
        found = re.search(r"\b([0-9a-f]{1,2}(?::[0-9a-f]{1,2}){5})\b",
                          result.stdout, re.IGNORECASE)
        if found:
            return parse_mac(found.group(1))
        tried.append(f"{argv[0]}: "
                     f"{(result.stdout or result.stderr).strip()[:120]!r}")
    raise Failure(f"no MAC for {address} in the neighbour table; pass --mac. "
                  + "; ".join(tried))


def other_mac(mac: bytes) -> bytes:
    """A MAC that is not the device's, for the packet that must be ignored. The
    last octet is moved, so it stays a plausible neighbour address."""
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


def set_named_item(api: UltimateApi, store: str, item: str, value: str) -> None:
    """Set a setting and read back what the application now holds.

    This proves the application took the value, not that the control module
    stored it: nothing over REST reports what the module ended up with. Only
    what the machine does with a packet shows that.
    """
    api.configs.set(store, item, value)
    current = api.configs.current(store, item)
    if current != value:
        raise Failure(f"setting {item!r} to {value!r} left it at {current!r}")
    detail(f"{item} is {value!r}")


def set_item(api: UltimateApi, store: str, value: str) -> None:
    """Set the wake setting itself."""
    set_named_item(api, store, ITEM, value)


def wake_hint(args: argparse.Namespace) -> str:
    """What to look at when a wake that should have happened did not. The module
    comes first, being the one suspect this suite cannot see for itself."""
    return (f"no wake: check that the control module is 1.14 or newer (the "
            f"application prints what it found at boot, and greys the item out "
            f"in the menu when the module cannot store it), that the device is "
            f"associated over Wi-Fi rather than plugged into the wired jack, "
            f"and that this harness shares its broadcast domain (sent to "
            f"{args.broadcast}:{args.port})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "c64u"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "5.0")))
    parser.add_argument("--mac", default=os.environ.get("U64_WOL_MAC", ""),
                        help="The device's MAC address. Discovered from the "
                             "host's ARP table when not given.")
    parser.add_argument("--broadcast",
                        default=os.environ.get("U64_WOL_BROADCAST", DEFAULT_BROADCAST),
                        help=f"Where to send the packet (default: {DEFAULT_BROADCAST}). "
                             "A subnet's own broadcast address, such as "
                             "192.168.1.255, for a harness with more than one "
                             "interface.")
    parser.add_argument("--port", type=int,
                        default=int(os.environ.get("U64_WOL_PORT", DEFAULT_PORT)),
                        help=f"UDP port to send to (default: {DEFAULT_PORT}).")
    parser.add_argument("--power-button-cmd",
                        default=os.environ.get("U64_POWER_BUTTON_CMD", ""),
                        help="Shell command that presses the machine's power "
                             "button. Without it the operator is asked, which "
                             "needs a terminal: the last scenario ends with the "
                             "machine off and no packet may revive it.")
    parser.add_argument("--power-off-cmd", default=os.environ.get("U64_POWER_OFF_CMD", ""),
                        help="Shell command that removes mains from the machine. "
                             "With it and --power-on-cmd the cold start scenario "
                             "runs; without them it is skipped, because cutting "
                             "mains by hand is not something to ask of an "
                             "operator who did not offer.")
    parser.add_argument("--power-on-cmd", default=os.environ.get("U64_POWER_ON_CMD", ""),
                        help="Shell command that puts mains back.")
    parser.add_argument("--off-seconds", type=float, default=DEFAULT_OFF_SECONDS,
                        help=f"How long mains stays off (default: {DEFAULT_OFF_SECONDS:.0f}). "
                             "Below about ten the control module keeps running "
                             "and nothing cold starts.")
    parser.add_argument("--up-timeout", type=float, default=DEFAULT_UP_TIMEOUT,
                        help=f"How long a wake may take (default: {DEFAULT_UP_TIMEOUT:.0f}).")
    parser.add_argument("--silence-seconds", type=float, default=DEFAULT_SILENCE_SECONDS,
                        help="How long a machine that should stay off must stay "
                             f"silent (default: {DEFAULT_SILENCE_SECONDS:.0f}).")
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    original = ""
    original_mode = ""
    store = ""
    button = None
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
        # Closes the check the two skips above would have closed; report.py
        # nests every later check inside an open one and counts nothing.
        check_ok()
        # After the skips and before anything switches the machine off, so a run
        # that was going to skip is never asked for a power button.
        button = PowerButton(args.power_button_cmd)
        # Only when the socket can be scripted; the cold start scenario is
        # skipped otherwise.
        mains = None
        if args.power_off_cmd or args.power_on_cmd:
            mains = Mains(args.power_off_cmd, args.power_on_cmd, args.off_seconds)

        # The negative case first: the positive case that follows is what
        # brings the machine back, so no operator is needed in between.
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

        # The path no other check reaches: the watcher armed from the button
        # handler's own start rather than from an ON or OFF event, which is what
        # a user meets after a real power loss with the default mode.
        section("4. Enabled, a machine that cold starts into the off state")
        if mains is None:
            check_start("wakes after the input power returned")
            check_skip("no socket commands; pass --power-off-cmd and --power-on-cmd")
        else:
            with check("wakes after the input power returned"):
                # Set again: the machine rebooted in between, and a boot
                # pushes the value held in flash down to the module.
                set_item(api, store, ENABLED)
                original_mode = api.configs.current(store, MODE_ITEM)
                set_named_item(api, store, MODE_ITEM, MODE_OFF)
                switch_machine_off(api, args.up_timeout)
                mains.cycle()
                # A booting machine is silent too, so this waits out the same
                # budget a boot is given rather than sampling.
                if not stays_off(api, args.silence_seconds):
                    raise Failure(f"came up by itself with {MODE_ITEM!r} at "
                                  f"{MODE_OFF!r}, so the wake proves nothing")
                send_magic(mac, args.broadcast, args.port)
                if not wait_for_state(api, True, args.up_timeout):
                    raise Failure(wake_hint(args))

        # Last, because nothing on the network can revive what it leaves off.
        section("5. Disabled")
        with check("a magic packet is ignored"):
            set_item(api, store, DISABLED)
            switch_machine_off(api, args.up_timeout)
            send_magic(mac, args.broadcast, args.port)
            if not stays_off(api, args.silence_seconds):
                raise Failure(f"came up with {ITEM!r} at {DISABLED!r}")
        button.press(api, args.up_timeout)

        suite_ok(SUITE)
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        # A failure after switching the machine off skipped its press, and the
        # setting restore below needs a machine that answers.
        recover_if_off(button, api, args.up_timeout)
        # Put the settings back if they were read and the machine is there to
        # take them. A machine still off cannot be written to, and saying so is
        # better than a traceback out of the cleanup path.
        for item, value in ((ITEM, original), (MODE_ITEM, original_mode)):
            if not (store and value):
                continue
            try:
                api.configs.set(store, item, value)
            except Exception:  # noqa: BLE001
                detail(f"could not restore {item!r} to {value!r}")


if __name__ == "__main__":
    raise SystemExit(main())
