#!/usr/bin/env python3
"""B16 decisive characterization probe (loop 26).

Replays the exact failing RAM-under-ROM lifecycle step sequence over Telnet:
enter Debug at $C000, single-step D through $C001..$C00B (the STA $01 at $C009
flips the live bank to 5), then perform the failing 7th step D from $C00B
(JMP $E000), which is expected to re-trap at $E000.

The $E000 fixture payload is self-identifying on runaway:

    $E000: INC $0400,X
    $E003: INC $0500,X
    $E006: INX
    $E007: STX $D021
    $E00A: JMP $E000

The fixture loader zeroes $0400-$07E7 first. So after the failing step:

  * $0400+/$0500+/$D021 CHANGED  -> the live 6510 executed the $E000 payload,
        i.e. it did NOT see the DMA-committed BRK ($00) at $E000.
        Root-cause family: BRK commit / live-fetch aperture for bank-5
        RAM-under-ROM (the firmware-view DMA read sees $00 but the live CPU
        fetches the original payload byte).

  * $0400+/$0500+/$D021 UNCHANGED and reported PC != $E000 -> the CPU never
        reached $E000. Root-cause family: release / trampoline / clean-stop
        handoff (CPU stuck in the spin or never released).

  * UNCHANGED and reported PC == $E000 -> B16 did not reproduce this run.

Production REST (readmem) + Telnet only. No firmware change required.
Local-only developer tool; excluded from git.
"""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import monitor_test as mt  # noqa: E402
import monitor_debug_test as mdt  # noqa: E402


def read_counters(host):
    p0400 = mt.read_rest_memory(host, 0x0400, 16)
    p0500 = mt.read_rest_memory(host, 0x0500, 16)
    d020 = mt.read_rest_memory(host, 0xD020, 2)   # $D020, $D021
    p01 = mt.read_rest_memory(host, 0x0001, 1)
    e000 = mt.read_rest_memory(host, 0xE000, 4)
    jiffy = mt.read_rest_memory(host, 0x00A0, 3)  # jiffy clock MSB..LSB
    spin = mt.read_rest_memory(host, 0x0387, 6)   # SPIN_JMP + operand + tramp[0..2]
    sentinel = mt.read_rest_memory(host, 0x03F7, 1)
    c00b = mt.read_rest_memory(host, 0xC00B, 4)   # launch site JMP $E000 + next
    return {
        "0400": p0400.hex(),
        "0500": p0500.hex(),
        "d020_d021": d020.hex(),
        "01": p01.hex(),
        "e000": e000.hex(),
        "jiffy": jiffy.hex(),
        "spin_0387": spin.hex(),
        "sentinel": sentinel.hex(),
        "c00b": c00b.hex(),
    }


def show(tag, c):
    print(f"  [{tag}] $0400={c['0400']} $0500={c['0500']} "
          f"$D020/$D021={c['d020_d021']} $01={c['01']} $E000={c['e000']} "
          f"$C00B={c['c00b']} jiffy={c['jiffy']} spin$0387={c['spin_0387']} "
          f"sentinel={c['sentinel']}", flush=True)


