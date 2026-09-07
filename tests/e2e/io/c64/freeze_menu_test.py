#!/usr/bin/env python3
# E2E: Verifies the freezer menu remains responsive across SID-mirroring settings.

"""Validate the freezer menu against SID mappings without auto address mirroring.

With "Interface Type" set to "Freeze" the firmware stops the C64 and pokes the SID volume
registers before it draws the menu. Those registers used to be hard coded to
$D418/$D438/$D518, which are only decoded while "Auto Address Mirroring" widens every SID
decode to the full $D400-$D7FF range. With mirroring disabled the writes addressed nothing
at all, which stalled the C64 bus and left the device on a black screen that only a forced
power off could clear. See GideonZ/1541ultimate#733.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import machine as machine_lib  # noqa: E402  (needs tests/lib on sys.path first)
import pacing  # noqa: E402  (needs tests/lib on sys.path first)
import rest as rest_lib  # noqa: E402  (needs tests/lib on sys.path first)
import targets  # noqa: E402  (needs tests/lib on sys.path first)
from api import UltimateApi  # noqa: E402  (needs tests/lib on sys.path first)
from backend import LAUNCHER_ENTRY_ROWS, find_selected_row_rest  # noqa: E402
from rest import header_value, json_object  # noqa: E402  (needs tests/lib first)
from report import (Failure, check, check_skip, check_start, detail, format_exception,
                    section, suite_fail, suite_ok, warn)


MENU_SCREEN_PATH = "/v1/machine:menu_screen"
MENU_BUTTON_PATH = "/v1/machine:menu_button"
READMEM_PATH = "/v1/machine:readmem"
CONFIGS_PATH = "/v1/configs"
UI_STORE = "User Interface Settings"
UI_ITEM = "Interface Type"
SID_STORE = "SID Addressing"
SID_ITEM = "Auto Address Mirroring"
FREEZE = "Freeze"
ENABLED = "Enabled"
DISABLED = "Disabled"
# The KERNAL interrupt bumps the low byte of the jiffy clock 50 or 60 times a second, so it
# only moves while the 6510 runs. That distinguishes a properly resumed C64 from one that
# was left stopped in Ultimax mode, which is what the reported "garbled characters" are.
JIFFY_ADDRESS = 0x00A2
JIFFY_SAMPLE_SECONDS = 0.5
JIFFY_SETTLE_SECONDS = 5.0
# Shared with every suite; see tests/lib/pacing.py.
MENU_TOGGLE_SETTLE_SECONDS = pacing.MENU_TOGGLE_SETTLE_SECONDS
MENU_TOGGLE_TIMEOUT_SECONDS = 5.0
# The query form is fetched from a third-party server, so it is slower than a
# redraw.
FORM_OPEN_TIMEOUT_SECONDS = 20.0
SCREEN_WIDTH = 40
SCREEN_HEIGHT = 25
SCREEN_CELLS = SCREEN_WIDTH * SCREEN_HEIGHT
SCREEN_BYTES = SCREEN_CELLS * 2
WEDGE_HINT = (
    "the device stopped answering REST requests. This is the wedge from issue #733; "
    "hold the menu button for 5 seconds or redeploy over JTAG to recover"
)


class RestSession:
    def __init__(self, host: str, password: str | None, timeout: float) -> None:
        self.target = targets.parse(host)
        self.host = self.target.device
        self.password = password
        self.timeout = timeout
        # For the calls this suite makes no assertion about, so that the menu
        # teardown has one implementation across the tree.
        self.api = UltimateApi(host, password, timeout)

    @property
    def machine(self) -> machine_lib.Machine:
        """Which machine this is, for its menu layout and keys."""
        info = self.api.info()
        return machine_lib.identify(self.host, lambda: (info.product,
                                                        info.firmware_version))

    def url(self, path: str, params: dict[str, object] | None = None) -> str:
        query = ""
        if params:
            query = "?" + urllib.parse.urlencode(params)
        # Keyboard injection belongs to the C64-side computer on a cartridge
        # target; see tests/lib/targets.py.
        return f"http://{self.target.host_for(path)}{path}{query}"

    def request(
        self,
        method: str,
        path: str,
        params: dict[str, object] | None = None,
        payload: dict[str, object] | None = None,
    ) -> tuple[int, dict[str, str], bytes]:
        headers: dict[str, str] = {}
        if self.password:
            headers["X-Password"] = self.password
        body = None
        if payload is not None:
            body = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"

        request = urllib.request.Request(self.url(path, params), data=body, headers=headers, method=method)
        try:
            with rest_lib.retrying_urlopen(request, self.timeout) as response:
                return response.status, dict(response.headers.items()), response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, dict(exc.headers.items()), exc.read()
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            raise Failure(f"{method} {self.url(path, params)} failed: {format_exception(exc)}") from exc

    def responds(self) -> bool:
        try:
            status, _, _ = self.request("GET", "/v1/version")
        except Failure:
            return False
        return status == 200

    def reset(self) -> None:
        # Through the API, which skips a reset that cannot change anything.
        self.api.machine.reset(wait=False)
        time.sleep(1.0)

    def close_menu_from_anywhere(self) -> None:
        self.api.machine.close_menu_from_anywhere()

    def config_path(self, store: str, item: str) -> str:
        return f"{CONFIGS_PATH}/{urllib.parse.quote(store)}/{urllib.parse.quote(item)}"

    def get_config(self, store: str, item: str) -> str:
        status, _, body = self.request("GET", self.config_path(store, item))
        if status != 200:
            raise Failure(f"reading '{item}' failed with HTTP {status}: {body[:160]!r}")
        data = json_object(f"config {item}", body)
        # The reply nests the setting under its category and name.
        entry = data.get(store, {})
        if isinstance(entry, dict):
            entry = entry.get(item, {})
        current = entry.get("current") if isinstance(entry, dict) else None
        if not isinstance(current, str):
            raise Failure(f"config '{item}' has no string 'current': {data!r}")
        return current

    def has_config(self, store: str, item: str) -> bool:
        """Whether this machine serves that setting at all.

        Hardware with no Overlay mode has no "Interface Type" to choose, so the
        setting is simply absent rather than present and fixed: an Ultimate
        II+L answers GET /v1/configs/User Interface Settings with an empty
        object. Everything this suite does is about switching between the two
        interface types, so its absence is a reason to skip rather than a
        failure to report.
        """
        status, _, body = self.request("GET", self.config_path(store, item))
        if status != 200:
            return False
        data = json_object(f"config {item}", body)
        entry = data.get(store, {})
        return isinstance(entry, dict) and isinstance(entry.get(item), dict)

    def set_config(self, store: str, item: str, value: str) -> None:
        status, _, body = self.request("PUT", self.config_path(store, item), params={"value": value})
        if status != 200:
            raise Failure(f"setting '{item}' to '{value}' failed with HTTP {status}: {body[:160]!r}")

    def menu_button(self) -> None:
        status, _, body = self.request("PUT", MENU_BUTTON_PATH)
        if status != 200:
            raise Failure(f"menu_button failed with HTTP {status}: {body[:160]!r}")
        time.sleep(MENU_TOGGLE_SETTLE_SECONDS)

    def tap(self, key: str) -> None:
        status, _, body = self.request(
            "POST",
            "/v1/machine:input",
            payload={"events": [{"kind": "keyboard", "inputs": [key], "transition": "tap"}]},
        )
        if status != 200:
            raise Failure(f"tapping '{key}' failed with HTTP {status}: {body[:160]!r}")
        time.sleep(MENU_TOGGLE_SETTLE_SECONDS)

    def tap_keys(self, keys) -> None:
        status, _, body = self.request(
            "POST",
            "/v1/machine:input",
            payload={"events": [{"kind": "keyboard", "inputs": list(keys), "transition": "tap"}]},
        )
        if status != 200:
            raise Failure(f"tapping {keys!r} failed with HTTP {status}: {body[:160]!r}")
        time.sleep(MENU_TOGGLE_SETTLE_SECONDS)

    def form_visible(self, title: str) -> bool:
        body = self.menu_screen_bytes()
        if body is None:
            return False
        text = "".join(
            chr(c & 0x7F) if 0x20 <= (c & 0x7F) <= 0x7E else " " for c in body[:1000]
        )
        return title in text

    def wait_form_title(self, title: str, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            body = self.menu_screen_bytes()
            if body is not None:
                text = "".join(
                    chr(c & 0x7F) if 0x20 <= (c & 0x7F) <= 0x7E else " " for c in body[:1000]
                )
                if title in text:
                    return True
            time.sleep(MENU_TOGGLE_SETTLE_SECONDS)
        return False

    def menu_screen_bytes(self) -> bytes | None:
        status, _, body = self.request("GET", MENU_SCREEN_PATH)
        if status == 404:
            return None
        if status != 200:
            raise Failure(f"menu_screen failed with HTTP {status}: {body[:160]!r}")
        return body

    def wait_screen_changes(self, before: bytes | None, timeout: float) -> bool:
        """Wait until the menu screen differs from 'before'.

        Drawing the context menu takes longer than the fixed settle used between
        key taps, and pressing the menu button while it is still being drawn
        loses the press. Waiting for the screen to actually change synchronises
        on the event itself instead of guessing how long it takes.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.menu_screen_bytes() != before:
                return True
            time.sleep(MENU_TOGGLE_SETTLE_SECONDS)
        return False

    def menu_is_open(self) -> bool:
        status, headers, body = self.request("GET", MENU_SCREEN_PATH)
        if status == 404:
            return False
        if status != 200:
            raise Failure(f"menu_screen failed with HTTP {status}: {body[:160]!r}")
        content_type = header_value(headers, "Content-Type")
        if "application/octet-stream" not in content_type:
            raise Failure(f"expected application/octet-stream from menu_screen, got {content_type!r}")
        if len(body) != SCREEN_BYTES:
            raise Failure(f"expected {SCREEN_BYTES} bytes from menu_screen, got {len(body)}")
        return True

    def wait_menu_state(self, want_open: bool) -> bool:
        deadline = time.monotonic() + MENU_TOGGLE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if self.menu_is_open() == want_open:
                return True
            time.sleep(MENU_TOGGLE_SETTLE_SECONDS)
        return False

    def read_jiffy(self) -> int:
        status, _, body = self.request(
            "GET", READMEM_PATH, params={"address": f"{JIFFY_ADDRESS:04X}", "length": 1}
        )
        if status != 200:
            raise Failure(f"readmem failed with HTTP {status}: {body[:160]!r}")
        if len(body) != 1:
            raise Failure(f"expected 1 byte from readmem, got {len(body)}")
        return body[0]

    def jiffy_advances(self) -> bool:
        first = self.read_jiffy()
        time.sleep(JIFFY_SAMPLE_SECONDS)
        return self.read_jiffy() != first


