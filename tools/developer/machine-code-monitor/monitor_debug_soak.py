#!/usr/bin/env python3
"""Dynamic Telnet soak test for the U64 Machine Code Monitor Debugger.

The deterministic ``monitor_debug_test.py`` suite proves specific edge cases.
This soak test keeps a Debug session alive for a configurable duration while it
repeatedly seeds controlled CPU state, enters selected BASIC/KERNAL routines,
single-steps instructions, and compares the Debug footer against a small local
6510 model for every supported instruction.

The soak runs against the normal ROM mapping. It deliberately does not copy
BASIC/KERNAL into underlying RAM; ROM breakpoints must use the debugger's
backend-specific writable execution/shadow representation.
"""

from __future__ import annotations

import argparse
import os
import random
import re
import sys
import time
import urllib.error
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

sys.path.insert(0, str(Path(__file__).parent))
import monitor_test as mt  # type: ignore  # noqa: E402
import monitor_debug_test as dbg  # type: ignore  # noqa: E402


BOOTSTRAP_ADDR = 0xC500
SCRATCH_ADDR = 0xC5F0
DEFAULT_DURATION = 600.0

FLAG_C = 0x01
FLAG_Z = 0x02
FLAG_I = 0x04
FLAG_D = 0x08
FLAG_B = 0x10
FLAG_V = 0x40
FLAG_N = 0x80


@dataclass(frozen=True)
class Routine:
    name: str
    address: int
    region: str


# High-value entry points from the C64 BASIC/KERNAL disassembly. These are not
# all safe to run forever, so the soak restarts often and steps only while the
# local model can validate the instruction exactly.
ROUTINES = [
    Routine("BASIC END", 0xA831, "BASIC"),
    Routine("BASIC FOR", 0xA742, "BASIC"),
    Routine("BASIC RUN", 0xA871, "BASIC"),
    Routine("BASIC GOTO", 0xA8A0, "BASIC"),
    Routine("BASIC GOSUB", 0xA883, "BASIC"),
    Routine("BASIC RETURN", 0xA8D2, "BASIC"),
    Routine("BASIC LET", 0xA9A5, "BASIC"),
    Routine("BASIC PRINT", 0xAAA0, "BASIC"),
    Routine("BASIC PEEK", 0xB80D, "BASIC"),
    Routine("BASIC POKE", 0xB824, "BASIC"),
    Routine("KERNAL LOAD", 0xE168, "KERNAL"),
    Routine("KERNAL SAVE", 0xE156, "KERNAL"),
    Routine("KERNAL SYS", 0xE12A, "KERNAL"),
    Routine("KERNAL CINT", 0xFF81, "KERNAL"),
    Routine("KERNAL IOINIT", 0xFF84, "KERNAL"),
    Routine("KERNAL RAMTAS", 0xFF87, "KERNAL"),
    Routine("KERNAL RESTOR", 0xFF8A, "KERNAL"),
    Routine("KERNAL VECTOR", 0xFF8D, "KERNAL"),
    Routine("KERNAL SETMSG", 0xFF90, "KERNAL"),
    Routine("KERNAL SECOND", 0xFF93, "KERNAL"),
    Routine("KERNAL CIOUT", 0xFFA8, "KERNAL"),
    Routine("KERNAL UNLSN", 0xFFAE, "KERNAL"),
    Routine("KERNAL LISTN", 0xFFB1, "KERNAL"),
    Routine("KERNAL SETLFS", 0xFFBA, "KERNAL"),
    Routine("KERNAL SETNAM", 0xFFBD, "KERNAL"),
    Routine("KERNAL OPEN", 0xFFC0, "KERNAL"),
    Routine("KERNAL CLOSE", 0xFFC3, "KERNAL"),
    Routine("KERNAL CHKIN", 0xFFC6, "KERNAL"),
    Routine("KERNAL CHKOUT", 0xFFC9, "KERNAL"),
    Routine("KERNAL CLRCHN", 0xFFCC, "KERNAL"),
    Routine("KERNAL CHRIN", 0xFFCF, "KERNAL"),
    Routine("KERNAL CHROUT", 0xFFD2, "KERNAL"),
    Routine("KERNAL STOP", 0xFFE1, "KERNAL"),
    Routine("KERNAL GETIN", 0xFFE4, "KERNAL"),
    Routine("KERNAL CLALL", 0xFFE7, "KERNAL"),
]


