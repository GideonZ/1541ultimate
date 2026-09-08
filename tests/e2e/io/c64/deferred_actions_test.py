#!/usr/bin/env python3
# E2E: a machine action taken from the menu runs after the UI has torn down.

"""Reset and Reboot taken from the menu leave the machine at a clean prompt.

Two behaviours belong to GideonZ/commodore#1 (commit c66ef028), and this
suite covers one of them.

Covered. A machine action taken from the menu is queued rather than run where
the menu took it. `C64_Subsys::request_hide_then_action` records the action
with `C64::defer_machine_action` and answers `MENU_HIDE`, so
`UserInterface::run_once` calls `release_host` and `release_ownership` and
only then `perform_deferred_machine_action`. The teardown therefore runs
against a machine that is still frozen, and the screen the C64 comes back to
holds the KERNAL's boot text and nothing of the menu's. Three routes reach
that path and each is checked: "Reset C64" and "Reboot C64" from the task
menu, and C= + R in the file browser, whose `KEY_CTRL_R` case in
tree_browser.cc issues `MENU_C64_RESET` and returns
`user_interface->menu_response_to_action`.

Not covered, and the reason is a harness limit rather than a choice. The
other behaviour is that the menu opens again after such an action:
`C64::checkButton` holds its edge detector in the member `button_prev` and
`C64::syncButtonState` resyncs it once `run_once` returns. Only two routes
reach that detector, and this harness can drive neither. `PUT
/v1/machine:menu_button` calls `setButtonPushed()` directly and never enters
`checkButton`. The other is the hardware button, or the C64 keyboard
combination that raises the same signal on a C64 Ultimate; that mapping is in
the prebuilt bitstream (external/u64e2_*.bit) and in no C source here, and an
injected combination does not reach it. Measured on c64u (C64 Ultimate,
firmware 1.2RC) with the Overlay interface selected: a press of `commodore`,
a tap of `restore` and a release of `commodore` left machine:menu_screen
answering 404 for the whole 8s it was polled, with the same result when the
three requests were spaced 0.5s apart and when `restore` was tapped alone,
while `PUT /v1/machine:menu_button` in the same probe opened the menu every
time. The same sequence does nothing on u64 either. So the three
menu-reopening checks report SKIP with that measurement in the reason, and
stay in the suite as the record that the behaviour needs a person.

The REST routes are the third part and are machine-independent. With no menu
open, `cmd->user_interface` is NULL, so `request_hide_then_action` takes its
else branch: release the host, queue, and perform immediately. `PUT
/v1/machine:reset` and `PUT /v1/machine:menu_button` must act on the machine
straight away, and those checks run everywhere.

Not covered either: starting a .crt from the menu, which the author's own
reproduction lists. It reaches the same `request_hide_then_action` call
(`C64_START_CART`), but it needs a cartridge image fixture on the device and
leaves the machine running foreign code that no assertion here could then
read a BASIC prompt from. It is left out rather than approximated.

"A clean BASIC prompt" is asserted from screen RAM at $0400 rather than from
the video stream: the KERNAL's banner and READY must be there, and no cell may
hold a PETSCII graphic. Screen codes 0x00 to 0x3F are the letters, digits and
punctuation a boot screen is made of; 0x40 to 0x7F are the graphics the menu
draws its borders and its highlights with. Bit 7 is reverse video, so the test
is on the low seven bits, which lets the cursor (a reverse space, 0xA0) pass
and still catches a reversed menu character.

Which of the two halves of that screen check can catch anything depends on
the Interface Type the run selected, and `--mode` sets it. Under `freeze` the
menu is drawn in C64 screen RAM, which is where a leftover would be; under
`overlay` it is drawn on its own plane and the menu never writes screen RAM
at all, so only the "the machine came back to BASIC" half is load bearing.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import cli  # noqa: E402
import pacing  # noqa: E402
from api import SCREEN_RAM, UltimateApi  # noqa: E402
from report import (Failure, check, check_skip, check_start, detail,  # noqa: E402
                    format_exception, section, suite_fail, suite_ok,
                    teardown_step)
from ui_backend import MODE_FREEZE, add_mode_argument, make_browser  # noqa: E402

SUITE = "deferred_actions_test"

# The whole 40x25 text screen. A leftover from the menu can be anywhere on it,
# so the check reads all of it rather than the first few lines the KERNAL
# writes.
SCREEN_COLUMNS = 40
SCREEN_ROWS = 25
SCREEN_BYTES = SCREEN_COLUMNS * SCREEN_ROWS
SPACE_CODE = 0x20

# The first screen code of the PETSCII graphics. Everything below it is a
# letter, a digit or punctuation; everything from it up to 0x7F is a shape the
# KERNAL's boot screen never prints and the menu draws with.
FIRST_GRAPHIC_CODE = 0x40

# Screen codes for the two strings the KERNAL's boot screen always carries.
# Written out as codes rather than as text because screen codes are not ASCII:
# 'A' is 0x01 on the screen and 0x41 in a Python string.
BANNER = bytes([0x02, 0x01, 0x13, 0x09, 0x03])          # "BASIC"
READY = bytes([0x12, 0x05, 0x01, 0x04, 0x19, 0x2E])     # "READY."

# The two task-menu entries this suite invokes. The category they sit under is
# the machine's (Machine.machine_task_category); the labels are not.
RESET_ACTION = "Reset C64"
REBOOT_ACTION = "Reboot C64"

# A reset reaches READY in well under a second on the machines here; a reboot
# restarts the cartridge first and is slower. Both are generous enough that a
# failure means the prompt did not arrive, not that the budget was tight.
RESET_TIMEOUT = 15.0
REBOOT_TIMEOUT = 40.0
# How long the menu is given to appear after the route that opens it. The
# firmware polls the button in its main loop, so this is not instant.
MENU_TIMEOUT = 8.0
MENU_CLOSE_TIMEOUT = 8.0
# How many menu buttons a step that only needs the menu up may send. See
# open_menu, and the check that measures the single press.
MENU_OPEN_PRESSES = 3

# The browser row layout, the same as every other suite that drives the menu.
ENTRY_ROWS = range(2, 24)
STATUS_ROW = 24
TELNET_ENTRY_ROWS = range(2, 23)
TELNET_STATUS_ROW = 23


# ------------------------------------------------------------------ screen --

def read_screen(api: UltimateApi) -> bytes:
    return api.machine.readmem(SCREEN_RAM, SCREEN_BYTES)


def blank_screen(api: UltimateApi) -> None:
    """Fill screen RAM with spaces, so what is read back is this boot's.

    Without it the previous boot's READY is still on the screen and a wait for
    the prompt returns at once, proving nothing. The menu's own characters
    would still be there too, which is exactly what the leftover check is
    looking for.

    This must only be called while the menu is closed. The freeze interface
    draws the menu into the same screen RAM, so blanking it there erases a
    menu the firmware still reports as open and does not redraw. The browser
    then reads a blank screen and cannot find its selected row. Blanking
    before the menu opens still gives each check what it needs, because the
    menu is drawn into the blanked screen afterwards and any characters it
    leaves behind are still there to be found.
    """
    api.machine.writemem(SCREEN_RAM, bytes([SPACE_CODE]) * SCREEN_BYTES)


def wait_for_prompt(api: UltimateApi, timeout: float) -> bytes:
    """Poll screen RAM until READY appears, and answer the whole screen."""
    deadline = time.monotonic() + timeout
    while True:
        screen = read_screen(api)
        if READY in screen:
            return screen
        if time.monotonic() >= deadline:
            raise Failure(
                f"BASIC did not print its READY prompt within {timeout:.0f}s; "
                f"the screen held:\n{describe(screen)}")
        time.sleep(pacing.POLL_INTERVAL_SECONDS)


def describe(screen: bytes) -> str:
    """The screen as rows of screen codes, for a message someone has to read."""
    rows = []
    for row in range(SCREEN_ROWS):
        codes = screen[row * SCREEN_COLUMNS:(row + 1) * SCREEN_COLUMNS]
        text = "".join(_readable(code) for code in codes)
        if text.strip():
            rows.append(f"    {row:02d}|{text}|")
    return "\n".join(rows) or "    (the screen is blank)"


def _readable(code: int) -> str:
    """One screen code as a character, with the graphics marked rather than named."""
    plain = code & 0x7F
    if 0x01 <= plain <= 0x1A:
        return chr(ord("A") + plain - 1)
    if 0x20 <= plain < FIRST_GRAPHIC_CODE:
        return chr(plain)
    if plain == 0x00:
        return "@"
    return "#"


def graphic_cells(screen: bytes) -> list[tuple[int, int, int]]:
    """(row, column, code) for every cell holding a PETSCII graphic."""
    found = []
    for index, code in enumerate(screen):
        if (code & 0x7F) >= FIRST_GRAPHIC_CODE:
            found.append((index // SCREEN_COLUMNS, index % SCREEN_COLUMNS, code))
    return found


def expect_clean_prompt(screen: bytes, what: str) -> None:
    """The screen is the KERNAL's boot screen and nothing else."""
    if BANNER not in screen:
        raise Failure(f"{what}: the KERNAL's banner is not on the screen, so "
                      f"the machine did not come back to BASIC. It held:\n"
                      f"{describe(screen)}")
    leftovers = graphic_cells(screen)
    if leftovers:
        where = ", ".join(f"row {row} column {column} code 0x{code:02X}"
                          for row, column, code in leftovers[:8])
        more = "" if len(leftovers) <= 8 else f", and {len(leftovers) - 8} more"
        raise Failure(
            f"{what}: {len(leftovers)} cell(s) hold a PETSCII graphic the "
            f"KERNAL's boot screen never prints, so the menu's characters were "
            f"left in screen RAM: {where}{more}. The screen held:\n"
            f"{describe(screen)}")


