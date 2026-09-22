#!/usr/bin/env python3
# E2E: Verifies tape playback outlives the menu and that the tape actions follow the tape.

"""Drive a .TAP through the on-device menu and check what the Tape task menu offers.

The Tape category holds the recorder's own actions at all times, and the
player's three actions only while a tape is loaded: Pause while it plays,
Resume and Stop while it is paused. Those three are therefore a direct read of
whether the firmware still has the tape file open, which is what this suite
uses as its observable.

The fixture is a TAP of nothing but maximum-length pulses. It carries no
Commodore tape file, so the C64 cannot load anything from it, which keeps the
test off the KERNAL's tape routines: the pulses are clocked out by the FPGA
streamer (fpga/io/c2n_playback/vhdl_source/c2n_playback_io.vhd), so the tape's
running time is its pulse count divided by the configured tick rate, and the
firmware closes the file once the last pulse has been played.

That clock runs on phi2, and the on-device menu freezes the C64 while it is up
(C64::take_ownership in software/io/c64/c64.h), so a tape only advances while
the menu is closed. The last check times the tape by how long the menu was
closed, not by wall time. Measured on an Ultimate 64 Elite: a 10,000-pulse tape
closed itself 23.3s after Start Tape with the menu shut the whole time, and had
not finished after 41s of a menu left open.

Three things are checked:

  a tape survives the menu   Start Tape, then close and open the on-device
                             menu three times. Pause has to be offered after
                             every one of them.
  pause and resume work      Pause, then Resume, reading the offered actions
                             after each.
  the tape plays out         A short tape reaches its own end without anyone
                             touching it, and the player's actions go away
                             when it does, no earlier than the pulse count
                             says it can.
"""

import argparse
import struct
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import ftp as ftp_lib
import targets
from report import (Failure, check, check_skip, detail, format_exception, section,
                    suite_fail, suite_ok, suite_skip, teardown_step)
from ui_backend import MODE_OVERLAY, close_host_menu, make_browser

SUITE = "tape_playback_test"

TEMP_DIRECTORY = "Temp"
TEMP_PATH = "/Temp"
LONG_TAP_NAME = "tapeplay-long.tap"
SHORT_TAP_NAME = "tapeplay-short.tap"

# A v1 TAP byte is a pulse of eight times its value in cycles, so 0xFF is the
# longest pulse one byte can carry and the fewest bytes per second of tape.
PULSE_BYTE = 0xFF
PULSE_CYCLES = PULSE_BYTE * 8
# software/io/tape/tape_controller.cc offers both rates as "Tape Playback
# Rate"; the suite does not set it, so a tape's running time is bounded by
# the two.
TICK_HZ_PAL = 985248
TICK_HZ_NTSC = 1022727

# 120 seconds of tape, which outlasts the menu work done while it plays.
LONG_TAP_PULSES = 60000
# 20 seconds, which the last check waits out in full.
SHORT_TAP_PULSES = 10000

TAPE_CATEGORY = "Tape"
PAUSE_ACTION = "Pause Tape Playback"
RESUME_ACTION = "Resume Tape Playback"
STOP_ACTION = "Stop Tape Playback"
PLAYER_ACTIONS = (PAUSE_ACTION, RESUME_ACTION, STOP_ACTION)
START_ACTION = "Start Tape"

MENU_CYCLES = 3
# Start Tape unfreezes the C64 and closes the menu on its way out, so the
# screen is gone for a moment; this is the allowance for it.
START_SETTLE_SECONDS = 1.5
# How long the last check lets the tape play, with the menu closed, between
# two readings of the menu. Reading it costs a task-menu round trip and stops
# the tape for the duration, so the poll is not tight.
END_PLAY_SECONDS = 5.0
# What the tape can take on top of its nominal running time: the 2.5s of
# silence the firmware writes ahead of the file (TapeController::start), the
# FIFO, the real tick rate, and up to one poll window of playing that has
# already happened when the menu is read.
END_SLACK_SECONDS = 15.0
# A tape that ends earlier than this fraction of its nominal time did not play
# to its end, it was stopped.
END_EARLY_FRACTION = 0.8


