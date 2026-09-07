#!/usr/bin/env python3
# E2E: drives the machine's online file search against the live remote server.

"""The online file search, end to end, through the menu, against the real service.

Which service that is depends on the machine: an Ultimate 64 and an Ultimate II+
search Assembly 64, and a C64 Ultimate searches CommoServe. The two draw the same
query form and differ in where the menu keeps the entry, so one set of scenarios
covers both; see Device.form_title and Device.search_in_launcher.

Queries really are sent to that server. The point is not to prove the server
works: it is to prove a user can drive it through the UI and, above all, that the
UI survives being driven badly. The form is the one place in the menu that blocks
on a network fetch and owns a modal edit field, and it is one RETURN away from a
freshly opened menu, so a user lands in it by accident easily.

Most checks are therefore negative: abort mid-query, hammer keys during a fetch,
leave the edit field by the menu button, submit nothing, overrun the field. After
each one the device must still be responsive, the menu must still open, and the
browser must still be reachable.

Requires the device to have a working network route to that service. The suite
skips, rather than fails, when the service cannot be reached, so a broken uplink
is not reported as a firmware defect.
"""
import argparse
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import rest as rest_lib
import targets
from report import (
    teardown_step, Failure,
    check,
    check_skip,
    detail,
    section,
    suite_fail,
    suite_ok,
    suite_skip,
)
from menu import wait_until  # noqa: E402
import machine  # noqa: E402
import search_form  # noqa: E402
from search_form import SearchForm  # noqa: E402
from ui_backend import (
    Backend,
    MODE_TELNET,
    Snapshot,
    add_mode_argument,
    make_backend,
    strip_frame,
)

MENU_BUTTON_PATH = "/v1/machine:menu_button"

MENU_TOGGLE_TIMEOUT = 6.0
EMPTY_QUERY_MESSAGE = "Queries cannot be empty"
EMPTY_MARKER = "< No Items >"
# The task menu is built from every registered category, so it takes noticeably
# longer to draw than an ordinary redraw.
TASK_MENU_TIMEOUT = 10.0
QUERY_TIMEOUT = 40.0
RECOVER_TIMEOUT = 15.0
# Unwinding is bounded by time, not by a press count: the UI task ignores keys
# while a fetch is in flight, so a press can take as long as the service does.
UNWIND_BUDGET = 60.0
UNWIND_STEP_TIMEOUT = 6.0
# Longer than the 26-character edit limit in AssemblySearchForm::change().
OVERLONG_TEXT = "abcdefghijklmnopqrstuvwxyz0123456789"
# Both corpora answer it: Assembly 64 with 20 rows, CommoServe with one.
SEARCH_TERM = "turrican"
ENTRY_ROWS = range(1, 24)
STATUS_ROW = 24
# Telnet renders the same form in its 24-row remote session, one row shorter
# than REST's 25-row physical display.
TELNET_ENTRY_ROWS = range(1, 23)
TELNET_STATUS_ROW = 23


# The form never appeared, which tests/e2e/lib/search_form.py raises and
# main() reports as a suite skip: whether a third-party service answers is not
# what a run of this firmware measures.
Skip = search_form.Unreachable


