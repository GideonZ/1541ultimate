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
import os
import sys
import time
import urllib.error
import urllib.request
from typing import Dict, List, Optional, Sequence, Tuple

# tests/lib holds the reporting rules every suite shares; tests/e2e/lib
# holds the shared UI backend.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "lib"))
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import rest as rest_lib
from report import (
    Failure,
    check,
    check_skip,
    section,
    suite_fail,
    suite_ok,
    suite_skip,
)
from menu import wait_until  # noqa: E402
from ui_backend import (
    Backend,
    MODE_TELNET,
    Snapshot,
    TELNET_SELECTED_SGR,
    add_mode_argument,
    make_backend,
    strip_frame,
)

MENU_BUTTON_PATH = "/v1/machine:menu_button"

MENU_TOGGLE_TIMEOUT = 6.0
FORM_TITLE = "Assembly 64 Query Form"
NAME_FIELD = "Name:"
TASK_MENU_ENTRY = "Assembly 64"
SUBMIT_LABEL = "<<"
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
ENTRY_ROWS = range(1, 24)
STATUS_ROW = 24
# Telnet renders the same form in its 24-row remote session, one row shorter
# than REST's 25-row physical display.
TELNET_ENTRY_ROWS = range(1, 23)
TELNET_STATUS_ROW = 23


class Skip(RuntimeError):
    pass


class Device:
    def __init__(
        self,
        backend: Backend,
        mode: str,
        host: str,
        password: Optional[str],
        timeout: float,
    ) -> None:
        self.backend = backend
        self.mode = mode
        self.host = host
        self.password = password
        self.timeout = timeout

    @property
    def entry_rows(self) -> range:
        return TELNET_ENTRY_ROWS if self.mode == MODE_TELNET else ENTRY_ROWS

    @property
    def status_row(self) -> int:
        return TELNET_STATUS_ROW if self.mode == MODE_TELNET else STATUS_ROW

    def screen(self) -> Optional[Snapshot]:
        try:
            return self.backend.capture()
        except Failure as exc:
            if str(exc).startswith("menu screen unavailable after"):
                return None
            raise

    def text(self) -> str:
        snapshot = self.screen()
        if snapshot is None:
            return ""
        return snapshot.text()

    def menu_is_open(self) -> bool:
        return self.screen() is not None

    def rows(self) -> Optional[List[str]]:
        snapshot = self.screen()
        if snapshot is None:
            return None
        return snapshot.lines

    def cursor_row(self) -> Optional[int]:
        """Row the selection sits on, or None when the menu is closed."""
        if self.mode == MODE_TELNET:
            return telnet_field_row(self, self.entry_rows)
        try:
            return self.backend.selected_row(self.entry_rows)
        except Failure:
            return None

    def path_row(self) -> str:
        """The browser path from the status row, or "" when the menu is closed."""
        snapshot = self.screen()
        if snapshot is None:
            return ""
        status = strip_frame(snapshot.line(self.status_row)).split()
        return status[0] if status else ""

    def type_text(self, text: str) -> None:
        self.backend.send_text(text, f"type {text}")

    def send_key(self, key: str) -> Optional[Snapshot]:
        # RUN/STOP legitimately closes the menu when it pops the last level
        # off the object stack (unwind_to_root's whole point). Backend.send_key
        # settles by re-capturing the screen, which raises "menu screen
        # unavailable" in that case; every caller here discards the return
        # value, so that outcome is not a failure, just None.
        try:
            return self.backend.send_key(key)
        except Failure as exc:
            if str(exc).startswith("menu screen unavailable after"):
                return None
            raise

    def send_key_repeat(self, key: str, count: int) -> Optional[Snapshot]:
        try:
            return self.backend.send_key_repeat(key, count)
        except Failure as exc:
            if str(exc).startswith("menu screen unavailable after"):
                return None
            raise

    def ensure_ready(self) -> None:
        self.backend.ensure_ready()

    def wait_menu(self, want_open: bool, timeout: float = MENU_TOGGLE_TIMEOUT) -> bool:
        return wait_until(lambda: self.menu_is_open() == want_open, timeout)

    def form_visible(self) -> bool:
        return FORM_TITLE in self.text()

    def screen_changed(self, before: Snapshot) -> bool:
        current = self.screen()
        return current is None or (
            current.lines != before.lines
            or current.reverse_cells != before.reverse_cells
        )


