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
module powered and associated. Scenarios 2 and 3 leave the setting Enabled, so
the suite wakes the machine itself with a packet to the Wi-Fi module's MAC:
`wifi_mac` from GET /v1/info, or the harness's neighbour table on firmware
that does not report it. Scenario 2 watches for --silence-seconds; scenario 3
times the wake, and the run fails if that window was shorter than twice it.
Scenario 5 leaves the machine off with the setting Disabled, where no packet
can wake it, so it needs --power-button-cmd. Scenario 4, the cold start, needs
the socket commands. Both are skipped without them.

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
                           DEFAULT_UP_TIMEOUT, WAKE_BROADCAST, WAKE_PORT,
                           Mains, PowerButton, WakePacketButton, alive,
                           format_mac, recover_if_off, send_magic,
                           silence_window, stays_off, switch_machine_off,
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


def parse_mac(text: str) -> bytes:
    """A MAC; a short octet ("24:6f:28:1:22:33") is padded, not refused."""
    parts = re.split(r"[:-]", text.strip())
    if len(parts) != 6:
        raise Failure(f"not a MAC address: {text!r}")
    try:
        return bytes(int(part, 16) for part in parts)
    except ValueError:
        raise Failure(f"not a MAC address: {text!r}") from None


def wifi_mac(info) -> bytes | None:
    """The Wi-Fi module's MAC as GET /v1/info reports it, or None."""
    reported = info.extra.get("wifi_mac")
    if not isinstance(reported, str) or not reported.strip():
        return None
    return parse_mac(reported)


