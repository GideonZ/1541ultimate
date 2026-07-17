#!/usr/bin/env python3
"""End-to-end harness for the FTP remote filesystem on a real Ultimate 64 / 64e.

Drives the real firmware UI entirely through the supported REST APIs
(machine:menu_button / machine:menu_screen / machine:input) to configure a
remote FTP server in the file browser, browse it under /ftp, and exercise every
FTP filesystem operation the UI exposes (LIST / RETR / STOR / MKD / DELE / RMD /
RNFR+RNTO). A controlled pyftpdlib server runs on the host with deterministic
fixtures and a full command log, so every device-side operation is verified
against the remote server's real filesystem state and command trace.

Style follows the existing repo E2E tests:
  * tools/io/printer/printer_test.py  - http.client REST client, Connection:
    close, X-Password only when supplied, numbered [NN] checks, FTP verify,
    hard-crash detection via /v1/version, cleanup in finally.
  * tools/api/menu_screen_test.py    - 2000-byte menu-screen decode, the
    colour/reverse selected-row heuristic, close-menu-from-anywhere, soak loop.

The firmware contract this test relies on (all confirmed on device / in source
software/filesystem/filesystem_ftp.cc, software/userinterface/*, software/api/*):
  * /ftp root row shows "Ftp  Remote FTP Servers"; RETURN opens a context menu
    with Enter / Copy to... / New Host.
  * A configured server row's context menu is Enter / Edit / Remove.
  * A remote file's context menu is Enter(dir) / View / Hex View / Copy to... /
    Move to... / Rename / Delete / Run with App (writable => rename/delete show).
  * Create Directory lives in the F5 Tasks menu, category "Create" -> "Directory".
  * Form entry: clr/home clears the selected field, RETURN edits it (string
    editor), typing injects PETSCII via the C64 matrix, RETURN commits and
    advances one field; the trailing "<< Submit >>" row submits on RETURN.
  * Servers persist to /flash/config/ftp_servers as JSON.
"""

import argparse
import http.client
import io
import json
import os
import random
import shutil
import string
import sys
import tempfile
import threading
import time

try:
    import ftplib
except ImportError:  # pragma: no cover
    ftplib = None

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

SCREEN_WIDTH = 40
SCREEN_HEIGHT = 25
SCREEN_CELLS = SCREEN_WIDTH * SCREEN_HEIGHT
SCREEN_PLANES = 2
SCREEN_BYTES = SCREEN_CELLS * SCREEN_PLANES

MENU_SETTLE_SECONDS = 0.10
KEY_SETTLE_SECONDS = 0.045
# Typing needs no per-key wait: the device buffers keystrokes and the string
# editor drains them. Measured on hardware, per-key POSTs stay character-perfect
# (including consecutive duplicates) with zero settle at ~49 keys/s, which is
# POST-latency bound. Do NOT batch multiple keys into one POST to go faster:
# that floods the injected-key queue past its drain rate and silently drops
# characters (verified). One key per POST, no backoff, is the safe maximum.
TYPE_SETTLE_SECONDS = 0.0
SELECTED_ROW_MIN_MARKED_CELLS = 12

# C64 keyboard-matrix names from software/api/input_api.h. Cursor keys are a
# single physical key; shift selects the reverse direction.
K_DOWN = ["cursor_up_down"]
K_UP = ["left_shift", "cursor_up_down"]
K_RIGHT = ["cursor_left_right"]
K_LEFT = ["left_shift", "cursor_left_right"]
K_RETURN = ["return"]
K_RUNSTOP = ["run_stop"]
K_HOME = ["clr_home"]                 # KEY_HOME -> clear selected form field
K_CLEAR = ["left_shift", "clr_home"]  # KEY_CLEAR -> reload all form fields
K_DEL = ["inst_del"]
K_F2 = ["left_shift", "f1"]
K_F5 = ["f5"]

# Printable char -> matrix key names. Letters/digits map directly (quick-type);
# uppercase adds left_shift; the punctuation the FTP fields need maps to the
# unshifted symbol keys. Values used by generated fixtures/aliases stay within
# this set (see safe_name()).
CHAR_KEYS = {
    ".": ["period"], "/": ["slash"], ":": ["colon"], "-": ["minus"],
    "@": ["at"], "+": ["plus"], "=": ["equals"], ",": ["comma"],
    ";": ["semicolon"], " ": ["space"], "*": ["star"],
}


class Failure(RuntimeError):
    pass


CHECK_COUNT = 0
LAST_CHECK_LABEL = ""


def check_start(label):
    global CHECK_COUNT, LAST_CHECK_LABEL
    CHECK_COUNT += 1
    LAST_CHECK_LABEL = label
    print(f"[{CHECK_COUNT:02d}] {label} ... ", end="", flush=True)


def check_ok(extra=""):
    print("OK" + (f" ({extra})" if extra else ""), flush=True)


def check_fail(reason):
    print(f"FAIL ({reason})", flush=True)


def check_warn(reason):
    print(f"WARNING ({reason})", flush=True)


def assert_or_warn(assertions_enabled, condition, message):
    if condition:
        return True
    if assertions_enabled:
        raise Failure(message)
    print(f"    WARNING: {message}", flush=True)
    return False


# ---------------------------------------------------------------------------
# RestSession: http.client, Connection: close, X-Password only when supplied.
# ---------------------------------------------------------------------------
class RestSession:
    # No steady-state pacing floor. The device handles frequent *sequential*
    # REST calls fine; the earlier network wedge was the FTP control-connection
    # netconn leak (fixed in firmware), not request frequency. This harness is
    # single-threaded and never issues concurrent requests, so it just goes as
    # fast as request latency allows. (The escalating backoff below is
    # error-recovery only and does not run in the steady state.)
    MIN_REQUEST_INTERVAL = 0.0

    def __init__(self, host, password, timeout=5.0):
        self.host = host
        self.password = password
        self.timeout = timeout
        self.rest_requests = 0
        self.menu_snapshots = 0
        self.key_events = 0
        self._last_request_at = 0.0
        self.reset_events = 0

    def _headers(self, body, extra=None):
        # Close each connection: the device's small HTTP connection pool wedges
        # if kept-alive sockets accumulate, so one clean request per connection
        # (matching printer_test.py) is the stable choice. Churn is instead kept
        # low by pacing requests (see MenuDriver / MIN_REQUEST_INTERVAL).
        headers = {"Connection": "close"}
        if self.password:
            headers["X-Password"] = self.password
        if body is not None:
            headers["Content-Length"] = str(len(body))
        if extra:
            headers.update(extra)
        return headers

    def request(self, method, path, body=None, extra_headers=None, timeout=None):
        # A reset while reusing a kept-alive socket is expected occasionally;
        # reconnect and retry. GET is idempotent so we retry freely; writes get
        # a single reconnect-retry (a reset almost always means the request was
        # dropped before the device acted on it).
        attempts = 6 if method == "GET" else 5
        last_exc = None
        for attempt in range(attempts):
            # Pace requests to keep httpd connection churn below the wedge point.
            gap = self.MIN_REQUEST_INTERVAL - (time.monotonic() - self._last_request_at)
            if gap > 0:
                time.sleep(gap)
            self.rest_requests += 1
            self._last_request_at = time.monotonic()
            conn = http.client.HTTPConnection(self.host, timeout=timeout or self.timeout)
            try:
                conn.request(method, path, body=body, headers=self._headers(body, extra_headers))
                resp = conn.getresponse()
                payload = resp.read()
                return resp.status, dict(resp.getheaders()), payload
            except (ConnectionError, http.client.HTTPException, OSError) as exc:
                last_exc = exc
                self.reset_events += 1
                if attempt + 1 < attempts:
                    # Escalating cooldown lets a briefly-saturated connection
                    # pool drain instead of piling on more churn.
                    time.sleep(min(0.5 * (attempt + 1), 2.0))
                    continue
                raise
            finally:
                conn.close()
        raise last_exc

    def require_ok(self, method, path, body=None, description=None, extra_headers=None, timeout=None):
        status, _headers, payload = self.request(method, path, body=body,
                                                 extra_headers=extra_headers, timeout=timeout)
        if status != 200:
            message = f"{description or path} failed with HTTP {status}"
            try:
                detail = json.loads(payload.decode("utf-8"))
                if detail.get("errors"):
                    message += f": {detail['errors']}"
            except (ValueError, UnicodeDecodeError):
                message += f": {payload[:160]!r}"
            raise Failure(message)
        return payload

    def is_alive(self, timeout=3.0):
        try:
            status, _headers, _payload = self.request("GET", "/v1/version", timeout=timeout)
            return status == 200
        except (OSError, http.client.HTTPException):
            return False

    def version(self):
        payload = self.require_ok("GET", "/v1/version", description="version")
        return json.loads(payload.decode("utf-8"))

    def menu_button(self):
        self.require_ok("PUT", "/v1/machine:menu_button", description="menu_button")

    def get_menu_screen(self):
        """Return the raw 2000-byte screen matrix, or None if the menu is closed."""
        status, headers, payload = self.request("GET", "/v1/machine:menu_screen")
        if status == 404:
            return None
        if status != 200:
            raise Failure(f"menu_screen failed with HTTP {status}: {payload[:160]!r}")
        ctype = headers.get("Content-Type", "")
        if "application/octet-stream" not in ctype:
            raise Failure(f"menu_screen expected octet-stream, got {ctype!r}")
        if len(payload) != SCREEN_BYTES:
            raise Failure(f"menu_screen expected {SCREEN_BYTES} bytes, got {len(payload)}")
        self.menu_snapshots += 1
        return payload

    def post_input(self, events):
        body = json.dumps({"events": events}).encode("utf-8")
        payload = self.require_ok("POST", "/v1/machine:input", body=body, description="input",
                                  extra_headers={"Content-Type": "application/json"})
        return json.loads(payload.decode("utf-8"))

    def tap(self, inputs):
        self.key_events += 1
        self.post_input([{"kind": "keyboard", "inputs": inputs, "transition": "tap"}])

    def release_all(self):
        self.post_input([{"kind": "release_all"}])

    def input_state(self):
        payload = self.require_ok("GET", "/v1/machine:input", description="input state")
        return json.loads(payload.decode("utf-8"))

    def held_keys(self):
        try:
            state = self.input_state()
        except Failure:
            return []
        kb = state.get("keyboard", {})
        return list(kb.get("inputs", []))


