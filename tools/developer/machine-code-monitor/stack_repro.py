#!/usr/bin/env python3
"""Reproduce the KERNAL JSR / run-to-cursor / RTS stack-corruption bug.

Exact user sequence (canonical KERNAL):
  E000 STA $56 ; E002 JSR $BC0F ; E005 LDA $61
  BC0F LDX #$06 ; BC11 LDA $60,X ; BC13 STA $68,X ; BC15 DEX ; BC16 BNE $BC11
  BC18 STX $70 ; BC1A RTS
Step E000 -> step-into JSR -> run-to-cursor (K) to BC1A -> step RTS.
RTS must return to E005. Observe SP / stack return slot at every stop.
"""
import sys
import time

sys.path.insert(0, ".")
import monitor_test as mt          # noqa: E402
import monitor_debug_test as dbg   # noqa: E402

HOST = "u64"
PORT = 23
REST = "u64"
TIMEOUT = 5.0


def footer(session):
    session.capture()
    line = dbg._footer_value_line(session)
    return dbg._parse_footer_values(line)


def stack_ret(sp):
    """Return address RTS will pull, given SP (the program stack pointer)."""
    stack = mt.read_rest_memory(REST, 0x0100, 256)
    return stack[(sp + 1) & 0xFF] | (stack[(sp + 2) & 0xFF] << 8)


def device_alive():
    """Jiffy clock must advance => KERNAL IRQ running => C64 not wedged."""
    try:
        j0 = list(mt.read_rest_memory(REST, 0x00A0, 3))
        time.sleep(0.6)
        j1 = list(mt.read_rest_memory(REST, 0x00A0, 3))
        return j0 != j1, j0, j1
    except Exception as exc:  # noqa: BLE001
        return False, str(exc), None


NO_RESET = "--no-reset" in sys.argv


def one_iteration(session, idx):
    print(f"\n=== iteration {idx} ===", flush=True)
    if not NO_RESET:
        dbg._reset_c64_core(REST)
        snap = dbg._enter_rom_debug_at(session, 0xE000, "KRN",
                                       "stack-repro E000", "$E:KRN")
    else:
        # Accumulate debugger state: stay in one monitor/debug session, just
        # re-seat the debug cursor at E000 without resetting the C64.
        dbg._ensure_no_debug(session)
        session.goto("E000")
        session.send_char("A")
        if "Dbg" not in dbg._header_line(session):
            session.send_char("D")
        session.capture()
    # E000 STA $56 -> E002
    session.send_char("T")
    p = dbg._wait_for_pc(session, "E002")
    print(f"  after STA $56: PC={p['pc']} SP={p['sp']}", flush=True)

    # E002 JSR $BC0F -> step into -> BC0F
    session.send_char("T")
    p = dbg._wait_for_pc(session, "BC0F")
    sp_into = int(p["sp"], 16)
    ret = stack_ret(sp_into)
    print(f"  step-into JSR: PC={p['pc']} SP=${sp_into:02X} stack_ret=${ret:04X} "
          f"(want $E005-pull=$E004)", flush=True)
    ok_into = (ret == 0xE004)

    # cursor to BC1A, run-to-cursor (K) -> BC1A
    session.goto("BC1A")
    session.send_char("K")
    try:
        p = dbg._wait_for_pc(session, "BC1A", timeout=8.0)
    except mt.Failure as exc:
        print(f"  run-to-cursor FAILED to reach BC1A: {exc}", flush=True)
        return {"idx": idx, "phase": "run_to_cursor", "ok": False}
    sp_at_rts = int(p["sp"], 16)
    ret2 = stack_ret(sp_at_rts)
    print(f"  at RTS BC1A: PC={p['pc']} SP=${sp_at_rts:02X} stack_ret=${ret2:04X} "
          f"(want $E004)", flush=True)
    ok_rts_stack = (ret2 == 0xE004)
    ok_sp_balance = (sp_at_rts == sp_into)

    # step the RTS -> expect E005
    timed_out = False
    wrong_pc = None
    session.send_char("T")
    try:
        p = dbg._wait_for_pc(session, "E005", timeout=12.0)
        print(f"  after RTS: PC={p['pc']} SP={p['sp']}  -> RETURNED CORRECTLY", flush=True)
        rts_ok = True
    except mt.Failure:
        rts_ok = False
        f = footer(session)
        wrong_pc = f.get("pc")
        txt = session.capture().text()
        timed_out = "TIMEOUT" in txt or "TIME OUT" in txt
        print(f"  after RTS: PC={wrong_pc} timeout={timed_out}  -> WRONG/HUNG", flush=True)
        if timed_out:
            print("    DEBUG TIMEOUT screen:\n" + txt, flush=True)

    # leave the monitor; check for wedge
    try:
        dbg._ensure_no_debug(session)
    except Exception:
        pass
    if not NO_RESET:
        # close the monitor entirely (Ctrl+O) and check device
        try:
            session.sock.sendall(b"\x0f")  # Ctrl+O closes the monitor
            time.sleep(1.0)
        except Exception:
            pass
    alive, j0, j1 = device_alive()
    print(f"  device alive after leave: {alive} (jiffy {j0}->{j1})", flush=True)

    return {
        "idx": idx, "ok_into_stack": ok_into, "ok_rts_stack": ok_rts_stack,
        "ok_sp_balance": ok_sp_balance, "rts_ok": rts_ok, "timed_out": timed_out,
        "wrong_pc": wrong_pc, "alive": alive,
    }


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 6
    mt.wait_for_monitor_ready(HOST, PORT, None, TIMEOUT)
    session = mt.MonitorSession(HOST, PORT, None, TIMEOUT)
    results = []
    try:
        for i in range(1, n + 1):
            try:
                results.append(one_iteration(session, i))
            except mt.Failure as exc:
                print(f"  iteration {i} raised: {exc}", flush=True)
                results.append({"idx": i, "raised": str(exc)})
                # try to recover the session for the next iteration
                try:
                    session.sock.sendall(b"\x0f")
                    time.sleep(0.5)
                except Exception:
                    pass
                dbg._reset_c64_core(REST)
    finally:
        try:
            session.sock.sendall(b"\x0f")
        except Exception:
            pass
        session.close()
    print("\n===== SUMMARY =====", flush=True)
    bad = 0
    for r in results:
        line = str(r)
        is_bad = r.get("rts_ok") is False or r.get("alive") is False or "raised" in r
        if is_bad:
            bad += 1
        print(("FAIL " if is_bad else "ok   ") + line, flush=True)
    print(f"\n{bad}/{len(results)} iterations showed the bug", flush=True)
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