@dataclass
class CpuState:
    pc: int
    sp: int
    ac: int
    xr: int
    yr: int
    sr: int

    @classmethod
    def from_footer(cls, session: "mt.MonitorSession") -> "CpuState":
        parsed = dbg._parse_footer_values(dbg._footer_value_line(session))
        missing = [name for name in ("pc", "sp", "ac", "xr", "yr", "sr") if not parsed[name]]
        if missing:
            raise mt.Failure(f"Debug footer has blank fields {missing}: {parsed!r}")
        return cls(
            pc=int(parsed["pc"], 16),
            sp=int(parsed["sp"], 16),
            ac=int(parsed["ac"], 16),
            xr=int(parsed["xr"], 16),
            yr=int(parsed["yr"], 16),
            sr=int(parsed["sr"], 2),
        )

    def footer_dict(self) -> dict[str, str]:
        return {
            "pc": f"{self.pc:04X}",
            "sp": f"{self.sp:02X}",
            "ac": f"{self.ac:02X}",
            "xr": f"{self.xr:02X}",
            "yr": f"{self.yr:02X}",
            "sr": f"{self.sr:08b}",
        }


def _u8(value: int) -> int:
    return value & 0xFF


def _u16(value: int) -> int:
    return value & 0xFFFF


def _rel(pc: int, offset: int) -> int:
    if offset & 0x80:
        offset -= 0x100
    return _u16(pc + 2 + offset)


def _set_nz(sr: int, value: int) -> int:
    sr &= ~(FLAG_N | FLAG_Z)
    value &= 0xFF
    if value == 0:
        sr |= FLAG_Z
    if value & 0x80:
        sr |= FLAG_N
    return sr


class Memory:
    def __init__(self, rest_host: str) -> None:
        self.rest_host = rest_host
        self.cache: dict[int, int] = {}

    def read(self, address: int) -> int:
        address = _u16(address)
        if address not in self.cache:
            self.cache[address] = mt.read_rest_memory(self.rest_host, address, 1)[0]
        return self.cache[address]

    def read_word(self, address: int) -> int:
        return self.read(address) | (self.read(address + 1) << 8)

    def read_word_6502_bug(self, address: int) -> int:
        hi_addr = (address & 0xFF00) | ((address + 1) & 0x00FF)
        return self.read(address) | (self.read(hi_addr) << 8)

    def note_write(self, address: int, value: int) -> None:
        self.cache[_u16(address)] = _u8(value)


def _abs(bytes_: bytes) -> int:
    return bytes_[1] | (bytes_[2] << 8)


def _zp(bytes_: bytes) -> int:
    return bytes_[1]


def _operand_value(mem: Memory, state: CpuState, bytes_: bytes, mode: str) -> int:
    if mode == "imm":
        return bytes_[1]
    if mode == "zp":
        return mem.read(_zp(bytes_))
    if mode == "zpx":
        return mem.read(_u8(_zp(bytes_) + state.xr))
    if mode == "zpy":
        return mem.read(_u8(_zp(bytes_) + state.yr))
    if mode == "abs":
        return mem.read(_abs(bytes_))
    if mode == "absx":
        return mem.read(_abs(bytes_) + state.xr)
    if mode == "absy":
        return mem.read(_abs(bytes_) + state.yr)
    if mode == "indx":
        ptr = _u8(_zp(bytes_) + state.xr)
        return mem.read(mem.read(ptr) | (mem.read(_u8(ptr + 1)) << 8))
    if mode == "indy":
        ptr = _zp(bytes_)
        return mem.read((mem.read(ptr) | (mem.read(_u8(ptr + 1)) << 8)) + state.yr)
    raise mt.Failure(f"Unsupported operand mode {mode}")


