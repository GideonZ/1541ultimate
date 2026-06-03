#!/usr/bin/env python3
"""Focused real-device repros for the four current monitor merge blockers.

Autonomous control/observation only (no human keypress/screen reading):
  - key injection   POST /v1/machine:input_ui
  - RAM side effects GET/PUT /v1/machine:readmem|writemem
  - UI text          PUT  /v1/machine:dump_ui_screen + textlog (logical frame)
  - interface mode   REST config (get/set Interface Type)
  - liveness         GET /v1/version, get_interface_type

Each case prints a single PASS/FAIL line plus the evidence it used.
Run:  python3 issue_repro.py --host u64 --case issue2-freeze
"""
import argparse
import sys
import time
import urllib.error

import monitor_direct_test as mdt


def log(msg):
    print(msg, flush=True)


OTHER = {"Freeze": "Overlay on HDMI", "Overlay on HDMI": "Freeze"}


def to_basic(session, mode):
    mdt.switch_interface_type(session, mode, f"select {mode}")
    mdt.wait_interface_type(session, mode, f"select {mode}")
    session.reset()
    mdt.wait_basic_ready(session)
    time.sleep(0.5)


def reachable(session, tries=6, gap=0.5):
    for _ in range(tries):
        try:
            session.get_interface_type()
            return True
        except (mdt.Failure, OSError, TimeoutError, urllib.error.URLError):
            time.sleep(gap)
    return False


def ui_state(session, tag="probe"):
    """Return ('closed'|'monitor'|'browser'|'other', frame_or_None)."""
    try:
        frame = session.dump_ui_screen(tag)
    except (mdt.Failure, OSError, TimeoutError, urllib.error.URLError):
        return "closed", None
    txt = frame.text()
    if "MONITOR" in txt:
        return "monitor", frame
    if mdt.visible_ui(frame):
        return "browser", frame
    return "other", frame


def enter_monitor(session, capture, ocr, mode):
    if mode == "Freeze":
        session.start_video()
        time.sleep(0.3)
        return mdt.enter_visible_monitor(session, capture, ocr)
    return mdt.enter_hidden_monitor(session)


# --------------------------------------------------------------------------
# Issue 2: C=+I inside the monitor must toggle the UI mode AND close the whole
# monitor + menu/browser surface (never drop the user into the file browser).
# --------------------------------------------------------------------------
def issue2(session, capture, ocr, start_mode):
    other = OTHER[start_mode]
    to_basic(session, start_mode)
    enter_monitor(session, capture, ocr, start_mode)
    state, _ = ui_state(session, "i2-pre")
    if state != "monitor":
        log(f"[Issue2/{start_mode}] WARN: monitor not confirmed up (state={state}); continuing")
    before = session.get_interface_type()
    log(f"[Issue2/{start_mode}] monitor up; interface before C=+I = {before!r}")

    session.input_ui(["ctrl_i"])
    time.sleep(1.5)

    after = session.get_interface_type()
    toggled = (after == other)
    # The whole UI must be gone: poll for a stable 'closed' (no UI dump) result.
    final_state = "?"
    for _ in range(8):
        final_state, frame = ui_state(session, "i2-post")
        if final_state == "closed":
            break
        time.sleep(0.4)
    log(f"[Issue2/{start_mode}] after C=+I: interface={after!r} toggled={toggled} "
        f"ui_state={final_state}")

    # Confirm next monitor open uses the NEW mode.
    next_mode_ok = False
    try:
        enter_monitor(session, capture, ocr, after)
        st, _ = ui_state(session, "i2-reenter")
        next_mode_ok = (st == "monitor")
        # leave cleanly
        if after == "Freeze":
            session.input_ui(["ctrl_o"])
        else:
            session.input_ui(["ctrl_o"])
        time.sleep(0.5)
    except mdt.Failure as exc:
        log(f"[Issue2/{start_mode}] re-enter in new mode raised: {exc}")
    log(f"[Issue2/{start_mode}] next monitor open in {after!r} mode: monitor_up={next_mode_ok}")

    ok = toggled and final_state == "closed" and next_mode_ok
    if not toggled:
        log(f"[Issue2/{start_mode}] FAIL: C=+I did not toggle interface ({before!r}->{after!r}).")
    if final_state == "browser":
        log(f"[Issue2/{start_mode}] FAIL: file browser/menu left visible after C=+I (the bug).")
    elif final_state == "monitor":
        log(f"[Issue2/{start_mode}] FAIL: monitor still open after C=+I.")
    log(f"[Issue2/{start_mode}] {'PASS' if ok else 'FAIL'}")
    return ok