def require_alive(session: RestSession, label: str) -> None:
    if not session.responds():
        raise Failure(f"{label}: {WEDGE_HINT}")


def wedge_aware(session: RestSession, label: str, action) -> None:
    """Run 'action', and report a dead device as the known wedge rather than a raw error."""
    try:
        action()
    except Failure as exc:
        if not session.responds():
            raise Failure(f"{label}: {WEDGE_HINT}") from exc
        raise


def require_machine_running(session: RestSession, label: str) -> None:
    with check(label):
        deadline = time.monotonic() + JIFFY_SETTLE_SECONDS
        while time.monotonic() < deadline:
            if session.jiffy_advances():
                return
        raise Failure(
            f"{label}: the jiffy clock at ${JIFFY_ADDRESS:04X} is not advancing, so the C64 "
            "is not running. It must be resumed after the menu closes, and it must be at a "
            "prompt with KERNAL interrupts enabled before the test starts"
        )


def require_machine_frozen(session: RestSession, label: str) -> None:
    with check(label):
        if session.jiffy_advances():
            raise Failure(
                f"{label}: the jiffy clock at ${JIFFY_ADDRESS:04X} keeps advancing, so the menu "
                f"did not freeze the C64. Check that '{UI_ITEM}' really is '{FREEZE}'"
            )