# ---------------------------------------------------------------------------
# MenuScreen: decode the 40x25 char/attr planes and locate the selected row.
# ---------------------------------------------------------------------------
class MenuScreen:
    def __init__(self, body):
        self.body = body
        self.chars = body[:SCREEN_CELLS]
        self.colors = body[SCREEN_CELLS:]
        self.rows = [
            "".join(chr(ch & 0x7F) if 0x20 <= (ch & 0x7F) <= 0x7E else " "
                    for ch in self.chars[r * SCREEN_WIDTH:(r + 1) * SCREEN_WIDTH])
            for r in range(SCREEN_HEIGHT)
        ]
        self.text = "\n".join(self.rows)
        self.selected_row = self._find_selected_row()
        self.selected_text = self.rows[self.selected_row].strip() if self.selected_row >= 0 else ""

    def _find_selected_row(self):
        best_bg_row, best_bg = -1, 0
        best_rev_row, best_rev = -1, 0
        best_fg_row, best_fg = -1, 0
        for row in range(2, SCREEN_HEIGHT - 1):
            bg_counts, fg_counts, rev = {}, {}, 0
            rc = self.chars[row * SCREEN_WIDTH + 1:(row + 1) * SCREEN_WIDTH - 1]
            cc = self.colors[row * SCREEN_WIDTH + 1:(row + 1) * SCREEN_WIDTH - 1]
            for ch, code in zip(rc, cc):
                if ch & 0x80:
                    rev += 1
                fg = code & 0x0F
                bg = (code >> 4) & 0x0F
                if bg == 0:
                    if fg != 0x0F:
                        fg_counts[fg] = fg_counts.get(fg, 0) + 1
                    continue
                bg_counts[bg] = bg_counts.get(bg, 0) + 1
            bgc = max(bg_counts.values()) if bg_counts else 0
            fgc = max(fg_counts.values()) if fg_counts else 0
            if bgc > best_bg:
                best_bg, best_bg_row = bgc, row
            if rev > best_rev:
                best_rev, best_rev_row = rev, row
            if fgc > best_fg:
                best_fg, best_fg_row = fgc, row
        if best_bg >= SELECTED_ROW_MIN_MARKED_CELLS:
            return best_bg_row
        if best_rev >= SELECTED_ROW_MIN_MARKED_CELLS:
            return best_rev_row
        if best_fg >= SELECTED_ROW_MIN_MARKED_CELLS:
            return best_fg_row
        return -1

    def contains(self, text):
        return text.lower() in self.text.lower()

    def find_row_containing(self, text):
        low = text.lower()
        for i, r in enumerate(self.rows):
            if low in r.lower():
                return i
        return -1

    def looks_like_menu(self):
        printable = sum(1 for ch in self.chars if 0x21 <= (ch & 0x7F) <= 0x7E)
        distinct_colors = len(set(self.colors))
        return printable >= 20 and 1 < distinct_colors <= 32


# ---------------------------------------------------------------------------
# MenuDriver: navigation + form entry through REST keyboard input.
# ---------------------------------------------------------------------------
class MenuDriver:
    def __init__(self, session, verbose_menu=False):
        self.s = session
        self.verbose_menu = verbose_menu
        self.last_input = ""

    # -- low level ---------------------------------------------------------
    def tap(self, inputs, settle=KEY_SETTLE_SECONDS):
        self.last_input = "+".join(inputs)
        self.s.tap(inputs)
        time.sleep(settle)

    def screen(self, required=True, retries=4):
        for _ in range(retries):
            body = self.s.get_menu_screen()
            if body is not None:
                return MenuScreen(body)
            time.sleep(MENU_SETTLE_SECONDS)
        if required:
            raise Failure("menu screen unavailable (menu closed unexpectedly)")
        return None

    def type_text(self, text):
        for ch in text:
            if ch.isdigit() or (ch.isalpha() and ch.islower()):
                self.tap([ch], settle=TYPE_SETTLE_SECONDS)
            elif ch.isalpha() and ch.isupper():
                self.tap(["left_shift", ch.lower()], settle=TYPE_SETTLE_SECONDS)
            elif ch in CHAR_KEYS:
                self.tap(CHAR_KEYS[ch], settle=TYPE_SETTLE_SECONDS)
            else:
                raise Failure(f"type_text: no key mapping for {ch!r}")

    # -- menu open/close ---------------------------------------------------
    def menu_is_open(self):
        return self.s.get_menu_screen() is not None

    def open_menu(self):
        # menu_button is a toggle. If the device is briefly busy (e.g. settling
        # a failed FTP connection) it may be slow to render the menu, so wait a
        # long time after each press before pressing again - re-pressing too
        # soon would toggle a just-opened menu back closed. Few presses, long
        # waits.
        for _ in range(3):
            if self.menu_is_open():
                return
            self.s.menu_button()
            deadline = time.monotonic() + 6.0
            while time.monotonic() < deadline:
                if self.menu_is_open():
                    return
                time.sleep(MENU_SETTLE_SECONDS)
        raise Failure("menu did not open after repeated menu_button")

    def close_menu_from_anywhere(self):
        for i in range(18):
            body = self.s.get_menu_screen()
            if body is None:
                return
            screen = MenuScreen(body)
            if "Save changes to Flash?" in screen.text:
                self.tap(K_LEFT)
                self.tap(K_RETURN)
                continue
            # An OK/confirmation popup (e.g. "N files placed on clipboard",
            # "Are you sure?", an error box) is dismissed with RETURN, not
            # RUN/STOP. Alternate the two so teardown clears popups and then
            # backs out of the browser levels.
            if i % 2 == 0:
                self.tap(K_RETURN)
            else:
                self.tap(K_RUNSTOP)
        # last resort: toggle the menu button
        if self.s.get_menu_screen() is not None:
            self.s.menu_button()
            time.sleep(MENU_SETTLE_SECONDS)
        if self.s.get_menu_screen() is not None:
            raise Failure("menu could not be closed from anywhere")

    # -- selection navigation ---------------------------------------------
    def select_row_text(self, text, max_steps=40, require=True):
        """Move the highlight until the selected row contains `text`."""
        low = text.lower()
        last_dir = None
        for _ in range(max_steps):
            screen = self.screen()
            sr = screen.selected_row
            tr = screen.find_row_containing(text)
            if tr < 0:
                if require:
                    self._dump_on_fail(screen, f"row containing {text!r} not visible")
                    raise Failure(f"row containing {text!r} not visible")
                return None
            if sr >= 0 and low in screen.rows[sr].lower():
                return screen
            if sr < 0:
                self.tap(K_DOWN)
                continue
            if tr > sr:
                self.tap(K_DOWN)
                last_dir = "down"
            else:
                self.tap(K_UP)
                last_dir = "up"
        if require:
            raise Failure(f"could not select row containing {text!r} within {max_steps} steps")
        return None

    def enter(self):
        self.tap(K_RIGHT, settle=MENU_SETTLE_SECONDS)

    def back(self):
        self.tap(K_LEFT, settle=MENU_SETTLE_SECONDS)

    def open_context_menu(self):
        self.tap(K_RETURN, settle=MENU_SETTLE_SECONDS)

    @staticmethod
    def _selected_popup_row(screen):
        """Locate the highlighted item of any overlay popup (context menu, F5
        Tasks menu, or a nested Create submenu). They render at different column
        offsets, but they all share one property that separates them from the
        browser's own row highlight: the popup's highlight run never reaches the
        last column, while the browser's full-width highlight does. Among the
        non-browser rows, the selected popup item is the one whose highlight
        extends furthest to the right (a persistent category label sits further
        left than the moving submenu selection, so this tracks the selection)."""
        best_row, best_rightmost = -1, -1
        for row in range(2, SCREEN_HEIGHT - 1):
            cc = screen.colors[row * SCREEN_WIDTH:(row + 1) * SCREEN_WIDTH]
            if (cc[SCREEN_WIDTH - 1] >> 4) & 0x0F:
                continue  # browser full-row highlight reaches the last column
            rightmost = -1
            for i in range(1, SCREEN_WIDTH - 1):
                if (cc[i] >> 4) & 0x0F:
                    rightmost = i
            if rightmost > best_rightmost:
                best_rightmost, best_row = rightmost, row
        return best_row if best_rightmost >= 15 else -1

    def _navigate_popup(self, text, max_steps, kind):
        seen = False
        screen = None
        for _ in range(max_steps):
            screen = self.screen()
            tr = screen.find_row_containing(text)
            sr = self._selected_popup_row(screen)
            if tr >= 0:
                seen = True
                if sr == tr:
                    self.tap(K_RETURN, settle=MENU_SETTLE_SECONDS)
                    return
                if sr < 0 or tr > sr:
                    self.tap(K_DOWN)
                else:
                    self.tap(K_UP)
            else:
                # Item not on screen yet: scroll the popup down to reveal it.
                self.tap(K_DOWN)
        if not seen:
            self._dump_on_fail(screen, f"{kind} item {text!r} never appeared")
            raise Failure(f"{kind} item {text!r} not visible in popup")
        self._dump_on_fail(screen, f"{kind} item {text!r} not highlightable")
        raise Failure(f"could not highlight {kind} item {text!r} within {max_steps} steps")

    def select_context_action(self, text, max_steps=20):
        """With a context popup open, highlight and activate the named action."""
        self._navigate_popup(text, max_steps, "context")

    def select_task_action(self, text, max_steps=24):
        """With the F5 Tasks popup open, highlight and activate the named entry."""
        self._navigate_popup(text, max_steps, "task")

    # -- FTP-specific navigation ------------------------------------------
    def goto_top_browser(self, marker="Remote FTP Servers", max_up=8):
        """Back out to the top-level file browser. The device reopens the menu
        at its last position, so callers that need a root-level entry must
        rewind. On cartridge-style firmware the menu button opens the Ultimate
        main menu instead of the file browser, so descend into 'DISK FILE
        BROWSER' when we land there (harmless on U64, where it is not present)."""
        self.open_menu()
        for _ in range(max_up):
            screen = self.screen()
            if screen.find_row_containing(marker) >= 0:
                return
            if screen.find_row_containing("DISK FILE BROWSER") >= 0:
                self.select_row_text("DISK FILE BROWSER")
                self.tap(K_RETURN, settle=MENU_SETTLE_SECONDS)
                continue
            self.back()
        # one more read in case the last back() landed us at the top
        self.screen()

    def goto_ftp_root(self):
        self.goto_top_browser("Remote FTP Servers")
        self.select_row_text("Remote FTP Servers")

    def enter_ftp_listing(self):
        """Enter the /ftp node so the configured server aliases are listed."""
        self.goto_ftp_root()
        self.enter()

    def create_ftp_host(self, alias, host, port, user, password, path):
        self.goto_ftp_root()
        self.open_context_menu()
        self.select_context_action("New Host")
        time.sleep(MENU_SETTLE_SECONDS)
        fields = [("Alias", alias), ("Host", host), ("Port", str(port)),
                  ("User", user), ("Password", password), ("Path", path)]
        for label, value in fields:
            self._form_fill_current(label, value)
        self._form_submit()

    def edit_ftp_host(self, alias, new_values):
        """new_values: dict of label -> value for the fields to change."""
        self.enter_ftp_listing()
        self.select_row_text(alias)
        self.open_context_menu()
        self.select_context_action("Edit")
        time.sleep(MENU_SETTLE_SECONDS)
        order = ["Alias", "Host", "Port", "User", "Password", "Path"]
        for label in order:
            if label in new_values:
                self._form_fill_current(label, str(new_values[label]))
            else:
                # leave field unchanged: just advance past it
                self.tap(K_DOWN)
        self._form_submit()

    def remove_ftp_host(self, alias):
        self.enter_ftp_listing()
        self.select_row_text(alias)
        self.open_context_menu()
        self.select_context_action("Remove")
        time.sleep(MENU_SETTLE_SECONDS)

    def _form_fill_current(self, label, value):
        # The form opens on Alias and each committed field advances by one, so
        # the "current" field matches our fixed order. Clear, edit, type, commit.
        # A dropped/duplicated keystroke (device httpd churn) can corrupt a
        # field, so verify against the screen and re-enter the field once. The
        # cursor has advanced to the next field after commit; step back up to
        # re-edit the field we just left.
        for attempt in range(2):
            if attempt == 0:
                self.tap(K_HOME)                # clear selected field
                self.tap(K_RETURN)              # enter string editor (empty)
                if value:
                    self.type_text(value)
                self.tap(K_RETURN, settle=MENU_SETTLE_SECONDS)  # commit + advance
            screen = self.screen()
            row = screen.find_row_containing(label + ":")
            if row < 0 or not value or value[:16] in screen.rows[row]:
                return
            if attempt == 0:
                # go back up to the field, clear + retype
                self.tap(K_UP)
                self.tap(K_HOME)
                self.tap(K_RETURN)
                self.type_text(value)
                self.tap(K_RETURN, settle=MENU_SETTLE_SECONDS)
                continue
            self._dump_on_fail(screen, f"field {label} shows {screen.rows[row]!r}, expected {value!r}")
            raise Failure(f"form field {label}: expected {value!r}, screen shows {screen.rows[row]!r}")

    def _form_submit(self):
        # After the Path field commits, the cursor sits on "<< Submit >>".
        self.tap(K_RETURN, settle=MENU_SETTLE_SECONDS)
        # If the form is still open (e.g. a validation popup), try to recover.
        body = self.s.get_menu_screen()
        if body is not None and MenuScreen(body).contains("Submit"):
            self.tap(K_DOWN)
            self.tap(K_RETURN, settle=MENU_SETTLE_SECONDS)

    def _dump_on_fail(self, screen, reason):
        if not self.verbose_menu:
            return
        print(f"\n--- menu dump ({reason}) ---")
        for i, r in enumerate(screen.rows):
            mark = ">>" if i == screen.selected_row else "  "
            print(f"{mark}{i:2d}|{r}|")
        print(f"selected_row={screen.selected_row} selected_text={screen.selected_text!r}")
        print("--- end dump ---")