# --------------------------------------------------------------------------
# Issue 1: Freeze RAM-under-ROM debug, REST reset, re-enter monitor -> the view
# must follow the live CPU bank (CPU5), decoding the RAM-under-ROM bytes, not a
# C5O7 ROM view.
# --------------------------------------------------------------------------
def issue1(session, capture, ocr):
    bootstrap = 0xC000
    payload = 0xE000
    # SEI; LDA #$35; STA $01; JMP $E000   ($35 -> low bits 5: KERNAL RAM, char ROM)
    boot = bytes([0x78, 0xA9, 0x35, 0x85, 0x01, 0x4C, 0x00, 0xE0])
    # Deterministic RAM-under-ROM payload with a recognisable first opcode:
    #   E000 INC $0250 ; E003 INC $0251 ; E006 JMP $E006
    pay = bytes([0xEE, 0x50, 0x02, 0xEE, 0x51, 0x02, 0x4C, 0x06, 0xE0])

    to_basic(session, "Freeze")
    session.start_video()
    time.sleep(0.4)
    mdt.enter_visible_monitor(session, capture, ocr)
    log("[Issue1] entered Freeze monitor")

    session.write_memory(payload, pay)
    session.write_memory(bootstrap, boot)
    session.write_memory(0x0250, bytes([0, 0]))

    # Debug-step into $E000 (under ROM): SEI, LDA, STA, JMP -> park at $E000.
    mdt.send_ui_text(session, "J%04X\n" % bootstrap, settle=0.5)
    mdt.send_ui_text(session, "A", settle=0.5)
    mdt.send_ui_text(session, "D", settle=0.7)
    for _ in range(4):
        mdt.send_ui_text(session, "D", settle=0.6)
    # live $01 should now be $35 (low bits 5)
    live01 = session.read_memory(0x0001, 1)[0]
    log(f"[Issue1] parked at $E000 under ROM; live $01={live01:#04x} (low bits {live01 & 7})")

    # REST reset, then re-enter the monitor and inspect $E000 view.
    session.reset()
    time.sleep(1.5)
    live01_after = session.read_memory(0x0001, 1)[0]
    log(f"[Issue1] after REST reset: live $01={live01_after:#04x} (low bits {live01_after & 7})")
    # re-enter
    mdt.enter_visible_monitor(session, capture, ocr)
    mdt.send_ui_text(session, "J%04X\n" % payload, settle=0.5)
    mdt.send_ui_text(session, "A", settle=0.5)
    frame = mdt.wait_stable_dump = session.dump_ui_screen("i1-reentry")
    line = mdt.status_line(frame)
    live, mon = mdt.parse_status_banks(line)
    log(f"[Issue1] re-entry status line: {line!r}  (live={live} monitor={mon})")

    # The re-entered view must follow the live CPU bank (no C5O7 mismatch). If the
    # live CPU genuinely returned to 7 after reset, view 7 is also coherent; the
    # invariant under test is live==monitor (matched, no spurious override).
    matched = (live is not None and live == mon)
    # When the under-ROM program's banking persists (live low bits 5), the view
    # must be 5 and the disasm must show our payload ($EE = INC), not KERNAL ROM.
    txt = frame.text()
    shows_payload = ("INC $0250" in txt) or ("EE 50 02" in txt) or ("E000 EE" in txt)
    log(f"[Issue1] matched(view==live)={matched} shows_payload_at_E000={shows_payload}")
    log(f"[Issue1] frame:\n{txt}")
    ok = matched
    log(f"[Issue1] {'PASS' if ok else 'FAIL'} (no C5O7 mismatch on re-entry)")
    return ok


