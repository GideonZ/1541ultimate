#!/usr/bin/env python3
# E2E helper: assert and restore the device's known-good menu UI state.

"""Known-good UI state for the menu, used by run-tests as a suite fixture.

The suites share one device and the firmware keeps its UI object stack, browser
location and cursor position across them, so a suite that navigates away hands
the next one a device it did not expect. Ordering around that only moves the
damage to whoever runs last.

The contract: the menu is closed, and opening it shows the root browser at "/"
with a non-empty listing. Cursor position is deliberately not part of it, because
it cannot be read back from the screen matrix and so could not be verified. A
suite that needs a known cursor sets it itself; repair() homes it as a
convenience only.

Exit codes: 0 clean, 1 could not be cleaned (ensure), 2 dirty (verify).
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from typing import List, Optional

# tests/lib holds the pacing every suite shares; this directory holds the
# window parser this gate borrows rather than writing a second one. Both are
# added here because this module is imported from elsewhere in the tree as
# well as run directly.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "lib"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import machine as machine_lib  # noqa: E402  (needs tests/lib on sys.path first)
import ui_backend  # noqa: E402  (needs this directory on sys.path first)
import pacing  # noqa: E402  (needs tests/lib on sys.path first)
import rest as rest_lib  # noqa: E402  (needs tests/lib on sys.path first)
import targets  # noqa: E402  (needs tests/lib on sys.path first)

SCREEN_BYTES = 2000
SCREEN_COLS = 40
SCREEN_ROWS = 25
PATH_ROW = SCREEN_ROWS - 1
ROOT_PATH = "/"
EMPTY_MARKER = "< No Items >"

# Pacing is shared with every suite; see tests/lib/pacing.py.
MENU_SETTLE_SECONDS = pacing.MENU_TOGGLE_SETTLE_SECONDS
KEY_SETTLE_SECONDS = pacing.KEY_SETTLE_SECONDS
MENU_TOGGLE_TIMEOUT_SECONDS = pacing.MENU_TOGGLE_TIMEOUT_SECONDS
# One press per nested object, plus room for directory levels.
UNWIND_PRESSES = 14
# Deeper than any listing the suites build.
HOME_PRESSES = 16
REPAIR_ROUNDS = 4
# Enough Back presses to climb out of the deepest settings screen a launcher
# leads to, and one descent into the browser; see Device.enter_file_browser.
LAUNCHER_DESCENT_STEPS = 10
# The rows a launcher's own entries can occupy: everything between its title
# and its status row.
LAUNCHER_ENTRY_ROWS = range(2, SCREEN_ROWS - 1)


class Unrecoverable(RuntimeError):
    pass


class UiWedged(Unrecoverable):
    """The UI task is not answering the menu button or injected keys.

    Told apart from the other things that stop this gate -- a device that has
    gone off the network, a menu screen of the wrong size -- because it is the
    one repair() has an answer for. Resetting the machine releases a wedged UI
    task; it does nothing for a device that has stopped answering, and hiding a
    transport fault behind four resets would only make it slower to report.
    """


class Device:
    """The device this gate drives, addressed through its target.

    The two halves of this gate belong to different machines on a cartridge
    target such as "u2@c64u": the menu screen, the menu button and the reset
    are the cartridge's, while the keys that back out of a nested object have
    to be injected into the computer it is plugged into, because the cartridge
    answers machine:input with HTTP 501. Assuming one host for both is what
    made the gate report "the root browser is not on top; a nested object
    still holds the UI" for the whole of a U2 run: every unwind key went to the
    cartridge, was refused, and nothing on the menu ever moved. See
    tests/lib/targets.py.
    """

    def __init__(self, host: str, password: Optional[str], timeout: float) -> None:
        self.target = targets.parse(host)
        self.host = self.target.device
        self.input_host = self.target.input_host
        self.password = password
        self.timeout = timeout

    @property
    def machine(self) -> machine_lib.Machine:
        """Which machine this is, asked once of the device.

        The contract this gate enforces is the same on all three, but the way
        to reach it is not: a C64 Ultimate keeps the file browser one level
        inside a launcher. See tests/lib/machine.py.
        """
        return machine_lib.identify(self.host, self._fetch_product)

    def _fetch_product(self) -> str:
        body = self._request("GET", "/v1/info")
        if body is None:
            raise Unrecoverable("/v1/info returned nothing")
        try:
            payload = json.loads(body.decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            raise Unrecoverable(f"/v1/info returned no readable product: {exc}")
        return (str(payload.get("product", "")),
                str(payload.get("firmware_version", "")))

    def enter_file_browser(self) -> None:
        """Descend from a launcher into the file browser, where there is one.

        A no-op on a machine whose menu button opens the browser itself.

        The cursor is moved onto the entry by reading which row it is on and
        which row the cursor is on, and the landing is confirmed before RETURN
        is sent. This gate runs around every suite on the machine that has a
        launcher, and a launcher lists hardware actions, so a burst of Back
        presses that under-delivered would leave RETURN to fire whichever of
        them the cursor stopped on.
        """
        entry = self.machine.launcher_browser_entry
        if entry is None:
            return
        for _ in range(LAUNCHER_DESCENT_STEPS):
            rows = self.screen()
            if rows is None or not describe_path(rows):
                return
            row = next((n for n, text in enumerate(rows) if entry in text), None)
            if row is None:
                self.tap(["left_shift", "cursor_left_right"])
                continue
            cursor = self.selected_row()
            if cursor is None:
                # Nothing on screen says where the cursor is, so nothing here
                # may press RETURN. Back out and let the loop look again.
                self.tap(["left_shift", "cursor_left_right"])
                continue
            for _ in range(abs(row - cursor)):
                self.tap(["cursor_up_down"] if row > cursor
                         else ["left_shift", "cursor_up_down"])
            if self.selected_row() != row:
                continue
            self.tap(["return"])

    def selected_row(self) -> Optional[int]:
        """Which row the open menu marks as the cursor, or None when unreadable."""
        body = self._request("GET", "/v1/machine:menu_screen")
        if body is None or len(body) != SCREEN_BYTES:
            return None
        try:
            return ui_backend.find_selected_row_rest(
                body[:ui_backend.SCREEN_CELLS], body[ui_backend.SCREEN_CELLS:],
                LAUNCHER_ENTRY_ROWS)
        except Failure:
            return None

    def _request(self, method: str, path: str, payload=None) -> Optional[bytes]:
        headers = {}
        if self.password:
            headers["X-Password"] = self.password
        body = None
        if payload is not None:
            body = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(
            rest_lib.url_for(self.target, path),
            data=body, headers=headers, method=method
        )
        # Transport and retry policy come from tests/lib/rest.py; see
        # rest.may_retry. The device serves few concurrent HTTP connections, so
        # a read can time out while it is busy and is simply retried. The menu
        # button is a toggle and is never resent blindly: a lost press is found
        # by re-reading the state and pressing again only if nothing changed.
        try:
            with rest_lib.retrying_urlopen(request, self.timeout) as response:
                return response.read()
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                return None
            raise Unrecoverable(f"{method} {path} returned HTTP {exc.code}") from exc
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            raise Unrecoverable(f"{method} {path} failed: {exc}") from exc

    def screen(self) -> Optional[List[str]]:
        """Menu screen as text rows, or None when the menu is closed."""
        body = self._request("GET", "/v1/machine:menu_screen")
        if body is None:
            return None
        if len(body) != SCREEN_BYTES:
            raise Unrecoverable(f"menu_screen returned {len(body)} bytes, expected {SCREEN_BYTES}")
        chars = "".join(
            chr(c & 0x7F) if 0x20 <= (c & 0x7F) <= 0x7E else " "
            for c in body[: SCREEN_ROWS * SCREEN_COLS]
        )
        return [chars[r * SCREEN_COLS:(r + 1) * SCREEN_COLS] for r in range(SCREEN_ROWS)]

    def showing_ok_dialog(self) -> bool:
        """Whether a dialog offering only Ok is on top; see showing_ok_dialog."""
        body = self._request("GET", "/v1/machine:menu_screen")
        if body is None or len(body) != SCREEN_BYTES:
            return False
        return showing_ok_dialog(body)

    def menu_is_open(self) -> bool:
        return self.screen() is not None

    def press_menu_button(self) -> None:
        try:
            self._request("PUT", "/v1/machine:menu_button")
        except Unrecoverable:
            # The toggle may or may not have been applied. Callers decide by
            # reading the state back, which answers it either way.
            pass
        time.sleep(MENU_SETTLE_SECONDS)

    def tap(self, inputs: List[str]) -> None:
        try:
            self._request(
                "POST",
                "/v1/machine:input",
                {"events": [{"kind": "keyboard", "inputs": inputs, "transition": "tap"}]},
            )
        except Unrecoverable:
            # A dropped keystroke just means one less unwind step; the caller
            # loops on the observed state.
            pass
        time.sleep(KEY_SETTLE_SECONDS)

    def reset_machine(self) -> None:
        """Reset the machine, closing the menu first where that is possible.

        MENU_C64_RESET releases the machine from whichever UserInterface holds
        it (c64_subsys.cc), and that teardown is what takes the device off the
        network when the UI it releases is not the one now active. Closing the
        menu first means there is nothing holding the machine to release.
        Reproduced live: resetting with the menu up, after the Interface Type
        had been switched during an open session, killed the device within a
        few cycles and needed a JTAG recovery.

        The close is a safety margin rather than a precondition, so a menu that
        will not shut does not cancel the reset: this is repair()'s last resort
        for a UI that keystrokes could not fix, and refusing to reset there
        would leave the device dirty for every suite that follows. The stale
        client the reset has to survive is created by switching the Interface
        Type under an open menu, which ui_backend.RestBackend no longer does.
        """
        for _ in range(2):
            if not self.menu_is_open():
                break
            self.press_menu_button()
            self.wait_menu(want_open=False)
        try:
            self._request("PUT", "/v1/machine:reset")
        except Unrecoverable:
            pass
        time.sleep(1.0)

    def computer_menu_open(self) -> bool:
        """Whether the C64-side computer has its own menu up.

        Only meaningful for a cartridge target. The computer's menu takes the
        keyboard while it is open, so keys meant for the cartridge never reach
        the C64 matrix it reads, and the cartridge's menu sits there unmoved
        while every key is answered with HTTP 200. Reproduced directly: with
        the computer's menu left open, RUN/STOP changed nothing on the
        cartridge; with it closed, the same key closed the cartridge's menu.
        """
        if not self.target.split:
            return False
        headers = {"X-Password": self.password} if self.password else {}
        request = urllib.request.Request(
            rest_lib.url_for(self.target.computer, "/v1/machine:menu_screen"),
            headers=headers, method="GET")
        try:
            with rest_lib.retrying_urlopen(request, self.timeout):
                return True
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                return False
            raise Unrecoverable(
                f"the computer's menu_screen returned HTTP {exc.code}") from exc
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            raise Unrecoverable(f"the computer stopped answering: {exc}") from exc

    def clear_computer_menu(self) -> None:
        """Get the computer's own menu out of the way of the cartridge."""
        for _ in range(2):
            if not self.computer_menu_open():
                return
            headers = {"X-Password": self.password} if self.password else {}
            request = urllib.request.Request(
                rest_lib.url_for(self.target.computer,
                                 "/v1/machine:menu_button"),
                data=b"", headers=headers, method="PUT")
            try:
                with rest_lib.retrying_urlopen(request, self.timeout):
                    pass
            except (OSError, TimeoutError, urllib.error.URLError,
                    urllib.error.HTTPError):
                pass
            time.sleep(MENU_SETTLE_SECONDS)
        if self.computer_menu_open():
            raise Unrecoverable(
                f"{self.target.computer} keeps its own menu open, so keys "
                f"cannot reach {self.target.device}")

    def wait_menu(self, want_open: bool) -> bool:
        deadline = time.monotonic() + MENU_TOGGLE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if self.menu_is_open() == want_open:
                return True
            time.sleep(pacing.POLL_INTERVAL_SECONDS)
        return False

    def wait_screen_change(self, before: List[str]) -> Optional[List[str]]:
        """The screen once it differs from `before`.

        Returns None if the menu closed, and `before` unchanged if the screen
        never moved within the timeout, so a caller that only needs the latest
        state can use the result without checking which happened.

        A keystroke does not land at the same speed everywhere. Injected into
        the machine it drives, it is applied almost at once; injected into the
        computer a cartridge is plugged into, it goes out over REST, into that
        computer's C64 keyboard matrix, and is picked up when the cartridge
        next scans it. One sample taken straight after the key reads the
        screen the key has not reached yet, and every decision made from it is
        about a state that no longer exists a moment later.
        """
        deadline = time.monotonic() + MENU_TOGGLE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            after = self.screen()
            if after is None or after != before:
                return after
            time.sleep(pacing.POLL_INTERVAL_SECONDS)
        return before