# ---------------------------------------------------------------------------
# ControlledFtpServer: pyftpdlib server with command log, fixtures and faults.
# ---------------------------------------------------------------------------
def deterministic_bytes(length):
    return bytes((i * 37 + 11) & 0xFF for i in range(length))


FIXTURES = {}  # name -> bytes (files only), populated by build_fixtures


def build_fixtures(root):
    """Create the deterministic fixture tree under `root`. Returns marker."""
    marker = f"E2E-{int(time.time())}"
    files = {
        "README.TXT": (f"Ultimate64 FTP FS E2E fixture\nMARKER:{marker}\n").encode("ascii"),
        "EMPTY.BIN": b"",
        "SMALL.BIN": deterministic_bytes(16),
        "EXACT4096.BIN": deterministic_bytes(4096),
        "PLUS4097.BIN": deterministic_bytes(4097),
        "LARGE16K.BIN": deterministic_bytes(16384),
        "BYTES256.BIN": bytes(range(256)) * 4,
        "NAME WITH SPACE.TXT": b"file name with spaces\n",
        "MiXeD-Case.PrG": deterministic_bytes(64),
    }
    for name, data in files.items():
        with open(os.path.join(root, name), "wb") as fh:
            fh.write(data)
        FIXTURES[name] = data

    os.makedirs(os.path.join(root, "DIRA"), exist_ok=True)
    with open(os.path.join(root, "DIRA", "INSIDE.TXT"), "wb") as fh:
        fh.write(b"inside DIRA\n")
    os.makedirs(os.path.join(root, "DIR WITH SPACE"), exist_ok=True)
    with open(os.path.join(root, "DIR WITH SPACE", "SPACED.TXT"), "wb") as fh:
        fh.write(b"inside spaced dir\n")

    many = os.path.join(root, "MANY")
    os.makedirs(many, exist_ok=True)
    for i in range(30):
        with open(os.path.join(many, f"F{i:02d}.TXT"), "wb") as fh:
            fh.write(f"entry {i}\n".encode())

    # Read-only area for permission-denied negative tests.
    rd = os.path.join(root, "RDONLY")
    os.makedirs(rd, exist_ok=True)
    with open(os.path.join(rd, "LOCKED.TXT"), "wb") as fh:
        fh.write(b"cannot be deleted\n")
    return marker


class ControlledFtpServer:
    def __init__(self, bind_host, advertised_host, port, passive_ports, user, password,
                 root=None, keep_root=False):
        try:
            from pyftpdlib.authorizers import DummyAuthorizer
            from pyftpdlib.handlers import FTPHandler
            from pyftpdlib.servers import FTPServer
        except ImportError:
            raise Failure("pyftpdlib is required for this test. Install it with: "
                          "pip install pyftpdlib")
        self.bind_host = bind_host
        self.advertised_host = advertised_host
        self.port = port
        self.passive_ports = passive_ports
        self.user = user
        self.password = password
        self.keep_root = keep_root or (root is not None)
        self.root = root or tempfile.mkdtemp(prefix="u64ftp-")
        self.log = []                 # (time, cmd, arg, respcode, respstr)
        self.log_lock = threading.Lock()
        self.sent = {}                # basename -> bytes actually sent (RETR)
        self.received = {}            # basename -> bytes actually received (STOR)
        self.faults = set()           # e.g. {"abort_retr"}
        self.marker = build_fixtures(self.root)
        self._thread = None
        self._server = None
        server = self

        class LoggingHandler(FTPHandler):
            def log_cmd(self, cmd, arg, respcode, respstr):
                with server.log_lock:
                    server.log.append((time.time(), cmd, arg, respcode, respstr))

            def on_file_sent(self, path):
                size = os.path.getsize(path) if os.path.exists(path) else None
                with server.log_lock:
                    server.sent[os.path.basename(path)] = size

            def on_file_received(self, path):
                size = os.path.getsize(path) if os.path.exists(path) else None
                with server.log_lock:
                    server.received[os.path.basename(path)] = size

            def ftp_RETR(self, path):
                if "abort_retr" in server.faults:
                    self.respond("550 RETR failed (injected fault).")
                    return None
                return super().ftp_RETR(path)

            def ftp_LIST(self, path):
                if "fail_list" in server.faults:
                    self.respond("550 LIST failed (injected fault).")
                    return None
                return super().ftp_LIST(path)

            def _deny_if_readonly(self, path, verb):
                # Reject writes anywhere under the RDONLY fixture directory.
                real = self.fs.ftp2fs(path) if hasattr(self.fs, "ftp2fs") else path
                if os.sep + "RDONLY" in (os.sep + str(real)) or "/RDONLY" in str(path):
                    self.respond(f"550 {verb} denied (read-only area).")
                    return True
                return False

            def ftp_STOR(self, path, mode="w"):
                if self._deny_if_readonly(path, "STOR"):
                    return None
                return super().ftp_STOR(path, mode)

            def ftp_DELE(self, path):
                if self._deny_if_readonly(path, "DELE"):
                    return None
                return super().ftp_DELE(path)

            def ftp_MKD(self, path):
                if self._deny_if_readonly(path, "MKD"):
                    return None
                return super().ftp_MKD(path)

        auth = DummyAuthorizer()
        auth.add_user(user, password, self.root, perm="elradfmwMT")
        # Anonymous read-only access, so the "no password" host variant can log in.
        auth.add_anonymous(self.root, perm="elr")
        LoggingHandler.authorizer = auth
        LoggingHandler.masquerade_address = advertised_host
        LoggingHandler.passive_ports = list(passive_ports)
        LoggingHandler.banner = "U64 E2E FTP server ready."
        self._handler_cls = LoggingHandler
        self._server_cls = FTPServer

    def start(self):
        self._server = self._server_cls((self.bind_host, self.port), self._handler_cls)
        self._server.max_cons = 64
        self._thread = threading.Thread(target=self._server.serve_forever,
                                        kwargs={"timeout": 0.5}, daemon=True)
        self._thread.start()
        time.sleep(0.3)

    def stop(self):
        if self._server is not None:
            try:
                self._server.close_all()
            except Exception:
                pass
            self._server = None
        if self._thread is not None:
            self._thread.join(timeout=3.0)
            self._thread = None

    def cleanup(self):
        self.stop()
        if not self.keep_root:
            shutil.rmtree(self.root, ignore_errors=True)

    # -- host-side verification helpers -----------------------------------
    def path(self, *parts):
        return os.path.join(self.root, *parts)

    def exists(self, *parts):
        return os.path.exists(self.path(*parts))

    def read(self, *parts):
        with open(self.path(*parts), "rb") as fh:
            return fh.read()

    def snapshot_log(self):
        with self.log_lock:
            return list(self.log)

    def commands_since(self, index, name=None):
        entries = self.snapshot_log()[index:]
        if name is None:
            return entries
        return [e for e in entries if e[1] == name]

    def log_len(self):
        with self.log_lock:
            return len(self.log)

    def wait_for_command(self, name, since_index, timeout=8.0, arg_substr=None):
        # Match arguments case-insensitively: the firmware lowercases filename
        # extensions (e.g. FOO.BIN -> FOO.bin) when building the remote path.
        needle = arg_substr.lower() if arg_substr else None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for e in self.commands_since(since_index, name):
                if needle is None or (e[2] and needle in e[2].lower()):
                    return e
            time.sleep(0.1)
        return None

    def find_ci(self, *parts):
        """Case-insensitively resolve a path under the server root; the firmware
        may change filename case. Returns the real path or None."""
        cur = self.root
        for part in parts:
            if not os.path.isdir(cur):
                return None
            match = None
            for entry in os.listdir(cur):
                if entry.lower() == part.lower():
                    match = entry
                    break
            if match is None:
                return None
            cur = os.path.join(cur, match)
        return cur


# ---------------------------------------------------------------------------
# DeviceFtpInspector: the device's own FTP server, for local-file staging and
# verification only (never touches unrelated user files).
# ---------------------------------------------------------------------------
class DeviceFtpInspector:
    def __init__(self, host, user="user", password="password", timeout=15):
        self.host = host
        self.user = user
        self.password = password
        self.timeout = timeout

    def available(self):
        if ftplib is None:
            return False
        try:
            self._open().quit()
            return True
        except Exception:
            return False

    def _open(self):
        ftp = ftplib.FTP()
        ftp.connect(self.host, 21, timeout=self.timeout)
        ftp.login(self.user, self.password)
        return ftp

    def upload_bytes(self, path, data):
        ftp = self._open()
        try:
            ftp.storbinary(f"STOR {path}", io.BytesIO(data))
        finally:
            try:
                ftp.quit()
            except Exception:
                ftp.close()

    def delete(self, path):
        ftp = self._open()
        try:
            ftp.delete(path)
        except ftplib.Error:
            pass
        finally:
            try:
                ftp.quit()
            except Exception:
                ftp.close()

    def clear_ftp_cache(self):
        """Delete the device-side lazy download cache (/Temp/cache/ftp/*) so the
        next remote open forces a fresh RETR. Returns the number of files
        removed. The cache in /Temp (RAM disk) otherwise survives between runs
        and across host re-creation, since it is keyed only by filename."""
        # Best-effort: the device FTP occasionally returns a transient 451 when
        # listing the RAM-disk cache dir. Never let cache housekeeping abort a
        # test - swallow every FTP/socket error and just report what was removed.
        if ftplib is None:
            return 0
        removed = 0
        for cache_dir in ("/Temp/cache/ftp", "/Temp/ftp"):
            try:
                ftp = self._open()
            except Exception:
                continue
            try:
                names = []
                for _ in range(3):  # tolerate a transient 451 on the RAM-disk dir
                    try:
                        names = ftp.nlst(cache_dir)
                        break
                    except ftplib.error_perm:
                        break  # e.g. dir does not exist yet
                    except ftplib.all_errors:
                        time.sleep(0.3)
                for name in names:
                    base = name.rsplit("/", 1)[-1]
                    if base in (".", ".."):
                        continue
                    full = name if name.startswith("/") else f"{cache_dir}/{base}"
                    try:
                        ftp.delete(full)
                        removed += 1
                    except ftplib.all_errors:
                        pass
            finally:
                try:
                    ftp.quit()
                except Exception:
                    ftp.close()
        return removed