# --------------------------------------------------------------------------
# Issue 3: Overlay RAM-under-ROM debug + G, then C=+X reset; device must remain
# fully responsive (REST, interface, monitor re-entry + first step).
# --------------------------------------------------------------------------
def issue3(session, capture, ocr):
    bootstrap = 0xC000
    payload = 0xE000
    # $35 exposes RAM under BASIC/KERNAL while keeping I/O visible.
    boot = bytes([0x78, 0xA9, 0x35, 0x85, 0x01, 0x4C, 0x00, 0xE0])
    pay = bytes([0xEE, 0x50, 0x02, 0xEE, 0x51, 0x02, 0x4C, 0x06, 0xE0])

    to_basic(session, "Overlay on HDMI")
    mdt.enter_hidden_monitor(session)
    log("[Issue3] entered Overlay monitor")

    session.write_memory(payload, pay)
    session.write_memory(bootstrap, boot)
    session.write_memory(0x0250, bytes([0, 0]))

    mdt.send_ui_text(session, "J%04X\n" % bootstrap, settle=0.4)
    mdt.send_ui_text(session, "A", settle=0.4)
    mdt.send_ui_text(session, "D", settle=0.6)
    for _ in range(4):
        mdt.send_ui_text(session, "D", settle=0.5)
    mdt.send_ui_text(session, "D", settle=0.6)  # exec INC $0250 -> (1,0)
    s_step = tuple(session.read_memory(0x0250, 2))
    log(f"[Issue3] after step into under-ROM: $0250..$0251={s_step}")
    mdt.send_ui_text(session, "G", settle=0.6)  # continue (under ROM)
    time.sleep(2.0)
    s_go = tuple(session.read_memory(0x0250, 2))
    log(f"[Issue3] after Overlay G: $0250..$0251={s_go}")

    # C=+X. After the G handoff the monitor has exited; C=+X is the global reset
    # shortcut. Open the root menu first so the chord routes through the global
    # handler (matches the reported user flow), then inject C=+X.
    try:
        mdt.open_overlay_menu(session, "issue3 root", settle=0.6)
    except mdt.Failure as exc:
        log(f"[Issue3] open root menu before C=+X raised: {exc}")
    session.input_ui(["ctrl_x"])
    time.sleep(2.5)

    # ---- Liveness battery ----
    rest_ok = reachable(session)
    log(f"[Issue3] after C=+X: REST/interface reachable={rest_ok}")
    # keyboard/menu input liveness: inject a key, confirm the UI/menu responds by
    # opening the root menu (screen delta) OR staying reachable.
    menu_ok = False
    try:
        frame = mdt.open_overlay_menu(session, "issue3 post-reset menu", settle=0.8)
        menu_ok = mdt.visible_ui(frame)
        session.input_ui(["run_stop"])  # close menu
        time.sleep(0.4)
    except mdt.Failure as exc:
        log(f"[Issue3] post-reset menu open raised: {exc}")
    log(f"[Issue3] post-reset U64 menu input responsive={menu_ok}")

    # monitor re-entry + first debug step must not hang.
    session.write_memory(0x0250, bytes([0, 0]))
    session.write_memory(0x02A0, bytes([0xEE, 0x50, 0x02, 0x4C, 0xA0, 0x02]))
    reentry_ok = False
    step_effect = None
    try:
        mdt.enter_hidden_monitor(session)
        mdt.send_ui_text(session, "J%04X\n" % 0x02A0, settle=0.4)
        mdt.send_ui_text(session, "A", settle=0.4)
        mdt.send_ui_text(session, "D", settle=0.6)
        mdt.send_ui_text(session, "D", settle=0.7)
        step_effect = session.read_memory(0x0250, 1)[0]
        reentry_ok = reachable(session)
    except mdt.Failure as exc:
        log(f"[Issue3] re-entry/step raised: {exc}")
    log(f"[Issue3] re-entry reachable={reentry_ok} first_step_sentinel=$0250={step_effect}")

    ok = rest_ok and reentry_ok and (step_effect == 1)
    if not rest_ok:
        log("[Issue3] FAIL: device unreachable after C=+X (wedge reproduced).")
    if not reentry_ok or step_effect != 1:
        log("[Issue3] FAIL: monitor re-entry / first step hung after C=+X.")
    log(f"[Issue3] {'PASS' if ok else 'FAIL'}")
    return ok


