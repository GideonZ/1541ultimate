#!/usr/bin/env python3
"""Visible ROM step coherency measurement harness.

Measures the *first-try* launch outcome of CPU-visible ROM single-steps - the
quantity the e2e suite masks behind the relaunch self-heal.

This tool is useful for validating that the firmware's single-step implementation is coherent with the 
CPU-visible state of the C64, and for measuring the raw coherency of the single-step launch, i.e. how often 
it hits vs. misses, without the relaunch heal.

The firmware writes a probe byte at $02A7 immediately after the FIRST
wait_for_sentinel in perform_run (the step path), before relaunch heals a miss:

    1 = first-try clean trap (HIT)        - survives, since a clean trap never resets
    2 = first-try TIMEOUT                 - launch missed the BRK, ran to the wait cap
    0 = first-try RESET / no-launch       - the runaway crashed the C64 into a reset,
                                            whose KERNAL RAMTAS wipes $0200-$03FF
                                            (so the byte reads back 0)

Therefore the reliable classifier is:  HIT  <=>  $02A7 == 1
                                       FAIL <=>  $02A7 != 1  (2=timeout, 0=reset)

Each iteration reproduces the suite's per-config preamble (reset -> cursor-enter
at the ROM site -> one step) so the launch travels the same no-context
nmi_redirect_to path the suite exercises. A failed step can leave the monitor
wedged in Debug, so the harness hard-recovers (REST reset + reconnect) before
each measurement to keep failures from contaminating neighbouring configs.

Usage:
    python3 coherency_probe.py --host u64 --iterations 25
    python3 coherency_probe.py --host u64 --iterations 25 --configs KRN-JSR-into
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import monitor_test as mt  # type: ignore  # noqa: E402
import monitor_debug_test as dt  # type: ignore  # noqa: E402

PROBE_STEP = 0x02A7  # perform_run() first-try outcome (1=hit, 2=timeout, 0=reset)

# name, base_addr, marker, status_token, step_key ("T"=into, "D"=over), kind
CONFIGS = [
    ("KRN-linear",   0xE000, "KRN", "$E:KRN", "T", "linear"),
    ("KRN-JSR-over",  0xE002, "KRN", "$E:KRN", "D", "jsr"),
    ("KRN-JSR-into",  0xE002, "KRN", "$E:KRN", "T", "jsr"),
    ("BAS-JSR-over",  0xA800, "BAS", "$A:BAS", "D", "jsr"),
]


class Conn:
    def __init__(self, args):
        self.host = args.host
        self.port = args.port
        self.password = args.password
        self.timeout = args.timeout
        self.rest_host = args.rest_host or args.host


def _new_session(conn):
    dt._reset_c64_core(conn.rest_host)
    mt.wait_for_monitor_ready(conn.host, conn.port, conn.password, conn.timeout)
    s = mt.MonitorSession(conn.host, conn.port, conn.password, conn.timeout)
    mt.TestConfig.session = s
    return s


def _clean_slate(holder, conn):
    """Always return a fresh C64 + open monitor, hard-recovering (reset +
    reconnect) if the prior step wedged Debug mode."""
    try:
        dt._ensure_no_debug(holder[0])
        dt._reset_c64_core(conn.rest_host)
        dt._reopen_monitor(holder[0])
        return
    except Exception:  # noqa: BLE001
        pass
    try:
        holder[0].close()
    except Exception:  # noqa: BLE001
        pass
    holder[0] = _new_session(conn)


def _diag(session, full=False):
    try:
        text = session.capture().text()
    except Exception as exc:  # noqa: BLE001
        return f"capture-failed:{exc!r}"
    toks = [t for t in dt.FORBIDDEN_DEBUG_TEXT if t in text]
    hdr = ""
    try:
        hdr = dt._header_line(session).strip()
    except Exception:  # noqa: BLE001
        pass
    out = ("tok=" + ",".join(toks) if toks else "tok=none") + f" | {hdr[:50]}"
    if full:
        rows = [ln.rstrip() for ln in text.splitlines() if ln.strip()]
        out += "\n    SCREEN: " + " / ".join(rows)
    return out


def _resolve(holder, conn, base_addr, marker, status_token, name, step_key, kind):
    _clean_slate(holder, conn)
    snap = dt._enter_rom_debug_at(holder[0], base_addr, marker, name, status_token)
    row = dt._disassembly_row(snap, base_addr)
    if kind == "linear":
        return base_addr, base_addr + dt._instruction_length_from_row(row)
    if "JSR $" in row and f"[{marker}]" in row:
        jsr_addr = base_addr
    else:
        jsr_addr, row = dt._find_visible_jsr_row(snap, marker)
    if step_key == "D":
        expected = jsr_addr + dt._instruction_length_from_row(row)
    else:
        expected = dt._jsr_target_from_row(row)
    return jsr_addr, expected


# Reboot detector: page-2 tail ($02A7-$02FF) is unused by KERNAL/BASIC and the
# debug machinery (which lives at $0314-$0319 + $0363-$03FB), and RAMTAS wipes
# $0200-$03FF to $00 on every C64 reset. So a sentinel written here before the
# step survives a *spurious* reset_cancel (no real reboot) but is wiped by a
# *real* C64 reboot.
_REBOOT_SENTINELS = (0x02A7, 0x02B5, 0x02C3, 0x02D1, 0x02DF, 0x02ED, 0x02FB)
_SENTINEL_VAL = 0x5A


def _reboot_probe(conn):
    """Read the sentinels back. Returns (verdict, detail). verdict in
    {REBOOT, NO-REBOOT, MIXED, ?}."""
    try:
        vals = [mt.read_rest_memory(conn.rest_host, a, 1)[0]
                for a in _REBOOT_SENTINELS]
    except Exception as exc:  # noqa: BLE001
        return "?", f"read-failed:{exc!r}"
    survived = sum(1 for v in vals if v == _SENTINEL_VAL)
    wiped = sum(1 for v in vals if v == 0x00)
    detail = "[" + ",".join(f"{v:02X}" for v in vals) + f"] surv={survived} wiped={wiped}"
    n = len(_REBOOT_SENTINELS)
    if wiped == n:
        return "REBOOT", detail
    if survived == n:
        return "NO-REBOOT", detail
    return "MIXED", detail


# Firmware TEMP DIAGNOSTIC decoder: GET /v1/machine:stepdiag returns NIOS-RAM
# step outcome record (immune to C64-RAM contention). Packed: [0]=wait exit
# [1]=key [2]=result [3]=sentinel [4..5]=captured PC LE [6..9]=fail_count LE
# [10..49]=result_counts[0..9] LE.
_EXIT = {0: "(unset)", 1: "reset-flag", 2: "ok", 3: "CANCELLED", 4: "ctrlx",
         5: "timeout"}
_RES = {0: "OK", 1: "NOT_SUP", 2: "REFUSED", 3: "UNSUP_OP", 4: "NOT_IN_SUB",
        5: "RET_NOT_REACHED", 6: "TIMEOUT", 7: "CANCELLED", 8: "RESET",
        9: "PATCH_FAIL"}


def _read_stepdiag(host):
    try:
        with urllib.request.urlopen(f"http://{host}/v1/machine:stepdiag",
                                    timeout=5.0) as r:
            raw = bytes.fromhex(json.loads(r.read().decode())["diag"])
    except Exception as exc:  # noqa: BLE001
        return f"stepdiag-failed:{exc!r}"
    if len(raw) < 10:
        return f"stepdiag-short:{raw.hex()}"
    wexit, key, res, sent = raw[0], raw[1], raw[2], raw[3]
    pc = raw[4] | (raw[5] << 8)
    fc = int.from_bytes(raw[6:10], "little")
    counts = []
    for ri in range(10):
        off = 10 + 4 * ri
        if off + 4 <= len(raw):
            v = int.from_bytes(raw[off:off + 4], "little")
            if v:
                counts.append(f"{_RES.get(ri, ri)}={v}")
    return (f"exit={_EXIT.get(wexit, wexit)} key=${key:02X} "
            f"res={_RES.get(res, res)} sent=${sent:02X} pc=${pc:04X} "
            f"fails={fc} [{','.join(counts)}]")


def _measure_once(holder, conn, entry_pc, marker, status_token, name, step_key,
                  expected):
    """One suite-faithful step. Returns (final_ok, entryfail, diag).

    The HONEST metric is final_ok: did the footer reach the EXPECTED PC, i.e. a
    clean trap at the installed BRK? A coherency miss runs away and traps on a
    stray $00 (e.g. $0002), which does NOT reach the expected PC. (The old
    firmware "byte" probe conflated those two, so it is gone.)"""
    try:
        _clean_slate(holder, conn)
        session = holder[0]
        dt._enter_rom_debug_at(session, entry_pc, marker, name, status_token)
        for a in _REBOOT_SENTINELS:
            mt.write_rest_memory(conn.rest_host, a, bytes((_SENTINEL_VAL,)))
    except Exception as exc:  # noqa: BLE001
        return False, True, f"setup-failed:{exc!r}"
    session.send_char(step_key)
    final_ok = True
    try:
        dt._wait_for_pc(session, f"{expected:04X}", timeout=8.0)
    except mt.Failure:
        final_ok = False
    if final_ok:
        return True, False, ""
    verdict, vdetail = _reboot_probe(conn)
    cstr = _read_stepdiag(conn.rest_host)
    return False, False, f"{verdict} | {cstr} | {vdetail} | " + _diag(session)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=os.environ.get("U64_MONITOR_HOST", "u64"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("U64_MONITOR_PORT", "23")))
    parser.add_argument("--rest-host", default=None)
    parser.add_argument("--password", default=os.environ.get("U64_MONITOR_PASSWORD"))
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--iterations", type=int, default=25)
    parser.add_argument("--configs", default=None,
                        help="comma-separated subset of: " + ",".join(c[0] for c in CONFIGS))
    args = parser.parse_args()
    conn = Conn(args)

    mt.TestConfig.target = "u64"
    mt.TestConfig.keep_going = True
    mt.TestConfig.failures = []
    mt.TestConfig.skipped = []

    wanted = set(args.configs.split(",")) if args.configs else None
    configs = [c for c in CONFIGS if (wanted is None or c[0] in wanted)]

    holder = [_new_session(conn)]

    # Resolve every config first, then measure ROUND-ROBIN so any time-based
    # load-degradation hits all configs equally.
    resolved = []
    for name, base_addr, marker, status_token, step_key, kind in configs:
        for attempt in range(3):
            try:
                entry_pc, expected = _resolve(holder, conn, base_addr, marker,
                                              status_token, name, step_key, kind)
                break
            except Exception as exc:  # noqa: BLE001
                print(f"resolve {name} attempt {attempt+1} failed: {exc!r}", flush=True)
                _clean_slate(holder, conn)
        else:
            print(f"resolve {name}: GIVING UP", flush=True)
            continue
        print(f"resolved {name}: entry ${entry_pc:04X} key {step_key} "
              f"expect ${expected:04X}", flush=True)
        resolved.append((name, entry_pc, marker, status_token, step_key, expected))

    # ok, fail, entryfail  (fail = clean step did NOT reach the expected PC)
    results = {name: [0, 0, 0] for name, *_ in resolved}
    t0 = time.time()
    try:
        for rnd in range(args.iterations):
            for name, entry_pc, marker, status_token, step_key, expected in resolved:
                final_ok, entryfail, diag = _measure_once(
                    holder, conn, entry_pc, marker, status_token, name,
                    step_key, expected)
                r = results[name]
                if entryfail:
                    r[2] += 1
                elif final_ok:
                    r[0] += 1
                else:
                    r[1] += 1
                tag = "ENTRYFAIL" if entryfail else ("ok" if final_ok else "FAIL")
                extra = f"  {diag}" if diag else ""
                print(f"  round {rnd+1:2d}/{args.iterations} {name:14s} "
                      f"{tag:9s}{extra}", flush=True)
    except KeyboardInterrupt:
        print("\n[interrupted]", flush=True)
    finally:
        try:
            holder[0].close()
        except Exception:  # noqa: BLE001
            pass

    print("\n================ SUMMARY (metric = step reached EXPECTED PC) ========",
          flush=True)
    print(f"{'config':16s} {'n':>4s} {'ok':>4s} {'FAIL':>5s} {'entryF':>6s}   FAIL%",
          flush=True)
    for name, (ok, fail, entryfail) in results.items():
        n = ok + fail + entryfail
        valid = ok + fail
        pct = (100.0 * fail / valid) if valid else 0.0
        print(f"{name:16s} {n:4d} {ok:4d} {fail:5d} {entryfail:6d}   {pct:5.1f}%",
              flush=True)
    print(f"elapsed {time.time()-t0:.0f}s", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