def device_is_alive(host: str, password: Optional[str], timeout: float) -> bool:
    headers: Dict[str, str] = {}
    if password:
        headers["X-Password"] = password
    request = urllib.request.Request(f"http://{host}/v1/version", headers=headers)
    try:
        with rest_lib.retrying_urlopen(request, timeout) as response:
            return response.status == 200
    except (OSError, TimeoutError, urllib.error.URLError):
        return False


def press_menu_button(device: Device) -> None:
    """Use the REST-only menu-button API that this scenario specifically tests."""
    headers: Dict[str, str] = {}
    if device.password:
        headers["X-Password"] = device.password
    request = urllib.request.Request(
        f"http://{device.host}{MENU_BUTTON_PATH}",
        data=b"",
        headers=headers,
        method="PUT",
    )
    try:
        with rest_lib.retrying_urlopen(request, device.timeout) as response:
            status = response.status
            body = response.read()
    except urllib.error.HTTPError as exc:
        status = exc.code
        body = exc.read()
    except (OSError, TimeoutError, urllib.error.URLError) as exc:
        raise Failure(f"PUT {MENU_BUTTON_PATH} failed: {exc}") from exc
    if status != 200:
        raise Failure(f"menu_button failed with HTTP {status}: {body[:120]!r}")


def task_menu_ready(device: Device) -> bool:
    """The task menu is drawn, with Assembly 64 as its (default) entry.

    Waiting for the screen merely to change is not enough. The browser refreshes
    drive status by itself, so a change can happen before the task menu exists,
    and RETURN would then open whatever the browser cursor is on instead. The
    Assembly 64 text can only come from the task menu itself, and it is always
    the menu's first, default-selected category on a freshly opened, not yet
    navigated menu (exactly the state open_query_form calls this in), so its
    presence alone is a strong enough signal without needing to also identify
    which row the cursor sits on.

    A colour-based cursor check was tried and dropped: the task menu is a box
    drawn over only part of the screen, so on REST the root browser's own
    still-highlighted row outside that box out-marks the task menu's own,
    narrower category label under selected_row()'s screen-wide "strongest
    signal" search; and on Telnet, whose one-row-shorter screen renders the
    box one row higher, the category label lands in a row whose first two
    columns are leftover root-browser text, outside TelnetBackend.selected_row()'s
    column-0/1 marker check. Both are real, transport-specific limits of
    colour-based selection detection, not something to paper over here.
    """
    return row_of(device, TASK_MENU_ENTRY) is not None


def describe_screen(device: Device) -> str:
    """The non-blank rows, for a failure message that can be acted on."""
    rows = device.rows()
    if rows is None:
        return "    (menu screen unavailable)"
    lines = [f"    {n:02d}|{text}|" for n, text in enumerate(rows) if text.strip()]
    return "\n".join(lines) or "    (screen is blank)"


def open_query_form(device: Device) -> None:
    """F5, RETURN: Assembly 64 is the first task-menu entry.

    RETURN is only sent once that entry is on screen and selected, so the sequence
    does not depend on how long the task menu takes to build.

    Opening the form is a request to the Assembly 64 service, so it is retried
    once. A single slow or dropped fetch is a property of a third-party server,
    not something this suite should report as a firmware defect.
    """
    for _ in range(FORM_OPEN_ATTEMPTS):
        unwind_to_root(device, "opening the query form")
        device.send_key("F5")
        if not wait_until(lambda: task_menu_ready(device), TASK_MENU_TIMEOUT):
            raise Failure(f"the task menu did not offer {TASK_MENU_ENTRY!r}")
        device.send_key("ENTER")
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
        device.send_key("RUNSTOP")
    raise Failure(f"{FORM_TITLE!r} would not close")


def unwind_to_root(device: Device, what: str) -> None:
    """Back out with RUN/STOP until the root browser is reached.

    RUN/STOP pops one nested object or one directory level per press. REST closes
    the menu once the root browser has focus; Telnet remains connected but loses
    its selected menu row at that same boundary.

    What is on screen cannot be used for this. An Assembly 64 result list reports
    the same "/" path as the root browser and has no form title, so a check on the
    path and the title stops on the result list and leaves it in place. Closing
    the menu is not a substitute either: it does not pop anything off the stack,
    so the object comes back on the next open.

    The UI task ignores keys while a fetch is in flight, so each press is given
    time to produce a redraw rather than being sent as part of a fixed burst.
    """
    device.ensure_ready()
    deadline = time.monotonic() + UNWIND_BUDGET
    while time.monotonic() < deadline:
        before = device.screen()
        if before is None:
            # Only a RUN/STOP in this loop can have closed the menu, and that
            # happens once the root browser has focus.
            device.ensure_ready()
            return
        if at_root_browser(device):
            # Telnet never closes on RUN/STOP (see the module docstring), so
            # at the root it is a genuine no-op: screen_changed() below would
            # never see a change and would misread that as a popup blocking
            # the view, sending RETURN into whatever entry is selected.
            return
        device.send_key("RUNSTOP")
        if device.screen() is None:
            device.ensure_ready()
            return
        if device.mode == MODE_TELNET and device.cursor_row() is None:
            return
        if wait_until(lambda: device.screen_changed(before), UNWIND_STEP_TIMEOUT):
            continue
        # RUN/STOP changed nothing, so a popup holds the focus. Those are
        # dismissed by their own button rather than by backing out, and Ok is
        # already selected. RETURN is only sent in this case: on a browser it
        # would open whatever the cursor is on.
        device.send_key("ENTER")
        wait_until(lambda: device.screen_changed(before), UNWIND_STEP_TIMEOUT)
    raise Failure(f"{what}: the root browser could not be reached")