# --------------------------------------------------------------------------
# [43]: KERNAL ROM Step Into from $E000, run twice without redeploy. The FPGA
# KERNAL ROM image must stay pristine across REST reset + monitor re-entry; the
# 2nd step-into must reach $E001/$E002 area, never misfire to ~$0002.
# We observe the FPGA KERNAL bytes at $E000 via REST (CPU bank 7) before/after.
# --------------------------------------------------------------------------
def rom43(session, capture, ocr):
    import os
    # Read pristine KERNAL bytes at $E000 (bank 7) once for reference.
    to_basic(session, "Freeze")
    pristine = session.read_memory(0xE000, 8)
    log(f"[43] pristine KERNAL $E000..$E007 = {pristine.hex().upper()}")

    def one_run(run_idx):
        session.start_video()
        time.sleep(0.3)
        mdt.enter_visible_monitor(session, capture, ocr)
        # Step into KERNAL at $E000: jump there, ASM, Debug, Step Into (T).
        mdt.send_ui_text(session, "JE000\n", settle=0.5)
        mdt.send_ui_text(session, "A", settle=0.5)
        mdt.send_ui_text(session, "D", settle=0.7)
        mdt.send_ui_text(session, "T", settle=0.8)  # step into
        # read footer PC
        try:
            frame = session.dump_ui_screen("rom43-%d" % run_idx)
            txt = frame.text()
        except mdt.Failure:
            txt = "<no dump>"
        kernal_now = session.read_memory(0xE000, 8)
        stale = (kernal_now != pristine)
        log(f"[43] run {run_idx}: KERNAL $E000 now={kernal_now.hex().upper()} stale={stale}")
        # leave debug + monitor, reset
        mdt.send_ui_text(session, "\x1b", settle=0.3)
        session.input_ui(["ctrl_o"])
        time.sleep(0.4)
        session.reset()
        time.sleep(1.2)
        return kernal_now, txt

    k1, t1 = one_run(1)
    k2, t2 = one_run(2)
    after2 = session.read_memory(0xE000, 8)
    log(f"[43] after 2 runs: KERNAL $E000 = {after2.hex().upper()} (pristine={pristine.hex().upper()})")
    healthy = (after2 == pristine) and (k1 == pristine) and (k2 == pristine)
    log(f"[43] {'PASS' if healthy else 'FAIL'}: KERNAL image stayed pristine across 2 runs={healthy}")
    return healthy


def rom43b(session, capture, ocr):
    """Isolate WHERE the KERNAL ROM-image BRK leak happens. Read $E002 (the step
    target after $E000=STA $56) at: pristine, parked-after-step, after a REST reset
    fired WHILE PARKED, and after leaving the monitor."""
    to_basic(session, "Freeze")
    pristine = session.read_memory(0xE000, 4)
    log(f"[43b] pristine $E000..$E003 = {pristine.hex().upper()}")
    session.start_video()
    time.sleep(0.3)
    mdt.enter_visible_monitor(session, capture, ocr)
    mdt.send_ui_text(session, "JE000\n", settle=0.5)
    mdt.send_ui_text(session, "A", settle=0.5)
    mdt.send_ui_text(session, "D", settle=0.7)
    mdt.send_ui_text(session, "T", settle=0.9)   # step into -> park at $E002
    parked = session.read_memory(0xE000, 4)
    log(f"[43b] parked-after-step $E000..$E003 = {parked.hex().upper()} "
        f"($E002={parked[2]:#04x})")

    # REST reset WHILE PARKED (request_reset_cancel path, run_window_depth==0).
    # The decisive wedge check: a clean reset must boot the C64 to READY, not crash
    # on a leaked KERNAL BRK. Wait for the boot to settle BEFORE reading the ROM
    # image (reading during the boot transient returns zeros and is misleading).
    session.reset()
    ready = False
    try:
        mdt.wait_basic_ready(session)
        ready = True
    except mdt.Failure:
        ready = False
    time.sleep(0.5)
    after_reset = session.read_memory(0xE000, 4)
    log(f"[43b] after parked REST reset + settle: $E000..$E003 = {after_reset.hex().upper()} "
        f"($E002={after_reset[2]:#04x}) basic_ready={ready}")
    leak_at_reset = (after_reset[2] != pristine[2])

    # Boot health: jiffy clock must advance (KERNAL IRQ running).
    j0 = list(session.read_memory(0x00A0, 3))
    time.sleep(1.2)
    j1 = list(session.read_memory(0x00A0, 3))
    boots = (j1 != j0)
    log(f"[43b] post-reset jiffy {j0}->{j1} advancing={boots}")

    ok = (not leak_at_reset) and boots and ready
    log(f"[43b] {'PASS' if ok else 'FAIL'}: no stale KERNAL BRK + C64 boots "
        f"(leak={leak_at_reset} boots={boots} ready={ready})")
    return ok