def _store_address(mem: Memory, state: CpuState, bytes_: bytes, mode: str) -> int:
    if mode == "zp":
        return _zp(bytes_)
    if mode == "zpx":
        return _u8(_zp(bytes_) + state.xr)
    if mode == "zpy":
        return _u8(_zp(bytes_) + state.yr)
    if mode == "abs":
        return _abs(bytes_)
    if mode == "absx":
        return _u16(_abs(bytes_) + state.xr)
    if mode == "absy":
        return _u16(_abs(bytes_) + state.yr)
    if mode == "indx":
        ptr = _u8(_zp(bytes_) + state.xr)
        return mem.read(ptr) | (mem.read(_u8(ptr + 1)) << 8)
    if mode == "indy":
        ptr = _zp(bytes_)
        return _u16((mem.read(ptr) | (mem.read(_u8(ptr + 1)) << 8)) + state.yr)
    raise mt.Failure(f"Unsupported store mode {mode}")


@dataclass
class StepPlan:
    key: str
    expected: CpuState
    verify: list[tuple[int, int]]


def emulate(mem: Memory, state: CpuState) -> Optional[StepPlan]:
    pc = state.pc
    bytes_ = bytes(mem.read(pc + i) for i in range(3))
    op = bytes_[0]
    s = CpuState(pc=pc, sp=state.sp, ac=state.ac, xr=state.xr, yr=state.yr, sr=state.sr)
    verify: list[tuple[int, int]] = []
    key = "D"

    def advance(length: int) -> None:
        s.pc = _u16(pc + length)

    def load(reg: str, value: int, length: int) -> None:
        setattr(s, reg, _u8(value))
        s.sr = _set_nz(s.sr, getattr(s, reg))
        advance(length)

    def compare(reg_value: int, value: int, length: int) -> None:
        result = _u8(reg_value - value)
        s.sr &= ~(FLAG_C | FLAG_N | FLAG_Z)
        if reg_value >= value:
            s.sr |= FLAG_C
        s.sr = _set_nz(s.sr, result)
        advance(length)

    load_modes: dict[int, tuple[str, str, int]] = {
        0xA9: ("ac", "imm", 2), 0xA5: ("ac", "zp", 2), 0xB5: ("ac", "zpx", 2),
        0xAD: ("ac", "abs", 3), 0xBD: ("ac", "absx", 3), 0xB9: ("ac", "absy", 3),
        0xA1: ("ac", "indx", 2), 0xB1: ("ac", "indy", 2),
        0xA2: ("xr", "imm", 2), 0xA6: ("xr", "zp", 2), 0xB6: ("xr", "zpy", 2),
        0xAE: ("xr", "abs", 3), 0xBE: ("xr", "absy", 3),
        0xA0: ("yr", "imm", 2), 0xA4: ("yr", "zp", 2), 0xB4: ("yr", "zpx", 2),
        0xAC: ("yr", "abs", 3), 0xBC: ("yr", "absx", 3),
    }
    if op in load_modes:
        reg, mode, length = load_modes[op]
        load(reg, _operand_value(mem, state, bytes_, mode), length)
        return StepPlan(key, s, verify)

    store_modes: dict[int, tuple[str, str, int]] = {
        0x85: ("ac", "zp", 2), 0x95: ("ac", "zpx", 2), 0x8D: ("ac", "abs", 3),
        0x9D: ("ac", "absx", 3), 0x99: ("ac", "absy", 3), 0x81: ("ac", "indx", 2),
        0x91: ("ac", "indy", 2), 0x86: ("xr", "zp", 2), 0x96: ("xr", "zpy", 2),
        0x8E: ("xr", "abs", 3), 0x84: ("yr", "zp", 2), 0x94: ("yr", "zpx", 2),
        0x8C: ("yr", "abs", 3),
    }
    if op in store_modes:
        reg, mode, length = store_modes[op]
        address = _store_address(mem, state, bytes_, mode)
        value = getattr(state, reg)
        mem.note_write(address, value)
        verify.append((address, value))
        advance(length)
        return StepPlan(key, s, verify)

    compare_modes: dict[int, tuple[str, str, int]] = {
        0xC9: ("ac", "imm", 2), 0xC5: ("ac", "zp", 2), 0xD5: ("ac", "zpx", 2),
        0xCD: ("ac", "abs", 3), 0xDD: ("ac", "absx", 3), 0xD9: ("ac", "absy", 3),
        0xC1: ("ac", "indx", 2), 0xD1: ("ac", "indy", 2),
        0xE0: ("xr", "imm", 2), 0xE4: ("xr", "zp", 2), 0xEC: ("xr", "abs", 3),
        0xC0: ("yr", "imm", 2), 0xC4: ("yr", "zp", 2), 0xCC: ("yr", "abs", 3),
    }
    if op in compare_modes:
        reg, mode, length = compare_modes[op]
        compare(getattr(state, reg), _operand_value(mem, state, bytes_, mode), length)
        return StepPlan(key, s, verify)

    logic_modes: dict[int, tuple[str, int, Callable[[int, int], int]]] = {
        0x29: ("imm", 2, lambda a, b: a & b), 0x25: ("zp", 2, lambda a, b: a & b),
        0x2D: ("abs", 3, lambda a, b: a & b), 0x09: ("imm", 2, lambda a, b: a | b),
        0x05: ("zp", 2, lambda a, b: a | b), 0x0D: ("abs", 3, lambda a, b: a | b),
        0x49: ("imm", 2, lambda a, b: a ^ b), 0x45: ("zp", 2, lambda a, b: a ^ b),
        0x4D: ("abs", 3, lambda a, b: a ^ b),
    }
    if op in logic_modes:
        mode, length, func = logic_modes[op]
        s.ac = _u8(func(state.ac, _operand_value(mem, state, bytes_, mode)))
        s.sr = _set_nz(s.sr, s.ac)
        advance(length)
        return StepPlan(key, s, verify)

    if op in (0x69, 0x65, 0x6D) and not (state.sr & FLAG_D):
        mode, length = {0x69: ("imm", 2), 0x65: ("zp", 2), 0x6D: ("abs", 3)}[op]
        value = _operand_value(mem, state, bytes_, mode)
        carry = 1 if state.sr & FLAG_C else 0
        total = state.ac + value + carry
        result = _u8(total)
        s.sr &= ~(FLAG_C | FLAG_V | FLAG_N | FLAG_Z)
        if total > 0xFF:
            s.sr |= FLAG_C
        if (~(state.ac ^ value) & (state.ac ^ result) & 0x80) != 0:
            s.sr |= FLAG_V
        s.ac = result
        s.sr = _set_nz(s.sr, s.ac)
        advance(length)
        return StepPlan(key, s, verify)

    if op == 0x24:  # BIT zp
        value = mem.read(_zp(bytes_))
        s.sr &= ~(FLAG_N | FLAG_V | FLAG_Z)
        s.sr |= value & (FLAG_N | FLAG_V)
        if (state.ac & value) == 0:
            s.sr |= FLAG_Z
        advance(2)
        return StepPlan(key, s, verify)
    if op == 0x2C:  # BIT abs
        value = mem.read(_abs(bytes_))
        s.sr &= ~(FLAG_N | FLAG_V | FLAG_Z)
        s.sr |= value & (FLAG_N | FLAG_V)
        if (state.ac & value) == 0:
            s.sr |= FLAG_Z
        advance(3)
        return StepPlan(key, s, verify)

    transfers = {
        0xAA: ("xr", state.ac), 0xA8: ("yr", state.ac),
        0x8A: ("ac", state.xr), 0x98: ("ac", state.yr),
        0xBA: ("xr", state.sp),
    }
    if op in transfers:
        reg, value = transfers[op]
        setattr(s, reg, _u8(value))
        s.sr = _set_nz(s.sr, getattr(s, reg))
        advance(1)
        return StepPlan(key, s, verify)
    if op == 0x9A:  # TXS
        s.sp = state.xr
        advance(1)
        return StepPlan(key, s, verify)

    incdec_reg = {
        0xE8: ("xr", 1), 0xC8: ("yr", 1), 0xCA: ("xr", -1), 0x88: ("yr", -1),
    }
    if op in incdec_reg:
        reg, delta = incdec_reg[op]
        setattr(s, reg, _u8(getattr(state, reg) + delta))
        s.sr = _set_nz(s.sr, getattr(s, reg))
        advance(1)
        return StepPlan(key, s, verify)

    incdec_mem = {
        0xE6: ("zp", 2, 1), 0xF6: ("zpx", 2, 1), 0xEE: ("abs", 3, 1),
        0xFE: ("absx", 3, 1), 0xC6: ("zp", 2, -1), 0xD6: ("zpx", 2, -1),
        0xCE: ("abs", 3, -1), 0xDE: ("absx", 3, -1),
    }
    if op in incdec_mem:
        mode, length, delta = incdec_mem[op]
        address = _store_address(mem, state, bytes_, mode)
        value = _u8(mem.read(address) + delta)
        mem.note_write(address, value)
        verify.append((address, value))
        s.sr = _set_nz(s.sr, value)
        advance(length)
        return StepPlan(key, s, verify)

    flag_ops = {
        0x18: (FLAG_C, False), 0x38: (FLAG_C, True),
        0x58: (FLAG_I, False), 0x78: (FLAG_I, True),
        0xD8: (FLAG_D, False), 0xF8: (FLAG_D, True),
        0xB8: (FLAG_V, False),
    }
    if op in flag_ops:
        flag, enabled = flag_ops[op]
        if enabled:
            s.sr |= flag
        else:
            s.sr &= ~flag
        advance(1)
        return StepPlan(key, s, verify)

    branches = {
        0x10: not (state.sr & FLAG_N), 0x30: bool(state.sr & FLAG_N),
        0x50: not (state.sr & FLAG_V), 0x70: bool(state.sr & FLAG_V),
        0x90: not (state.sr & FLAG_C), 0xB0: bool(state.sr & FLAG_C),
        0xD0: not (state.sr & FLAG_Z), 0xF0: bool(state.sr & FLAG_Z),
    }
    if op in branches:
        s.pc = _rel(pc, bytes_[1]) if branches[op] else _u16(pc + 2)
        return StepPlan(key, s, verify)

    if op == 0xEA:
        advance(1)
        return StepPlan(key, s, verify)
    if op == 0x4C:
        s.pc = _abs(bytes_)
        return StepPlan(key, s, verify)
    if op == 0x6C:
        ptr = _abs(bytes_)
        s.pc = mem.read_word_6502_bug(ptr)
        return StepPlan(key, s, verify)
    if op == 0x20:
        s.pc = _abs(bytes_)
        s.sp = _u8(state.sp - 2)
        key = "T"
        return StepPlan(key, s, verify)

    return None


