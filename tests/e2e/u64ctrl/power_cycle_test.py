#!/usr/bin/env python3
"""E2E: what the machine does when its input power comes back.

Supported target: a C64 Ultimate carrying the "Power On After Power Loss"
setting. The suite skips, rather than fails, on firmware that does not serve
the item, so it is safe to name on an older build.

The setting lives in the control module's NVS, because the module is the only
part of the machine powered before the power button is pressed. Asserting it
therefore means actually removing mains from the machine: the whole point is
what happens when the module cold starts, and nothing reachable over the
network can bring that about.

Two of the four scenarios end with the machine deliberately off, and those
cannot be recovered by software. While the machine is off the Ultimate
application is not running, and with it neither the REST API nor the IP stack
it serves -- the control module only bridges Ethernet frames, it does not
listen on anything itself. So a machine that correctly stays off can only be
revived by the power button, and the suite asks the operator to press it. This
is why the suite is registered `manual`, and why adapter support cannot make
it unattended.

Switching the mains is asked of the operator by default, which needs no
hardware beyond whatever socket the tester already owns. Give
--power-off-cmd/--power-on-cmd to drive a socket that can be scripted; the
outcome is still read from the device either way, never from the operator's
say-so. Some command lines that work, for a reader who would rather not
research their own -- untested here beyond the shape, so treat them as
starting points:

    Zigbee2MQTT  mosquitto_pub -t zigbee2mqtt/c64u/set -m '{"state":"OFF"}'
    Tasmota      curl -s -o /dev/null 'http://PLUG/cm?cmnd=Power%20Off'
    Shelly       curl -s -o /dev/null 'http://PLUG/relay/0?turn=off'

Run it by hand, or with --manual, when the power-on behaviour is changed:

    ./run-tests --suite power-cycle --manual c64u
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))

from api import UltimateApi
from report import (Failure, check, check_skip, check_start, detail,
                    format_exception, section, suite_fail, suite_ok)

SUITE = "power_cycle_test"

ITEM = "Power On After Power Loss"
MODE_OFF = "Off"
MODE_ON = "On"
MODE_LAST = "Last State"

# Long enough for the control module to lose power. A brief dip leaves the
# ESP32 running, so nothing cold starts and the machine simply stays as it
# was, which reads exactly like the setting being ignored. Ten seconds was
# enough on the machine this was written against; fifteen is comfortable.
DEFAULT_OFF_SECONDS = 15.0
# A cold start has to load the FPGA before the application answers anything.
DEFAULT_UP_TIMEOUT = 90.0
# How long a machine that is supposed to stay off has to stay silent for the
# check to believe it. Sized above the boot time above, so that "still off"
# cannot just mean "not up yet".
DEFAULT_SILENCE_SECONDS = 30.0
POLL_SECONDS = 2.0


def alive(api: UltimateApi) -> bool:
    """Whether the application answers. Nothing answers while the machine is off."""
    try:
        return bool(api.version())
    except Exception:  # noqa: BLE001  (any transport failure means "not there")
        return False


def wait_for(api: UltimateApi, want_alive: bool, timeout: float) -> bool:
    """Poll until the device reaches `want_alive`, or the timeout runs out.

    Returns whether it got there. Polling rather than sleeping is what lets the
    up cases finish as soon as the machine is back instead of always paying the
    worst case.
    """
    deadline = time.monotonic() + timeout
    while True:
        if alive(api) == want_alive:
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(POLL_SECONDS)


def stays_off(api: UltimateApi, seconds: float) -> bool:
    """Whether the device keeps quiet for the whole window.

    The negative assertion cannot be made by a single probe: a machine that is
    booting is also silent for a while. This one fails the moment it hears
    anything, so it costs the full window only when the answer is the one the
    check wants.
    """
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if alive(api):
            return False
        time.sleep(POLL_SECONDS)
    return True


class Mains:
    """The socket the machine is plugged into, scripted or switched by hand."""

    def __init__(self, off_cmd: str, on_cmd: str, off_seconds: float) -> None:
        self.off_cmd = off_cmd
        self.on_cmd = on_cmd
        self.off_seconds = off_seconds
        self.scripted = bool(off_cmd and on_cmd)
        if bool(off_cmd) != bool(on_cmd):
            raise Failure("--power-off-cmd and --power-on-cmd go together; "
                          "give both or neither")
        if not self.scripted and not sys.stdin.isatty():
            raise Failure(
                "no terminal to prompt on and no socket commands given.\n"
                "Pass --power-off-cmd and --power-on-cmd to run this without "
                "an operator, or run it from a terminal.")

    def _run(self, cmd: str, what: str) -> None:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        if result.returncode != 0:
            raise Failure(f"{what} command failed with {result.returncode}: "
                          f"{(result.stderr or result.stdout).strip()[:200]}")

    def _ask(self, instruction: str) -> None:
        print(f"      >>> {instruction}, then press Enter: ", end="", flush=True)
        sys.stdin.readline()

    def cycle(self) -> None:
        """Remove mains, hold it off long enough to matter, put it back."""
        if self.scripted:
            self._run(self.off_cmd, "power off")
            detail(f"mains off for {self.off_seconds:.0f}s")
            time.sleep(self.off_seconds)
            self._run(self.on_cmd, "power on")
        else:
            self._ask(f"switch the socket OFF and leave it off for "
                      f"{self.off_seconds:.0f}s")
            self._ask("switch the socket back ON")

    def revive(self, api: UltimateApi, up_timeout: float) -> None:
        """Get a deliberately-off machine running again, for what comes next.

        Only the power button can do this, so it is always the operator's job,
        even when the socket itself is scripted. Leaving the machine off would
        fail the next check and then the runner's own teardown, which resets
        the machine after every suite.
        """
        self._ask("press the machine's power button to switch it back on")
        if not wait_for(api, True, up_timeout):
            raise Failure("the machine did not come back after the power button; "
                          "the rest of the suite cannot run")


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


def set_mode(api: UltimateApi, store: str, mode: str) -> None:
    """Set the mode and read it back, so a silent refusal is not mistaken for a pass."""
    api.configs.set(store, ITEM, mode)
    current = api.configs.current(store, ITEM)
    if current != mode:
        raise Failure(f"setting the mode to {mode!r} left it at {current!r}")
    detail(f"mode is {mode!r}")


def switch_machine_off(api: UltimateApi, up_timeout: float) -> None:
    """Put the machine in the off state the next scenario needs it to be in."""
    api.machine.poweroff()
    if not wait_for(api, False, up_timeout):
        raise Failure("the machine still answered after machine:poweroff")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "c64u"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "5.0")))
    parser.add_argument("--power-off-cmd", default="",
                        help="Shell command that removes mains from the machine. "
                             "Without it the operator is asked to switch the socket.")
    parser.add_argument("--power-on-cmd", default="",
                        help="Shell command that puts mains back.")
    parser.add_argument("--off-seconds", type=float, default=DEFAULT_OFF_SECONDS,
                        help=f"How long mains stays off (default: {DEFAULT_OFF_SECONDS:.0f}). "
                             "Below about ten the control module keeps running "
                             "and nothing cold starts.")
    parser.add_argument("--up-timeout", type=float, default=DEFAULT_UP_TIMEOUT,
                        help=f"How long a cold start may take (default: {DEFAULT_UP_TIMEOUT:.0f}).")
    parser.add_argument("--silence-seconds", type=float, default=DEFAULT_SILENCE_SECONDS,
                        help="How long a machine that should stay off must stay "
                             f"silent (default: {DEFAULT_SILENCE_SECONDS:.0f}).")
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    original = ""
    store = ""
    try:
        mains = Mains(args.power_off_cmd, args.power_on_cmd, args.off_seconds)

        section("1. Preconditions")
        check_start("the machine answers")
        if not alive(api):
            check_skip(f"nothing answers on {args.host}; switch the machine on first")
            suite_ok(SUITE)
            return 0
        detail(f"firmware {api.version()}")
        store = find_store(api)
        if not store:
            check_skip(f"no config store serves {ITEM!r}; "
                       "this firmware predates the setting")
            suite_ok(SUITE)
            return 0
        original = api.configs.current(store, ITEM)
        detail(f"store {store!r}, currently {original!r}")

        # Every check below waits out a real mains interruption, so all of them
        # are past the ten second mark that report.py paints yellow. That time
        # is the behaviour under test and cannot be polled away.
        section("2. Mode On, machine switched off before the cut")
        with check("comes up by itself"):
            set_mode(api, store, MODE_ON)
            switch_machine_off(api, args.up_timeout)
            mains.cycle()
            if not wait_for(api, True, args.up_timeout):
                raise Failure(f"stayed off with the mode at {MODE_ON!r}")

        section("3. Mode Last State, machine on before the cut")
        with check("comes up by itself"):
            set_mode(api, store, MODE_LAST)
            mains.cycle()
            if not wait_for(api, True, args.up_timeout):
                raise Failure("stayed off although it was on when power was lost")

        # The two negative scenarios go last, because each one ends with a
        # machine only the operator can revive.
        section("4. Mode Last State, machine switched off before the cut")
        with check("stays off"):
            set_mode(api, store, MODE_LAST)
            switch_machine_off(api, args.up_timeout)
            mains.cycle()
            if not stays_off(api, args.silence_seconds):
                raise Failure("came up although it was off when power was lost")
        mains.revive(api, args.up_timeout)

        section("5. Mode Off, machine on before the cut")
        with check("stays off"):
            set_mode(api, store, MODE_OFF)
            mains.cycle()
            if not stays_off(api, args.silence_seconds):
                raise Failure(f"came up with the mode at {MODE_OFF!r}")
        mains.revive(api, args.up_timeout)

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
        # it. A machine left off cannot be written to, and saying so is better
        # than a traceback out of the cleanup path.
        if store and original:
            try:
                api.configs.set(store, ITEM, original)
            except Exception:  # noqa: BLE001
                detail(f"could not restore {ITEM!r} to {original!r}")


if __name__ == "__main__":
    raise SystemExit(main())