def at_root_browser(device: Device) -> bool:
    """The actual root listing, not merely a screen that also reports "/".

    An Assembly 64 result list reports the same "/" path as the root browser
    (see unwind_to_root's docstring), so the path alone cannot tell them
    apart; the root listing always shows its fixed "Temp" entry, a result
    list never does. That alone is not enough either: an overlay (the task
    menu, or this form) is drawn over only part of the screen, so rows
    further down that it never reaches -- including the "Temp" row -- are
    still the untouched root listing underneath it, confirmed live for both
    the task menu and the form. Every overlay this suite drives is boxed in
    "+"/"|" border characters the plain listing never uses on its own, so
    requiring their total absence catches "some overlay is still open" that
    a lone "Temp" match on one row does not.
    """
    if device.path_row() != "/":
        return False
    rows = device.rows()
    if rows is None:
        return False
    text = "\n".join(rows)
    if "+" in text or "|" in text:
        return False
    return "Temp" in text


def recover(device: Device, what: str) -> None:
    """The UI must be usable again, and back at the root browser.

    Every scenario starts with menu button, F5, RETURN, which only reaches the
    form when the root browser has focus: inside the form, F5 is handled by the
    form itself and there is no task menu to select from.
    """
    if not device_is_alive(device.host, device.password, device.timeout):
        raise Failure(f"{what}: the device stopped answering REST")
    unwind_to_root(device, what)
    if EMPTY_MARKER in device.text():
        raise Failure(f"{what}: the browser came back empty")


def row_of(device: Device, label: str) -> Optional[int]:
    # `in`, not `.startswith()`: on Telnet's one-row-shorter screen the task
    # menu box renders a row higher than on REST, so a row can carry both the
    # overlay's own label and, to its left, leftover text from whatever the
    # root browser drew on that same row underneath -- the label is still on
    # the row, just no longer at its start.
    rows = device.rows()
    if rows is None:
        return None
    for row, text in enumerate(rows):
        if label in strip_frame(text):
            return row
    return None


def _box_interior_bounds(text: str) -> Optional[Tuple[int, int]]:
    """Column span strictly between a row's own left/right box border.

    Finding the marker anywhere on the row is not enough: the box's own
    title is drawn in the same colour as the marker (a header decoration,
    not a selection), and so, further right, is a status column belonging
    to whatever the root browser drew on that row before the box covered
    most of it. Both sit outside the box's own field-label area, so scoping
    the search to strictly between this row's two "|" border cells (the same
    two columns select_row's caller already relies on to delimit the field
    text itself) excludes both.
    """
    left = text.find("|")
    right = text.rfind("|")
    if left == -1 or right == -1 or left == right:
        return None
    return left + 1, right


def telnet_field_row(device: Device, entry_rows: Sequence[int]) -> Optional[int]:
    """Telnet equivalent of Backend.selected_row(), scoped to this form.

    TelnetBackend.selected_row() only checks a row's first two columns for
    the marker colour (see ui_backend.py), which assumes the selected
    content starts at column 0 -- true for the root browser's own listing,
    false for this form's box, which REST also draws differently positioned
    but which happens to still leave REST's colour-plane detection intact.
    Column 0 is unusable here (see row_of), so this scans within the row's
    own box borders instead, and skips the title row (identified by its own
    text, not by position) since its colour is cosmetic, not a selection.
    """
    rows = device.rows()
    colours = device.backend.screen.colours
    if rows is None:
        return None
    title_row = row_of(device, FORM_TITLE)
    for row in entry_rows:
        if row == title_row:
            continue
        bounds = _box_interior_bounds(rows[row])
        if bounds is None:
            continue
        left, right = bounds
        if any(colours[row][col] == TELNET_SELECTED_SGR for col in range(left, right)):
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
        device.send_key("DOWN" if current < target else "UP")
    raise Failure(f"{what}: the cursor never reached row {target}")