# ------------------------------------------------------------------- menu --

def open_menu(api: UltimateApi, presses: int = MENU_OPEN_PRESSES) -> str:
    """Open the on-device menu for a step that needs it open, and name the route.

    Always the REST menu button: it is the only route this harness has. See
    the module docstring for the measurement that rules out an injected
    C= + RESTORE, and for what that costs in coverage.

    More than one press is allowed here because a menu button issued straight
    after a close is dropped on some firmware, which
    scenario_rest_routes asserts directly and which c64u fails. This is the
    setup for a different assertion, so it retries rather than carrying that
    defect into every check that has to get the menu up first.
    """
    for attempt in range(presses):
        api.machine.menu_button()
        if wait_menu(api, True, MENU_TIMEOUT):
            if attempt:
                detail(f"the menu button had to be sent {attempt + 1} times")
            return "PUT /v1/machine:menu_button"
    return "PUT /v1/machine:menu_button"


def wait_menu(api: UltimateApi, want_open: bool, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while True:
        if api.machine.menu_open() == want_open:
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(pacing.POLL_INTERVAL_SECONDS)


def close_menu(api: UltimateApi) -> None:
    """Leave no menu open behind a check, whatever the check did."""
    if api.machine.menu_open():
        api.machine.menu_button()
        wait_menu(api, False, MENU_CLOSE_TIMEOUT)


# -------------------------------------------------------------- scenarios --

def scenario_rest_routes(api: UltimateApi) -> None:
    """The NULL-UI branch, which every machine takes and which must be unchanged."""
    section("the REST routes still act immediately with no menu open")

    with check("machine:reset with no menu open reaches a clean BASIC prompt"):
        close_menu(api)
        blank_screen(api)
        api.machine.reset(force=True, wait=False)
        screen = wait_for_prompt(api, RESET_TIMEOUT)
        expect_clean_prompt(screen, "after PUT /v1/machine:reset")

    with check("machine:menu_button with no menu open opens the menu"):
        if api.machine.menu_open():
            raise Failure("a menu was already open, so this proves nothing")
        api.machine.menu_button()
        if not wait_menu(api, True, MENU_TIMEOUT):
            raise Failure(
                f"the menu did not open within {MENU_TIMEOUT:.0f}s of "
                f"PUT /v1/machine:menu_button")

    with check("the same call closes it again"):
        api.machine.menu_button()
        if not wait_menu(api, False, MENU_CLOSE_TIMEOUT):
            raise Failure(
                f"the menu did not close within {MENU_CLOSE_TIMEOUT:.0f}s of "
                f"a second PUT /v1/machine:menu_button")



# How long the browser listing is given to stop changing, and how often it is
# read while waiting. See settle_listing.
LISTING_SETTLE_READS = 12
LISTING_SETTLE_INTERVAL = 0.15


def settle_listing(browser) -> None:
    """Wait for the browser listing to stop changing before an overlay is opened.

    Browser.overlay_items reads an overlay by diffing the screen against the
    one captured before the key that opened it, so a row that changed for its
    own reasons in between is taken for part of the overlay and the parse
    comes back empty. A C64 Ultimate refreshes the drive status in its listing
    without being asked, which is exactly such a row.

    Measured on c64u (C64 Ultimate, firmware 1.2RC), overlay mode: opening the
    task menu straight after go_to_root() parsed its categories in 3 of 5
    attempts, and reading until two consecutive screens matched first parsed
    them in 5 of 5. Browser.open_task_menu already presses the key twice
    before giving up, and the second press closes a menu the first one opened,
    so the retry cannot rescue this on its own.
    """
    last = browser.rows()
    for _ in range(LISTING_SETTLE_READS):
        time.sleep(LISTING_SETTLE_INTERVAL)
        current = browser.rows()
        if current == last:
            return
        last = current


def scenario_menu_button_reopen(api: UltimateApi) -> None:
    """One menu button, sent with nothing between it and the close before it.

    Last in the suite deliberately. This measures a defect that a machine can
    have on its own, and a failure here would otherwise stop the run before
    the deferral checks, which are what the suite is mainly for.
    """
    section("a single menu button straight after a close")
    with check("one menu button reopens the menu straight after a close"):
        # The menu has to be up first, and getting it there is setup, so it
        # may take more than one press.
        open_menu(api)
        if not api.machine.menu_open():
            raise Failure("the menu would not open, so the reopen cannot be "
                          "measured")
        # From here the sequence is the one under test and nothing is added to
        # it: close, wait only until the close is visible, then one press.
        # Measured on c64u (C64 Ultimate, firmware 1.2RC), freeze mode: that
        # press was dropped in 1 trial of 6 with no gap after the close, and
        # opened the menu in 6 trials of 6 once a 0.25s gap was left.
        api.machine.menu_button()
        if not wait_menu(api, False, MENU_CLOSE_TIMEOUT):
            raise Failure("the menu would not close, so the reopen cannot be "
                          "measured")
        api.machine.menu_button()
        if not wait_menu(api, True, MENU_TIMEOUT):
            raise Failure(
                f"one PUT /v1/machine:menu_button sent straight after the "
                f"close did not open the menu within {MENU_TIMEOUT:.0f}s, so "
                f"the press was dropped")
        close_menu(api)


def take_menu_action(browser, category: str, item: str) -> None:
    """Take a task-menu action whose whole effect is to close the menu.

    Browser.invoke_task_action settles by re-reading the menu screen, and
    machine:menu_screen answers 404 once the action has hidden the menu, so
    the last keystroke raises "menu screen unavailable after ENTER". Here that
    is the intended outcome, not a fault.

    Swallowing it cannot hide a menu that was never driven. The caller blanks
    screen RAM first and then waits for the BASIC prompt: had the category or
    the item not been reached, nothing would have reset the machine and the
    blanked screen would still be blank when the wait timed out.
    """
    settle_listing(browser)
    try:
        browser.invoke_task_action(category, item)
    except Failure as exc:
        if not str(exc).startswith("menu screen unavailable after"):
            raise


def run_menu_action(browser, api: UltimateApi, action: str,
                    timeout: float) -> tuple[bytes, str]:
    """Open the menu, take `action` from the task menu, and read the screen back."""
    # C= + RESTORE toggles, so opening it while it is already open would close
    # it instead. Nothing here should leave a menu behind, and this is the
    # cheap guarantee that it did not.
    close_menu(api)
    blank_screen(api)
    route = open_menu(api)
    if not api.machine.menu_open():
        raise Failure(f"the menu did not open after {MENU_OPEN_PRESSES} x "
                      f"{route}")
    browser.go_to_root()
    take_menu_action(browser, browser.backend.machine.machine_task_category,
                     action)
    return wait_for_prompt(api, timeout), route


def report_unreachable_reopen(label: str) -> None:
    """Record that the menu-reopening behaviour needs a person, and why.

    Reported as a skipped check rather than left out, so every run says the
    behaviour was not measured and names what stops it. Passing it on the one
    route the harness has would be worse than not running it: PUT
    /v1/machine:menu_button never enters C64::checkButton, so it would answer
    the same whatever the edge detector held.
    """
    check_start(label)
    check_skip(
        "the harness has no route to C64::checkButton's edge detector: PUT "
        "/v1/machine:menu_button calls setButtonPushed() directly, and an "
        "injected C= + RESTORE does not open the menu on a C64 Ultimate "
        "(measured on c64u 1.2RC; see the module docstring), so this needs a "
        "person at the machine")


def scenario_menu_actions(browser, api: UltimateApi) -> None:
    section("a machine action taken from the menu (GideonZ/commodore#1)")
    for action, timeout in ((RESET_ACTION, RESET_TIMEOUT),
                            (REBOOT_ACTION, REBOOT_TIMEOUT)):
        # Not tagged with a firmware fix. Measured on u64 (Ultimate 64
        # Elite, firmware 3.15, whose firmware carries none of c66ef028):
        # both of these pass there, so a vintage gate would report SKIP for
        # behaviour the machine satisfies and would leave the invariant
        # unguarded on the machine that holds it.
        with check(f"{action!r} from the menu reaches a clean BASIC prompt"):
            screen, route = run_menu_action(browser, api, action, timeout)
            detail(f"the menu was opened with {route}")
            expect_clean_prompt(screen, f"after {action!r} from the menu")
        report_unreachable_reopen(f"the menu opens again after {action!r}")


def scenario_browser_reset_shortcut(browser, api: UltimateApi) -> None:
    section("C= + R in the file browser takes the same deferred path")
    with check("C= + R in the browser reaches a clean BASIC prompt"):
        # tree_browser.cc's KEY_CTRL_R case issues MENU_C64_RESET and then
        # returns user_interface->menu_response_to_action, which
        # request_hide_then_action sets to MENU_HIDE, so the browser leaves
        # before the reset runs.
        close_menu(api)
        blank_screen(api)
        route = open_menu(api)
        if not api.machine.menu_open():
            raise Failure(f"the menu did not open after {MENU_OPEN_PRESSES} x "
                          f"{route}")
        browser.go_to_root()
        # C= + R resets the machine, which closes the menu, so the settle read
        # after the keystroke answers 404; see take_menu_action.
        try:
            browser.press("CBM_R")
        except Failure as exc:
            if not str(exc).startswith("menu screen unavailable after"):
                raise
        screen = wait_for_prompt(api, RESET_TIMEOUT)
        expect_clean_prompt(screen, "after C= + R in the browser")
    report_unreachable_reopen("the menu opens again after C= + R")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    cli.add_device_arguments(parser, timeout=10.0)
    parser.add_argument("--telnet-port", type=int,
                        default=int(os.environ.get("U64_TELNET_PORT", "23")))
    add_mode_argument(parser)
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    browser = make_browser(
        args.mode, args.host, args.password or None, args.timeout,
        entry_rows=ENTRY_ROWS, status_row=STATUS_ROW,
        telnet_port=args.telnet_port,
        telnet_entry_rows=TELNET_ENTRY_ROWS, telnet_status_row=TELNET_STATUS_ROW,
    )
    detail(f"{browser.backend.machine.described}, menu opened with PUT "
           f"/v1/machine:menu_button")
    if args.mode == MODE_FREEZE:
        detail("the freeze interface draws the menu in C64 screen RAM, so a "
               "leftover from it would be visible to the screen check")
    else:
        detail(f"the {args.mode} interface draws the menu on its own plane, "
               f"so the screen check can only show that the machine came "
               f"back to BASIC, not that no menu characters were left")
    try:
        scenario_rest_routes(api)
        scenario_menu_actions(browser, api)
        scenario_browser_reset_shortcut(browser, api)
        scenario_menu_button_reopen(api)
        suite_ok(SUITE)
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        teardown_step("close any menu this suite left open",
                      lambda: close_menu(api))
        teardown_step("close the browser session", browser.close)


if __name__ == "__main__":
    raise SystemExit(main())