def assert_footer(session: "mt.MonitorSession", expected: CpuState, context: str) -> CpuState:
    actual = CpuState.from_footer(session)
    diffs = []
    if actual.pc != expected.pc:
        diffs.append(f"PC expected {expected.pc:04X} got {actual.pc:04X}")
    if actual.sp != expected.sp:
        diffs.append(f"SP expected {expected.sp:02X} got {actual.sp:02X}")
    if actual.ac != expected.ac:
        diffs.append(f"AC expected {expected.ac:02X} got {actual.ac:02X}")
    if actual.xr != expected.xr:
        diffs.append(f"XR expected {expected.xr:02X} got {actual.xr:02X}")
    if actual.yr != expected.yr:
        diffs.append(f"YR expected {expected.yr:02X} got {actual.yr:02X}")
    # BRK capture sets the B bit; compare the stable status bits we model.
    mask = FLAG_N | FLAG_V | FLAG_D | FLAG_I | FLAG_Z | FLAG_C
    if (actual.sr & mask) != (expected.sr & mask):
        diffs.append(f"SR expected {expected.sr:08b} got {actual.sr:08b}")
    if diffs:
        raise mt.Failure(f"{context}: footer mismatch: {', '.join(diffs)}")
    return actual


def assert_ux(session: "mt.MonitorSession", expected_pc: int, context: str) -> None:
    header = dbg._header_line(session)
    if "MONITOR ASM" not in header or "Dbg" not in header:
        raise mt.Failure(f"{context}: expected ASM Debug header, got {header!r}")
    if f"${expected_pc:04X}" not in header:
        # The user may have intentionally jumped away in another view; returning
        # to ASM must snap back to the current debug PC.
        session.send_char("A")
        header = dbg._header_line(session)
        if f"${expected_pc:04X}" not in header:
            raise mt.Failure(f"{context}: ASM header did not follow PC {expected_pc:04X}: {header!r}")
    labels = dbg._footer_header_line(session)
    for token in ("PC", "SP", "AC", "XR", "YR", "NV-BDIZC", "IRQ", "NMI"):
        if token not in labels:
            raise mt.Failure(f"{context}: footer label {token!r} missing: {labels!r}")


