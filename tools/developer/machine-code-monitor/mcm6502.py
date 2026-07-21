#!/usr/bin/env python3
"""Independent NMOS 6502/6510 instruction oracle for the Machine Code Monitor gate.

This is a from-scratch, self-contained CPU emulator. It deliberately shares NO
code with the firmware debugger (monitor_debug*.cc) or its host predictor
(monitor_debug_predictor.cc): it is a truth source, so it must be derived only
from the published NMOS 6502/6510 architecture, not from the implementation
under test.

Scope and fidelity:
  * All 151 documented opcodes, every addressing mode.
  * Per-flag SR (N V - B D I Z C); the unused bit 5 reads as 1.
  * NMOS decimal-mode ADC/SBC, including the NMOS-specific N/V/Z behaviour
    (ADC: Z from the binary sum, N/V from the partially BCD-adjusted
    intermediate; SBC: all flags identical to binary mode, only A differs).
  * JMP (ind) page-boundary wrap bug ($xxFF reads the high byte from $xx00).
  * Exact stack semantics for JSR/RTS/RTI/BRK/PHA/PLA/PHP/PLP including the
    pushed P having B=1,bit5=1 for PHP/BRK and B ignored on pull.
  * Page-cross detection for indexed reads and branch targets (exposed for the
    page-cross oracle; cycle counts are also produced but are advisory only).
  * Memory reads/writes are recorded so the memory oracle can diff side effects.
  * Undocumented opcodes are classified (never silently executed): step() raises
    UndocumentedOpcode so the harness can refuse clearly and prove clean state.

Memory model: a flat 64 KiB array. Banking is handled by the *caller*: seed the
array with exactly the bytes the live CPU sees at its current $01 (e.g. KERNAL
ROM bytes at $E000 when KERNAL is mapped), step once, then diff only the bytes
the instruction is expected to touch. The oracle itself is bank-agnostic; the
source/bank oracle lives in the harness.

Run `python3 mcm6502.py --selftest` for the built-in correctness vectors.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Tuple

# Status flag bit masks.
C = 0x01  # carry
Z = 0x02  # zero
I = 0x04  # interrupt disable
D = 0x08  # decimal
B = 0x10  # break (only meaningful in pushed copies)
U = 0x20  # unused, always reads 1
V = 0x40  # overflow
N = 0x80  # negative


class UndocumentedOpcode(Exception):
    """Raised when step() encounters an opcode outside the documented 151."""

    def __init__(self, opcode: int, pc: int):
        self.opcode = opcode
        self.pc = pc
        super().__init__(f"undocumented opcode ${opcode:02X} at ${pc:04X}")


@dataclass
class MemEvent:
    addr: int
    value: int
    is_write: bool


@dataclass
class StepResult:
    """Architectural delta produced by executing exactly one instruction."""
    opcode: int
    mnemonic: str
    mode: str
    pc_before: int
    pc_after: int
    bytes_consumed: int
    cycles: int
    page_crossed: bool
    branch_taken: Optional[bool]
    reads: List[MemEvent] = field(default_factory=list)
    writes: List[MemEvent] = field(default_factory=list)


class CPU6502:
    def __init__(self, mem: Optional[bytearray] = None):
        self.mem = mem if mem is not None else bytearray(0x10000)
        if len(self.mem) != 0x10000:
            raise ValueError("memory must be exactly 64 KiB")
        self.a = 0
        self.x = 0
        self.y = 0
        self.sp = 0xFD
        self.pc = 0
        # status without B/U; we track the 6 real flags + carry as one byte,
        # keeping bit5 set and B clear in the live register (B only exists in
        # pushed copies on the NMOS part).
        self.p = U | I
        self._reads: List[MemEvent] = []
        self._writes: List[MemEvent] = []
        self._build_table()

    # ---- state helpers -------------------------------------------------
    def set_state(self, a: int, x: int, y: int, sp: int, pc: int, p: int) -> None:
        self.a = a & 0xFF
        self.x = x & 0xFF
        self.y = y & 0xFF
        self.sp = sp & 0xFF
        self.pc = pc & 0xFFFF
        self.p = (p | U) & 0xFF  # force unused bit; keep whatever B caller gave

    def status_str(self) -> str:
        bits = "NV-BDIZC"
        masks = [N, V, U, B, D, I, Z, C]
        return "".join(b if (self.p & m) else "." for b, m in zip(bits, masks))

    # ---- memory --------------------------------------------------------
    def _read(self, addr: int) -> int:
        addr &= 0xFFFF
        val = self.mem[addr]
        self._reads.append(MemEvent(addr, val, False))
        return val

    def _write(self, addr: int, val: int) -> None:
        addr &= 0xFFFF
        val &= 0xFF
        self.mem[addr] = val
        self._writes.append(MemEvent(addr, val, True))

    def _read_word(self, addr: int) -> int:
        return self._read(addr) | (self._read((addr + 1) & 0xFFFF) << 8)

    def _read_word_zp(self, addr: int) -> int:
        # zero-page indirect: high byte wraps within the zero page.
        lo = self._read(addr & 0xFF)
        hi = self._read((addr + 1) & 0xFF)
        return lo | (hi << 8)

    def _push(self, val: int) -> None:
        self._write(0x0100 | self.sp, val & 0xFF)
        self.sp = (self.sp - 1) & 0xFF

    def _pull(self) -> int:
        self.sp = (self.sp + 1) & 0xFF
        return self._read(0x0100 | self.sp)

    # ---- flag helpers --------------------------------------------------
    def _set_flag(self, mask: int, on: bool) -> None:
        if on:
            self.p |= mask
        else:
            self.p &= ~mask & 0xFF

    def _set_zn(self, val: int) -> None:
        val &= 0xFF
        self._set_flag(Z, val == 0)
        self._set_flag(N, (val & 0x80) != 0)

    # ---- addressing modes ---------------------------------------------
    # each returns (address, page_crossed). Operand bytes are consumed off PC.
    def _imm(self) -> Tuple[int, bool]:
        addr = self.pc
        self.pc = (self.pc + 1) & 0xFFFF
        return addr, False

    def _zp(self) -> Tuple[int, bool]:
        addr = self._read(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        return addr, False

    def _zpx(self) -> Tuple[int, bool]:
        base = self._read(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        return (base + self.x) & 0xFF, False

    def _zpy(self) -> Tuple[int, bool]:
        base = self._read(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        return (base + self.y) & 0xFF, False

    def _abs(self) -> Tuple[int, bool]:
        addr = self._read_word(self.pc)
        self.pc = (self.pc + 2) & 0xFFFF
        return addr, False

    def _absx(self) -> Tuple[int, bool]:
        base = self._read_word(self.pc)
        self.pc = (self.pc + 2) & 0xFFFF
        addr = (base + self.x) & 0xFFFF
        return addr, (base & 0xFF00) != (addr & 0xFF00)

    def _absy(self) -> Tuple[int, bool]:
        base = self._read_word(self.pc)
        self.pc = (self.pc + 2) & 0xFFFF
        addr = (base + self.y) & 0xFFFF
        return addr, (base & 0xFF00) != (addr & 0xFF00)

    def _indx(self) -> Tuple[int, bool]:
        zp = self._read(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        addr = self._read_word_zp((zp + self.x) & 0xFF)
        return addr, False

    def _indy(self) -> Tuple[int, bool]:
        zp = self._read(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        base = self._read_word_zp(zp)
        addr = (base + self.y) & 0xFFFF
        return addr, (base & 0xFF00) != (addr & 0xFF00)

    # ---- ALU core ------------------------------------------------------
    def _adc(self, val: int) -> None:
        a = self.a
        carry = 1 if (self.p & C) else 0
        if self.p & D:
            # NMOS decimal ADC.
            al = (a & 0x0F) + (val & 0x0F) + carry
            if al >= 0x0A:
                al = ((al + 0x06) & 0x0F) + 0x10
            inter = (a & 0xF0) + (val & 0xF0) + al  # may exceed 0xFF; signed-ish
            # NMOS: Z from the *binary* sum; N/V from the intermediate.
            bin_sum = a + val + carry
            self._set_flag(Z, (bin_sum & 0xFF) == 0)
            self._set_flag(N, (inter & 0x80) != 0)
            self._set_flag(V, ((~(a ^ val) & (a ^ inter)) & 0x80) != 0)
            if inter >= 0xA0:
                inter += 0x60
            self._set_flag(C, inter >= 0x100)
            self.a = inter & 0xFF
        else:
            total = a + val + carry
            result = total & 0xFF
            self._set_flag(C, total > 0xFF)
            self._set_flag(V, ((~(a ^ val) & (a ^ result)) & 0x80) != 0)
            self.a = result
            self._set_zn(result)

    def _sbc(self, val: int) -> None:
        a = self.a
        carry = 1 if (self.p & C) else 0
        # Binary subtraction drives N,V,Z,C in BOTH modes on the NMOS part.
        total = a - val - (1 - carry)
        result = total & 0xFF
        self._set_flag(C, total >= 0)
        self._set_flag(V, (((a ^ val) & (a ^ result)) & 0x80) != 0)
        self._set_zn(result)
        if self.p & D:
            al = (a & 0x0F) - (val & 0x0F) + carry - 1
            if al < 0:
                al = ((al - 0x06) & 0x0F) - 0x10
            inter = (a & 0xF0) - (val & 0xF0) + al
            if inter < 0:
                inter -= 0x60
            self.a = inter & 0xFF
        else:
            self.a = result

    def _cmp_reg(self, reg: int, val: int) -> None:
        diff = (reg - val) & 0x1FF
        self._set_flag(C, reg >= val)
        self._set_zn(diff & 0xFF)

    def _branch(self, cond: bool) -> Tuple[bool, bool, int]:
        off = self._read(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        if off & 0x80:
            off -= 0x100
        if cond:
            target = (self.pc + off) & 0xFFFF
            crossed = (self.pc & 0xFF00) != (target & 0xFF00)
            self.pc = target
            return True, crossed, 1 + (1 if crossed else 0)
        return False, False, 0

    # ---- instruction table --------------------------------------------
    def _build_table(self) -> None:
        # table[opcode] = (mnemonic, mode_fn_name or None, handler, base_cycles,
        #                  add_cycle_on_page_cross)
        t: Dict[int, Tuple] = {}

        def reg_load(setter):
            def fn(addr, pc_op):
                val = self._read(addr)
                setter(val)
                self._set_zn(val)
            return fn

        # We define handlers inline per opcode group below. To keep this honest
        # and exhaustive, each opcode is registered explicitly.

        # LDA
        for op, mode, cyc, pc in [
            (0xA9, "_imm", 2, False), (0xA5, "_zp", 3, False),
            (0xB5, "_zpx", 4, False), (0xAD, "_abs", 4, False),
            (0xBD, "_absx", 4, True), (0xB9, "_absy", 4, True),
            (0xA1, "_indx", 6, False), (0xB1, "_indy", 5, True)]:
            t[op] = ("LDA", mode, self._op_lda, cyc, pc)
        # LDX
        for op, mode, cyc, pc in [
            (0xA2, "_imm", 2, False), (0xA6, "_zp", 3, False),
            (0xB6, "_zpy", 4, False), (0xAE, "_abs", 4, False),
            (0xBE, "_absy", 4, True)]:
            t[op] = ("LDX", mode, self._op_ldx, cyc, pc)
        # LDY
        for op, mode, cyc, pc in [
            (0xA0, "_imm", 2, False), (0xA4, "_zp", 3, False),
            (0xB4, "_zpx", 4, False), (0xAC, "_abs", 4, False),
            (0xBC, "_absx", 4, True)]:
            t[op] = ("LDY", mode, self._op_ldy, cyc, pc)
        # STA
        for op, mode, cyc in [
            (0x85, "_zp", 3), (0x95, "_zpx", 4), (0x8D, "_abs", 4),
            (0x9D, "_absx", 5), (0x99, "_absy", 5), (0x81, "_indx", 6),
            (0x91, "_indy", 6)]:
            t[op] = ("STA", mode, self._op_sta, cyc, False)
        # STX
        for op, mode, cyc in [(0x86, "_zp", 3), (0x96, "_zpy", 4), (0x8E, "_abs", 4)]:
            t[op] = ("STX", mode, self._op_stx, cyc, False)
        # STY
        for op, mode, cyc in [(0x84, "_zp", 3), (0x94, "_zpx", 4), (0x8C, "_abs", 4)]:
            t[op] = ("STY", mode, self._op_sty, cyc, False)
        # ADC
        for op, mode, cyc, pc in [
            (0x69, "_imm", 2, False), (0x65, "_zp", 3, False),
            (0x75, "_zpx", 4, False), (0x6D, "_abs", 4, False),
            (0x7D, "_absx", 4, True), (0x79, "_absy", 4, True),
            (0x61, "_indx", 6, False), (0x71, "_indy", 5, True)]:
            t[op] = ("ADC", mode, self._op_adc, cyc, pc)
        # SBC
        for op, mode, cyc, pc in [
            (0xE9, "_imm", 2, False), (0xE5, "_zp", 3, False),
            (0xF5, "_zpx", 4, False), (0xED, "_abs", 4, False),
            (0xFD, "_absx", 4, True), (0xF9, "_absy", 4, True),
            (0xE1, "_indx", 6, False), (0xF1, "_indy", 5, True)]:
            t[op] = ("SBC", mode, self._op_sbc, cyc, pc)
        # AND / ORA / EOR
        for op, mode, cyc, pc in [
            (0x29, "_imm", 2, False), (0x25, "_zp", 3, False),
            (0x35, "_zpx", 4, False), (0x2D, "_abs", 4, False),
            (0x3D, "_absx", 4, True), (0x39, "_absy", 4, True),
            (0x21, "_indx", 6, False), (0x31, "_indy", 5, True)]:
            t[op] = ("AND", mode, self._op_and, cyc, pc)
        for op, mode, cyc, pc in [
            (0x09, "_imm", 2, False), (0x05, "_zp", 3, False),
            (0x15, "_zpx", 4, False), (0x0D, "_abs", 4, False),
            (0x1D, "_absx", 4, True), (0x19, "_absy", 4, True),
            (0x01, "_indx", 6, False), (0x11, "_indy", 5, True)]:
            t[op] = ("ORA", mode, self._op_ora, cyc, pc)
        for op, mode, cyc, pc in [
            (0x49, "_imm", 2, False), (0x45, "_zp", 3, False),
            (0x55, "_zpx", 4, False), (0x4D, "_abs", 4, False),
            (0x5D, "_absx", 4, True), (0x59, "_absy", 4, True),
            (0x41, "_indx", 6, False), (0x51, "_indy", 5, True)]:
            t[op] = ("EOR", mode, self._op_eor, cyc, pc)
        # CMP / CPX / CPY
        for op, mode, cyc, pc in [
            (0xC9, "_imm", 2, False), (0xC5, "_zp", 3, False),
            (0xD5, "_zpx", 4, False), (0xCD, "_abs", 4, False),
            (0xDD, "_absx", 4, True), (0xD9, "_absy", 4, True),
            (0xC1, "_indx", 6, False), (0xD1, "_indy", 5, True)]:
            t[op] = ("CMP", mode, self._op_cmp, cyc, pc)
        for op, mode, cyc in [(0xE0, "_imm", 2), (0xE4, "_zp", 3), (0xEC, "_abs", 4)]:
            t[op] = ("CPX", mode, self._op_cpx, cyc, False)
        for op, mode, cyc in [(0xC0, "_imm", 2), (0xC4, "_zp", 3), (0xCC, "_abs", 4)]:
            t[op] = ("CPY", mode, self._op_cpy, cyc, False)
        # BIT
        for op, mode, cyc in [(0x24, "_zp", 3), (0x2C, "_abs", 4)]:
            t[op] = ("BIT", mode, self._op_bit, cyc, False)
        # Shifts/rotates (accumulator + memory)
        t[0x0A] = ("ASL", None, self._op_asl_acc, 2, False)
        t[0x4A] = ("LSR", None, self._op_lsr_acc, 2, False)
        t[0x2A] = ("ROL", None, self._op_rol_acc, 2, False)
        t[0x6A] = ("ROR", None, self._op_ror_acc, 2, False)
        for op, mode, cyc in [(0x06, "_zp", 5), (0x16, "_zpx", 6), (0x0E, "_abs", 6), (0x1E, "_absx", 7)]:
            t[op] = ("ASL", mode, self._op_asl_mem, cyc, False)
        for op, mode, cyc in [(0x46, "_zp", 5), (0x56, "_zpx", 6), (0x4E, "_abs", 6), (0x5E, "_absx", 7)]:
            t[op] = ("LSR", mode, self._op_lsr_mem, cyc, False)
        for op, mode, cyc in [(0x26, "_zp", 5), (0x36, "_zpx", 6), (0x2E, "_abs", 6), (0x3E, "_absx", 7)]:
            t[op] = ("ROL", mode, self._op_rol_mem, cyc, False)
        for op, mode, cyc in [(0x66, "_zp", 5), (0x76, "_zpx", 6), (0x6E, "_abs", 6), (0x7E, "_absx", 7)]:
            t[op] = ("ROR", mode, self._op_ror_mem, cyc, False)
        # INC/DEC memory
        for op, mode, cyc in [(0xE6, "_zp", 5), (0xF6, "_zpx", 6), (0xEE, "_abs", 6), (0xFE, "_absx", 7)]:
            t[op] = ("INC", mode, self._op_inc, cyc, False)
        for op, mode, cyc in [(0xC6, "_zp", 5), (0xD6, "_zpx", 6), (0xCE, "_abs", 6), (0xDE, "_absx", 7)]:
            t[op] = ("DEC", mode, self._op_dec, cyc, False)
        # INX/INY/DEX/DEY
        t[0xE8] = ("INX", None, self._op_inx, 2, False)
        t[0xC8] = ("INY", None, self._op_iny, 2, False)
        t[0xCA] = ("DEX", None, self._op_dex, 2, False)
        t[0x88] = ("DEY", None, self._op_dey, 2, False)
        # Flag ops
        t[0x18] = ("CLC", None, lambda a, p: self._set_flag(C, False), 2, False)
        t[0x38] = ("SEC", None, lambda a, p: self._set_flag(C, True), 2, False)
        t[0x58] = ("CLI", None, lambda a, p: self._set_flag(I, False), 2, False)
        t[0x78] = ("SEI", None, lambda a, p: self._set_flag(I, True), 2, False)
        t[0xD8] = ("CLD", None, lambda a, p: self._set_flag(D, False), 2, False)
        t[0xF8] = ("SED", None, lambda a, p: self._set_flag(D, True), 2, False)
        t[0xB8] = ("CLV", None, lambda a, p: self._set_flag(V, False), 2, False)
        # Transfers
        t[0xAA] = ("TAX", None, self._op_tax, 2, False)
        t[0x8A] = ("TXA", None, self._op_txa, 2, False)
        t[0xA8] = ("TAY", None, self._op_tay, 2, False)
        t[0x98] = ("TYA", None, self._op_tya, 2, False)
        t[0xBA] = ("TSX", None, self._op_tsx, 2, False)
        t[0x9A] = ("TXS", None, self._op_txs, 2, False)
        # Stack
        t[0x48] = ("PHA", None, self._op_pha, 3, False)
        t[0x68] = ("PLA", None, self._op_pla, 4, False)
        t[0x08] = ("PHP", None, self._op_php, 3, False)
        t[0x28] = ("PLP", None, self._op_plp, 4, False)
        # Branches
        t[0x10] = ("BPL", None, lambda a, p: self._branch(not (self.p & N)), 2, False)
        t[0x30] = ("BMI", None, lambda a, p: self._branch(bool(self.p & N)), 2, False)
        t[0x50] = ("BVC", None, lambda a, p: self._branch(not (self.p & V)), 2, False)
        t[0x70] = ("BVS", None, lambda a, p: self._branch(bool(self.p & V)), 2, False)
        t[0x90] = ("BCC", None, lambda a, p: self._branch(not (self.p & C)), 2, False)
        t[0xB0] = ("BCS", None, lambda a, p: self._branch(bool(self.p & C)), 2, False)
        t[0xD0] = ("BNE", None, lambda a, p: self._branch(not (self.p & Z)), 2, False)
        t[0xF0] = ("BEQ", None, lambda a, p: self._branch(bool(self.p & Z)), 2, False)
        # Jumps / subroutines / returns
        t[0x4C] = ("JMP", "_abs", self._op_jmp, 3, False)
        t[0x6C] = ("JMP", None, self._op_jmp_ind, 5, False)
        t[0x20] = ("JSR", None, self._op_jsr, 6, False)
        t[0x60] = ("RTS", None, self._op_rts, 6, False)
        t[0x40] = ("RTI", None, self._op_rti, 6, False)
        t[0x00] = ("BRK", None, self._op_brk, 7, False)
        t[0xEA] = ("NOP", None, lambda a, p: None, 2, False)
        self.table = t

    # ---- per-op handlers ----------------------------------------------
    def _op_lda(self, addr, pc):
        self.a = self._read(addr); self._set_zn(self.a)
    def _op_ldx(self, addr, pc):
        self.x = self._read(addr); self._set_zn(self.x)
    def _op_ldy(self, addr, pc):
        self.y = self._read(addr); self._set_zn(self.y)
    def _op_sta(self, addr, pc):
        self._write(addr, self.a)
    def _op_stx(self, addr, pc):
        self._write(addr, self.x)
    def _op_sty(self, addr, pc):
        self._write(addr, self.y)
    def _op_adc(self, addr, pc):
        self._adc(self._read(addr))
    def _op_sbc(self, addr, pc):
        self._sbc(self._read(addr))
    def _op_and(self, addr, pc):
        self.a &= self._read(addr); self._set_zn(self.a)
    def _op_ora(self, addr, pc):
        self.a |= self._read(addr); self._set_zn(self.a)
    def _op_eor(self, addr, pc):
        self.a ^= self._read(addr); self._set_zn(self.a)
    def _op_cmp(self, addr, pc):
        self._cmp_reg(self.a, self._read(addr))
    def _op_cpx(self, addr, pc):
        self._cmp_reg(self.x, self._read(addr))
    def _op_cpy(self, addr, pc):
        self._cmp_reg(self.y, self._read(addr))
    def _op_bit(self, addr, pc):
        val = self._read(addr)
        self._set_flag(Z, (self.a & val) == 0)
        self._set_flag(N, (val & 0x80) != 0)
        self._set_flag(V, (val & 0x40) != 0)

    def _shift_common(self, val, left, rotate):
        carry_in = 1 if (self.p & C) else 0
        if left:
            new_c = (val & 0x80) != 0
            res = ((val << 1) | (carry_in if rotate else 0)) & 0xFF
        else:
            new_c = (val & 0x01) != 0
            res = ((val >> 1) | ((carry_in << 7) if rotate else 0)) & 0xFF
        self._set_flag(C, new_c)
        self._set_zn(res)
        return res
    def _op_asl_acc(self, a, p): self.a = self._shift_common(self.a, True, False)
    def _op_lsr_acc(self, a, p): self.a = self._shift_common(self.a, False, False)
    def _op_rol_acc(self, a, p): self.a = self._shift_common(self.a, True, True)
    def _op_ror_acc(self, a, p): self.a = self._shift_common(self.a, False, True)
    def _op_asl_mem(self, addr, pc): self._write(addr, self._shift_common(self._read(addr), True, False))
    def _op_lsr_mem(self, addr, pc): self._write(addr, self._shift_common(self._read(addr), False, False))
    def _op_rol_mem(self, addr, pc): self._write(addr, self._shift_common(self._read(addr), True, True))
    def _op_ror_mem(self, addr, pc): self._write(addr, self._shift_common(self._read(addr), False, True))
    def _op_inc(self, addr, pc):
        val = (self._read(addr) + 1) & 0xFF; self._write(addr, val); self._set_zn(val)
    def _op_dec(self, addr, pc):
        val = (self._read(addr) - 1) & 0xFF; self._write(addr, val); self._set_zn(val)
    def _op_inx(self, a, p): self.x = (self.x + 1) & 0xFF; self._set_zn(self.x)
    def _op_iny(self, a, p): self.y = (self.y + 1) & 0xFF; self._set_zn(self.y)
    def _op_dex(self, a, p): self.x = (self.x - 1) & 0xFF; self._set_zn(self.x)
    def _op_dey(self, a, p): self.y = (self.y - 1) & 0xFF; self._set_zn(self.y)
    def _op_tax(self, a, p): self.x = self.a; self._set_zn(self.x)
    def _op_txa(self, a, p): self.a = self.x; self._set_zn(self.a)
    def _op_tay(self, a, p): self.y = self.a; self._set_zn(self.y)
    def _op_tya(self, a, p): self.a = self.y; self._set_zn(self.a)
    def _op_tsx(self, a, p): self.x = self.sp; self._set_zn(self.x)
    def _op_txs(self, a, p): self.sp = self.x  # TXS affects no flags
    def _op_pha(self, a, p): self._push(self.a)
    def _op_pla(self, a, p): self.a = self._pull(); self._set_zn(self.a)
    def _op_php(self, a, p): self._push(self.p | B | U)  # B,bit5 set in pushed copy
    def _op_plp(self, a, p):
        self.p = (self._pull() | U) & ~B & 0xFF | (self.p & 0)  # B not retained in live reg
        self.p |= U
    def _op_jmp(self, addr, pc): self.pc = addr
    def _op_jmp_ind(self, a, p):
        ptr = self._read_word(self.pc)
        self.pc = (self.pc + 2) & 0xFFFF
        # NMOS page-wrap bug: high byte fetched from same page.
        lo = self._read(ptr)
        hi = self._read((ptr & 0xFF00) | ((ptr + 1) & 0x00FF))
        self.pc = lo | (hi << 8)
    def _op_jsr(self, a, p):
        target = self._read_word(self.pc)
        ret = (self.pc + 1) & 0xFFFF  # JSR pushes addr of last operand byte (PC+2-1)
        self._push((ret >> 8) & 0xFF)
        self._push(ret & 0xFF)
        self.pc = target
    def _op_rts(self, a, p):
        lo = self._pull(); hi = self._pull()
        self.pc = ((hi << 8) | lo) + 1 & 0xFFFF
    def _op_rti(self, a, p):
        self.p = (self._pull() | U) & ~B & 0xFF
        self.p |= U
        lo = self._pull(); hi = self._pull()
        self.pc = (hi << 8) | lo
    def _op_brk(self, a, p):
        # BRK is a 2-byte instruction: the pushed PC skips the padding byte.
        ret = (self.pc + 1) & 0xFFFF
        self._push((ret >> 8) & 0xFF)
        self._push(ret & 0xFF)
        self._push(self.p | B | U)
        self._set_flag(I, True)
        self.pc = self._read_word(0xFFFE)

    # ---- public step ---------------------------------------------------
    def step(self) -> StepResult:
        self._reads = []
        self._writes = []
        pc0 = self.pc
        opcode = self._read(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        entry = self.table.get(opcode)
        if entry is None:
            # Undocumented: refuse. Restore PC so caller sees a clean pre-state.
            self.pc = pc0
            raise UndocumentedOpcode(opcode, pc0)
        mnem, mode_name, handler, cyc, add_pc = entry
        page_crossed = False
        branch_taken = None
        addr = None
        if mode_name is not None:
            addr, page_crossed = getattr(self, mode_name)()
        # branch handlers and a few specials return their own info
        if mnem in ("BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ"):
            taken, crossed, extra = handler(None, None)
            branch_taken = taken
            page_crossed = crossed
            cyc += extra
        else:
            handler(addr, pc0)
        self.p |= U
        self.p &= ~B & 0xFF  # B never set in the live register on NMOS
        if add_pc and page_crossed:
            cyc += 1
        return StepResult(
            opcode=opcode, mnemonic=mnem, mode=mode_name or "impl",
            pc_before=pc0, pc_after=self.pc, bytes_consumed=(self.pc - pc0) & 0xFFFF,
            cycles=cyc, page_crossed=page_crossed, branch_taken=branch_taken,
            reads=list(self._reads), writes=list(self._writes))


# Documented opcode set, for the generator and the undocumented classifier.
def documented_opcodes() -> set:
    return set(CPU6502().table.keys())


def is_documented(opcode: int) -> bool:
    return opcode in documented_opcodes()


# ----------------------------------------------------------------------------
# Self-test: hand-computed vectors covering the tricky NMOS behaviours.
# ----------------------------------------------------------------------------
def _selftest() -> int:
    failures = []

    def check(name, cond, detail=""):
        if not cond:
            failures.append(f"{name}: {detail}")

    def run(setup_mem, a=0, x=0, y=0, sp=0xFD, pc=0x1000, p=U | I):
        cpu = CPU6502()
        for addr, byte in setup_mem.items():
            cpu.mem[addr] = byte
        cpu.set_state(a, x, y, sp, pc, p)
        res = cpu.step()
        return cpu, res

    # 1. Coverage: exactly 151 documented opcodes.
    check("opcode-count", len(documented_opcodes()) == 151,
          f"got {len(documented_opcodes())}")

    # 2. LDA immediate sets Z/N.
    cpu, _ = run({0x1000: 0xA9, 0x1001: 0x00})
    check("lda-zero", cpu.a == 0 and (cpu.p & Z) and not (cpu.p & N))
    cpu, _ = run({0x1000: 0xA9, 0x1001: 0x80})
    check("lda-neg", cpu.a == 0x80 and (cpu.p & N) and not (cpu.p & Z))

    # 3. ADC binary with overflow: 0x50 + 0x50 = 0xA0, V=1, N=1, C=0.
    cpu, _ = run({0x1000: 0x69, 0x1001: 0x50}, a=0x50, p=U)
    check("adc-ovf", cpu.a == 0xA0 and (cpu.p & V) and (cpu.p & N) and not (cpu.p & C),
          cpu.status_str())
    # carry out: 0xFF + 0x01 = 0x00, C=1, Z=1
    cpu, _ = run({0x1000: 0x69, 0x1001: 0x01}, a=0xFF, p=U)
    check("adc-carry", cpu.a == 0x00 and (cpu.p & C) and (cpu.p & Z), cpu.status_str())

    # 4. ADC decimal: 0x09 + 0x01 = 0x10 (BCD), C=0.
    cpu, _ = run({0x1000: 0x69, 0x1001: 0x01}, a=0x09, p=U | D)
    check("adc-bcd-10", cpu.a == 0x10 and not (cpu.p & C), f"a={cpu.a:02X} {cpu.status_str()}")
    # ADC decimal carry: 0x99 + 0x01 = 0x00, C=1, and NMOS Z from binary sum (0x9A!=0)->Z=0.
    cpu, _ = run({0x1000: 0x69, 0x1001: 0x01}, a=0x99, p=U | D)
    check("adc-bcd-99", cpu.a == 0x00 and (cpu.p & C) and not (cpu.p & Z),
          f"a={cpu.a:02X} {cpu.status_str()}")
    # Classic NMOS quirk: 0x05 + 0x05 + C=1 in decimal -> 0x11.
    cpu, _ = run({0x1000: 0x69, 0x1001: 0x05}, a=0x05, p=U | D | C)
    check("adc-bcd-quirk", cpu.a == 0x11, f"a={cpu.a:02X}")

    # 5. SBC binary borrow, no overflow: 0x50 - 0xF0 with C=1 (=+80 - -16 = +96).
    #    => A=0x60, C=0 (borrow), V=0 (result in signed range), N=0.
    cpu, _ = run({0x1000: 0xE9, 0x1001: 0xF0}, a=0x50, p=U | C)
    check("sbc-bin", cpu.a == 0x60 and not (cpu.p & C) and not (cpu.p & V) and not (cpu.p & N),
          cpu.status_str())
    # SBC binary WITH signed overflow: 0x80 - 0x01 C=1 (=-128 - +1) => A=0x7F, C=1, V=1, N=0.
    cpu, _ = run({0x1000: 0xE9, 0x1001: 0x01}, a=0x80, p=U | C)
    check("sbc-ovf", cpu.a == 0x7F and (cpu.p & C) and (cpu.p & V) and not (cpu.p & N),
          cpu.status_str())
    # SBC decimal: 0x46 - 0x12 (C=1) = 0x34.
    cpu, _ = run({0x1000: 0xE9, 0x1001: 0x12}, a=0x46, p=U | D | C)
    check("sbc-bcd", cpu.a == 0x34, f"a={cpu.a:02X}")

    # 6. JMP indirect page wrap: ptr=$30FF -> lo from $30FF, hi from $3000.
    cpu, _ = run({0x1000: 0x6C, 0x1001: 0xFF, 0x1002: 0x30,
                  0x30FF: 0x40, 0x3000: 0x80})
    check("jmp-ind-wrap", cpu.pc == 0x8040, f"pc={cpu.pc:04X}")

    # 7. JSR/RTS round trip: JSR $2000 then at $2000 an RTS returns to $1003.
    cpu = CPU6502()
    for a, b in {0x1000: 0x20, 0x1001: 0x00, 0x1002: 0x20, 0x2000: 0x60}.items():
        cpu.mem[a] = b
    cpu.set_state(0, 0, 0, 0xFF, 0x1000, U | I)
    cpu.step()  # JSR
    check("jsr-pc", cpu.pc == 0x2000, f"pc={cpu.pc:04X}")
    check("jsr-sp", cpu.sp == 0xFD, f"sp={cpu.sp:02X}")
    check("jsr-stack", cpu.mem[0x01FF] == 0x10 and cpu.mem[0x01FE] == 0x02,
          f"{cpu.mem[0x01FF]:02X}{cpu.mem[0x01FE]:02X}")
    cpu.step()  # RTS
    check("rts-pc", cpu.pc == 0x1003, f"pc={cpu.pc:04X}")
    check("rts-sp", cpu.sp == 0xFF, f"sp={cpu.sp:02X}")

    # 8. PHP/PLP: pushed copy has B and bit5 set; PLP restores without B.
    cpu = CPU6502(); cpu.set_state(0, 0, 0, 0xFF, 0x1000, U | C | N)
    cpu.mem[0x1000] = 0x08  # PHP
    cpu.step()
    pushed = cpu.mem[0x01FF]
    check("php-b-set", (pushed & B) and (pushed & U) and (pushed & C) and (pushed & N),
          f"{pushed:02X}")
    cpu.mem[0x1001] = 0x28  # PLP
    cpu.step()
    check("plp-no-b", not (cpu.p & B) and (cpu.p & U), cpu.status_str())

    # 9. BRK: pushes PC+2, P with B set, sets I, jumps to ($FFFE).
    cpu = CPU6502(); cpu.set_state(0, 0, 0, 0xFF, 0x1000, U)
    cpu.mem[0x1000] = 0x00
    cpu.mem[0xFFFE] = 0x00; cpu.mem[0xFFFF] = 0xE0
    cpu.step()
    check("brk-pc", cpu.pc == 0xE000, f"pc={cpu.pc:04X}")
    check("brk-push", cpu.mem[0x01FF] == 0x10 and cpu.mem[0x01FE] == 0x02,
          f"{cpu.mem[0x01FF]:02X}{cpu.mem[0x01FE]:02X}")
    check("brk-i", (cpu.p & I) != 0)
    check("brk-pushed-b", (cpu.mem[0x01FD] & B) != 0, f"{cpu.mem[0x01FD]:02X}")

    # 10. RTI restores P (no B) and PC.
    cpu = CPU6502(); cpu.set_state(0, 0, 0, 0xFC, 0x1000, U)
    cpu.mem[0x1000] = 0x40
    cpu.mem[0x01FD] = U | C  # pulled P
    cpu.mem[0x01FE] = 0x34   # pcl
    cpu.mem[0x01FF] = 0x12   # pch
    cpu.step()
    check("rti-pc", cpu.pc == 0x1234, f"pc={cpu.pc:04X}")
    check("rti-c", (cpu.p & C) and not (cpu.p & B), cpu.status_str())
    check("rti-sp", cpu.sp == 0xFF, f"sp={cpu.sp:02X}")

    # 11. Branch taken with page cross adds cycles + crosses.
    cpu = CPU6502(); cpu.set_state(0, 0, 0, 0xFF, 0x10F0, U)
    cpu.mem[0x10F0] = 0xD0; cpu.mem[0x10F1] = 0x40  # BNE +0x40 -> 0x1132 (cross)
    res = cpu.step()
    check("bne-cross", cpu.pc == 0x1132 and res.page_crossed and res.cycles == 4,
          f"pc={cpu.pc:04X} cyc={res.cycles} cross={res.page_crossed}")
    # not taken
    cpu = CPU6502(); cpu.set_state(0, 0, 0, 0xFF, 0x1000, U | Z)
    cpu.mem[0x1000] = 0xD0; cpu.mem[0x1001] = 0x40
    res = cpu.step()
    check("bne-not-taken", cpu.pc == 0x1002 and res.cycles == 2, f"pc={cpu.pc:04X}")

    # 12. LDA abs,X page cross adds a cycle.
    cpu = CPU6502(); cpu.set_state(0, 0xFF, 0, 0xFF, 0x1000, U)
    cpu.mem[0x1000] = 0xBD; cpu.mem[0x1001] = 0x10; cpu.mem[0x1002] = 0x20  # $2010,X
    cpu.mem[0x210F] = 0xAB
    res = cpu.step()
    check("ldax-cross", cpu.a == 0xAB and res.page_crossed and res.cycles == 5,
          f"a={cpu.a:02X} cyc={res.cycles}")

    # 13. Indexed indirect / indirect indexed.
    cpu = CPU6502(); cpu.set_state(0, 0x04, 0, 0xFF, 0x1000, U)
    cpu.mem[0x1000] = 0xA1; cpu.mem[0x1001] = 0x20  # LDA ($20,X), X=4 -> ($24)
    cpu.mem[0x24] = 0x00; cpu.mem[0x25] = 0x40
    cpu.mem[0x4000] = 0x77
    cpu.step()
    check("indx", cpu.a == 0x77, f"a={cpu.a:02X}")
    cpu = CPU6502(); cpu.set_state(0, 0, 0x10, 0xFF, 0x1000, U)
    cpu.mem[0x1000] = 0xB1; cpu.mem[0x1001] = 0x20  # LDA ($20),Y
    cpu.mem[0x20] = 0xF8; cpu.mem[0x21] = 0x40      # base $40F8 + Y(0x10)=$4108 cross
    cpu.mem[0x4108] = 0x99
    res = cpu.step()
    check("indy-cross", cpu.a == 0x99 and res.page_crossed and res.cycles == 6,
          f"a={cpu.a:02X} cyc={res.cycles}")

    # 14. zero-page indirect wrap: ($FF) reads lo $FF, hi $00.
    cpu = CPU6502(); cpu.set_state(0, 0, 0, 0xFF, 0x1000, U)
    cpu.mem[0x1000] = 0xB1; cpu.mem[0x1001] = 0xFF
    cpu.mem[0xFF] = 0x34; cpu.mem[0x00] = 0x12  # ptr -> $1234
    cpu.mem[0x1234] = 0x55
    cpu.step()
    check("zp-wrap", cpu.a == 0x55, f"a={cpu.a:02X}")

    # 15. ROR memory through carry.
    cpu = CPU6502(); cpu.set_state(0, 0, 0, 0xFF, 0x1000, U | C)
    cpu.mem[0x1000] = 0x66; cpu.mem[0x1001] = 0x40
    cpu.mem[0x40] = 0x01
    cpu.step()
    check("ror-mem", cpu.mem[0x40] == 0x80 and (cpu.p & C),
          f"m={cpu.mem[0x40]:02X} {cpu.status_str()}")

    # 16. CMP sets C/Z/N.
    cpu = CPU6502(); cpu.set_state(0x40, 0, 0, 0xFF, 0x1000, U)
    cpu.mem[0x1000] = 0xC9; cpu.mem[0x1001] = 0x40
    cpu.step()
    check("cmp-eq", (cpu.p & C) and (cpu.p & Z) and not (cpu.p & N), cpu.status_str())

    # 17. BIT copies bit7->N, bit6->V, A&M->Z.
    cpu = CPU6502(); cpu.set_state(0x01, 0, 0, 0xFF, 0x1000, U)
    cpu.mem[0x1000] = 0x24; cpu.mem[0x1001] = 0x40
    cpu.mem[0x40] = 0xC0
    cpu.step()
    check("bit", (cpu.p & N) and (cpu.p & V) and (cpu.p & Z), cpu.status_str())

    # 18. Undocumented opcode refusal leaves a clean pre-state.
    cpu = CPU6502(); cpu.set_state(0, 0, 0, 0xFF, 0x1000, U)
    cpu.mem[0x1000] = 0x02  # JAM/undocumented
    try:
        cpu.step()
        check("undoc-refuse", False, "did not raise")
    except UndocumentedOpcode as e:
        check("undoc-refuse", e.opcode == 0x02 and cpu.pc == 0x1000, f"pc={cpu.pc:04X}")

    # 19. Memory write recorded for STA.
    cpu = CPU6502(); cpu.set_state(0xAA, 0, 0, 0xFF, 0x1000, U)
    cpu.mem[0x1000] = 0x8D; cpu.mem[0x1001] = 0x00; cpu.mem[0x1002] = 0xC0
    res = cpu.step()
    check("sta-write", any(w.addr == 0xC000 and w.value == 0xAA for w in res.writes))

    if failures:
        print("mcm6502 selftest: FAIL")
        for f in failures:
            print(f"  - {f}")
        return 1
    print(f"mcm6502 selftest: OK ({len(documented_opcodes())} opcodes, all vectors pass)")
    return 0


if __name__ == "__main__":
    import sys
    if "--selftest" in sys.argv or len(sys.argv) == 1:
        sys.exit(_selftest())
    print("usage: mcm6502.py --selftest")
