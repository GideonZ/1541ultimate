#!/usr/bin/env python3
"""Focused stress for the contextless visible-ROM debug entry (bp+Go to $E002).

Each iteration performs ONE firmware ROM entry (single 'G', so only the
firmware's own runaway relaunch fires - no outer harness retry) and records
whether the CPU trapped at the target or ran past it (DEBUG TIMEOUT / PC did
not reach). Optional concurrent REST readmem load reproduces the Nios-busy
timing pressure under which the fetch race surfaces in the full matrix.

Usage:
  python3 rom_entry_stress.py --host u64 --rest-host u64 --iters 40 --load
"""
import argparse
import sys
import threading
import time

import monitor_test as mt
import monitor_debug_test as mdt

TARGET = 0xE002
BOOT = 0xC5F0
# LDX #$40 / DEX / BNE *-1 (~ delay) / JMP TARGET  - the canonical RAM bootstrap.
BOOT_PROG = bytes([0xA2, 0x40, 0xCA, 0xD0, 0xFD,
                   0x4C, TARGET & 0xFF, (TARGET >> 8) & 0xFF])


def rest_load(host, stop_evt):
    while not stop_evt.is_set():
        try:
            mt.read_rest_memory(host, 0xE000, 64)
        except Exception:
            pass
        time.sleep(0.01)


def one_entry(session, rest_host):
    """Return (ok, detail). ok=True when the CPU trapped at TARGET."""
    mdt._reset_c64_core(rest_host)
    mdt._reopen_monitor(session)
    mt.ensure_status(session, "CPU7 $A:BAS $D:I/O $E:KRN VIC")
    mdt._ensure_no_debug(session)
    mt.write_rest_memory(rest_host, BOOT, BOOT_PROG)
    session.goto(f"{TARGET:04X}")
    session.send_char("A")
    header = mdt._header_line(session)
    if "Dbg" not in header:
        session.send_char("D")
    mdt._ensure_breakpoint_at(session, TARGET, "stress entry bp")
    session.goto(f"{BOOT:04X}")
    session.send_char("G")
    try:
        mdt._wait_for_pc(session, f"{TARGET:04X}", timeout=6.0)
        return True, "trapped"
    except mt.Failure as exc:
        return False, str(exc).splitlines()[0][:120]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--rest-host", required=True)
    ap.add_argument("--port", type=int, default=23)
    ap.add_argument("--password")
    ap.add_argument("--timeout", type=float, default=6.0)
    ap.add_argument("--iters", type=int, default=40)
    ap.add_argument("--retries", type=int, default=0,
                    help="extra reset+retry attempts per logical entry")
    ap.add_argument("--load", action="store_true",
                    help="run concurrent REST readmem load")
    args = ap.parse_args()

    stop_evt = threading.Event()
    loaders = []
    if args.load:
        for _ in range(3):
            t = threading.Thread(target=rest_load,
                                 args=(args.rest_host, stop_evt), daemon=True)
            t.start()
            loaders.append(t)

    def fresh_session():
        for _ in range(6):
            try:
                mdt._reset_c64_core(args.rest_host)
                return mt.MonitorSession(args.host, args.port, args.password,
                                         args.timeout)
            except Exception:
                time.sleep(1.0)
        raise RuntimeError("could not (re)establish telnet monitor session")

    session = fresh_session()
    ok = 0
    fail = 0
    fails = []
    try:
        for i in range(1, args.iters + 1):
            good = False
            detail = ""
            attempts = 0
            # one_entry() already resets the C64 at its start, so each attempt is
            # an independent fresh-fetch try - reset breaks the correlated ROM
            # first-fetch race that the in-place firmware relaunch cannot.
            for attempt in range(args.retries + 1):
                attempts = attempt + 1
                try:
                    good, detail = one_entry(session, args.rest_host)
                except Exception as exc:  # noqa: BLE001
                    good, detail = False, f"EXC {type(exc).__name__}: {exc}"[:120]
                if good:
                    break
                # A missed ROM entry leaves the 6510 running wild in KERNAL and
                # can break the remote menu; recover the machine + telnet before
                # the next attempt so tries are counted independently.
                try:
                    session.close()
                except Exception:
                    pass
                session = fresh_session()
            if good:
                ok += 1
            else:
                fail += 1
                fails.append((i, detail))
            print(f"[{i:03d}/{args.iters}] {'OK ' if good else 'FAIL'} "
                  f"(tries={attempts})  {detail}", flush=True)
    finally:
        stop_evt.set()
        try:
            mdt._ensure_no_debug(session)
        except Exception:
            pass
        session.close()

    print(f"\nROM-ENTRY STRESS: iters={args.iters} ok={ok} fail={fail} "
          f"fail_rate={fail/max(args.iters,1)*100:.1f}%", flush=True)
    for i, d in fails:
        print(f"  fail@{i}: {d}", flush=True)
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