def dismiss_debug_cancelled(session: "mt.MonitorSession") -> None:
    """Close the informational popup shown when G stops at a breakpoint."""
    if "DEBUG CANCELLED" not in session.capture().text():
        return
    session.send_key("ENTER")
    if "DEBUG CANCELLED" in session.capture().text():
        raise mt.Failure("Could not dismiss DEBUG CANCELLED popup")


def status_cpu(session: "mt.MonitorSession") -> int:
    snap = session.capture()
    for y in range(mt.HEIGHT):
        match = re.search(r"CPU([0-7])", snap.line(y))
        if match:
            return int(match.group(1))
    raise mt.Failure(f"No CPU status line found:\n{snap.text()}")


def set_cpu(session: "mt.MonitorSession", cpu: int) -> None:
    dbg._ensure_no_debug(session)
    for _ in range(8):
        if status_cpu(session) == cpu:
            return
        session.send_char("o")
    raise mt.Failure(f"Could not switch monitor CPU bank to CPU{cpu}")


def verify_rom_mapping(rest_host: str) -> None:
    basic = mt.read_rest_memory(rest_host, 0xA000, 0x2000)
    kernal = mt.read_rest_memory(rest_host, 0xE000, 0x2000)
    if basic[0:2] != b"\x94\xE3":
        raise mt.Failure(f"BASIC ROM signature mismatch at $A000: {basic[0:2].hex().upper()}")
    if kernal[-6:] == b"\x00" * 6:
        raise mt.Failure("KERNAL vector area read as all zeroes; ROM mapping is not usable")