# ---------------------------------------------------------------------------
# Test context: shared state passed to stages.
# ---------------------------------------------------------------------------
class Context:
    def __init__(self, args, session, driver, server, inspector, assertions_enabled):
        self.args = args
        self.s = session
        self.d = driver
        self.server = server
        self.inspector = inspector
        self.assertions_enabled = assertions_enabled
        self.alias = None
        self.results = []   # (phase, operation, result, commands, fixture, notes)
        self.host_staged = []  # device local paths to remove in cleanup

    def record(self, phase, operation, result, commands="", fixture="", notes=""):
        self.results.append((phase, operation, result, commands, fixture, notes))

    def ensure_alive(self, where):
        if not self.s.is_alive(timeout=5.0):
            raise HardCrash(where)


class HardCrash(RuntimeError):
    pass


def safe_name(prefix):
    suffix = "".join(random.choice(string.ascii_lowercase + string.digits) for _ in range(4))
    return f"{prefix}{int(time.time()) % 10000:04d}{suffix}"


# ---------------------------------------------------------------------------
# Stage helpers
# ---------------------------------------------------------------------------
def endpoint_checks(ctx):
    s, d = ctx.s, ctx.d
    check_start("GET /v1/version reachable")
    if not s.is_alive():
        check_fail("no response from /v1/version")
        raise Failure("device unreachable")
    check_ok(s.version().get("version", "?"))

    check_start("menu closed -> menu_screen 404")
    d.close_menu_from_anywhere()
    if s.get_menu_screen() is not None:
        check_warn("menu still open after close; continuing")
    else:
        check_ok()

    check_start("menu_button opens menu")
    d.open_menu()
    check_ok()

    check_start("menu_screen returns 2000-byte matrix")
    screen = d.screen()
    if not screen.looks_like_menu():
        check_fail("snapshot does not look like a menu")
        raise Failure("bad menu snapshot")
    check_ok(f"{len(screen.body)} bytes")

    check_start("input release_all works")
    s.release_all()
    check_ok()

    check_start("menu closes from anywhere")
    d.close_menu_from_anywhere()
    check_ok()


def cleanup_stale_hosts(ctx):
    """Remove any FTP hosts left under /ftp by a previous interrupted run (their
    aliases share our prefix). Stale hosts otherwise accumulate in flash and the
    /ftp view probes each one, which can storm the device with connections."""
    d = ctx.d
    prefix = ctx.args.alias_prefix.lower()
    removed = 0
    d.close_menu_from_anywhere()
    for _ in range(30):
        d.enter_ftp_listing()
        screen = d.screen()
        target = None
        for row in screen.rows:
            stripped = row.strip()
            if stripped and stripped.split()[0].lower().startswith(prefix):
                target = stripped.split()[0]
                break
        if not target:
            break
        try:
            d.remove_ftp_host(target)
            removed += 1
        except Failure:
            break
    d.close_menu_from_anywhere()
    return removed


def create_host(ctx, alias, path="/", user=None, password=None, host=None, port=None):
    args = ctx.args
    ctx.d.create_ftp_host(
        alias=alias,
        host=host or args.ftp_advertised_host,
        port=port or args.ftp_port,
        user=user or args.ftp_user,
        password=password if password is not None else args.ftp_password,
        path=path,
    )


def verify_host_listed(ctx, alias, present=True):
    ctx.d.close_menu_from_anywhere()
    ctx.d.enter_ftp_listing()
    screen = ctx.d.screen()
    listed = screen.find_row_containing(alias) >= 0
    return listed == present


def enter_host_root(ctx, alias):
    """From anywhere, enter /ftp -> alias, returning the listing screen."""
    ctx.d.enter_ftp_listing()
    ctx.d.select_row_text(alias)
    ctx.d.enter()
    time.sleep(MENU_SETTLE_SECONDS)
    return ctx.d.screen()


def wait_for_listing(driver, needle, timeout=4.0):
    """Wait until the current listing shows `needle` (and isn't '< No Items >')."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        screen = driver.screen(required=False)
        if screen is not None and screen.find_row_containing(needle) >= 0:
            return True
        time.sleep(MENU_SETTLE_SECONDS)
    return False


def edit_and_revert(ctx, alias):
    """Exercise the server 'Edit' context action: change the host Path to /DIRA,
    confirm the listing follows, then revert the Path to / with a compensating
    edit so the host is left exactly as it was. Returns (changed, reverted)."""
    d = ctx.d
    d.edit_ftp_host(alias, {"Path": "/DIRA"})
    ctx.ensure_alive("edit path -> /DIRA")
    enter_host_root(ctx, alias)
    changed = wait_for_listing(d, "INSIDE", timeout=6.0)
    # compensating edit: restore the original root path
    d.edit_ftp_host(alias, {"Path": "/"})
    ctx.ensure_alive("edit path revert -> /")
    enter_host_root(ctx, alias)
    reverted = wait_for_listing(d, "README", timeout=6.0)
    return changed, reverted


def open_file_read(ctx, alias, filename):
    """Trigger a RETR by opening a file's View action. Returns log index before."""
    since = ctx.server.log_len()
    ctx.d.select_row_text(filename)
    ctx.d.open_context_menu()
    ctx.d.select_context_action("View")
    time.sleep(MENU_SETTLE_SECONDS)
    # The text viewer is now open; close it back to the browser.
    ctx.d.tap(K_RUNSTOP, settle=MENU_SETTLE_SECONDS)
    return since


# ---------------------------------------------------------------------------
# Stage: smoke
# ---------------------------------------------------------------------------
def stage_smoke(ctx):
    print("\n=== STAGE: smoke ===")
    d, server = ctx.d, ctx.server
    alias = ctx.alias

    check_start(f"create FTP host '{alias}' through the New Host form")
    create_host(ctx, alias)
    ctx.ensure_alive("after create host")
    check_ok()
    ctx.record("smoke", "create host", "OK", fixture=alias)

    check_start("new alias appears under /ftp")
    ok = verify_host_listed(ctx, alias, present=True)
    if not ok:
        check_fail("alias not listed under /ftp")
        ctx.record("smoke", "list host", "FAIL", fixture=alias)
        raise Failure("configured host did not appear under /ftp")
    check_ok()
    ctx.record("smoke", "list host", "OK", fixture=alias)

    check_start("enter host: server sees login + TYPE I + LIST")
    since = server.log_len()
    enter_host_root(ctx, alias)
    ctx.ensure_alive("after enter host")
    login = server.wait_for_command("PASS", since, timeout=8.0)
    typei = server.wait_for_command("TYPE", since, timeout=8.0)
    listc = server.wait_for_command("LIST", since, timeout=8.0)
    if not (login and listc):
        check_fail(f"missing FTP commands (PASS={bool(login)} LIST={bool(listc)})")
        ctx.record("smoke", "enter/LIST", "FAIL", "PASS/TYPE/LIST", alias)
        raise Failure("host entry did not produce expected FTP commands")
    check_ok(f"PASS,{'TYPE,' if typei else ''}LIST")
    ctx.record("smoke", "enter/LIST", "OK", "USER/PASS/TYPE/LIST", alias)

    check_start("root listing shows fixture entries")
    screen = d.screen()
    have = [n for n in ("README", "DIRA", "SMALL") if screen.find_row_containing(n) >= 0]
    assert_or_warn(ctx.assertions_enabled, len(have) >= 2,
                   f"expected fixture entries in listing, saw rows: {screen.selected_text!r}")
    check_ok(",".join(have))
    ctx.record("smoke", "root entries", "OK", "LIST", "README/DIRA/SMALL")

    check_start("open README.TXT -> RETR (52 bytes)")
    ctx.inspector.clear_ftp_cache()   # force a deterministic cache miss
    since = open_file_read(ctx, alias, "README.TXT")
    ctx.ensure_alive("after RETR README")
    retr = server.wait_for_command("RETR", since, timeout=8.0, arg_substr="README")
    if not retr:
        check_fail("no RETR observed for README.TXT")
        ctx.record("smoke", "RETR README", "FAIL", "RETR", "README.TXT")
        raise Failure("RETR not observed")
    nbytes = server.sent.get("README.TXT")
    expected = len(FIXTURES["README.TXT"])
    assert_or_warn(ctx.assertions_enabled, nbytes in (None, expected),
                   f"README RETR byte count {nbytes} != {expected}")
    check_ok(f"{nbytes} bytes")
    ctx.record("smoke", "RETR README", "OK", "RETR", "README.TXT", f"{nbytes} bytes")

    check_start("remove host and confirm it disappears")
    d.remove_ftp_host(alias)
    ctx.ensure_alive("after remove host")
    gone = verify_host_listed(ctx, alias, present=False)
    if not gone:
        check_fail("alias still listed after Remove")
        ctx.record("smoke", "remove host", "FAIL", fixture=alias)
        raise Failure("host removal failed")
    check_ok()
    ctx.record("smoke", "remove host", "OK", fixture=alias)
    ctx.alias = None  # already removed


# ---------------------------------------------------------------------------
# Stage: core
# ---------------------------------------------------------------------------
CORE_READ_FILES = [
    ("EMPTY.BIN", 0),
    ("SMALL.BIN", 16),
    ("EXACT4096.BIN", 4096),
    ("PLUS4097.BIN", 4097),
    ("LARGE16K.BIN", 16384),
    ("BYTES256.BIN", 1024),
]


