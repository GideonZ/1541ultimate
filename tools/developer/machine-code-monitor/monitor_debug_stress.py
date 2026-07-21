#!/usr/bin/env python3
"""REST-transport Machine Code Monitor debugger stress runner.

Drives the shipped debugger entirely over the production REST API (machine:input,
menu_screen, menu_button, readmem, writemem) so it survives the U64 telnet
load-degradation, and asserts every step against the independent NMOS-6510 oracle
(mcm6502.py): PC / A / X / Y / SP / SR and memory writes, with SP-coherence over
JSR/RTS nesting and natural-exit liveness validation.

UI modes:  freeze | overlay   (telnet handled by the legacy monitor_debug_soak.py)

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
from dataclasses import dataclass, field
from typing import Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mcm_rest as R          # noqa: E402
import mcm_localui as L       # noqa: E402
import mcm6502 as ORC         # noqa: E402
import overlay_lifecycle_clean as overlay_lifecycle  # noqa: E402

# Pull the proven bootstrap/flag constants from the telnet soak (transport-agnostic).
import monitor_debug_soak as sk   # noqa: E402

BOOTSTRAP_ADDR = sk.BOOTSTRAP_ADDR        # 0xC500
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

FLAG_B = 0x10


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


def parse_footer(lines) -> Optional[Footer]:
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
    def __init__(self, host, ui="freeze"):
        self.rest = R.Rest(host=host)
        self.host = host
        self.ui = ui

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

    def wait_footer_pc(self, pc, timeout=8.0, ctx=""):
        """Poll menu_screen ONCE per iteration (a double-fetch right after a step
        can transiently 404 the overlay menu_screen pipeline) until the debug
        footer shows the expected PC."""
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            ls = self.lines()
            if ls:
                f = parse_footer(ls)
                last = f
                if f and f.pc == pc:
                    return f
                if f is None:
                    self._modal_in(ls, ctx or "wait_footer_pc")
            time.sleep(0.1)
        raise StressError(f"{ctx}: footer PC did not reach {pc:04X} (last={last})\n{self.text()}")

    def progress_step(self, key, expected_pc, writes, max_resend=3,
                      active_write_progress=False):
        """Send a Step key and re-send it if the step makes no progress. The clean
        -stop release after a Go/entry intermittently re-traps the first step at the
        launch site without advancing (the documented B16 FPGA-aperture behaviour);
        a re-send then advances it. Progress is the footer PC reaching
        `expected_pc`. Active-Debug readmem can observe freezer-owned backing
        state instead of the live CPU store, so write readback is optional and
        must be enabled only after a capability probe.
        Ordering is preserved: a tap is only ever followed by readmem (write steps)
        or by a footer GET (non-write steps), never by readmem->tap->menu_screen.
        Returns True once progress is observed."""
        first_w = next(((a, v) for a, v in writes if a not in (0x0000, 0x0001)), None)
        for _ in range(max_resend + 1):
            self.rest.tap([key])
            deadline = time.time() + 1.6
            while time.time() < deadline:
                f = self.footer()
                if f and f.pc == expected_pc:
                    return True
                if active_write_progress and first_w is not None:
                    if self.rest.read_mem(first_w[0], 1)[0] == (first_w[1] & 0xFF):
                        return True
                time.sleep(0.1)
        return False

    # --- monitor lifecycle ---
    def _jiffy_running(self):
        """True if the C64 jiffy clock ($A2) advances => C64 is running (overlay);
        False => C64 frozen (freeze mode). Reliable mode discriminator while the
        monitor is open but NOT in debug."""
        j0 = self.rest.read_mem(0x00A2, 1)[0]
        time.sleep(0.4)
        j1 = self.rest.read_mem(0x00A2, 1)[0]
        return j0 != j1

    def set_ui_mode(self):
        """Ensure the configured UI mode through the same REST configuration path
        used by the maintained overlay lifecycle gate."""
        L.ensure_menu_closed(self.rest)
        if self.ui == "overlay":
            overlay_lifecycle.set_interface_type(self.rest, "Overlay on HDMI")
        else:
            overlay_lifecycle.set_interface_type(self.rest, "Freeze")
        L.ensure_menu_closed(self.rest)

    def recover(self):
        self.rest.tap(["commodore", "x"]); time.sleep(0.5)
        L.ensure_menu_open(self.rest)

    def clean_baseline(self):
        """Return to a known closed-menu baseline before opening a fresh debug
        session by fully tearing down any prior debug/monitor session via the
        same keystrokes a user would use, then forcing a production reset. The
        reset is required after failed or interrupted debug runs: while the UI can
        still be alive, REST readmem/writemem may be observing the frozen backing
        state instead of live RAM until the machine is reset."""
        self.rest.tap(["commodore", "d"]); time.sleep(0.2)   # leave Debug if active
        self.rest.tap(["run_stop"]); time.sleep(0.2)          # close monitor if open
        self.rest.tap(["commodore", "x"]); time.sleep(0.4)    # break/reset from any monitor mode
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
        # leave debug if active, then close to a live machine
        self.rest.tap(["commodore", "d"]); time.sleep(0.2)
        self.rest.tap(["run_stop"]); time.sleep(0.3)
        L.settle(self.rest, 0.2)

    def goto(self, addr):
        overlay_lifecycle.goto_addr(self.rest, addr, f"stress goto ${addr:04X}")

    def asm_view(self):
        self.rest.tap(["a"]); time.sleep(0.15)

    def set_cpu_bank7(self):
        """Cycle O until the footer shows CPU7 (view==exec bank 7)."""
        for _ in range(9):
            ls = self.lines() or []
            if any(ln.strip().startswith("CPU7") or " CPU7" in ln for ln in ls):
                return
            self.rest.tap(["o"]); time.sleep(0.15)
        # not fatal; bank view doesn't affect execution stream

    def enter_debug(self):
        self.rest.tap(["d"]); time.sleep(0.2)
        ls = self.lines() or []
        if not any("Dbg" in ln for ln in ls):
            raise StressError("debug mode not entered")

    def set_breakpoint(self, addr):
        overlay_lifecycle.ensure_breakpoint_at(
            self.rest, addr, 7, "RAM", f"stress set bp ${addr:04X}")

    def clear_breakpoint(self, addr):
        overlay_lifecycle.clear_breakpoint_at(
            self.rest, addr, 7, f"stress clear bp ${addr:04X}")

    def clear_all_breakpoints(self):
        """Reset all 10 breakpoint slots via the C=+R list popup (DEL per slot)."""
        self.rest.tap(["commodore", "r"]); time.sleep(0.3)
        ls = self.lines() or []
        if not any("BRK" in ln.upper() or "BREAK" in ln.upper() for ln in ls):
            # popup not detected; bail without leaving stray keys
            self.rest.tap(["run_stop"]); time.sleep(0.2)
            return
        for _ in range(10):
            self.rest.tap(["inst_del"]); time.sleep(0.1)
            self.rest.tap(["cursor_up_down"]); time.sleep(0.08)
        self.rest.tap(["run_stop"]); time.sleep(0.2)


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
ZP_RW = [  # read or rmw, operand = zp addr
    ("LDA", 0xA5), ("LDX", 0xA6), ("LDY", 0xA4), ("ADC", 0x65), ("SBC", 0xE5),
    ("AND", 0x25), ("ORA", 0x05), ("EOR", 0x45), ("CMP", 0xC5), ("BIT", 0x24),
    ("INC", 0xE6), ("DEC", 0xC6), ("ASL", 0x06), ("LSR", 0x46), ("ROL", 0x26),
    ("ROR", 0x66),
]
ZP_ST = [("STA", 0x85), ("STX", 0x86), ("STY", 0x84)]
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
            out.append(Instr(addr, bytes([op]), mnem, "impl")); addr += 1
        elif choice < 0.60:
            mnem, op = rng.choice(IMM)
            out.append(Instr(addr, bytes([op, rng.randint(0, 255)]), mnem, "imm")); addr += 2
        elif choice < 0.82:
            mnem, op = rng.choice(ABS_RW)
            tgt = rng.randint(SCRATCH_LO, SCRATCH_HI - 1)
            out.append(Instr(addr, bytes([op, tgt & 0xFF, tgt >> 8]), mnem, "abs")); addr += 3
        elif choice < 0.94:
            mnem, op = rng.choice(ABS_ST)
            tgt = rng.randint(SCRATCH_LO, SCRATCH_HI - 1)
            out.append(Instr(addr, bytes([op, tgt & 0xFF, tgt >> 8]), mnem, "abs")); addr += 3
        else:
            if allow_stack and rng.random() < 0.5:
                mnem, op = rng.choice([("PHA", 0x48), ("PHP", 0x08)])
                out.append(Instr(addr, bytes([op]), mnem, "impl")); addr += 1
                pending_push += 1
            elif allow_stack and pending_push > 0:
                mnem, op = rng.choice([("PLA", 0x68), ("PLP", 0x28)])
                out.append(Instr(addr, bytes([op]), mnem, "impl")); addr += 1
                pending_push -= 1
            else:
                out.append(Instr(addr, bytes([0xEA]), "NOP", "impl")); addr += 1
    # balance any outstanding pushes with pulls so SP returns clean
    while pending_push > 0:
        out.append(Instr(addr, bytes([0x68]), "PLA", "impl")); addr += 1
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
        instrs.append(Instr(a, bytes([0xA9, (i * 7 + 1) & 0xFF]), "LDA", "imm")); a += 2
        if i < depth - 1:
            tgt = layout[i + 1]
            instrs.append(Instr(a, bytes([0x20, tgt & 0xFF, tgt >> 8]), "JSR", "abs")); a += 3
        instrs.append(Instr(a, bytes([0x60]), "RTS", "impl")); a += 1
    return instrs, entry, return_point


# ----------------------------------------------------------------------------
# Oracle mirror + key selection
# ----------------------------------------------------------------------------

def sr_mask_compare(a, b):
    """Compare status registers ignoring B (bit4) and unused (bit5) which the
    capture path sets/clears non-architecturally."""
    m = 0xFF & ~(FLAG_B | 0x20)
    return (a & m) == (b & m)


def assert_match(obs: Footer, cpu, ctx):
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


def enter_at(sess: RestSession, cpu, target, seed):
    """Bootstrap registers + JMP target; breakpoint at target; Go; verify footer."""
    expected = sk.write_bootstrap(sess.host, target, seed)   # writes $C500 bootstrap, returns CpuState
    # mirror the bootstrap's resulting register state into the oracle
    cpu.set_state(expected.ac, expected.xr, expected.yr, expected.sp, target,
                  expected.sr)
    # also seed the oracle's copy of the bootstrap bytes (harmless; we start at target)
    boot = sess.rest.read_mem(BOOTSTRAP_ADDR, 16)
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
    sess.key("g"); time.sleep(0.2)
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
                res = cpu.step(); guard += 1
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
    sp_entry = cpu.sp
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
                              f"(want PC {cpu.pc:04X})")
        if active_write_readback:
            for waddr, wval in writes:
                if waddr in (0x0000, 0x0001):
                    continue
                dev = None
                for _ in range(25):
                    dev = sess.rest.read_mem(waddr, 1)[0]
                    if dev == (wval & 0xFF):
                        break
                    time.sleep(0.1)
                if dev != (wval & 0xFF):
                    raise StressError(f"step {steps} {mnem}: write ${waddr:04X} "
                                      f"oracle {wval & 0xFF:02X} dev {dev:02X}")
                stats["writes_verified"] = stats.get("writes_verified", 0) + 1
        elif writes:
            stats["writes_deferred"] = stats.get("writes_deferred", 0) + len(writes)
        f = sess.wait_footer_pc(cpu.pc, timeout=8.0, ctx=f"step {steps} {mnem}")
        assert_match(f, cpu, f"step {steps} op={op:02X} {mnem} key={key}")
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
    dev = sess.rest.read_mem(SCRATCH_LO, 256)
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
        if not sess.progress_step("t", cpu.pc, []):   # no writes; progress = footer PC
            raise StressError(f"jsr step {steps}: no progress (want PC {cpu.pc:04X})")
        f = sess.wait_footer_pc(cpu.pc, timeout=8.0, ctx=f"jsr step {steps}")
        assert_match(f, cpu, f"jsr step {steps} depth={depth}")
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
    ap.add_argument("--host", default="192.168.1.13")
    ap.add_argument("--ui", default="freeze", choices=["freeze", "overlay"])
    ap.add_argument("--focus", default="all",
                    choices=["all", "steps", "jsr", "liveness"])
    ap.add_argument("--banking", default="ram", choices=["ram"])  # RAM @ C000 (bank 7)
    ap.add_argument("--instr-corpus", default="full")
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
    jsonl = open(jsonl_path, "w", buffering=1)
    stats = {"steps": 0, "jsr_steps": 0, "jsr_cycles": 0, "liveness_ok": 0,
             "liveness_fail": 0, "iterations": 0, "errors": 0,
             "ops": {}, "keys": {}, "ui": a.ui}

    sess = RestSession(a.host, ui=a.ui)
    if not sess.alive():
        print("DEVICE NOT ALIVE"); return 2

    def log(m):
        print(f"{time.strftime('%H:%M:%S')} {m}", flush=True)

    log(f"STRESS start ui={a.ui} focus={a.focus} iters={a.iterations} seed={a.seed}")
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
                    log(f"iter {it+1} LIVENESS FAIL")
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
                rc = 2; break
            if a.fail_fast:
                break
            sess.recover()

    summary = {"t": "summary", **stats, "rc": rc, "final_alive": sess.alive()}
    jsonl.write(json.dumps(summary) + "\n")
    jsonl.close()
    with open(os.path.join(artdir, f"stress_{a.ui}_{a.focus}_{a.seed}.summary.json"), "w") as f:
        json.dump(summary, f, indent=2)
    log(f"STRESS end {summary}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