def describe_path(rows: List[str]) -> str:
    """Why this screen is not the file browser at the root, or "" when it is.

    The browser puts the directory it is showing on the status row and nothing
    else does, so a screen with no path there is not the browser at all: on a
    C64 Ultimate that is the launcher, or one of the settings screens it leads
    to.
    """
    path = rows[PATH_ROW].split()
    if not path:
        return "no path shown on the status row"
    if path[0] != ROOT_PATH:
        return f"browser is at {path[0]!r}, not {ROOT_PATH!r}"
    return ""


def describe(rows: List[str]) -> str:
    """Why this screen is not the clean root browser, or "" when it is."""
    if EMPTY_MARKER in "\n".join(rows):
        return f"browser listing is {EMPTY_MARKER!r}"
    return describe_path(rows)


def try_open_menu(device: Device) -> bool:
    """Whether the menu could be opened, unwinding a blocked UI task first.

    Reports rather than raises, because repair() has one more thing to try
    after this fails and cannot try it while an exception is on its way out.
    """
    if device.menu_is_open():
        return True
    device.press_menu_button()
    if device.wait_menu(want_open=True):
        return True
    # The button is ignored while the UI task sits in a modal. Back out
    # of it; a 404 from menu_screen does not prove the UI is idle.
    for _ in range(UNWIND_PRESSES):
        device.tap(["run_stop"])
        if device.menu_is_open():
            return True
    device.press_menu_button()
    return device.wait_menu(want_open=True)


