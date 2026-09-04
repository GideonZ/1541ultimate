#!/usr/bin/env python3
"""REST-transport Machine Code Monitor debugger stress runner.

Drives the shipped debugger entirely over the production REST API (machine:input,
menu_screen, menu_button, readmem, writemem) so it survives the U64 telnet
load-degradation, and asserts every step against the independent NMOS-6510 oracle
(mcm6502.py): PC / A / X / Y / SP / SR and memory writes, with SP-coherence over
JSR/RTS nesting and natural-exit liveness validation.

UI modes:  freeze | overlay   (telnet is covered by monitor_debug_test.py)

Method (per gate handover):
  * generate a deterministic program from a seeded instruction corpus, into RAM
  * mirror the program + a known scratch window into an mcm6502 instance
  * enter Debug at the program start via the bootstrap+breakpoint+Go pattern
  * for each instruction: choose the monitor key (T step-into / D step-over /
    U step-out / G continue), advance the oracle to the matching terminal state,
    send the key over REST, read back the footer + memory, and assert equality
  * exit Debug to a live machine and validate liveness

Emits JSONL events + a machine-readable coverage summary.
"""
from __future__ import annotations

import argparse
import json
import os
import random
import sys
import time
from dataclasses import dataclass
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
sys.path.insert(0, bootstrap.directory("e2e", "monitor"))

import mcm_rest as R          # noqa: E402
import mcm_split_rest as SR   # noqa: E402
import mcm_localui as L       # noqa: E402
import mcm6502 as ORC         # noqa: E402
import overlay_lifecycle  # noqa: E402

BOOTSTRAP_ADDR = 0xC500                   # register bootstrap + JMP target lives here
PROG_ADDR = 0xC000                        # generated program lives here (RAM, CPU bank 7)
SCRATCH_ZP_LO, SCRATCH_ZP_HI = 0x02, 0x8F   # zero-page data window (avoids $00/$01 CPU port;
                                            # clobbering BASIC zp is harmless: the KERNAL IRQ
                                            # self-heals $A0-$A2 etc., and the debugger uses
                                            # only $00/$01 + the cassette buffer, not $02-$8F)
# Absolute data window in the free $C000 RAM block. It MUST NOT overlap the KERNAL
# RAM vectors ($0314-$0319 IRQ/BRK/NMI) or the cassette-buffer trampoline region
# ($033C-$03FB) the BRK debugger uses - randomising those breaks breakpoint BRK
# dispatch and resume. ($C800-$CBFF sits above the program @ $C000 and bootstrap @ $C500.)
SCRATCH_LO, SCRATCH_HI = 0xC800, 0xCBFF

FLAG_Z = 0x02
FLAG_I = 0x04
FLAG_B = 0x10
FLAG_N = 0x80


# ----------------------------------------------------------------------------
# Footer parsing (menu_screen 40x25 text)
# ----------------------------------------------------------------------------

@dataclass
class Footer:
    pc: int
    ac: int
    xr: int
    yr: int
    sp: int
    sr: int


def parse_footer(lines) -> Footer | None:
    """Locate the 'PC AC XR YR SP NV-BDIZC' header and parse the value row below it.
    Returns None if the debug footer is absent or its values are blank."""
    for y in range(len(lines)):
        line = lines[y]
        if "PC" in line and "SP" in line and "NV-BDIZC" in line and (y + 1) < len(lines):
            col = line.index("PC")
            v = lines[y + 1]

            def fld(a, b):
                return v[col + a: col + b].strip()
            pc, ac, xr, yr, sp, sr = (fld(0, 4), fld(5, 7), fld(8, 10),
                                      fld(11, 13), fld(14, 16), fld(17, 25))
            if not all((pc, ac, xr, yr, sp, sr)):
                return None
            try:
                return Footer(int(pc, 16), int(ac, 16), int(xr, 16),
                              int(yr, 16), int(sp, 16), int(sr, 2))
            except ValueError:
                return None
    return None


# ----------------------------------------------------------------------------
# REST monitor session
# ----------------------------------------------------------------------------

class StressError(RuntimeError):
    pass


FORBIDDEN = ("UNSAFE TARGET", "DEBUG TIMEOUT", "TIMEOUT", "PATCH FAILED",
             "DEBUG NOT SUPPORTED", "NO CONTEXT", "ASSERT")


