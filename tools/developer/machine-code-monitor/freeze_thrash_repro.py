#!/usr/bin/env python3
"""Regression repro for the C64 freeze/restore double-freeze hard-halt.

Root cause (JTAG post-mortem 2026-06-28): C64::freeze() lacked an isFrozen guard,
so a re-open / mode-toggle of the local-UI monitor over REST could call freeze()
(-> backup_io()) twice without an intervening unfreeze(). The second backup_io()
trips configASSERT(!backupIsValid) in c64.cc and hard-halts the Nios (vAssertCalled
while(1)) -> REST dead on both NICs -> power-cycle required.

This script hammers the freeze-entry paths (open / C=+O toggle / C=+I mode toggle /
re-open / C=+X recover) that can drive freeze() re-entry, checking REST liveness
after every step. On firmware WITHOUT the fix it hard-halts within a few cycles; on
the fixed firmware (idempotent freeze) it runs clean.

Usage: freeze_thrash_repro.py [host] [cycles]
Exit 0 = no wedge; 2 = WEDGE (REST dead).
"""
import sys, time
sys.path.insert(0, "tools/developer/machine-code-monitor")
import mcm_rest as R
import mcm_localui as L


def alive(rest):
    return rest.alive()


def check(rest, label):
    if not rest.alive():
        time.sleep(2.0)
        if not rest.alive():
            print(f"  *** WEDGE after {label}: REST dead on .13 ***", flush=True)
            r70 = R.Rest(host="192.168.1.70")
            print(f"  .70 alive: {r70.alive()}", flush=True)
            return False
    return True


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.13"
    cycles = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    rest = R.Rest(host=host)
    print(f"FREEZE-THRASH host={host} cycles={cycles} alive={alive(rest)}", flush=True)
    if not alive(rest):
        print("DEVICE NOT ALIVE AT START"); return 2

    # Establish a known baseline.
    rest.tap(["commodore", "x"]); time.sleep(0.5)
    L.ensure_menu_closed(rest)

    for i in range(1, cycles + 1):
        # Pattern A: open (freeze) -> C=+I mode toggle (closes menu) -> reopen (re-freeze attempt)
        L.ensure_menu_open(rest); time.sleep(0.15)
        rest.tap(["commodore", "o"]); time.sleep(0.25)            # open monitor (freeze)
        if not check(rest, f"cycle {i} open"): return 2
        rest.tap(["commodore", "i"]); time.sleep(0.3)             # toggle Freeze<->Overlay (auto-close)
        if not check(rest, f"cycle {i} C=+I"): return 2
        L.ensure_menu_open(rest)
        rest.tap(["commodore", "o"]); time.sleep(0.25)            # reopen monitor (re-freeze)
        if not check(rest, f"cycle {i} reopen"): return 2
        # Pattern B: rapid menu_button toggling (open/close) which re-enters run_once
        for _ in range(3):
            rest.menu_button(); time.sleep(0.12)
        if not check(rest, f"cycle {i} menu_button"): return 2
        # Pattern C: C=+X recover (reset) then immediate reopen+freeze
        rest.tap(["commodore", "x"]); time.sleep(0.4)
        L.ensure_menu_open(rest)
        rest.tap(["commodore", "o"]); time.sleep(0.25)
        if not check(rest, f"cycle {i} recover+reopen"): return 2
        # toggle back to freeze mode if we left it in overlay (keep parity)
        rest.tap(["commodore", "i"]); time.sleep(0.25)
        rest.tap(["commodore", "x"]); time.sleep(0.3)
        L.ensure_menu_closed(rest)
        if not check(rest, f"cycle {i} end"): return 2
        if i % 5 == 0:
            print(f"  cycle {i}/{cycles} ok, alive", flush=True)

    print(f"FREEZE-THRASH COMPLETE: {cycles} cycles, no wedge, final alive={alive(rest)}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