def tap_image(pulses: int) -> bytes:
    """A TAP v1 file of `pulses` maximum-length pulses."""
    body = bytes([PULSE_BYTE]) * pulses
    return b"C64-TAPE-RAW" + bytes([1, 0, 0, 0]) + struct.pack("<I", len(body)) + body


def tap_seconds(pulses: int) -> tuple[float, float]:
    """The running time of such a tape at the faster and the slower tick rate."""
    cycles = pulses * PULSE_CYCLES
    return cycles / TICK_HZ_NTSC, cycles / TICK_HZ_PAL


def upload(host: str, password: str | None, name: str, payload: bytes) -> None:
    with ftp_lib.connect(host, password) as client:
        ftp_lib.store(client, f"{TEMP_PATH}/{name}", payload)


def remove(host: str, password: str | None, name: str) -> None:
    with ftp_lib.connect(host, password) as client:
        ftp_lib.delete_quietly(client, f"{TEMP_PATH}/{name}")


def dismiss_overlays(browser, clean: list[str]) -> None:
    """Close whatever the reading of a menu left open, back to `clean`."""
    for _ in range(4):
        if not browser.overlay_items(clean):
            return
        browser.press("RUNSTOP")
    raise Failure(f"an overlay would not close; screen was:\n{browser.screen()}")


def ready(browser) -> None:
    """Menu up, browser standing in /Temp.

    The Tape category carries the recorder's two actions as well, and those
    are disabled where the browser stands on a path it cannot write, which
    takes the whole category out of the task menu. Reading it from one
    directory keeps what it offers a statement about the tape alone.
    """
    browser.backend.ensure_ready()
    if browser.current_path() != TEMP_PATH + "/":
        browser.go_to_directory(TEMP_DIRECTORY)


def tape_actions(browser) -> list[str]:
    """What the Tape category of the task menu offers, right now."""
    ready(browser)
    clean = browser.rows()
    categories = browser.open_task_menu()
    if not categories:
        raise Failure(f"no task menu appeared; screen was:\n{browser.screen()}")
    if TAPE_CATEGORY not in categories:
        dismiss_overlays(browser, clean)
        return []
    before = browser.rows()
    browser.choose_overlay_item(categories, TAPE_CATEGORY)
    items = [label.split("||", 1)[0].strip()
             for label in browser.wait_for_overlay(before)]
    dismiss_overlays(browser, clean)
    return items


def player_actions(browser) -> list[str]:
    return [item for item in tape_actions(browser) if item in PLAYER_ACTIONS]


def expect_actions(browser, expected: tuple[str, ...], context: str) -> None:
    offered = player_actions(browser)
    if sorted(offered) != sorted(expected):
        raise Failure(f"{context}: the Tape menu offers {offered}, expected "
                      f"{list(expected)}")


def start_tape(browser, name: str) -> float:
    """Start `name` playing, and answer how long it has been playing."""
    ready(browser)
    browser.select_entry(name)
    labels = browser.open_context_menu()
    try:
        browser.choose_overlay_item(labels, START_ACTION)
    except Failure as exc:
        # Start Tape sends C64_UNFREEZE, which closes the on-device menu, so
        # the screen read that follows the key finds nothing to read.
        if "menu screen unavailable" not in str(exc):
            raise
    started = time.monotonic()
    time.sleep(START_SETTLE_SECONDS)
    return time.monotonic() - started


def play_with_the_menu_closed(browser, seconds: float) -> float:
    """Let the tape run for `seconds`, and answer how long it really ran."""
    close_host_menu(browser.backend.machine_host, browser.backend.machine_password,
                    browser.backend.timeout)
    started = time.monotonic()
    time.sleep(seconds)
    return time.monotonic() - started


def stop_tape(browser) -> None:
    """Leave the machine with no tape loaded, from whatever state it is in."""
    offered = player_actions(browser)
    if PAUSE_ACTION in offered:
        browser.invoke_task_action(TAPE_CATEGORY, PAUSE_ACTION)
        offered = player_actions(browser)
    if STOP_ACTION in offered:
        browser.invoke_task_action(TAPE_CATEGORY, STOP_ACTION)