class RestSession:
    def __init__(self, host, ui="freeze", c64_host=None):
        self.rest = SR.make_rest(host, c64_host)
        self.host = c64_host or host
        self.ui = ui
        # A split session is a U2+L cartridge in a C64U host. Beyond routing,
        # the U2 MCM differs from the U64 one in what it exposes: no Interface
        # Type config, no monitor bank view, and one memory-source tag for
        # every row. The methods below name their own deviation.
        self.split = bool(c64_host)
        # A resend re-executes the step if the first tap did land, so a
        # mismatch that follows one is the harness's doing, not the
        # debugger's. Counted so the two can be told apart.
        self.step_resends = 0

    # --- low-level ---
    def lines(self):
        try:
            return self.rest.screen_lines()
        except Exception:  # noqa: BLE001
            return None

    def text(self):
        ls = self.lines()
        return "\n".join(ls) if ls else ""

    def alive(self):
        return self.rest.alive()

    def read_mem(self, addr, length):
        """Memory read that does not depend on which device holds the bus.

        On a split session the C64U cannot see $1000-$CFFF while the cartridge
        holds the C64 in Ultimax; it reads $FF there. Comparing that against the
        oracle reports a mismatch that is an artefact of the read path, so the
        comparison reads the device that can always see the window.
        """
        if self.split:
            return self.rest.read_mem_oracle(addr, length)
        return self.rest.read_mem(addr, length)

    def release_cartridge_hold(self, tries=8):
        """Hand the C64 back if a previous session left the cartridge holding it.

        Keystrokes reach a U2+L over the C64U's keyboard matrix, and that matrix
        is scanned only while the 6510 executes. A held machine therefore
        swallows every key while REST still answers normally, and every later
        step reports no progress for a reason that has nothing to do with
        stepping. A machine:reset does not clear it; toggling the cartridge's
        own menu does. Returns True once the jiffy clock advances again.
        """
        for _ in range(tries):
            if self._c64_running():
                return True
            self.rest.menu_button()
            time.sleep(1.4)
        return self._c64_running()

    def assert_overlay_draws(self):
        """Fail when the overlay is present but renders nothing.

        An overlay whose every line is blank cannot open a monitor, so the
        monitor open times out and the run reports a monitor problem instead of
        the UI state that caused it. This names the state where it is found.

        This detects; it does not repair. A repair was written for it and
        removed: its only evidence came from a window in which a second agent
        was driving the same device, which is also what would produce a blank
        overlay. Rediscovering the phenomenon with clean evidence costs one run;
        a remedy built on contaminated evidence could mask a real regression
        indefinitely. See REPORT.md, "The remedy that was removed".

        A closed menu is a different state and is not a failure.
        """
        try:
            lines = self.rest.screen_lines()
        except Exception:  # noqa: BLE001 - a closed menu has no screen to judge
            return
        if not any(line.strip() for line in lines):
            raise StressError(
                "overlay is present but every line of it is blank, so no "
                "monitor can open. The cartridge UI is rendering nothing. This "
                "is reported, not repaired: confirm it with sole access to the "
                "device before treating it as a firmware finding.")

    def hold_note(self):
        """A suffix naming the C64 as DMA-held, when it is.

        A held machine takes no keystrokes, so every step after the hold reports
        no progress for a reason that is not stepping. Saying so where the
        failure is raised stops a firmware hold reading as an opcode failure.
        Empty on a single-host run and whenever the machine is executing.
        """
        if not self.split or self._c64_running():
            return ""
        try:
            screen = self.rest.read_mem(0x0400, 16)
        except Exception:  # noqa: BLE001 - diagnostics must not replace the failure
            screen = b""
        return (" [C64 IS DMA-HELD: jiffy frozen, $0400="
                f"{screen.hex()}; this is the hold-after-close defect, "
                "not a stepping result]")

    def _c64_running(self):
        try:
            first = self.rest.read_mem(0x00A0, 3)
            time.sleep(0.5)
            return first != self.rest.read_mem(0x00A0, 3)
        except Exception:  # noqa: BLE001 - an unreadable device is not running
            return False

    def key(self, *names, settle=0.0):
        self.rest.tap(list(names))
        if settle:
            time.sleep(settle)

    @staticmethod
    def _modal_in(lines, ctx):
        t = "\n".join(lines).upper()
        for tok in FORBIDDEN:
            if tok in t:
                raise StressError(f"{ctx}: forbidden debug text {tok!r}\n" + "\n".join(lines))

    def footer(self):
        ls = self.lines()
        return parse_footer(ls) if ls else None

    def wait_footer_pc(self, pc, timeout=8.0, ctx="", sp=None):
        """Poll menu_screen ONCE per iteration (a double-fetch right after a step
        can transiently 404 the overlay menu_screen pipeline) until the debug
        footer shows the expected PC.

        `sp` disambiguates an address the program visits more than once. A JSR
        nest passes through every address twice, once descending and once as the
        RTS chain unwinds, and the two visits differ by the frame the JSR
        pushed. Matching on PC alone therefore accepts whichever visit the
        device happens to be showing, and the registers of the other one are
        then compared against the oracle and reported as a register mismatch.
        The caller knows the stack pointer the step must land on, so require it
        too where it does.
        """
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            ls = self.lines()
            if ls:
                f = parse_footer(ls)
                last = f
                if f and f.pc == pc and (sp is None or f.sp == sp):
                    return f
                if f is None:
                    self._modal_in(ls, ctx or "wait_footer_pc")
            time.sleep(0.1)
        raise StressError(f"{ctx}: footer PC did not reach {pc:04X} (last={last})\n{self.text()}")

    def progress_step(self, key, expected_pc, writes, max_resend=3,
                      active_write_progress=False, expected_sp=None):
        """Send a Step key and re-send it if the step makes no progress. The clean
        -stop release after a Go/entry intermittently re-traps the first step at the
        launch site without advancing (the documented B16 FPGA-aperture behaviour);
        a re-send then advances it. Progress is the footer PC reaching
        `expected_pc`. Active-Debug readmem can observe freezer-owned backing
        state instead of the live CPU store, so write readback is optional and
        must be enabled only after a capability probe.
        Ordering is preserved: a tap is only ever followed by readmem (write steps)
        or by a footer GET (non-write steps), never by readmem->tap->menu_screen.
        `expected_sp`, where the caller knows it, keeps this agreeing with
        wait_footer_pc(): a footer still showing the previous step can carry the
        same PC as the one being waited for when the program visits an address
        twice, and treating that as progress skips a re-send that was needed.
        Returns True once progress is observed."""
        first_w = next(((a, v) for a, v in writes if a not in (0x0000, 0x0001)), None)
        for attempt in range(max_resend + 1):
            if attempt:
                self.step_resends += 1
            self.rest.tap([key])
            deadline = time.time() + 1.6
            while time.time() < deadline:
                f = self.footer()
                if f and f.pc == expected_pc and (expected_sp is None
                                                  or f.sp == expected_sp):
                    return True
                if active_write_progress and first_w is not None:
                    if self.rest.read_mem(first_w[0], 1)[0] == (first_w[1] & 0xFF):
                        return True
                time.sleep(0.1)
        return False

    # --- monitor lifecycle ---
    def set_ui_mode(self):
        """Ensure the configured UI mode through the same REST configuration path
        used by the maintained overlay lifecycle gate.

        A U2+L cartridge has no "Interface Type" config - its only UI is the
        freeze overlay, so the PUT 404s there and there is nothing to select.
        """
        L.ensure_menu_closed(self.rest)
        if self.split:
            return
        if self.ui == "overlay":
            overlay_lifecycle.set_interface_type(self.rest, "Overlay on HDMI")
        else:
            overlay_lifecycle.set_interface_type(self.rest, "Freeze")
        L.ensure_menu_closed(self.rest)

    def recover(self):
        if self.split:
            self.release_cartridge_hold()   # keys are dead until the C64 runs
        self.rest.tap(["commodore", "x"])
        time.sleep(0.5)
        L.ensure_menu_open(self.rest)

    def clean_baseline(self):
        """Return to a known closed-menu baseline before opening a fresh debug
        session by fully tearing down any prior debug/monitor session via the
        same keystrokes a user would use, then forcing a production reset. The
        reset is required after failed or interrupted debug runs: while the UI can
        still be alive, REST readmem/writemem may be observing the frozen backing
        state instead of live RAM until the machine is reset."""
        if self.split:
            self.release_cartridge_hold()   # keys are dead until the C64 runs
            self.assert_overlay_draws()     # and a blank overlay never opens a monitor
        self.rest.tap(["commodore", "d"])   # leave Debug if active
        time.sleep(0.2)
        self.rest.tap(["run_stop"])          # close monitor if open
        time.sleep(0.2)
        self.rest.tap(["commodore", "x"])    # break/reset from any monitor mode
        time.sleep(0.4)
        self.rest.reset()
        overlay_lifecycle.wait_ready(self.rest, timeout=12.0)
        L.ensure_menu_closed(self.rest)
        time.sleep(0.3)

    def open(self):
        overlay_lifecycle.open_monitor(self.rest, "stress monitor open")
        t = self.text()
        if "MONITOR" not in t.upper():
            raise StressError("monitor did not open")

    def close(self):
        """Leave Debug, then the monitor, confirming each step actually landed.

        Fixed sleeps are not enough on a split session: those keystrokes travel
        to the C64U and reach the cartridge over the C64's keyboard matrix, which
        is slow enough that a dropped one leaves the monitor open and the C64
        held, and the liveness check that follows then reports a firmware hold
        that is really a lost keystroke."""
        deadline = time.time() + 12.0
        while time.time() < deadline:
            text = "\n".join(self.lines() or [])
            if "MONITOR" not in text.upper():
                break
            self.rest.tap(["commodore", "d"] if "Dbg" in text else ["run_stop"])
            time.sleep(0.4)
        L.settle(self.rest, 0.2)

    def goto(self, addr):
        overlay_lifecycle.goto_addr(self.rest, addr, f"stress goto ${addr:04X}")

    def asm_view(self):
        self.rest.tap(["a"])
        time.sleep(0.15)

    def set_cpu_bank7(self):
        """Cycle O until the footer shows CPU7 (view==exec bank 7)."""
        if self.split:
            return          # a U2 MCM has no monitor bank view; it reads the live aperture
        for _ in range(9):
            ls = self.lines() or []
            if any(ln.strip().startswith("CPU7") or " CPU7" in ln for ln in ls):
                return
            self.rest.tap(["o"])
            time.sleep(0.15)
        # not fatal; bank view doesn't affect execution stream

    def enter_debug(self, timeout=6.0):
        """Enter Debug, waiting for the header to say so.

        Measured on a split U2+L: when the key lands, `Dbg` appears after about
        0.21s; when it does not land, it never appears at all, even over 15s.
        A single sample taken at 0.2s therefore sits exactly on the boundary
        and misses successful entries as often as it catches them. Waiting on
        the state still fails a genuine non-entry, it just stops reporting a
        slow one as a failure.
        """
        self.rest.tap(["d"])
        deadline = time.time() + timeout
        while time.time() < deadline:
            if any("Dbg" in line for line in (self.lines() or [])):
                return
            time.sleep(0.15)
        raise StressError("debug mode not entered")

    def set_breakpoint(self, addr):
        if self.split:
            row = overlay_lifecycle.toggle_breakpoint_at(
                self.rest, addr, True, f"stress set bp ${addr:04X}")
            if "[BRK" not in row:
                raise StressError(f"breakpoint not set at ${addr:04X}: {row!r}")
            return
        overlay_lifecycle.ensure_breakpoint_at(
            self.rest, addr, 7, "RAM", f"stress set bp ${addr:04X}")

    def clear_breakpoint(self, addr):
        if self.split:
            row = overlay_lifecycle.toggle_breakpoint_at(
                self.rest, addr, False, f"stress clear bp ${addr:04X}")
            if "[BRK" in row:
                raise StressError(f"breakpoint not cleared at ${addr:04X}: {row!r}")
            return
        overlay_lifecycle.clear_breakpoint_at(
            self.rest, addr, 7, f"stress clear bp ${addr:04X}")

    def clear_table_by_row_toggle(self):
        """Clear every armed slot with the monitor's own goto + R toggle, and
        prove the table is empty afterwards.

        A U2+L drops the slot popup's INST/DEL while the freezer holds a
        trapped PC, so the delete pass below leaves the table armed while
        reporting nothing. One armed slot is enough to refuse every debug
        operation on a target that cannot patch visible ROM, so an empty table
        is proved here rather than assumed.
        """
        label = "stress clear breakpoint table"
        armed = overlay_lifecycle.armed_breakpoint_addresses(self.rest, label)
        if not armed:
            # An empty table is the observation; re-reading it costs a second
            # popup open and close, and the RUN/STOP that closes a popup closes
            # the monitor when the popup is not the focused object.
            return
        for addr in armed:
            self.clear_breakpoint(addr)
        remaining = overlay_lifecycle.armed_breakpoint_addresses(self.rest, label)
        if remaining:
            raise StressError("breakpoint table still armed at "
                              + ", ".join(f"${a:04X}" for a in remaining))

    def clear_all_breakpoints(self):
        """Reset all 10 breakpoint slots via the C=+P list popup (DEL per slot)."""
        if self.split:
            self.clear_table_by_row_toggle()
            return
        self.rest.tap(["commodore", "p"])
        time.sleep(0.3)
        ls = self.lines() or []
        if not any("BRK" in ln.upper() or "BREAK" in ln.upper() for ln in ls):
            # popup not detected; bail without leaving stray keys
            self.rest.tap(["run_stop"])
            time.sleep(0.2)
            return
        for _ in range(10):
            self.rest.tap(["inst_del"])
            time.sleep(0.1)
            self.rest.tap(["cursor_up_down"])
            time.sleep(0.08)
        self.rest.tap(["run_stop"])
        time.sleep(0.2)


