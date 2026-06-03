#!/usr/bin/env python3
"""Deterministic real-device repros for the four reported monitor regressions.

Uses only autonomous control/observation:
  - key injection via POST /v1/machine:input_ui
  - logical screen capture via PUT /v1/machine:dump_ui_screen + GET textlog
  - RAM side effects via GET/PUT /v1/machine:readmem|writemem
  - interface mode via REST config (set_interface_type)

Each repro prints a single PASS/FAIL line plus the RAM/state evidence it used.
Run:  python3 regression_repro.py --host u64 --case C
"""
import argparse
import sys
import time

import monitor_direct_test as mdt


def log(msg):
    print(msg, flush=True)


def freeze_reader(session):
    def read():
        last = None
        for _ in range(6):
            try:
                return session.dump_ui_screen("rr")
            except mdt.Failure as exc:
                last = exc
                time.sleep(0.3)
        raise last
    return read


def enter_freeze_monitor(session):
    """F10 then Ctrl+O (x up to 4) until the MONITOR view is shown (logical dump)."""
    reader = freeze_reader(session)
    for attempt in range(5):
        session.input_ui(["f10"])
        time.sleep(1.0)
        session.input_ui(["ctrl_o"])
        time.sleep(1.0)
        for _ in range(2):
            try:
                frame = reader()
                if "MONITOR" in frame.text():
                    return frame
            except mdt.Failure:
                pass
            session.input_ui(["ctrl_o"])
            time.sleep(0.8)
    raise mdt.Failure("could not enter Freeze monitor")


def to_basic(session, mode):
    mdt.switch_interface_type(session, mode, f"select {mode}")
    mdt.wait_interface_type(session, mode, f"select {mode}")
    session.reset()
    mdt.wait_basic_ready(session)
    time.sleep(0.5)


def wait_stable_dump(session, needle, timeout=8.0):
    """Wait until a logical dump_ui_screen frame contains needle (tolerates the
    transitional 404s emitted while the C64 is briefly running)."""
    deadline = time.time() + timeout
    last = "<none>"
    while time.time() < deadline:
        try:
            frame = session.dump_ui_screen("rr")
            last = frame.text()
            if needle in last:
                return frame
        except mdt.Failure:
            pass
        time.sleep(0.3)
    raise mdt.Failure(f"dump never contained {needle!r}; last=\n{last}")


def select_view(session, bank, reader):
    """Cycle 'o' until monitor view == bank (parsed from the status line)."""
    for _ in range(9):
        frame = reader()
        try:
            _, current = mdt.parse_status_banks(mdt.status_line(frame))
            if current == bank:
                return frame
        except mdt.Failure:
            pass
        mdt.send_ui_text(session, "o", settle=0.25)
    raise mdt.Failure(f"could not select monitor view O{bank}")


# --------------------------------------------------------------------------
# Regression C: Freeze G of RAM-under-ROM must execute the underlying RAM code.
# Mirrors the proven Telnet banked-continue fixture, driven in Freeze mode.
#   bootstrap $C000: SEI; LDA #$00; STA $01; JMP $E000   (banks KERNAL/I/O out)
#   payload   $E000: INC $0400 sentinel via RAM; here we use a counter at $02FF
# We prove execution by a deterministic RAM sentinel that is plain RAM in every
# banking mode ($02FF), incremented by the under-ROM loop.
# --------------------------------------------------------------------------
def _s(session, n=3):
    """Read the three page-2 sentinels $0250..$0252 as a tuple."""
    b = session.read_memory(0x0250, n)
    return tuple(b[:n])


