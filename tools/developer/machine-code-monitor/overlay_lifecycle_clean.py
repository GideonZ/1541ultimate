#!/usr/bin/env python3
"""Direct UI debug lifecycle reproducer using ONLY production REST APIs.

Sequence per iteration (the handover's broken path):
  enter monitor -> debug step -> set bp -> continue to bp -> continue full speed
  -> reset -> RE-ENTER monitor -> re-enter debug -> debug step
with a network-liveness probe after each iteration. No temporary E2E hooks.
"""
import argparse
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.parse

sys.path.insert(0, "tools/developer/machine-code-monitor")
from mcm_rest import Rest, wait_for, Failure  # noqa: E402

READY_SCREEN_TOKEN = bytes([0x12, 0x05, 0x01, 0x04, 0x19])

C300_LOOP = bytes([
    0xA2, 0x00,             # LDX #$00       ; $C300
    0xFE, 0x00, 0x06,       # INC $0600,X    ; $C302
    0xFE, 0xE8, 0x06,       # INC $06E8,X
    0xE8,                   # INX
    0x8E, 0x20, 0xD0,       # STX $D020
    0x4C, 0x02, 0xC3,       # JMP $C302
])

HIDDEN_BOOTSTRAP = bytes([
    0x78,                   # SEI
    0xA2, 0x00,             # LDX #$00
    0xA9, 0x37,             # LDA #$37
    0x85, 0x00,             # STA $00
    0xA9, 0x35,             # LDA #$35
    0x85, 0x01,             # STA $01
    0x4C, 0x00, 0xE0,       # JMP $E000
])

HIDDEN_PAYLOAD = bytes([
    0xFE, 0x00, 0x04,       # INC $0400,X
    0xFE, 0x00, 0x05,       # INC $0500,X
    0xE8,                   # INX
    0x8E, 0x21, 0xD0,       # STX $D021
    0x4C, 0x00, 0xE0,       # JMP $E000
])

SCENARIOS = ("ram", "ram-under-rom", "rom")
MODES = ("Overlay on HDMI", "Freeze")


def request_json(r: Rest, method, path, params=None, body=None):
    import json
    data = None if body is None else json.dumps(body).encode()
    _, raw = r.req(method, path, params=params, body=data,
                   ctype="application/json" if body is not None else None)
    return json.loads(raw.decode("utf-8"))


def set_interface_type(r: Rest, value: str):
    r.req("PUT", "/v1/configs/User%20Interface%20Settings/Interface%20Type",
          params={"value": urllib.parse.quote(value)})
    data = request_json(r, "GET", "/v1/configs/User%20Interface%20Settings/Interface%20Type")
    current = data["User Interface Settings"]["Interface Type"]["current"]
    if current != value:
        raise Failure(f"Interface Type is {current!r}, expected {value!r}")


def wait_ready(r: Rest, timeout=9.0):
    deadline = time.time() + timeout
    stable_since = 0.0
    while time.time() < deadline:
        screen = r.read_mem(0x0400, 1000)
        if READY_SCREEN_TOKEN in screen:
            now = time.time()
            if stable_since == 0.0:
                stable_since = now
            elif now - stable_since >= 0.75:
                jiffy = r.read_mem(0x00A2, 3)
                jiffy_deadline = time.time() + 2.0
                while time.time() < jiffy_deadline:
                    time.sleep(0.1)
                    if r.read_mem(0x00A2, 3) != jiffy:
                        return
                raise Failure("READY appeared but jiffy clock did not advance")
        else:
            stable_since = 0.0
        time.sleep(0.1)
    raise Failure("C64 reset did not reach READY")


def reset_to_basic(r: Rest, label="reset"):
    try:
        r.reset()
    except (OSError, TimeoutError, urllib.error.URLError) as exc:
        print(f"  [info] {label}: reset response timed out, checking READY: {exc}", flush=True)
    wait_ready(r)
    time.sleep(1.0)