class Device:
    def __init__(
        self,
        backend: Backend,
        mode: str,
        host: str,
        password: str | None,
        timeout: float,
    ) -> None:
        self.backend = backend
        self.mode = mode
        self.host = host
        self.password = password
        self.timeout = timeout
        # Everything this suite does inside the query form goes through here,
        # so the suite reads no field label off the screen itself. The one
        # step the form cannot do for itself is reaching it, because where the
        # menu keeps the entry is the machine's business; see reach_form.
        self.form: SearchForm = SearchForm(
            backend, mode, self.entry_rows, self.form_title, self.reach_form,
            telnet_mode=MODE_TELNET)

    @property
    def entry_rows(self) -> range:
        return TELNET_ENTRY_ROWS if self.mode == MODE_TELNET else ENTRY_ROWS

    # -- the online search, which is a different service on each machine --
    #
    # An Ultimate 64 and an Ultimate II+ search Assembly 64, reached from the
    # first entry of the task menu. A C64 Ultimate searches CommoServe,
    # reached from its launcher, and its task menu has no search entry at all.
    # What the two services put on that form differs, which is why the fields
    # are discovered rather than named; see tests/e2e/lib/search_form.py.
    # These four are navigation, which is the machine's; see
    # tests/lib/machine.py.
    @property
    def form_title(self) -> str:
        return self.backend.machine.search_form_title

    @property
    def search_entry(self) -> str:
        return self.backend.machine.search_menu_entry

    @property
    def search_in_launcher(self) -> bool:
        return self.backend.machine.search_in_launcher

    @property
    def task_menu_key(self) -> str:
        return self.backend.machine.task_menu_key

    @property
    def status_row(self) -> int:
        return TELNET_STATUS_ROW if self.mode == MODE_TELNET else STATUS_ROW

    def screen(self) -> Snapshot | None:
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

    def rows(self) -> list[str] | None:
        snapshot = self.screen()
        if snapshot is None:
            return None
        return snapshot.lines

    def cursor_row(self) -> int | None:
        """Row the selection sits on, or None when the menu is closed."""
        return self.form.cursor_row()

    def path_row(self) -> str:
        """The browser path from the status row, or "" when the menu is closed."""
        snapshot = self.screen()
        if snapshot is None:
            return ""
        status = strip_frame(snapshot.line(self.status_row)).split()
        return status[0] if status else ""

    def type_text(self, text: str) -> None:
        self.backend.send_text(text, f"type {text}")

    def send_key(self, key: str) -> Snapshot | None:
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

    def send_key_repeat(self, key: str, count: int) -> Snapshot | None:
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
        return self.form.visible()

    def reach_form(self) -> None:
        """Navigate to the search entry and open it, from wherever the UI is.

        SearchForm.open() calls this and then waits for the form's title, so
        this is only the navigation: back out to the root browser, then open
        the entry from wherever this machine keeps it.
        """
        unwind_to_root(self, "opening the query form")
        if self.search_in_launcher:
            open_search_entry_in_launcher(self)
        else:
            open_search_entry_in_task_menu(self)

    def screen_changed(self, before: Snapshot) -> bool:
        current = self.screen()
        return current is None or (
            current.lines != before.lines
            or current.reverse_cells != before.reverse_cells
        )


def device_is_alive(host: str, password: str | None, timeout: float) -> bool:
    headers: dict[str, str] = {}
    if password:
        headers["X-Password"] = password
    request = urllib.request.Request(f"http://{targets.device_of(host)}/v1/version", headers=headers)
    try:
        with rest_lib.retrying_urlopen(request, timeout) as response:
            return response.status == 200
    except (OSError, TimeoutError, urllib.error.URLError):
        return False