def toggle_menu(session: RestSession, label: str, want_open: bool) -> None:
    def action() -> None:
        session.menu_button()
        if not session.wait_menu_state(want_open=want_open):
            raise Failure(f"menu did not {'open' if want_open else 'close'}")

    with check(label):
        wedge_aware(session, label, action)


def open_menu(session: RestSession) -> None:
    toggle_menu(session, "open the menu", want_open=True)
    require_machine_frozen(session, "C64 frozen while the menu is open")


def close_menu(session: RestSession) -> None:
    toggle_menu(session, "close the menu", want_open=False)
    require_machine_running(session, "C64 resumed after the menu closed")


def prepare(session: RestSession, mirroring: str) -> None:
    with check(f"select '{FREEZE}' interface with mirroring {mirroring.lower()}"):
        session.set_config(UI_STORE, UI_ITEM, FREEZE)
        session.set_config(SID_STORE, SID_ITEM, mirroring)
    require_alive(session, "applying the SID mapping")
    require_machine_running(session, "C64 running before the menu is opened")


def run_menu_cycle(session: RestSession, mirroring: str) -> None:
    section(f"menu cycle with {SID_ITEM} {mirroring}")
    prepare(session, mirroring)
    open_menu(session)
    close_menu(session)


def run_toggle_while_open(session: RestSession) -> None:
    section(f"{SID_ITEM} switched off while the menu is open")
    prepare(session, ENABLED)
    open_menu(session)
    with check(f"set {SID_ITEM} to {DISABLED} while the menu is open"):
        wedge_aware(
            session,
            "applying the new SID mapping",
            lambda: session.set_config(SID_STORE, SID_ITEM, DISABLED),
        )
    close_menu(session)


