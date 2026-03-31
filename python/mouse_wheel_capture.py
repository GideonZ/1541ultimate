#!/usr/bin/env python3

import argparse
import glob
import json
import os
import re
import select
import struct
import sys
import time
from pathlib import Path


EV_SYN = 0x00
EV_KEY = 0x01
EV_REL = 0x02

SYN_REPORT = 0x00

REL_X = 0x00
REL_Y = 0x01
REL_HWHEEL = 0x06
REL_WHEEL = 0x08
REL_WHEEL_HI_RES = 0x0B
REL_HWHEEL_HI_RES = 0x0C

EVENT_STRUCT = struct.Struct("llHHi")

TYPE_NAMES = {
    EV_SYN: "EV_SYN",
    EV_KEY: "EV_KEY",
    EV_REL: "EV_REL",
}

REL_NAMES = {
    REL_X: "REL_X",
    REL_Y: "REL_Y",
    REL_HWHEEL: "REL_HWHEEL",
    REL_WHEEL: "REL_WHEEL",
    REL_WHEEL_HI_RES: "REL_WHEEL_HI_RES",
    REL_HWHEEL_HI_RES: "REL_HWHEEL_HI_RES",
}

PHASES = [
    {
        "id": "slow_up",
        "title": "Slow Up",
        "duration": 10.0,
        "instructions": [
            "Rotate the wheel upward one notch at a time.",
            "Do 6 notches over about 10 seconds.",
            "Leave about 1 second between notches.",
            "Keep the mouse body still. Do not click any buttons.",
        ],
    },
    {
        "id": "slow_down",
        "title": "Slow Down",
        "duration": 10.0,
        "instructions": [
            "Rotate the wheel downward one notch at a time.",
            "Do 6 notches over about 10 seconds.",
            "Leave about 1 second between notches.",
            "Keep the mouse body still. Do not click any buttons.",
        ],
    },
    {
        "id": "fast_up",
        "title": "Fast Up",
        "duration": 4.0,
        "instructions": [
            "Do 3 fast upward flicks.",
            "Each flick should be deliberately quick.",
            "Keep the mouse body still. Do not click any buttons.",
        ],
    },
    {
        "id": "fast_down",
        "title": "Fast Down",
        "duration": 4.0,
        "instructions": [
            "Do 3 fast downward flicks.",
            "Each flick should be deliberately quick.",
            "Keep the mouse body still. Do not click any buttons.",
        ],
    },
]


def parse_input_devices():
    blocks = Path("/proc/bus/input/devices").read_text().strip().split("\n\n")
    entries = []
    for block in blocks:
        entry = {"name": "", "handlers": [], "raw": block}
        for line in block.splitlines():
            if line.startswith("N:"):
                match = re.search(r'Name="([^"]+)"', line)
                if match:
                    entry["name"] = match.group(1)
            elif line.startswith("H:"):
                match = re.search(r"Handlers=(.*)$", line)
                if match:
                    entry["handlers"] = match.group(1).split()
        entries.append(entry)
    return entries


def resolve_by_id_map():
    mapping = {}
    for path in glob.glob("/dev/input/by-id/*"):
        try:
            resolved = os.path.realpath(path)
        except OSError:
            continue
        mapping.setdefault(resolved, []).append(path)
    return mapping


def candidate_devices():
    by_id_map = resolve_by_id_map()
    candidates = []
    for entry in parse_input_devices():
        event_handlers = [handler for handler in entry["handlers"] if handler.startswith("event")]
        if not event_handlers:
            continue
        event_handler = event_handlers[0]
        devnode = f"/dev/input/{event_handler}"
        by_id = sorted(by_id_map.get(devnode, []))
        lowered = entry["name"].lower()

        score = 0
        if any(path.endswith("event-mouse") for path in by_id):
            score += 100
        if any("logitech_usb_receiver" in path.lower() for path in by_id):
            score += 100
        if "logitech" in lowered:
            score += 40
        if "mouse" in lowered:
            score += 40
        if any(handler.startswith("mouse") for handler in entry["handlers"]):
            score += 40
        if "keyboard" in lowered or "consumer" in lowered or "system control" in lowered:
            score -= 80

        candidates.append({
            "name": entry["name"],
            "handlers": entry["handlers"],
            "event": event_handler,
            "devnode": devnode,
            "by_id": by_id,
            "score": score,
        })

    candidates.sort(key=lambda candidate: (candidate["score"], candidate["name"]), reverse=True)
    return candidates