def write_bootstrap(rest_host: str, target: int, seed: int) -> CpuState:
    ac = (0x20 + seed * 17) & 0xFF
    xr = (0x40 + seed * 29) & 0xFF
    yr = (0x01 + seed * 7) & 0x7F
    program = bytes([
        0xA9, 0x37, 0x8D, 0x01, 0x00,       # force live $0001 to BASIC/KERNAL ROM
        0xD8, 0x18, 0x78, 0xB8,             # CLD / CLC / SEI / CLV
        0xA2, 0xF8, 0x9A,                   # deterministic SP for footer checks
        0xA9, ac, 0xA2, xr, 0xA0, yr,       # known registers
        0x4C, target & 0xFF, target >> 8,   # JMP target
    ])
    mt.write_rest_memory(rest_host, BOOTSTRAP_ADDR, program)
    sr = FLAG_B | FLAG_I
    sr = _set_nz(sr, yr)
    return CpuState(pc=target, sp=0xF8, ac=ac, xr=xr, yr=yr, sr=sr)


def enter_routine(session: "mt.MonitorSession", rest_host: str, routine: Routine,
                  seed: int) -> CpuState:
    expected = write_bootstrap(rest_host, routine.address, seed)
    dbg._reopen_monitor(session)
    set_cpu(session, 7)
    session.goto(f"{routine.address:04X}")
    session.send_char("A")
    session.send_char("D")
    session.send_char("R")
    session.goto(f"{BOOTSTRAP_ADDR:04X}")
    session.send_char("G")
    dbg._wait_for_pc(session, f"{routine.address:04X}")
    dismiss_debug_cancelled(session)
    actual = assert_footer(session, expected, f"{routine.name} entry")
    assert_ux(session, routine.address, f"{routine.name} entry")
    return actual


def exercise_debug_ux(session: "mt.MonitorSession", current: CpuState, iteration: int) -> None:
    if iteration % 11 == 0:
        session.send_key("F3")
        text = session.capture().text()
        for token in ("D Debug/Over", "T Trace", "O Out", "C=+X Reset", "RSTOP"):
            if token not in text:
                raise mt.Failure(f"Debug help missing {token!r} during soak:\n{text}")
        session.send_key("ESC")
    if iteration % 17 == 0:
        session.send_char("M")
        session.goto(f"{SCRATCH_ADDR:04X}")
        session.send_char("A")
        header = dbg._header_line(session)
        if f"${current.pc:04X}" not in header:
            raise mt.Failure(f"ASM did not return to debug PC {current.pc:04X}: {header!r}")
    if iteration % 23 == 0:
        session.last_command = "CBM_R"
        session.sock.sendall(b"\x1bR")
        text = session.capture().text()
        if "BREAKPOINTS" not in text:
            raise mt.Failure(f"Breakpoint popup missing during soak:\n{text}")
        session.send_key("ESC")