def open_menu(device: Device) -> List[str]:
    """Open the menu, unwinding a blocked UI task if the button does nothing.

    Tried twice, because the descent into the browser can itself close the
    menu. `enter_file_browser` walks Back towards the launcher, and on a
    machine whose launcher entry it cannot find on screen it presses Back
    until the menu is gone, which is a screen that read as an open menu on the
    way in and as no menu on the way out. Reopening is what that costs; a
    machine that does it twice is not racing, and is handed to repair() as a
    wedge so the reset at the end of the round can have it.
    """
    for attempt in range(2):
        if not try_open_menu(device):
            raise UiWedged(
                "the menu will not open; the UI task is blocked and RUN/STOP "
                "did not release it"
            )
        # A C64 Ultimate's menu button opens a launcher, not the browser, and
        # it reopens wherever it was last left. Everything below expects the
        # browser.
        device.enter_file_browser()
        rows = device.screen()
        if rows is not None:
            return rows
    raise UiWedged(
        "the menu reported open and then returned no screen, twice; the UI "
        "task is not holding a browser the gate can read"
    )


def close_menu(device: Device) -> None:
    if not device.menu_is_open():
        return
    device.press_menu_button()
    if device.wait_menu(want_open=False):
        return
    # A dialog offering only Ok holds the menu open and ignores the button.
    # Answering it is the only way past, and this is the first place that
    # notices: every caller below reaches for close_menu before it reaches for
    # the unwind that also knows about such a dialog. Observed on a C64
    # Ultimate, where a CFG load left "There were errors." on screen and the
    # gate reported a UI that was not reading keys at all.
    if device.showing_ok_dialog():
        device.tap(["return"])
        device.press_menu_button()
        if device.wait_menu(want_open=False):
            return
    raise UiWedged("the menu will not close")