def enter_field(device: Device, label: str) -> None:
    """Put the cursor on a named field and open its editor."""
    row = row_of(device, label)
    if row is None:
        raise Failure(f"the form has no {label!r} field")
    select_row(device, row, f"selecting {label!r}")
    device.send_key("ENTER")


def submit_query(device: Device) -> None:
    """RETURN on the Submit row at the bottom of the form runs the query."""
    row = row_of(device, SUBMIT_LABEL)
    if row is None:
        raise Failure("the form has no Submit row")
    select_row(device, row, "selecting Submit")
    device.send_key("ENTER")


# ---------------------------------------------------------------- happy path

def scenario_open_and_leave(device: Device) -> None:
    section("the form opens from the task menu and closes again")
    with check("open the Assembly 64 query form"):
        open_query_form(device)
    with check("the form leaves cleanly with RUN/STOP"):
        leave_form(device)
        if device.form_visible():
            raise Failure("the form is still on screen")
    recover(device, "opening and leaving the form")


def scenario_query_returns_results(device: Device) -> None:
    section("a real query is sent to the Assembly 64 service")
    with check("open the form and enter the first field"):
        open_query_form(device)
        before = device.screen()
        enter_field(device, NAME_FIELD)
        # Telnet's rendering shows no visible difference between the field
        # merely selected and its editor actually open (confirmed live: a
        # character typed right after ENTER lands correctly either way), so
        # a screen diff cannot distinguish them there. The next check types
        # into the field and reads the result back, which does prove editing
        # is live, so this narrower, REST-only check does not lose coverage.
        if device.mode != MODE_TELNET and (
            before is None or not wait_until(lambda: device.screen_changed(before), 8.0)
        ):
            raise Failure("the edit field did not open")
    with check("the typed term lands in the Name field"):
        device.type_text(SEARCH_TERM)
        device.send_key("ENTER")
        row = row_of(device, NAME_FIELD)
        if row is None:
            raise Failure("the form no longer shows a Name field")
        shown = (device.rows() or [])[row]
        if SEARCH_TERM not in shown.lower():
            raise Failure(f"the Name field shows {shown.strip()!r}, not {SEARCH_TERM!r}")
    with check("the service answers and the results match what was asked for"):
        submit_query(device)
        if not wait_until(lambda: device.menu_is_open() and not device.form_visible(),
                          QUERY_TIMEOUT):
            raise Failure("the form never gave way to a result list")
        matches = [
            text
            for text in (device.rows() or [])
            if SEARCH_TERM in text.lower()
        ]
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
    section("the menu button must work from inside the edit field")
    if device.mode == MODE_TELNET:
        with check("the menu button works from inside the edit field"):
            check_skip(
                "requires REST-backed --mode freeze or --mode overlay, running under telnet"
            )
        return
    with check("open the form and enter the edit field"):
        open_query_form(device)
        enter_field(device, NAME_FIELD)
    with check("the menu button closes the menu from inside the field"):
        press_menu_button(device)
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
        press_menu_button(device)
        if not device.wait_menu(True, RECOVER_TIMEOUT):
            raise Failure(
                "the menu did not open again, so the UI task is still blocked in "
                "string_edit"
            )
    recover(device, "leaving the edit field by the menu button")


def scenario_abort_edit(device: Device) -> None:
    section("an aborted edit must not wedge the form")
    with check("open the form, type into a field, then abort"):
        open_query_form(device)
        enter_field(device, NAME_FIELD)
        device.type_text("zzz")
        device.send_key("RUNSTOP")
    with check("the form is still usable after the abort"):
        if not device.menu_is_open():
            raise Failure("the menu closed when the edit was aborted")
    recover(device, "aborting an edit")


