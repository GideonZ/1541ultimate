#!/usr/bin/env python3
# Reading the menu over REST, in overlay and freeze modes.

"""Reading the menu over REST, in overlay and freeze modes.

The screen comes back as a character plane and a colour plane
from machine:menu_screen, so the selection rules in backend.py
are what decide which row is the cursor.
"""

import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
from report import Failure
from collections.abc import Sequence
import api as api_lib
import json
import pacing
import rest as rest_lib
import screens as screen_spool
import targets
import time
import urllib.parse
import urllib.request
from menu import wait_screen_changes
from menu import wait_screen_settled
from backend import (Backend, CONFIGS_PATH, KEY_ALIASES, LAUNCHER_DESCENT_STEPS,
    LAUNCHER_ENTRY_ROWS, MENU_GLYPHS, NoCursorDrawn, POLL_INTERVAL_SECONDS,
    SCREEN_BYTES, SCREEN_CELLS, SCREEN_HEIGHT, SCREEN_WIDTH,
    SETTLE_TIMEOUT_SECONDS, Snapshot, char_to_combo, find_selected_row_rest,
    measure_cursor_colour, strip_frame)


# The routes this transport drives the UI through, and the settings it reads.
# Here rather than in backend.py because they belong to REST and not to the
# UI: a Telnet session drives the same menu and asks for none of them.
MENU_SCREEN_PATH = "/v1/machine:menu_screen"

MENU_BUTTON_PATH = "/v1/machine:menu_button"

INPUT_PATH = "/v1/machine:input"

UI_STORE = "User Interface Settings"

UI_ITEM = "Interface Type"

OVERLAY_MODE = "Overlay on HDMI"

# How many times to re-read the screen while no cursor marker is drawn at all.
CURSOR_SETTLE_ATTEMPTS = 4


def host_menu_open(host: str, password: str | None, timeout: float) -> bool:
    """Whether the on-device menu of one machine, named directly, is open.

    Takes a host rather than a target because the caller that needs it is
    asking about the *computer* half of a cartridge target, which the target's
    own routing would never send it to. A closed menu answers 404.
    """
    headers = {"X-Password": password} if password else {}
    request = urllib.request.Request(
        rest_lib.url_for(host, MENU_SCREEN_PATH), headers=headers)
    try:
        with rest_lib.retrying_urlopen(request, timeout) as response:
            response.read()
        return True
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            return False
        raise Failure(f"{MENU_SCREEN_PATH} on {host} failed: {exc}")
    except (OSError, urllib.error.URLError) as exc:
        raise Failure(f"{MENU_SCREEN_PATH} on {host} failed: {exc}")


def close_host_menu(host: str, password: str | None, timeout: float) -> None:
    """Shut the on-device menu of one machine, named directly.

    A cartridge target injects its keys into the computer, and the firmware
    decides what such an event means from whether the keyboard matrix is
    enabled (software/api/route_input.cc, isMatrixEnabled). While the
    computer's own menu is up the matrix is disabled, so the keys are pushed
    into that menu's queue instead of onto the matrix the cartridge taps. Every
    request still answers HTTP 200 and the cartridge never sees a keystroke, so
    this has to be checked rather than hoped for.
    """
    if not host_menu_open(host, password, timeout):
        return
    headers = {"X-Password": password} if password else {}
    request = urllib.request.Request(
        rest_lib.url_for(host, MENU_BUTTON_PATH), headers=headers,
        method="PUT")
    try:
        with rest_lib.retrying_urlopen(request, timeout) as response:
            response.read()
    except (OSError, urllib.error.URLError) as exc:
        raise Failure(f"{MENU_BUTTON_PATH} on {host} failed: {exc}")
    deadline = time.monotonic() + SETTLE_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        if not host_menu_open(host, password, timeout):
            return
        time.sleep(POLL_INTERVAL_SECONDS)
    raise Failure(
        f"the menu on {host} would not close, so keys sent to it would be "
        "consumed by that menu instead of reaching the cartridge")


