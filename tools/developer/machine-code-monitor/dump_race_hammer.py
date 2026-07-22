#!/usr/bin/env python3
"""Decisive stress test for the dump_ui_screen cross-task race hypothesis.

Thread A: tight loop hammering PUT /v1/machine:dump_ui_screen (reads firmware
          UI screen memory on the REST task).
Thread B: drives monitor open (ctrl+m) / close (escape) and periodic reset, so
          the UI/monitor objects are repeatedly created and torn down.

If the firmware wedges (drops off the network) under this load, the temporary
dump hook races with UI/monitor lifecycle teardown (use-after-free / bus fault).
"""
import socket
import sys
import threading
import time

import monitor_direct_test as M


STOP = threading.Event()
WEDGED = threading.Event()


def alive(host: str) -> bool:
    try:
        s = socket.create_connection((host, 80), timeout=2)
        s.close()
        return True
    except OSError:
        return False


def dump_loop(session: M.RestSession):
    n = 0
    while not STOP.is_set():
        try:
            session.request("PUT", "/v1/machine:dump_ui_screen", params={"tag": "race"})
            n += 1
        except Exception:
            if not alive(session.host):
                WEDGED.set()
                STOP.set()
                return
        time.sleep(0.01)
    print(f"  dump_loop issued ~{n} dumps", flush=True)


def driver_loop(session: M.RestSession):
    i = 0
    while not STOP.is_set():
        i += 1
        try:
            session.input_ui(["ctrl", "m"])
            time.sleep(0.15)
            session.input_ui(["escape"])
            time.sleep(0.15)
            if i % 5 == 0:
                session.reset()
                time.sleep(1.0)
        except Exception:
            if not alive(session.host):
                WEDGED.set()
                STOP.set()
                return


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "u64"
    duration = float(sys.argv[2]) if len(sys.argv) > 2 else 90.0
    session_a = M.RestSession(host, 5.0)
    session_b = M.RestSession(host, 5.0)
    capture = M.VicStreamCapture()
    ocr = M.C64FrameOCR()
    try:
        session_a.stop_video()
    except Exception:
        pass
    M.ensure_stream_hidden_overlay_mode(session_a, capture, ocr)

    ta = threading.Thread(target=dump_loop, args=(session_a,))
    tb = threading.Thread(target=driver_loop, args=(session_b,))
    ta.start(); tb.start()
    deadline = time.time() + duration
    while time.time() < deadline and not STOP.is_set():
        time.sleep(1.0)
        if not alive(host):
            print("*** liveness probe failed ***", flush=True)
            WEDGED.set(); STOP.set(); break
    STOP.set()
    ta.join(timeout=5); tb.join(timeout=5)

    if WEDGED.is_set():
        print("\n*** WEDGE DETECTED under dump+lifecycle stress ***", flush=True)
        return 2
    print("\n=== NO WEDGE across stress window ===", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
