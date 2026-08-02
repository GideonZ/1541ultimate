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
import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from typing import List, Optional

SCREEN_BYTES = 2000
SCREEN_COLS = 40
SCREEN_ROWS = 25
PATH_ROW = SCREEN_ROWS - 1
ROOT_PATH = "/"
EMPTY_MARKER = "< No Items >"

MENU_SETTLE_SECONDS = 0.35
MENU_TOGGLE_TIMEOUT_SECONDS = 6.0
# One press per nested object, plus room for directory levels.
UNWIND_PRESSES = 14
# Deeper than any listing the suites build.
HOME_PRESSES = 16
REPAIR_ROUNDS = 4
# The device serves a small fixed number of HTTP connections, so a request can
# time out while it is busy. Reads are idempotent and simply retried. Writes are
# not resent blindly: the menu button is a toggle, so a lost press is detected by
# re-reading the state and pressing again only if nothing changed.
TRANSPORT_RETRIES = 3
TRANSPORT_RETRY_PAUSE_SECONDS = 0.5


class Unrecoverable(RuntimeError):
    pass


class Device:
    def __init__(self, host: str, password: Optional[str], timeout: float) -> None:
        self.host = host
        self.password = password
        self.timeout = timeout

    def _request(self, method: str, path: str, payload=None, retries: int = 1) -> Optional[bytes]:
        headers = {}
        if self.password:
            headers["X-Password"] = self.password
        body = None
        if payload is not None:
            body = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(
            f"http://{self.host}{path}", data=body, headers=headers, method=method
        )
        last = None
        for attempt in range(max(1, retries)):
            try:
                with urllib.request.urlopen(request, timeout=self.timeout) as response:
                    return response.read()
            except urllib.error.HTTPError as exc:
                if exc.code == 404:
                    return None
                raise Unrecoverable(f"{method} {path} returned HTTP {exc.code}") from exc
            except (OSError, TimeoutError, urllib.error.URLError) as exc:
                last = exc
                if attempt + 1 < max(1, retries):
                    time.sleep(TRANSPORT_RETRY_PAUSE_SECONDS)
        raise Unrecoverable(f"{method} {path} failed: {last}")

    def screen(self) -> Optional[List[str]]:
        """Menu screen as text rows, or None when the menu is closed."""
        body = self._request("GET", "/v1/machine:menu_screen", retries=TRANSPORT_RETRIES)
        if body is None:
            return None
        if len(body) != SCREEN_BYTES:
            raise Unrecoverable(f"menu_screen returned {len(body)} bytes, expected {SCREEN_BYTES}")
        chars = "".join(
            chr(c & 0x7F) if 0x20 <= (c & 0x7F) <= 0x7E else " "
            for c in body[: SCREEN_ROWS * SCREEN_COLS]
        )
        return [chars[r * SCREEN_COLS:(r + 1) * SCREEN_COLS] for r in range(SCREEN_ROWS)]

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
        time.sleep(MENU_SETTLE_SECONDS)

    def reset_machine(self) -> None:
        try:
            self._request("PUT", "/v1/machine:reset")
        except Unrecoverable:
            pass
        time.sleep(1.0)

    def wait_menu(self, want_open: bool) -> bool:
        deadline = time.monotonic() + MENU_TOGGLE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if self.menu_is_open() == want_open:
                return True
            time.sleep(MENU_SETTLE_SECONDS)
        return False


def describe(rows: List[str]) -> str:
    """Why this screen is not the clean root browser, or "" when it is."""
    text = "\n".join(rows)
    if EMPTY_MARKER in text:
        return f"browser listing is {EMPTY_MARKER!r}"
    path = rows[PATH_ROW].split()
    if not path:
        return "no path shown on the status row"
    if path[0] != ROOT_PATH:
        return f"browser is at {path[0]!r}, not {ROOT_PATH!r}"
    return ""


