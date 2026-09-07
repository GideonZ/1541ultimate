#!/usr/bin/env python3
"""E2E: Fast Reset has to make the machine reach the BASIC prompt sooner.

Most of the time between a reset and the READY prompt is the KERNAL's RAM test
at $FD6C. Fast Reset patches 22 bytes over it that jump to $FD8C. The patch is
applied when the system ROMs load, which is on cartridge start, so this suite
reboots between the two states rather than only resetting.

The measurement is reset to READY, screen blanked first so a READY from the
previous boot cannot match, once per state. On an Ultimate 64 Elite I that is
0.18s enabled against 2.40s disabled where the patch lands, and 2.40s against
2.42s where it does not, so a single reset each separates them by 10x and no
averaging is needed. Above MAX_POLL_SECONDS the poll cost swamps the
difference and the suite skips instead of failing.

Two checks the verdict depends on:

- The disabled measurement is the control. A machine that reaches READY
  quickly with Fast Reset off is not running the RAM test the setting removes,
  so the comparison says nothing. That is reported as a control failure.
- A machine whose KERNAL the firmware does not supply cannot be patched. An
  Ultimate 64 always loads the KERNAL from a file; an Ultimate II takes it from
  the computer unless an "Alternate Kernal" is configured. The suite skips when
  that item is empty.

The setting and a reboot into it are restored on the way out.
"""

from __future__ import annotations

import argparse
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

# One reset each is enough: the two states are about 10x apart and a machine
# that cannot patch is 1.0x, so no threshold between them is delicate. 2.0 keeps
# the margin wide on a slower link.
MIN_SPEEDUP = 2.0

# Half the 2.40s the disabled state measures. Below this the machine is not
# running the RAM test, so there is nothing to compare against.
MIN_CONTROL_SECONDS = 1.20

# One screen read is the resolution, 12 to 18ms here. Past this the poll cost
# swamps the difference, so the suite skips rather than guess.
MAX_POLL_SECONDS = 0.5

RESET_TIMEOUT_SECONDS = 20.0
REBOOT_TIMEOUT_SECONDS = 30.0

# READY after a reboot means the KERNAL printed it, not that the firmware has
# finished restarting the cartridge. A reset inside that window measured 10.74s
# against 2.32s just after, so the first sample of each state waits.
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
    """Reset-to-READY seconds with Fast Reset set to `value`."""
    apply_and_reboot(api, value)
    seconds = seconds_to_ready(api)
    detail(f"{FAST_RESET}={value}: reset to READY {seconds:.2f}s")
    return seconds


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