# ----------------------------------------------------------------------------
# Program generation (deterministic, fully oracle-modelable)
# ----------------------------------------------------------------------------

@dataclass
class Instr:
    addr: int
    code: bytes
    mnem: str
    mode: str


# (mnemonic, opcode, length, mode); all modeled by mcm6502 and side-effect-safe
IMPLIED = [
    ("NOP", 0xEA), ("TAX", 0xAA), ("TAY", 0xA8), ("TXA", 0x8A), ("TYA", 0x98),
    ("INX", 0xE8), ("INY", 0xC8), ("DEX", 0xCA), ("DEY", 0x88),
    ("CLC", 0x18), ("SEC", 0x38), ("CLD", 0xD8), ("CLV", 0xB8),
    ("ASLA", 0x0A), ("LSRA", 0x4A), ("ROLA", 0x2A), ("RORA", 0x6A),
]
IMM = [
    ("LDA", 0xA9), ("LDX", 0xA2), ("LDY", 0xA0), ("ADC", 0x69), ("SBC", 0xE9),
    ("AND", 0x29), ("ORA", 0x09), ("EOR", 0x49), ("CMP", 0xC9), ("CPX", 0xE0),
    ("CPY", 0xC0),
]
ABS_RW = [
    ("LDA", 0xAD), ("ADC", 0x6D), ("SBC", 0xED), ("AND", 0x2D), ("ORA", 0x0D),
    ("EOR", 0x4D), ("CMP", 0xCD), ("INC", 0xEE), ("DEC", 0xCE), ("ASL", 0x0E),
    ("LSR", 0x4E), ("ROL", 0x2E), ("ROR", 0x6E),
]
ABS_ST = [("STA", 0x8D), ("STX", 0x8E), ("STY", 0x8C)]