def describe_open_menu(device: Device) -> str:
    """Why the open menu is not the root browser, or "" when it is.

    Costs one RUN/STOP press per level between the browser and the closed
    menu, and leaves the menu closed either way, which is the state the
    contract asks for.

    The screen cannot answer this on its own. The Assembly 64 query form prints
    the same "/" status row as the root browser and leaves the listing area
    filled with its own fields, so describe() calls it clean and the gate hands
    the next suite a modal instead of a browser. RUN/STOP leaves the menu only
    once the root browser has focus, so the menu closing is what proves nothing
    is stacked on top of it. How many presses that takes is a property of the
    machine: a C64 Ultimate's launcher sits between the browser and the closed
    menu, so one press there only returns to the launcher.
    """
    rows = device.screen()
    if rows is None:
        return "the menu reported open but returned no screen"
    problem = describe(rows)
    if problem:
        close_menu(device)
        return problem
    for _ in range(device.machine.back_presses_to_close_menu):
        device.tap(["run_stop"])
    # Waited for rather than sampled: see Device.wait_screen_change. Reading
    # once here called a cartridge target dirty on every suite, and then
    # pressed the menu button against a RUN/STOP that was still in flight.
    if device.wait_menu(want_open=False):
        return ""
    close_menu(device)
    return "the root browser is not on top; a nested object still holds the UI"