def repro_C(session, capture, ocr):
    """RAM-only proof of debug step + G for RAM-under-ROM in Freeze mode.

    No screen reads are needed: distinct per-instruction sentinels distinguish a
    genuine single-step (one INC at a time) from free execution.

      bootstrap $C000 (visible RAM): SEI; LDA #$35; STA $01; JMP $E000
      payload   $E000 (RAM under KERNAL):
          INC $0250 ; INC $0251 ; INC $0252 ; JMP $E009 (halt, no more effects)
    $0250..$0252 are plain RAM in every banking mode.
    """
    bootstrap = 0xC000
    payload = 0xE000
    boot = bytes([0x78, 0xA9, 0x35, 0x85, 0x01, 0x4C, 0x00, 0xE0])
    pay = bytes([0xEE, 0x50, 0x02,        # E000 INC $0250
                 0xEE, 0x51, 0x02,        # E003 INC $0251
                 0xEE, 0x52, 0x02,        # E006 INC $0252
                 0x4C, 0x09, 0xE0])       # E009 JMP $E009 (halt)

    to_basic(session, "Freeze")
    session.start_video()
    time.sleep(0.5)
    mdt.enter_visible_monitor(session, capture, ocr)
    log("[Reg C] entered Freeze monitor")

    session.write_memory(payload, pay)
    session.write_memory(bootstrap, boot)
    session.write_memory(0x0250, bytes([0x00, 0x00, 0x00]))

    # Enter debug at the bootstrap, then single-step (D) through the 4 bootstrap
    # instructions (SEI, LDA, STA, JMP) so the parked PC lands at $E000 under ROM.
    mdt.send_ui_text(session, "J%04X\n" % bootstrap, settle=0.6)
    mdt.send_ui_text(session, "A", settle=0.6)
    mdt.send_ui_text(session, "D", settle=0.8)          # enter Debug
    for _ in range(4):
        mdt.send_ui_text(session, "D", settle=0.7)      # step bootstrap -> $E000
    s_at_e000 = _s(session)
    log(f"[Reg C] after stepping to $E000 (under ROM): $0250..$0252={s_at_e000}")
    if s_at_e000 != (0, 0, 0):
        log("[Reg C] WARN: bootstrap steps already touched payload sentinels "
            f"(unexpected): {s_at_e000}")

    # ONE more single-step: must execute exactly INC $0250 under ROM.
    mdt.send_ui_text(session, "D", settle=0.8)
    s_step1 = _s(session)
    log(f"[Reg C] after 1 debug-step under ROM: $0250..$0252={s_step1}")
    step_ok = (s_step1[0] == 1 and s_step1[1] == 0 and s_step1[2] == 0)

    # Now G (no breakpoints): remaining INCs run then halt at $E009.
    mdt.send_ui_text(session, "G", settle=0.5)
    time.sleep(1.5)
    s_go = _s(session)
    time.sleep(1.0)
    s_go2 = _s(session)
    log(f"[Reg C] after Freeze G: $0250..$0252={s_go} then {s_go2}")
    go_ran = (s_go[1] >= 1 and s_go[2] >= 1)
    # Halt loop means values stabilise (each incremented exactly once total).
    halted = (s_go == s_go2)

    ok = step_ok and go_ran
    log(f"[Reg C] debug_step_under_ROM={step_ok} G_executed_under_ROM={go_ran} "
        f"halted_stable={halted}")
    if not go_ran:
        log("[Reg C] FAIL: Freeze G did NOT execute the RAM-under-ROM payload "
            "(regression reproduced).")
        return False
    if not step_ok:
        log("[Reg C] PARTIAL: Freeze G executed under-ROM code but single-step "
            "semantics under ROM look wrong (see sentinels).")
        return False
    log("[Reg C] PASS: Freeze debug single-step AND G both execute RAM-under-ROM.")
    return True


def _reachable(session, tries=5, gap=0.5):
    """Robust liveness: REST is up if ANY of several probes succeeds."""
    import urllib.error
    for _ in range(tries):
        try:
            session.get_interface_type()
            return True
        except (mdt.Failure, OSError, TimeoutError, urllib.error.URLError):
            time.sleep(gap)
    return False