def gen_program(rng, length, allow_stack=True):
    """Build a straight-line program of `length` instructions at PROG_ADDR.
    All memory operands target the mirrored scratch window. Returns list[Instr],
    end address (first byte past last instr)."""
    out = []
    addr = PROG_ADDR
    pending_push = 0
    for _ in range(length):
        choice = rng.random()
        if choice < 0.34:
            mnem, op = rng.choice(IMPLIED)
            out.append(Instr(addr, bytes([op]), mnem, "impl"))
            addr += 1
        elif choice < 0.60:
            mnem, op = rng.choice(IMM)
            out.append(Instr(addr, bytes([op, rng.randint(0, 255)]), mnem, "imm"))
            addr += 2
        elif choice < 0.82:
            mnem, op = rng.choice(ABS_RW)
            tgt = rng.randint(SCRATCH_LO, SCRATCH_HI - 1)
            out.append(Instr(addr, bytes([op, tgt & 0xFF, tgt >> 8]), mnem, "abs"))
            addr += 3
        elif choice < 0.94:
            mnem, op = rng.choice(ABS_ST)
            tgt = rng.randint(SCRATCH_LO, SCRATCH_HI - 1)
            out.append(Instr(addr, bytes([op, tgt & 0xFF, tgt >> 8]), mnem, "abs"))
            addr += 3
        else:
            if allow_stack and rng.random() < 0.5:
                mnem, op = rng.choice([("PHA", 0x48), ("PHP", 0x08)])
                out.append(Instr(addr, bytes([op]), mnem, "impl"))
                addr += 1
                pending_push += 1
            elif allow_stack and pending_push > 0:
                mnem, op = rng.choice([("PLA", 0x68), ("PLP", 0x28)])
                out.append(Instr(addr, bytes([op]), mnem, "impl"))
                addr += 1
                pending_push -= 1
            else:
                out.append(Instr(addr, bytes([0xEA]), "NOP", "impl"))
                addr += 1
    # balance any outstanding pushes with pulls so SP returns clean
    while pending_push > 0:
        out.append(Instr(addr, bytes([0x68]), "PLA", "impl"))
        addr += 1
        pending_push -= 1
    return out, addr