def press_menu_button(device: Device) -> None:
    """Use the REST-only menu-button API that this scenario specifically tests."""
    headers: dict[str, str] = {}
    if device.password:
        headers["X-Password"] = device.password
    request = urllib.request.Request(
        f"http://{targets.device_of(device.host)}{MENU_BUTTON_PATH}",
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
    """The task menu is drawn, with the search as its (default) entry.

    Waiting for the screen merely to change is not enough. The browser refreshes
    drive status by itself, so a change can happen before the task menu exists,
    and RETURN would then open whatever the browser cursor is on instead. The
    The entry's text can only come from the task menu itself, and it is always
    the menu's first, default-selected category on a freshly opened, not yet
    navigated menu (exactly the state Device.reach_form calls this in), so its
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
    return search_form.label_row(device.rows() or [], device.search_entry) is not None


def open_search_entry_in_task_menu(device: Device) -> None:
    """The task menu's first entry is the search, so open it and press RETURN.

    RETURN is only sent once that entry is on screen, so the sequence does not
    depend on how long the task menu takes to build.
    """
    device.send_key(device.task_menu_key)
    if not wait_until(lambda: task_menu_ready(device), TASK_MENU_TIMEOUT):
        raise Failure(f"the task menu did not offer {device.search_entry!r}")
    device.send_key("ENTER")


def launcher_entry_row(device: Device) -> tuple[int, int] | None:
    """(row of the search entry, row of the cursor), or None when either is gone.

    Both come from one screen, because a repaint between two reads would make
    the distance between them describe a screen that no longer exists. That is
    the same rule Backend.selection_and_rows exists for.
    """
    try:
        cursor, rows = device.backend.selection_and_rows(device.entry_rows)
    except Failure:
        return None
    row = next((n for n, text in enumerate(rows)
                if device.search_entry in text), None)
    return None if row is None else (row, cursor)


def open_search_entry_in_launcher(device: Device) -> None:
    """Leave the browser for the launcher and open the search entry there.

    RUN/STOP at the root browser leaves the browser rather than closing the
    menu, which is where the launcher's own entries appear (see
    Machine.back_presses_to_close_menu). The cursor is then moved by reading
    which row the entry is on and which row the cursor is on, rather than by
    counting presses from an assumed position: the launcher lists what this
    machine can do, so an entry's row is not a constant.
    """
    device.send_key("RUNSTOP")
    if not wait_until(lambda: launcher_entry_row(device) is not None,
                      TASK_MENU_TIMEOUT):
        raise Failure(f"the launcher did not offer {device.search_entry!r}; "
                      f"screen was:\n{device.form.describe_screen()}")
    found = launcher_entry_row(device)
    if found is None:
        raise Failure(f"{device.search_entry!r} left the launcher between two "
                      f"reads; screen was:\n{device.form.describe_screen()}")
    row, cursor = found
    if row != cursor:
        device.send_key_repeat("DOWN" if row > cursor else "UP", abs(row - cursor))
    landed = launcher_entry_row(device)
    if landed is None or landed[1] != row:
        raise Failure(f"the launcher cursor is not on {device.search_entry!r} "
                      f"at row {row}; screen was:\n{device.form.describe_screen()}")
    device.send_key("ENTER")


def unwind_to_root(device: Device, what: str) -> None:
    """Back out with RUN/STOP until the root browser is reached.

    RUN/STOP pops one nested object or one directory level per press. REST closes
    the menu once the root browser has focus; Telnet remains connected but loses
    its selected menu row at that same boundary.

    What is on screen cannot be used for this. A result list reports
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
            prime_selection_marker(device)
            return
        device.send_key("RUNSTOP")
        if device.screen() is None:
            device.ensure_ready()
            return
        # None also means "marker never measured", and the measurement is at
        # the root this loop walks towards, so stopping for it is circular.
        if (device.mode == MODE_TELNET
                and device.backend.selected_sgr is not None
                and device.cursor_row() is None):
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

    A result list reports the same "/" path as the root browser
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
    # The outer frame comes off first. A C64 Ultimate draws its own file
    # browser inside a framed window, so "any border character on screen"
    # is true of its root listing and this could never be satisfied there:
    # every scenario then walked until the menu closed instead of stopping at
    # the browser. What identifies an overlay is a box drawn inside the
    # listing, which survives the strip.
    # Listing rows only: the title row carries the product name, and the "+"
    # in "Ultimate II+L" would read as an overlay border.
    text = "\n".join(strip_frame(rows[index]) for index in device.entry_rows
                     if index < len(rows))
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


def prime_selection_marker(device: Device) -> None:
    """Teach the Telnet backend which colour marks a selected row.

    TelnetBackend measures that colour the first time it is asked for a
    selected row, and this suite never asks: inside the form,
    SearchForm.cursor_row reads the box interior instead, because the form's
    fields are indented past the first two columns TelnetBackend scans. So the
    colour was never measured, that reader found none, and every cursor_row()
    answered None as though the menu had closed.

    The root browser is the screen the backend can measure, so it is measured
    here while that screen is still up and before a form covers it.
    """
    if device.mode != MODE_TELNET or device.backend.selected_sgr is not None:
        return
    try:
        device.backend.selected_row(device.entry_rows)
    except Failure:
        pass


def scenario_open_and_leave(device: Device) -> None:
    section("the form opens from the menu and closes again")
    with check(f"open the {device.form_title}"):
        device.form.open()
    with check("the form leaves cleanly with RUN/STOP"):
        device.form.leave()
        if device.form_visible():
            raise Failure("the form is still on screen")
    recover(device, "opening and leaving the form")


def scenario_query_returns_results(device: Device) -> None:
    section(f"a real query is sent to {device.backend.machine.search_service}")
    with check("open the form and enter the first field"):
        device.form.open()
        before = device.screen()
        first = device.form.first_field()
        detail(f"the query term goes in {first.label!r}, the form's first field")
        device.form.edit(first)
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
    with check("the typed term lands in the first field"):
        device.form.type_text(SEARCH_TERM)
        device.form.confirm()
        shown = device.form.field(first.label).value
        if SEARCH_TERM not in shown.lower():
            raise Failure(f"the {first.label} field shows {shown!r}, "
                          f"not {SEARCH_TERM!r}")
    with check("the service answers and the results match what was asked for"):
        device.form.submit()
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
            detail("the service returned no matches")
        else:
            detail(f"{len(matches)} result rows mention {SEARCH_TERM!r}")
    recover(device, "running a query")


