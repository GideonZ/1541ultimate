#!/usr/bin/env python3
"""Local-UI (overlay/freeze) Machine Code Monitor driver over the SHIPPED REST API.

Uses only machine:input (keyboard injection), machine:menu_button, machine:menu_screen,
machine:readmem/writemem (via mcm_rest.Rest). Replaces the dead input_ui/dump_ui_screen API.

Discipline contract (Section 2 of the gate handover):
  * momentary keys are sent as transition="tap"
  * release_all between navigation steps
  * after a state-changing key, poll menu_screen and WAIT for the expected text
  * menu_screen 404 during transitions is normal -> retry (mcm_rest handles this)
  * pace operations; never flood

This module is the foundation for both the wedge repro/soak and monitor_debug_stress.py.
"""
import sys
import time

sys.path.insert(0, "tools/developer/machine-code-monitor")
import mcm_rest as R  # noqa: E402


# ---- screen helpers -------------------------------------------------------

def screen_or_none(rest):
    """Return screen text, or None if no UI is active (menu_screen 404)."""
    try:
        return rest.screen_text()
    except Exception:  # noqa: BLE001
        return None


def menu_active(rest):
    return screen_or_none(rest) is not None


def header_line(rest):
    ls = None
    try:
        ls = rest.screen_lines()
    except Exception:  # noqa: BLE001
        return ""
    return ls[0] if ls else ""


def wait_text(rest, needle, timeout=6.0, interval=0.2):
    """Poll menu_screen until `needle` appears (case-insensitive). Returns text or None."""
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        t = screen_or_none(rest)
        last = t
        if t and needle.lower() in t.lower():
            return t
        time.sleep(interval)
    return last if (last and needle.lower() in last.lower()) else None


def wait_no_menu(rest, timeout=6.0, interval=0.2):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not menu_active(rest):
            return True
        time.sleep(interval)
    return not menu_active(rest)


# ---- navigation primitives (disciplined) ----------------------------------

def settle(rest, s=0.25):
    rest.release_all()
    time.sleep(s)


def ensure_menu_closed(rest, tries=5):
    for _ in range(tries):
        if not menu_active(rest):
            return True
        rest.menu_button()
        time.sleep(0.6)
    return not menu_active(rest)


def ensure_menu_open(rest, tries=5):
    for _ in range(tries):
        if menu_active(rest):
            return True
        rest.menu_button()
        time.sleep(0.6)
    return menu_active(rest)


def open_monitor(rest, timeout=8.0):
    """Open the Machine Code Monitor via menu + C=+O. Returns the monitor screen text or None."""
    if not ensure_menu_open(rest):
        return None
    # C=+O chord (Commodore held + O)
    rest.tap(["commodore", "o"])
    time.sleep(0.4)
    settle(rest)
    t = wait_text(rest, "MONITOR", timeout=timeout)
    return t


def to_assembly_debug(rest):
    """From an open monitor, switch to Assembly view (A) and enter Debug (D)."""
    rest.tap(["a"]); settle(rest)
    t = wait_text(rest, "ASM", timeout=4.0)
    rest.tap(["d"]); settle(rest)  # first D enters Debug (no execution)
    t = wait_text(rest, "Dbg", timeout=4.0)
    return t


def close_monitor(rest):
    """Close the monitor cleanly (RUN/STOP), then ensure the menu is closed."""
    rest.tap(["run_stop"]); settle(rest, 0.4)
    # If still in monitor (e.g. debug mode swallowed it), exit debug then run_stop again.
    t = screen_or_none(rest)
    if t and "MONITOR" in t.upper():
        rest.tap(["commodore", "d"]); settle(rest, 0.3)  # exit debug
        rest.tap(["run_stop"]); settle(rest, 0.4)
    ensure_menu_closed(rest)


def toggle_ui_mode(rest):
    """C=+I toggles Freeze<->Overlay (auto-closes the menu)."""
    rest.tap(["commodore", "i"]); settle(rest, 0.3)


# ---- probe ----------------------------------------------------------------

def probe(host):
    rest = R.Rest(host=host)
    print("alive:", rest.alive())
    print("== ensure closed ==")
    print("  closed:", ensure_menu_closed(rest))
    time.sleep(0.5)
    print("== open menu ==")
    print("  open:", ensure_menu_open(rest))
    h = header_line(rest)
    print("  header:", repr(h))
    print("== open monitor via C=+O ==")
    t = open_monitor(rest)
    print("  monitor header:", repr(header_line(rest)))
    print("  MONITOR seen:", bool(t and "MONITOR" in t.upper()))
    if t:
        for i, ln in enumerate(t.split("\n")):
            if ln.strip():
                print(f"   {i:2d}|{ln}")
    print("== enter ASM+Debug ==")
    td = to_assembly_debug(rest)
    print("  header now:", repr(header_line(rest)))
    print("  Dbg seen:", bool(td and "Dbg" in td))
    if td:
        for i, ln in enumerate(td.split("\n")):
            if ln.strip():
                print(f"   {i:2d}|{ln}")
    print("== close monitor ==")
    close_monitor(rest)
    print("  menu closed:", not menu_active(rest))
    print("  alive at end:", rest.alive())


def recover_baseline(rest):
    """Universal recovery: C=+X breaks out of any monitor/debug state, then
    leave the firmware menu open as the cycle baseline. Always REST-safe."""
    rest.tap(["commodore", "x"]); settle(rest, 0.4)
    time.sleep(0.4)
    # Land on a known firmware-menu baseline (menu open is fine; we reopen from it).
    ensure_menu_open(rest)


