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

import socket
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
# The window has to outlast a wake this machine would actually perform, or it
# ends while a machine that did wrongly come up is still booting and reads that
# silence as "stayed off". What it does not have to outlast is the worst-case
# boot budget: `DEFAULT_UP_TIMEOUT` is a cap for a machine that may be slow, not
# a measurement, and using it made the negative check cost 90s every run.
#
# So the window is derived from a wake this run measured. `silence_window()`
# takes the measured time and multiplies it, and this constant is only the
# floor for a machine that woke faster than the floor. Measured on a C64
# Ultimate 1.2RC: a magic packet to REST answering again took 8.8s.
MIN_SILENCE_SECONDS = 10.0
SILENCE_SAFETY_FACTOR = 2.0
# Twice the wake this bench measures, with room over it. A C64 Ultimate 1.2RC
# answered 8.8s after the packet on three consecutive runs, and the suite
# checks the window it used against the wake it measured rather than trusting
# this number.
DEFAULT_SILENCE_SECONDS = 20.0
# A quarter second: every wait in this file ends as soon as the device changes
# state, so the interval, with PROBE_TIMEOUT_SECONDS, is what decides how much
# of a wake or a shutdown is spent asleep in the harness rather than watching.
POLL_SECONDS = 0.25


def silence_window(measured_wake: float | None,
                   fallback: float = DEFAULT_SILENCE_SECONDS) -> float:
    """How long to wait before believing a machine stayed off.

    `measured_wake` is how long this machine took to answer after a packet that
    was supposed to wake it, measured earlier in the same run. Without one
    there is nothing to scale from and the caller's own budget is used.
    """
    if not measured_wake:
        return fallback
    return max(MIN_SILENCE_SECONDS, measured_wake * SILENCE_SAFETY_FACTOR)

# Long enough for the control module to lose power. A brief dip leaves the
# ESP32 running, so nothing cold starts and the machine simply stays as it
# was, which reads exactly like the setting being ignored. Ten seconds was
# enough on the machine this was written against; fifteen is comfortable.
DEFAULT_OFF_SECONDS = 15.0


# What a liveness probe waits for an answer, which is not what the suite's own
# requests wait. A request to a machine that is off gets no TCP reply at all,
# so the probe blocks for its whole timeout, and run-tests passes -t 30: two
# such probes made a shutdown that finishes in about a second read as a minute.
# 1.5s is well beyond the 10-25ms this device answers a healthy /v1/version in.
PROBE_TIMEOUT_SECONDS = 1.5

_probes: dict[tuple[str, str], UltimateApi] = {}


def probe_client(api: UltimateApi) -> UltimateApi:
    """A client on the same device with a liveness-sized timeout, made once."""
    key = (api.rest.host, api.rest.password)
    probe = _probes.get(key)
    if probe is None:
        probe = UltimateApi(api.rest.target, api.rest.password or None,
                            PROBE_TIMEOUT_SECONDS)
        _probes[key] = probe
    return probe


def alive(api: UltimateApi) -> bool:
    """Whether the application answers. Nothing answers while the machine is off."""
    try:
        return bool(probe_client(api).version())
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


def switch_machine_off(api: UltimateApi, up_timeout: float) -> float:
    """Put the machine in the off state a scenario needs it to be in.

    Answers how long it took to go quiet, which is the proof that it is off
    rather than an assumption that it must be by now.
    """
    started = time.monotonic()
    api.machine.poweroff()
    if not wait_for_state(api, False, up_timeout):
        raise Failure("the machine still answered after machine:poweroff")
    took = time.monotonic() - started
    detail(f"the machine stopped answering {took:.1f}s after machine:poweroff")
    return took


def run_command(cmd: str, what: str) -> None:
    """Run one of the caller's shell commands, failing the check if it does."""
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True, check=False)
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


# The magic packet a wake tool sends, and where it sends it. Port 9 is the
# convention; the firmware matches on the pattern rather than on the port. The
# copies cover a lost datagram and cost nothing, because the watcher disarms on
# the first match.
WAKE_BROADCAST = "255.255.255.255"
WAKE_PORT = 9
WAKE_COPIES = 3
WAKE_COPY_PAUSE = 0.2


def magic_packet(mac: bytes) -> bytes:
    """The 102 bytes every wake tool sends: six 0xFF, then the MAC sixteen times."""
    return (b"\xff" * 6) + (mac * 16)


def send_magic(mac: bytes, broadcast: str = WAKE_BROADCAST,
               port: int = WAKE_PORT) -> None:
    """Send the magic packet for `mac`, repeated, to a broadcast address."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        payload = magic_packet(mac)
        for _ in range(WAKE_COPIES):
            sock.sendto(payload, (broadcast, port))
            time.sleep(WAKE_COPY_PAUSE)
    detail(f"{WAKE_COPIES} magic packets for {format_mac(mac)} to "
           f"{broadcast}:{port}")


def format_mac(mac: bytes) -> str:
    return ":".join(f"{octet:02x}" for octet in mac)


class WakePacketButton:
    """Switching a machine on with a magic packet rather than with a finger.

    The Wi-Fi module keeps running while the machine is off, so a machine whose
    "Wake On Wi-Fi" setting is Enabled comes back from a packet addressed to
    that module. That makes the scenarios which end with the machine off, and
    the setting left Enabled, recoverable without an operator or an actuator.

    The address is the module's own, which GET /v1/info reports as `wifi_mac`.
    It is not the address the harness has in its ARP table: that is whichever
    interface answered the last request, and the wired PHY is powered down with
    the machine, so a packet aimed there reaches nothing.

    A machine left off with the setting Disabled cannot be woken by anything on
    the network, which is why this is not a replacement for `PowerButton`
    everywhere; the caller decides which of the two a scenario needs.
    """

    scripted = True

    def __init__(self, mac: bytes, broadcast: str = WAKE_BROADCAST,
                 port: int = WAKE_PORT) -> None:
        self.mac = mac
        self.broadcast = broadcast
        self.port = port

    def press(self, api: UltimateApi, up_timeout: float) -> float:
        """Wake the machine, and answer how long it took to answer again."""
        started = time.monotonic()
        send_magic(self.mac, self.broadcast, self.port)
        if not wait_for_state(api, True, up_timeout):
            raise Failure(
                f"the machine did not come back after a magic packet for "
                f"{format_mac(self.mac)}. It wakes only with 'Wake On Wi-Fi' "
                f"Enabled, on Wi-Fi, and with the harness in the device's own "
                f"broadcast domain")
        took = time.monotonic() - started
        detail(f"answered {took:.1f}s after the packet")
        return took


def recover_if_off(button: PowerButton | WakePacketButton | None,
                   api: UltimateApi,
                   up_timeout: float) -> None:
    """Get the machine back on, whatever left it off, including a failure.

    `check()` re-raises, so a failure between switching the machine off and the
    press that follows skips the press, leaving a device that answers nothing
    for every later suite. A no-op when the machine is up, and it only presses,
    so it can never turn a failure into a pass.
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
