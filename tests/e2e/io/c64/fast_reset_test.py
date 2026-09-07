#!/usr/bin/env python3
"""E2E: Fast Reset has to make the machine reach the BASIC prompt sooner.

Most of the time a C64 spends between a reset and the READY prompt is the
KERNAL's RAM test at $FD6C. The firmware's "Fast Reset" setting replaces that
test with 22 bytes that skip it and jump to $FD8C, patched into the KERNAL
image the machine boots from. The setting is applied when the system ROMs are
loaded, which happens when the cartridge starts, so it takes effect on the next
reboot rather than on the next reset. This suite reboots between the two
states for that reason.

The observable is the only one a user has: how long the machine takes to print
READY after a reset. The suite measures that with the setting enabled and with
it disabled, and requires the enabled state to be the faster of the two.

Measured reset-to-READY, median of five samples, the screen blanked before each
reset so a READY left by the previous boot cannot match:

    Ultimate 64 Elite I, firmware 3.15, git 4acc148c, before the fix
        Enabled 2.39s   Disabled 2.39s
    Ultimate 64 Elite I, firmware 3.15, git 4acc148c, with the fix
        Enabled 0.18s   Disabled 2.40s
    C64 Ultimate, firmware 1.2RC, git 6e1530b0, which already applied the patch
        Enabled 0.18s   Disabled 2.45s

So a machine that applies the patch is about ten times faster and a machine
that does not is not faster at all. MIN_SPEEDUP is 2.0, which sits between
those two populations with a wide margin on both sides, and the margin is what
absorbs a slow network: one screen read costs about 25ms on this bench, and
even a poll costing 0.5s would leave the enabled measurement at roughly 0.7s
against a 1.20s threshold. Above that the measurement can no longer resolve the
difference, so the suite says so and skips rather than failing.

Two things the pass would otherwise not mean anything against:

- The disabled measurement is the control. If the machine reaches READY
  quickly with Fast Reset disabled, then it is not running the RAM test the
  setting removes, and no comparison against it says anything about the
  setting. That is reported as a failure of the control, and named as such,
  rather than as the Fast Reset defect.
- A machine whose KERNAL the firmware does not supply cannot be patched at
  all, so Fast Reset genuinely does nothing there. On an Ultimate 64 the
  firmware always loads the KERNAL from a file, and the store carries a
  "Kernal ROM" item. On an Ultimate II the KERNAL comes from the computer
  unless an alternate one is configured, and the store carries an "Alternate
  Kernal" item instead. The suite skips when that item is empty.

The machine is left with the Fast Reset setting it was found with, and rebooted
so that setting is the one in force.
"""

from __future__ import annotations

import argparse
import statistics
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import cli                                                          # noqa: E402
from api import READY_SCREEN_CODES, SCREEN_RAM, UltimateApi          # noqa: E402
from report import (Failure, check, check_ok, check_skip, check_start,  # noqa: E402
                    detail, format_exception, section, suite_fail,
                    suite_ok, suite_skip, teardown_step)

SUITE = "fast_reset_test"

STORE = "C64 and Cartridge Settings"
FAST_RESET = "Fast Reset"
ENABLED = "Enabled"
DISABLED = "Disabled"

# The item that says where the KERNAL the machine boots comes from. An
# Ultimate 64 serves the first and always loads a file; an Ultimate II serves
# the second, and leaves it empty when the computer's own KERNAL is in use.
KERNAL_FILE_ITEM = "Kernal ROM"
ALTERNATE_KERNAL_ITEM = "Alternate Kernal"

# How much of the screen is blanked and searched. The KERNAL prints READY on
# the seventh line, so 400 bytes covers it, and a shorter read is a cheaper
# poll and therefore a finer measurement.
SCREEN_BYTES = 400
BLANK = bytes([0x20]) * SCREEN_BYTES

# Three samples per state, reported as a median, because the quantity separating
# a pass from a failure is about 2.2s and the spread within a state was 0.02s
# over five samples on this bench. The median discards a single sample delayed
# by the network.
SAMPLES = 3

# See the measurements in the module docstring.
MIN_SPEEDUP = 2.0

# Below this, the machine did not run the RAM test with the setting disabled, so
# the comparison has nothing to measure against. The disabled state measured
# 2.39s on an Ultimate 64 and 2.45s on a C64 Ultimate; 1.20s is half of that.
MIN_CONTROL_SECONDS = 1.20

# One screen read is the resolution of the measurement. 12 to 18ms on this bench
# over wired Ethernet. Above this the enabled state cannot be told from the
# threshold any more, so the suite skips instead of reporting a failure it
# cannot stand behind.
MAX_POLL_SECONDS = 0.5

RESET_TIMEOUT_SECONDS = 20.0
REBOOT_TIMEOUT_SECONDS = 30.0

# READY appearing after a reboot says the KERNAL has printed it, not that the
# firmware has finished restarting the cartridge. A reset issued in that window
# measured 10.74s once, against 2.32s and 2.34s for the two samples after it, so
# the first sample of every state waits this long after the prompt appears.
SETTLE_SECONDS = 2.0


def serves(api: UltimateApi, store: str, item: str) -> bool:
    """Whether this machine has that setting, without raising if it has not."""
    try:
        return item in api.configs.category(store)
    except Failure:
        return False


def kernal_source(api: UltimateApi) -> str | None:
    """Why the firmware can patch this machine's KERNAL, or None if it cannot."""
    if serves(api, STORE, KERNAL_FILE_ITEM):
        name = api.configs.current(STORE, KERNAL_FILE_ITEM)
        return f"{KERNAL_FILE_ITEM} is {name!r}, so the firmware supplies the KERNAL"
    if serves(api, STORE, ALTERNATE_KERNAL_ITEM):
        name = api.configs.current(STORE, ALTERNATE_KERNAL_ITEM)
        if name:
            return f"{ALTERNATE_KERNAL_ITEM} is {name!r}, so the firmware supplies the KERNAL"
    return None


