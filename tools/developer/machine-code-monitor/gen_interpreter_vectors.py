#!/usr/bin/env python3
"""Generate differential test vectors for the parked-step linear interpreter.

The firmware's BrkDebugSession::interpret_simple_linear executes every
documented non-control-flow instruction while the CPU stays parked; it is the
production stepping path for RAM under ROM and visible ROM. This script sweeps
that instruction set through the independent mcm6502 oracle (itself
cross-validated against VICE) and emits a C header of vectors consumed by
machine_monitor_debug_test.cc (test_parked_interpreter_matches_mcm6502_vectors).

Regenerate with:
    python3 tools/developer/machine-code-monitor/gen_interpreter_vectors.py \
        > software/test/monitor/monitor_debug_interpreter_vectors.h

Vector constraints (mirrored by the host test):
  * PC is always $E900 (KERNAL ROM) so the interpreter path is exercised.
  * Data addresses stay inside $0002-$9FFF plus the $0100 stack page, so the
    fake machine's flat RAM and the oracle's flat memory agree byte-for-byte,
    and the $00/$01 CPU port is never touched.
  * Every seeded memory cell and every expected post-value fits the fixed
    4-entry tables in the emitted struct.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mcm6502 as ORC  # noqa: E402

PC = 0xE900
U = 0x20
FLAG_C, FLAG_Z, FLAG_I, FLAG_D = 0x01, 0x02, 0x04, 0x08
FLAG_V, FLAG_N = 0x40, 0x80
BASE_SR = U | FLAG_I  # parked debug contexts run with I masked

CONTROL_FLOW = {0x00, 0x20, 0x4C, 0x6C, 0x60, 0x40,
                0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0}

# Effective-address plans per oracle mode name. Each entry:
#   (case-tag, operand bytes builder, pre-mem builder, register overrides)
# 'value' is the memory operand the case wants at the effective address.
ZP_ADDR = 0x80
ZP_WRAP_BASE, ZP_WRAP_IDX = 0xF4, 0x30      # wraps to $24
ABS_ADDR = 0x6234
ABSI_BASE, ABSI_IDX = 0x6134, 0x05          # no page cross
ABSI_CROSS_BASE, ABSI_CROSS_IDX = 0x61F0, 0x30  # crosses to $6220
INDX_ZP, INDX_X, INDX_TARGET = 0x40, 0x04, 0x6320
INDX_WRAPPTR_ZP, INDX_WRAPPTR_X = 0xFA, 0x04    # pointer at $FE/$FF
INDY_ZP, INDY_TARGET = 0x44, 0x6340
INDY_CROSS_Y = 0xF0


def plans_for_mode(mode):
    """Return a list of (tag, bytes_fn, mem_fn, reg_overrides).

    bytes_fn(value) -> list of operand bytes
    mem_fn(value)   -> list of (addr, byte) seeds incl. the operand cell
    """
    if mode == "_imm":
        return [("imm", lambda v: [v], lambda v: [], {})]
    if mode == "_zp":
        return [("zp", lambda v: [ZP_ADDR], lambda v: [(ZP_ADDR, v)], {})]
    if mode == "_zpx":
        return [
            ("zpx", lambda v: [ZP_ADDR], lambda v: [((ZP_ADDR + 5) & 0xFF, v)],
             {"x": 5}),
            ("zpx-wrap", lambda v: [ZP_WRAP_BASE],
             lambda v: [((ZP_WRAP_BASE + ZP_WRAP_IDX) & 0xFF, v)],
             {"x": ZP_WRAP_IDX}),
        ]
    if mode == "_zpy":
        return [
            ("zpy", lambda v: [ZP_ADDR], lambda v: [((ZP_ADDR + 5) & 0xFF, v)],
             {"y": 5}),
            ("zpy-wrap", lambda v: [ZP_WRAP_BASE],
             lambda v: [((ZP_WRAP_BASE + ZP_WRAP_IDX) & 0xFF, v)],
             {"y": ZP_WRAP_IDX}),
        ]
    if mode == "_abs":
        return [("abs", lambda v: [ABS_ADDR & 0xFF, ABS_ADDR >> 8],
                 lambda v: [(ABS_ADDR, v)], {})]
    if mode == "_absx":
        return [
            ("absx", lambda v: [ABSI_BASE & 0xFF, ABSI_BASE >> 8],
             lambda v: [(ABSI_BASE + ABSI_IDX, v)], {"x": ABSI_IDX}),
            ("absx-cross", lambda v: [ABSI_CROSS_BASE & 0xFF, ABSI_CROSS_BASE >> 8],
             lambda v: [(ABSI_CROSS_BASE + ABSI_CROSS_IDX, v)],
             {"x": ABSI_CROSS_IDX}),
        ]
    if mode == "_absy":
        return [
            ("absy", lambda v: [ABSI_BASE & 0xFF, ABSI_BASE >> 8],
             lambda v: [(ABSI_BASE + ABSI_IDX, v)], {"y": ABSI_IDX}),
            ("absy-cross", lambda v: [ABSI_CROSS_BASE & 0xFF, ABSI_CROSS_BASE >> 8],
             lambda v: [(ABSI_CROSS_BASE + ABSI_CROSS_IDX, v)],
             {"y": ABSI_CROSS_IDX}),
        ]
    if mode == "_indx":
        return [
            ("indx", lambda v: [INDX_ZP],
             lambda v: [((INDX_ZP + INDX_X) & 0xFF, INDX_TARGET & 0xFF),
                        ((INDX_ZP + INDX_X + 1) & 0xFF, INDX_TARGET >> 8),
                        (INDX_TARGET, v)],
             {"x": INDX_X}),
            ("indx-hiptr", lambda v: [INDX_WRAPPTR_ZP],
             lambda v: [(0xFE, INDX_TARGET & 0xFF),
                        (0xFF, INDX_TARGET >> 8),
                        (INDX_TARGET, v)],
             {"x": INDX_WRAPPTR_X}),
        ]
    if mode == "_indy":
        return [
            ("indy", lambda v: [INDY_ZP],
             lambda v: [(INDY_ZP, INDY_TARGET & 0xFF),
                        (INDY_ZP + 1, INDY_TARGET >> 8),
                        (INDY_TARGET + 5, v)],
             {"y": 5}),
            ("indy-cross", lambda v: [INDY_ZP],
             lambda v: [(INDY_ZP, INDY_TARGET & 0xFF),
                        (INDY_ZP + 1, INDY_TARGET >> 8),
                        (INDY_TARGET + INDY_CROSS_Y, v)],
             {"y": INDY_CROSS_Y}),
        ]
    raise ValueError(f"unhandled mode {mode}")


def reg_cases(mnem):
    """Return a list of (case-tag, reg dict, sr-extra, mem-operand-value).

    'mem-operand-value' is None for pure register/implied instructions.
    """
    if mnem in ("LDA", "LDX", "LDY"):
        return [(f"v{v:02X}", {}, 0, v) for v in (0x00, 0x80, 0x41)]
    if mnem == "STA":
        return [(f"a{v:02X}", {"a": v}, 0, 0xEE) for v in (0x00, 0x80, 0x37)]
    if mnem == "STX":
        return [(f"x{v:02X}", {"x": v}, 0, 0xEE) for v in (0x00, 0x80, 0x37)]
    if mnem == "STY":
        return [(f"y{v:02X}", {"y": v}, 0, 0xEE) for v in (0x00, 0x80, 0x37)]
    if mnem in ("ORA", "AND", "EOR"):
        return [(f"a{a:02X}m{m:02X}", {"a": a}, 0, m)
                for a, m in ((0x0F, 0xF0), (0xFF, 0xFF), (0x55, 0xAA), (0x00, 0x00))]
    if mnem == "ADC":
        cases = []
        for a, m, c in ((0x50, 0x10, 0), (0x50, 0x50, 0), (0xFF, 0x01, 0),
                        (0x7F, 0x01, 0), (0x80, 0xFF, 1), (0x3F, 0x40, 1)):
            cases.append((f"bin-a{a:02X}m{m:02X}c{c}", {"a": a},
                          FLAG_C if c else 0, m))
        for a, m, c in ((0x09, 0x01, 0), (0x50, 0x50, 0), (0x99, 0x01, 0),
                        (0x0F, 0x01, 0), (0x99, 0x99, 1), (0x00, 0x00, 0)):
            cases.append((f"bcd-a{a:02X}m{m:02X}c{c}", {"a": a},
                          FLAG_D | (FLAG_C if c else 0), m))
        return cases
    if mnem == "SBC":
        cases = []
        for a, m, c in ((0x50, 0x10, 1), (0x50, 0xB0, 1), (0x00, 0x01, 1),
                        (0x80, 0x01, 1), (0x7F, 0xFF, 0), (0x40, 0x40, 1)):
            cases.append((f"bin-a{a:02X}m{m:02X}c{c}", {"a": a},
                          FLAG_C if c else 0, m))
        for a, m, c in ((0x00, 0x01, 1), (0x50, 0x25, 1), (0x99, 0x99, 1),
                        (0x0A, 0x0F, 0), (0x20, 0x01, 0), (0x00, 0x00, 1)):
            cases.append((f"bcd-a{a:02X}m{m:02X}c{c}", {"a": a},
                          FLAG_D | (FLAG_C if c else 0), m))
        return cases
    if mnem in ("CMP", "CPX", "CPY"):
        reg = {"CMP": "a", "CPX": "x", "CPY": "y"}[mnem]
        return [(f"r{r:02X}m{m:02X}", {reg: r}, 0, m)
                for r, m in ((0x10, 0x20), (0x20, 0x20), (0x30, 0x20),
                             (0x80, 0x7F), (0x00, 0xFF))]
    if mnem == "BIT":
        return [(f"m{m:02X}", {"a": 0x0F}, 0, m)
                for m in (0xC0, 0x40, 0x80, 0x00, 0x3F)]
    if mnem in ("ASL", "LSR", "ROL", "ROR"):
        cases = []
        for m in (0x80, 0x01, 0xAA, 0x55):
            for c in (0, 1):
                cases.append((f"m{m:02X}c{c}",
                              {"a": m},  # used only by accumulator form
                              FLAG_C if c else 0, m))
        return cases
    if mnem in ("INC", "DEC"):
        return [(f"m{m:02X}", {}, 0, m) for m in (0xFF, 0x7F, 0x00)]
    if mnem in ("INX", "INY", "DEX", "DEY"):
        reg = "x" if mnem[-1] == "X" else "y"
        return [(f"r{v:02X}", {reg: v}, 0, None) for v in (0xFF, 0x7F, 0x00)]
    if mnem in ("TAX", "TAY"):
        return [(f"a{v:02X}", {"a": v}, 0, None) for v in (0x00, 0x80, 0x41)]
    if mnem == "TXA":
        return [(f"x{v:02X}", {"x": v}, 0, None) for v in (0x00, 0x80, 0x41)]
    if mnem == "TYA":
        return [(f"y{v:02X}", {"y": v}, 0, None) for v in (0x00, 0x80, 0x41)]
    if mnem == "TSX":
        return [(f"s{v:02X}", {"sp": v}, 0, None) for v in (0x00, 0x80, 0xFD)]
    if mnem == "TXS":
        return [(f"x{v:02X}", {"x": v}, 0, None) for v in (0x00, 0x80, 0xFD)]
    if mnem == "PHA":
        return [("sp-FD", {"a": 0x5A, "sp": 0xFD}, 0, None),
                ("sp-00-wrap", {"a": 0xA5, "sp": 0x00}, 0, None)]
    if mnem == "PHP":
        return [("flags-NC", {"sp": 0xFD}, FLAG_N | FLAG_C, None),
                ("flags-DV", {"sp": 0xFD}, FLAG_D | FLAG_V, None),
                ("sp-00-wrap", {"sp": 0x00}, FLAG_Z, None)]
    if mnem == "PLA":
        return [(f"v{v:02X}", {"sp": 0xF0}, 0, ("stack", v))
                for v in (0x00, 0x80, 0x41)] + \
               [("sp-FF-wrap", {"sp": 0xFF}, 0, ("stack", 0x77))]
    if mnem == "PLP":
        return [(f"v{v:02X}", {"sp": 0xF0}, 0, ("stack", v))
                for v in (0xFF, 0x00, FLAG_C | FLAG_Z | FLAG_D,
                          FLAG_N | FLAG_V)]
    if mnem in ("CLC", "SEC"):
        return [("from-set", {}, FLAG_C, None), ("from-clear", {}, 0, None)]
    if mnem in ("CLI", "SEI"):
        # BASE_SR already sets I; add a from-clear case by masking later.
        return [("from-set", {}, 0, None), ("from-clear-i", {}, -FLAG_I, None)]
    if mnem == "CLV":
        return [("from-set", {}, FLAG_V, None), ("from-clear", {}, 0, None)]
    if mnem in ("CLD", "SED"):
        return [("from-set", {}, FLAG_D, None), ("from-clear", {}, 0, None)]
    if mnem == "NOP":
        return [("plain", {}, 0, None)]
    raise ValueError(f"unhandled mnemonic {mnem}")


def run_oracle(op, operand, pre_mem, a, x, y, sp, sr, label, length,
               expect_linear):
    """Execute one instruction on a fresh oracle and package a vector."""
    cpu = ORC.CPU6502()
    cpu.mem[PC] = op
    for i, b in enumerate(operand):
        cpu.mem[PC + 1 + i] = b
    for addr, byte in pre_mem:
        cpu.mem[addr] = byte
    cpu.set_state(a, x, y, sp, PC, sr)
    result = cpu.step()

    post_mem = []
    seen = set()
    for ev in result.writes:
        if ev.addr in seen:
            continue
        seen.add(ev.addr)
        post_mem.append((ev.addr, cpu.mem[ev.addr]))
    if len(post_mem) > 4 or len(pre_mem) > 4:
        raise ValueError(f"memory table overflow in {label}")
    return {
        "op": op, "len": length,
        "b1": operand[0] if len(operand) > 0 else 0,
        "b2": operand[1] if len(operand) > 1 else 0,
        "a": a, "x": x, "y": y, "sp": sp, "sr": sr,
        "pre_mem": list(pre_mem),
        "ea": cpu.a, "ex": cpu.x, "ey": cpu.y,
        "esp": cpu.sp, "esr": cpu.p,
        "epc": result.pc_after,
        "post_mem": post_mem,
        "label": label,
        "linear": expect_linear,
    }


def build_control_flow_vectors():
    """Vectors for the parked control-flow emulation path.

    Covers JSR (Step Into consumes the JSR architecturally, including the
    pushed return bytes), JMP abs, JMP (ind) incl. the NMOS page-wrap bug,
    all 8 branches taken and not taken (forward and backward), RTS (with the
    real-JSR caller byte the firmware's active-frame guard requires), and RTI
    (SR normalization: pulled SR gets U set, B cleared).
    """
    vectors = []

    # JSR $8000: pushes (PC+2) hi then lo. Normal SP and SP wrap-around.
    for sp, tag in ((0xF0, "sp-F0"), (0x00, "sp-00-wrap")):
        vectors.append(run_oracle(
            0x20, [0x00, 0x80], [], 0x11, 0x22, 0x33, sp, BASE_SR,
            f"JSR-20-{tag}", 3, 0))

    # JMP $8123.
    vectors.append(run_oracle(
        0x4C, [0x23, 0x81], [], 0x11, 0x22, 0x33, 0xF0, BASE_SR,
        "JMP-4C-abs", 3, 0))

    # JMP ($60FE) straight and JMP ($60FF) NMOS page-wrap (high vector byte
    # is fetched from $6000, not $6100).
    vectors.append(run_oracle(
        0x6C, [0xFE, 0x60], [(0x60FE, 0x23), (0x60FF, 0x81)],
        0x11, 0x22, 0x33, 0xF0, BASE_SR, "JMPI-6C-straight", 3, 0))
    vectors.append(run_oracle(
        0x6C, [0xFF, 0x60], [(0x60FF, 0x23), (0x6000, 0x81), (0x6100, 0x55)],
        0x11, 0x22, 0x33, 0xF0, BASE_SR, "JMPI-6C-pagewrap", 3, 0))

    # Branches: (opcode, flag mask, taken when flag set?).
    branches = [
        (0x10, FLAG_N, False, "BPL"), (0x30, FLAG_N, True, "BMI"),
        (0x50, FLAG_V, False, "BVC"), (0x70, FLAG_V, True, "BVS"),
        (0x90, FLAG_C, False, "BCC"), (0xB0, FLAG_C, True, "BCS"),
        (0xD0, FLAG_Z, False, "BNE"), (0xF0, FLAG_Z, True, "BEQ"),
    ]
    for op, mask, taken_when_set, name in branches:
        for offset, otag in ((0x10, "fwd"), (0xF0, "back")):
            for flag_set in (False, True):
                sr = BASE_SR | (mask if flag_set else 0)
                taken = "taken" if (flag_set == taken_when_set) else "nottaken"
                vectors.append(run_oracle(
                    op, [offset], [], 0x11, 0x22, 0x33, 0xF0, sr,
                    f"{name}-{op:02X}-{otag}-f{int(flag_set)}-{taken}", 2, 0))

    # RTS: return address on the stack; the firmware's active-frame guard
    # requires a real JSR opcode at (return - 2), so seed one.
    for sp, s1, s2, tag in ((0xF0, 0x01F1, 0x01F2, "sp-F0"),
                            (0xFE, 0x01FF, 0x0100, "sp-FE-wrap")):
        ret = 0x8005
        vectors.append(run_oracle(
            0x60, [], [(s1, ret & 0xFF), (s2, ret >> 8), (ret - 2, 0x20)],
            0x11, 0x22, 0x33, sp, BASE_SR, f"RTS-60-{tag}", 1, 0))

    # RTI: pulls SR (U forced set, B cleared) then PC.
    for pulled_sr, tag in ((0xFF, "sr-FF"), (0x00, "sr-00"),
                           (FLAG_C | FLAG_Z | FLAG_D, "sr-CZD"),
                           (FLAG_N | FLAG_V, "sr-NV")):
        vectors.append(run_oracle(
            0x40, [], [(0x01F1, pulled_sr), (0x01F2, 0x23), (0x01F3, 0x81)],
            0x11, 0x22, 0x33, 0xF0, BASE_SR, f"RTI-40-{tag}", 1, 0))

    return vectors


def build_vectors():
    probe = ORC.CPU6502()
    vectors = []
    opcodes = sorted(op for op in probe.table if op not in CONTROL_FLOW)
    for op in opcodes:
        mnem, mode_name, _handler, _cyc, _addpc = probe.table[op]
        accumulator_form = mode_name is None and mnem in ("ASL", "LSR", "ROL", "ROR")
        plans = ([("acc", lambda v: [], lambda v: [], {})]
                 if mode_name is None else plans_for_mode(mode_name))
        for rtag, regs, sr_extra, opval in reg_cases(mnem):
            for ptag, bytes_fn, mem_fn, plan_regs in plans:
                # Immediate-mode stores do not exist; plans always match table.
                stack_seed = None
                if isinstance(opval, tuple) and opval[0] == "stack":
                    stack_seed = opval[1]
                    value = None
                elif opval is None:
                    value = None
                else:
                    value = opval

                a = regs.get("a", 0x11)
                x = plan_regs.get("x", regs.get("x", 0x22))
                y = plan_regs.get("y", regs.get("y", 0x33))
                sp = regs.get("sp", 0xF0)
                if "x" in regs and "x" in plan_regs:
                    # Indexed plan owns X; skip conflicting register case.
                    continue
                if "y" in regs and "y" in plan_regs:
                    continue
                sr = BASE_SR
                if sr_extra == -FLAG_I:
                    sr &= ~FLAG_I & 0xFF
                else:
                    sr |= sr_extra

                operand = bytes_fn(value) if value is not None else bytes_fn(0)
                pre_mem = list(mem_fn(value)) if value is not None else []
                if stack_seed is not None:
                    pre_mem.append((0x0100 | ((sp + 1) & 0xFF), stack_seed))

                if len(pre_mem) > 4:
                    raise ValueError("pre_mem overflow")

                cpu = ORC.CPU6502()
                cpu.mem[PC] = op
                for i, b in enumerate(operand):
                    cpu.mem[PC + 1 + i] = b
                for addr, byte in pre_mem:
                    cpu.mem[addr] = byte
                cpu.set_state(a, x, y, sp, PC, sr)
                result = cpu.step()

                post_mem = []
                seen = set()
                for ev in result.writes:
                    if ev.addr in seen:
                        continue
                    seen.add(ev.addr)
                    post_mem.append((ev.addr, cpu.mem[ev.addr]))
                if len(post_mem) > 4:
                    raise ValueError("post_mem overflow")

                label = f"{mnem}-{op:02X}-{ptag}-{rtag}"
                vectors.append({
                    "op": op, "len": 1 + len(operand),
                    "b1": operand[0] if len(operand) > 0 else 0,
                    "b2": operand[1] if len(operand) > 1 else 0,
                    "a": a, "x": x, "y": y, "sp": sp, "sr": sr,
                    "pre_mem": pre_mem,
                    "ea": cpu.a, "ex": cpu.x, "ey": cpu.y,
                    "esp": cpu.sp, "esr": cpu.p,
                    "epc": result.pc_after,
                    "post_mem": post_mem,
                    "label": label,
                    "linear": 1,
                })
                if accumulator_form:
                    break  # plans loop (single acc plan anyway)
    vectors.extend(build_control_flow_vectors())
    return vectors


def emit(vectors):
    out = []
    out.append("// Generated by tools/developer/machine-code-monitor/"
               "gen_interpreter_vectors.py.")
    out.append("// Do not hand-edit. Regenerate with:")
    out.append("//   python3 tools/developer/machine-code-monitor/"
               "gen_interpreter_vectors.py \\")
    out.append("//       > software/test/monitor/"
               "monitor_debug_interpreter_vectors.h")
    out.append("// Differential vectors: parked-step linear interpreter vs the")
    out.append("// independent mcm6502 oracle (VICE-cross-validated).")
    out.append("#ifndef MONITOR_DEBUG_INTERPRETER_VECTORS_H")
    out.append("#define MONITOR_DEBUG_INTERPRETER_VECTORS_H")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("struct InterpreterVectorMem { uint16_t addr; uint8_t value; };")
    out.append("")
    out.append("struct InterpreterVector {")
    out.append("    uint8_t opcode;")
    out.append("    uint8_t length;")
    out.append("    uint8_t operand1;")
    out.append("    uint8_t operand2;")
    out.append("    uint8_t a, x, y, sp, sr;")
    out.append("    uint8_t pre_mem_count;")
    out.append("    InterpreterVectorMem pre_mem[4];")
    out.append("    uint8_t expect_a, expect_x, expect_y, expect_sp, expect_sr;")
    out.append("    uint16_t expect_pc;")
    out.append("    uint8_t post_mem_count;")
    out.append("    InterpreterVectorMem post_mem[4];")
    out.append("    uint8_t expect_linear;")
    out.append("    const char *label;")
    out.append("};")
    out.append("")
    out.append(f"static const uint16_t interpreter_vector_pc = 0x{PC:04X};")
    out.append(f"static const int interpreter_vector_count = {len(vectors)};")
    out.append("")
    out.append("static const InterpreterVector interpreter_vectors[] = {")
    for v in vectors:
        pre = ", ".join(f"{{0x{a:04X}, 0x{b:02X}}}" for a, b in v["pre_mem"])
        pre = pre if pre else "{0, 0}"
        post = ", ".join(f"{{0x{a:04X}, 0x{b:02X}}}" for a, b in v["post_mem"])
        post = post if post else "{0, 0}"
        out.append(
            "    { "
            f"0x{v['op']:02X}, {v['len']}, 0x{v['b1']:02X}, 0x{v['b2']:02X}, "
            f"0x{v['a']:02X}, 0x{v['x']:02X}, 0x{v['y']:02X}, "
            f"0x{v['sp']:02X}, 0x{v['sr']:02X}, "
            f"{len(v['pre_mem'])}, {{{pre}}}, "
            f"0x{v['ea']:02X}, 0x{v['ex']:02X}, 0x{v['ey']:02X}, "
            f"0x{v['esp']:02X}, 0x{v['esr']:02X}, 0x{v['epc']:04X}, "
            f"{len(v['post_mem'])}, {{{post}}}, "
            f"{v['linear']}, \"{v['label']}\" }},")
    out.append("};")
    out.append("")
    out.append("#endif // MONITOR_DEBUG_INTERPRETER_VECTORS_H")
    return "\n".join(out) + "\n"


def main() -> int:
    vectors = build_vectors()
    ops = {v["op"] for v in vectors}
    sys.stdout.write(emit(vectors))
    print(f"// vectors={len(vectors)} opcodes={len(ops)}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