BROWSER_UNWIND_STEPS = 12
STATUS_ROW = SCREEN_HEIGHT - 1
HOME_CURSOR_STEPS = 14


def decode_rows(body: bytes) -> list[str]:
    rows = []
    for row in range(SCREEN_HEIGHT):
        cells = body[row * SCREEN_WIDTH:(row + 1) * SCREEN_WIDTH]
        rows.append("".join(chr(code & 0x7F) if 0x20 <= (code & 0x7F) <= 0x7E
                            else " " for code in cells))
    return rows


def menu_rows(session: RestSession) -> list[str] | None:
    """The menu screen as text, or None when the menu is not drawn."""
    body = session.menu_screen_bytes()
    if body is None or len(body) < SCREEN_CELLS:
        return None
    return decode_rows(body)


def browser_directory(rows: list[str]) -> str | None:
    """The path on the status row, or None. The launcher shows key hints there
    instead; a settings screen keeps the browser's path."""
    status = rows[STATUS_ROW].lstrip()
    if not status.startswith("/"):
        return None
    return status.split()[0]


def show_browser_root(session: RestSession) -> None:
    """Leave the menu showing the top of the file browser.

    Back at the browser root reaches the launcher on a C64 Ultimate and closes
    the menu on the others, so each step reads the screen rather than counting.
    """
    for _ in range(BROWSER_UNWIND_STEPS):
        if not session.menu_is_open():
            open_menu(session)
            continue
        rows = menu_rows(session)
        if rows is None:
            continue
        directory = browser_directory(rows)
        if directory == "/":
            return
        if directory is not None:
            session.tap_keys(["left_shift", "cursor_left_right"])
            continue
        if session.machine.menu_opens_on_launcher:
            home_cursor(session)
            session.tap("return")
            continue
        session.tap_keys(["left_shift", "cursor_left_right"])
    raise Failure("could not reach the root of the file browser")


def home_cursor(session: RestSession) -> None:
    for _ in range(HOME_CURSOR_STEPS):
        session.tap_keys(["left_shift", "cursor_up_down"])


def launcher_selection(session: RestSession) -> tuple[int, list[str]] | None:
    """(cursor row, rows) of the launcher on screen, or None when it is not."""
    body = session.menu_screen_bytes()
    if body is None or len(body) < SCREEN_BYTES:
        return None
    try:
        cursor = find_selected_row_rest(body[:SCREEN_CELLS], body[SCREEN_CELLS:],
                                        LAUNCHER_ENTRY_ROWS)
    except Failure:
        return None
    if cursor < 0:
        return None
    return cursor, decode_rows(body)


def put_launcher_cursor_on(session: RestSession, entry: str) -> None:
    """From the root browser, reach the launcher and select `entry` there."""
    show_browser_root(session)
    wedge_aware(session, "leaving the browser for the launcher",
                lambda: session.tap("run_stop"))
    deadline = time.monotonic() + MENU_TOGGLE_TIMEOUT_SECONDS
    found = None
    while time.monotonic() < deadline:
        found = launcher_selection(session)
        if found is not None and any(entry in text for text in found[1]):
            break
        time.sleep(MENU_TOGGLE_SETTLE_SECONDS)
    else:
        raise Failure(f"the launcher did not offer {entry!r}")
    cursor, rows = found
    row = next(n for n, text in enumerate(rows) if entry in text)
    for _ in range(abs(row - cursor)):
        session.tap_keys(["cursor_up_down"] if row > cursor
                         else ["left_shift", "cursor_up_down"])
    landed = launcher_selection(session)
    if landed is None or landed[0] != row:
        raise Failure(f"the launcher cursor is not on {entry!r} at row {row}")