def showing_ok_dialog(body: bytes) -> bool:
    """Whether the frontmost window is a dialog whose only action is Ok.

    Some dialogs report a result and offer one button. Back does not dismiss
    them, so the unwind below presses its way around the object stack for ever
    and the device is abandoned as unhealthy with a perfectly responsive UI on
    screen. Observed on a C64 Ultimate, where a CFG load left "There were
    errors." over the browser and every later suite in that run was skipped.

    Read from the character plane rather than the text rows, because those
    render the window frame as spaces: the dialog is drawn over a listing, so
    its row reads "base_7.bin         Ok         N  750K" and the button
    cannot be told from a file called Ok. Inside the window's own columns it
    is the only thing there. The frame parser is the one the suites use; see
    ui_backend.find_open_window.

    RETURN is otherwise refused here because it activates whatever the cursor
    is on. It is safe on this screen and only this screen: a lone Ok is the
    single thing the window can do, so the keystroke cannot pick anything
    else.
    """
    rows = range(2, SCREEN_ROWS - 1)
    chars = body[:ui_backend.SCREEN_CELLS]
    window = ui_backend.find_open_window(chars, rows)
    if window == ui_backend.whole_screen(rows):
        return False
    said = []
    for row in window.rows:
        start = row * ui_backend.SCREEN_WIDTH
        text = "".join(
            chr(c & 0x7F) if 0x20 <= (c & 0x7F) <= 0x7E else " "
            for c in chars[start + window.first_column:start + window.last_column])
        if text.strip():
            said.append(text.strip())
    return bool(said) and said[-1].lower() == "ok"