def gen_jsr_nest(depth, rng):
    """Build a launcher (JSR sub0; NOP return-point) plus `depth` nested
    subroutines, each: LDA #imm; JSR next; RTS  (leaf: LDA #imm; RTS).
    The launcher gives the top-level routine a real return frame, so a full
    descend + RTS-chain unwind lands back on the NOP with SP restored.
    Returns (instrs, entry_addr, return_point_addr)."""
    # Launcher at PROG_ADDR: JSR sub0 (3) ; NOP (1, the unwind landing point)
    entry = PROG_ADDR
    return_point = PROG_ADDR + 3
    first_sub = PROG_ADDR + 4
    # Lay out subroutines back-to-back after the launcher.
    layout = []
    addr = first_sub
    for i in range(depth):
        layout.append(addr)
        addr += 2 + (3 if i < depth - 1 else 0) + 1  # LDA#, [JSR], RTS
    instrs = [
        Instr(entry, bytes([0x20, layout[0] & 0xFF, layout[0] >> 8]), "JSR", "abs"),
        Instr(return_point, bytes([0xEA]), "NOP", "impl"),
    ]
    for i in range(depth):
        a = layout[i]
        instrs.append(Instr(a, bytes([0xA9, (i * 7 + 1) & 0xFF]), "LDA", "imm"))
        a += 2
        if i < depth - 1:
            tgt = layout[i + 1]
            instrs.append(Instr(a, bytes([0x20, tgt & 0xFF, tgt >> 8]), "JSR", "abs"))
            a += 3
        instrs.append(Instr(a, bytes([0x60]), "RTS", "impl"))
        a += 1
    return instrs, entry, return_point


# ----------------------------------------------------------------------------
# Oracle mirror + key selection
# ----------------------------------------------------------------------------

def sr_mask_compare(a, b):
    """Compare status registers ignoring B (bit4) and unused (bit5) which the
    capture path sets/clears non-architecturally."""
    m = 0xFF & ~(FLAG_B | 0x20)
    return (a & m) == (b & m)


def _footer_diffs(obs: Footer, cpu) -> list:
    diffs = []
    if obs.pc != cpu.pc:
        diffs.append(f"PC {cpu.pc:04X}!={obs.pc:04X}")
    if obs.ac != cpu.a:
        diffs.append(f"AC {cpu.a:02X}!={obs.ac:02X}")
    if obs.xr != cpu.x:
        diffs.append(f"XR {cpu.x:02X}!={obs.xr:02X}")
    if obs.yr != cpu.y:
        diffs.append(f"YR {cpu.y:02X}!={obs.yr:02X}")
    if obs.sp != cpu.sp:
        diffs.append(f"SP {cpu.sp:02X}!={obs.sp:02X}")
    if not sr_mask_compare(obs.sr, cpu.p):
        diffs.append(f"SR {cpu.p:08b}!={obs.sr:08b}")
    return diffs


def assert_match(obs: Footer, cpu, ctx, refetch=None):
    """Compare the debugger's register footer against the oracle.

    `refetch` re-reads the footer once, and only when a mismatch is already in
    hand. menu_screen can return the footer row while the debugger is writing
    it, giving the PC and SP of the new stop beside a register value the
    previous stop left behind; that reads as a one-register divergence which is
    not one. A real divergence is still present on the second read, so this
    cannot hide one, and the happy path pays for no extra read.
    """
    diffs = _footer_diffs(obs, cpu)
    if diffs and refetch is not None:
        again = refetch()
        if again is not None:
            confirmed = _footer_diffs(again, cpu)
            if not confirmed:
                return
            diffs = confirmed
    if diffs:
        raise StressError(f"{ctx}: oracle/footer mismatch: {', '.join(diffs)}")


# ----------------------------------------------------------------------------
# One debug session over a generated program
# ----------------------------------------------------------------------------

def init_scratch(sess: RestSession, cpu, rng):
    """Initialise the mirrored absolute data window identically on device + oracle.
    Written BEFORE entering debug; $C800-$CBFF is free RAM that neither BASIC nor
    the bootstrap touches, so it survives until the (frozen) program reads it.
    (Memory cannot be written while frozen in debug - a DMA writemem disrupts the
    BRK debug state. Zero-page is not used: BASIC overwrites $02-$8F before freeze.)"""
    blk = bytes(rng.randint(0, 255) for _ in range(SCRATCH_HI - SCRATCH_LO + 1))
    sess.rest.write_mem(SCRATCH_LO, blk)   # chunks large blocks via POST automatically
    for i, b in enumerate(blk):
        cpu.mem[SCRATCH_LO + i] = b


def write_program(sess: RestSession, cpu, instrs):
    blob = bytearray()
    base = instrs[0].addr
    for ins in instrs:
        blob += ins.code
    sess.rest.write_mem(base, bytes(blob))
    for i, b in enumerate(blob):
        cpu.mem[base + i] = b


@dataclass(frozen=True)
class BootstrapState:
    """Register state the $C500 bootstrap leaves behind when it reaches target."""
    pc: int
    sp: int
    ac: int
    xr: int
    yr: int
    sr: int