def run_context_reopen(session: RestSession) -> None:
    """Reopen the freezer menu with a context menu still on the UI object stack.

    Closing the menu on a non-permanent host runs UserInterface::release_host(),
    which deinitialises every object on the stack without peeling any off, so
    'focus' still points at the context menu when the menu is next opened.
    ContextMenu::deinit() deletes its window and clears the pointer, and the
    next open runs appear(), which re-initialises each object through the
    no-argument init() and then calls redraw() on it.

    ContextMenu used to define only init(Window *, Keyboard *), a separate
    overload rather than an override, so the UIObject no-op ran, the window was
    never rebuilt, and redraw() dereferenced it. That halted the whole device:
    the fault was raised in ContextMenu::redraw() and reached soft_exceptions,
    which executes a break instruction and stops the CPU.

    Only the Freeze interface reaches this. The overlay host is permanent, so
    run_once() never re-runs appear() and the window is never torn down, which
    is why this needs the Freeze interface selected above.
    """
    section("reopen the freezer menu with a context menu left open")
    prepare(session, ENABLED)
    open_menu(session)
    with check("open the context menu on the first browser entry"):
        # Homed first: on the search entry RETURN opens a query form the menu
        # button cannot dismiss.
        show_browser_root(session)
        home_cursor(session)
        before = session.menu_screen_bytes()
        wedge_aware(session, "opening the context menu", lambda: session.tap("return"))
        if not session.wait_screen_changes(before, MENU_TOGGLE_TIMEOUT_SECONDS):
            raise Failure("the context menu was not drawn")
        if not session.menu_is_open():
            raise Failure("the menu closed when the context menu was opened")
    close_menu(session)
    with check("reopen the menu with the context menu still on the object stack"):
        wedge_aware(session, "reopening the freezer menu", session.menu_button)
        if not session.wait_menu_state(want_open=True):
            raise Failure("the menu did not reopen")
    require_alive(session, "reopening the menu over a deinitialised context menu")
    # Leave the stack unwound, so a later suite does not inherit the nested menu.
    with check("dismiss the restored context menu"):
        session.tap("run_stop")
    close_menu(session)


def open_search_entry(session: RestSession) -> None:
    """Put the machine's online-search entry under the cursor.

    The first entry of the task menu on an Ultimate 64 and an Ultimate II+; an
    entry of the launcher, reached by Back from the root browser, on a C64
    Ultimate.
    """
    if session.machine.search_in_launcher:
        put_launcher_cursor_on(session, session.machine.search_menu_entry)
        return
    before = session.menu_screen_bytes()
    wedge_aware(session, "opening the task menu",
                lambda: session.tap(session.machine.task_menu_key.lower()))
    if not session.wait_screen_changes(before, MENU_TOGGLE_TIMEOUT_SECONDS):
        raise Failure("the task menu was not drawn")