def field_term(index: int, field: search_form.Field) -> str:
    """A distinct value to type into a discovered free-text field.

    Built from the field's own name rather than from a table, so it stays
    readable in a failure message whatever the service calls its fields, and
    is unique per field so one field's value cannot pass for another's. Short
    enough for the 26-character limit in AssemblySearchForm::change().
    """
    return f"{field.name.lower().replace(' ', '')[:8]}{index}"


def scenario_dropdown_preserves_fields(device: Device) -> None:
    section("dropdown selections preserve the query form (#863)")
    form = device.form
    texts: tuple[search_form.Field, ...] = ()
    presets: tuple[search_form.Field, ...] = ()

    with check("the form draws fields, and at least one of them is a dropdown"):
        form.open()
        drawn = form.fields()
        detail(f"{len(drawn)} field(s) drawn: "
               + ", ".join(field.label for field in drawn))
        texts, presets = form.classify()
        detail(f"{len(texts)} free text: "
               + ", ".join(field.label for field in texts))
        detail(f"{len(presets)} dropdown: "
               + ", ".join(field.label for field in presets))
        # Discovery that found nothing must fail rather than pass quietly: a
        # scenario that silently covered no field would look the same as one
        # that covered every field, which is exactly the state this scenario
        # was in on a C64 Ultimate before the fields were discovered.
        if not presets:
            raise Failure(
                f"none of the {len(drawn)} field(s) on the "
                f"{device.form_title!r} took a value when it was cycled, so "
                f"this machine's service offers no dropdown and the "
                f"behaviour #863 is about cannot be exercised here")
        if not texts:
            raise Failure(
                f"every field on the {device.form_title!r} is a dropdown, so "
                f"there is no free-text field to prove is left alone")

    with check("populate the free-text fields"):
        expected: dict[str, str] = {}
        for index, field in enumerate(texts):
            form.edit(field)
            form.type_text(field_term(index, field))
            form.confirm()
        expected = form.values()
        for index, field in enumerate(texts):
            term = field_term(index, field)
            if expected.get(field.label) != term:
                raise Failure(f"{field.label} holds "
                              f"{expected.get(field.label)!r}, not {term!r}")

    for field in presets:
        label = field.label
        # A field discovery found and that has since gone is a defect, not a
        # reason to skip: SearchForm.field raises rather than returning None.
        with check(f"{label} +/- preserves existing fields"):
            form.focus(field)
            # Each step asserts the same thing: this field took a value, and
            # no other field moved. Reading this field's own value back after
            # every keystroke, rather than requiring the pair "+ +  -" to end
            # where it started, keeps the check independent of how many
            # presets the service listed for it. updown() clamps at both ends,
            # so a two-entry list would not come back to where it started.
            for keystroke in (form.cycle_next, form.cycle_next,
                              form.cycle_previous):
                keystroke()
                value = form.field(label).value
                if not value:
                    raise Failure(f"{label} lost its value when it was cycled")
                expected[label] = value
                form.expect_unchanged(expected)
        with check(f"cancelling {label} preserves existing fields"):
            form.edit(field)
            form.cancel()
            form.expect_unchanged(expected)
        confirm_label = f"confirming {label} preserves its value and all other fields"
        if device.backend.machine.skip_without_fix(
                machine.QUERY_FORM_SURVIVES_DROPDOWN_CONFIRM, confirm_label):
            # The check is skipped rather than run and failed, so the rest of
            # this scenario still runs on that machine: the defect empties
            # every field, so a form left in that state would fail every
            # later check for a reason that has nothing to do with what they
            # assert.
            continue
        with check(confirm_label):
            # The context menu opens on its own first item, not on the value
            # the field is holding: AssemblySearchForm::change calls
            # browser->context(0), and fetch_context_items lists the presets in
            # the order the service gave them. So DOWN then UP is a no-op on
            # the menu, and confirming applies the menu's first item. That
            # preserves the field's value only when the field is already on the
            # first preset, which is where the field is put first. The check
            # that came before this one drove the field to an arbitrary preset,
            # so without this step "confirming preserves its value" would be
            # asserting the order the service listed its presets in.
            form.focus(field)
            form.cycle_to_first()
            rewound = form.field(label).value
            if not rewound:
                raise Failure(f"{label} lost its value when it was rewound to "
                              f"its first preset")
            expected[label] = rewound
            form.expect_unchanged(expected)
            form.edit(field)
            form.press("DOWN")
            form.press("UP")
            form.confirm()
            form.expect_unchanged(expected)
    recover(device, "selecting query presets")


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
        device.form.open()
        device.form.edit(device.form.first_field())
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
        device.form.open()
        device.form.edit(device.form.first_field())
        device.form.type_text("zzz")
        device.form.cancel()
    with check("the form is still usable after the abort"):
        if not device.menu_is_open():
            raise Failure("the menu closed when the edit was aborted")
    recover(device, "aborting an edit")