def disciplined_cycle(rest):
    """One paced open/observe/navigate/close cycle exercising the machine:input +
    menu + monitor path (the B14 wedge trigger) WITHOUT stepping uninitialised
    BRK bytes. Raises R.Failure on failure to reach an expected state."""
    t = open_monitor(rest)
    if not (t and "MONITOR" in t.upper()):
        raise R.Failure("monitor did not open")
    # Cycle through every view (heavy input + redraw churn).
    for key, tag in (("m", "HEX"), ("a", "ASM"), ("b", "BIN"),
                     ("i", "ASC"), ("v", "SCR"), ("m", "HEX")):
        rest.tap([key]); settle(rest, 0.15)
        wait_text(rest, tag, timeout=3.0)
    # Jump to a couple of addresses (J <hex> RETURN).
    for addr in ("e000", "0400", "0801"):
        rest.tap(["j"]); settle(rest, 0.1)
        rest.send_text(addr, settle=0.06)
        rest.tap(["return"]); settle(rest, 0.15)
    # Enter ASM + Debug then leave debug (no execution of BRK bytes).
    rest.tap(["a"]); settle(rest, 0.15); wait_text(rest, "ASM", timeout=3.0)
    rest.tap(["d"]); settle(rest, 0.15)
    if not wait_text(rest, "Dbg", timeout=3.0):
        raise R.Failure("debug mode not entered")
    rest.tap(["commodore", "d"]); settle(rest, 0.2)  # exit debug
    # Close cleanly.
    rest.tap(["run_stop"]); settle(rest, 0.4)
    t2 = screen_or_none(rest)
    if t2 and "MONITOR" in t2.upper():
        raise R.Failure("monitor did not close on run_stop")


def aggressive_burst(rest):
    """Deliberately UNdisciplined: rapid menu_button toggles + bursts without release_all."""
    # rapid menu_button toggling (no settle, no verify)
    for _ in range(6):
        rest.menu_button()
        time.sleep(0.05)
    # ensure something is open, then flood taps without release_all
    ensure_menu_open(rest)
    for k in ("cursor_up_down", "cursor_up_down", "f5", "return",
              "cursor_up_down", "run_stop", "commodore", "o"):
        try:
            rest.tap([k])
        except Exception:  # noqa: BLE001
            pass
        time.sleep(0.02)
    # interleaved fast polling during transitions
    for _ in range(8):
        screen_or_none(rest)
        time.sleep(0.02)
    # cleanup
    rest.release_all()
    ensure_menu_closed(rest)


def soak(host, mode, cycles, ui, logpath):
    rest = R.Rest(host=host)
    import datetime
    f = open(logpath, "a", buffering=1) if logpath else None

    def log(msg):
        line = f"{datetime.datetime.now().strftime('%H:%M:%S')} {msg}"
        print(line, flush=True)
        if f:
            f.write(line + "\n")

    log(f"SOAK start mode={mode} cycles={cycles} ui={ui} host={host} alive={rest.alive()}")
    if not rest.alive():
        log("DEVICE NOT ALIVE AT START"); return 2
    recover_baseline(rest)

    ui_modes = {"freeze": ["freeze"], "overlay": ["overlay"],
                "both": ["freeze", "overlay"]}[ui]
    # Establish a known UI mode at start; default is freeze. We toggle to reach overlay.
    current = "freeze"

    wedges = 0
    errors = 0
    for i in range(1, cycles + 1):
        want = ui_modes[(i - 1) % len(ui_modes)]
        if want != current:
            ensure_menu_closed(rest)
            toggle_ui_mode(rest)
            current = want
            time.sleep(0.3)
        try:
            if mode == "disciplined":
                disciplined_cycle(rest)
            else:
                aggressive_burst(rest)
        except R.Failure as e:
            errors += 1
            log(f"cycle {i} [{current}] STATE-ERROR: {e}")
            if rest.alive():
                recover_baseline(rest)
        except Exception as e:  # noqa: BLE001
            errors += 1
            log(f"cycle {i} [{current}] EXC: {type(e).__name__}: {e}")
            if rest.alive():
                recover_baseline(rest)
        # liveness gate
        alive = rest.alive()
        if not alive:
            # confirm with a couple retries (transient socket vs real wedge)
            time.sleep(2.0)
            alive = rest.alive()
        if not alive:
            wedges += 1
            log(f"cycle {i} [{current}] *** WEDGE: REST dead -> stopping for post-mortem ***")
            # also check .70
            r70 = R.Rest(host="192.168.1.70")
            log(f"  .70 alive: {r70.alive()}")
            break
        if i % 10 == 0 or mode == "aggressive":
            log(f"cycle {i}/{cycles} [{current}] ok (errors={errors})")
    log(f"SOAK end wedges={wedges} errors={errors} final_alive={rest.alive()}")
    if f:
        f.close()
    return 2 if wedges else 0


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "probe"
    host = sys.argv[2] if len(sys.argv) > 2 else "u64"
    if cmd == "probe":
        probe(host)
    elif cmd == "soak":
        import argparse
        p = argparse.ArgumentParser()
        p.add_argument("cmd")
        p.add_argument("host", nargs="?", default="u64")
        p.add_argument("--mode", default="disciplined", choices=["disciplined", "aggressive"])
        p.add_argument("--cycles", type=int, default=50)
        p.add_argument("--ui", default="freeze", choices=["freeze", "overlay", "both"])
        p.add_argument("--log", default="")
        a = p.parse_args()
        sys.exit(soak(a.host, a.mode, a.cycles, a.ui, a.log))
    else:
        print("usage: mcm_localui.py [probe|soak] [host] [--mode --cycles --ui --log]")