def find_hidraw_for_event(event_path):
    sys_event = Path("/sys/class/input") / Path(event_path).name / "device"
    if not sys_event.exists():
        return None

    search_root = Path(os.path.realpath(str(sys_event)))
    current = search_root
    for _ in range(6):
        direct = sorted(current.glob("hidraw/hidraw*"))
        if direct:
            return f"/dev/{direct[0].name}"
        nested = sorted(current.glob("**/hidraw/hidraw*"))
        if nested:
            return f"/dev/{nested[0].name}"
        if current.parent == current:
            break
        current = current.parent
    return None


def pick_device(requested_path=None):
    if requested_path:
        resolved = os.path.realpath(requested_path)
        return {
            "selected": requested_path,
            "resolved": resolved,
            "name": Path(resolved).name,
            "by_id": [requested_path] if requested_path != resolved else [],
            "hidraw": find_hidraw_for_event(resolved),
        }

    preferred = "/dev/input/by-id/usb-Logitech_USB_Receiver-if02-event-mouse"
    if os.path.exists(preferred):
        resolved = os.path.realpath(preferred)
        return {
            "selected": preferred,
            "resolved": resolved,
            "name": "Logitech MX Master 3",
            "by_id": [preferred],
            "hidraw": find_hidraw_for_event(resolved),
        }

    candidates = candidate_devices()
    if not candidates:
        raise RuntimeError("No mouse-like input device found.")

    best = candidates[0]
    return {
        "selected": best["by_id"][0] if best["by_id"] else best["devnode"],
        "resolved": best["devnode"],
        "name": best["name"],
        "by_id": best["by_id"],
        "hidraw": find_hidraw_for_event(best["devnode"]),
    }


def event_type_name(event_type):
    return TYPE_NAMES.get(event_type, f"EV_{event_type}")


def event_code_name(event_type, code):
    if event_type == EV_REL:
        return REL_NAMES.get(code, f"REL_{code}")
    if event_type == EV_SYN and code == SYN_REPORT:
        return "SYN_REPORT"
    return str(code)


def empty_report():
    return {
        "rel_x": 0,
        "rel_y": 0,
        "wheel": 0,
        "wheel_hi_res": 0,
        "hwheel": 0,
        "hwheel_hi_res": 0,
        "keys": [],
        "events": [],
    }


def finalize_report(report, report_time, previous_time, index):
    finished = dict(report)
    finished["index"] = index
    finished["t"] = round(report_time, 6)
    finished["dt_ms"] = None if previous_time is None else round((report_time - previous_time) * 1000.0, 3)
    return finished