def poll_cost(api: UltimateApi) -> float:
    """Seconds one screen read takes, which is the resolution of a measurement."""
    started = time.monotonic()
    api.machine.readmem(SCREEN_RAM, SCREEN_BYTES)
    return time.monotonic() - started


def seconds_to_ready(api: UltimateApi) -> float:
    """Reset the machine and return how long it took to print READY.

    The screen is blanked first, so the READY of the previous boot cannot be
    read as this one's. The reset is forced because the previous sample left
    the client believing nothing has moved the machine since, which would make
    this reset a no-op.
    """
    api.machine.writemem(SCREEN_RAM, BLANK)
    started = time.monotonic()
    api.machine.reset(force=True, wait=False)
    while True:
        if READY_SCREEN_CODES in api.machine.readmem(SCREEN_RAM, SCREEN_BYTES):
            return time.monotonic() - started
        if time.monotonic() - started > RESET_TIMEOUT_SECONDS:
            raise Failure(f"the machine did not print READY within "
                          f"{RESET_TIMEOUT_SECONDS:.0f}s of a reset")


def apply_and_reboot(api: UltimateApi, value: str) -> None:
    """Write the Fast Reset setting and restart the cartridge so it takes effect."""
    api.configs.set(STORE, FAST_RESET, value)
    api.machine.writemem(SCREEN_RAM, BLANK)
    api.machine.reboot()
    deadline = time.monotonic() + REBOOT_TIMEOUT_SECONDS
    while READY_SCREEN_CODES not in api.machine.readmem(SCREEN_RAM, SCREEN_BYTES):
        if time.monotonic() > deadline:
            raise Failure(f"the machine did not print READY within "
                          f"{REBOOT_TIMEOUT_SECONDS:.0f}s of a reboot with "
                          f"{FAST_RESET}={value}")
        time.sleep(0.05)
    time.sleep(SETTLE_SECONDS)


def measure(api: UltimateApi, value: str) -> float:
    """Median reset-to-READY seconds with Fast Reset set to `value`."""
    apply_and_reboot(api, value)
    samples = [seconds_to_ready(api) for _ in range(SAMPLES)]
    median = statistics.median(samples)
    detail(f"{FAST_RESET}={value}: reset to READY "
           f"{', '.join(f'{s:.2f}s' for s in samples)}, median {median:.2f}s")
    return median


def run(args) -> str | None:
    """Run the suite, or answer why this machine could not run it."""
    api = UltimateApi(args.host, args.password or None, args.timeout)
    info = api.info()

    # Asked before the check opens: a machine without the store answers with an
    # error and ConfigsApi.category raises on it, which inside the check would
    # report a failure on exactly the machines meant to be skipped.
    source = kernal_source(api) if serves(api, STORE, FAST_RESET) else None
    check_start(f"the firmware supplies this machine's KERNAL, so {FAST_RESET} can patch it")
    if source is None:
        reason = (f"{info.product} does not serve a {FAST_RESET} setting over a "
                  f"KERNAL the firmware supplies, so the setting cannot do anything")
        check_skip(reason)
        return reason
    check_ok(f"{info.product}, firmware {info.firmware_version}: {source}")

    cost = poll_cost(api)
    check_start("one screen read is quick enough to resolve the difference")
    if cost > MAX_POLL_SECONDS:
        reason = (f"one {SCREEN_BYTES}-byte screen read costs {cost * 1000:.0f}ms, "
                  f"over the {MAX_POLL_SECONDS * 1000:.0f}ms this measurement "
                  f"needs; the link is too slow to time a reset on")
        check_skip(reason)
        return reason
    check_ok(f"one {SCREEN_BYTES}-byte screen read costs {cost * 1000:.0f}ms")

    was = api.configs.current(STORE, FAST_RESET)
    try:
        section("1. The control, with Fast Reset disabled")
        with check("the machine runs the KERNAL RAM test when Fast Reset is off"):
            slow = measure(api, DISABLED)
            if slow < MIN_CONTROL_SECONDS:
                raise Failure(
                    f"the machine reached READY in {slow:.2f}s with {FAST_RESET} "
                    f"off, under the {MIN_CONTROL_SECONDS:.2f}s a machine running "
                    f"the KERNAL RAM test takes. Something other than this "
                    f"setting is already skipping that test, so there is nothing "
                    f"for the measurement below to be faster than.")

        section("2. The guard, with Fast Reset enabled")
        with check("the machine reaches READY sooner when Fast Reset is on"):
            fast = measure(api, ENABLED)
            speedup = slow / fast if fast else 0.0
            detail(f"{slow:.2f}s disabled against {fast:.2f}s enabled, {speedup:.1f}x")
            if speedup < MIN_SPEEDUP:
                raise Failure(
                    f"{FAST_RESET}={ENABLED} reached READY in {fast:.2f}s against "
                    f"{slow:.2f}s with it {DISABLED}, {speedup:.1f}x, where at "
                    f"least {MIN_SPEEDUP:.1f}x was expected. The KERNAL the "
                    f"machine booted still runs the RAM test at $FD6C, so the "
                    f"firmware did not patch the image it loaded.")
        return None
    finally:
        teardown_step(f"restore {FAST_RESET}={was} and reboot",
                      lambda: apply_and_reboot(api, was))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that Fast Reset makes the machine reach the BASIC "
                    "prompt sooner.")
    cli.add_device_arguments(parser)
    args = parser.parse_args()
    try:
        skipped = run(args)
        if skipped:
            suite_skip(SUITE, skipped)
            return 0
    except Exception as exc:            # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