def tcp_accepts(host: str, port: int, timeout=4.0):
    s = socket.create_connection((host, port), timeout=timeout)
    s.close()


def check_liveness(r: Rest, label: str):
    subprocess.run(["ping", "-c1", "-W2", r.host], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    request_json(r, "GET", "/v1/version")
    tcp_accepts(r.host, 23)
    time.sleep(1.5)
    tcp_accepts(r.host, 21)
    r.read_mem(0x0400, 1)


def load_fixture(r: Rest):
    r.write_mem(0x0400, bytes([0x00]) * 0x3E8)
    r.write_mem(0xC000, HIDDEN_BOOTSTRAP)
    r.write_mem(0xE000, HIDDEN_PAYLOAD)
    r.write_mem(0xC300, C300_LOOP)
    back = r.read_mem(0xC300, len(C300_LOOP))
    if back != C300_LOOP:
        raise Failure(f"fixture did not round-trip: {back.hex()}")


def open_monitor(r: Rest, label: str):
    def monitor_is_open():
        lines = r.screen_lines()
        return any(line.lstrip().startswith("MONITOR ") and "$" in line for line in lines)

    def visible_menu_text():
        try:
            text = r.screen_text()
        except Exception:
            return ""
        return text if text.strip() else ""

    # If a monitor is already up, reuse it.
    try:
        if monitor_is_open():
            return
    except Exception:
        pass
    force_menu_button = False
    visible_misses = 0
    for _ in range(8):
        if force_menu_button or not visible_menu_text():
            r.menu_button()
            time.sleep(2.5)
            force_menu_button = False
        deadline = time.time() + 3.0
        while time.time() < deadline:
            try:
                lines = r.screen_lines()
                if any(line.lstrip().startswith("MONITOR ") and "$" in line for line in lines):
                    return
                text = "\n".join(lines)
                if text.strip():
                    break
            except Exception:
                pass
            time.sleep(0.2)
        try:
            r.tap(["ctrl", "o"])
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            print(f"  [info] {label}: ctrl+O timed out, checking monitor state: {exc}",
                  flush=True)
        time.sleep(1.2)
        try:
            wait_for_monitor(r, f"{label}: monitor open", timeout=2.5)
            return
        except Failure:
            visible_misses += 1
            if visible_misses >= 2:
                force_menu_button = True
                visible_misses = 0
            time.sleep(0.6)
    wait_for_monitor(r, f"{label}: monitor open (final)", timeout=4.0)


def wait_for_monitor(r: Rest, label: str, timeout=6.0):
    deadline = time.time() + timeout
    last = ""
    while time.time() < deadline:
        try:
            lines = r.screen_lines()
            last = "\n".join(lines)
            if any(line.lstrip().startswith("MONITOR ") and "$" in line for line in lines):
                return last
        except Exception:
            pass
        time.sleep(0.2)
    raise Failure(f"{label}: timed out waiting for monitor screen\n--- last screen ---\n{last}")


def wait_pc(r: Rest, pc: int, label: str, timeout=6.0):
    deadline = time.time() + timeout
    last = ""
    while time.time() < deadline:
        lines = r.screen_lines()
        last = "\n".join(lines)
        for i, line in enumerate(lines[:-1]):
            if "PC" in line and "NV-BDIZC" in line:
                fields = lines[i + 1].split()
                if fields and fields[0].upper() == f"{pc:04X}":
                    return last
        time.sleep(0.2)
    raise Failure(f"{label}: timed out waiting for debug PC ${pc:04X}\n--- last screen ---\n{last}")


def status_line(r: Rest):
    lines = r.screen_lines()
    for line in lines:
        if "CPU" in line and "VIC" in line:
            return line
        if "$A:" in line and "VIC" in line:
            return line
    return ""


def select_monitor_bank(r: Rest, bank: int, label: str):
    for _ in range(8):
        status = status_line(r)
        if f"CPU{bank}" in status or f"O{bank}" in status:
            return
        r.send_text("o", settle=0.18)
        time.sleep(0.25)
    raise Failure(f"{label}: could not select monitor bank {bank}; status={status_line(r)!r}")


def line_for_address(r: Rest, addr: int):
    prefix = f" {addr:04X} "
    for line in r.screen_lines():
        if line.startswith(prefix):
            return line
    return ""


def goto_addr(r: Rest, addr: int, label: str):
    r.send_text(f"j{addr:04X}\n")
    time.sleep(0.3)
    wait_for(r, [f"MONITOR", f"${addr:04X}"], f"{label}: goto ${addr:04X}")


def enter_debug_at(r: Rest, addr: int, bank: int, source: str, label: str):
    select_monitor_bank(r, bank, f"{label}: select bank {bank}")
    goto_addr(r, addr, label)
    r.send_text("a")
    time.sleep(0.3)
    wait_for(r, [f"MONITOR ASM ${addr:04X}"], f"{label}: ASM ${addr:04X}")
    row = line_for_address(r, addr)
    if f"[{source}]" not in row:
        raise Failure(f"{label}: row ${addr:04X} missing [{source}]: {row!r}\n{r.screen_text()}")
    r.send_text("d")
    time.sleep(0.3)
    wait_for(r, "NV-BDIZC", f"{label}: enter Debug")


def ensure_breakpoint_at(r: Rest, addr: int, bank: int, source: str, label: str):
    select_monitor_bank(r, bank, f"{label}: select bp bank {bank}")
    goto_addr(r, addr, label)
    row = line_for_address(r, addr)
    if f"[{source}]" not in row:
        raise Failure(f"{label}: bp row ${addr:04X} missing [{source}]: {row!r}")
    if "[BRK" in row:
        return
    r.send_text("r")
    time.sleep(0.3)
    row = line_for_address(r, addr)
    if "[BRK" not in row:
        raise Failure(f"{label}: breakpoint was not set at ${addr:04X}: {row!r}\n{r.screen_text()}")


def clear_breakpoint_at(r: Rest, addr: int, bank: int, label: str):
    select_monitor_bank(r, bank, f"{label}: select clear bank {bank}")
    goto_addr(r, addr, label)
    row = line_for_address(r, addr)
    if "[BRK" not in row:
        return
    r.send_text("r")
    time.sleep(0.3)
    row = line_for_address(r, addr)
    if "[BRK" in row:
        raise Failure(f"{label}: breakpoint was not cleared at ${addr:04X}: {row!r}")


def assert_region_changes(r: Rest, addr: int, length: int, label: str,
                          minimum_cells=1, timeout=4.0):
    previous = r.read_mem(addr, length)
    changed = set()
    deadline = time.time() + timeout
    while time.time() < deadline and len(changed) < minimum_cells:
        time.sleep(0.1)
        current = r.read_mem(addr, length)
        for i, (a, b) in enumerate(zip(previous, current)):
            if a != b:
                changed.add(i)
        previous = current
    if len(changed) < minimum_cells:
        raise Failure(f"{label}: ${addr:04X}-${addr + length - 1:04X} did not keep changing")


def first_session(r: Rest, scenario: str, label: str):
    open_monitor(r, label)
    if scenario == "ram":
        enter_debug_at(r, 0xC300, 7, "RAM", label)
        r.send_text("d")
        wait_pc(r, 0xC302, f"{label}: step to C302")
        ensure_breakpoint_at(r, 0xC302, 7, "RAM", f"{label}: set bp C302")
        r.send_text("g")
        wait_pc(r, 0xC302, f"{label}: run to bp C302")
        clear_breakpoint_at(r, 0xC302, 7, f"{label}: remove bp C302")
        r.send_text("g")
        assert_region_changes(r, 0x0600, 0x01E8, f"{label}: RAM loop after continue",
                              minimum_cells=2)
    elif scenario == "ram-under-rom":
        enter_debug_at(r, 0xC000, 7, "RAM", label)
        for pc in (0xC001, 0xC003, 0xC005, 0xC007, 0xC009, 0xC00B, 0xE000):
            r.send_text("d")
            wait_pc(r, pc, f"{label}: step to {pc:04X}")
        ensure_breakpoint_at(r, 0xE000, 5, "RAM", f"{label}: set bp E000")
        r.send_text("g")
        wait_pc(r, 0xE000, f"{label}: run to bp E000")
        clear_breakpoint_at(r, 0xE000, 5, f"{label}: remove bp E000")
        r.send_text("g")
        assert_region_changes(r, 0x0400, 0x0200, f"{label}: under-ROM loop after continue",
                              minimum_cells=2)
    elif scenario == "rom":
        enter_debug_at(r, 0xE002, 7, "KRN", label)
        ensure_breakpoint_at(r, 0xBC0F, 7, "BAS", f"{label}: set bp BC0F")
        r.send_text("g")
        wait_pc(r, 0xBC0F, f"{label}: run to bp BC0F")
        clear_breakpoint_at(r, 0xBC0F, 7, f"{label}: remove bp BC0F")
        r.send_text("g")
        time.sleep(1.0)
    else:
        raise Failure(f"unknown scenario {scenario!r}")


def reenter_and_step(r: Rest, scenario: str, label: str):
    reset_to_basic(r, f"{label}: reset")
    check_liveness(r, f"{label}: liveness after reset")
    open_monitor(r, label + " reenter")
    if scenario == "ram":
        enter_debug_at(r, 0xC300, 7, "RAM", label + " reenter")
        r.send_text("d")
        wait_pc(r, 0xC302, f"{label}: re-entered step to C302")
    elif scenario == "ram-under-rom":
        enter_debug_at(r, 0xC000, 7, "RAM", label + " reenter")
        r.send_text("d")
        wait_pc(r, 0xC001, f"{label}: re-entered step to C001")
    elif scenario == "rom":
        enter_debug_at(r, 0xE002, 7, "KRN", label + " reenter")
        r.send_text("t")
        wait_pc(r, 0xBC0F, f"{label}: re-entered trace to BC0F")
    check_liveness(r, f"{label}: liveness after re-entry")
    r.release_all(); time.sleep(0.3)


def run_scenario(r: Rest, mode: str, scenario: str, iters: int):
    set_interface_type(r, mode)
    for i in range(1, iters + 1):
        label = f"clean {mode} {scenario} iter {i}/{iters}"
        print(f"[{i}] {label} ...", flush=True)
        reset_to_basic(r, label)
        check_liveness(r, f"{label}: initial liveness")
        load_fixture(r)
        first_session(r, scenario, label)
        reenter_and_step(r, scenario, label)
        check_liveness(r, f"{label}: final liveness")
        print(f"  [alive] iter {i} complete", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host", nargs="?", default="u64")
    parser.add_argument("iters", nargs="?", type=int, default=6)
    parser.add_argument("--mode", choices=MODES + ("all",), default="Overlay on HDMI")
    parser.add_argument("--scenario", choices=SCENARIOS + ("all",), default="ram")
    args = parser.parse_args()

    r = Rest(args.host, 12)
    modes = MODES if args.mode == "all" else (args.mode,)
    scenarios = SCENARIOS if args.scenario == "all" else (args.scenario,)
    for mode in modes:
        for scenario in scenarios:
            run_scenario(r, mode, scenario, args.iters)
    print("\n=== NO WEDGE across all iterations ===", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as e:
        print(f"\nFAIL: {e}", flush=True)
        # liveness on failure
        try:
            print("alive after failure:", Rest(sys.argv[1] if len(sys.argv) > 1 else "u64").alive())
        except Exception:
            pass
        raise SystemExit(1)