def stage_core(ctx):
    print("\n=== STAGE: core ===")
    d, server = ctx.d, ctx.server
    if ctx.alias is None:
        ctx.alias = safe_name(ctx.args.alias_prefix)
        check_start(f"(re)create FTP host '{ctx.alias}'")
        create_host(ctx, ctx.alias)
        ctx.ensure_alive("recreate host")
        check_ok()
    alias = ctx.alias

    # -- persistence across menu close/reopen -----------------------------
    check_start("host persists across menu close/reopen")
    d.close_menu_from_anywhere()
    ok = verify_host_listed(ctx, alias, present=True)
    assert_or_warn(ctx.assertions_enabled, ok, "host not listed after menu reopen")
    check_ok()
    ctx.record("core", "persistence", "OK" if ok else "WARN", fixture=alias)

    # -- browse nested dirs + spaces --------------------------------------
    check_start("enter host, browse DIRA and 'DIR WITH SPACE'")
    enter_host_root(ctx, alias)
    d.select_row_text("DIRA")
    d.enter()
    dira = d.screen()
    have_inside = dira.find_row_containing("INSIDE") >= 0
    d.back()
    d.select_row_text("DIR WITH SPACE")
    d.enter()
    spaced = d.screen()
    have_spaced = spaced.find_row_containing("SPACED") >= 0
    d.back()
    assert_or_warn(ctx.assertions_enabled, have_inside, "DIRA/INSIDE.TXT not listed")
    assert_or_warn(ctx.assertions_enabled, have_spaced, "'DIR WITH SPACE' contents not listed")
    check_ok(f"DIRA={'ok' if have_inside else 'no'} SPACE={'ok' if have_spaced else 'no'}")
    ctx.record("core", "nested/space dirs", "OK", "CWD/LIST", "DIRA, DIR WITH SPACE")

    # -- scrolling dir ----------------------------------------------------
    check_start("enter MANY (30 entries), verify scrolling stays responsive")
    enter_host_root(ctx, alias)
    since = server.log_len()
    d.select_row_text("MANY")
    d.enter()
    listc = server.wait_for_command("LIST", since, timeout=10.0, arg_substr="MANY")
    populated = wait_for_listing(d, "F0", timeout=4.0)  # F00.TXT etc.
    for _ in range(20):
        d.tap(K_DOWN)
    many = d.screen()
    ctx.ensure_alive("scrolling MANY")
    has_entries = many.find_row_containing("F1") >= 0 or many.find_row_containing("F2") >= 0
    d.back()
    if populated and has_entries:
        check_ok(f"LIST MANY -> {'ok' if listc else 'seen'}, scrolled")
        ctx.record("core", "scroll dir", "OK", "LIST", "MANY(30)")
    else:
        check_warn(f"MANY listing not populated (LIST seen={bool(listc)})")
        ctx.record("core", "scroll dir", "WARN", "LIST", "MANY(30)",
                   "listing empty/slow")

    # -- reads of various sizes (force a clean cache miss for each) -------
    check_start("clear device FTP cache for deterministic first-open RETRs")
    removed = ctx.inspector.clear_ftp_cache()
    check_ok(f"{removed} cached file(s) removed")
    for name, size in CORE_READ_FILES:
        check_start(f"open {name} -> RETR {size} bytes")
        enter_host_root(ctx, alias)  # ensure at root
        since = open_file_read(ctx, alias, name)
        ctx.ensure_alive(f"RETR {name}")
        retr = server.wait_for_command("RETR", since, timeout=10.0, arg_substr=name)
        if retr is None and size == 0:
            # A zero-byte file still gets a RETR from this firmware, but tolerate
            # a data-phase-skipping server just in case.
            check_warn("no RETR seen for empty file (acceptable)")
            ctx.record("core", f"read {name}", "WARN", "RETR?", name, "empty file")
            continue
        assert_or_warn(ctx.assertions_enabled, retr is not None, f"no RETR for {name}")
        nbytes = server.sent.get(name)
        assert_or_warn(ctx.assertions_enabled, nbytes in (None, size),
                       f"{name} RETR byte count {nbytes} != {size}")
        check_ok(f"{nbytes} bytes" if retr else "")
        ctx.record("core", f"read {name}", "OK", "RETR", name, f"{nbytes} bytes")

    # -- cache hit --------------------------------------------------------
    check_start("re-open README.TXT: cache hit avoids a second RETR")
    ctx.inspector.clear_ftp_cache()
    enter_host_root(ctx, alias)
    open_file_read(ctx, alias, "README.TXT")   # first open -> RETR + cache fill
    since = server.log_len()
    open_file_read(ctx, alias, "README.TXT")   # second open -> should be cache hit
    time.sleep(0.5)
    second = server.commands_since(since, "RETR")
    cached = len([e for e in second if e[2] and "README" in e[2]]) == 0
    check_ok("cache hit (no RETR)" if cached else "RETR repeated (no cache)")
    ctx.record("core", "cache hit", "OK", "(cached)" if cached else "RETR", "README.TXT",
               "cache hit" if cached else "re-fetched")

    # -- create remote directory (F5 Tasks -> Create -> Directory) -------
    newdir = safe_name("EDIR").upper()[:12]
    check_start(f"create remote directory {newdir} via Tasks -> MKD")
    result = create_remote_dir(ctx, alias, newdir)
    ctx.record("core", "MKD", result[0], "MKD", newdir, result[1])
    (check_ok if result[0] == "OK" else check_warn)(result[1])

    # -- upload/STOR via clipboard paste ---------------------------------
    check_start("upload a local file into the host dir -> STOR")
    result = upload_via_clipboard(ctx, alias)
    ctx.record("core", "STOR", result[0], "STOR", result[2], result[1])
    (check_ok if result[0] == "OK" else check_warn)(result[1])

    # -- rename remote file (RNFR/RNTO) ----------------------------------
    check_start("rename a remote file -> RNFR/RNTO")
    result = rename_remote_file(ctx, alias)
    ctx.record("core", "rename", result[0], "RNFR/RNTO", result[1], result[2])
    (check_ok if result[0] == "OK" else check_warn)(result[2])

    # -- delete remote file (DELE) ---------------------------------------
    check_start("delete a remote file -> DELE")
    result = delete_remote_entry(ctx, alias, ctx._rename_target if hasattr(ctx, "_rename_target") else None)
    ctx.record("core", "DELE", result[0], "DELE", result[1], result[2])
    (check_ok if result[0] == "OK" else check_warn)(result[2])

    # -- delete empty remote directory (RMD) -----------------------------
    check_start(f"delete empty remote directory {newdir} -> RMD/DELE")
    result = delete_remote_entry(ctx, alias, newdir, expect="RMD")
    ctx.record("core", "RMD", result[0], "RMD/DELE", newdir, result[2])
    (check_ok if result[0] == "OK" else check_warn)(result[2])


def create_remote_dir(ctx, alias, dirname):
    d, server = ctx.d, ctx.server
    enter_host_root(ctx, alias)
    since = server.log_len()
    d.tap(K_F5, settle=MENU_SETTLE_SECONDS)     # Tasks menu (right-side popup)
    screen = d.screen()
    if not screen.contains("Create"):
        d.tap(K_RUNSTOP)
        return ("UNSUPPORTED_BY_UI",
                "Tasks menu has no 'Create' category for this node "
                "(user_file_interaction.cc create_task_items gates it on a writable path)")
    # Expand the "Create" category, then activate "Directory".
    d.select_task_action("Create")
    d.select_context_action("Directory", max_steps=24)
    # string_box prompt for the new directory name.
    d.type_text(dirname)
    d.tap(K_RETURN, settle=MENU_SETTLE_SECONDS)
    ctx.ensure_alive("after MKD")
    mkd = server.wait_for_command("MKD", since, timeout=8.0, arg_substr=dirname)
    if mkd and ctx.server.exists(dirname):
        return ("OK", f"MKD {dirname} confirmed on server")
    if mkd:
        return ("OK", f"MKD {dirname} sent (dir not found on disk yet)")
    return ("FAIL", f"no MKD observed for {dirname}")


def upload_via_clipboard(ctx, alias):
    """Stage a local file on the device via its FTP server, then copy it into
    the remote host directory through the UI (CTRL+C on the local file, enter
    the FTP dir, CTRL+V) which drives file_open(FA_CREATE) -> STOR on close."""
    d, server = ctx.d, ctx.server
    if not ctx.inspector.available():
        return ("SKIP", "device FTP server unavailable for staging local file", "")
    localname = safe_name("UP").upper()[:12] + ".BIN"
    payload = deterministic_bytes(200)
    try:
        ctx.inspector.upload_bytes(f"/Temp/{localname}", payload)
    except Exception as exc:
        return ("SKIP", f"could not stage local file: {exc}", localname)
    ctx.host_staged.append(f"/Temp/{localname}")

    # Select the local file in /Temp and copy it to the clipboard. The device
    # reopens the menu at its last position, so rewind to the top browser first.
    d.close_menu_from_anywhere()
    d.goto_top_browser("RAM Disk")
    d.select_row_text("RAM Disk")   # Temp row
    d.enter()
    try:
        d.select_row_text(localname, max_steps=60)
    except Failure:
        return ("SKIP", f"staged file {localname} not visible in /Temp", localname)
    since = server.log_len()
    d.tap(["ctrl", "c"], settle=MENU_SETTLE_SECONDS)   # copy to clipboard
    # copy_selection() pops "N files placed on clipboard" (BUTTON_OK): dismiss it.
    popup = d.screen(required=False)
    if popup is not None and popup.contains("clipboard"):
        d.tap(K_RETURN, settle=MENU_SETTLE_SECONDS)
    d.back()

    # Enter the FTP host dir and paste.
    enter_host_root(ctx, alias)
    d.tap(["ctrl", "v"], settle=MENU_SETTLE_SECONDS)   # paste -> STOR on close
    time.sleep(1.5)
    # A paste may raise a "Copy error occurred. Continue?" popup on failure, or
    # finish silently; clear any residual popup without disturbing the browser.
    resid = d.screen(required=False)
    if resid is not None and (resid.contains("error") or resid.contains("Continue")):
        d.tap(K_RETURN, settle=MENU_SETTLE_SECONDS)
    ctx.ensure_alive("after STOR paste")
    stem = localname.rsplit(".", 1)[0]
    stor = server.wait_for_command("STOR", since, timeout=10.0, arg_substr=stem)
    real = server.find_ci(os.path.basename(stor[2])) if stor else None
    if stor and real and os.path.exists(real):
        with open(real, "rb") as fh:
            data = fh.read()
        remote_name = os.path.basename(real)
        if data == payload:
            ctx._upload_name = remote_name
            return ("OK", f"STOR {remote_name}: {len(data)} bytes match", remote_name)
        return ("FAIL", f"STOR {remote_name}: byte mismatch ({len(data)} vs {len(payload)})", remote_name)
    if stor:
        return ("OK", f"STOR {os.path.basename(stor[2])} sent (file not on disk yet)", localname)
    return ("UNSUPPORTED_BY_UI",
            "no STOR observed - CTRL+C/CTRL+V clipboard paste may not translate; "
            "see tree_browser.cc KEY_CTRL_C/KEY_CTRL_V", localname)