def unwind(device: Device) -> None:
    """Back out of nested objects and directories until the menu closes.

    RUN/STOP leaves one nested object, or one directory level, per press, and
    leaves the menu entirely once the root browser has focus, so the menu
    closing is the signal that the object stack is empty. Stopping on what the
    screen shows instead would stop on the Assembly 64 query form, which reports
    the root browser's own "/" path.

    F5 is never pressed here: it opens the task menu onto the Assembly 64
    entry, which is what creates this mess in the first place. RETURN is
    pressed only to answer a dialog that offers nothing else; see
    sole_action_row.
    """
    for _ in range(UNWIND_PRESSES):
        rows = device.screen()
        if rows is None:
            return
        # LEFT leaves a directory or disk, which is what the menu's own help
        # calls "go one level up". RUN/STOP leaves the menu or backs out of a
        # nested object, so it cannot walk the path.
        device.tap(["left_shift", "cursor_left_right"])
        after = device.wait_screen_change(rows)
        if after is None:
            return
        if after[PATH_ROW] == rows[PATH_ROW]:
            # The path did not move, so this is a nested object rather than a
            # directory. Back out of it instead.
            device.tap(["run_stop"])
            settled = device.wait_screen_change(after)
            if settled is None:
                return
            if settled == after and device.showing_ok_dialog():
                device.tap(["return"])
                device.wait_screen_change(settled)


def repair(device: Device) -> None:
    """Bring the UI back to the root browser, cursor homed.

    Escalates: unwind with keystrokes, then close and reopen the menu so the
    browser reloads its listing, then reset the machine. A root browser showing
    no items cannot be fixed by keystrokes, because there is nothing to back out
    of; only a reload repopulates it.

    A menu that will not open at all skips straight to the reset, because every
    rung below it needs an open menu to work on. Observed on a C64 Ultimate,
    which answered REST, ran the C64 and served FTP while machine:menu_button
    returned 200 and machine:menu_screen stayed at 404 through repeated presses
    and RUN/STOP: the UI task was blocked somewhere no keystroke reaches. One
    machine:reset released it, and the menu opened on the next press. Before
    this, the first open_menu of round 0 raised instead, so the reset this
    function keeps as its last resort was never reached and the whole run was
    abandoned with the device reported unhealthy.
    """
    for round_index in range(REPAIR_ROUNDS):
        try:
            if not try_open_menu(device):
                device.reset_machine()
                continue
            open_menu(device)
            if not describe_open_menu(device):
                home_cursor(device)
                return
            open_menu(device)
            unwind(device)
            open_menu(device)
            if not describe_open_menu(device):
                home_cursor(device)
                return
            # Reopening runs appear(), which re-inits the root browser and reloads it.
            open_menu(device)
            close_menu(device)
            open_menu(device)
            if not describe_open_menu(device):
                home_cursor(device)
                return
        except UiWedged:
            # The menu stopped answering part-way through a round: it would not
            # close, or it would not reopen. Neither can be pressed past, and
            # both are what the reset is for.
            device.reset_machine()
            continue
        if round_index >= 1:
            # Last resort: release the host and restart the machine, then reload.
            device.reset_machine()

    open_menu(device)
    problem = describe_open_menu(device)
    if problem:
        raise Unrecoverable(f"could not reach the root browser: {problem}")
    home_cursor(device)


def home_cursor(device: Device) -> None:
    """Put the browser cursor on the first entry, and leave the menu closed.

    A convenience, not part of the contract: the cursor cannot be read back from
    the screen matrix, so it could not be verified.
    """
    open_menu(device)
    for _ in range(HOME_PRESSES):
        device.tap(["left_shift", "cursor_up_down"])
    close_menu(device)