def scenario_overlong_and_empty(device: Device) -> None:
    section("over-long and empty input")
    with check("type more than the field accepts"):
        device.form.open()
        device.form.edit(device.form.first_field())
        device.form.type_text(OVERLONG_TEXT)
        device.form.confirm()
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
            # Emptying the field is confirmed rather than assumed: a query
            # that still holds the previous check's text is a real search,
            # which takes tens of seconds to come back and reports no warning.
            device.form.clear(device.form.first_field())
            device.form.submit()
            if not wait_until(lambda: EMPTY_QUERY_MESSAGE in device.text(), QUERY_TIMEOUT):
                raise Failure(
                    f"submitting an empty query did not report "
                    f"{EMPTY_QUERY_MESSAGE!r}; screen was:\n{device.text()}"
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
        device.form.open()
        device.form.edit(device.form.first_field())
        device.form.type_text(SEARCH_TERM)
        device.form.confirm()
        device.form.submit()
        # Deliberately not this machine's task-menu key. These presses are
        # noise a user makes while the form is busy, and the recovery
        # afterwards expects to be at most one nested object away from the
        # browser. On a C64 Ultimate the task-menu key is F1, so sending it
        # here opened the task menu and the ENTER behind it activated the
        # first category, leaving the UI several levels inside a menu of
        # hardware actions that Back could not climb out of. F5 is Page Down
        # there and the task menu on the other two, so it is noise on all of
        # them.
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
            device.form.open()
            device.form.leave()
    with check("the menu button closes and reopens the form three times"):
        if device.mode == MODE_TELNET:
            check_skip(
                "requires REST-backed --mode freeze or --mode overlay, running under telnet"
            )
        else:
            for _ in range(3):
                device.form.open()
                press_menu_button(device)
                if not device.wait_menu(False, RECOVER_TIMEOUT):
                    raise Failure("the menu would not close with the form open")
                press_menu_button(device)
                if not device.wait_menu(True, RECOVER_TIMEOUT):
                    raise Failure("the menu would not reopen after the form was left open")
                device.form.leave()
    recover(device, "repeated open and abandon")


SCENARIOS = {
    "open-and-leave": scenario_open_and_leave,
    "query": scenario_query_returns_results,
    "dropdown-preserves-fields": scenario_dropdown_preserves_fields,
    "menu-button-in-field": scenario_menu_button_in_edit_field,
    "abort-edit": scenario_abort_edit,
    "overlong-and-empty": scenario_overlong_and_empty,
    "key-mashing": scenario_key_mashing,
    "reopen": scenario_reopen_repeatedly,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float, default=10.0)
    parser.add_argument(
        "--telnet-port",
        type=int,
        default=int(os.environ.get("U64_TELNET_PORT", "23")),
    )
    add_mode_argument(parser, default=os.environ.get("U64_MODE", "overlay"))
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
        # runs past column 40 and is truncated, exactly like the file
        # browser needs a wider Telnet session (see
        # browser_long_filename_test.py's TELNET_WIDTH).
        telnet_width=60,
    )
    device = Device(backend, args.mode, args.host, password, args.timeout)
    detail(f"this machine searches {backend.machine.search_service}, from "
           + ("its launcher" if device.search_in_launcher else "its task menu"))

    try:
        for name in names:
            SCENARIOS[name](device)
    except Skip as exc:
        suite_skip("assembly64_test", str(exc))
        teardown_step("put the UI back after skipping",
                    lambda: recover(device, "skipping"))
        return 0
    finally:
        def leave_any_open_form() -> None:
            if device.menu_is_open():
                device.form.leave()

        teardown_step("leave the query form", leave_any_open_form)
        backend.close()

    suite_ok("assembly64_test")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("assembly64_test", str(exc))
        raise SystemExit(1)
