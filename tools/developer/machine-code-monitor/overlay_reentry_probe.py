#!/usr/bin/env python3
"""Fine-grained Overlay re-entry hang isolator.

Replicates the failing Direct Overlay RAM lifecycle (continue -> reset ->
re-enter monitor -> debug step) but inserts a bounded network-liveness probe
after every micro-operation in the re-entry phase, so we learn exactly which
firmware operation wedges the device (drops it off the network).
"""
import socket
import sys
import time
import urllib.error

import monitor_direct_test as M


def probe(session: M.RestSession, where: str) -> None:
    """Bounded liveness probe; raises if the firmware has wedged."""
    try:
        s = socket.create_connection((session.host, 80), timeout=3)
        s.close()
    except OSError as exc:
        raise SystemExit(f"\n*** WEDGE DETECTED right AFTER: {where}  ({exc}) ***")
    print(f"  [alive] after: {where}", flush=True)


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "u64"
    session = M.RestSession(host, 8.0)
    capture = M.VicStreamCapture()
    ocr = M.C64FrameOCR()
    try:
        session.stop_video()
    except Exception:
        pass
    M.ensure_stream_hidden_overlay_mode(session, capture, ocr)
    reader = lambda: session.dump_ui_screen("overlay_probe")
    enter_monitor = lambda: M.enter_hidden_monitor(session)
    label = "PROBE Overlay ram"

    print("=== PHASE 1: first debug session + continue ===", flush=True)
    M.reset_to_basic(session, close_ui=False)
    probe(session, "reset_to_basic")
    M.load_repeat_redebug_fixtures(session)
    probe(session, "load fixtures")
    enter_monitor()
    probe(session, "enter_monitor #1")
    M.run_to_breakpoint_then_continue(
        session, reader, label,
        0xC300, 0xC302, 7, "RAM", (0xC302,),
        progress_addr=0x0600, progress_len=0x1E8)
    probe(session, "run_to_breakpoint_then_continue (full-speed continue)")

    print("=== PHASE 2: reset + re-enter (the suspected wedge) ===", flush=True)
    session.reset()
    probe(session, "session.reset()")
    M.wait_basic_ready(session)
    probe(session, "wait_basic_ready")
    M.check_network_liveness(session, f"{label}: liveness after reset")
    probe(session, "check_network_liveness")

    # enter_monitor() expanded with probes
    print("--- entering hidden overlay monitor (ctrl_m path) ---", flush=True)
    enter_monitor()
    probe(session, "enter_monitor #2 (re-entry)")

    # enter_direct_debug_at -> enter_debug_at expanded with probes
    addr, source, bank = 0xC300, "RAM", 7
    print("--- J C300 ---", flush=True)
    M.send_ui_text(session, f"J{addr:04X}\n", settle=0.25)
    probe(session, "send J C300")
    M.wait_for_text(reader, (f"${addr:04X}",), f"{label}: goto", timeout=5.0)
    probe(session, "wait goto $C300")
    print("--- A (asm view) ---", flush=True)
    M.send_ui_text(session, "A", settle=0.25)
    probe(session, "send A")
    M.wait_for_text(reader, (f"MONITOR ASM ${addr:04X}", source),
                    f"{label}: asm", timeout=5.0)
    probe(session, "wait ASM view")
    print("--- D (enter debug) ---", flush=True)
    M.send_ui_text(session, "D", settle=0.25)
    probe(session, "send D (enter debug)")
    M.wait_for_text(reader, ("Dbg", "PC", "NV-BDIZC"), f"{label}: debug", timeout=5.0)
    probe(session, "wait Debug entered")

    print("--- D (first re-entered step) ---", flush=True)
    M.send_ui_text(session, "D", settle=0.25)
    probe(session, "send D (step)")
    M.wait_for_pc(reader, 0xC302, f"{label}: first re-entered step")
    probe(session, "wait_for_pc 0xC302 (step done)")

    print("\n=== NO WEDGE: full re-entry+step completed ===", flush=True)
    M.leave_debug(session)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