class RestBackend(Backend):
    """Drives the physical/HDMI UI over REST: machine:input + machine:menu_screen.

    Opens the on-device menu if it is not already open and switches Interface
    Type to Overlay for the duration (restored on close), matching how
    tests/e2e/io/c64/freeze_menu_test.py captures and restores the same
    setting for Freeze.

    `host` is a target, so a cartridge target such as "u2@c64u" reads the menu
    from the cartridge and injects keys into the computer it is plugged into.
    See tests/lib/targets.py; the cartridge answers machine:input with HTTP
    501, and the keys reach it over the expansion port instead.
    """

    def __init__(
        self,
        host: str,
        password: str | None = None,
        timeout: float = 5.0,
        interface_type: str | None = OVERLAY_MODE,
    ) -> None:
        self.target = targets.parse(host)
        self.host = self.target.device
        self.input_host = self.target.input_host
        self.password = password
        self.timeout = timeout
        self.last_command = "<connect>"
        # What one key of a batch costs on this target. A cartridge target
        # pays the computer's matrix tap rate rather than the device's own key
        # queue; see tests/lib/pacing.py.
        # A property rather than a value: pacing keeps what a machine was
        # measured to need, by host, and a measurement taken during the run
        # has to reach the batches sent after it.
        self._key_drain_host = targets.device_of(self.target.input_host)
        # The foreground colour this machine marks the cursor row with, once a
        # screen has shown it unambiguously. See find_cursor_colour.
        self._cursor_colour: int | None = None
        self._original_interface_type: str | None = None
        if interface_type is not None:
            # Asked of the device rather than assumed from its name: a
            # cartridge has one way of drawing its UI and does not offer the
            # setting at all, so there is nothing to switch and nothing to
            # restore. Overlay and Freeze then mean the same transport there.
            current = self.get_config_optional(UI_STORE, UI_ITEM)
            if current is not None and current != interface_type:
                # Change it only with the menu closed. Which UserInterface owns
                # the machine is decided when the menu opens, so switching the
                # setting under an open one leaves the firmware holding a
                # client that is no longer the active interface. A machine
                # reset then tears that stale one down and takes the device off
                # the network, needing a JTAG recovery. Reproduced directly:
                # toggling the type with the menu up and then resetting killed
                # the device within a few cycles, while the same open-and-reset
                # without the toggle survived every attempt.
                #
                # _close_menu is best effort, so the result is checked here
                # rather than assumed: writing the setting anyway would be the
                # exact sequence this is meant to avoid. A session that cannot
                # get the menu shut refuses to start instead.
                self._close_menu()
                if self._menu_open():
                    raise Failure("the on-device menu would not close, so the "
                                  f"Interface Type cannot be set to {interface_type!r}")
                self._original_interface_type = current
                self.set_config(UI_STORE, UI_ITEM, interface_type)
                # Matches MENU_TOGGLE_SETTLE_SECONDS in freeze_menu_test.py: a
                # config change needs a moment to take effect before the menu
                # is opened, or the very next interaction can land mid-switch.
                time.sleep(0.25)
        if self.target.split:
            # The computer's menu, not this cartridge's: see close_host_menu.
            # Done before the cartridge's menu is opened, because from here on
            # every key this session sends goes to the computer.
            close_host_menu(self.input_host, password, timeout)
        self._open_menu()

    # -- transport --
    def _url(self, path: str, params: dict[str, object] | None = None) -> str:
        # The handle says where the device is, ports included, so one builder
        # answers for every caller here.
        return rest_lib.url_for(self.target, path, params)

    @property
    def key_drain_seconds(self) -> float:
        """What one key of a batch costs on this target.

        Keyed on the machine whose keyboard the keys actually cross, which on
        a cartridge target is the computer rather than the device under test.
        See tests/lib/pacing.py.
        """
        return pacing.key_drain_seconds(self.target.split, self._key_drain_host)

    @property
    def machine_host(self) -> str:
        return self.host

    @property
    def machine_password(self) -> str | None:
        return self.password

    def _request(
        self, method: str, path: str,
        params: dict[str, object] | None = None,
        payload: dict[str, object] | None = None,
    ) -> tuple[int, bytes]:
        body = None if payload is None else json.dumps(payload).encode("utf-8")
        headers: dict[str, str] = {}
        if self.password:
            headers["X-Password"] = self.password
        if body is not None:
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(self._url(path, params), data=body, headers=headers, method=method)
        # Transport and retry policy come from tests/lib/rest.py; see
        # rest.may_retry for the rule and why there is only one copy of it.
        #
        # Retrying cannot hide a double application here: the callers read the
        # cursor or the resulting name back, so a duplicated keystroke fails
        # that check rather than passing unnoticed.
        try:
            with rest_lib.retrying_urlopen(request, self.timeout) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            raise Failure(f"{method} {self._url(path, params)} failed: {exc}")

    # -- config --
    def get_config_optional(self, store: str, item: str) -> str | None:
        """The setting's value, or None when this device does not have it."""
        status, body = self._request(
            "GET", f"{CONFIGS_PATH}/{urllib.parse.quote(store)}/{urllib.parse.quote(item)}")
        if status != 200:
            raise Failure(f"reading '{item}' failed with HTTP {status}: {body[:160]!r}")
        entry = json.loads(body).get(store, {})
        if isinstance(entry, dict):
            entry = entry.get(item, {})
        current = entry.get("current") if isinstance(entry, dict) else None
        return current if isinstance(current, str) else None

    def get_config(self, store: str, item: str) -> str:
        current = self.get_config_optional(store, item)
        if current is None:
            raise Failure(f"config '{item}' has no string 'current' on {self.host}")
        return current

    def set_config(self, store: str, item: str, value: str) -> None:
        status, body = self._request(
            "PUT", f"{CONFIGS_PATH}/{urllib.parse.quote(store)}/{urllib.parse.quote(item)}",
            params={"value": value},
        )
        if status != 200:
            raise Failure(f"setting '{item}' to '{value}' failed with HTTP {status}: {body[:160]!r}")

    # -- menu open/close --
    def _menu_screen_body(self) -> bytes | None:
        status, body = self._request("GET", MENU_SCREEN_PATH)
        if status == 404:
            return None
        if status != 200:
            raise Failure(f"menu_screen failed with HTTP {status}: {body[:160]!r}")
        if len(body) != SCREEN_BYTES:
            raise Failure(f"menu_screen returned {len(body)} bytes, expected {SCREEN_BYTES}")
        # The screens this suite already fetched, spooled for whoever reads
        # the run afterwards. It costs the device nothing and is the only
        # record of what was on screen when a check failed. The decode is
        # behind the guard because it is the only part of this that costs
        # anything when nothing is spooling.
        if screen_spool.enabled():
            # The raw payload is what "a different screen" means: bit 7 of a
            # character byte is how the selected row is marked, and that does
            # not survive into the text.
            screen_spool.publish(screen_spool.MENU, self._decode(body).lines,
                                 body, cols=SCREEN_WIDTH, key=body)
        return body

    def _menu_open(self) -> bool:
        return self._menu_screen_body() is not None

    def ensure_ready(self) -> None:
        self._open_menu()

    def _open_menu(self) -> None:
        if self._menu_open():
            self.enter_file_browser()
            return
        status, body = self._request("PUT", MENU_BUTTON_PATH)
        if status != 200:
            raise Failure(f"menu_button failed with HTTP {status}: {body[:160]!r}")
        deadline = time.monotonic() + SETTLE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if self._menu_open():
                self.enter_file_browser()
                return
            time.sleep(POLL_INTERVAL_SECONDS)
        raise Failure("the on-device menu did not open")

    def _in_file_browser(self) -> bool:
        """Whether the screen showing is the file browser.

        The browser puts the directory it is showing on the status row and
        nothing else does, so a leading "/" is what identifies it. True on
        every machine; the rest of this method only ever runs where it can be
        false.
        """
        rows = self._decode(self._body()).lines
        return rows[SCREEN_HEIGHT - 1].lstrip().startswith("/")

    def enter_file_browser(self) -> None:
        """Leave the menu showing the file browser, whatever it opened on.

        A C64 Ultimate does not put the file browser behind the menu button.
        The button opens a launcher whose entries are the browser, the online
        search and the settings screens, and it reopens wherever it was last
        left, which can be several levels into those settings. Every suite
        expects the browser, so the descent belongs here rather than in each
        of them.

        The other two machines open the browser directly and this returns at
        once. Measured on a C64 Ultimate: RUN/STOP on the launcher closes the
        whole menu, so the way back up is the Back key.

        The cursor is moved onto the entry by reading which row it is on and
        which row the cursor is on, not by pressing Back further than the list
        is long. The launcher lists hardware actions, so a burst that
        under-delivers would leave RETURN to fire whichever of them the cursor
        stopped on.
        """
        entry = self.machine.launcher_browser_entry
        if entry is None:
            return
        for _ in range(LAUNCHER_DESCENT_STEPS):
            if self._in_file_browser():
                return
            cursor, rows = self.selection_and_rows(LAUNCHER_ENTRY_ROWS)
            row = next((n for n, text in enumerate(rows) if entry in text), None)
            if row is None:
                self.send_key("LEFT")
                continue
            if row != cursor:
                self.send_key_repeat("UP" if row < cursor else "DOWN",
                                     abs(row - cursor))
            landed, rows = self.selection_and_rows(LAUNCHER_ENTRY_ROWS)
            if landed != row:
                # A repaint between the two reads, or a key that did not
                # arrive. Neither is a reason to press RETURN on a launcher.
                continue
            self.send_key("ENTER")
        raise Failure(
            f"could not reach the file browser: no {entry!r} entry and no "
            f"directory on the status row after {LAUNCHER_DESCENT_STEPS} steps")

    def _close_menu(self) -> None:
        if not self._menu_open():
            return
        self._request("PUT", MENU_BUTTON_PATH)
        deadline = time.monotonic() + SETTLE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if not self._menu_open():
                return
            time.sleep(POLL_INTERVAL_SECONDS)
        # Best effort: run-tests' own ui_state gate recovers a dirty menu
        # before the next suite starts.

    # -- decode --
    def _decode(self, body: bytes) -> Snapshot:
        chars = body[:SCREEN_CELLS]
        lines = []
        reverse_cells: list[tuple[int, int]] = []
        for row in range(SCREEN_HEIGHT):
            cells = []
            for col in range(SCREEN_WIDTH):
                code = chars[row * SCREEN_WIDTH + col]
                base = code & 0x7F
                cells.append(chr(base) if 0x20 <= base <= 0x7E else MENU_GLYPHS.get(base, "?"))
                if code & 0x80:
                    reverse_cells.append((col, row))
            lines.append("".join(cells))
        return Snapshot(lines, reverse_cells, self.last_command)

    def _body(self) -> bytes:
        body = self._menu_screen_body()
        if body is None:
            raise Failure(f"menu screen unavailable after {self.last_command}")
        self._learn_cursor_colour(body, None)
        return body

    def _learn_cursor_colour(self, body: bytes,
                             entry_rows: Sequence[int] | None) -> None:
        """Take the machine's cursor colour from this screen, if it teaches one.

        Every screen the backend fetches is offered, not just the ones a
        caller asks a row of, because a caller can reach a screen that needs
        the colour without ever having asked for a row on one that teaches it.
        Measured on an Ultimate II+L: a suite that opened the Assembly 64 form
        from the task menu asked for its first row on the form itself, and a
        form marks a ten-cell field, which no rule can find without the colour.
        The task menu it passed through would have taught it.

        Learnt once and then kept. The colour scheme belongs to the machine
        (userinterface.cc effectuate_settings), not to the screen, and a later
        screen can answer confidently and wrongly: a disk image listing draws
        its volume row in one colour across the full width, which is unique
        among the rows on that screen, so re-learning there adopts the volume
        colour and every read afterwards returns the volume row. Measured on
        an Ultimate II+L showing a D64 with one program.
        """
        if self._cursor_colour is not None:
            return
        rows = entry_rows if entry_rows is not None else range(2, SCREEN_HEIGHT - 1)
        measured = measure_cursor_colour(body[:SCREEN_CELLS], body[SCREEN_CELLS:], rows)
        if measured is not None:
            self._cursor_colour = measured

    def _selected_row_from_body(self, body: bytes, entry_rows: Sequence[int] | None,
                                strict: bool = False) -> int:
        chars = body[:SCREEN_CELLS]
        colours = body[SCREEN_CELLS:]
        rows = entry_rows if entry_rows is not None else range(2, SCREEN_HEIGHT - 1)
        # Offered again with the caller's own row range, which is narrower and
        # more likely to hold exactly one listing than the default above.
        self._learn_cursor_colour(body, entry_rows)
        return find_selected_row_rest(chars, colours, rows, strict, self._cursor_colour)

    def _settled_selection(self, entry_rows: Sequence[int] | None) -> tuple[bytes, int]:
        """One screen and its cursor row, once a cursor is actually drawn.

        A repaint can leave the browser with no cursor marker for a moment.
        The foreground fallback cannot tell that state from a real selection,
        so it returns an arbitrary entry row, and a caller scanning a listing
        then never matches the entry it is looking for even though the entry is
        on screen. Re-read a few times before accepting the weaker signal.
        """
        for attempt in range(CURSOR_SETTLE_ATTEMPTS):
            body = self._body()
            try:
                return body, self._selected_row_from_body(body, entry_rows, strict=True)
            except NoCursorDrawn:
                if attempt + 1 == CURSOR_SETTLE_ATTEMPTS:
                    return body, self._selected_row_from_body(body, entry_rows)
                time.sleep(POLL_INTERVAL_SECONDS)
        raise Failure("unreachable: the settle loop always returns or raises")

    def capture(self) -> Snapshot:
        return self._decode(self._body())

    def selected_row(self, entry_rows: Sequence[int] | None = None) -> int:
        return self._settled_selection(entry_rows)[1]

    def selected_text(self, entry_rows: Sequence[int] | None = None) -> str:
        body, index = self._settled_selection(entry_rows)
        return strip_frame(self._decode(body).lines[index].rstrip())

    def selection_and_rows(
        self, entry_rows: Sequence[int] | None = None
    ) -> tuple[int, list[str]]:
        body, index = self._settled_selection(entry_rows)
        return index, [line.rstrip() for line in self._decode(body).lines]

    # -- input --
    def _post_events(self, events: list[dict]) -> None:
        for batch in api_lib.input_batches(events):
            status, body = self._request("POST", INPUT_PATH, payload={"events": batch})
            if status != 200:
                raise Failure(f"machine:input failed with HTTP {status}: {body[:160]!r}")

    def _settle(self, before: bytes | None,
                change_timeout: float | None = None,
                min_drain: float = 0.0) -> Snapshot:
        # A batch is accepted by REST immediately but drains through the C64
        # matrix over time (see tests/e2e/lib/menu.py), so the first poll or
        # two can land before the firmware has started applying it -- reading
        # the still-unchanged "before" screen as already "stable" and
        # returning before the keypress took visible effect. Waiting for a
        # change first (best-effort: a genuine no-op keypress never changes
        # the screen, so this can legitimately time out) avoids that false
        # settle; wait_screen_settled below still catches multi-frame
        # redraws once a change has started.
        #
        # The two waits have different jobs and so different budgets. This one
        # only has to cover the delay before the first changed pixel, so it
        # uses the much shorter KEY_CHANGE_TIMEOUT_SECONDS: it is the wait that
        # runs to full length on every keypress that cannot do anything, such
        # as DOWN on the last row of a listing. Sharing the settle timeout made
        # each of those cost 6 seconds.
        if change_timeout is None:
            change_timeout = pacing.KEY_CHANGE_TIMEOUT_SECONDS
        started = time.monotonic()
        self.last_key_changed, body = wait_screen_changes(
            self._menu_screen_body, before, timeout=change_timeout,
            min_samples=pacing.KEY_CHANGE_MIN_SAMPLES,
            hard_timeout=SETTLE_TIMEOUT_SECONDS)
        _, body = wait_screen_settled(self._menu_screen_body,
                                      timeout=SETTLE_TIMEOUT_SECONDS, known=body)
        # A batch is still draining through the matrix after the screen has
        # gone quiet once: a gap between two of its keystrokes looks exactly
        # like the end of it. Give the rest of the batch the time it needs to
        # arrive, then settle whatever did.
        remaining = min_drain - (time.monotonic() - started)
        if remaining > 0:
            time.sleep(remaining)
            _, body = wait_screen_settled(self._menu_screen_body,
                                          timeout=SETTLE_TIMEOUT_SECONDS)
        # The screen the settle stopped on is the screen this returns. Reading
        # it again would cost a further round trip to be told the same thing,
        # and the settle has just proved it is not changing. A menu that closed
        # under the caller leaves nothing to decode, which capture() reports as
        # the failure it is.
        if body is None:
            return self.capture()
        self._learn_cursor_colour(body, None)
        return self._decode(body)

    def send_combo(self, matrix_keys: Sequence[str]) -> Snapshot:
        before = self._menu_screen_body()
        self._post_events([{"kind": "keyboard", "inputs": list(matrix_keys), "transition": "tap"}])
        return self._settle(before)

    def send_key(self, key: str, *, settle: bool = False,
                 expect_redraw: bool = True) -> Snapshot:
        # `settle` and `expect_redraw` are accepted for interface parity with
        # TelnetBackend only; see send_char below.
        combo = KEY_ALIASES.get(key)
        if combo is None:
            raise Failure(f"Unknown key alias {key!r} for RestBackend")
        self.last_command = key
        return self.send_combo(combo)

    def send_char(self, ch: str, *, settle: bool = False,
                  expect_redraw: bool = True) -> Snapshot:
        # REST reads are a fixed-size settle regardless of what changed, so
        # neither the two-burst race `settle` guards against nor the
        # first-byte wait `expect_redraw` avoids applies here; both are
        # accepted for interface parity with TelnetBackend only.
        self.last_command = ch
        return self.send_combo(char_to_combo(ch))

    def send_text(self, text: str, label: str) -> Snapshot:
        self.last_command = label
        before = self._menu_screen_body()
        events = [{"kind": "keyboard", "inputs": char_to_combo(ch), "transition": "tap"} for ch in text]
        self._post_events(events)
        return self._settle(before, min_drain=len(events) * self.key_drain_seconds)

    def send_key_repeat(self, key: str, count: int) -> Snapshot:
        combo = KEY_ALIASES.get(key)
        if combo is None:
            raise Failure(f"Unknown key alias {key!r} for RestBackend")
        self.last_command = f"{key} x{count}"
        before = self._menu_screen_body()
        events = [{"kind": "keyboard", "inputs": list(combo), "transition": "tap"} for _ in range(count)]
        self._post_events(events)
        return self._settle(before, min_drain=count * self.key_drain_seconds)

    clear_field_key = "CLEAR"

    def send_key_sequence(self, keys: Sequence[str], label: str) -> Snapshot:
        if not keys:
            return self.capture()
        events = []
        for key in keys:
            combo = KEY_ALIASES.get(key)
            if combo is None:
                raise Failure(f"Unknown key alias {key!r} for RestBackend")
            events.append({"kind": "keyboard", "inputs": list(combo),
                           "transition": "tap"})
        self.last_command = label
        before = self._menu_screen_body()
        self._post_events(events)
        return self._settle(before, min_drain=len(events) * self.key_drain_seconds)

    def send_key_then_text(self, key: str, text: str, label: str) -> Snapshot:
        combo = KEY_ALIASES.get(key)
        if combo is None:
            raise Failure(f"Unknown key alias {key!r} for RestBackend")
        self.last_command = label
        before = self._menu_screen_body()
        events = [{"kind": "keyboard", "inputs": list(combo), "transition": "tap"}]
        events += [{"kind": "keyboard", "inputs": char_to_combo(ch), "transition": "tap"}
                   for ch in text]
        self._post_events(events)
        # The seek's own short budget: its caller confirms the result by
        # reading the cursor back, so an early "nothing changed" here is free,
        # and a seek onto the entry already under the cursor changes nothing at
        # all. See pacing.SEEK_CHANGE_TIMEOUT_SECONDS.
        return self._settle(before, change_timeout=pacing.SEEK_CHANGE_TIMEOUT_SECONDS,
                            min_drain=len(events) * self.key_drain_seconds)

    def close(self) -> None:
        # Same rule on the way out as on the way in: the menu is closed before
        # the setting is put back, never while a session still owns the machine.
        # Teardown must not raise over the failure it is cleaning up after, so
        # a menu that will not close skips the restore rather than writing the
        # setting under an open one. What that leaves behind is the Interface
        # Type this session already set, which the next session reads as
        # current and does not toggle; run-tests' ui_state gate recovers the
        # menu itself before the next suite starts.
        closed = False
        try:
            self._close_menu()
            closed = not self._menu_open()
        except Failure:
            pass
        if closed and self._original_interface_type is not None:
            try:
                self.set_config(UI_STORE, UI_ITEM, self._original_interface_type)
            except Failure:
                pass
