#!/usr/bin/env python3
"""Characterize the post-debug jiffy/CIA state after one Overlay/RAM lifecycle.

This is a production-REST-only probe for the RALPH H8/B12 hypothesis. It reuses
the direct-UI soak lifecycle, records labeled machine snapshots, and then
distinguishes a real post-exit CIA/IRQ leak from the expected stopped-CPU state
while Debug is still active.
"""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


MCM_DIR = Path(__file__).resolve().parent
SOAK_DIR = MCM_DIR / "soak"
sys.path.insert(0, str(MCM_DIR))
sys.path.insert(0, str(SOAK_DIR))

from mcm_rest import Rest  # noqa: E402
import overlay_lifecycle_clean as overlay_lifecycle  # noqa: E402
import run_full_matrix_soak as soak  # noqa: E402


CIA1_BASE = 0xDC00
CIA2_BASE = 0xDD00


def now() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="milliseconds")


def read_hex(rest: Rest, addr: int, length: int) -> str:
    return rest.read_mem(addr, length).hex().upper()


def jiffy_value(raw: bytes) -> int:
    return raw[0] | (raw[1] << 8) | (raw[2] << 16)


def jiffy_samples(rest: Rest, count: int = 6, interval: float = 0.5) -> dict[str, Any]:
    values: list[int] = []
    raw_values: list[str] = []
    for i in range(count):
        raw = rest.read_mem(0x00A0, 3)
        values.append(jiffy_value(raw))
        raw_values.append(raw.hex().upper())
        if i + 1 < count:
            time.sleep(interval)
    return {
        "address": "00A0",
        "raw": raw_values,
        "values": values,
        "advanced": len(set(values)) > 1,
    }


def tcp_probe(host: str, port: int, timeout: float = 3.0) -> bool:
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.close()
    return True


def snapshot(rest: Rest, host: str, label: str) -> dict[str, Any]:
    data: dict[str, Any] = {
        "timestamp": now(),
        "label": label,
        "liveness": {},
        "memory": {},
        "notes": [
            "Reading CIA ICR ($DC0D/$DD0D) clears pending interrupt flags; "
            "this probe is characterization-only."
        ],
    }

    def probe(name: str, func) -> None:
        start = time.monotonic()
        try:
            value = func()
            data["liveness"][name] = {
                "status": "pass",
                "actual": value,
                "duration_ms": int((time.monotonic() - start) * 1000),
            }
        except BaseException as exc:  # noqa: BLE001
            data["liveness"][name] = {
                "status": "fail",
                "actual": f"{type(exc).__name__}: {exc}",
                "duration_ms": int((time.monotonic() - start) * 1000),
            }

    probe("ping", lambda: subprocess.run(
        ["ping", "-c1", "-W2", host],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ) is not None)
    probe("rest_version", lambda: soak.request_json(host, "/v1/version"))
    probe("tcp_23", lambda: tcp_probe(host, 23))
    probe("tcp_21", lambda: tcp_probe(host, 21))

    regions = [
        ("cpu_port_0001", 0x0001, 1),
        ("jiffy_00a0", 0x00A0, 3),
        ("irq_vector_0314", 0x0314, 2),
        ("nmi_vector_0318", 0x0318, 2),
        ("hard_vectors_fffa", 0xFFFA, 6),
        ("screen_0400", 0x0400, 16),
        ("cia1_dc00_dc0f", CIA1_BASE, 16),
        ("cia2_dd00_dd0f", CIA2_BASE, 16),
        ("vic_d011_d012", 0xD011, 2),
        ("vic_d019_d01a", 0xD019, 2),
    ]
    for name, addr, length in regions:
        try:
            data["memory"][name] = {
                "address": f"{addr:04X}",
                "length": length,
                "hex": read_hex(rest, addr, length),
            }
        except BaseException as exc:  # noqa: BLE001
            data["memory"][name] = {
                "address": f"{addr:04X}",
                "length": length,
                "error": f"{type(exc).__name__}: {exc}",
            }
    try:
        data["jiffy_samples"] = jiffy_samples(rest)
    except BaseException as exc:  # noqa: BLE001
        data["jiffy_samples"] = {"error": f"{type(exc).__name__}: {exc}"}
    return data


def screen_state(rest: Rest) -> str:
    try:
        text = rest.screen_text()
    except BaseException as exc:  # noqa: BLE001
        return f"menu_screen unavailable: {type(exc).__name__}: {exc}"
    for line in text.splitlines():
        stripped = line.strip()
        if stripped:
            return stripped
    return "menu_screen blank"


def write_snapshot(out_dir: Path, entry: dict[str, Any]) -> None:
    path = out_dir / f"{entry['label']}.json"
    path.write_text(json.dumps(entry, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    jiffy = entry.get("jiffy_samples", {})
    print(
        f"[snapshot] {entry['label']}: jiffy advanced={jiffy.get('advanced')} "
        f"raw={jiffy.get('raw')}",
        flush=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="u64")
    parser.add_argument("--out-dir", default="")
    args = parser.parse_args()

    out_dir = Path(args.out_dir) if args.out_dir else Path(
        f"/tmp/u64_h8_characterize/{datetime.now().strftime('%Y%m%d-%H%M%S')}"
    )
    out_dir.mkdir(parents=True, exist_ok=True)
    rest = Rest(args.host, 12)
    label = "h8 overlay/ram characterize"
    summary: dict[str, Any] = {
        "host": args.host,
        "started": now(),
        "out_dir": str(out_dir),
        "events": [],
    }

    def snap(name: str) -> None:
        entry = snapshot(rest, args.host, name)
        entry["screen_state"] = screen_state(rest)
        write_snapshot(out_dir, entry)
        summary["events"].append({
            "label": name,
            "jiffy_advanced": entry.get("jiffy_samples", {}).get("advanced"),
            "screen_state": entry.get("screen_state", ""),
        })

    soak.set_interface_type_robust(rest, "Overlay on HDMI", label)
    overlay_lifecycle.reset_to_basic(rest, label)
    snap("after_reset_before_menu")

    overlay_lifecycle.load_fixture(rest)
    snap("after_fixture_before_menu")

    soak.open_overlay_monitor_strict(rest, f"{label}: first monitor entry")
    snap("after_first_monitor_entry")

    soak.overlay_first_session_from_open_monitor(rest, "ram", label)
    snap("after_first_continue_no_breakpoint")

    overlay_lifecycle.reset_to_basic(rest, f"{label}: re-entry reset")
    snap("after_reentry_reset_before_menu")

    soak.open_overlay_monitor_strict(rest, f"{label}: re-entry monitor")
    snap("after_reentry_monitor_entry")

    soak.overlay_reenter_and_step_without_reset(rest, "ram", label)
    snap("after_reentry_step_debug_stopped")

    rest.release_all()
    time.sleep(0.3)
    snap("after_release_all")

    rest.tap(["run_stop"])
    time.sleep(1.0)
    snap("after_run_stop")

    # If RUN/STOP did not leave Debug, C=+D is the monitor's explicit debug exit.
    rest.tap(["commodore", "d"])
    time.sleep(1.0)
    snap("after_commodore_d")

    rest.release_all()
    time.sleep(0.3)
    snap("after_final_release_all")

    summary["finished"] = now()
    summary_path = out_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"[summary] {summary_path}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
