#!/usr/bin/env python3
"""Faithful fast-timing Overlay RAM lifecycle loop.

Replicates run_direct_lifecycle_case('ram') + direct_lifecycle_reenter at the
SAME back-to-back speed the matrix uses (no inter-step liveness probes), looping
it N times. Only checks liveness at the end of each full iteration. Designed to
reproduce the timing-dependent re-entry wedge deterministically.
"""
import socket
import sys

import monitor_direct_test as M


def alive(host: str) -> bool:
    try:
        s = socket.create_connection((host, 80), timeout=3)
        s.close()
        return True
    except OSError:
        return False


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "u64"
    iters = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    session = M.RestSession(host, 20.0)
    capture = M.VicStreamCapture()
    ocr = M.C64FrameOCR()
    try:
        session.stop_video()
    except Exception:
        pass
    M.ensure_stream_hidden_overlay_mode(session, capture, ocr)
    reader = lambda: session.dump_ui_screen("overlay_loop")
    enter_monitor = lambda: M.enter_hidden_monitor(session)

    for i in range(1, iters + 1):
        label = f"LOOP Overlay ram iter {i}/{iters}"
        print(f"[{i}] {label} ...", flush=True)
        M.run_direct_lifecycle_case(session, reader, enter_monitor, label, "ram")
        if not alive(host):
            print(f"\n*** WEDGE DETECTED during iter {i} ***", flush=True)
            return 2
        print(f"  [alive] iter {i} complete", flush=True)

    print("\n=== NO WEDGE across all iterations ===", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