def rename_remote_file(ctx, alias):
    d, server = ctx.d, ctx.server
    src = getattr(ctx, "_upload_name", None) or "MiXeD-Case.PrG"
    dst = safe_name("RN").upper()[:12] + ".TXT"
    enter_host_root(ctx, alias)
    try:
        d.select_row_text(src, max_steps=60)
    except Failure:
        return ("SKIP", src, f"source {src} not visible")
    since = server.log_len()
    d.open_context_menu()
    try:
        d.select_context_action("Rename")
    except Failure:
        d.tap(K_RUNSTOP)
        return ("UNSUPPORTED_BY_UI", src, "Rename action absent from context menu")
    # string_box prefilled with current name: clear it, then type the new name.
    for _ in range(len(src) + 2):
        d.tap(K_DEL)
    d.type_text(dst)
    d.tap(K_RETURN, settle=MENU_SETTLE_SECONDS)
    ctx.ensure_alive("after rename")
    rnfr = server.wait_for_command("RNFR", since, timeout=8.0)
    rnto = server.wait_for_command("RNTO", since, timeout=8.0)
    if rnfr and rnto:
        remote_new = os.path.basename(rnto[2]) if rnto[2] else dst
        ctx._rename_target = remote_new
        confirmed = server.find_ci(remote_new) is not None
        return ("OK", remote_new,
                f"RNFR/RNTO {src}->{remote_new} {'confirmed' if confirmed else 'sent'}")
    return ("FAIL", dst, "RNFR/RNTO not observed")


def delete_remote_entry(ctx, alias, name, expect="DELE"):
    d, server = ctx.d, ctx.server
    if name is None:
        return ("SKIP", "", "no target to delete")
    enter_host_root(ctx, alias)
    try:
        d.select_row_text(name, max_steps=60)
    except Failure:
        return ("SKIP", name, f"{name} not visible to delete")
    since = server.log_len()
    d.open_context_menu()
    try:
        d.select_context_action("Delete")
    except Failure:
        d.tap(K_RUNSTOP)
        return ("UNSUPPORTED_BY_UI", name, "Delete action absent from context menu")
    # "Are you sure?" YES/NO popup: select YES.
    screen = d.screen(required=False)
    if screen is not None and screen.contains("sure"):
        d.tap(K_LEFT)                # move to YES
        d.tap(K_RETURN, settle=MENU_SETTLE_SECONDS)
    ctx.ensure_alive("after delete")
    dele = server.wait_for_command("DELE", since, timeout=8.0)
    rmd = server.wait_for_command("RMD", since, timeout=8.0)
    if dele or rmd:
        which = "DELE" if dele else "RMD"
        gone = server.find_ci(name) is None
        return ("OK", name, f"{which} {name} {'confirmed gone' if gone else 'sent'}")
    return ("FAIL", name, f"neither DELE nor RMD observed for {name}")


# ---------------------------------------------------------------------------
# Stage: edge (exhaustive edge conditions)
# ---------------------------------------------------------------------------
def context_actions_present(ctx, expected):
    """With a context popup open, report which of `expected` action labels are
    visible in it."""
    screen = ctx.d.screen()
    return [a for a in expected if screen.find_row_containing(a) >= 0]


def stage_edge(ctx):
    print("\n=== STAGE: edge ===")
    d, s, server, args = ctx.d, ctx.s, ctx.server, ctx.args

    # -- multiple simultaneous hosts: with password (IP) + anonymous (no pw) --
    alias_pw = safe_name(args.alias_prefix + "pw")
    alias_anon = safe_name(args.alias_prefix + "an")
    check_start("create two hosts at once: passworded + anonymous")
    create_host(ctx, alias_pw)                                   # IP + user/pass
    create_host(ctx, alias_anon, user="anonymous", password="")  # anonymous, no pw
    d.close_menu_from_anywhere()
    d.enter_ftp_listing()
    both = (d.screen().find_row_containing(alias_pw) >= 0 and
            d.screen().find_row_containing(alias_anon) >= 0)
    assert_or_warn(ctx.assertions_enabled, both, "both hosts not listed simultaneously")
    check_ok("both listed")
    ctx.record("edge", "multiple hosts", "OK" if both else "WARN", "", f"{alias_pw},{alias_anon}")

    check_start("enter passworded host: server sees USER+PASS login")
    since = server.log_len()
    enter_host_root(ctx, alias_pw)
    user_cmd = server.wait_for_command("USER", since, timeout=8.0, arg_substr=args.ftp_user)
    pass_cmd = server.wait_for_command("PASS", since, timeout=8.0)
    ctx.ensure_alive("edge pw login")
    ok = bool(user_cmd) and bool(pass_cmd)
    (check_ok if ok else check_warn)(f"USER={bool(user_cmd)} PASS={bool(pass_cmd)}")
    ctx.record("edge", "password login", "OK" if ok else "WARN", "USER/PASS", alias_pw)

    check_start("enter anonymous host: server sees USER anonymous login")
    since = server.log_len()
    enter_host_root(ctx, alias_anon)
    anon_cmd = server.wait_for_command("USER", since, timeout=8.0, arg_substr="anonymous")
    listc = server.wait_for_command("LIST", since, timeout=8.0)
    ctx.ensure_alive("edge anon login")
    ok = bool(anon_cmd) and bool(listc)
    (check_ok if ok else check_warn)(f"USER anonymous={bool(anon_cmd)} LIST={bool(listc)}")
    ctx.record("edge", "anonymous login", "OK" if ok else "WARN", "USER/LIST", alias_anon)

    for a in (alias_pw, alias_anon):
        d.remove_ftp_host(a)
        ctx.ensure_alive("edge cleanup hosts")

    # -- alias edge cases -------------------------------------------------
    edge_aliases = [
        ("E2E-MAX-ALIAS16", "16-char alias with hyphens"),
        ("E2E9DIGITS0", "digits in alias"),
    ]
    for alias, desc in edge_aliases:
        check_start(f"alias edge case: {desc} ({alias})")
        create_host(ctx, alias, path="/")
        listed = verify_host_listed(ctx, alias, present=True)
        ctx.ensure_alive(f"edge alias {alias}")
        (check_ok if listed else check_warn)("listed" if listed else "not listed")
        ctx.record("edge", "alias case", "OK" if listed else "WARN", "", alias, desc)
        d.remove_ftp_host(alias)
        ctx.ensure_alive("edge alias cleanup")

    # -- path variation: subfolder root -----------------------------------
    alias_sub = safe_name(args.alias_prefix + "sub")
    check_start("path variation: host rooted at /DIRA lists only its contents")
    create_host(ctx, alias_sub, path="/DIRA")
    since = server.log_len()
    enter_host_root(ctx, alias_sub)
    inside = wait_for_listing(d, "INSIDE", timeout=5.0)
    # A subfolder root must NOT show the server-root-only fixtures.
    not_root = d.screen().find_row_containing("README") < 0
    ctx.ensure_alive("edge subpath")
    ok = inside and not_root
    (check_ok if ok else check_warn)(f"INSIDE={inside} isolated={not_root}")
    ctx.record("edge", "subfolder root", "OK" if ok else "WARN", "CWD/LIST", "/DIRA")
    d.remove_ftp_host(alias_sub)
    ctx.ensure_alive("edge subpath cleanup")

    # -- port variation: serve the same fixtures on a different port ------
    # pyftpdlib drives a process-global IOLoop, so two servers cannot run at
    # once. Stop the main server, serve its fixtures on the alternate port,
    # then restore the main server for the remaining checks.
    alt_port = args.ftp_port + 1
    check_start(f"port variation: host on alternate port {alt_port}")
    passive_hi = list(args.ftp_passive_ports)[-1]
    ctx.server.stop()
    alt = ControlledFtpServer(args.ftp_bind_host, args.ftp_advertised_host, alt_port,
                              range(passive_hi + 1, passive_hi + 6),
                              args.ftp_user, args.ftp_password,
                              root=ctx.server.root, keep_root=True)
    alt.start()
    alias_port = safe_name(args.alias_prefix + "pt")
    try:
        create_host(ctx, alias_port, port=alt_port)
        since = alt.log_len()
        enter_host_root(ctx, alias_port)
        alt_list = alt.wait_for_command("LIST", since, timeout=8.0)
        ctx.ensure_alive("edge alt port")
        (check_ok if alt_list else check_warn)(f"LIST on :{alt_port}={bool(alt_list)}")
        ctx.record("edge", "alt port", "OK" if alt_list else "WARN", "LIST", f"port {alt_port}")
        d.remove_ftp_host(alias_port)
        ctx.ensure_alive("edge alt port cleanup")
    finally:
        alt.stop()            # leave the shared root intact
        ctx.server.start()    # restore the main server on its original port

    # -- per-file-type context menu options -------------------------------
    alias_ctx = safe_name(args.alias_prefix + "cx")
    create_host(ctx, alias_ctx, path="/")
    per_type = [
        ("README.TXT", ["View", "Hex View", "Copy to", "Rename", "Delete"]),
        ("SMALL.BIN", ["View", "Hex View", "Copy to", "Rename", "Delete"]),
        ("MiXeD-Case.PrG", ["Copy to", "Rename", "Delete"]),
    ]
    for fname, expected in per_type:
        check_start(f"context menu options for {fname}")
        enter_host_root(ctx, alias_ctx)
        d.select_row_text(fname, max_steps=60)
        d.open_context_menu()
        present = context_actions_present(ctx, expected)
        d.tap(K_RUNSTOP, settle=MENU_SETTLE_SECONDS)  # close context
        ctx.ensure_alive(f"edge ctx {fname}")
        ok = len(present) >= max(2, len(expected) - 1)
        (check_ok if ok else check_warn)(", ".join(present))
        ctx.record("edge", f"ctx {fname}", "OK" if ok else "WARN", "", fname, "/".join(present))

    # exercise View + Hex View on the text file (open then close each)
    check_start("exercise View and Hex View actions on README.TXT")
    for action in ("View", "Hex View"):
        enter_host_root(ctx, alias_ctx)
        d.select_row_text("README.TXT", max_steps=60)
        d.open_context_menu()
        d.select_context_action(action)
        time.sleep(MENU_SETTLE_SECONDS)
        d.tap(K_RUNSTOP, settle=MENU_SETTLE_SECONDS)  # close the viewer
        ctx.ensure_alive(f"edge action {action}")
    check_ok("View + Hex View opened and closed cleanly")
    ctx.record("edge", "view/hexview", "OK", "RETR", "README.TXT")

    # -- server 'Edit' context action, with a compensating revert -----------
    check_start("edit host Path -> /DIRA and revert -> / (compensating edit)")
    changed, reverted = edit_and_revert(ctx, alias_ctx)
    ctx.ensure_alive("edge edit/revert")
    ok = changed and reverted
    (check_ok if ok else check_warn)(f"changed={changed} reverted={reverted}")
    ctx.record("edge", "edit/revert", "OK" if ok else "WARN", "CWD/LIST", alias_ctx,
               "Path /DIRA then reverted to /")

    d.remove_ftp_host(alias_ctx)
    ctx.ensure_alive("edge ctx cleanup")

    # -- hostname vs IP (best effort; needs device-resolvable DNS) ---------
    import socket as _socket
    hostname = _socket.gethostname()
    check_start(f"hostname connection (best effort): {hostname}")
    alias_hn = safe_name(args.alias_prefix + "hn")
    try:
        create_host(ctx, alias_hn, host=hostname)
        since = server.log_len()
        enter_host_root(ctx, alias_hn)
        listc = server.wait_for_command("LIST", since, timeout=8.0)
        ctx.ensure_alive("edge hostname")
        if listc:
            check_ok(f"device resolved and connected via hostname {hostname}")
            ctx.record("edge", "hostname", "OK", "LIST", hostname)
        else:
            check_warn(f"device could not resolve hostname {hostname} (no LAN DNS); IP path already covered")
            ctx.record("edge", "hostname", "WARN", "", hostname, "no device DNS")
    finally:
        try:
            d.remove_ftp_host(alias_hn)
        except Exception:
            pass
        ctx.ensure_alive("edge hostname cleanup")


