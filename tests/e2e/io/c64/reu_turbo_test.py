#!/usr/bin/env python3
"""E2E: an REU transfer started above 1 MHz still returns the bytes it was given.

Starting an REU transfer has to stop the 6510 in the same cycle as the write to
$DF01. If the core inserts any delay before it asserts DMA, a machine running
faster than 1 MHz executes the instructions that follow while the transfer is
already in progress. Real REU code puts the setup for the next transfer there,
so those instructions rewrite the base and length registers of the transfer in
flight and the data that comes back is not the data that went out. At 1 MHz the
CPU is halted before the next instruction can run, so the same code is clean and
nothing in the suite set notices.

That is the shape of GideonZ/1541ultimate#804: Doom C64U streams level data out
of a 16 MB REU with the CPU in turbo, its boot-time check of the REU image
failed, and the picture was corrupt. The fix is in the U64 and U64 Elite II
cores, where `reu.vhd` now asserts `reu_dma_n` on the command write itself when
`g_no_dma_delay` is set. This suite is the regression guard for it, and needs
neither that game nor a network.

The stimulus is `reu_turbo.asm`, which is written the way
`software/6502/unsorted/reu_test.tas` is: it copies 1 KB of screen memory out to
the REU and straight back into another block, with no instruction between the
command write and the register writes for the transfer that follows, and
compares every byte. Anything inserted in that gap hides the defect.

Three measurements, in this order, because each one only means something if the
one before it held:

1. the same program at 1 MHz, which is the control. A failure here is not the
   defect above: it is an REU that is off, absent, or broken outright, and it
   also rules out this suite's own polling. The host reads the result block
   over machine:readmem, which is a DMA cycle on the same bus, and the 1 MHz
   run is polled the same way as the fast one for long enough that interference
   would show up here first.
2. the program at the highest CPU speed the machine offers, which is the guard.
3. that the fast run really was fast. Without this a machine that quietly
   ignored the CPU speed setting would run measurement 2 at 1 MHz and pass it
   for the wrong reason.

The machine is left as it was found: every setting this changes is read first
and written back, whatever the run does.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from typing import Dict, Tuple

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "lib"))
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))

from api import UltimateApi                                        # noqa: E402
from assembler import assemble                                     # noqa: E402
from report import (Failure, check, check_ok, check_skip,          # noqa: E402
                    check_start, detail, format_exception, section,
                    suite_fail, suite_ok)

SUITE = "reu_turbo_test"

SOURCE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "reu_turbo.asm")

# The result block reu_turbo.asm writes. These mirror its equates and have to be
# changed with it.
STATUS = 0xC000         # 0 running, 1 every round matched, 2 a byte differed
READY = 0xC001          # $A5 once the program is running
ITER = 0xC002           # the round that was running
ERRADDR = 0xC003        # 16 bit, the address of the first byte that differed
EXPECT = 0xC005
GOT = 0xC006
RUNNING = 0xC007        # $A5 once the settle wait is over and the rounds began
BLOCK_BASE = STATUS
RESULT_BYTES = 8

STATUS_RUNNING = 0x00
STATUS_PASSED = 0x01
STATUS_MISMATCH = 0x02
READY_MARK = 0xA5

# reu_turbo.asm runs 256 rounds of 1 KB out and back, so each speed round-trips
# 256 KB. Measured on a U64 Elite, firmware 3.15, core 1.4F: 8.5 s at 1 MHz and
# 0.32 s at 48 MHz, plus the program's own 5 s settle wait at each speed.
ROUNDS = 256
BYTES_PER_ROUND = 1024

REU_STORE = "C64 and Cartridge Settings"
REU_ITEM = "RAM Expansion Unit"
U64_STORE = "U64 Specific Settings"
TURBO_ITEM = "Turbo Control"
SPEED_ITEM = "CPU Speed"
BADLINE_ITEM = "Badline Timing"

# "Manual" runs the machine at the configured CPU Speed without the program
# having to touch a turbo register, so the stimulus stays the plain REU sequence
# and nothing model-specific gets into it.
TURBO_MANUAL = "Manual"
SLOWEST_SPEED = " 1"            # the enum labels are right-aligned in two columns

# How much faster the fast run has to be before the CPU speed setting counts as
# having taken effect. Measured at 30x on a U64 Elite (8.5 s against 0.32 s),
# so five leaves six times the margin. It is not only a check that the setting
# works: without the settle wait in reu_turbo.asm the fast run spends its first
# seconds at 1 MHz, and that measured 3.0x, which is what this rules out.
MIN_SPEEDUP = 5.0

START_TIMEOUT_SECONDS = 20.0
# reu_turbo.asm waits 250 frames before its first round, so that it does not do
# its work inside the seconds of CPU and REU slowdown the machine applies after
# a reset. That is 5.0s on PAL and 4.2s on NTSC.
SETTLE_TIMEOUT_SECONDS = 20.0
RUN_TIMEOUT_SECONDS = 60.0
POLL_SECONDS = 0.05


def serves(api: UltimateApi, store: str, item: str) -> bool:
    """Whether this machine has that setting, without raising if it has not."""
    try:
        return item in api.configs.category(store)
    except Failure:
        return False


def fastest_speed(api: UltimateApi) -> str:
    """The highest CPU Speed this machine offers, as the label to write back."""
    values = api.configs.item(U64_STORE, SPEED_ITEM).get("values", [])
    labels = [value for value in values if isinstance(value, str)]
    if not labels:
        raise Failure(f"{U64_STORE}/{SPEED_ITEM} offers no values: {values!r}")
    return labels[-1]


def apply_settings(api: UltimateApi, previous: Dict[Tuple[str, str], str],
                   speed: str) -> None:
    """Apply what the stimulus needs, recording each previous value first.

    `previous` belongs to the caller, so a failure part way through still leaves
    it holding everything already changed. A suite that returned the dict
    instead would abandon the machine in turbo.
    """
    wanted = ((REU_STORE, REU_ITEM, "Enabled"),
              (U64_STORE, BADLINE_ITEM, "Enabled"),
              (U64_STORE, TURBO_ITEM, TURBO_MANUAL),
              (U64_STORE, SPEED_ITEM, speed))
    for store, item, value in wanted:
        if (store, item) not in previous:
            previous[(store, item)] = api.configs.current(store, item)
        if api.configs.current(store, item) != value:
            api.configs.set(store, item, value)


def restore_settings(api: UltimateApi, previous: Dict[Tuple[str, str], str]) -> None:
    for (store, item), value in previous.items():
        if not value:
            # current() answers "" when the device reported no value, and
            # writing that back is refused. Saying so beats a silent skip,
            # which would leave a setting this suite changed unrestored and
            # unmentioned.
            detail(f"cannot restore {store}/{item}: it had no readable value")
            continue
        try:
            api.configs.set(store, item, value)
        except Exception as exc:        # noqa: BLE001 - teardown must continue
            detail(f"could not restore {store}/{item} to {value!r}: {exc}")


class Result:
    """What one run of the stimulus reported, and how long it took.

    `block` is the result block as machine:readmem returned it, so every field
    is read at its own address minus the block's base rather than at a second
    set of literal offsets that could drift from the equates above.
    """

    def __init__(self, block: bytes, seconds: float) -> None:
        self.status = block[STATUS - BLOCK_BASE]
        self.round = block[ITER - BLOCK_BASE]
        self.address = (block[ERRADDR - BLOCK_BASE]
                        | (block[ERRADDR + 1 - BLOCK_BASE] << 8))
        self.expect = block[EXPECT - BLOCK_BASE]
        self.got = block[GOT - BLOCK_BASE]
        self.seconds = seconds

    @property
    def passed(self) -> bool:
        return self.status == STATUS_PASSED

    def describe(self) -> str:
        if self.status == STATUS_MISMATCH:
            return (f"${self.address:04X} came back as ${self.got:02X} where "
                    f"${self.expect:02X} went out, in round {self.round} of "
                    f"{ROUNDS}")
        # The program only ever writes $01 or $02 here. Anything else means the
        # block read back is not the one it wrote, which is a device that was
        # still settling or a reset that landed mid-run, and saying so beats
        # reporting it as an REU that returned wrong data.
        return (f"the result block reads back as status ${self.status:02X} in "
                f"round {self.round}, which the program never writes")


def await_mark(api: UltimateApi, address: int, timeout: float, what: str) -> None:
    """Wait for the 6502 program to write $A5 at `address`."""
    deadline = time.time() + timeout
    while api.machine.readmem(address, 1)[0] != READY_MARK:
        if time.time() > deadline:
            raise Failure(f"{what}: it did not become ${READY_MARK:02X} within "
                          f"{timeout:.0f}s")
        time.sleep(POLL_SECONDS)


def run_stimulus(api: UltimateApi, prg: bytes, speed: str) -> Result:
    """Run the 6502 program once at `speed` and return what it reported."""
    api.configs.set(U64_STORE, SPEED_ITEM, speed)
    # The result block is RAM the machine does not clear, so a stale $01 from
    # the previous run would read as a pass before this one has started.
    api.machine.writemem(BLOCK_BASE, bytes(RESULT_BYTES), idempotent=True)

    status, _, body = api.runners.upload("run_prg", prg)
    if status != 200:
        raise Failure(f"runners:run_prg returned HTTP {status}: {body[:160]!r}")

    await_mark(api, READY, START_TIMEOUT_SECONDS,
               f"the 6502 program never started at ${READY:04X}")
    # Timed from here, not from the upload, so the settle wait is not counted as
    # work and the two speeds stay comparable.
    await_mark(api, RUNNING, SETTLE_TIMEOUT_SECONDS,
               f"the 6502 program never left its settle wait at ${RUNNING:04X}")

    started = time.time()
    deadline = started + RUN_TIMEOUT_SECONDS
    while True:
        block = api.machine.readmem(BLOCK_BASE, RESULT_BYTES)
        if block[0] != STATUS_RUNNING:
            return Result(block, time.time() - started)
        if time.time() > deadline:
            raise Failure(
                f"the 6502 program did not finish within "
                f"{RUN_TIMEOUT_SECONDS:.0f}s at {speed.strip()} MHz; it reached "
                f"round {block[ITER - BLOCK_BASE]} of {ROUNDS}")
        time.sleep(POLL_SECONDS)


def report(result: Result, speed: str) -> None:
    detail(f"{ROUNDS} rounds of {BYTES_PER_ROUND} bytes out and back at "
           f"{speed.strip()} MHz in {result.seconds:.2f}s")


def run(args) -> bool:
    """Returns True when the suite has already reported itself and should stop."""
    api = UltimateApi(args.host, args.password or None, args.timeout)
    info = api.info()

    # Asked before the check opens: a machine without these stores answers with
    # an error and ConfigsApi.category raises on it, which inside the check
    # would report a failure on exactly the machines meant to be skipped.
    equipped = (serves(api, REU_STORE, REU_ITEM)
                and serves(api, U64_STORE, SPEED_ITEM))
    check_start("the machine has an REU and a CPU speed setting")
    if not equipped:
        check_skip(f"{info.product} does not serve both {REU_STORE}/{REU_ITEM} "
                   f"and {U64_STORE}/{SPEED_ITEM}")
        return True
    check_ok(f"{info.product}, firmware {info.firmware_version}, "
             f"FPGA {info.fpga_version}, core "
             f"{info.extra.get('core_version', '?')}")

    prg = assemble(SOURCE)
    detail(f"assembled reu_turbo.asm: {len(prg)} bytes, load address "
           f"${int.from_bytes(prg[:2], 'little'):04X}")

    previous: Dict[Tuple[str, str], str] = {}
    try:
        section("1. Set the machine up")
        with check("apply the REU and turbo settings the stimulus needs"):
            apply_settings(api, previous, SLOWEST_SPEED)
            speed = fastest_speed(api)
            detail(f"{REU_ITEM}=Enabled, {TURBO_ITEM}={TURBO_MANUAL}, "
                   f"{BADLINE_ITEM}=Enabled, {SPEED_ITEM} sweeps "
                   f"{SLOWEST_SPEED.strip()} then {speed.strip()} MHz")

        section("2. The control, at 1 MHz")
        with check(f"the REU round trip is clean at {SLOWEST_SPEED.strip()} MHz"):
            slow = run_stimulus(api, prg, SLOWEST_SPEED)
            report(slow, SLOWEST_SPEED)
            if not slow.passed:
                raise Failure(
                    f"the REU did not return what it was given even at "
                    f"{SLOWEST_SPEED.strip()} MHz: {slow.describe()}. That is "
                    f"an REU that is off or broken outright, not the "
                    f"start-of-transfer defect this suite guards.")

        section("3. The guard, at full speed")
        with check(f"the REU round trip is clean at {speed.strip()} MHz"):
            fast = run_stimulus(api, prg, speed)
            report(fast, speed)
            if not fast.passed:
                raise Failure(
                    f"{fast.describe()} at {speed.strip()} MHz, while the same "
                    f"program is clean at {SLOWEST_SPEED.strip()} MHz. The "
                    f"write to $DF01 did not stop the CPU in its own cycle, so "
                    f"the register writes that follow it landed on a transfer "
                    f"that was still running.")

        with check(f"the {SPEED_ITEM} setting took effect"):
            speedup = slow.seconds / fast.seconds if fast.seconds else 0.0
            detail(f"{slow.seconds:.2f}s at {SLOWEST_SPEED.strip()} MHz against "
                   f"{fast.seconds:.2f}s at {speed.strip()} MHz, "
                   f"{speedup:.1f}x")
            if speedup < MIN_SPEEDUP:
                raise Failure(
                    f"the run at {speed.strip()} MHz was only {speedup:.1f}x "
                    f"the run at {SLOWEST_SPEED.strip()} MHz, so the machine "
                    f"did not change speed and the check above passed at "
                    f"1 MHz rather than in turbo")
        return False
    finally:
        restore_settings(api, previous)
        try:
            api.machine.reset(force=True)
        except Exception:               # noqa: BLE001 - best effort teardown
            pass


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that an REU transfer started above 1 MHz returns "
                    "the bytes it was given.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float, default=30.0)
    args = parser.parse_args()
    try:
        if run(args):
            suite_ok(SUITE)
            return 0
    except Failure as exc:
        suite_fail(SUITE, str(exc))
        return 1
    except Exception as exc:            # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
