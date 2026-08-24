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
revived by its power button. This is why the suite is registered `manual`.

Two separate pairs of hands are therefore needed, and a scripted socket
supplies only one of them:

    the mains   --power-off-cmd / --power-on-cmd, or the operator
    the button  --power-button-cmd, or the operator

Give all three and the suite runs unattended; give the socket commands alone
and it still stops at the button, so it still needs a terminal to ask on. The
outcome is read from the device in every case, never from the operator's
say-so. Some command lines that work for the socket, for a reader who would
rather not research their own -- untested here beyond the shape, so treat them
as starting points:

    Zigbee2MQTT  mosquitto_pub -t zigbee2mqtt/c64u/set -m '{"state":"OFF"}'
    Tasmota      curl -s -o /dev/null 'http://PLUG/cm?cmnd=Power%20Off'
    Shelly       curl -s -o /dev/null 'http://PLUG/relay/0?turn=off'

Run it by hand, or with --manual, when the power-on behaviour is changed:

    ./run-tests --suite power-cycle --manual c64u
"""

import argparse
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from api import UltimateApi
from machine_power import (DEFAULT_SILENCE_SECONDS, DEFAULT_UP_TIMEOUT,
                           PowerButton, alive, ask, run_command, stays_off,
                           switch_machine_off, wait_for_state)
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
                "Pass --power-off-cmd and --power-on-cmd to switch the mains "
                "without an operator, or run this from a terminal.")

    def cycle(self) -> None:
        """Remove mains, hold it off long enough to matter, put it back."""
        if self.scripted:
            run_command(self.off_cmd, "power off")
            detail(f"mains off for {self.off_seconds:.0f}s")
            time.sleep(self.off_seconds)
            run_command(self.on_cmd, "power on")
        else:
            ask(f"switch the socket OFF and leave it off for "
                f"{self.off_seconds:.0f}s")
            ask("switch the socket back ON")


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
    parser.add_argument("--power-button-cmd", default="",
                        help="Shell command that presses the machine's power "
                             "button. Without it the operator is asked, which "
                             "needs a terminal: two scenarios end with the "
                             "machine off and nothing on the network can "
                             "revive it.")
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
        # Both are checked before the first scenario, so a run that cannot be
        # completed says so now rather than after the first mains cut.
        mains = Mains(args.power_off_cmd, args.power_on_cmd, args.off_seconds)
        button = PowerButton(args.power_button_cmd)

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
            if not wait_for_state(api, True, args.up_timeout):
                raise Failure(f"stayed off with the mode at {MODE_ON!r}")

        section("3. Mode Last State, machine on before the cut")
        with check("comes up by itself"):
            set_mode(api, store, MODE_LAST)
            mains.cycle()
            if not wait_for_state(api, True, args.up_timeout):
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
        button.press(api, args.up_timeout)

        section("5. Mode Off, machine on before the cut")
        with check("stays off"):
            set_mode(api, store, MODE_OFF)
            mains.cycle()
            if not stays_off(api, args.silence_seconds):
                raise Failure(f"came up with the mode at {MODE_OFF!r}")
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
        # it. A machine left off cannot be written to, and saying so is better
        # than a traceback out of the cleanup path.
        if store and original:
            try:
                api.configs.set(store, ITEM, original)
            except Exception:  # noqa: BLE001
                detail(f"could not restore {ITEM!r} to {original!r}")


if __name__ == "__main__":
    raise SystemExit(main())