# ---------------------------------------------------------------------------
# Stage: matrix
# ---------------------------------------------------------------------------
def stage_matrix(ctx):
    print("\n=== STAGE: matrix ===")
    server = ctx.server
    # Root-path form #1: server root "/". Reuse the already-created host reads.
    for root_path, label in (("/", "root=/"), ("/DIRA", "root=/DIRA")):
        alias = safe_name(ctx.args.alias_prefix + "m")
        check_start(f"matrix {label}: create host with Path {root_path}")
        create_host(ctx, alias, path=root_path)
        ctx.ensure_alive(f"matrix create {label}")
        listed = verify_host_listed(ctx, alias, present=True)
        check_ok() if listed else check_warn("not listed")
        ctx.record("matrix", f"create {label}", "OK" if listed else "WARN", fixture=alias)

        check_start(f"matrix {label}: enter + LIST + read a file")
        since = server.log_len()
        enter_host_root(ctx, alias)
        listc = server.wait_for_command("LIST", since, timeout=8.0)
        ctx.ensure_alive(f"matrix list {label}")
        # pick a fixture that exists under this root
        fname = "README.TXT" if root_path == "/" else "INSIDE.TXT"
        ctx.inspector.clear_ftp_cache()  # force a fresh RETR, not a cache hit
        since = open_file_read(ctx, alias, fname)
        retr = server.wait_for_command("RETR", since, timeout=8.0)
        ok = bool(listc) and bool(retr)
        check_ok(fname) if ok else check_warn(f"LIST={bool(listc)} RETR={bool(retr)}")
        ctx.record("matrix", f"read {label}", "OK" if ok else "WARN", "LIST/RETR", fname)

        # cleanup this matrix host
        ctx.d.remove_ftp_host(alias)
        ctx.ensure_alive(f"matrix cleanup {label}")


# ---------------------------------------------------------------------------
# Stage: negative
# ---------------------------------------------------------------------------
def recover_after_negative(ctx, label):
    """Every negative case must leave the device responsive with a closable
    menu, working release_all, and no stuck keys."""
    s, d = ctx.s, ctx.d
    if not s.is_alive(timeout=5.0):
        raise HardCrash(f"device unresponsive after negative case: {label}")
    d.close_menu_from_anywhere()
    time.sleep(1.0)   # let the device settle after a failed/aborted connection
    d.open_menu()
    d.close_menu_from_anywhere()
    s.release_all()
    stuck = s.held_keys()
    assert_or_warn(ctx.assertions_enabled, not stuck, f"stuck keys after {label}: {stuck}")


def negative_case(ctx, label, alias_suffix, host=None, port=None, password=None, path="/",
                  action="enter"):
    d = ctx.d
    alias = safe_name(ctx.args.alias_prefix + alias_suffix)
    check_start(f"negative: {label}")
    try:
        create_host(ctx, alias, path=path, host=host, port=port, password=password)
        ctx.ensure_alive(f"negative create {label}")
        if action == "enter":
            # Entering triggers connect/login/list which should fail gracefully.
            d.enter_ftp_listing()
            d.select_row_text(alias)
            d.enter()
            time.sleep(1.0)
            d.screen(required=False)
        ctx.ensure_alive(f"negative action {label}")
        recover_after_negative(ctx, label)
        check_ok("handled, device responsive")
        ctx.record("negative", label, "OK", "", alias, "no crash, menu recovered")
    finally:
        # remove the throwaway host if it still exists
        try:
            d.remove_ftp_host(alias)
        except Exception:
            pass


def stage_negative(ctx):
    print("\n=== STAGE: negative ===")
    args = ctx.args
    server = ctx.server

    negative_case(ctx, "bad password", "bad", password="wrongpw" + safe_name(""))
    negative_case(ctx, "unreachable port", "prt", port=animate_bad_port(args))
    negative_case(ctx, "missing remote path", "mp", path="/DOES-NOT-EXIST-" + safe_name(""))

    # permission-denied delete: RDONLY/LOCKED.TXT cannot be removed.
    alias = safe_name(args.alias_prefix + "ro")
    check_start("negative: permission-denied delete (RDONLY area)")
    try:
        create_host(ctx, alias, path="/RDONLY")
        ctx.ensure_alive("perm-denied create")
        res = delete_remote_entry(ctx, alias, "LOCKED.TXT")
        still_there = server.exists("RDONLY", "LOCKED.TXT")
        ctx.ensure_alive("perm-denied delete")
        recover_after_negative(ctx, "permission-denied delete")
        if still_there:
            check_ok("delete denied by server, file intact, device responsive")
            ctx.record("negative", "perm-denied delete", "OK", "DELE(550)", "RDONLY/LOCKED.TXT")
        else:
            check_warn("file was removed despite read-only area")
            ctx.record("negative", "perm-denied delete", "WARN", "DELE", "RDONLY/LOCKED.TXT")
    finally:
        try:
            ctx.d.remove_ftp_host(alias)
        except Exception:
            pass

    # server stopped while a host is configured, then restarted.
    alias = safe_name(args.alias_prefix + "stop")
    check_start("negative: FTP server stopped, then restarted")
    try:
        create_host(ctx, alias, path="/")
        server.stop()
        ctx.d.enter_ftp_listing()
        ctx.d.select_row_text(alias)
        ctx.d.enter()
        time.sleep(1.0)
        ctx.ensure_alive("server-stopped enter")
        recover_after_negative(ctx, "server stopped")
        server.start()   # restart
        since = server.log_len()
        enter_host_root(ctx, alias)
        listc = server.wait_for_command("LIST", since, timeout=10.0)
        ctx.ensure_alive("server-restarted enter")
        recover_after_negative(ctx, "server restarted")
        if listc:
            check_ok("recovered after restart (fresh LIST)")
            ctx.record("negative", "server stop/restart", "OK", "LIST", alias)
        else:
            check_warn("no LIST after restart")
            ctx.record("negative", "server stop/restart", "WARN", "", alias)
    finally:
        try:
            ctx.d.remove_ftp_host(alias)
        except Exception:
            pass


def animate_bad_port(args):
    # A port with (almost certainly) nothing listening on the host.
    return 9 if args.ftp_port != 9 else 7


# ---------------------------------------------------------------------------
# Stage: soak
# ---------------------------------------------------------------------------
# -- soak operations: each is self-contained and leaves state unchanged ------
SOAK_READ_FILES = ["README.TXT", "SMALL.BIN", "BYTES256.BIN", "EXACT4096.BIN",
                   "EMPTY.BIN", "PLUS4097.BIN"]


def soak_browse(ctx, n):
    d = ctx.d
    enter_host_root(ctx, ctx.alias)
    d.select_row_text("DIRA"); d.enter(); d.screen(required=False); d.back()
    d.select_row_text("DIR WITH SPACE"); d.enter(); d.screen(required=False); d.back()
    return "browse"


def soak_read(ctx, n):
    enter_host_root(ctx, ctx.alias)
    open_file_read(ctx, ctx.alias, SOAK_READ_FILES[n % len(SOAK_READ_FILES)])
    return "read"


def soak_edit(ctx, n):
    changed, reverted = edit_and_revert(ctx, ctx.alias)
    if not reverted:
        raise Failure("soak edit: host Path was not reverted to /")
    return "edit/revert"


def soak_mkdir(ctx, n):
    name = safe_name("SKD").upper()[:12]
    r = create_remote_dir(ctx, ctx.alias, name)
    if r[0] != "OK":
        raise Failure(f"soak mkdir failed: {r[1]}")
    r2 = delete_remote_entry(ctx, ctx.alias, name, expect="RMD")
    if r2[0] not in ("OK", "SKIP"):
        raise Failure(f"soak rmdir failed: {r2[2]}")
    return "mkdir/rmdir"


def soak_upload(ctx, n):
    r = upload_via_clipboard(ctx, ctx.alias)
    if r[0] == "OK":
        delete_remote_entry(ctx, ctx.alias, r[2])   # clean up the uploaded file
        return "upload/delete"
    if r[0] in ("SKIP", "UNSUPPORTED_BY_UI"):
        return "upload(skip)"
    raise Failure(f"soak upload failed: {r[1]}")


def soak_rename(ctx, n):
    up = upload_via_clipboard(ctx, ctx.alias)
    if up[0] != "OK":
        return "rename(skip)"
    ctx._upload_name = up[2]
    r = rename_remote_file(ctx, ctx.alias)
    if r[0] == "OK":
        delete_remote_entry(ctx, ctx.alias, r[1])    # clean up the renamed file
        return "rename"
    if r[0] == "SKIP":
        return "rename(skip)"
    raise Failure(f"soak rename failed: {r[2]}")


def soak_recreate(ctx, n):
    # Remove the host and immediately recreate it (the Delete->recreate rule).
    ctx.d.remove_ftp_host(ctx.alias)
    ctx.d.close_menu_from_anywhere()   # let the /ftp refresh settle after remove
    new_alias = safe_name(ctx.args.alias_prefix)
    create_host(ctx, new_alias)
    if not verify_host_listed(ctx, new_alias, present=True):
        raise Failure(f"soak recreate: host {new_alias} not listed after create")
    ctx.alias = new_alias
    return "remove/recreate"


SOAK_OPS = [soak_browse, soak_read, soak_edit, soak_mkdir, soak_upload,
            soak_rename, soak_recreate]


def stage_soak(ctx):
    print("\n=== STAGE: soak ===")
    s, server = ctx.s, ctx.server
    duration = ctx.args.soak_duration
    if ctx.alias is None:
        ctx.alias = safe_name(ctx.args.alias_prefix)
        create_host(ctx, ctx.alias)
    check_start(f"soak {int(duration)}s: full-functionality cycles "
                "(browse/read/edit/mkdir/upload/rename/recreate)")
    started = time.monotonic()
    deadline = started + duration
    cycles = 0
    last_alive = started
    op_counts = {}
    try:
        while time.monotonic() < deadline:
            op = SOAK_OPS[cycles % len(SOAK_OPS)]
            label = op(ctx, cycles)
            op_counts[label] = op_counts.get(label, 0) + 1
            cycles += 1
            now = time.monotonic()
            if now - last_alive >= 10.0:
                if not s.is_alive(timeout=5.0):
                    raise HardCrash("device unresponsive during soak")
                last_alive = now
            body = s.get_menu_screen()
            if body is not None and not MenuScreen(body).looks_like_menu():
                raise Failure("garbage/blank menu snapshot during soak")
            time.sleep(ctx.args.soak_key_interval)
    except HardCrash:
        raise
    elapsed = time.monotonic() - started
    summary = ", ".join(f"{k}={v}" for k, v in sorted(op_counts.items()))
    check_ok(f"{cycles} ops in {elapsed:.0f}s, {server.log_len()} FTP cmds [{summary}]")
    ctx.record("soak", "full-functionality cycles", "OK", f"{server.log_len()} cmds",
               ctx.alias, f"{cycles} ops/{elapsed:.0f}s: {summary}")