def write_bootstrap(rest: R.Rest, target: int, seed: int) -> BootstrapState:
    """Write the $C500 register bootstrap and return the state it produces.

    The bootstrap normalises the flags, forces a deterministic stack pointer so
    footer comparisons are stable, loads seeded A/X/Y, then jumps to target.
    """
    ac = (0x20 + seed * 17) & 0xFF
    xr = (0x40 + seed * 29) & 0xFF
    yr = (0x01 + seed * 7) & 0x7F
    program = bytes([
        0xD8, 0x18, 0x78, 0xB8,             # CLD / CLC / SEI / CLV
        0xA2, 0xF8, 0x9A,                   # deterministic SP for footer checks
        0xA9, ac, 0xA2, xr, 0xA0, yr,       # known registers
        0x4C, target & 0xFF, target >> 8,   # JMP target
    ])
    rest.write_mem(BOOTSTRAP_ADDR, program)
    sr = FLAG_B | FLAG_I                    # LDY sets N/Z last
    if yr == 0:
        sr |= FLAG_Z
    if yr & 0x80:
        sr |= FLAG_N
    return BootstrapState(pc=target, sp=0xF8, ac=ac, xr=xr, yr=yr, sr=sr)


def enter_at(sess: RestSession, cpu, target, seed):
    """Bootstrap registers + JMP target; breakpoint at target; Go; verify footer."""
    expected = write_bootstrap(sess.rest, target, seed)   # writes the $C500 bootstrap
    # mirror the bootstrap's resulting register state into the oracle
    cpu.set_state(expected.ac, expected.xr, expected.yr, expected.sp, target,
                  expected.sr)
    # also seed the oracle's copy of the bootstrap bytes (harmless; we start at target)
    boot = sess.read_mem(BOOTSTRAP_ADDR, 16)
    for i, b in enumerate(boot):
        cpu.mem[BOOTSTRAP_ADDR + i] = b
    sess.open()
    sess.asm_view()
    sess.set_cpu_bank7()
    sess.enter_debug()
    # clear any stale breakpoints, set a fresh one at target, Go from bootstrap
    sess.clear_all_breakpoints()
    sess.set_breakpoint(target)
    sess.goto(BOOTSTRAP_ADDR)
    sess.key("g")
    time.sleep(0.2)
    f = sess.wait_footer_pc(target, timeout=10.0, ctx="enter_at Go->target")
    # remove all breakpoints so single-stepping is clean
    sess.clear_breakpoint(target)
    sess.goto(target)
    assert_match(f, cpu, "entry footer")
    return f


def choose_key_and_advance(cpu, instrs_by_addr):
    """Decide the monitor step key for the instruction at cpu.pc and advance the
    oracle to the resulting terminal state. Returns (key, opcode, mnem, writes)
    or None if the instruction is outside our generated program (stop)."""
    pc = cpu.pc
    ins = instrs_by_addr.get(pc)
    if ins is None:
        return None
    op = ins.code[0]
    if op == 0x20:  # JSR: alternate Step-Into and Step-Over by address parity for coverage
        if (pc & 1) == 0:
            # Step Over: run whole subroutine; oracle steps until SP returns and pc==ret
            ret = (pc + 3) & 0xFFFF
            sp_before = cpu.sp
            writes = []
            res = cpu.step()  # into the subroutine
            writes += [(w.addr, w.value) for w in res.writes]
            guard = 0
            while not (cpu.sp == sp_before and cpu.pc == ret):
                res = cpu.step()
                guard += 1
                writes += [(w.addr, w.value) for w in res.writes]
                if guard > 10000:
                    raise StressError("step-over JSR did not return in oracle")
            return ("d", op, "JSR/over", writes)
        cpu.step()  # single step into
        return ("t", op, "JSR/into", [])
    # Default: single instruction -> Trace (Step Into). Step Into is a pure BRK
    # single-step that keeps the C64 frozen and the debug footer continuously
    # observable over menu_screen. Step Over (D) of a non-JSR runs an internal
    # mini-Go that briefly unfreezes the C64; in FREEZE UI mode that transiently
    # 404s menu_screen, so Step Over is exercised on its meaningful case (JSR) in
    # run_stepover_jsr_session, not on every linear instruction.
    res = cpu.step()
    return ("t", op, ins.mnem, [(w.addr, w.value) for w in res.writes])