def run_soak(args: argparse.Namespace, rest_host: str, session: "mt.MonitorSession") -> None:
    original_port = mt.read_rest_memory(rest_host, 0x0001, 1)[0]

    with mt.check("Debug soak: verify BASIC/KERNAL ROM mapping"):
        verify_rom_mapping(rest_host)

    rng = random.Random(args.seed)
    deadline = time.time() + args.duration
    iteration = 0
    validated_steps = 0
    restarts = 0
    mem = Memory(rest_host)

    try:
        while time.time() < deadline:
            routine = rng.choice(ROUTINES)
            restarts += 1
            state = enter_routine(session, rest_host, routine, restarts)
            max_steps = rng.randint(4, args.max_steps_per_entry)

            for _ in range(max_steps):
                if time.time() >= deadline:
                    break
                iteration += 1
                exercise_debug_ux(session, state, iteration)
                plan = emulate(mem, state)
                if plan is None:
                    break
                session.send_char(plan.key)
                dbg._wait_for_pc(session, f"{plan.expected.pc:04X}")
                state = assert_footer(
                    session, plan.expected,
                    f"{routine.name} step {validated_steps + 1} via {plan.key}",
                )
                assert_ux(session, state.pc, f"{routine.name} step {validated_steps + 1}")
                for address, value in plan.verify:
                    actual = mt.read_rest_memory(rest_host, address, 1)[0]
                    if actual != value:
                        raise mt.Failure(
                            f"{routine.name}: memory ${address:04X} expected ${value:02X}, got ${actual:02X}")
                validated_steps += 1

            if validated_steps and validated_steps % args.progress_every == 0:
                remaining = max(0.0, deadline - time.time())
                print(
                    f"soak progress: {validated_steps} validated steps, "
                    f"{restarts} entries, {remaining:.0f}s remaining",
                    flush=True,
                )
    finally:
        active_error = sys.exc_info()[1]
        try:
            dismiss_debug_cancelled(session)
            dbg._ensure_no_debug(session)
        except Exception as exc:
            if active_error is None:
                raise
            print(f"cleanup warning: {mt.format_exception(exc)}", file=sys.stderr)
        finally:
            mt.write_rest_memory(rest_host, 0x0001, bytes([original_port]))

    if validated_steps < args.min_validated_steps:
        raise mt.Failure(
            f"Soak only validated {validated_steps} steps; expected at least {args.min_validated_steps}")
    print(f"debug_soak_test: OK ({validated_steps} validated steps across {restarts} entries)")


def main() -> int:
    parser = argparse.ArgumentParser(description="Soak the U64 machine monitor debugger over Telnet")
    parser.add_argument("--host", default=os.environ.get("U64_MONITOR_HOST", "u64"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("U64_MONITOR_PORT", "23")))
    parser.add_argument("--rest-host", default=os.environ.get("U64_MONITOR_REST_HOST"))
    parser.add_argument("--password", default=os.environ.get("U64_MONITOR_PASSWORD"))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("U64_MONITOR_TIMEOUT", "5.0")))
    parser.add_argument("--duration", type=float, default=float(os.environ.get("U64_MONITOR_SOAK_SECS", DEFAULT_DURATION)))
    parser.add_argument("--seed", type=int, default=int(os.environ.get("U64_MONITOR_SOAK_SEED", "1541")))
    parser.add_argument("--max-steps-per-entry", type=int, default=24)
    parser.add_argument("--min-validated-steps", type=int, default=50)
    parser.add_argument("--progress-every", type=int, default=100)
    args = parser.parse_args()

    rest_host = args.rest_host or args.host
    session: Optional[mt.MonitorSession] = None
    try:
        session = mt.MonitorSession(args.host, args.port, args.password, args.timeout)
        run_soak(args, rest_host, session)
        return 0
    except (mt.Failure, OSError, TimeoutError, urllib.error.URLError) as exc:
        print(mt.format_exception(exc), file=sys.stderr)
        if session is not None:
            try:
                print("\nFinal screen:", file=sys.stderr)
                print(session.capture().text(), file=sys.stderr)
            except Exception:
                pass
        return 1
    finally:
        if session is not None:
            session.close()


if __name__ == "__main__":
    raise SystemExit(main())