def check_menu_does_not_stop_the_tape(browser) -> None:
    start_tape(browser, LONG_TAP_NAME)
    expect_actions(browser, (PAUSE_ACTION,), "with the tape playing")
    for cycle in range(1, MENU_CYCLES + 1):
        browser.backend.reopen_menu_on_browser()
        expect_actions(browser, (PAUSE_ACTION,),
                       f"after closing and opening the menu {cycle} time(s)")
    detail(f"the tape was still loaded after {MENU_CYCLES} menu open/close cycles")


def check_pause_and_resume(browser) -> None:
    browser.invoke_task_action(TAPE_CATEGORY, PAUSE_ACTION)
    expect_actions(browser, (RESUME_ACTION, STOP_ACTION), "with the tape paused")
    browser.invoke_task_action(TAPE_CATEGORY, RESUME_ACTION)
    expect_actions(browser, (PAUSE_ACTION,), "with the tape resumed")


def check_tape_plays_to_its_end(browser) -> None:
    fast, slow = tap_seconds(SHORT_TAP_PULSES)
    played = start_tape(browser, SHORT_TAP_NAME)
    expect_actions(browser, (PAUSE_ACTION,), "with the short tape playing")
    deadline = slow + END_SLACK_SECONDS
    while True:
        played += play_with_the_menu_closed(browser, END_PLAY_SECONDS)
        offered = player_actions(browser)
        if not offered:
            break
        if played > deadline:
            raise Failure(f"{SHORT_TAP_NAME} plays for {fast:.1f}s to {slow:.1f}s, "
                          f"and the Tape menu still offered {offered} after "
                          f"{played:.1f}s of playing")
    if played < fast * END_EARLY_FRACTION:
        raise Failure(f"the tape stopped after {played:.1f}s of playing, and its "
                      f"{SHORT_TAP_PULSES} pulses take at least {fast:.1f}s to play")
    detail(f"the tape played for {played:.1f}s of its nominal "
           f"{fast:.1f}s to {slow:.1f}s and then closed itself")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check tape playback against the menu on real firmware.")
    cli.add_device_arguments(parser, timeout=5.0, colour=False)
    args = parser.parse_args()
    password = args.password or None

    browser = make_browser(MODE_OVERLAY, args.host, password, args.timeout)
    if not browser.backend.reopens_menu:
        suite_skip(SUITE, "this transport cannot close and open the on-device menu")
        return 0

    try:
        section("fixtures")
        with check("seed two TAP files in /Temp"):
            upload(args.host, password, LONG_TAP_NAME, tap_image(LONG_TAP_PULSES))
            upload(args.host, password, SHORT_TAP_NAME, tap_image(SHORT_TAP_PULSES))

        ready(browser)
        if not tape_actions(browser):
            suite_skip(SUITE, "this device offers no Tape menu, so it has no "
                              "tape hardware")
            return 0

        with check("no tape loaded, so no playback actions are offered"):
            stop_tape(browser)
            expect_actions(browser, (), "with no tape loaded")

        section("playback")
        with check("the tape stays loaded across menu open and close"):
            check_menu_does_not_stop_the_tape(browser)

        with check("pause and resume are offered in turn"):
            check_pause_and_resume(browser)

        with check("stopping the tape withdraws the playback actions"):
            stop_tape(browser)
            expect_actions(browser, (), "after Stop Tape Playback")

        with check("a tape plays to its end and then closes itself"):
            if targets.is_cartridge(args.host):
                # The tape clock runs only while the cassette motor is on
                # (tape_speed_control.vhd). A cartridge sees the computer's motor
                # line only through the tape adapter on the cassette port.
                check_skip("a cartridge's tape advances only with the tape adapter "
                           "on the computer's cassette port, which carries the motor line")
            else:
                check_tape_plays_to_its_end(browser)

        suite_ok(SUITE)
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        teardown_step("stop any tape left playing", lambda: stop_tape(browser))
        teardown_step("close the browser session", browser.close)
        teardown_step("remove the TAP fixtures",
                      lambda: [remove(args.host, password, LONG_TAP_NAME),
                               remove(args.host, password, SHORT_TAP_NAME)])


if __name__ == "__main__":
    raise SystemExit(main())