def discover_mac(host: str) -> bytes | None:
    """The device's MAC from the harness's neighbour table, or None.

    That entry is the Wi-Fi module's whenever the harness reaches the device
    over Wi-Fi, which is the only configuration this suite can pass in.
    """
    try:
        address = socket.gethostbyname(host)
    except OSError:
        return None
    for argv in (["ip", "neigh", "show", address], ["arp", "-n", address]):
        try:
            result = subprocess.run(argv, capture_output=True, text=True, check=False)
        except OSError:
            continue
        found = re.search(r"\b([0-9a-f]{1,2}(?::[0-9a-f]{1,2}){5})\b",
                          result.stdout, re.IGNORECASE)
        if found:
            return parse_mac(found.group(1))
    return None


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
                        help="The Wi-Fi module's MAC address. Taken from the "
                             "wifi_mac GET /v1/info reports, or from the "
                             "neighbour table, when not given.")
    parser.add_argument("--broadcast",
                        default=os.environ.get("U64_WOL_BROADCAST", WAKE_BROADCAST),
                        help=f"Where to send the packet (default: {WAKE_BROADCAST}). "
                             "A subnet's own broadcast address, such as "
                             "192.168.1.255, for a harness with more than one "
                             "interface.")
    parser.add_argument("--port", type=int,
                        default=int(os.environ.get("U64_WOL_PORT", WAKE_PORT)),
                        help=f"UDP port to send to (default: {WAKE_PORT}).")
    parser.add_argument("--power-button-cmd",
                        default=os.environ.get("U64_POWER_BUTTON_CMD", ""),
                        help="Shell command that presses the machine's power "
                             "button. The last scenario ends with the machine "
                             "off and the setting Disabled, where no packet can "
                             "revive it, so that scenario runs only with this "
                             "given and is skipped otherwise.")
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
    parser.add_argument("--silence-seconds", type=float, default=None,
                        help="How long a machine that should stay off must stay "
                             f"silent (default: {DEFAULT_SILENCE_SECONDS:.0f}). "
                             "The run checks this against the wake it measures "
                             "and says so if it was too short.")
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
        # The packet must carry the Wi-Fi module's MAC; the module is what
        # stays powered while the machine is off.
        if args.mac:
            mac, source = parse_mac(args.mac), "given on the command line"
        elif (mac := wifi_mac(info)) is not None:
            source = "the wifi_mac /v1/info reports"
        elif (mac := discover_mac(args.host)) is not None:
            source = "the neighbour table"
        else:
            check_skip("GET /v1/info does not report wifi_mac and the "
                       "neighbour table has no entry for the device; pass "
                       "--mac with the Wi-Fi module's MAC")
            suite_ok(SUITE)
            return 0
        detail(f"device MAC {format_mac(mac)} ({source})")
        # Closes the check the skips above would have closed; report.py
        # nests every later check inside an open one and counts nothing.
        check_ok()
        # Only scenario 5 leaves the machine where no packet can wake it.
        button = (PowerButton(args.power_button_cmd) if args.power_button_cmd
                  else WakePacketButton(mac, args.broadcast, args.port))
        # Only when the socket can be scripted; the cold start scenario is
        # skipped otherwise.
        mains = None
        if args.power_off_cmd or args.power_on_cmd:
            mains = Mains(args.power_off_cmd, args.power_on_cmd, args.off_seconds)

        # One off-and-on cycle covers both packets: the wrong one is sent
        # before the right one. machine:poweroff goes quiet in 1.3s on a C64
        # Ultimate 1.2RC, and the wake takes 8.8s.
        section("2. Enabled, a magic packet for another station")
        window = args.silence_seconds or DEFAULT_SILENCE_SECONDS
        with check("is ignored"):
            set_item(api, store, ENABLED)
            switch_machine_off(api, args.up_timeout)
            send_magic(other_mac(mac), args.broadcast, args.port)
            detail(f"watching for {window:.0f}s")
            if not stays_off(api, window):
                raise Failure("came up on a packet addressed to another station")

        section("3. Enabled, a magic packet for this station")
        measured_wake = None
        with check("wakes the machine"):
            started = time.monotonic()
            send_magic(mac, args.broadcast, args.port)
            if not wait_for_state(api, True, args.up_timeout):
                raise Failure(wake_hint(args))
            measured_wake = time.monotonic() - started
            detail(f"answered {measured_wake:.1f}s after the packet")

        # Scenario 2's silence proves nothing unless its window outlasts a wake.
        with check("the ignored-packet window outlasts a wake"):
            needed = silence_window(measured_wake)
            if window < needed:
                raise Failure(
                    f"the previous check watched for {window:.0f}s, and this "
                    f"machine wakes in {measured_wake:.1f}s, so a wake that "
                    f"did happen could have finished unseen. Watch for at "
                    f"least {needed:.0f}s: --silence-seconds {needed:.0f}")
            detail(f"watched {window:.0f}s for a machine that wakes in "
                   f"{measured_wake:.1f}s")

        justified = silence_window(measured_wake, window)
        if justified > window:
            detail(f"watching for {justified:.0f}s from here on, twice the "
                   f"{measured_wake:.1f}s wake, rather than {window:.0f}s")
            window = justified

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
                # A booting machine is silent too, so the whole window is
                # waited out. A cold start is the module's boot plus a wake.
                if not stays_off(api, window):
                    raise Failure(f"came up by itself with {MODE_ITEM!r} at "
                                  f"{MODE_OFF!r}, so the wake proves nothing")
                send_magic(mac, args.broadcast, args.port)
                if not wait_for_state(api, True, args.up_timeout):
                    raise Failure(wake_hint(args))

        # Last: it leaves the watcher disarmed, so only a power button revives
        # the machine.
        section("5. Disabled")
        if not isinstance(button, PowerButton):
            check_start("a magic packet is ignored")
            check_skip("this scenario switches the machine off with the "
                       "setting Disabled, after which nothing on the network "
                       "can wake it; it needs --power-button-cmd")
        else:
            with check("a magic packet is ignored"):
                set_item(api, store, DISABLED)
                switch_machine_off(api, args.up_timeout)
                send_magic(mac, args.broadcast, args.port)
                if not stays_off(api, window):
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