def open_menu(device: Device) -> List[str]:
    """Open the menu, unwinding a blocked UI task if the button does nothing."""
    if not device.menu_is_open():
        device.press_menu_button()
        if not device.wait_menu(want_open=True):
            # The button is ignored while the UI task sits in a modal. Back out
            # of it; a 404 from menu_screen does not prove the UI is idle.
            for _ in range(UNWIND_PRESSES):
                device.tap(["run_stop"])
                if device.menu_is_open():
                    break
            if not device.menu_is_open():
                device.press_menu_button()
                if not device.wait_menu(want_open=True):
                    raise Unrecoverable(
                        "the menu will not open; the UI task is blocked and RUN/STOP "
                        "did not release it"
                    )
    rows = device.screen()
    if rows is None:
        raise Unrecoverable("menu reported open but returned no screen")
    return rows


def close_menu(device: Device) -> None:
    if device.menu_is_open():
        device.press_menu_button()
        if not device.wait_menu(want_open=False):
            raise Unrecoverable("the menu will not close")


def describe_open_menu(device: Device) -> str:
    """Why the open menu is not the root browser, or "" when it is.

    Costs one RUN/STOP press, and leaves the menu closed either way, which is
    the state the contract asks for.

    The screen cannot answer this on its own. The Assembly 64 query form prints
    the same "/" status row as the root browser and leaves the listing area
    filled with its own fields, so describe() calls it clean and the gate hands
    the next suite a modal instead of a browser. RUN/STOP leaves the menu only
    once the root browser has focus, so the menu closing is what proves nothing
    is stacked on top of it.
    """
    rows = device.screen()
    if rows is None:
        return "the menu reported open but returned no screen"
    problem = describe(rows)
    if problem:
        close_menu(device)
        return problem
    device.tap(["run_stop"])
    if device.menu_is_open():
        close_menu(device)
        return "the root browser is not on top; a nested object still holds the UI"
    return ""


def unwind(device: Device) -> None:
    """Back out of nested objects and directories until the menu closes.

    RUN/STOP leaves one nested object, or one directory level, per press, and
    leaves the menu entirely once the root browser has focus, so the menu
    closing is the signal that the object stack is empty. Stopping on what the
    screen shows instead would stop on the Assembly 64 query form, which reports
    the root browser's own "/" path.

    Never RETURN or F5 here: RETURN activates the entry under the cursor, and F5
    opens the task menu onto the Assembly 64 entry, which is what creates this
    mess in the first place.
    """
    for _ in range(UNWIND_PRESSES):
        rows = device.screen()
        if rows is None:
            return
        before = rows[PATH_ROW]
        # LEFT leaves a directory or disk, which is what the menu's own help
        # calls "go one level up". RUN/STOP leaves the menu or backs out of a
        # nested object, so it cannot walk the path.
        device.tap(["left_shift", "cursor_left_right"])
        after = device.screen()
        if after is None:
            return
        if after[PATH_ROW] == before:
            # The path did not move, so this is a nested object rather than a
            # directory. Back out of it instead.
            device.tap(["run_stop"])


def repair(device: Device) -> None:
    """Bring the UI back to the root browser, cursor homed.

    Escalates: unwind with keystrokes, then close and reopen the menu so the
    browser reloads its listing, then reset the machine. A root browser showing
    no items cannot be fixed by keystrokes, because there is nothing to back out
    of; only a reload repopulates it.
    """
    for round_index in range(REPAIR_ROUNDS):
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
    if device.menu_is_open():
        close_menu(device)
        return "the menu was left open"
    device.press_menu_button()
    if not device.wait_menu(want_open=True):
        return "the menu will not open; the UI task is blocked in a modal"
    return describe_open_menu(device)


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
                print(f"UI state dirty{who}: {problem}")
                return 2
            return 0

        problem = verify(device)
        if not problem:
            return 0
        print(f"UI state dirty{who}: {problem}; repairing")
        repair(device)
        problem = verify(device)
        if problem:
            print(f"UI state still dirty{who}: {problem}")
            return 1
        print("UI state repaired")
        return 0
    except Unrecoverable as exc:
        print(f"UI state check failed{who}: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