def summarize_phase(phase_id, reports, quiet_gap_ms, hidraw_packets):
    wheel_reports = []
    for report in reports:
        if report["wheel"] or report["wheel_hi_res"] or report["hwheel"] or report["hwheel_hi_res"]:
            wheel_reports.append(report)

    gestures = []
    current = None

    for report in wheel_reports:
        direction = 0
        if report["wheel_hi_res"]:
            direction = 1 if report["wheel_hi_res"] > 0 else -1
        elif report["wheel"]:
            direction = 1 if report["wheel"] > 0 else -1

        start_new = current is None
        if current is not None and report["dt_ms"] is not None and report["dt_ms"] >= quiet_gap_ms:
            start_new = True

        if start_new:
            if current is not None:
                current["duration_ms"] = round((current["end_t"] - current["start_t"]) * 1000.0, 3)
                gestures.append(current)
            current = {
                "index": len(gestures),
                "start_t": report["t"],
                "end_t": report["t"],
                "report_indices": [report["index"]],
                "report_count": 1,
                "net_wheel": report["wheel"],
                "net_wheel_hi_res": report["wheel_hi_res"],
                "net_hwheel": report["hwheel"],
                "net_hwheel_hi_res": report["hwheel_hi_res"],
                "directions": [direction],
                "frames": [{
                    "report_index": report["index"],
                    "t": report["t"],
                    "wheel": report["wheel"],
                    "wheel_hi_res": report["wheel_hi_res"],
                    "hwheel": report["hwheel"],
                    "hwheel_hi_res": report["hwheel_hi_res"],
                    "dt_ms": report["dt_ms"],
                }],
            }
            continue

        current["end_t"] = report["t"]
        current["report_indices"].append(report["index"])
        current["report_count"] += 1
        current["net_wheel"] += report["wheel"]
        current["net_wheel_hi_res"] += report["wheel_hi_res"]
        current["net_hwheel"] += report["hwheel"]
        current["net_hwheel_hi_res"] += report["hwheel_hi_res"]
        current["directions"].append(direction)
        current["frames"].append({
            "report_index": report["index"],
            "t": report["t"],
            "wheel": report["wheel"],
            "wheel_hi_res": report["wheel_hi_res"],
            "hwheel": report["hwheel"],
            "hwheel_hi_res": report["hwheel_hi_res"],
            "dt_ms": report["dt_ms"],
        })

    if current is not None:
        current["duration_ms"] = round((current["end_t"] - current["start_t"]) * 1000.0, 3)
        gestures.append(current)

    return {
        "phase": phase_id,
        "report_count": len(reports),
        "wheel_report_count": len(wheel_reports),
        "gesture_count": len(gestures),
        "net_wheel": sum(report["wheel"] for report in wheel_reports),
        "net_wheel_hi_res": sum(report["wheel_hi_res"] for report in wheel_reports),
        "max_reports_per_gesture": max((gesture["report_count"] for gesture in gestures), default=0),
        "hidraw_packet_count": len(hidraw_packets),
        "gestures": gestures,
    }


def capture_phase(evdev_fd, hidraw_fd, phase, countdown, quiet_gap_ms):
    print()
    print(f"[{phase['title']}] {phase['duration']:.1f}s")
    for line in phase["instructions"]:
        print(f"  - {line}")
    input("Press Enter when ready... ")

    for remaining in range(countdown, 0, -1):
        print(f"Starting in {remaining}...", flush=True)
        time.sleep(1.0)

    print("Recording now.")

    deadline = time.monotonic() + phase["duration"]
    raw_events = []
    reports = []
    hidraw_packets = []
    buffer = b""
    report = empty_report()
    report_index = 0
    phase_start = None
    previous_report_time = None

    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break

        fds = [evdev_fd]
        if hidraw_fd is not None:
            fds.append(hidraw_fd)
        readable, _, _ = select.select(fds, [], [], remaining)
        if not readable:
            continue

        if hidraw_fd is not None and hidraw_fd in readable:
            while True:
                try:
                    packet = os.read(hidraw_fd, 256)
                except BlockingIOError:
                    break
                if not packet:
                    break
                packet_time = time.monotonic()
                if phase_start is None:
                    phase_start = packet_time
                hidraw_packets.append({
                    "t": round(packet_time - phase_start, 6),
                    "size": len(packet),
                    "hex": packet.hex(),
                })

        if evdev_fd not in readable:
            continue

        chunk = os.read(evdev_fd, EVENT_STRUCT.size * 64)
        if not chunk:
            break

        buffer += chunk
        while len(buffer) >= EVENT_STRUCT.size:
            sec, usec, event_type, code, value = EVENT_STRUCT.unpack_from(buffer)
            buffer = buffer[EVENT_STRUCT.size:]
            event_ts = sec + (usec / 1000000.0)
            if phase_start is None:
                phase_start = event_ts
            phase_t = event_ts - phase_start

            event_record = {
                "t": round(phase_t, 6),
                "type": event_type_name(event_type),
                "code": event_code_name(event_type, code),
                "value": value,
            }
            raw_events.append(event_record)

            if event_type == EV_REL:
                if code == REL_X:
                    report["rel_x"] += value
                elif code == REL_Y:
                    report["rel_y"] += value
                elif code == REL_WHEEL:
                    report["wheel"] += value
                elif code == REL_WHEEL_HI_RES:
                    report["wheel_hi_res"] += value
                elif code == REL_HWHEEL:
                    report["hwheel"] += value
                elif code == REL_HWHEEL_HI_RES:
                    report["hwheel_hi_res"] += value
            elif event_type == EV_KEY:
                report["keys"].append({"code": code, "value": value})

            if event_type != EV_SYN or code != SYN_REPORT:
                report["events"].append(event_record)
                continue

            finished = finalize_report(report, phase_t, previous_report_time, report_index)
            reports.append(finished)
            previous_report_time = phase_t
            report_index += 1
            report = empty_report()

    summary = summarize_phase(phase["id"], reports, quiet_gap_ms, hidraw_packets)
    print(
        f"Captured {summary['wheel_report_count']} wheel reports in {summary['gesture_count']} inferred gestures; "
        f"max reports per gesture = {summary['max_reports_per_gesture']}; hidraw packets = {summary['hidraw_packet_count']}."
    )
    return {
        "phase": phase["id"],
        "title": phase["title"],
        "duration_s": phase["duration"],
        "instructions": phase["instructions"],
        "raw_events": raw_events,
        "reports": reports,
        "hidraw_packets": hidraw_packets,
        "summary": summary,
    }