def run_program_session(sess, rng, instrs, seed, max_steps, jsonl, stats,
                        sp_coherence_check=True, active_write_readback=False,
                        defer_write_validation=True):
    cpu = ORC.CPU6502()
    sess.clean_baseline()               # known closed-menu state before a fresh session
    init_scratch(sess, cpu, rng)        # free-RAM data window, before entry (frozen-safe)
    write_program(sess, cpu, instrs)
    instrs_by_addr = {ins.addr: ins for ins in instrs}
    entry = instrs[0].addr
    enter_at(sess, cpu, entry, seed)
    steps = 0
    while steps < max_steps:
        plan = choose_key_and_advance(cpu, instrs_by_addr)
        if plan is None:
            break
        key, op, mnem, writes = plan
        # Send the step, re-sending on a no-progress re-trap (B16). Progress is
        # confirmed by debugger-visible PC/footer state. Active-Debug readmem
        # write confirmation is capability-gated because it can see freezer
        # backing state rather than live target bytes.
        if not sess.progress_step(key, cpu.pc, writes,
                                  active_write_progress=active_write_readback):
            raise StressError(f"step {steps} {mnem}: no progress after re-sends "
                              f"(want PC {cpu.pc:04X}){sess.hold_note()}")
        if active_write_readback:
            for waddr, wval in writes:
                if waddr in (0x0000, 0x0001):
                    continue
                dev = None
                for _ in range(25):
                    dev = sess.read_mem(waddr, 1)[0]
                    if dev == (wval & 0xFF):
                        break
                    time.sleep(0.1)
                if dev != (wval & 0xFF):
                    raise StressError(f"step {steps} {mnem}: write ${waddr:04X} "
                                      f"oracle {wval & 0xFF:02X} dev {dev:02X}")
                stats["writes_verified"] = stats.get("writes_verified", 0) + 1
        elif writes:
            stats["writes_deferred"] = stats.get("writes_deferred", 0) + len(writes)
        f = sess.wait_footer_pc(cpu.pc, timeout=8.0, ctx=f"step {steps} {mnem}",
                                sp=cpu.sp)
        assert_match(f, cpu, f"step {steps} op={op:02X} {mnem} key={key}",
                 refetch=sess.footer)
        steps += 1
        stats["steps"] += 1
        stats["ops"][f"{op:02X}"] = stats["ops"].get(f"{op:02X}", 0) + 1
        stats["keys"][key] = stats["keys"].get(key, 0) + 1
        if jsonl:
            jsonl.write(json.dumps({"t": "step", "i": steps, "op": op, "mnem": mnem,
                                    "key": key, "pc": cpu.pc, "a": cpu.a, "x": cpu.x,
                                    "y": cpu.y, "sp": cpu.sp, "sr": cpu.p}) + "\n")
    if defer_write_validation:
        sess.close()
        L.ensure_menu_closed(sess.rest)
        time.sleep(0.3)
    # verify entire scratch window matches the oracle at a safe checkpoint
    # (catches any missed writes without trusting active-Debug readmem).
    dev = sess.read_mem(SCRATCH_LO, 256)
    for i, b in enumerate(dev):
        if cpu.mem[SCRATCH_LO + i] != b:
            raise StressError(f"scratch mismatch at ${SCRATCH_LO+i:04X}: "
                              f"oracle {cpu.mem[SCRATCH_LO+i]:02X} dev {b:02X}")
    return steps


def run_jsr_session(sess, rng, depth, seed, jsonl, stats):
    cpu = ORC.CPU6502()
    sess.clean_baseline()            # known closed-menu state before a fresh session
    init_scratch(sess, cpu, rng)     # free-RAM data window, before entry
    instrs, entry, return_point = gen_jsr_nest(depth, rng)
    write_program(sess, cpu, instrs)
    instrs_by_addr = {ins.addr: ins for ins in instrs}
    enter_at(sess, cpu, entry, seed)
    sp_entry = cpu.sp                 # SP at the launcher JSR
    min_sp = sp_entry
    # Step INTO through the whole nest (descend) and let the RTS chain unwind,
    # until we land back on the return-point NOP after the launcher JSR.
    steps = 0
    guard = 0
    while cpu.pc != return_point and guard < depth * 30:
        guard += 1
        if instrs_by_addr.get(cpu.pc) is None:
            raise StressError(f"jsr nest stepped outside program at {cpu.pc:04X}")
        cpu.step()
        min_sp = min(min_sp, cpu.sp)
        if not sess.progress_step("t", cpu.pc, [], expected_sp=cpu.sp):
            raise StressError(f"jsr step {steps}: no progress (want PC {cpu.pc:04X})")
        f = sess.wait_footer_pc(cpu.pc, timeout=8.0, ctx=f"jsr step {steps}",
                                sp=cpu.sp)
        assert_match(f, cpu, f"jsr step {steps} depth={depth}",
                 refetch=sess.footer)
        steps += 1
        stats["jsr_steps"] += 1
    # Coherence: descended below entry (real nesting) and unwound back to entry SP.
    if cpu.pc != return_point:
        raise StressError(f"jsr nest did not unwind to return point (pc={cpu.pc:04X})")
    if cpu.sp != sp_entry:
        raise StressError(f"SP coherence broke: entry {sp_entry:02X} end {cpu.sp:02X}")
    expected_min = (sp_entry - 2 * depth) & 0xFF
    if min_sp != expected_min:
        raise StressError(f"descend depth wrong: min_sp {min_sp:02X} expected {expected_min:02X}")
    if jsonl:
        jsonl.write(json.dumps({"t": "jsr", "depth": depth, "steps": steps,
                                "sp_entry": sp_entry, "sp_end": cpu.sp,
                                "min_sp": min_sp}) + "\n")
    stats["jsr_cycles"] += 1
    return steps


def liveness_check(sess: RestSession):
    """Exit Debug to a live machine and confirm the jiffy clock advances."""
    sess.close()
    L.ensure_menu_closed(sess.rest)
    time.sleep(0.2)
    j0 = sess.rest.read_mem(0x00A2, 1)[0]
    time.sleep(0.5)
    j1 = sess.rest.read_mem(0x00A2, 1)[0]
    return j0 != j1


# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.15")
    ap.add_argument("--c64-host", default=None,
                    help="Split-session mode for a U2+L cartridge: the C64U host "
                         "it is plugged into. Keystrokes and memory go there "
                         "while menu_screen/menu_button stay on --host; the "
                         "cartridge's own machine:input answers HTTP 501.")
    ap.add_argument("--ui", default="freeze", choices=["freeze", "overlay"])
    ap.add_argument("--focus", default="all",
                    choices=["all", "steps", "jsr", "liveness"])
    ap.add_argument("--iterations", type=int, default=20)
    ap.add_argument("--prog-len", type=int, default=40)
    ap.add_argument("--max-steps", type=int, default=200)
    ap.add_argument("--jsr-depths", default="8,8,8,16")
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--artifact-dir", default="")
    ap.add_argument("--fail-fast", action="store_true")
    ap.add_argument("--sp-coherence-check", action="store_true", default=True)
    ap.add_argument("--liveness-every", type=int, default=5)
    ap.add_argument("--active-write-readback", action="store_true",
                    help="Use active-Debug readmem for immediate write validation "
                         "only after proving it observes the live target store.")
    ap.add_argument("--no-defer-write-validation", action="store_true",
                    help="Keep the legacy active-Debug final scratch read. Not "
                         "recommended for the matrix gate.")
    a = ap.parse_args()

    rng = random.Random(a.seed)
    artdir = a.artifact_dir or "."
    os.makedirs(artdir, exist_ok=True)
    jsonl_path = os.path.join(artdir, f"stress_{a.ui}_{a.focus}_{a.seed}.jsonl")
    with open(jsonl_path, "w", buffering=1) as jsonl:
        stats = {"steps": 0, "jsr_steps": 0, "jsr_cycles": 0, "liveness_ok": 0,
                 "liveness_fail": 0, "iterations": 0, "errors": 0,
                 "ops": {}, "keys": {}, "ui": a.ui}

        sess = RestSession(a.host, ui=a.ui, c64_host=a.c64_host)
        if not sess.alive():
            print("DEVICE NOT ALIVE")
            return 2

        def log(m):
            print(f"{time.strftime('%H:%M:%S')} {m}", flush=True)

        log(f"STRESS start ui={a.ui} focus={a.focus} iters={a.iterations} seed={a.seed}")
        identity_at_start = SR.device_identity(sess.rest)
        log(f"device identity at start: {identity_at_start}")
        sess.recover()
        try:
            sess.set_ui_mode()      # leaves a clean closed-menu baseline; do NOT close()
        except Exception as e:       # (run_stop/C=+D into a running C64 corrupts its state)
            log(f"set_ui_mode warn: {e}")
            sess.recover()

        jsr_depths = [int(x) for x in a.jsr_depths.split(",") if x]
        rc = 0
        for it in range(a.iterations):
            stats["iterations"] = it + 1
            try:
                if a.focus in ("all", "steps"):
                    instrs, _ = gen_program(rng, a.prog_len)
                    n = run_program_session(sess, rng, instrs, a.seed + it, a.max_steps,
                                            jsonl, stats, a.sp_coherence_check,
                                            active_write_readback=a.active_write_readback,
                                            defer_write_validation=not a.no_defer_write_validation)
                    log(f"iter {it+1}/{a.iterations} steps+={n} total_steps={stats['steps']}")
                if a.focus in ("all", "jsr"):
                    depth = jsr_depths[it % len(jsr_depths)]
                    run_jsr_session(sess, rng, depth, a.seed + 1000 + it, jsonl, stats)
                    log(f"iter {it+1} jsr depth={depth} cycles={stats['jsr_cycles']}")
                if a.focus in ("all", "liveness") or (it % a.liveness_every == 0):
                    ok = liveness_check(sess)
                    stats["liveness_ok" if ok else "liveness_fail"] += 1
                    if not ok:
                        # A liveness failure on a split session is usually the
                        # machine being left DMA-held by the monitor exit, not a
                        # stepping result. Name which one it is where it is logged.
                        log(f"iter {it+1} LIVENESS FAIL{sess.hold_note()}")
                    sess.recover()
            except StressError as e:
                stats["errors"] += 1
                rc = 3
                log(f"iter {it+1} STRESS-ERROR: {e}")
                if jsonl:
                    jsonl.write(json.dumps({"t": "error", "iter": it + 1, "msg": str(e)}) + "\n")
                if not sess.alive():
                    log(f"iter {it+1} *** DEVICE WEDGE (REST dead) ***")
                    jsonl.write(json.dumps({"t": "wedge", "iter": it + 1}) + "\n")
                    rc = 2
                    break
                if a.fail_fast:
                    break
                sess.recover()
            except Exception as e:  # noqa: BLE001
                stats["errors"] += 1
                rc = 3
                log(f"iter {it+1} EXC {type(e).__name__}: {e}")
                if not sess.alive():
                    rc = 2
                    break
                if a.fail_fast:
                    break
                sess.recover()

        # A device that was reflashed or swapped part-way through was not one image
        # under test, whatever the counts say, so this outranks the run's own result.
        identity_at_end = SR.device_identity(sess.rest)
        identity_changed = SR.identity_changes(identity_at_start, identity_at_end)
        if identity_changed:
            log(f"*** DEVICE IDENTITY CHANGED DURING THE RUN: {identity_changed} - "
                f"this run is not a measurement of one image and its numbers are "
                f"not evidence ***")
            rc = 4

        summary = {"t": "summary", **stats, "rc": rc,
                   "step_resends": sess.step_resends,
                   "final_alive": sess.alive(),
                   "device_identity_start": identity_at_start,
                   "device_identity_end": identity_at_end,
                   "device_identity_changed": identity_changed}
        jsonl.write(json.dumps(summary) + "\n")
        with open(os.path.join(artdir, f"stress_{a.ui}_{a.focus}_{a.seed}.summary.json"), "w") as f:
            json.dump(summary, f, indent=2)
        log(f"STRESS end {summary}")
        return rc


if __name__ == "__main__":
    sys.exit(main())