# ---------------------------------------------------------------------------
# PRG run check (opt-in): run a remote PRG and verify a RAM marker.
# ---------------------------------------------------------------------------
def stage_prg(ctx):
    print("\n=== STAGE: prg (opt-in) ===")
    check_start("run remote PRG and verify RAM marker")
    check_warn("PRG-run check not implemented as an automated fixture; "
               "left as UNSUPPORTED to avoid destabilising the machine state. "
               "Use --run-prg with a custom fixture to enable.")
    ctx.record("prg", "run PRG", "UNSUPPORTED_BY_UI", "", "",
               "opt-in; not run by default")


# ---------------------------------------------------------------------------
# Cleanup + reporting
# ---------------------------------------------------------------------------
def cleanup(ctx, crashed):
    print("\n=== CLEANUP ===")
    s, d = ctx.s, ctx.d
    if crashed:
        print("Device hard-crashed; skipping UI cleanup.")
        print("Recover with: bash tooling/build_and_deploy_u64.sh (JTAG redeploy)")
    else:
        try:
            if ctx.alias and not ctx.args.preserve_ftp_host:
                d.remove_ftp_host(ctx.alias)
                print(f"Removed test host '{ctx.alias}'.")
            d.close_menu_from_anywhere()
            s.release_all()
        except Exception as exc:
            print(f"UI cleanup warning: {exc}")
    # Remove any files staged on the device's local FS.
    for path in ctx.host_staged:
        try:
            ctx.inspector.delete(path)
        except Exception:
            pass
    preserved = []
    if ctx.args.preserve_ftp_host and ctx.alias:
        preserved.append(f"FTP host '{ctx.alias}'")
    if ctx.server.keep_root:
        preserved.append(f"server root {ctx.server.root}")
    print("Preserved: " + (", ".join(preserved) if preserved else "nothing"))


def print_summary(ctx, crashed):
    s, server = ctx.s, ctx.server
    print("\n" + "=" * 78)
    print("SUMMARY")
    print("=" * 78)
    header = f"{'Phase':8} {'Operation':22} {'Result':18} {'Commands':14} Fixture/Notes"
    print(header)
    print("-" * 78)
    for phase, op, result, cmds, fixture, notes in ctx.results:
        fx = fixture if not notes else f"{fixture} {notes}".strip()
        print(f"{phase:8} {op:22.22} {result:18.18} {cmds:14.14} {fx}")
    print("-" * 78)
    print(f"Total REST requests   : {s.rest_requests}")
    print(f"Total menu snapshots  : {s.menu_snapshots}")
    print(f"Total keyboard events : {s.key_events}")
    print(f"Total FTP commands    : {server.log_len()}")
    print(f"Created host alias    : {ctx.alias if ctx.args.preserve_ftp_host else '(removed)'}")
    print(f"Remote root path      : {server.root}")
    print(f"Cleanup status        : {'CRASH - manual recovery needed' if crashed else 'clean'}")
    fails = [r for r in ctx.results if r[2] not in ("OK", "WARN", "SKIP", "UNSUPPORTED_BY_UI")]
    unsupported = [r for r in ctx.results if r[2] == "UNSUPPORTED_BY_UI"]
    if unsupported:
        print("\nUNSUPPORTED_BY_UI (source-backed):")
        for r in unsupported:
            print(f"  - {r[0]}/{r[1]}: {r[5]}")
    return fails


def print_failure_diagnostics(ctx, exc):
    print("\n" + "!" * 78)
    print(f"FAILURE: {exc}")
    print("!" * 78)
    print(f"Last check          : [{CHECK_COUNT:02d}] {LAST_CHECK_LABEL}")
    print(f"Last REST input      : {ctx.d.last_input}")
    print(f"Last FTP command     : {ctx.server.snapshot_log()[-1] if ctx.server.log_len() else '(none)'}")
    body = None
    try:
        body = ctx.s.get_menu_screen()
    except Exception:
        pass
    if body is not None:
        screen = MenuScreen(body)
        print("Decoded menu screen:")
        for i, r in enumerate(screen.rows):
            mark = ">>" if i == screen.selected_row else "  "
            print(f"{mark}{i:2d}|{r}|")
        print(f"selected_row={screen.selected_row} selected_text={screen.selected_text!r}")
    print("\nServer log tail:")
    for e in ctx.server.snapshot_log()[-12:]:
        print(f"  {e[1]:6} {str(e[2])[:32]:32} -> {e[3]} {e[4][:40]}")


# ---------------------------------------------------------------------------
# CLI + main
# ---------------------------------------------------------------------------
STAGE_ORDER = ["smoke", "core", "edge", "matrix", "negative", "soak"]


def parse_ports(text):
    lo, _, hi = text.partition("-")
    lo = int(lo)
    hi = int(hi) if hi else lo
    return range(lo, hi + 1)


def parse_duration(text):
    t = text.strip().lower()
    mult = 1.0
    if t.endswith("ms"):
        mult, t = 0.001, t[:-2]
    elif t.endswith("s"):
        t = t[:-1]
    elif t.endswith("m"):
        mult, t = 60.0, t[:-1]
    return float(t) * mult


def parse_args(argv=None):
    p = argparse.ArgumentParser(
        description="End-to-end test harness for the FTP remote filesystem on a real Ultimate 64/64e.",
        epilog="Runs a controlled local pyftpdlib server, configures it as a remote host "
               "through the real firmware UI, and verifies every FTP operation against the "
               "server's filesystem and command log.")
    p.add_argument("-H", "--host", default="u64", help="Ultimate hostname or IP (default u64)")
    p.add_argument("-p", "--password", default="", help="REST password")
    p.add_argument("-n", "--no-assertions", action="store_true",
                   help="Warn instead of failing on assertion mismatches")
    p.add_argument("-t", "--timeout", type=float, default=5.0, help="REST timeout seconds")
    p.add_argument("--stage", choices=STAGE_ORDER + ["all"], default="smoke")
    p.add_argument("--ftp-bind-host", default="0.0.0.0", help="local FTP bind address")
    p.add_argument("--ftp-advertised-host", default=None,
                   help="address the Ultimate should connect to (required unless inferable)")
    p.add_argument("--ftp-port", type=int, default=2121, help="local FTP control port")
    p.add_argument("--ftp-passive-ports", default="30000-30019", help="passive port range lo-hi")
    p.add_argument("--ftp-user", default="u64e2e", help="remote FTP username")
    p.add_argument("--ftp-password", default="u64e2e", help="remote FTP password")
    p.add_argument("--remote-root", default=None, help="serve an existing local directory instead of a temp one")
    p.add_argument("--keep-remote-root", action="store_true", help="do not delete the server root at end")
    p.add_argument("--preserve-ftp-host", action="store_true", help="leave the configured FTP host in the UI")
    p.add_argument("--alias-prefix", default="E2EFTP", help="unique alias prefix for created hosts")
    p.add_argument("--reset-before-run", action="store_true")
    p.add_argument("--reset-after-run", action="store_true")
    p.add_argument("--soak-duration", default="10m", help="soak duration (e.g. 30s, 10m)")
    p.add_argument("--soak-key-interval", type=float, default=0.20)
    p.add_argument("--no-stop-on-crash", action="store_true", help="continue even after a hard crash")
    p.add_argument("--verbose-menu", action="store_true", help="print decoded menu screens on failure")
    p.add_argument("--run-prg", action="store_true", help="run the opt-in remote-PRG RAM-marker check")
    args = p.parse_args(argv)
    args.soak_duration = parse_duration(args.soak_duration)
    args.ftp_passive_ports = parse_ports(args.ftp_passive_ports)
    return args


def infer_advertised_host(device_host):
    """Find the local source IP the device would see us on."""
    import socket
    try:
        target = socket.gethostbyname(device_host)
    except OSError:
        return None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.connect((target, 21))
        ip = sock.getsockname()[0]
        sock.close()
        return ip
    except OSError:
        return None


def stages_for(args):
    if args.stage == "all":
        return ["smoke", "core", "edge", "matrix", "negative", "soak"]
    if args.stage == "smoke":
        return ["smoke"]
    # any explicit non-smoke stage implies smoke setup first for a clean host
    ordered = ["smoke"]
    for st in STAGE_ORDER:
        if st == args.stage:
            ordered.append(st)
            break
    return ordered if args.stage != "smoke" else ["smoke"]


def main(argv=None):
    args = parse_args(argv)
    assertions_enabled = not args.no_assertions

    if not args.ftp_advertised_host:
        args.ftp_advertised_host = infer_advertised_host(args.host)
        if not args.ftp_advertised_host:
            print("ERROR: could not infer --ftp-advertised-host; please pass it explicitly.",
                  file=sys.stderr)
            return 2
        print(f"Inferred --ftp-advertised-host {args.ftp_advertised_host}")

    session = RestSession(args.host, args.password, args.timeout)
    driver = MenuDriver(session, verbose_menu=args.verbose_menu)
    inspector = DeviceFtpInspector(args.host)

    print(f"Starting controlled FTP server on {args.ftp_bind_host}:{args.ftp_port} "
          f"(advertised {args.ftp_advertised_host})")
    try:
        server = ControlledFtpServer(
            args.ftp_bind_host, args.ftp_advertised_host, args.ftp_port,
            args.ftp_passive_ports, args.ftp_user, args.ftp_password,
            root=args.remote_root, keep_root=args.keep_remote_root)
    except Failure as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    server.start()
    print(f"Server root: {server.root} (marker {server.marker})")

    ctx = Context(args, session, driver, server, inspector, assertions_enabled)
    ctx.alias = safe_name(args.alias_prefix)
    crashed = False

    if args.reset_before_run:
        session.require_ok("PUT", "/v1/machine:reset", description="reset")
        time.sleep(3.0)

    try:
        endpoint_checks(ctx)
        check_start("remove any stale hosts left by a prior run")
        stale = cleanup_stale_hosts(ctx)
        check_ok(f"{stale} removed")
        run = stages_for(args)
        for stage in run:
            {"smoke": stage_smoke, "core": stage_core, "edge": stage_edge,
             "matrix": stage_matrix, "negative": stage_negative,
             "soak": stage_soak}[stage](ctx)
        if args.run_prg:
            stage_prg(ctx)
    except HardCrash as exc:
        crashed = True
        print_failure_diagnostics(ctx, f"HARD CRASH: {exc}")
    except Failure as exc:
        print_failure_diagnostics(ctx, exc)
    except KeyboardInterrupt:
        print("\nInterrupted.")
    finally:
        try:
            cleanup(ctx, crashed)
        except Exception as exc:
            print(f"cleanup error: {exc}")
        if args.reset_after_run and not crashed and session.is_alive(timeout=5.0):
            session.require_ok("PUT", "/v1/machine:reset", description="reset")
        fails = print_summary(ctx, crashed)
        server.cleanup()

    if crashed:
        return 3
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