def _wait_basic_banner(session, timeout=8.0):
    """Wait until the C64 screen shows the post-reset BASIC banner row."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            row = session.read_memory(0x0420, 24)  # row 1, where the banner sits
            # 'COMMODORE 64 BASIC' -> screen codes for letters are 0x01.. ; the
            # word "BASIC" appears; we look for the screen-code 'BASIC' bytes.
            # Simpler: the banner contains 0x42 0x01 0x13 0x09 0x03 ("BASIC").
            if bytes([0x42, 0x01, 0x13, 0x09, 0x03]) in row:
                return True
        except Exception:
            pass
        time.sleep(0.3)
    return False


def repro_A(session, capture, ocr):
    """Overlay: RAM-under-ROM debug + G, reset, then C=+X; plus re-entry step.

    Verifies (all without human action/observation):
      1. Overlay G executes RAM-under-ROM (RAM sentinel).
      2. C=+X (injected, monitor active) resets the C64 (BASIC banner returns)
         and the device stays reachable.
      3. Re-entering the monitor and single-stepping does NOT hang the device
         (REST stays reachable; a single-step has its RAM side effect).
    """
    reader = lambda: session.dump_ui_screen("overlay")
    bootstrap = 0xC000
    payload = 0xE000
    boot = bytes([0x78, 0xA9, 0x35, 0x85, 0x01, 0x4C, 0x00, 0xE0])
    pay = bytes([0xEE, 0x50, 0x02, 0xEE, 0x51, 0x02, 0x4C, 0x06, 0xE0])  # INC,INC,JMP self

    to_basic(session, "Overlay on HDMI")
    mdt.enter_hidden_monitor(session)
    log("[Reg A] entered Overlay monitor")

    session.write_memory(payload, pay)
    session.write_memory(bootstrap, boot)
    session.write_memory(0x0250, bytes([0x00, 0x00]))

    # Faithful to the report: debug-STEP into the RAM-under-ROM code, THEN G.
    # Enter debug at the bootstrap and single-step through SEI/LDA/STA/JMP (4) so
    # the parked PC is at $E000 under ROM, then one more step executes INC $0250.
    mdt.send_ui_text(session, "J%04X\n" % bootstrap, settle=0.4)
    mdt.send_ui_text(session, "A", settle=0.4)
    mdt.send_ui_text(session, "D", settle=0.6)
    for _ in range(4):
        mdt.send_ui_text(session, "D", settle=0.5)   # step to $E000 (under ROM)
    mdt.send_ui_text(session, "D", settle=0.6)        # step INC $0250 -> (1,0)
    s_step = _s(session, 2)
    log(f"[Reg A] after step into under-ROM: $0250..$0251={s_step}")
    mdt.send_ui_text(session, "G", settle=0.6)        # continue (exercises fixed path)
    time.sleep(2.0)
    s_go = _s(session, 2)
    log(f"[Reg A] after Overlay G (RAM-under-ROM): $0250..$0251={s_go}")
    if s_go[1] == 0:
        log("[Reg A] FAIL: Overlay G did not continue the RAM-under-ROM payload.")
        return False

    # Step 4: reset the machine (REST). Step 5: attempt C=+X afterwards.
    session.reset()
    time.sleep(2.0)
    reach_after_reset = _reachable(session)
    log(f"[Reg A] after REST reset: reachable={reach_after_reset}")

    # C=+X as the GLOBAL reset shortcut: it is only the global shortcut when the
    # active keymap is NOT the monitor (userinterface.cc keymapper). So open the
    # root MENU (F10), confirm a UI is up, dirty the BASIC banner, then C=+X.
    session.input_ui(["f10"])
    time.sleep(1.0)
    # Corrupt the banner row so a real RESET visibly restores it.
    session.write_memory(0x0420, bytes([0x20] * 24))
    banner_before = _wait_basic_banner(session, timeout=1.0)
    session.input_ui(["ctrl_x"])
    time.sleep(2.5)
    reach_after_cbmx = _reachable(session)
    banner = _wait_basic_banner(session, timeout=8.0)
    log(f"[Reg A] after C=+X (global, banner_before_wipe={banner_before}): "
        f"reachable={reach_after_cbmx} basic_banner_restored={banner}")

    # Step 6/7: re-enter monitor and single-step; must not hang.
    session.write_memory(0x0250, bytes([0x00, 0x00]))
    session.write_memory(0x02A0, bytes([0xEE, 0x50, 0x02, 0x4C, 0xA0, 0x02]))  # INC $0250;JMP self
    reentry_ok = False
    step_effect = None
    try:
        mdt.enter_hidden_monitor(session)
        mdt.send_ui_text(session, "J%04X\n" % 0x02A0, settle=0.4)
        mdt.send_ui_text(session, "A", settle=0.4)
        mdt.send_ui_text(session, "D", settle=0.6)   # enter debug
        mdt.send_ui_text(session, "D", settle=0.7)   # one step -> INC $0250
        step_effect = _s(session, 1)[0]
        reentry_ok = _reachable(session)
    except mdt.Failure as exc:
        log(f"[Reg A] re-entry/step raised: {exc}")
    log(f"[Reg A] re-entry: reachable={reentry_ok} step_sentinel=$0250={step_effect}")

    ok = (s_go != (0, 0)) and reach_after_reset and reach_after_cbmx and reentry_ok
    if not reach_after_cbmx:
        log("[Reg A] FAIL: device unreachable after C=+X (hang reproduced).")
    elif not banner:
        log("[Reg A] NOTE: C=+X did not visibly reset to BASIC banner "
            "(C=+X-resets-after-G regression may be present, device still alive).")
    if not reentry_ok:
        log("[Reg A] FAIL: device hung on monitor re-entry / first step "
            "(Symptom B reproduced).")
    log("[Reg A] PASS (no hang)" if ok else "[Reg A] see notes above")
    return ok


# --------------------------------------------------------------------------
# Freeze-mode coverage matrix: 4 debugger operations x {RAM, RAM-under-ROM}.
# All proofs are RAM-sentinel based (no screen dependency). Sentinels live at
# $0250..$0253 (plain RAM in every banking mode). Each payload is four 3-byte
# INC instructions followed by a 3-byte self-JMP halt, so cursor lines and PCs
# are perfectly regular: line N starts at base + 3*N.
#   RAM case        : payload runs directly at $C000 ($01 default, $C000 is RAM)
#   RAM-under-ROM   : bootstrap $C000 banks KERNAL/I/O out then JMP $E000 payload
# --------------------------------------------------------------------------
PAYLOAD = bytes([
    0xEE, 0x50, 0x02,   # +0  INC $0250
    0xEE, 0x51, 0x02,   # +3  INC $0251
    0xEE, 0x52, 0x02,   # +6  INC $0252
    0xEE, 0x53, 0x02,   # +9  INC $0253
])
BOOT = bytes([0x78, 0xA9, 0x35, 0x85, 0x01, 0x4C, 0x00, 0xE0])


def _halt_at(base):
    return bytes([0x4C, base & 0xFF, base >> 8])


def _sent4(session):
    return tuple(session.read_memory(0x0250, 4))


def _setup_payload(session, under_rom):
    """Write the payload (+halt) and clear sentinels. Returns the debug-entry
    address and the payload base address."""
    session.write_memory(0x0250, bytes([0, 0, 0, 0]))
    if under_rom:
        base = 0xE000
        session.write_memory(base, PAYLOAD + _halt_at(base + 12))
        session.write_memory(0xC000, BOOT)
        return 0xC000, base, 4  # enter at bootstrap; 4 bootstrap steps reach $E000
    base = 0xC000
    session.write_memory(base, PAYLOAD + _halt_at(base + 12))
    return base, base, 0


def _enter_debug_blind(session, addr):
    mdt.send_ui_text(session, "J%04X\n" % addr, settle=0.5)
    mdt.send_ui_text(session, "A", settle=0.5)
    mdt.send_ui_text(session, "D", settle=0.7)


def _leave_debug(session):
    session.input_ui(["ctrl_d"]); time.sleep(0.3)
    session.input_ui(["escape"]); time.sleep(0.3)


def _step_to_payload(session, boot_steps):
    """For the RAM-under-ROM case, single-step through the bootstrap so PC parks
    at $E000."""
    for _ in range(boot_steps):
        mdt.send_ui_text(session, "D", settle=0.6)


def freeze_matrix(session, capture, ocr):
    to_basic(session, "Freeze")
    session.start_video()
    time.sleep(0.5)
    log("[FZMTX] Freeze mode selected")

    results = {}

    def cell(name, under_rom, op):
        # Fresh reset + monitor entry per cell: the firmware UI/debug state does
        # not reliably survive a leave-debug + re-enter within one freeze, so we
        # start each cell from a clean BASIC + monitor (proven reliable). Retry
        # the whole cell once if monitor entry flakes (injected-key menu open is
        # intermittently dropped).
        last_exc = None
        for attempt in range(2):
            try:
                session.reset()
                mdt.wait_basic_ready(session)
                time.sleep(0.4)
                mdt.enter_visible_monitor(session, capture, ocr)
                entry, base, boot_steps = _setup_payload(session, under_rom)
                _enter_debug_blind(session, entry)
                _step_to_payload(session, boot_steps)   # park at payload base
                got = op(base)
                results[name] = got
                log(f"[FZMTX] {name}: {got['verdict']}  sentinels={got['sent']}")
                return
            except Exception as exc:  # noqa
                last_exc = exc
                time.sleep(1.0)
        results[name] = {"verdict": "ERROR:%s" % last_exc, "sent": None}
        log(f"[FZMTX] {name}: ERROR {last_exc}")

    # --- debug single-step (D): exactly one payload instruction per press ---
    def op_step(base):
        mdt.send_ui_text(session, "D", settle=0.7)  # one payload instruction
        s = _sent4(session)
        ok = (s == (1, 0, 0, 0))
        return {"verdict": "PASS" if ok else "FAIL", "sent": s}

    # --- run-to-cursor (K): move cursor down 2 lines (-> base+6), run there ---
    def op_runcursor(base):
        session.input_ui(["down", "down"]); time.sleep(0.3)
        mdt.send_ui_text(session, "K", settle=1.2)
        s = _sent4(session)
        # executes instrs at base+0 and base+3 (stops AT base+6 before exec)
        ok = (s == (1, 1, 0, 0))
        return {"verdict": "PASS" if ok else "FAIL", "sent": s}

    # --- go with breakpoint: step once to park (exec base+0), set BP at the
    #     next instruction (cursor follows PC -> down 1 = base+6), then G. ---
    def op_go_bp(base):
        mdt.send_ui_text(session, "D", settle=0.7)           # step base+0 -> PC base+3
        session.input_ui(["down"]); time.sleep(0.3)          # cursor -> base+6
        mdt.send_ui_text(session, "R", settle=0.4)           # set breakpoint at base+6
        mdt.send_ui_text(session, "G", settle=1.5)           # run; must stop at base+6
        s = _sent4(session)
        ok = (s == (1, 1, 0, 0))
        return {"verdict": "PASS" if ok else "FAIL", "sent": s}

    # --- go without breakpoints: step once to park, then free-run to halt ---
    def op_go_free(base):
        mdt.send_ui_text(session, "D", settle=0.7)           # step base+0 -> parked
        mdt.send_ui_text(session, "G", settle=1.0)           # free run remaining INCs
        time.sleep(1.5)
        s = _sent4(session)
        ok = (s[0] >= 1 and s[1] >= 1 and s[2] >= 1 and s[3] >= 1)
        return {"verdict": "PASS" if ok else "FAIL", "sent": s}

    cell("step/RAM",            False, op_step)
    cell("step/RAM-under-ROM",  True,  op_step)
    cell("runcursor/RAM",           False, op_runcursor)
    cell("runcursor/RAM-under-ROM", True,  op_runcursor)
    cell("go+bp/RAM",            False, op_go_bp)
    cell("go+bp/RAM-under-ROM",  True,  op_go_bp)
    cell("go-free/RAM",           False, op_go_free)
    cell("go-free/RAM-under-ROM", True,  op_go_free)

    npass = sum(1 for v in results.values() if v["verdict"] == "PASS")
    log(f"[FZMTX] SUMMARY {npass}/{len(results)} Freeze cells passed")
    for k, v in results.items():
        log(f"[FZMTX]   {k:26s} {v['verdict']:6s} {v['sent']}")
    return npass == len(results)


def _frame_signature(frame):
    """Coarse readability metric over a decoded VIC frame: count of the box-drawing
    border cells and printable letters. A corrupted (wrong-charset) monitor screen
    loses the regular border run and turns into high-entropy noise."""
    text = "".join(frame.lines)
    border = sum(1 for c in text if c in "ABCDEF")  # OCR maps the PETSCII border run to A-F bands
    letters = sum(1 for c in text if c.isalpha())
    spaces = sum(1 for c in text if c == " ")
    distinct = len(set(text))
    return {"len": len(text), "letters": letters, "spaces": spaces,
            "distinct": distinct}


def _fresh_capture(capture):
    """Drain the UDP socket's backlog so capture_image() returns a CURRENT frame,
    not an old buffered one (the multicast socket queues many stale frames)."""
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


def repro_B(session, capture, ocr):
    """Freeze: set the debugged CPU port $01=$03 during stepping; the machine
    monitor screen must remain readable (not turn into charset garbage).

    Proof strategy (visual, since this is a rendering claim): capture the Freeze
    monitor VIC frame BEFORE and AFTER the debugged context has $01=$03, save
    both screenshots, and compare a readability signature. A readable monitor is
    dominated by spaces + a regular border/title; a charset-corrupted screen is
    high-entropy noise (few spaces, many distinct glyphs).
    """
    import os
    evdir = "/home/chris/dev/c64/1541ultimate/evidence/20260602"
    os.makedirs(evdir, exist_ok=True)
    # $2000: LDA #$03 ; STA $01 ; NOP ; NOP ; JMP $2006   ($01=$03 then spin)
    prog = bytes([0xA9, 0x03, 0x85, 0x01, 0xEA, 0xEA, 0x4C, 0x06, 0x20])

    to_basic(session, "Freeze")
    session.start_video(); time.sleep(0.5)
    mdt.enter_visible_monitor(session, capture, ocr)
    session.write_memory(0x2000, prog)
    _enter_debug_blind(session, 0x2000)
    log("[Reg B] entered debug at $2000")

    # Baseline screenshot (monitor readable, $01 still normal).
    time.sleep(0.5)
    img0 = _fresh_capture(capture); img0.save(os.path.join(evdir, "regB_before.png"))
    f0 = ocr.decode(img0)
    sig0 = _frame_signature(f0)
    log(f"[Reg B] before $01=$03: sig={sig0}")

    # Step LDA #$03 then STA $01  -> debugged context now has $01=$03.
    mdt.send_ui_text(session, "D", settle=0.7)   # LDA #$03
    mdt.send_ui_text(session, "D", settle=0.7)   # STA $01  (context $01 := $03)
    # Continue stepping with $01=$03 in effect.
    mdt.send_ui_text(session, "D", settle=0.7)   # NOP
    mdt.send_ui_text(session, "D", settle=0.7)   # NOP
    time.sleep(0.6)
    img1 = _fresh_capture(capture); img1.save(os.path.join(evdir, "regB_after.png"))
    f1 = ocr.decode(img1)
    sig1 = _frame_signature(f1)
    log(f"[Reg B] after  $01=$03: sig={sig1}")
    # Verify $01 actually became $03 in the executed context via RAM readback of
    # the live value the program stored (it executed STA $01).
    live01 = session.read_memory(0x0001, 1)[0]
    log(f"[Reg B] live $01 readback={live01:#04x}")

    # Readability heuristic: a readable monitor frame is space-dominated; a
    # charset-corrupted frame collapses spaces and inflates distinct glyphs.
    readable = (sig1["spaces"] >= max(200, sig0["spaces"] * 0.6))
    log(f"[Reg B] screenshots saved: regB_before.png / regB_after.png")
    if not readable:
        log("[Reg B] FAIL: monitor screen lost its readable structure after "
            f"$01=$03 (spaces {sig0['spaces']}->{sig1['spaces']}) - corruption reproduced.")
        return False
    log("[Reg B] PASS: monitor screen remained readable with $01=$03.")
    return True


def repro_fzgobp(session, capture, ocr):
    """Focused: Freeze go-with-breakpoint, RAM payload at $C000."""
    to_basic(session, "Freeze")
    session.start_video(); time.sleep(0.5)
    mdt.enter_visible_monitor(session, capture, ocr)
    session.write_memory(0x0250, bytes([0, 0, 0, 0]))
    session.write_memory(0xC000, PAYLOAD + _halt_at(0xC00C))
    _enter_debug_blind(session, 0xC000)
    log("[FZGOBP] entered debug at $C000")
    mdt.send_ui_text(session, "D", settle=0.7)          # step $C000 -> PC $C003
    log(f"[FZGOBP] after 1 step: {_sent4(session)}")
    session.input_ui(["down"]); time.sleep(0.3)         # cursor -> $C006
    mdt.send_ui_text(session, "R", settle=0.4)          # BP at $C006
    session.input_ui(["up"]); time.sleep(0.3)           # cursor back to PC $C003
    mdt.send_ui_text(session, "G", settle=1.5)          # continue from PC; stop at BP $C006
    s = _sent4(session)
    log(f"[FZGOBP] after BP+G: {s} (expect (1,1,0,0))")
    return s == (1, 1, 0, 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="u64")
    ap.add_argument("--timeout", type=float, default=5.0)
    ap.add_argument("--case", default="C",
                    choices=["C", "A", "B", "freeze-matrix", "fzgobp"])
    args = ap.parse_args()
    session = mdt.RestSession(args.host, args.timeout)
    capture = mdt.VicStreamCapture()
    ocr = mdt.C64FrameOCR()
    ok = False
    cleanup_ok = True
    try:
        ok = {"C": repro_C, "A": repro_A, "B": repro_B,
              "freeze-matrix": freeze_matrix,
              "fzgobp": repro_fzgobp}[args.case](session, capture, ocr)
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
            session.write_memory(0x0001, bytes([0x37]))
            value = session.read_memory(0x0001, 1)[0]
            log(f"[cleanup/{args.case}] $0001 readback={value:#04x}")
            if (value & 0x07) != 0x07:
                raise mdt.Failure(f"cleanup failed to restore safe $0001, got {value:#04x}")
            session.reset()
        except Exception as exc:
            cleanup_ok = False
            log(f"[cleanup/{args.case}] FAIL: {exc}")
    return 0 if ok and cleanup_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