def zero_detector(host):
    mt.write_rest_memory(host, 0x0400, bytes(256))
    mt.write_rest_memory(host, 0x0500, bytes(256))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="u64")
    ap.add_argument("--port", type=int, default=23)
    ap.add_argument("--timeout", type=float, default=20.0)
    args = ap.parse_args()
    host = args.host

    print("=== B16 runaway probe (loop 26) ===", flush=True)
    print("[1] Loading repeat-redebug fixtures (zeroes $0400-$07E7, "
          "$C000 bootstrap, $E000 payload)", flush=True)
    mdt._load_repeat_redebug_fixtures(host)
    base = read_counters(host)
    show("baseline", base)

    print("[2] Reset C64 core + open Telnet monitor, enter Debug at $C000",
          flush=True)
    mdt._reset_c64_core(host)
    session = mt.MonitorSession(host, args.port, None, args.timeout)
    verdict = "UNKNOWN"
    try:
        mdt._reopen_monitor(session)
        mdt._ensure_no_debug(session)
        mdt._clear_all_breakpoints(session, "b16 probe: clear bps")
        mdt._enter_lifecycle_debug_at(session, 0xC000, 7, "RAM",
                                      "b16 probe: enter $C000")

        print("[3] Single-step D through $C001..$C00B (asserting PC)",
              flush=True)
        for expected in (0xC001, 0xC003, 0xC005, 0xC007, 0xC009, 0xC00B):
            parsed = mdt._step_and_assert_pc(
                session, "D", expected, f"b16 probe: step ${expected:04X}")
            print(f"  step -> PC={parsed['pc']} (expected {expected:04X})",
                  flush=True)

        print("    (zeroing $0400-$05FF detector while CPU is parked)",
              flush=True)
        zero_detector(host)
        pre = read_counters(host)
        show("pre-D7", pre)

        print("[4] FAILING step: D from $C00B (expect re-trap at $E000)",
              flush=True)
        snap = session.send_char("D")
        time.sleep(0.4)
        text = snap.text()
        modal = any(m in text for m in ("DEBUG", "CANCELLED", "not mapped",
                                        "Could not"))
        footer = mdt._footer_value_line(session)
        parsed = mdt._parse_footer_values(footer)
        actual_pc = parsed.get("pc", "????")
        print(f"  D#7 reported PC = {actual_pc} (expected E000); "
              f"modal_text={modal}", flush=True)
        if modal:
            print("  screen text:\n" + "\n".join(
                "    " + ln for ln in text.splitlines() if ln.strip()),
                flush=True)

        print("[5] Post-D7 counters (4 samples over ~3s)", flush=True)
        samples = []
        for _ in range(4):
            samples.append(read_counters(host))
            time.sleep(0.8)
        for i, s in enumerate(samples):
            show(f"post{i+1}", s)
        s1 = samples[0]

        # Compare against the in-Debug zeroed detector (pre), NOT the pre-reset
        # baseline. The reset between fixture-load and stepping clears the $0400
        # screen RAM to $20, so `base` is not a valid reference.
        ran = any(s["0400"] != pre["0400"] or s["0500"] != pre["0500"]
                  or s["d020_d021"] != pre["d020_d021"] for s in samples)
        changing = any(samples[i]["jiffy"] != samples[i + 1]["jiffy"]
                       for i in range(len(samples) - 1))

        print("\n=== VERDICT ===", flush=True)
        if ran:
            verdict = "RUNAWAY"
            print("RUNAWAY: the $E000 payload executed -> the live 6510 did "
                  "NOT see the BRK at $E000.", flush=True)
            print("Root-cause family: BRK commit / live-fetch aperture for "
                  "bank-5 RAM-under-ROM.", flush=True)
            print(f"  jiffy advancing across samples: {changing}", flush=True)
        elif actual_pc.upper() == "E000":
            verdict = "SUCCESS(no-repro)"
            print("SUCCESS: re-trapped at $E000 and payload untouched. "
                  "B16 did NOT reproduce this run.", flush=True)
        else:
            verdict = "STUCK"
            print("STUCK: payload untouched and PC != $E000 -> the CPU never "
                  "reached $E000 as bank-5 RAM.", flush=True)
            print("Root-cause family: release / trampoline / clean-stop "
                  "handoff (CPU stuck in spin or ran KERNAL ROM at $E000).",
                  flush=True)
            print(f"  jiffy advancing across samples: {changing} "
                  "(advancing => CPU running free, e.g. KERNAL; "
                  "frozen => parked/spin)", flush=True)
        print(f"reported_pc={actual_pc} ran={ran} jiffy_changing={changing}",
              flush=True)
    finally:
        print("\n[6] Recovery: Ctrl-X reset", flush=True)
        try:
            mdt._send_ctrl_x(session)
            time.sleep(1.0)
        except Exception as exc:  # noqa: BLE001
            print(f"  ctrl-x error: {exc}", flush=True)
        session.close()
        try:
            mdt._reset_c64_core(host)
        except Exception as exc:  # noqa: BLE001
            print(f"  reset error: {exc}", flush=True)

    print(f"\nFINAL_VERDICT={verdict}", flush=True)


if __name__ == "__main__":
    main()