def verify(device: Device) -> str:
    """Report why the device is dirty, or "" when it satisfies the contract.

    Observing costs one menu open and close, and leaves the menu closed on every
    path.
    """
    # On a cartridge target the computer's own menu has to be out of the way
    # first, or every key below is delivered to it instead.
    device.clear_computer_menu()
    if device.menu_is_open():
        close_menu(device)
        return "the menu was left open"
    device.press_menu_button()
    if not device.wait_menu(want_open=True):
        return "the menu will not open; the UI task is blocked in a modal"
    # Where the menu button lands is a property of the machine, so reaching
    # the browser from it is part of satisfying the contract rather than a
    # breach of it: a C64 Ultimate always opens its launcher, and reporting
    # that as dirty would make every suite on it pay for a repair that had
    # nothing to fix.
    device.enter_file_browser()
    return describe_open_menu(device)


def diagnose(device: Device) -> List[str]:
    """What a reader needs to tell one stuck UI from another.

    Written because a real wedge reported only "the menu will not close", and
    working out what had actually happened meant poking the device by hand.
    The three things that turned out to matter, in the order they were needed:

    - where the browser is. The wedge above sat in a /Temp directory a killed
      suite had deleted, which is not something any other line would have said.
    - whether the listing is empty, because an empty one cannot be backed out
      of by keystrokes and needs a reload.
    - whether the UI reacts to a keystroke at all. That is what separates a
      browser in an awkward place, which keys can fix, from a UI task that has
      stopped reading them, which only a restart can.
    """
    lines: List[str] = []
    rows = device.screen()
    if rows is None:
        return ["the menu is closed, so there is nothing on screen to show"]
    path = rows[PATH_ROW].strip()
    lines.append(f"browser path row: {path!r}")
    if EMPTY_MARKER in "\n".join(rows):
        lines.append(f"the listing is {EMPTY_MARKER!r}, so there is nothing to back out of; "
                     "only a reload repopulates it")
    # A key that changes nothing at all is the signal that the UI task has
    # stopped reading them. RUN/STOP is used because it is the one key that
    # cannot activate anything.
    device.tap(["run_stop"])
    after = device.wait_screen_change(rows)
    if after is None:
        lines.append("RUN/STOP closed the menu, so the UI is reading keys")
    elif after == rows:
        lines.append("the screen did not change after RUN/STOP: the UI task is not "
                     "reading injected keys, which no keystroke can fix")
    else:
        lines.append("the screen changed after RUN/STOP, so the UI is reading keys")
    return lines


def report_failure(message: str, device: Device) -> None:
    print(message)
    for line in diagnose(device):
        print(f"  {line}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-H", "--host", required=True)
    parser.add_argument("-p", "--password", default=None)
    parser.add_argument("-t", "--timeout", type=float, default=10.0)
    # Named --action, not --mode: every suite uses --mode for the UI transport
    # (telnet/freeze/overlay), and one flag name meaning two unrelated things
    # across the same test tree is a trap for anyone reading a command line.
    parser.add_argument("--action", choices=("verify", "ensure"), required=True)
    parser.add_argument("--label", default="", help="suite name, used in messages")
    args = parser.parse_args()

    device = Device(args.host, args.password or None, args.timeout)
    who = f" after {args.label}" if args.label else ""

    try:
        if args.action == "verify":
            problem = verify(device)
            if problem:
                report_failure(f"UI state dirty{who}: {problem}", device)
                return 2
            return 0

        problem = verify(device)
        if not problem:
            return 0
        print(f"UI state dirty{who}: {problem}; repairing")
        repair(device)
        problem = verify(device)
        if problem:
            report_failure(f"UI state still dirty{who}: {problem}", device)
            return 1
        print("UI state repaired")
        return 0
    except Unrecoverable as exc:
        try:
            report_failure(f"UI state check failed{who}: {exc}", device)
        except Unrecoverable:
            # The device went away while being asked about. The original
            # message is the one that matters; say why there is nothing more.
            print(f"UI state check failed{who}: {exc}")
            print("  the device stopped answering while it was being diagnosed")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