def run_menu_button_in_form(session: RestSession) -> None:
    """The menu button must still close the menu from inside the query form.

    The machine's online-search entry opens a query form, and a RETURN there
    enters the Name field, which puts the UI task inside
    UserInterface::string_edit. That loop tested only host->exists(), while the
    loop that polls the menu button lives in run_once() and is not running, so
    the button did nothing and the menu could not be opened again until the
    device was rebooted. machine:menu_screen answers 404 throughout, because
    C64::is_accessible() reports isFrozen, so nothing can detect the condition
    from outside.

    The modal helpers now poll the button. They also drop a press that predates
    the modal, otherwise one still latched from opening the menu would close the
    editor before a single character was accepted.
    """
    title = session.machine.search_form_title
    section(f"the menu button works inside the {title}")
    prepare(session, ENABLED)
    open_menu(session)
    with check(f"put the cursor on {session.machine.search_menu_entry!r}"):
        open_search_entry(session)
    with check(f"open the {title}"):
        wedge_aware(session, "opening the query form", lambda: session.tap("return"))
        if not session.wait_form_title(title, FORM_OPEN_TIMEOUT_SECONDS):
            raise Failure(f"{title!r} did not appear")
    with check("enter the form's first edit field"):
        wedge_aware(session, "entering the edit field", lambda: session.tap("return"))
        if not session.menu_is_open():
            raise Failure("the menu closed when the edit field was entered")
    with check("the menu button closes the menu from inside the edit field"):
        wedge_aware(session, "pressing the menu button", session.menu_button)
        if not session.wait_menu_state(want_open=False):
            raise Failure(
                "the menu button did nothing while the edit field had focus, so the "
                "UI task is still blocked in string_edit"
            )
    require_alive(session, "closing the menu from inside the query form")
    with check("the menu opens again afterwards"):
        wedge_aware(session, "reopening the menu", session.menu_button)
        if not session.wait_menu_state(want_open=True):
            raise Failure("the menu did not open again")
    # Leave the form behind, so a later suite does not inherit it. RUN/STOP backs
    # out one object per press and leaves the menu entirely once the root browser
    # has focus, so stop as soon as the form is gone rather than pressing a fixed
    # number of times.
    with check("leave the query form"):
        for _ in range(6):
            if not session.menu_is_open():
                break
            if not session.form_visible(title):
                break
            session.tap("run_stop")
        if session.form_visible(title):
            raise Failure(f"{title!r} is still on screen")
    if session.menu_is_open():
        close_menu(session)
    else:
        require_machine_running(session, "C64 resumed after the menu closed")


def expand_tests(selected) -> set:
    names = {"mirroring-off", "toggle-open", "mirroring-on", "context-reopen",
             "menu-button-in-form"}
    if not selected or "all" in selected:
        return names
    return {name for name in selected if name in names}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate the freezer menu against SID mappings without auto address mirroring."
    )
    cli.add_device_arguments(parser, password=None, timeout=5.0, colour=False)
    parser.add_argument("-r", "--rest-host", default=os.environ.get("U64_REST_HOST"))
    parser.add_argument(
        "--test",
        action="append",
        choices=("all", "mirroring-off", "toggle-open", "mirroring-on", "context-reopen",
                 "menu-button-in-form"),
    )
    parser.add_argument(
        "--repeat",
        type=int,
        default=1,
        metavar="COUNT",
        help="Number of times to run the selected checks (default: 1).",
    )
    args = parser.parse_args()

    if args.repeat < 1:
        parser.error("--repeat must be at least 1")

    session = RestSession(args.rest_host or args.host, args.password, args.timeout)
    tests = expand_tests(args.test)

    if not session.responds():
        raise Failure(f"{session.host} is not answering REST requests")

    session.close_menu_from_anywhere()
    session.reset()
    if not session.has_config(UI_STORE, UI_ITEM):
        check_start(f"this machine offers a '{UI_ITEM}' to switch")
        check_skip(f"no '{UI_ITEM}' setting on this machine, so there is no "
                   f"Overlay mode to switch away from and back to")
        suite_ok("freeze_menu_test")
        return 0
    initial_interface = session.get_config(UI_STORE, UI_ITEM)
    initial_mirroring = session.get_config(SID_STORE, SID_ITEM)
    detail(f"captured '{UI_ITEM}'={initial_interface}, '{SID_ITEM}'={initial_mirroring}")

    try:
        for iteration in range(args.repeat):
            if args.repeat > 1:
                section(f"pass {iteration + 1} of {args.repeat}")
            if "mirroring-off" in tests:
                run_menu_cycle(session, DISABLED)
            if "toggle-open" in tests:
                run_toggle_while_open(session)
            if "mirroring-on" in tests:
                run_menu_cycle(session, ENABLED)
            if "context-reopen" in tests:
                run_context_reopen(session)
            if "menu-button-in-form" in tests:
                run_menu_button_in_form(session)
    finally:
        # Never leave the device on a changed mapping or with the menu open.
        if session.responds():
            if session.menu_is_open():
                session.menu_button()
            session.set_config(SID_STORE, SID_ITEM, initial_mirroring)
            session.set_config(UI_STORE, UI_ITEM, initial_interface)
        else:
            warn("device is not responding; configuration left as-is")

    suite_ok("freeze_menu_test")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("freeze_menu_test", format_exception(exc))
        raise SystemExit(1)
