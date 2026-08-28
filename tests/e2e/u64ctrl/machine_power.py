"""Watching a machine go off and come back, and the hands that make it happen.

Shared by the suites in this folder, all of which assert what the control module
does with the machine's power. Two things they have in common:

- While the machine is off, the Ultimate application is not running, and with
  it neither the REST API nor the IP stack it serves. "Off" is therefore read
  as silence, and silence has to be waited out rather than sampled: a machine
  that is booting is silent too.
- Getting a deliberately-off machine back needs something outside the network.
  That is the operator, unless a power-button actuator is given, and either way
  it is `PowerButton` that knows which.

The waits here are their own rather than `tests/lib/wait.py`'s: these are
measured in minutes, the negative ones have to keep polling to prove absence
rather than stop at the first hit, and a timeout is a check's own failure text
rather than an exception from the helper.
"""

from __future__ import annotations

import subprocess
import sys
import time

from api import UltimateApi
from report import Failure, detail

# A cold start has to load the FPGA before the application answers anything.
DEFAULT_UP_TIMEOUT = 90.0
# How long a machine that is supposed to stay off has to stay silent for a
# check to believe it.
#
# It has to be at least the boot budget above, and for a reason worth stating:
# the two are the same question asked twice. A machine that wrongly powers up
# has to load the FPGA, boot the application and get on the network before
# anything answers, and this suite already says that may take up to
# DEFAULT_UP_TIMEOUT. A shorter silence window therefore proves nothing -- it
# ends while a machine that did wrongly come up is still booting, and reads its
# silence as "stayed off". This was 30s, which is a third of the budget the
# positive checks give the very same boot.
DEFAULT_SILENCE_SECONDS = DEFAULT_UP_TIMEOUT
POLL_SECONDS = 2.0

# Long enough for the control module to lose power. A brief dip leaves the
# ESP32 running, so nothing cold starts and the machine simply stays as it
# was, which reads exactly like the setting being ignored. Ten seconds was
# enough on the machine this was written against; fifteen is comfortable.
DEFAULT_OFF_SECONDS = 15.0


def alive(api: UltimateApi) -> bool:
    """Whether the application answers. Nothing answers while the machine is off."""
    try:
        return bool(api.version())
    except Exception:  # noqa: BLE001  (any transport failure means "not there")
        return False


def wait_for_state(api: UltimateApi, want_alive: bool, timeout: float) -> bool:
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


def switch_machine_off(api: UltimateApi, up_timeout: float) -> None:
    """Put the machine in the off state a scenario needs it to be in."""
    api.machine.poweroff()
    if not wait_for_state(api, False, up_timeout):
        raise Failure("the machine still answered after machine:poweroff")


def run_command(cmd: str, what: str) -> None:
    """Run one of the caller's shell commands, failing the check if it does."""
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        raise Failure(f"{what} command failed with {result.returncode}: "
                      f"{(result.stderr or result.stdout).strip()[:200]}")


def ask(instruction: str) -> None:
    """Ask the operator to do something the harness cannot do itself."""
    print(f"      >>> {instruction}, then press Enter: ", end="", flush=True)
    sys.stdin.readline()


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


def recover_if_off(button: "PowerButton | None", api: UltimateApi,
                   up_timeout: float) -> None:
    """Get the machine back on, whatever left it off, including a failure.

    Every scenario that ends with the machine off is followed by a press, but
    only on the path where the scenario passed: `check()` re-raises, so a
    failure between switching the machine off and that press skips it. What is
    left behind is a device that answers nothing -- the setting restore fails,
    and so does the runner's teardown and every suite after this one, none of
    which look like the failure that caused it.

    A no-op when the machine is up, so the suites can call it unconditionally
    on their way out. It never turns a failure into a pass: it only presses.
    """
    if button is None or alive(api):
        return
    try:
        button.press(api, up_timeout)
    except Exception as exc:  # noqa: BLE001  (recovery must not mask the failure)
        detail(f"could not switch the machine back on: {exc}")


class PowerButton:
    """The machine's power button: an actuator that can be driven, or a person.

    A machine that is off cannot be switched on over the network -- that is the
    whole point of the setting these suites cover -- so this is the only way
    back from the scenarios that end with the machine off. Without an actuator
    it is a person, and a person needs a terminal to be asked on: a run whose
    stdin is at EOF would otherwise sail past the prompt and wait out a timeout
    with nobody having touched the machine.
    """

    def __init__(self, press_cmd: str = "") -> None:
        self.press_cmd = press_cmd
        self.scripted = bool(press_cmd)
        if not self.scripted and not sys.stdin.isatty():
            raise Failure(
                "no terminal to prompt on and no power-button actuator given.\n"
                "Two of these scenarios end with the machine deliberately off, "
                "and only its power button can revive it. Pass "
                "--power-button-cmd to drive an actuator, or run this from a "
                "terminal.")

    def press(self, api: UltimateApi, up_timeout: float) -> None:
        """Get a deliberately-off machine running again, for what comes next.

        Leaving the machine off would fail the next check and then the runner's
        own teardown, which resets the machine after every suite.
        """
        if self.scripted:
            run_command(self.press_cmd, "power button")
            detail("power button pressed by the actuator")
        else:
            ask("press the machine's power button to switch it back on")
        if not wait_for_state(api, True, up_timeout):
            raise Failure("the machine did not come back after the power button; "
                          "the rest of the suite cannot run")