def list_candidates():
    for candidate in candidate_devices():
        by_id = candidate["by_id"][0] if candidate["by_id"] else "-"
        hidraw = find_hidraw_for_event(candidate["devnode"]) or "-"
        handlers = " ".join(candidate["handlers"])
        print(f"{candidate['score']:>3}  {candidate['devnode']:<18}  {hidraw:<14}  {by_id:<60}  {candidate['name']}  [{handlers}]")


def default_output_path():
    timestamp = time.strftime("%Y%m%d_%H%M%S")
    return Path.cwd() / f"mouse_wheel_capture_{timestamp}.json"


def main():
    parser = argparse.ArgumentParser(description="Record raw Linux mouse wheel events for menu navigation analysis.")
    parser.add_argument("--device", help="Explicit event device or /dev/input/by-id path.")
    parser.add_argument("--list", action="store_true", help="List candidate mouse devices and exit.")
    parser.add_argument("--output", help="Output JSON path.")
    parser.add_argument("--countdown", type=int, default=3, help="Seconds before each recording phase starts.")
    parser.add_argument("--quiet-gap-ms", type=float, default=150.0, help="Gap used to infer gestures from wheel reports.")
    args = parser.parse_args()

    if args.list:
        list_candidates()
        return 0

    selection = pick_device(args.device)
    output_path = Path(args.output) if args.output else default_output_path()

    print(f"Selected device: {selection['selected']}")
    print(f"Resolved event: {selection['resolved']}")
    if selection["by_id"]:
        print(f"Stable path: {selection['by_id'][0]}")
    if selection["hidraw"]:
        print(f"Matched hidraw: {selection['hidraw']}")

    try:
        evdev_fd = os.open(selection["resolved"], os.O_RDONLY | os.O_NONBLOCK)
    except PermissionError:
        print("Permission denied opening the input event device.", file=sys.stderr)
        print("Run this tool with sudo, for example:", file=sys.stderr)
        print(f"  sudo python3 {Path(__file__).resolve()}", file=sys.stderr)
        return 1

    hidraw_fd = None
    if selection["hidraw"]:
        try:
            hidraw_fd = os.open(selection["hidraw"], os.O_RDONLY | os.O_NONBLOCK)
        except PermissionError:
            hidraw_fd = None

    try:
        result = {
            "tool": "mouse_wheel_capture.py",
            "captured_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "device": selection,
            "quiet_gap_ms": args.quiet_gap_ms,
            "phases": [],
        }

        print()
        print("This run records four phases:")
        print("  1. Slow up, one notch at a time")
        print("  2. Slow down, one notch at a time")
        print("  3. Fast up")
        print("  4. Fast down")

        for phase in PHASES:
            result["phases"].append(capture_phase(evdev_fd, hidraw_fd, phase, args.countdown, args.quiet_gap_ms))

        output_path.write_text(json.dumps(result, indent=2))
        print()
        print(f"Wrote capture to {output_path}")
        print("Share that JSON file and I can correlate both the evdev wheel stream and the raw HID packets with the firmware thresholds.")
        return 0
    finally:
        os.close(evdev_fd)
        if hidraw_fd is not None:
            os.close(hidraw_fd)


if __name__ == "__main__":
    sys.exit(main())