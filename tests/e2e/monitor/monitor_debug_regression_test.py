#!/usr/bin/env python3
# E2E: Runs the debugger lanes that carry independent risk - every transport-divergent path and every lane with a failure history - once each.

"""Machine Code Monitor debugger regression gate.

The exhaustive `machine-code-monitor-matrix` suite runs the full product of 5
memory modes x 3 UI transports x 3 repetitions, and `machine-code-monitor-debug`
runs all 21 semantic groups over Telnet. Between them that is over two hours on
a U64, which is too long to run before a merge, so in practice neither ran and
regressions were found by the next person to open the monitor.

Two parts of that product are worth paying for. The first is where the
firmware genuinely diverges: a handful of places where the same debugger
operation takes a different code path depending on which UI owns the machine,
so a result on one transport says nothing about the other. The second is where
it has actually broken before, which the run ledgers and the branch's own
defect history name cell by cell. Everywhere else one lane's result predicts
the rest, and running those lanes again buys wall clock and no information.

This runner selects those two parts and nothing else. It owns no test logic of
its own - every check here is executed by `monitor_debug_matrix_test.py` or
`monitor_debug_test.py` - and each selected lane carries the reason it is in
the gate and the evidence behind that reason, printed by `--list-plan` and
written into the artifact directory. The full derivation is in
`tests/e2e/doc/machine-code-monitor-regression.md`.

The one check it adds is a ROM-image fence, because nothing else asserts it:
see `RomImageFence` below.

Run it directly the same way as its siblings:

    python3 tests/e2e/monitor/monitor_debug_regression_test.py \
        --host u64 --rest-host u64 --password <pass>
    python3 tests/e2e/monitor/monitor_debug_regression_test.py \
        --host u2 --rest-host u2 --c64-host c64u --password <pass>

or through the runner, which fills the hosts in from the target token:

    ./run-tests -H u64 -s machine-code-monitor-regression
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional, Sequence

MCM_DIR = Path(__file__).resolve().parent
REPO_ROOT = MCM_DIR.parents[2]
sys.path.insert(0, str(MCM_DIR))

import monitor_debug_matrix_test as matrix  # noqa: E402
import mcm_localui as L  # noqa: E402

sys.path.insert(0, str(REPO_ROOT / "tests" / "lib"))
import report as R  # noqa: E402

SUITE = "machine-code-monitor-regression"


# ---------------------------------------------------------------------------
# The plan.
#
# Each entry carries the reason it is selected, because a lane nobody can
# justify is a lane the next person will delete or duplicate. `why` is the
# risk it uniquely carries; `evidence` names where that risk was established.
# Both are printed by --list-plan and land in the artifact directory, so the
# rationale travels with the run rather than living only in a review comment.
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Cell:
    memory: str
    ui: str
    reps: int
    why: str
    evidence: str

    def term(self) -> str:
        return f"{self.memory}:{self.ui}:{self.reps}"


@dataclass(frozen=True)
class Group:
    name: str
    why: str
    evidence: str


@dataclass(frozen=True)
class Deferred:
    what: str
    why: str
    # The memory modes this entry accounts for, named rather than left to be
    # read out of the prose. A mode that is neither run nor named here has been
    # dropped silently, which the host-side suite fails on.
    modes: tuple[str, ...] = ()


# Repetitions are not a confidence multiplier here. The matrix builds the same
# fixture from the same bytes for every repetition - `build_fixture` takes no
# seed - so a second repetition re-runs an identical sequence against a machine
# that has already been through it once. That is exactly the state in which the
# intermittent failures happened: of the failures the run ledgers recorded that
# were reachable at all, about half appeared only on repetition 2 or 3, every
# one of them a debug-entry `goto` timeout or a fixture round-trip mismatch. So
# repetitions are spent only where a repeat-only failure has actually been
# recorded, and only on the transports cheap enough to repeat.
#
# Every one of the five memory modes is run. Three of them - plain RAM, RAM
# under ROM, visible ROM - are the regions the debugger was built to work in,
# and the two boundary modes are how a developer actually moves between them:
# RAM into ROM and back, and RAM into RAM-under-ROM and back out through ROM.
# None of those is inferred from another mode's result. What the selection cuts
# is the transports a mode is repeated on, never the mode.
U64_CELLS: tuple[Cell, ...] = (
    Cell("ram", "overlay", 1,
         "Plain RAM through the whole debugger flow: Step Over, a 32-level "
         "Step Into chain, Step Out, Continue To Cursor, Continue To "
         "Breakpoint, Continue, Reset, the breakpoint slot table proved empty "
         "afterwards, and the 32-call straight-call run. The closing "
         "random-program run drives RAM stepping and JSR nesting far harder "
         "than this, but it performs none of Step Out, either Continue "
         "variant, Reset or slot hygiene, so it does not stand in for this "
         "cell.",
         "monitor_debug_stress.py drives run_program_session, run_jsr_session "
         "and liveness_check only; RAM_OVERLAY_REP_02 goto $C000 timeout"),
    Cell("rom", "telnet", 1,
         "Visible-ROM linear Step Over/Into on the live BRK path, and "
         "Continue To Cursor launched from ROM without the priming step - both "
         "are guarded by `!debug_owner.remote`, which is true only over Telnet. "
         "This is also the only transport whose driver permits REST readback "
         "while Debug is active, so it is the only lane that proves the pushed "
         "return address on the stack rather than inferring it.",
         "monitor_debug_brk_session.cc:2192 and :2673; "
         "RestDebugDriver.active_debug_readback_allowed() returns False"),
    Cell("rom", "overlay", 2,
         "Visible-ROM linear steps taken through the RAM trampoline, with "
         "Step Over of a ROM callee and Step Out still going through "
         "breakpoint+Go - a combination neither of the other two transports "
         "produces. Two repetitions because the recorded failures are on "
         "repetitions 2 and 3.",
         "ROM_OVERLAY_REP_02 and _REP_03 goto $E002 timeouts in the run "
         "ledger; overlay:rom ~85% first-run failure during the B19 saga"),
    Cell("rom", "freeze", 2,
         "Visible-ROM Step Over and Step Out executed by the parked "
         "instruction walk instead of breakpoint+Go, the patch-original and "
         "patch-verify reads that are gated on machine_is_frozen(), and the "
         "staged NMI plus unfreeze/refreeze around every CPU run. Two "
         "repetitions because freeze:rom was measured 1 in 4 intermittent.",
         "monitor_debug_brk_session.cc:2068, :2175, :2422, :637; "
         "monitor_debug_u64.cc:215; scenario-matrix.md freeze:rom 1/4"),
    Cell("ram-under-rom", "telnet", 1,
         "The B16 family: stepping the bank switch and then executing under a "
         "banked-out KERNAL. Carries the 32-level Step Into chain, the 32-call "
         "straight-call run that is the only detector for a leaked breakpoint "
         "slot or a park/resume that drifts the stack, and the 100-instruction "
         "dual-oracle trace with stack readback.",
         "project_b16_two_mode_fetch_incoherence; "
         "run_straight_call_sequence docstring"),
    Cell("ram-under-rom", "freeze", 2,
         "Continue with no breakpoint while KERNAL is banked out, taken "
         "through the Freeze hand-back, plus the frozen patch-original read on "
         "a banked-out aperture. Freeze is the only transport that can defer "
         "the hand-back out of the monitor entirely.",
         "machine_monitor_debug_impl.inc:866 vs :887 vs :910; "
         "commits dd535e9a and 08c6989e"),
    Cell("ram-rom-ram", "overlay", 1,
         "The commonest real debugging shape: a RAM program that calls a ROM "
         "routine, stepped into and stepped back out of, with the banking left "
         "alone throughout. It reaches the visible-ROM leg from a live RAM "
         "context on the first crossing rather than after two bank switches, "
         "which is the difference between it and ram-rur-rom-ram - the leg is "
         "the same, the state it is entered from is not.",
         "_build_ram_rom_ram_fixture traversal D/T/U/D; 5 ledger failures on "
         "this mode crossed with overlay"),
    Cell("ram-rur-rom-ram", "overlay", 2,
         "Twelve boundary crossings in one session - RAM to RAM-under-ROM to "
         "RAM to visible ROM and back, twice - which is the other shape real "
         "debugging takes, and the single worst intersection in the recorded "
         "history.",
         "11 of the 26 ledger failures are overlay x a boundary mode: "
         "goto $C000 timeouts and $C1F1 fixture round-trip mismatches"),
    Cell("ram-rur-rom-ram", "freeze", 2,
         "The same crossings with the freezer owning the machine, so every "
         "crossing also crosses an unfreeze/refreeze pair.",
         "RAM_RUR_ROM_RAM_FREEZE_REP_02 round-trip mismatch at $C1F1"),
)

# A U2+L cartridge in a C64 Ultimate host supports one debugger lane. Visible
# ROM refuses with BRK $E000 IN ROM BLOCKS DEBUG, RAM-under-ROM entry is not
# demonstrated, and the cartridge has no Interface Type setting at all - its
# only UI is the freeze overlay - so on this target `overlay` and `freeze`
# drive the same firmware through the same keys and a second lane would be a
# second copy of the first.
SPLIT_CELLS: tuple[Cell, ...] = (
    Cell("ram", "overlay", 3,
         "The whole supported U2+L debugger surface, run three times in one "
         "session so a repeat-only entry failure has somewhere to appear. The "
         "third repetition is not spare capacity: the matrix fixture seed is a "
         "function of the repetition index, and repetition 3's program is the "
         "one that reproduced the torn debug-footer read, twice, where "
         "repetitions 1 and 2 passed.",
         "doc/machine_code_monitor.md:815; "
         "every recorded u2 matrix ledger holds this single row"),
)

# Semantic coverage that no matrix cell can produce. `monitor_debug_test.py` is
# pinned to Telnet on purpose: its liveness oracles read the C64's own screen
# and jiffy while the machine is meant to be running, which an Overlay or
# Freeze UI holding the machine would invalidate.
U64_GROUPS: tuple[Group, ...] = (
    Group("exit-liveness-reentry",
          "Leaving Debug and closing the monitor with no reset. Every matrix "
          "cell ends in a Reset instead, so this is the only place a natural "
          "exit is exercised at all - and it is where the interrupt-mask leak "
          "that left BASIC with a dead cursor, keyboard and jiffy would come "
          "back.",
          "commit 24aebf99; project_frozen_basic_iflag_resume"),
    Group("debug",
          "The Debug UI contract in 27 cheap checks: which key toggles a "
          "breakpoint, which key falls through to Range mode, the breakpoint "
          "popup chord, the refusal of an 11th breakpoint without evicting the "
          "other ten, the footer layout, and the reset chord.",
          "commits 4e7aedbf and 9dd32e8e both remapped a key and broke the "
          "harness drivers"),
    Group("refusal",
          "What the debugger must refuse and what it must say: Over on BRK, "
          "RTS and RTI with no captured context, Out outside a traced "
          "subroutine reporting NOT IN SUBROUTINE rather than PATCH FAILED, an "
          "undocumented NOP decoded but not stepped, and RTI restoring the "
          "stacked PC and flags.",
          "commit d66c1214 executed undocumented opcodes as their documented "
          "bit-pattern twin"),
    Group("page-cross",
          "The three 6502 shapes a predictor gets wrong: a taken branch across "
          "a page, a not-taken branch across a page, and JMP ($xxFF) following "
          "the real page-wrap target.",
          "commit 7091f16e"),
    Group("rom-breakpoints",
          "Setting, hitting, removing and then stepping a breakpoint in the "
          "BASIC and KERNAL ROM stores, which is the operation that writes into "
          "the volatile FPGA ROM image and has to put it back.",
          "commits e298ab3f, d4434c19, 8ff6a405, 0ed125a8"),
    Group("banked-breakpoints",
          "A KERNAL-store and a RAM-under-KERNAL breakpoint armed at the same "
          "$E000 with the running program banked out, and the cleanup that has "
          "to restore both stores. This is the exact scenario that corrupted "
          "the KERNAL image twice in one day.",
          "commits dd535e9a and 08c6989e; project_e000_read_is_not_rom"),
    Group("repeat-redebug",
          "Cancelling and re-entering Debug repeatedly against a running loop, "
          "in ordinary RAM and with KERNAL banked out. This is the ownership "
          "and reopen-state path, not the stepping path.",
          "commits ab86bd8f, 35f90fe6, fc9826db, 8d7ae9d4"),
    Group("banked-continue-no-breakpoints",
          "Continue with an empty breakpoint table at $01=$00, $35 and $37. "
          "The matrix reaches $35 and $37; CPU0 - all RAM, no I/O - is reached "
          "nowhere else, and this group also proves a Continue with "
          "breakpoints leaves the live backing store intact.",
          "commits 2af4728d and c6773f10: Go with no breakpoint fell to an NMI "
          "trampoline that has no handler when KERNAL is banked out"),
    Group("side-effect-step",
          "Step Over must execute what it steps over: the store lands, the "
          "subroutine's side effect happens, the skipped branch's store does "
          "not. The dual-oracle traces compare CPU state, not every write.",
          "monitor_debug_test.py run_side_effect_step_tests"),
    Group("breakpoint-reentry",
          "Continue issued from the breakpoint the CPU is already stopped on: "
          "it must step off once and re-arm, not trap on itself forever.",
          "commit 8ff6a405"),
    Group("brk-orchestrator",
          "The plain-RAM smoke: load a program, Continue to a breakpoint with "
          "known register values, Step Over a NOP, Step Into the JSR, Step Out, "
          "and prove the cleanup put the user's bytes back under the "
          "breakpoint. Six checks in about half a minute, and the only "
          "end-to-end RAM debugging sequence over Telnet.",
          "monitor_debug_test.py run_brk_orchestrator_tests"),
)

# On a split session the ROM, banking and CPU-bank-view groups have nothing to
# assert - they skip themselves - so running them buys only their per-group
# reset. These are the groups whose checks are reachable on a U2+L. Named
# rather than indexed: an index into the tuple above silently selects the wrong
# group the moment a group is inserted.
_SPLIT_GROUP_NAMES = (
    "exit-liveness-reentry", "debug", "refusal", "page-cross",
    "side-effect-step", "breakpoint-reentry", "repeat-redebug",
    "brk-orchestrator",
)
SPLIT_GROUPS: tuple[Group, ...] = tuple(
    group for name in _SPLIT_GROUP_NAMES
    for group in U64_GROUPS if group.name == name)

# Focused scopes that exist only for a split session, and that no cell reaches:
# the matrix driver skips monitor bank selection on a U2 because the cartridge
# has no CPU bank view.
SPLIT_FOCUS_SCOPES: tuple[tuple[str, str], ...] = (
    ("banking",
     "Five CPU-port states resolved to their visible sources, including the "
     "CPU0/CPU4 pair that expose the same map and so prove CHAREN itself was "
     "captured rather than inferred from the ROM signatures."),
    ("entry-footer",
     "Six CPU-port states x four VIC banks, asserted on the monitor's first "
     "frame. A footer that is only correct after a step is a footer that "
     "reported the debugger's banking instead of the program's."),
)

# What this gate does not cover, so a reader is never left inferring it from
# absence. Each of these is in the full matrix or the full debug suite.
DEFERRED: tuple[Deferred, ...] = (
    # Note what is NOT here: no memory mode is deferred. All five are run.
    # What is deferred is the number of transports a mode is repeated on.
    Deferred("Plain RAM and ram-rom-ram on transports other than Overlay",
             "Both run on Overlay, which is the transport with the worst "
             "recorded history, and the firmware analysis shows RAM stepping "
             "and control-flow steps take the same path on all three. Plain-RAM "
             "debugging over Telnet is additionally covered end to end by the "
             "brk-orchestrator group, and RAM stepping harder still by the "
             "closing random-program run.",
             modes=("ram", "ram-rom-ram")),
    Deferred("ram-under-rom on Overlay, and a third repetition anywhere",
             "The Overlay hand-back path is exercised by rom:overlay and by "
             "both boundary-mode Overlay cells; only the banked-out Continue "
             "would be new, and that leg is inside the shared session go(). "
             "Repetition 3 has produced one recorded failure that repetition 2 "
             "did not."),
    Deferred("The Telnet transport on the boundary modes",
             "No boundary-mode Telnet cell has ever failed, and a Telnet cell "
             "costs three to four times an Overlay or Freeze one."),
    Deferred("10 of the 21 debug semantic groups",
             "kernal-basic-breakpoint, deep-trace, jsr-runcursor-rts, flags, "
             "cleanup-exit, ram-edit, edit-visibility, rom-single-step, "
             "nested-out and step-out-target. The last two run in this gate "
             "anyway, inside the matrix preflight; the rest assert a property "
             "some selected cell asserts against two oracles."),
    Deferred("The opcode-volume run's headroom",
             "It drives 6 random programs rather than 12. The 1000-instruction "
             "requirement is unchanged and still met with margin; what is "
             "given up is the second half of the random program space."),
)

# 6 random programs land roughly 1300 verified instructions against the
# unchanged 1000 requirement, in about half the wall clock of the matrix's 12.
OPCODE_ITERATIONS = 6

# $E000 and $A000 heads. Read after a reset, when the CPU port maps both ROMs.
ROM_FENCE_READS = ((0xE000, 16, "KERNAL"), (0xA000, 16, "BASIC"))


def plan_cells(split: bool) -> tuple[Cell, ...]:
    return SPLIT_CELLS if split else U64_CELLS


def plan_groups(split: bool) -> tuple[Group, ...]:
    return SPLIT_GROUPS if split else U64_GROUPS


def cells_argument(cells: Sequence[Cell]) -> str:
    return ",".join(cell.term() for cell in cells)


def groups_argument(groups: Sequence[Group]) -> str:
    return ",".join(group.name for group in groups)


def plan_lines(split: bool) -> list[str]:
    """The plan as text, for --list-plan and for the artifact directory."""
    target = "u2@c64u (split session)" if split else "u64"
    lines = [f"machine-code-monitor-regression plan for {target}", ""]
    lines.append("Matrix cells (memory:ui:repetitions):")
    for cell in plan_cells(split):
        lines.append(f"  {cell.term()}")
        lines.append(f"      why      {cell.why}")
        lines.append(f"      evidence {cell.evidence}")
    lines.append("")
    lines.append("Debugger semantic groups (Telnet):")
    for group in plan_groups(split):
        lines.append(f"  {group.name}")
        lines.append(f"      why      {group.why}")
        lines.append(f"      evidence {group.evidence}")
    if split:
        lines.append("")
        lines.append("Split-session focused scopes:")
        for name, why in SPLIT_FOCUS_SCOPES:
            lines.append(f"  --focus {name}")
            lines.append(f"      why      {why}")
    lines.append("")
    lines.append(f"Opcode-volume run: {OPCODE_ITERATIONS} random programs, "
                 f"1000-instruction requirement unchanged")
    lines.append("")
    lines.append("Deferred to machine-code-monitor-matrix and "
                 "machine-code-monitor-debug:")
    for item in DEFERRED:
        lines.append(f"  {item.what}")
        lines.append(f"      {item.why}")
    if split:
        lines.append("")
        lines.append("Not deferred but unsupported on this target:")
        lines.append("  Visible ROM and RAM-under-ROM debugging, and the "
                     "second local UI")
        lines.append("      A U2+L refuses a visible-ROM breakpoint with BRK "
                     "$E000 IN ROM BLOCKS DEBUG, has no demonstrated "
                     "RAM-under-ROM entry, and has no Interface Type setting, "
                     "so Overlay and Freeze are one UI on this target rather "
                     "than two.")
    return lines


# ---------------------------------------------------------------------------
# The one check this gate owns.
# ---------------------------------------------------------------------------

class RomImageFence:
    """Prove the run gave the U64 its ROM images back.

    A breakpoint armed against the BASIC or KERNAL store writes a BRK into the
    volatile FPGA ROM image and records the byte it displaced so removal can
    put it back. Recording the wrong byte leaves the wrong byte in the ROM, and
    that survives a machine reset: only a firmware restart reloads the images.
    It was fixed twice in one day (dd535e9a, then 08c6989e).

    Nothing in either existing suite asserts this. The matrix's `rom` cell
    checks the KERNAL head as a *precondition* and blocks when it is wrong, so
    it names the run that inherited the damage rather than the run that did it,
    which is how the same corruption was first found half an hour and one suite
    later. This reads the heads before the gate touches the debugger and again
    after it has finished, so the damage is attributed to the run that caused
    it.

    Compared against this run's own snapshot rather than a constant: the bench
    device runs JiffyDOS, so a hard-coded stock KERNAL head would fail on a
    healthy machine.
    """

    def __init__(self, margs: argparse.Namespace, split: bool) -> None:
        self.margs = margs
        self.split = split
        self.baseline: dict[int, bytes] = {}
        self.skip_reason = ""
        if split:
            # A U2+L has no writable ROM image; its patch-original read is the
            # base implementation, so there is no image to corrupt or fence.
            self.skip_reason = "U2+L has no writable ROM image to displace"

    def _read_heads(self) -> dict[int, bytes]:
        rest = matrix.make_rest(self.margs, timeout=12.0)
        L.ensure_menu_closed(rest)
        rest.reset()
        # A reset puts the CPU port back to $2F/$37, which is the only state in
        # which readmem serves the ROMs rather than the RAM beneath them.
        deadline = time.time() + 25.0
        while time.time() < deadline and not rest.alive():
            time.sleep(0.5)
        time.sleep(2.0)
        return {address: rest.read_mem(address, length)
                for address, length, _name in ROM_FENCE_READS}

    def capture(self) -> None:
        R.check_start("ROM image fence: record the KERNAL and BASIC heads")
        if self.skip_reason:
            R.check_skip(self.skip_reason)
            return
        try:
            self.baseline = self._read_heads()
        except Exception as exc:  # noqa: BLE001 - report, do not raise
            self.skip_reason = f"could not read the ROM heads: {exc}"
            R.check_skip(self.skip_reason)
            return
        blank = [name for address, _length, name in ROM_FENCE_READS
                 if len(set(self.baseline[address])) <= 1]
        if blank:
            # An all-identical head is RAM or an unmapped aperture, not a ROM.
            # Fencing against it would compare two meaningless reads and pass.
            self.skip_reason = (f"{', '.join(blank)} head is not a ROM image "
                                f"on this device")
            self.baseline = {}
            R.check_skip(self.skip_reason)
            return
        R.check_ok(", ".join(
            f"{name} {self.baseline[address][:4].hex().upper()}"
            for address, _length, name in ROM_FENCE_READS))

    def verify(self) -> bool:
        R.check_start("ROM image fence: the ROM images are as the run found them")
        if not self.baseline:
            R.check_skip(self.skip_reason or "no baseline was recorded")
            return True
        try:
            now = self._read_heads()
        except Exception as exc:  # noqa: BLE001
            R.check_fail(f"could not re-read the ROM heads: {exc}")
            return False
        damaged = []
        for address, _length, name in ROM_FENCE_READS:
            if now[address] != self.baseline[address]:
                damaged.append(
                    f"{name} ${address:04X}: was "
                    f"{self.baseline[address].hex().upper()}, now "
                    f"{now[address].hex().upper()}")
        if damaged:
            R.check_fail("; ".join(damaged))
            R.detail("A displaced ROM byte survives a machine reset. Restart "
                     "the firmware to reload the images before the next run.")
            return False
        R.check_ok()
        return True


# ---------------------------------------------------------------------------
# Running the parts.
# ---------------------------------------------------------------------------

def matrix_argv(args: argparse.Namespace, cells: Sequence[Cell],
                artifact_dir: Path) -> list[str]:
    argv = [
        "--host", args.host,
        "--rest-host", args.rest_host,
        "--port", str(args.port),
        "--timeout", str(args.timeout),
        "--cells", cells_argument(cells),
        "--reps", "1",
        "--opcode-iterations", str(OPCODE_ITERATIONS),
        "--artifact-dir", str(artifact_dir),
        # Every selected lane carries risk no other lane carries, so a gate
        # that stopped at the first failure would report one lane and say
        # nothing about the rest. The matrix still stops on a hard C64 wedge,
        # where continuing would fail every later lane on a dead machine.
        "--continue-after-cell-failure",
    ]
    if args.password:
        argv += ["--password", args.password]
    if args.c64_host:
        argv += ["--c64-host", args.c64_host]
    if args.no_run_ledger:
        argv.append("--no-run-ledger")
    elif args.run_ledger:
        argv += ["--run-ledger", args.run_ledger]
    return argv


def focus_argv(args: argparse.Namespace, focus: str,
               artifact_dir: Path) -> list[str]:
    argv = [
        "--host", args.host,
        "--rest-host", args.rest_host,
        "--port", str(args.port),
        "--timeout", str(args.timeout),
        "--focus", focus,
        "--artifact-dir", str(artifact_dir),
        "--no-run-ledger",
    ]
    if args.password:
        argv += ["--password", args.password]
    if args.c64_host:
        argv += ["--c64-host", args.c64_host]
    return argv


def run_matrix_cells(args: argparse.Namespace, cells: Sequence[Cell],
                     artifact_dir: Path) -> bool:
    """Drive the selected cells through the matrix runner and report each one.

    The matrix prints its own per-cell progress as it goes; the observer turns
    each finished cell into one report line with its measured wall time, so the
    gate reads like every other suite without either output swallowing the
    other.
    """
    reason_for = {(cell.memory, cell.ui): cell.why for cell in cells}

    def observe(row: dict[str, Any], seconds: float) -> None:
        label = (f"{row['memory_mode']} x {row['interface']} "
                 f"rep {row['repetition']}")
        R.check_start(f"Cell {label}")
        status = row.get("status")
        if status == "PASS":
            R.check_ok(f"{row.get('opcode_count', 0)} opcodes",
                       elapsed=seconds)
            return
        failure = row.get("failure") or {}
        message = next(iter((failure.get("message") or "").splitlines()), "")
        if status == "BLOCKED_WITH_EVIDENCE":
            R.check_warn(f"BLOCKED: {message}", elapsed=seconds)
        else:
            R.check_fail(message or "cell did not reach PASS", elapsed=seconds)
        R.detail(f"why this lane is in the gate: "
                 f"{reason_for.get((row['memory_mode'], row['interface']), '')}")
        R.detail(f"evidence: {row.get('artifact_dir')}")

    argv = matrix_argv(args, cells, artifact_dir)
    matrix.CELL_OBSERVER = observe
    try:
        code = matrix.main(argv)
    except BaseException as exc:  # noqa: BLE001 - record, then report
        matrix._record_crashed_run(exc)
        raise
    finally:
        matrix.CELL_OBSERVER = None

    R.check_start(f"Opcode volume: {OPCODE_ITERATIONS} random programs against "
                  f"the 6510 oracle")
    summary = artifact_dir / "opcode-1000-summary.json"
    if not summary.exists():
        R.check_skip("the run stopped before the opcode volume gate")
    else:
        data = json.loads(summary.read_text(encoding="utf-8"))
        count = data.get("opcode_count", 0)
        if data.get("opcode_requirement_status") == "PASS":
            R.check_ok(f"{count} instructions, requirement 1000")
        else:
            R.check_fail(f"{count} instructions against a requirement of 1000 "
                         f"({data.get('skip_reason') or 'gate failed'})")
    return code == 0


_SUMMARY_LINE = re.compile(r"^\s*(passed|skipped|failed)\s*:\s*(\d+)\s*$")
_FAILED_CHECK = re.compile(r"^\s*\[(\d+)\]\s+(\S.*)$")


def run_debug_groups(args: argparse.Namespace, groups: Sequence[Group],
                     log_path: Path) -> bool:
    """Run the selected `monitor_debug_test.py` groups as one child process.

    One process rather than one per group: the suite already resets the machine
    and opens a fresh session between groups, and re-invoking it per group would
    repeat its closing hygiene - restoring the KERNAL it wrote over, proving no
    slot is left armed - once per group instead of once.

    The child's output is streamed rather than captured, because these groups
    take a quarter of an hour and an operator watching a silent process cannot
    tell a slow check from a hung one. Its own JSONL is suppressed so this run
    records one suite rather than two.
    """
    command = [
        sys.executable, str(MCM_DIR / "monitor_debug_test.py"),
        "--host", args.host,
        "--rest-host", args.rest_host,
        "--port", str(args.port),
        "--timeout", str(args.timeout),
        "--test", groups_argument(groups),
    ]
    if args.password:
        command += ["--password", args.password]
    if args.c64_host:
        # run-tests never passes --target, and the suite derives it from a
        # composite host token this gate has already split apart, so a split
        # session has to be named explicitly or every U64-only check runs.
        command += ["--target", "u2"]

    env = dict(os.environ)
    env["E2E_JSONL"] = ""

    counts: dict[str, int] = {}
    failed_labels: list[str] = []
    in_failures = False
    R.section("Debugger semantics over Telnet")
    started = time.monotonic()
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(command, cwd=str(REPO_ROOT), env=env,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True,
                                   bufsize=1)
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(line)
            log.write(line)
            stripped = line.rstrip()
            match = _SUMMARY_LINE.match(stripped)
            if match:
                counts[match.group(1)] = int(match.group(2))
                continue
            if stripped.startswith("Failed checks:"):
                in_failures = True
                continue
            if in_failures:
                found = _FAILED_CHECK.match(stripped)
                if found:
                    failed_labels.append(found.group(2))
                elif stripped:
                    in_failures = False
        code = process.wait()
    sys.stdout.flush()

    elapsed = time.monotonic() - started
    R.check_start(f"Groups {groups_argument(groups)}")
    extra = ", ".join(f"{name} {counts[name]}"
                      for name in ("passed", "skipped", "failed")
                      if name in counts)
    if code == 0:
        R.check_ok(extra or "no summary reported", elapsed=elapsed)
        return True
    R.check_fail(extra or f"exit status {code}", elapsed=elapsed)
    for label in failed_labels:
        R.detail(f"failed: {label}")
    R.detail(f"full output: {log_path}")
    return False


def run_focus_scopes(args: argparse.Namespace, artifact_dir: Path) -> bool:
    ok = True
    R.section("Split-session focused scopes")
    for focus, why in SPLIT_FOCUS_SCOPES:
        R.check_start(f"Focus {focus}")
        started = time.monotonic()
        try:
            code = matrix.main(focus_argv(args, focus, artifact_dir / focus))
        except Exception as exc:  # noqa: BLE001 - a scope must not abort the gate
            R.check_fail(f"{type(exc).__name__}: {exc}",
                         elapsed=time.monotonic() - started)
            R.detail(f"why this scope is in the gate: {why}")
            ok = False
            continue
        if code == 0:
            R.check_ok(elapsed=time.monotonic() - started)
        else:
            R.check_fail(f"exit status {code}",
                         elapsed=time.monotonic() - started)
            R.detail(f"why this scope is in the gate: {why}")
            ok = False
    return ok


# ---------------------------------------------------------------------------

def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the Machine Code Monitor debugger regression gate")
    parser.add_argument("--host", required=True,
                        help="The device's Telnet host. On a split session "
                             "this is the cartridge, which serves the remote "
                             "monitor.")
    parser.add_argument("--rest-host", required=True,
                        help="The device's REST host. On a split session this "
                             "is the cartridge, which serves menu_screen and "
                             "menu_button.")
    parser.add_argument("--c64-host", default=None,
                        help="Split-session mode for a U2+L cartridge: the "
                             "C64 Ultimate host it is plugged into, which "
                             "serves the machine operations. Empty or omitted "
                             "means a single-host target.")
    parser.add_argument("--port", type=int, default=23)
    parser.add_argument("--password")
    parser.add_argument("--timeout", type=float, default=30.0,
                        help="Per-request budget, and the Telnet "
                             "quiet-window wait the matrix preflight uses. The "
                             "5s default the sibling suites take is shorter "
                             "than the redraw a Step Out sends, so it is not "
                             "the default here.")
    parser.add_argument("--artifact-dir", default=None,
                        help="Where the cell evidence, the child suite log and "
                             "the plan are written. Defaults to a timestamped "
                             "directory under the system temp dir.")
    parser.add_argument("--run-ledger", default="",
                        help="Directory holding the matrix cross-run history. "
                             "Defaults to the matrix runner's own default.")
    parser.add_argument("--no-run-ledger", action="store_true",
                        help="Do not record the cell run in the cross-run "
                             "history.")
    parser.add_argument("--skip-cells", action="store_true",
                        help="Skip the matrix cells and run only the debugger "
                             "semantic groups.")
    parser.add_argument("--skip-groups", action="store_true",
                        help="Skip the debugger semantic groups and run only "
                             "the matrix cells.")
    parser.add_argument("--list-plan", action="store_true",
                        help="Print the lanes this gate would run, with the "
                             "reason and the evidence for each, and exit "
                             "without touching a device.")
    R.add_colour_argument(parser)
    args = parser.parse_args(argv)
    R.apply_colour(args.color)
    if args.password:
        R.mask_secret(args.password)
    # run-tests templates --c64-host unconditionally and substitutes an empty
    # string on a single-host target, which must read as "not split".
    if not args.c64_host:
        args.c64_host = None
    if args.skip_cells and args.skip_groups:
        parser.error("--skip-cells and --skip-groups leave nothing to run")
    return args


def main(argv: Optional[list[str]] = None) -> int:
    args = parse_args(argv)
    split = bool(args.c64_host)

    if args.list_plan:
        print("\n".join(plan_lines(split)))
        return 0

    artifact_dir = Path(args.artifact_dir or (
        Path(tempfile.gettempdir())
        / f"mcm-regression-{time.strftime('%Y%m%d-%H%M%S')}"))
    artifact_dir.mkdir(parents=True, exist_ok=True)
    (artifact_dir / "plan.txt").write_text(
        "\n".join(plan_lines(split)) + "\n", encoding="utf-8")

    cells = plan_cells(split)
    groups = plan_groups(split)
    print(f"artifact dir: {artifact_dir}", flush=True)
    print(f"plan: {len(cells)} cell definitions "
          f"({sum(cell.reps for cell in cells)} runs), "
          f"{len(groups)} semantic groups"
          f"{', 2 focused scopes' if split else ''}", flush=True)

    # Built here rather than inside the runner so the fence and the run share
    # one description of the target, and so a malformed selection is a usage
    # error before the device is touched.
    margs = matrix.parse_args(matrix_argv(args, cells, artifact_dir / "cells"))
    fence = RomImageFence(margs, split)

    ok = True
    R.section("ROM image integrity")
    fence.capture()

    try:
        if not args.skip_cells:
            R.section("Selected debugger lanes")
            ok = run_matrix_cells(args, cells, artifact_dir / "cells") and ok
        if not args.skip_groups:
            ok = run_debug_groups(args, groups,
                                  artifact_dir / "debug-groups.log") and ok
        if split and not args.skip_cells:
            ok = run_focus_scopes(args, artifact_dir / "focus") and ok
    finally:
        # The matrix runner performs this from its own __main__ block, which
        # this caller bypasses. Without it the device is left holding input or
        # with a menu open, and the next suite reports a dead machine. It is a
        # no-op when no matrix scope claimed the device.
        matrix._run_final_teardown()

    R.section("ROM image integrity")
    ok = fence.verify() and ok

    if ok:
        R.suite_ok(SUITE, f"{R.check_count()} checks")
        return 0
    R.suite_fail(SUITE, "a selected debugger lane did not pass")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
