#!/usr/bin/env python3
# E2E: drives the Assembly 64 search UI against the live remote server.

"""Assembly 64 end to end, through the menu, against the real service.

Queries really are sent to the Assembly 64 server. The point is not to prove the
server works: it is to prove a user can drive it through the UI and, above all,
that the UI survives being driven badly. The form is the one place in the menu
that blocks on a network fetch and owns a modal edit field, and it is reached by
a single RETURN on the first entry of the F5 task menu, so a user lands in it by
accident easily.

Most checks are therefore negative: abort mid-query, hammer keys during a fetch,
leave the edit field by the menu button, submit nothing, overrun the field. After
each one the device must still be responsive, the menu must still open, and the
browser must still be reachable.

Requires the device to have a working network route to the Assembly 64 service.
The suite skips, rather than fails, when the service cannot be reached, so a
broken uplink is not reported as a firmware defect.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from contextlib import contextmanager
from pathlib import Path
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))
from menu import repeat_key, wait_screen_settled, wait_until  # noqa: E402

CHECK_COUNT = 0
SCREEN_BYTES = 2000
SCREEN_COLS = 40
SCREEN_ROWS = 25
MENU_SCREEN_PATH = "/v1/machine:menu_screen"
MENU_BUTTON_PATH = "/v1/machine:menu_button"
INPUT_PATH = "/v1/machine:input"

SETTLE = 0.06
CURSOR_SETTLE = 0.12
MENU_TOGGLE_TIMEOUT = 6.0
FORM_TITLE = "Assembly 64 Query Form"
NAME_FIELD = "Name:"
TASK_MENU_ENTRY = "Assembly 64"
SUBMIT_LABEL = "<<"
ROOT_PATH = "/"
EMPTY_MARKER = "< No Items >"
# AssemblySearchForm refuses a query with no criteria, with a modal popup.
EMPTY_QUERY_MESSAGE = "Queries cannot be empty"
# The task menu is built from every registered category, so it takes noticeably
# longer to draw than an ordinary redraw.
TASK_MENU_TIMEOUT = 10.0
# The form is fetched from the remote service, so it is far slower than a redraw.
FORM_OPEN_TIMEOUT = 25.0
# One retry, so a single slow fetch from a third-party server is not a failure.
FORM_OPEN_ATTEMPTS = 2
QUERY_TIMEOUT = 40.0
RECOVER_TIMEOUT = 15.0
# More than the form has selectable rows, so a walk always terminates.
FIELD_WALK_LIMIT = 30
# Unwinding is bounded by time, not by a press count: the UI task ignores keys
# while a fetch is in flight, so a press can take as long as the service does.
UNWIND_BUDGET = 60.0
UNWIND_STEP_TIMEOUT = 6.0
# Longer than the 26-character edit limit in AssemblySearchForm::change().
OVERLONG_TEXT = "abcdefghijklmnopqrstuvwxyz0123456789"
# A term the Assembly 64 corpus has many entries for.
SEARCH_TERM = "turrican"
# Enough rows to distinguish a result list from an incidental match.
MIN_RESULT_ROWS = 3


class Failure(RuntimeError):
    pass


class Skip(RuntimeError):
    pass


@contextmanager
def check(label: str):
    global CHECK_COUNT
    CHECK_COUNT += 1
    print(f"[{CHECK_COUNT:02d}] {label} ... ", end="", flush=True)
    try:
        yield
    except Exception:
        print("FAIL", flush=True)
        raise
    print("OK", flush=True)


class Device:
    def __init__(self, host: str, password: Optional[str], timeout: float) -> None:
        self.host = host
        self.password = password
        self.timeout = timeout

    def request(self, method: str, path: str, payload=None) -> Tuple[int, bytes]:
        headers: Dict[str, str] = {}
        if self.password:
            headers["X-Password"] = self.password
        body = None
        if payload is not None:
            body = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(
            f"http://{self.host}{path}", data=body, headers=headers, method=method
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            raise Failure(f"{method} {path} failed: {exc}") from exc

    def alive(self) -> bool:
        try:
            status, _ = self.request("GET", "/v1/version")
        except Failure:
            return False
        return status == 200

    def screen(self) -> Optional[bytes]:
        status, body = self.request("GET", MENU_SCREEN_PATH)
        if status == 404:
            return None
        if status != 200:
            raise Failure(f"menu_screen failed with HTTP {status}")
        return body

    def text(self) -> str:
        body = self.screen()
        if body is None:
            return ""
        return "".join(
            chr(c & 0x7F) if 0x20 <= (c & 0x7F) <= 0x7E else " "
            for c in body[: SCREEN_ROWS * SCREEN_COLS]
        )

    def menu_is_open(self) -> bool:
        return self.screen() is not None

    def rows(self) -> Optional[List[Tuple[str, bool]]]:
        """Each row as its text and whether it is selected, from one screen read.

        The character half is identical wherever the selection is; only the colour
        half marks it, by drawing the selected row in reverse video. Both halves
        have to come from the same read to describe the same moment.
        """
        body = self.screen()
        if body is None:
            return None
        text = "".join(
            chr(c & 0x7F) if 0x20 <= (c & 0x7F) <= 0x7E else " "
            for c in body[: SCREEN_ROWS * SCREEN_COLS]
        )
        colours = body[SCREEN_ROWS * SCREEN_COLS:]
        out = []
        for row in range(SCREEN_ROWS):
            span = slice(row * SCREEN_COLS, (row + 1) * SCREEN_COLS)
            out.append((text[span], any(c > 0x0F for c in colours[span])))
        return out

    def cursor_row(self) -> Optional[int]:
        """Row the selection sits on, or None when the menu is closed."""
        rows = self.rows()
        if rows is None:
            return None
        for row, (_, selected) in enumerate(rows):
            if selected:
                return row
        return None

    def path_row(self) -> str:
        """The browser path from the status row, or "" when the menu is closed."""
        text = self.text()
        if not text:
            return ""
        status = text[(SCREEN_ROWS - 1) * SCREEN_COLS:SCREEN_ROWS * SCREEN_COLS].split()
        return status[0] if status else ""

    def press_menu_button(self) -> None:
        status, body = self.request("PUT", MENU_BUTTON_PATH)
        if status != 200:
            raise Failure(f"menu_button failed with HTTP {status}: {body[:120]!r}")
        time.sleep(SETTLE)

    def post_events(self, events: List[dict]) -> None:
        status, body = self.request("POST", INPUT_PATH, {"events": events})
        if status != 200:
            raise Failure(f"input failed with HTTP {status}: {body[:120]!r}")

    def tap(self, keys: List[str], settle: float = SETTLE) -> None:
        self.post_events([{"kind": "keyboard", "inputs": keys, "transition": "tap"}])
        time.sleep(settle)

    def type_text(self, text: str) -> None:
        keys = []
        for character in text:
            if character.isalnum():
                keys.append([character.lower()])
            elif character == " ":
                keys.append(["space"])
            else:
                raise Failure(f"cannot type {character!r} through the REST keyboard")
        repeat = [k for k in keys]
        for key in repeat:
            self.tap(key, 0.04)

    def wait_menu(self, want_open: bool, timeout: float = MENU_TOGGLE_TIMEOUT) -> bool:
        return wait_until(lambda: self.menu_is_open() == want_open, timeout)

    def open_menu(self) -> None:
        if not self.menu_is_open():
            self.press_menu_button()
            if not self.wait_menu(True):
                raise Failure("the menu did not open")

    def close_menu(self) -> None:
        if self.menu_is_open():
            self.press_menu_button()
            if not self.wait_menu(False):
                raise Failure("the menu did not close")

    def form_visible(self) -> bool:
        return FORM_TITLE in self.text()


def task_menu_ready(device: Device) -> bool:
    """The task menu is drawn and its Assembly 64 entry is the selected one.

    Waiting for the screen merely to change is not enough. The browser refreshes
    drive status by itself, so a change can happen before the task menu exists,
    and RETURN would then open whatever the browser cursor is on instead.
    """
    rows = device.rows()
    if rows is None:
        return False
    return any(TASK_MENU_ENTRY in text and selected for text, selected in rows)


def describe_screen(device: Device) -> str:
    """The non-blank rows, for a failure message that can be acted on."""
    rows = device.rows()
    if rows is None:
        return "    (menu_screen returned 404)"
    lines = [f"    {n:02d}|{text}|" for n, (text, _) in enumerate(rows) if text.strip()]
    return "\n".join(lines) or "    (screen is blank)"


def open_query_form(device: Device) -> None:
    """Menu button, F5, RETURN: Assembly 64 is the first task-menu entry.

    RETURN is only sent once that entry is on screen and selected, so the sequence
    does not depend on how long the task menu takes to build.

    Opening the form is a request to the Assembly 64 service, so it is retried
    once. A single slow or dropped fetch is a property of a third-party server,
    not something this suite should report as a firmware defect.
    """
    for _ in range(FORM_OPEN_ATTEMPTS):
        unwind_to_root(device, "opening the query form")
        device.tap(["f5"], SETTLE)
        if not wait_until(lambda: task_menu_ready(device), TASK_MENU_TIMEOUT):
            raise Failure(f"the task menu did not offer {TASK_MENU_ENTRY!r}")
        device.tap(["return"], SETTLE)
        if wait_until(device.form_visible, FORM_OPEN_TIMEOUT):
            return
    raise Skip(
        f"{FORM_TITLE!r} did not appear within {FORM_OPEN_TIMEOUT:.0f}s on "
        f"{FORM_OPEN_ATTEMPTS} attempts; the service is most likely unreachable "
        f"from the device. Last screen:\n{describe_screen(device)}"
    )


def leave_form(device: Device) -> None:
    """RUN/STOP unwinds one level per press until the form is gone."""
    for _ in range(8):
        if not device.menu_is_open() or not device.form_visible():
            return
        device.tap(["run_stop"], 0.35)
    raise Failure(f"{FORM_TITLE!r} would not close")


def unwind_to_root(device: Device, what: str) -> None:
    """Back out with RUN/STOP until the menu closes, then reopen it.

    RUN/STOP pops one nested object or one directory level per press and leaves
    the menu entirely once the root browser has focus, so the menu closing is the
    signal that nothing is left on the object stack. Reopening then lands on the
    root browser.

    What is on screen cannot be used for this. An Assembly 64 result list reports
    the same "/" path as the root browser and has no form title, so a check on the
    path and the title stops on the result list and leaves it in place. Closing
    the menu is not a substitute either: it does not pop anything off the stack,
    so the object comes back on the next open.

    The UI task ignores keys while a fetch is in flight, so each press is given
    time to produce a redraw rather than being sent as part of a fixed burst.
    """
    device.open_menu()
    deadline = time.monotonic() + UNWIND_BUDGET
    while time.monotonic() < deadline:
        before = device.screen()
        if before is None:
            # Only a RUN/STOP in this loop can have closed the menu, and that
            # happens once the root browser has focus.
            device.open_menu()
            return
        device.tap(["run_stop"], SETTLE)
        if wait_until(lambda: device.screen() != before, UNWIND_STEP_TIMEOUT):
            continue
        # RUN/STOP changed nothing, so a popup holds the focus. Those are
        # dismissed by their own button rather than by backing out, and Ok is
        # already selected. RETURN is only sent in this case: on a browser it
        # would open whatever the cursor is on.
        device.tap(["return"], SETTLE)
        wait_until(lambda: device.screen() != before, UNWIND_STEP_TIMEOUT)
    raise Failure(f"{what}: the root browser could not be reached")


def recover(device: Device, what: str) -> None:
    """The UI must be usable again, and back at the root browser.

    Every scenario starts with menu button, F5, RETURN, which only reaches the
    form when the root browser has focus: inside the form, F5 is handled by the
    form itself and there is no task menu to select from.
    """
    if not device.alive():
        raise Failure(f"{what}: the device stopped answering REST")
    unwind_to_root(device, what)
    if EMPTY_MARKER in device.text():
        raise Failure(f"{what}: the browser came back empty")
    device.close_menu()


def row_of(device: Device, label: str) -> Optional[int]:
    text = device.text()
    for row in range(SCREEN_ROWS):
        if text[row * SCREEN_COLS:(row + 1) * SCREEN_COLS].strip().startswith(label):
            return row
    return None


def select_row(device: Device, target: int, what: str) -> None:
    """Walk the selection onto a known row, checking each step.

    The cursor is only visible in the colour half of the screen, so a fixed number
    of key presses cannot be verified. This reads the position back instead, which
    also removes the assumption that the form clamps rather than wraps at its
    ends.
    """
    for _ in range(FIELD_WALK_LIMIT):
        current = device.cursor_row()
        if current is None:
            raise Failure(f"{what}: the menu closed while moving the cursor")
        if current == target:
            return
        key = ["cursor_up_down"] if current < target else ["left_shift", "cursor_up_down"]
        device.tap(key, CURSOR_SETTLE)
    raise Failure(f"{what}: the cursor never reached row {target}")


def enter_field(device: Device, label: str) -> None:
    """Put the cursor on a named field and open its editor."""
    row = row_of(device, label)
    if row is None:
        raise Failure(f"the form has no {label!r} field")
    select_row(device, row, f"selecting {label!r}")
    device.tap(["return"], 0.35)


def submit_query(device: Device) -> None:
    """RETURN on the Submit row at the bottom of the form runs the query."""
    row = row_of(device, SUBMIT_LABEL)
    if row is None:
        raise Failure("the form has no Submit row")
    select_row(device, row, "selecting Submit")
    device.tap(["return"], 0.5)


# ---------------------------------------------------------------- happy path

def scenario_open_and_leave(device: Device) -> None:
    print("-- the form opens from the task menu and closes again")
    with check("open the Assembly 64 query form"):
        open_query_form(device)
    with check("the form leaves cleanly with RUN/STOP"):
        leave_form(device)
        if device.form_visible():
            raise Failure("the form is still on screen")
    recover(device, "opening and leaving the form")


def scenario_query_returns_results(device: Device) -> None:
    print("-- a real query is sent to the Assembly 64 service")
    with check("open the form and enter the first field"):
        open_query_form(device)
        before = device.screen()
        enter_field(device, NAME_FIELD)
        if not wait_until(lambda: device.screen() != before, 8.0):
            raise Failure("the edit field did not open")
    with check("the typed term lands in the Name field"):
        device.type_text(SEARCH_TERM)
        device.tap(["return"], 0.4)
        row = row_of(device, NAME_FIELD)
        if row is None:
            raise Failure("the form no longer shows a Name field")
        shown = (device.rows() or [])[row][0]
        if SEARCH_TERM not in shown.lower():
            raise Failure(f"the Name field shows {shown.strip()!r}, not {SEARCH_TERM!r}")
    with check("the service answers and the results match what was asked for"):
        submit_query(device)
        if not wait_until(lambda: device.menu_is_open() and not device.form_visible(),
                          QUERY_TIMEOUT):
            raise Failure("the form never gave way to a result list")
        matches = [text for text, _ in (device.rows() or [])
                   if SEARCH_TERM in text.lower()]
        if not matches:
            # An empty corpus is the service's business, not the firmware's. What
            # this suite owns is that the UI left the form and stayed usable.
            print("(the service returned no matches) ", end="")
        elif len(matches) < MIN_RESULT_ROWS:
            raise Failure(
                f"only {len(matches)} result rows mention {SEARCH_TERM!r}, which "
                "does not look like a result list"
            )
    recover(device, "running a query")


# ------------------------------------------------------------ misbehaviour

def scenario_menu_button_in_edit_field(device: Device) -> None:
    print("-- the menu button must work from inside the edit field")
    with check("open the form and enter the edit field"):
        open_query_form(device)
        enter_field(device, NAME_FIELD)
    with check("the menu button closes the menu from inside the field"):
        device.press_menu_button()
        if not device.wait_menu(False, RECOVER_TIMEOUT):
            raise Failure(
                "the menu button did nothing while the edit field had focus, so the "
                "UI task is blocked in string_edit"
            )
    with check("the menu opens again, so the UI task left string_edit"):
        # This is the half of the check that can actually fail. menu_screen
        # answers 404 while the UI task is parked in the editor, so "the menu
        # closed" is also what a blocked UI looks like from outside. Only getting
        # the menu back tells the two apart.
        device.press_menu_button()
        if not device.wait_menu(True, RECOVER_TIMEOUT):
            raise Failure(
                "the menu did not open again, so the UI task is still blocked in "
                "string_edit"
            )
    recover(device, "leaving the edit field by the menu button")


def scenario_abort_edit(device: Device) -> None:
    print("-- an aborted edit must not wedge the form")
    with check("open the form, type into a field, then abort"):
        open_query_form(device)
        enter_field(device, NAME_FIELD)
        device.type_text("zzz")
        device.tap(["run_stop"], 0.35)
    with check("the form is still usable after the abort"):
        if not device.menu_is_open():
            raise Failure("the menu closed when the edit was aborted")
    recover(device, "aborting an edit")


def scenario_overlong_and_empty(device: Device) -> None:
    print("-- over-long and empty input")
    with check("type more than the field accepts"):
        open_query_form(device)
        enter_field(device, NAME_FIELD)
        device.type_text(OVERLONG_TEXT)
        device.tap(["return"], 0.4)
    with check("an empty query is refused rather than sent"):
        enter_field(device, NAME_FIELD)
        repeat_key(device.post_events, ["inst_del"], 40)
        wait_screen_settled(device.screen, 5.0)
        device.tap(["return"], 0.4)
        submit_query(device)
        if not wait_until(lambda: EMPTY_QUERY_MESSAGE in device.text(), QUERY_TIMEOUT):
            raise Failure(
                f"submitting an empty query did not report {EMPTY_QUERY_MESSAGE!r}"
            )
    with check("the warning is dismissed and the form is still usable"):
        # The popup is modal and only its own button dismisses it, so RUN/STOP
        # does nothing here. Ok is already selected.
        device.tap(["return"], 0.4)
        if not wait_until(
            lambda: EMPTY_QUERY_MESSAGE not in device.text(), RECOVER_TIMEOUT
        ):
            raise Failure("the warning stayed on screen after Ok")
        if not device.form_visible():
            raise Failure("the form did not come back after the warning")
    recover(device, "over-long and empty input")


def scenario_key_mashing(device: Device) -> None:
    print("-- a user mashing keys while the form is busy")
    with check("submit a query and hammer keys while it runs"):
        open_query_form(device)
        enter_field(device, NAME_FIELD)
        device.type_text(SEARCH_TERM)
        device.tap(["return"], 0.3)
        submit_query(device)
        # No settle: the point is to arrive while the fetch is still in flight.
        for keys in (["cursor_up_down"], ["return"], ["run_stop"],
                     ["left_shift", "cursor_up_down"], ["f5"], ["return"]):
            try:
                device.post_events(
                    [{"kind": "keyboard", "inputs": keys, "transition": "tap"}]
                )
            except Failure:
                pass
    with check("the device is still answering after the mashing"):
        if not wait_until(device.alive, RECOVER_TIMEOUT):
            raise Failure("the device stopped answering REST")
    recover(device, "mashing keys during a query")


def scenario_reopen_repeatedly(device: Device) -> None:
    print("-- opening and abandoning the form repeatedly")
    with check("open and abandon the form three times"):
        for _ in range(3):
            open_query_form(device)
            device.press_menu_button()
            if not device.wait_menu(False, RECOVER_TIMEOUT):
                raise Failure("the menu would not close with the form open")
            device.press_menu_button()
            if not device.wait_menu(True, RECOVER_TIMEOUT):
                raise Failure("the menu would not reopen after the form was left open")
            leave_form(device)
    recover(device, "repeated open and abandon")


SCENARIOS = {
    "open-and-leave": scenario_open_and_leave,
    "query": scenario_query_returns_results,
    "menu-button-in-field": scenario_menu_button_in_edit_field,
    "abort-edit": scenario_abort_edit,
    "overlong-and-empty": scenario_overlong_and_empty,
    "key-mashing": scenario_key_mashing,
    "reopen": scenario_reopen_repeatedly,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-H", "--host", default=os.environ.get("U64_INPUT_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float, default=10.0)
    parser.add_argument("--test", action="append", choices=("all", *SCENARIOS))
    args = parser.parse_args()

    selected = args.test or ["all"]
    names = list(SCENARIOS) if "all" in selected else [n for n in SCENARIOS if n in selected]

    device = Device(args.host, args.password or None, args.timeout)
    if not device.alive():
        raise Failure(f"{device.host} is not answering REST requests")

    try:
        device.close_menu()
        for name in names:
            SCENARIOS[name](device)
    except Skip as exc:
        print(f"assembly64_test: SKIP: {exc}", file=sys.stderr)
        try:
            recover(device, "skipping")
        except Exception:
            pass
        return 0
    finally:
        try:
            if device.alive() and device.menu_is_open():
                leave_form(device)
                device.close_menu()
        except Exception:
            pass

    print(f"assembly64_test: OK ({CHECK_COUNT} checks)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        print(f"assembly64_test: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