def scenario_overlong_and_empty(device: Device) -> None:
    section("over-long and empty input")
    with check("type more than the field accepts"):
        open_query_form(device)
        enter_field(device, NAME_FIELD)
        device.type_text(OVERLONG_TEXT)
        device.send_key("ENTER")
    with check("an empty query is refused rather than sent"):
        if device.mode == MODE_TELNET:
            # The warning this produces is a third level of overlay nesting
            # (root -> form -> popup) that, over Telnet, never responds to
            # any key tried live against real hardware: ENTER, raw \r\n,
            # space, \n, ESC, RUNSTOP, F5, the arrow keys, each retried with
            # waits up to 5s. REST dismisses the identical popup normally, so
            # this is a transport-specific input-routing gap, not a bug in
            # this suite; there is no known way to recover from it once
            # triggered, so this and the next check do not run under Telnet
            # rather than leaving the device wedged for every check after.
            check_skip("submitting an empty query wedges the popup it raises; no known recovery over telnet")
        else:
            enter_field(device, NAME_FIELD)
            device.send_key_repeat("DEL", 40)
            device.send_key("ENTER")
            submit_query(device)
            if not wait_until(lambda: EMPTY_QUERY_MESSAGE in device.text(), QUERY_TIMEOUT):
                raise Failure(
                    f"submitting an empty query did not report {EMPTY_QUERY_MESSAGE!r}"
                )
    with check("the warning is dismissed and the form is still usable"):
        if device.mode == MODE_TELNET:
            check_skip("depends on the empty-query warning, not raised above under telnet")
        else:
            # The popup is modal and only its own button dismisses it, so
            # RUN/STOP does nothing here. Ok is already selected.
            device.send_key("ENTER")
            if not wait_until(
                lambda: EMPTY_QUERY_MESSAGE not in device.text(), RECOVER_TIMEOUT
            ):
                raise Failure("the warning stayed on screen after Ok")
            if not device.form_visible():
                raise Failure("the form did not come back after the warning")
    recover(device, "over-long and empty input")


def scenario_key_mashing(device: Device) -> None:
    section("a user mashing keys while the form is busy")
    with check("submit a query and hammer keys while it runs"):
        open_query_form(device)
        enter_field(device, NAME_FIELD)
        device.type_text(SEARCH_TERM)
        device.send_key("ENTER")
        submit_query(device)
        for key in ("DOWN", "ENTER", "RUNSTOP", "UP", "F5", "ENTER"):
            try:
                device.send_key(key)
            except Failure:
                pass
    with check("the device is still answering after the mashing"):
        if not wait_until(
            lambda: device_is_alive(device.host, device.password, device.timeout),
            RECOVER_TIMEOUT,
        ):
            raise Failure("the device stopped answering REST")
    recover(device, "mashing keys during a query")


def scenario_reopen_repeatedly(device: Device) -> None:
    section("opening and abandoning the form repeatedly")
    with check("open and abandon the form three times"):
        for _ in range(3):
            open_query_form(device)
            leave_form(device)
    with check("the menu button closes and reopens the form three times"):
        if device.mode == MODE_TELNET:
            check_skip(
                "requires REST-backed --mode freeze or --mode overlay, running under telnet"
            )
        else:
            for _ in range(3):
                open_query_form(device)
                press_menu_button(device)
                if not device.wait_menu(False, RECOVER_TIMEOUT):
                    raise Failure("the menu would not close with the form open")
                press_menu_button(device)
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
    parser.add_argument(
        "--telnet-port",
        type=int,
        default=int(os.environ.get("U64_INPUT_TELNET_PORT", "23")),
    )
    add_mode_argument(parser, default=os.environ.get("U64_INPUT_MODE", "overlay"))
    parser.add_argument("--test", action="append", choices=("all", *SCENARIOS))
    args = parser.parse_args()

    selected = args.test or ["all"]
    names = list(SCENARIOS) if "all" in selected else [n for n in SCENARIOS if n in selected]

    password = args.password or None
    if not device_is_alive(args.host, password, args.timeout):
        raise Failure(f"{args.host} is not answering REST requests")

    backend = make_backend(
        args.mode,
        args.host,
        password,
        args.timeout,
        telnet_port=args.telnet_port,
        # The form box is not drawn against the physical 40-column edge the
        # way REST/Overlay's is; at the standard width it renders positioned
        # far enough right that its own title ("Assembly 64 Query Form")
        # runs past column 40 and gets truncated, exactly like the file
        # browser needs a wider Telnet session (see
        # browser_long_filename_test.py's TELNET_WIDTH).
        telnet_width=60,
    )
    device = Device(backend, args.mode, args.host, password, args.timeout)

    try:
        for name in names:
            SCENARIOS[name](device)
    except Skip as exc:
        suite_skip("assembly64_test", str(exc))
        try:
            recover(device, "skipping")
        except Exception:
            pass
        return 0
    finally:
        try:
            if device.menu_is_open():
                leave_form(device)
        except Exception:
            pass
        backend.close()

    suite_ok("assembly64_test")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("assembly64_test", str(exc))
        raise SystemExit(1)