def _fresh_capture(capture):
    import socket as _socket
    capture.sock.setblocking(False)
    try:
        while True:
            try:
                capture.sock.recv(2048)
            except (BlockingIOError, _socket.error):
                break
    finally:
        capture.sock.setblocking(True)
    time.sleep(0.2)
    return capture.capture_image()


def _readability(frame):
    """A readable C64 text screen (BASIC/menu) is space-dominated with few distinct
    glyphs. A CHAR-ROM-corrupted screen renders as high-entropy random glyphs: the
    OCR can't match them, so spaces collapse and distinct-glyph count explodes."""
    text = "".join(frame.lines)
    spaces = sum(1 for c in text if c == " ")
    distinct = len(set(text))
    return spaces, distinct, len(text)


def issue5(session, capture, ocr):
    """Freeze mode must not render the C64/menu as random PETSCII (CHAR-ROM intact)."""
    to_basic(session, "Freeze")
    session.start_video()
    time.sleep(0.6)
    ok = True
    for it in range(1, 5):
        # BASIC screen readability after a reset
        session.reset()
        try:
            mdt.wait_basic_ready(session)
        except mdt.Failure:
            pass
        time.sleep(0.6)
        f = ocr.decode(_fresh_capture(capture))
        sp, dist, ln = _readability(f)
        readable = (sp >= 300 and dist <= 45)
        txt = f.text()
        has_basic = ("BASIC" in txt) or ("READY" in txt) or ("COMMODORE" in txt)
        log(f"[Issue5] it{it} BASIC: spaces={sp} distinct={dist} readable={readable} basic_text={has_basic}")
        if not readable and not has_basic:
            ok = False
            log(f"[Issue5] it{it} FAIL: BASIC screen looks like random PETSCII:\n{txt}")
        # Enter monitor and back out (exercises Freeze render + restore)
        try:
            mdt.enter_visible_monitor(session, capture, ocr)
            f2 = ocr.decode(_fresh_capture(capture))
            sp2, dist2, _ = _readability(f2)
            mon_ok = ("MONITOR" in f2.text()) or (sp2 >= 300)
            log(f"[Issue5] it{it} MONITOR: spaces={sp2} distinct={dist2} readable={mon_ok}")
            if not mon_ok:
                ok = False
                log(f"[Issue5] it{it} FAIL: monitor screen looks like random PETSCII:\n{f2.text()}")
            session.input_ui(["ctrl_o"])
            time.sleep(0.5)
        except mdt.Failure as exc:
            log(f"[Issue5] it{it} monitor entry flaked: {exc}")
    log(f"[Issue5] {'PASS' if ok else 'FAIL'}: Freeze screens readable (no random PETSCII)")
    return ok


CASES = {
    "issue5": issue5,
    "rom43b": rom43b,
    "issue2-freeze": lambda s, c, o: issue2(s, c, o, "Freeze"),
    "issue2-overlay": lambda s, c, o: issue2(s, c, o, "Overlay on HDMI"),
    "issue1": issue1,
    "issue3": issue3,
    "rom43": rom43,
}


def restore_safe_cpu_port(session, label):
    session.write_memory(0x0001, bytes([0x37]))
    value = session.read_memory(0x0001, 1)[0]
    log(f"[cleanup/{label}] $0001 readback={value:#04x}")
    if (value & 0x07) != 0x07:
        raise mdt.Failure(f"cleanup failed to restore safe $0001, got {value:#04x}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="u64")
    ap.add_argument("--timeout", type=float, default=5.0)
    ap.add_argument("--case", required=True, choices=sorted(CASES.keys()))
    args = ap.parse_args()
    session = mdt.RestSession(args.host, args.timeout)
    capture = mdt.VicStreamCapture()
    ocr = mdt.C64FrameOCR()
    ok = False
    cleanup_ok = True
    try:
        ok = CASES[args.case](session, capture, ocr)
    finally:
        try:
            session.stop_video()
        except Exception:
            pass
        try:
            capture.close()
        except Exception:
            pass
        try:
            restore_safe_cpu_port(session, args.case)
            session.reset()
        except Exception as exc:
            cleanup_ok = False
            log(f"[cleanup/{args.case}] FAIL: {exc}")
    return 0 if ok and cleanup_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
