#!/usr/bin/env python3
# E2E: Verifies open browsers refresh after filesystem changes from every origin.

"""Operation x origin x observer matrix for file-system change notification.

Every observer of a directory must converge on the committed file-system state
after any mutation, whoever initiated it, without leaving and re-entering the
directory and without reconnecting or changing directory on a remote session.

Observers, all three polled after every mutation and none of them navigating:

  Menu    the on-device browser rendered on the C64/HDMI overlay, read over
          REST through GET /v1/machine:menu_screen.
  Telnet  the browser served on port 23. It is a TreeBrowser with its own
          cached child list, exactly like the Menu, so it is a push observer
          and not a fresh reader.
  FTP     LIST on a session that stays in the fixture directory. It re-reads
          the file system on every look, so it is the pull observer.

REST GET /v1/files/<path>:info is the independent oracle: it proves what
actually committed, so a failure can be attributed to notification rather than
to the operation itself.
"""

import argparse
import ftplib
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from collections.abc import Callable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "api"))
# tests/lib holds the reporting rules every suite shares; tests/e2e/lib
# holds the shared UI backend.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import ftp as ftp_lib
import machine as machine_lib
import rest as rest_lib
import targets
from report import detail, suite_fail, suite_ok

from menu_screen_test import (
    SCREEN_WIDTH,
    Failure,
    RestSession,
    check,
)
import ui_backend


FTP_USER = "user"
FTP_DEFAULT_PASSWORD = "password"

ROOT_PATH = "/"
TEST_DIR_PREFIX = "zbfr"

# Rendered sizes have to differ, and size_to_string_bytes rounds hard: 1000 and
# 1400 both render as "   1K". 100 renders " 100 ", 4096 renders "   4K".
#
# Only the rendering has to differ; nothing here depends on the second size
# being large. The partial-write checks snapshot the observers before any data
# is sent, so what they see is the same whatever follows, and the subject of
# this suite is change notification rather than transfer throughput. It was
# 200000, which sent 200KB over FTP and had the device copy it again for every
# row that used it, and the two copy rows were the most expensive checks in the
# whole gate at 20.6s and 35.6s.
SIZE_S1 = 100
SIZE_S2 = 4096
# BinImage(35 tracks) is 683 sectors of 256 bytes, written by Create > D64 Image.
SIZE_D64 = 174848
REST_IMAGE_SIZES = {
    "create_d64": 683 * 256,
    "create_d71": 683 * 2 * 256,
    "create_d81": 3200 * 256,
    "create_dnp": 4 * 65536,
}

# Convergence is event driven, so this is only a settling allowance. It matches
# the 2 s mount wait in browser_long_filename_test plus the second a Telnet
# full-screen repaint needs to arrive and go quiet.
CONVERGE_TIMEOUT_SECONDS = 3.0
CONVERGE_POLL_SECONDS = 0.10

# The Menu is 40x25 and the Telnet screen is 60x24 (screen_vt100.h:25-26). Both
# browsers own screen rows 2..height-2; the bottom row is the status line.
MENU_ENTRY_ROWS = range(2, 24)
MENU_STATUS_ROW = 24
TELNET_WIDTH = 60
TELNET_HEIGHT = 24
TELNET_ENTRY_ROWS = range(2, 23)
TELNET_STATUS_ROW = 23
PICKER_TITLE = "Select Path"
PICKER_SELECT_ENTRY = "<< Select Current Dir >>"
EMPTY_DIRECTORY_MARKER = "< No Items >"

# Box glyphs: the C64 screen draws them below 0x20 so the menu snapshot renders
# them as spaces, while Screen_VT100 draws them in the alternate character set,
# which ui_backend's screen model folds onto + - |.
FRAME_CHARS = " |+-"

# --------------------------------------------------------------------------
# Canonical snapshot
# --------------------------------------------------------------------------


def size_to_string_bytes(size: int) -> str:
    """Exact port of software/components/size_str.cc:6-22."""
    if size < 1000:
        return "%4d " % size
    size = (size + 512) >> 10
    if size < 10000:
        return "%4dK" % size
    size = (size + 512) >> 10
    return "%4dM" % size


class Entry:
    """One directory entry as a single observer reports it."""

    def __init__(self, name: str, kind: str, size_text: str, exact_size: int | None = None) -> None:
        self.name = name
        self.kind = kind  # "dir", "file" or "volume"
        self.size_text = size_text  # the 5-character rendered size, "" for a directory
        self.exact_size = exact_size  # bytes, where the observer reports them

    def key(self) -> tuple[str, str, str]:
        return (self.name, self.kind, self.size_text)

    def __repr__(self) -> str:
        if self.kind != "file":
            return f"{self.name}({self.kind})"
        exact = "" if self.exact_size is None else f"={self.exact_size}"
        return f"{self.name}[{self.size_text}]{exact}"


Snapshot = dict[str, Entry]


def expected_snapshot(entries: Sequence[tuple[str, int | None]]) -> Snapshot:
    """Canonical state: (name, exact size), or (name, None) for a directory."""
    result: Snapshot = {}
    for name, size in entries:
        if size is None:
            result[name] = Entry(name, "dir", "")
        else:
            result[name] = Entry(name, "file", size_to_string_bytes(size), size)
    return result


def snapshot_keys(snapshot: Snapshot) -> list[tuple[str, str, str]]:
    return sorted(entry.key() for entry in snapshot.values())


def format_snapshot(snapshot: Snapshot) -> str:
    if not snapshot:
        return "<empty>"
    return ", ".join(repr(snapshot[name]) for name in sorted(snapshot))


def strip_browser_frame(row: str, width: int) -> tuple[str, int]:
    """A browser row without the window frame, and the width that leaves.

    A C64 Ultimate draws its file browser inside a framed window, so every
    field sits one column to the right of where the other two machines put it
    and the row is two columns narrower. parse_browser_row measures its fields
    from the first content column, so the frame has to come off first.
    Measured on a C64 Ultimate: with the frame left on, every row parsed as
    empty, so all three observers reported an empty directory while the screen
    plainly listed the file, and all 35 matrix rows failed to converge.
    """
    row = row.ljust(width)
    if row[0] == "|" and row[width - 1] == "|":
        return row[1:width - 1], width - 2
    return row, width


def parse_browser_row(row: str, width: int) -> Entry | None:
    """Split one rendered browser line.

    BrowsableDirEntry::getDisplayString (browsable_root.h:200-224) lays the row
    out as the name in a field of width - 11, a space, a 3-character extension
    field, the selection marker and the 5-character size string.
    """
    row = row.ljust(width)
    name = row[: width - 11].rstrip()
    if not name:
        return None
    tail = row[width - 10 :]
    extension = row[width - 10 : width - 7].strip()
    size_text = row[width - 6 : width - 1]
    if "VOLUME" in tail:
        return Entry(name, "volume", "")
    if extension == "DIR" and not size_text.strip():
        return Entry(name, "dir", "")
    return Entry(name, "file", size_text)


def restrict(snapshot: Snapshot, names: Sequence[str]) -> Snapshot:
    wanted = set(names)
    return {name: entry for name, entry in snapshot.items() if name in wanted}


# --------------------------------------------------------------------------
# REST
# --------------------------------------------------------------------------


def rest_json(host: str, password: str, method: str, path: str, timeout: float = 15.0) -> dict[str, object]:
    headers = {"X-Password": password} if password else {}
    request = urllib.request.Request(
        # Keyboard injection belongs to the C64-side computer on a cartridge
        # target; see tests/lib/targets.py.
        f"http://{targets.host_for(host, path)}{path}",
        data=b"" if method == "PUT" else None,
        headers=headers,
        method=method,
    )
    with rest_lib.retrying_urlopen(request, timeout) as response:
        return json.loads(response.read().decode("utf-8"))


class RestOracle:
    """Independent file-system reader; never counted as an observer."""

    name = "REST"

    def __init__(self, host: str, password: str, directory: str) -> None:
        self.host = host
        self.password = password
        self.directory = directory.strip("/")

    def info(self, name: str) -> dict[str, object] | None:
        quoted = urllib.parse.quote(f"{self.directory}/{name}")
        try:
            body = rest_json(self.host, self.password, "GET", f"/v1/files/{quoted}:info")
        except urllib.error.HTTPError:
            return None
        except (urllib.error.URLError, OSError) as exc:
            raise Failure(f"REST oracle unreachable: {exc}") from exc
        info = body.get("files")
        return info if isinstance(info, dict) else None

    def snapshot(self, names: Sequence[str]) -> Snapshot:
        result: Snapshot = {}
        for name in names:
            info = self.info(name)
            if info is None:
                continue
            size = int(info.get("size", 0))
            result[name] = Entry(str(info.get("filename", name)), "file", size_to_string_bytes(size), size)
        return result


# --------------------------------------------------------------------------
# Observers
# --------------------------------------------------------------------------


class FtpObserver:
    """Pull observer: it LISTs the fixture directory on every look.

    It holds a session that stays in the fixture directory, so a look costs a
    LIST and nothing else. It is not created at all on a machine that cannot
    afford the socket; see where it is built.
    """

    name = "FTP"

    def __init__(self, host: str, password: str, directory: str) -> None:
        self.ftp = ftp_connect(host, password)
        self.ftp.cwd(directory)
        self.last_raw: list[str] = []

    def close(self) -> None:
        ftp_lib.close(self.ftp)

    def snapshot(self, names: Sequence[str]) -> Snapshot:
        lines = ftp_lib.listing(self.ftp)
        self.last_raw = lines
        wanted = set(names)
        result: Snapshot = {}
        for line in lines:
            fields = line.split(None, 8)
            if len(fields) < 9:
                continue
            name = fields[8]
            if name not in wanted:
                continue
            if line.startswith("d"):
                result[name] = Entry(name, "dir", "")
            else:
                size = int(fields[4])
                result[name] = Entry(name, "file", size_to_string_bytes(size), size)
        return result

    def raw(self) -> str:
        return "\n".join(self.last_raw)


class BrowserObserver:
    """Push observer: a TreeBrowser holding a cached child list."""

    def __init__(self, browser: "FilesystemRefreshBrowser") -> None:
        self.browser = browser
        self.name = browser.name

    def snapshot(self, names: Sequence[str]) -> Snapshot:
        return restrict(self.browser.entries(), names)

    def raw(self) -> str:
        return "\n".join(self.browser.rows())


# --------------------------------------------------------------------------
# Browser driving: one surface, two transports
# --------------------------------------------------------------------------


class FilesystemRefreshBrowser(ui_backend.Browser):
    """Suite-specific browser snapshot parser over the shared UI facade."""

    def __init__(
        self,
        backend: ui_backend.Backend,
        name: str,
        entry_rows: Sequence[int],
        status_row: int,
        width: int,
    ) -> None:
        super().__init__(backend, entry_rows, status_row)
        self.name = name
        self.width = width

    def entries(self) -> Snapshot:
        result: Snapshot = {}
        rows = self.capture().lines
        for index in self.entry_rows:
            entry = parse_browser_row(*strip_browser_frame(rows[index], self.width))
            if entry:
                result[entry.name] = entry
        return result


# --------------------------------------------------------------------------
# FTP helpers
# --------------------------------------------------------------------------


def ftp_connect(host: str, password: str, timeout: float = 30.0) -> ftplib.FTP:
    return ftp_lib.connect(host, password, timeout)


ftp_try = ftp_lib.quietly
ftp_store = ftp_lib.store
remove_tree = ftp_lib.remove_tree


# --------------------------------------------------------------------------
# Matrix bookkeeping
# --------------------------------------------------------------------------


class Matrix:
    def __init__(self) -> None:
        self.cells: list[tuple[str, str, str, str, str]] = []

    def record(self, operation: str, origin: str, observer: str, verdict: str, note: str = "") -> None:
        self.cells.append((operation, origin, observer, verdict, note))

    def report(self) -> str:
        lines = ["", "operation x origin x observer matrix", "-" * 78]
        for operation, origin, observer, verdict, note in self.cells:
            line = f"  {operation:<12} {origin:<7} {observer:<7} {verdict:<5}"
            if note:
                line += f" {note}"
            lines.append(line)
        counts: dict[str, int] = {}
        for _, _, _, verdict, _ in self.cells:
            counts[verdict] = counts.get(verdict, 0) + 1
        lines.append("-" * 78)
        lines.append("  " + ", ".join(f"{verdict}={count}" for verdict, count in sorted(counts.items())))
        return "\n".join(lines)


class Context:
    def __init__(self, args: argparse.Namespace) -> None:
        self.host = args.host
        self.password = args.password
        self.timeout = args.timeout
        self.test_dir = args.test_dir
        self.source_dir = args.test_dir + "s"
        self.fixture_path = f"/Temp/{self.test_dir}"
        self.source_path = f"/Temp/{self.source_dir}"
        self.matrix = Matrix()

        self.session = RestSession(self.host, self.password or None, self.timeout)
        self.menu: FilesystemRefreshBrowser | None = None
        self.telnet: FilesystemRefreshBrowser | None = None
        self.ftp_observer: FtpObserver | None = None
        self.ftp_driver: ftplib.FTP | None = None
        self.oracle = RestOracle(self.host, self.password, f"Temp/{self.test_dir}")

    @property
    def machine(self) -> machine_lib.Machine:
        """Which machine this is, for the rows that need a firmware fix."""
        return machine_lib.identify(
            targets.device_of(self.host),
            lambda: ui_backend.fetch_product(self.host, self.password or None,
                                             self.timeout))

    def observers(self, exclude: Sequence[str] = ()) -> list[object]:
        assert self.menu is not None and self.telnet is not None
        every = [BrowserObserver(self.menu), BrowserObserver(self.telnet)]
        if self.ftp_observer is not None:
            every.append(self.ftp_observer)
        return [observer for observer in every if observer.name not in exclude]

    def converge(
        self,
        operation: str,
        origin: str,
        expected: Snapshot,
        names: Sequence[str],
        exclude: Sequence[str] = (),
        record_passes: bool = True,
    ) -> None:
        """Poll every observer until all of them show `expected`."""
        observers = self.observers(exclude)
        expected_keys = snapshot_keys(expected)
        start = time.monotonic()
        latest: dict[str, Snapshot] = {}
        while True:
            latest = {observer.name: observer.snapshot(names) for observer in observers}
            if all(snapshot_keys(snapshot) == expected_keys for snapshot in latest.values()):
                break
            if time.monotonic() - start >= CONVERGE_TIMEOUT_SECONDS:
                break
            time.sleep(CONVERGE_POLL_SECONDS)
        elapsed = time.monotonic() - start

        oracle = self.oracle.snapshot(names)
        failed: list[object] = []
        for observer in observers:
            snapshot = latest[observer.name]
            problem = ""
            if snapshot_keys(snapshot) != expected_keys:
                problem = format_snapshot(snapshot)
            else:
                # FTP reports exact bytes, so a rounded match must not hide a
                # wrong size.
                for name, entry in expected.items():
                    reported = snapshot.get(name)
                    if reported and reported.exact_size is not None and reported.exact_size != entry.exact_size:
                        problem = f"{name} is {reported.exact_size} bytes, expected {entry.exact_size}"
                        break
            if problem:
                failed.append(observer)
            if problem or record_passes:
                self.matrix.record(operation, origin, observer.name,
                                   "FAIL" if problem else "OK",
                                   problem or f"{elapsed:.2f}s")

        oracle_problem = ""
        for name, entry in expected.items():
            reported = oracle.get(name)
            if reported is None:
                # :info does not distinguish a directory from a file, so a
                # directory is confirmed by existing, a file also by its size.
                oracle_problem = f"{name} missing"
            elif entry.kind == "file" and reported.exact_size != entry.exact_size:
                oracle_problem = f"{name} is {reported.exact_size} bytes, expected {entry.exact_size}"
            if oracle_problem:
                break
        if not oracle_problem:
            for name in sorted(set(names) - set(expected)):
                if self.oracle.info(name) is not None:
                    oracle_problem = f"{name} still present"
                    break
        if oracle_problem or record_passes:
            self.matrix.record(operation, origin, "REST",
                               "FAIL" if oracle_problem else "OK",
                               oracle_problem or f"{elapsed:.2f}s")

        if failed or oracle_problem:
            mismatch = "".join(
                f"  {observer.name:<7}: {format_snapshot(latest[observer.name])}\n" for observer in observers)
            raw = "".join(f"  raw {observer.name}:\n{observer.raw()}\n" for observer in failed)
            raise Failure(
                f"{operation} from {origin} did not converge within {elapsed:.2f}s\n"
                f"  expected: {format_snapshot(expected)}\n{mismatch}"
                f"  REST   : {format_snapshot(oracle)}{' -- ' + oracle_problem if oracle_problem else ''}\n{raw}")

    def baseline(self, expected: Snapshot, names: Sequence[str]) -> None:
        """Every observer must already agree before the mutation under test.

        Only failures are recorded: a green baseline is not a matrix cell, but
        a red one has to show up rather than silently swallow the whole row.
        """
        self.converge("seed", "-", expected, names, record_passes=False)


# --------------------------------------------------------------------------
# Fixtures
# --------------------------------------------------------------------------


def seed_files(ctx: Context, entries: Sequence[tuple[str, int]]) -> None:
    assert ctx.ftp_driver is not None
    for name, size in entries:
        ftp_store(ctx.ftp_driver, f"{ctx.fixture_path}/{name}", b"U" * size)


def drop_names(ctx: Context, names: Sequence[str]) -> None:
    """Remove a row's fixtures, and wait until every observer has seen them go.

    The wait is what makes each row independent. A row ends by deleting its
    fixtures and the next row begins by requiring every observer to already
    agree, so a deletion still working its way through an observer's event
    queue is reported against the next row's seed rather than this one's
    teardown. Observed on an Ultimate II+L in a full run: the row that
    deliberately floods the observers with ten extra files left the Telnet
    browser still showing that row's renamed file, and the next row's baseline
    failed with a name it had never heard of.
    """
    assert ctx.ftp_driver is not None
    ftp = ctx.ftp_driver
    for name in names:
        target = f"{ctx.fixture_path}/{name}"
        try:
            ftp.delete(target)
        except ftplib.all_errors:
            # RMD alone leaves a directory with contents in place, and the
            # rows that rename or move a directory put a file inside it. The
            # failure was silent until the wait below started checking, and
            # the directory stayed on the device for the rest of the run.
            remove_tree(ftp, target)
    ctx.converge("cleanup", "-", expected_snapshot([]), names, record_passes=False)


# --------------------------------------------------------------------------
# Matrix rows
# --------------------------------------------------------------------------


def row_rename_browser(ctx: Context, browser: FilesystemRefreshBrowser, origin: str, old: str, new: str) -> None:
    names = [old, new]
    seed_files(ctx, [(old, SIZE_S1)])
    ctx.baseline(expected_snapshot([(old, SIZE_S1)]), names)

    browser.select_entry(old)
    browser.invoke_context_action("Rename")
    browser.wait_for_text("Give a new name..")
    browser.fill_edit_field(new, clear_taps=len(old) + 4)
    browser.wait_until_gone("Give a new name..")

    ctx.converge("rename", origin, expected_snapshot([(new, SIZE_S1)]), names)
    drop_names(ctx, names)


def row_rename_ftp(ctx: Context, old: str, new: str) -> None:
    assert ctx.ftp_driver is not None
    names = [old, new]
    seed_files(ctx, [(old, SIZE_S1)])
    ctx.baseline(expected_snapshot([(old, SIZE_S1)]), names)

    ctx.ftp_driver.rename(f"{ctx.fixture_path}/{old}", f"{ctx.fixture_path}/{new}")

    ctx.converge("rename", "FTP", expected_snapshot([(new, SIZE_S1)]), names)
    drop_names(ctx, names)


# Long enough for a browser to drain a queue's worth of events at its poll
# rate, short enough that it is not a wait anyone notices. Only the two rows
# that deliberately overflow the queue pay it.
QUEUE_DRAIN_SECONDS = 0.6


def row_rename_under_event_pressure(ctx: Context, browser: FilesystemRefreshBrowser, origin: str,
                                    old: str, new: str, noise: Sequence[str]) -> None:
    """Rename while the observer queue is being filled behind the context menu.

    TreeBrowser::poll() returns from its `contextMenu` branch before it reaches
    checkFileManagerEvent(), so a browser drains no events at all for as long as
    a context menu and its string box are up. The queue holds 8 (observer.h:27)
    and putEvent() drops what does not fit, so a busy device can lose the
    notification for the very rename being typed - and the listing then stays
    stale until the directory is left and re-entered.

    Here the pressure is applied deliberately instead of waiting for background
    traffic: the noise files are created over FTP while the rename box is open.
    """
    assert ctx.ftp_driver is not None
    names = [old, new]
    seed_files(ctx, [(old, SIZE_S1)])
    ctx.baseline(expected_snapshot([(old, SIZE_S1)]), names)

    browser.select_entry(old)
    browser.invoke_context_action("Rename")
    browser.wait_for_text("Give a new name..")

    # More events than the queue can hold, all while the browser is not draining.
    # They are raised in the *sibling* directory on purpose: an event for the
    # watched directory would refresh it by itself and mask the very drop this
    # row is trying to catch.
    for name in noise:
        ftp_store(ctx.ftp_driver, f"{ctx.source_path}/{name}", b"N" * SIZE_S1)

    browser.fill_edit_field(new, clear_taps=len(old) + 4)
    browser.wait_until_gone("Give a new name..")

    try:
        ctx.converge("rename-under-load", origin, expected_snapshot([(new, SIZE_S1)]), names)
    finally:
        for name in noise:
            ftp_try(lambda n=name: ctx.ftp_driver.delete(f"{ctx.source_path}/{n}"))
        # Removing the noise raises one event per file, and the queue holds 8
        # (observer.h:27), so the two deletions drop_names is about to make can
        # be the ones putEvent() discards. A browser would then still show this
        # row's file and the next row's baseline would fail on a name it had
        # never heard of. Waiting lets the browsers drain what the noise
        # raised, so the teardown's own deletions fit.
        time.sleep(QUEUE_DRAIN_SECONDS)
    drop_names(ctx, names)


def row_rename_dir_browser(ctx: Context, browser: FilesystemRefreshBrowser, origin: str, old: str, new: str) -> None:
    """Rename a directory, not a file: the entry keeps no size but must still move."""
    assert ctx.ftp_driver is not None
    names = [old, new]
    ctx.ftp_driver.mkd(f"{ctx.fixture_path}/{old}")
    ftp_store(ctx.ftp_driver, f"{ctx.fixture_path}/{old}/inner.tst", b"I" * SIZE_S1)
    ctx.baseline(expected_snapshot([(old, None)]), names)

    browser.select_entry(old)
    browser.invoke_context_action("Rename")
    browser.wait_for_text("Give a new name..")
    browser.fill_edit_field(new, clear_taps=len(old) + 4)
    browser.wait_until_gone("Give a new name..")

    ctx.converge("rename-dir", origin, expected_snapshot([(new, None)]), names)
    drop_names(ctx, names)


def row_delete_dir_browser(ctx: Context, browser: FilesystemRefreshBrowser, origin: str, name: str) -> None:
    """Delete a non-empty directory, which goes through delete_recursive."""
    assert ctx.ftp_driver is not None
    names = [name]
    ctx.ftp_driver.mkd(f"{ctx.fixture_path}/{name}")
    ftp_store(ctx.ftp_driver, f"{ctx.fixture_path}/{name}/inner.tst", b"I" * SIZE_S1)
    ctx.baseline(expected_snapshot([(name, None)]), names)

    browser.select_entry(name)
    browser.invoke_context_action("Delete")
    browser.wait_for_text("Are you sure?")
    browser.press_popup_button("y")

    ctx.converge("delete-dir", origin, expected_snapshot([]), names)


def row_delete_browser(ctx: Context, browser: FilesystemRefreshBrowser, origin: str, name: str) -> None:
    names = [name]
    seed_files(ctx, [(name, SIZE_S1)])
    ctx.baseline(expected_snapshot([(name, SIZE_S1)]), names)

    browser.select_entry(name)
    browser.invoke_context_action("Delete")
    browser.wait_for_text("Are you sure?")
    browser.press_popup_button("y")

    ctx.converge("delete", origin, expected_snapshot([]), names)
    # The browser has to stay in the directory and keep a valid selection after
    # the entry it was sitting on disappeared. An emptied directory legitimately
    # has no selection and says so.
    expected_path = f"{ctx.fixture_path}/"
    if browser.current_path() != expected_path:
        raise Failure(f"{browser.name} left {expected_path!r} after the delete, "
                      f"it is now at {browser.current_path()!r}")
    if EMPTY_DIRECTORY_MARKER not in browser.screen():
        browser.selected_row()


def row_delete_ftp(ctx: Context, name: str) -> None:
    assert ctx.ftp_driver is not None
    names = [name]
    seed_files(ctx, [(name, SIZE_S1)])
    ctx.baseline(expected_snapshot([(name, SIZE_S1)]), names)

    ctx.ftp_driver.delete(f"{ctx.fixture_path}/{name}")

    ctx.converge("delete", "FTP", expected_snapshot([]), names)


def row_create_dir_browser(ctx: Context, browser: FilesystemRefreshBrowser, origin: str, name: str) -> None:
    """Create > Directory adds an entry to the directory the browser is in."""
    names = [name]
    ctx.baseline(expected_snapshot([]), names)

    browser.invoke_task_action("Create", "Directory")
    browser.wait_for_text("Give name for new directory..")
    browser.fill_edit_field(name)
    browser.wait_until_gone("Give name for new directory..")

    ctx.converge("create", origin, expected_snapshot([(name, None)]), names)
    drop_names(ctx, names)


def row_create_ftp(ctx: Context, name: str) -> None:
    """FTP STOR of a new file, driven phase by phase.

    storbinary() would hide the phases, so the data connection is opened by
    hand. The create notification fires when the file is opened, long before
    the size is committed by the writable close.
    """
    assert ctx.ftp_driver is not None
    names = [name]
    ctx.baseline(expected_snapshot([]), names)

    data_socket = ctx.ftp_driver.transfercmd(f"STOR {ctx.fixture_path}/{name}")
    try:
        # Phase 1: create notification; nothing has been written yet.
        time.sleep(0.4)
        during_open = {observer.name: observer.snapshot(names) for observer in ctx.observers()}
        # Phase 2: data transfer.
        data_socket.sendall(b"U" * SIZE_S2)
    finally:
        # Phase 3: writable close.
        data_socket.close()
    ctx.ftp_driver.voidresp()

    detail("STOR phase 1 (open, 0 bytes committed): "
           + "; ".join(f"{who}={format_snapshot(what)}"
                       for who, what in during_open.items()))

    # Phase 4: final metadata notification.
    ctx.converge("create", "FTP", expected_snapshot([(name, SIZE_S2)]), names)
    drop_names(ctx, names)


def row_create_mkd(ctx: Context, name: str) -> None:
    """FTP MKD, addressed absolutely the way a client normally addresses it."""
    assert ctx.ftp_driver is not None
    names = [name]
    ctx.baseline(expected_snapshot([]), names)

    ctx.ftp_driver.mkd(f"{ctx.fixture_path}/{name}")

    ctx.converge("create", "FTP/mkd", expected_snapshot([(name, None)]), names)
    drop_names(ctx, names)


def row_create_rest(ctx: Context, name: str, endpoint: str) -> None:
    names = [name]
    ctx.baseline(expected_snapshot([]), names)

    quoted = urllib.parse.quote(f"Temp/{ctx.test_dir}/{name}")
    query = "diskname=ZBFR" + ("&tracks=4" if endpoint == "create_dnp" else "")
    body = rest_json(ctx.host, ctx.password, "PUT", f"/v1/files/{quoted}:{endpoint}?{query}")
    if body.get("errors"):
        raise Failure(f"REST {endpoint} failed: {body}")

    origin = "REST/" + endpoint.split("_")[1]
    ctx.converge("create", origin, expected_snapshot([(name, REST_IMAGE_SIZES[endpoint])]), names)
    drop_names(ctx, names)


def row_write_browser(ctx: Context, browser: FilesystemRefreshBrowser, origin: str, disk_name: str) -> None:
    """Create > D64 Image writes 174848 bytes into the directory being watched.

    It is the writable-close case that keeps the origin browser in place: the
    file is created empty (and notified), then filled, then committed on close.
    """
    name = f"{disk_name}.d64"
    names = [name]
    ctx.baseline(expected_snapshot([]), names)

    browser.invoke_task_action("Create", "D64 Image")
    browser.wait_for_text("Give name for new disk..")
    browser.fill_edit_field(disk_name)
    browser.wait_until_gone("Give name for new disk..")
    browser.wait_until_gone("Creating...")

    ctx.converge("write", origin, expected_snapshot([(name, SIZE_D64)]), names)
    drop_names(ctx, names)


def row_write_ftp(ctx: Context, name: str) -> None:
    """FTP STOR over an existing name: S1 must become S2 and never stay at S1."""
    assert ctx.ftp_driver is not None
    names = [name]
    seed_files(ctx, [(name, SIZE_S1)])
    ctx.baseline(expected_snapshot([(name, SIZE_S1)]), names)

    data_socket = ctx.ftp_driver.transfercmd(f"STOR {ctx.fixture_path}/{name}")
    try:
        time.sleep(0.4)
        during_open = {observer.name: observer.snapshot(names) for observer in ctx.observers()}
        data_socket.sendall(b"W" * SIZE_S2)
    finally:
        data_socket.close()
    ctx.ftp_driver.voidresp()

    detail("STOR phase 1 (open, truncated): "
           + "; ".join(f"{who}={format_snapshot(what)}"
                       for who, what in during_open.items()))

    ctx.converge("write", "FTP", expected_snapshot([(name, SIZE_S2)]), names)
    drop_names(ctx, names)


def row_copy_browser(ctx: Context, browser: FilesystemRefreshBrowser, origin: str, name: str) -> None:
    """Copy to... over an existing name, answering 'File exists. Overwrite?'.

    S_copyTo keeps the source filename and picks the destination through an
    interactive path browser, so the origin browser has to stand on the source
    entry in another directory and cannot also observe the destination without
    navigating. It is left out of this row and recorded as not applicable; the
    other two observers stay in the fixture directory throughout.
    """
    assert ctx.ftp_driver is not None
    names = [name]
    ftp_store(ctx.ftp_driver, f"{ctx.source_path}/{name}", b"C" * SIZE_S2)
    seed_files(ctx, [(name, SIZE_S1)])
    ctx.baseline(expected_snapshot([(name, SIZE_S1)]), names)

    browser.go_to_directory(f"Temp/{ctx.source_dir}")
    browser.select_entry(name)
    browser.invoke_context_action("Copy to...")
    browser.pick_directory(f"Temp/{ctx.test_dir}", PICKER_TITLE, PICKER_SELECT_ENTRY)
    browser.wait_for_text("File exists. Overwrite?")
    browser.press_popup_button("y")
    browser.wait_for_text("Copy complete.")
    browser.press_popup_button("o")

    ctx.matrix.record("copy-write", origin, origin, "N/A",
                      "origin stands on the source entry elsewhere; see arrival check + paste-write")
    try:
        ctx.converge("copy-write", origin, expected_snapshot([(name, SIZE_S2)]), names, exclude=[origin])
    finally:
        browser.recover_to(f"Temp/{ctx.test_dir}")

    # The origin browser could not observe the destination while it stood on the
    # source, but it had the destination cached from before it left. Walking back
    # in must not resurrect that cache.
    arrival = restrict(browser.entries(), names)
    expected = expected_snapshot([(name, SIZE_S2)])
    if snapshot_keys(arrival) != snapshot_keys(expected):
        ctx.matrix.record("copy-arrival", origin, origin, "FAIL", format_snapshot(arrival))
        raise Failure(
            f"{origin} showed a stale destination on arrival after its own copy\n"
            f"  expected: {format_snapshot(expected)}\n"
            f"  {origin:<7}: {format_snapshot(arrival)}\n{browser.screen()}")
    ctx.matrix.record("copy-arrival", origin, origin, "OK", "no stale cache on re-entry")

    drop_names(ctx, names)
    ftp_try(lambda: ctx.ftp_driver.delete(f"{ctx.source_path}/{name}"))


def row_move_out_browser(ctx: Context, browser: FilesystemRefreshBrowser, origin: str, name: str) -> None:
    """Move to... an entry out of the directory every observer is watching.

    Unlike Copy to..., the source is the watched directory, so the browser that
    starts the move is standing in it and is one of the three observers. It also
    exercises the four-argument FileManager::rename overload that S_moveTo uses
    for a same-filesystem move.
    """
    assert ctx.ftp_driver is not None
    names = [name]
    seed_files(ctx, [(name, SIZE_S2)])
    ctx.baseline(expected_snapshot([(name, SIZE_S2)]), names)

    browser.select_entry(name)
    browser.invoke_context_action("Move to...")
    browser.pick_directory(f"Temp/{ctx.source_dir}", PICKER_TITLE, PICKER_SELECT_ENTRY)
    browser.wait_for_text("Move complete.")
    browser.press_popup_button("o")

    try:
        ctx.converge("move-out", origin, expected_snapshot([]), names)
    finally:
        browser.recover_to(f"Temp/{ctx.test_dir}")
    ftp_try(lambda: ctx.ftp_driver.delete(f"{ctx.source_path}/{name}"))


def row_bulk_delete_browser(ctx: Context, browser: FilesystemRefreshBrowser, origin: str, names: Sequence[str]) -> None:
    """Select all, then SHIFT+INST/DEL: TreeBrowser::delete_selected on many entries."""
    seed_files(ctx, [(name, SIZE_S1) for name in names])
    ctx.baseline(expected_snapshot([(name, SIZE_S1) for name in names]), names)

    browser.press("SELECT_ALL")
    browser.press("SHIFT_DEL")
    browser.wait_for_text("Delete ")
    browser.press_popup_button("y")
    browser.wait_until_gone("Deleting...")

    ctx.converge("delete-bulk", origin, expected_snapshot([]), names)


def row_rmdir_ftp(ctx: Context, name: str) -> None:
    """FTP RMD, the one mutating command the matrix did not exercise."""
    assert ctx.ftp_driver is not None
    names = [name]
    ctx.ftp_driver.mkd(f"{ctx.fixture_path}/{name}")
    ctx.baseline(expected_snapshot([(name, None)]), names)

    ctx.ftp_driver.rmd(f"{ctx.fixture_path}/{name}")

    ctx.converge("delete-dir", "FTP/rmd", expected_snapshot([]), names)


def row_paste_browser(ctx: Context, browser: FilesystemRefreshBrowser, origin: str, name: str) -> None:
    """Ctrl-C in the source directory, Ctrl-V in the directory being watched.

    Unlike Copy to..., paste writes into the directory the browser is already
    standing in, so here the browser that starts the copy is also one of the
    three observers. The clipboard is filled before the baseline is taken, so
    at the moment of the mutation all three observers are in the fixture
    directory and none of them moves.
    """
    assert ctx.ftp_driver is not None
    names = [name]
    ftp_store(ctx.ftp_driver, f"{ctx.source_path}/{name}", b"P" * SIZE_S2)

    browser.go_to_directory(f"Temp/{ctx.source_dir}")
    browser.select_entry(name)
    browser.press("COPY")
    browser.wait_for_text("clipboard")
    browser.press_popup_button("o")
    browser.go_to_directory(f"Temp/{ctx.test_dir}")

    ctx.baseline(expected_snapshot([]), names)

    browser.press("PASTE")
    browser.wait_until_gone("Copying...")

    ctx.converge("paste-write", origin, expected_snapshot([(name, SIZE_S2)]), names)
    drop_names(ctx, names)
    ftp_try(lambda: ctx.ftp_driver.delete(f"{ctx.source_path}/{name}"))


# ---- failure paths -------------------------------------------------------


def row_failed_rename(ctx: Context, existing: str, blocker: str) -> None:
    assert ctx.ftp_driver is not None
    names = [existing, blocker]
    seed_files(ctx, [(existing, SIZE_S1), (blocker, SIZE_S2)])
    expected = expected_snapshot([(existing, SIZE_S1), (blocker, SIZE_S2)])
    ctx.baseline(expected, names)

    try:
        ctx.ftp_driver.rename(f"{ctx.fixture_path}/{existing}", f"{ctx.fixture_path}/{blocker}")
    except ftplib.all_errors:
        pass
    else:
        raise Failure(f"renaming {existing!r} onto the existing {blocker!r} was accepted")

    ctx.converge("rename-fail", "FTP", expected, names)
    drop_names(ctx, names)


def row_failed_delete(ctx: Context, present: str, absent: str) -> None:
    assert ctx.ftp_driver is not None
    names = [present, absent]
    seed_files(ctx, [(present, SIZE_S1)])
    expected = expected_snapshot([(present, SIZE_S1)])
    ctx.baseline(expected, names)

    try:
        ctx.ftp_driver.delete(f"{ctx.fixture_path}/{absent}")
    except ftplib.all_errors:
        pass
    else:
        raise Failure(f"deleting the absent {absent!r} was accepted")

    ctx.converge("delete-fail", "FTP", expected, names)
    drop_names(ctx, names)


def row_failed_write(ctx: Context, blocking_dir: str) -> None:
    """A STOR that cannot open its destination must leave nothing behind."""
    assert ctx.ftp_driver is not None
    names = [blocking_dir]
    ctx.ftp_driver.mkd(f"{ctx.fixture_path}/{blocking_dir}")
    expected = expected_snapshot([(blocking_dir, None)])
    ctx.baseline(expected, names)

    try:
        ftp_store(ctx.ftp_driver, f"{ctx.fixture_path}/{blocking_dir}", b"X" * SIZE_S1)
    except (*ftplib.all_errors, Failure):
        # The refusal is the point of this row; ftp.store reports it as Failure.
        pass
    else:
        raise Failure(f"STOR over the directory {blocking_dir!r} was accepted")

    ctx.converge("write-fail", "FTP", expected, names)
    drop_names(ctx, names)


def row_short_write(ctx: Context, name: str) -> None:
    """A transfer cut short commits what it wrote; record what that turns out to be."""
    assert ctx.ftp_driver is not None
    names = [name]
    seed_files(ctx, [(name, SIZE_S2)])
    ctx.baseline(expected_snapshot([(name, SIZE_S2)]), names)

    data_socket = ctx.ftp_driver.transfercmd(f"STOR {ctx.fixture_path}/{name}")
    try:
        data_socket.sendall(b"S" * SIZE_S1)
    finally:
        data_socket.close()
    ctx.ftp_driver.voidresp()

    committed = ctx.oracle.info(name)
    if committed is None:
        raise Failure("a short write left no file at all")
    size = int(committed.get("size", -1))
    detail(f"short write committed {size} bytes")
    ctx.converge("short-write", "FTP", expected_snapshot([(name, size)]), names)
    drop_names(ctx, names)


# --------------------------------------------------------------------------
# Setup and teardown
# --------------------------------------------------------------------------


# The rows that cannot converge without a given firmware fix. Kept beside the
# rows themselves so a label renamed below is renamed here too.
ROWS_NEEDING_FIX = {
    # These three hold the FTP data connection open on purpose and look through
    # every observer while it is open, because the create notification fires
    # when the file is opened and the size only when it is closed. That needs a
    # fourth socket: the Telnet session, the FTP control, the FTP data
    # connection, and then whatever the observer looks through. A C64 Ultimate
    # serves three across Telnet and FTP, and the observer's own read is what
    # the device resets. See machine.SERVES_FOUR_TELNET_FTP_SOCKETS.
    machine_lib.SERVES_FOUR_TELNET_FTP_SOCKETS: (
        "create from FTP",
        "write from FTP",
        "short write commits consistently",
    ),
    machine_lib.BROWSER_REFRESH_AFTER_QUEUE_OVERFLOW: (
        "rename under observer-queue pressure from the Menu",
        "rename under observer-queue pressure from Telnet",
    ),
    machine_lib.BROWSER_REFRESH_ON_DIRECTORY_CHANGE: (
        "rename a directory from the Menu",
        "rename a directory from Telnet",
        "delete a non-empty directory from the Menu",
        "delete a non-empty directory from Telnet",
        "remove a directory from FTP",
        "create from FTP (mkd)",
        "failed write creates nothing",
    ),
    machine_lib.BROWSER_REFRESH_FROM_TELNET_WRITER: (
        "write from Telnet",
        "copy over an existing file from Telnet",
        "paste into the watched directory from Telnet",
    ),
    machine_lib.BROWSER_REFRESH_FROM_MENU_WRITER: (
        "write from the Menu",
        "copy over an existing file from the Menu",
        "paste into the watched directory from the Menu",
    ),
}


def build_rows(ctx: "Context") -> list[tuple[str, Callable[[], None], Sequence[str]]]:
    """Every matrix row, in run order. Labels are what -r/--row matches on."""
    return [
        ("rename from the Menu",
         lambda: row_rename_browser(ctx, ctx.menu, "Menu", "rmenu1.tst", "rmenu2.tst"),
         ["rmenu1.tst", "rmenu2.tst"]),
        ("rename from Telnet",
         lambda: row_rename_browser(ctx, ctx.telnet, "Telnet", "rtel1.tst", "rtel2.tst"),
         ["rtel1.tst", "rtel2.tst"]),
        ("rename from FTP",
         lambda: row_rename_ftp(ctx, "rftp1.tst", "rftp2.tst"),
         ["rftp1.tst", "rftp2.tst"]),

        ("rename under observer-queue pressure from the Menu",
         lambda: row_rename_under_event_pressure(
             ctx, ctx.menu, "Menu", "qmenu1.tst", "qmenu2.tst",
             [f"qn{i}.tst" for i in range(10)]),
         ["qmenu1.tst", "qmenu2.tst"]),
        ("rename under observer-queue pressure from Telnet",
         lambda: row_rename_under_event_pressure(
             ctx, ctx.telnet, "Telnet", "qtel1.tst", "qtel2.tst",
             [f"qt{i}.tst" for i in range(10)]),
         ["qtel1.tst", "qtel2.tst"]),

        ("rename a directory from the Menu",
         lambda: row_rename_dir_browser(ctx, ctx.menu, "Menu", "rdmenu1", "rdmenu2"),
         ["rdmenu1", "rdmenu2"]),
        ("rename a directory from Telnet",
         lambda: row_rename_dir_browser(ctx, ctx.telnet, "Telnet", "rdtel1", "rdtel2"),
         ["rdtel1", "rdtel2"]),

        ("delete from the Menu",
         lambda: row_delete_browser(ctx, ctx.menu, "Menu", "dmenu1.tst"), ["dmenu1.tst"]),
        ("delete from Telnet",
         lambda: row_delete_browser(ctx, ctx.telnet, "Telnet", "dtel1.tst"), ["dtel1.tst"]),
        ("delete from FTP",
         lambda: row_delete_ftp(ctx, "dftp1.tst"), ["dftp1.tst"]),
        ("delete a non-empty directory from the Menu",
         lambda: row_delete_dir_browser(ctx, ctx.menu, "Menu", "ddmenu1"), ["ddmenu1"]),
        ("delete a non-empty directory from Telnet",
         lambda: row_delete_dir_browser(ctx, ctx.telnet, "Telnet", "ddtel1"), ["ddtel1"]),
        ("remove a directory from FTP",
         lambda: row_rmdir_ftp(ctx, "dftp2"), ["dftp2"]),
        ("select all and bulk delete from the Menu",
         lambda: row_bulk_delete_browser(ctx, ctx.menu, "Menu",
                                         ["bmenu1.tst", "bmenu2.tst", "bmenu3.tst"]),
         ["bmenu1.tst", "bmenu2.tst", "bmenu3.tst"]),

        ("create from the Menu",
         lambda: row_create_dir_browser(ctx, ctx.menu, "Menu", "cmenu1"), ["cmenu1"]),
        ("create from Telnet",
         lambda: row_create_dir_browser(ctx, ctx.telnet, "Telnet", "ctel1"), ["ctel1"]),
        ("create from FTP",
         lambda: row_create_ftp(ctx, "cftp1.tst"), ["cftp1.tst"]),
        ("create from FTP (mkd)",
         lambda: row_create_mkd(ctx, "cftp2"), ["cftp2"]),
        ("create from REST (d64)",
         lambda: row_create_rest(ctx, "crest1.d64", "create_d64"), ["crest1.d64"]),
        ("create from REST (d71)",
         lambda: row_create_rest(ctx, "crest2.d71", "create_d71"), ["crest2.d71"]),
        ("create from REST (d81)",
         lambda: row_create_rest(ctx, "crest3.d81", "create_d81"), ["crest3.d81"]),
        ("create from REST (dnp)",
         lambda: row_create_rest(ctx, "crest4.dnp", "create_dnp"), ["crest4.dnp"]),

        ("write from the Menu",
         lambda: row_write_browser(ctx, ctx.menu, "Menu", "wmenu1"), ["wmenu1.d64"]),
        ("write from Telnet",
         lambda: row_write_browser(ctx, ctx.telnet, "Telnet", "wtel1"), ["wtel1.d64"]),
        ("write from FTP",
         lambda: row_write_ftp(ctx, "wftp1.tst"), ["wftp1.tst"]),

        ("copy over an existing file from the Menu",
         lambda: row_copy_browser(ctx, ctx.menu, "Menu", "pmenu1.tst"), ["pmenu1.tst"]),
        ("copy over an existing file from Telnet",
         lambda: row_copy_browser(ctx, ctx.telnet, "Telnet", "ptel1.tst"), ["ptel1.tst"]),

        ("move an entry out of the watched directory from the Menu",
         lambda: row_move_out_browser(ctx, ctx.menu, "Menu", "mmenu1.tst"), ["mmenu1.tst"]),
        ("move an entry out of the watched directory from Telnet",
         lambda: row_move_out_browser(ctx, ctx.telnet, "Telnet", "mtel1.tst"), ["mtel1.tst"]),

        ("paste into the watched directory from the Menu",
         lambda: row_paste_browser(ctx, ctx.menu, "Menu", "vmenu1.tst"), ["vmenu1.tst"]),
        ("paste into the watched directory from Telnet",
         lambda: row_paste_browser(ctx, ctx.telnet, "Telnet", "vtel1.tst"), ["vtel1.tst"]),

        ("failed rename shows nothing",
         lambda: row_failed_rename(ctx, "frn1.tst", "frn2.tst"), ["frn1.tst", "frn2.tst"]),
        ("failed delete removes nothing",
         lambda: row_failed_delete(ctx, "fdl1.tst", "fdl0.tst"), ["fdl1.tst"]),
        ("failed write creates nothing",
         lambda: row_failed_write(ctx, "fwr1"), ["fwr1"]),
        ("short write commits consistently",
         lambda: row_short_write(ctx, "fsw1.tst"), ["fsw1.tst"]),
    ]


def run_row(ctx: Context, label: str, action: Callable[[], None], fixtures: Sequence[str]) -> Failure | None:
    """Run one matrix row, then clean up and resync whatever it left behind.

    A red cell must not hide the cells after it: the whole matrix is measured
    in one pass and the run only reports FAIL at the end.
    """
    problem: Failure | None = None
    try:
        with check(label):
            action()
    except Failure as exc:
        problem = exc
        print(f"     {exc}")
    finally:
        try:
            drop_names(ctx, fixtures)
        except Exception:
            pass
    if problem is not None:
        resync_browsers(ctx)
    return problem


def resync_browsers(ctx: Context) -> None:
    """Put both browsers back in the fixture directory after a failed row."""
    assert ctx.telnet is not None
    for browser in (ctx.menu, ctx.telnet):
        browser.recover_to(f"Temp/{ctx.test_dir}")


def default_test_dir() -> str:
    return f"{TEST_DIR_PREFIX}{int(time.time()) % 1000000:06d}{os.getpid() % 1000:03d}"


def prepare_device(ctx: Context) -> None:
    ftp = ftp_connect(ctx.host, ctx.password)
    try:
        remove_tree(ftp, ctx.fixture_path)
        remove_tree(ftp, ctx.source_path)
        ftp.mkd(ctx.fixture_path)
        ftp.mkd(ctx.source_path)
    finally:
        ftp_try(ftp.quit)


def cleanup_device(ctx: Context) -> None:
    try:
        ftp = ftp_connect(ctx.host, ctx.password)
    except Exception:
        return
    try:
        remove_tree(ftp, ctx.fixture_path)
        remove_tree(ftp, ctx.source_path)
    finally:
        ftp_try(ftp.quit)


def open_observers(ctx: Context) -> None:
    # Always start from a closed menu: a half-finished action can leave a
    # nested screen on the UI stack that looks enough like the browser to
    # silently swallow the whole run.
    ctx.session.close_menu_from_anywhere()

    # RestBackend opens the menu from its constructor, so it is built only
    # after the stack above has been unwound. interface_type=None leaves User
    # Interface Settings / Interface Type alone: this suite watches one
    # directory through the menu, a Telnet session and FTP at the same time,
    # and it has never owned that setting.
    ctx.menu = FilesystemRefreshBrowser(
        ui_backend.RestBackend(
            ctx.host,
            ctx.password or None,
            ctx.timeout,
            interface_type=None,
        ),
        "Menu",
        MENU_ENTRY_ROWS,
        MENU_STATUS_ROW,
        SCREEN_WIDTH,
    )
    ctx.menu.go_to_directory(f"Temp/{ctx.test_dir}")

    ctx.telnet = FilesystemRefreshBrowser(
        ui_backend.TelnetBackend(
            # Telnet is a session on the device itself, so a cartridge target
            # connects to the cartridge; see tests/lib/targets.py.
            targets.device_of(ctx.host),
            23,
            ctx.password,
            ctx.timeout,
            width=TELNET_WIDTH,
            height=TELNET_HEIGHT,
        ),
        "Telnet",
        TELNET_ENTRY_ROWS,
        TELNET_STATUS_ROW,
        TELNET_WIDTH,
    )
    ctx.telnet.go_to_directory(f"Temp/{ctx.test_dir}")

    ctx.ftp_driver = ftp_connect(ctx.host, ctx.password)
    if ctx.machine.missing_fix(machine_lib.SERVES_FOUR_TELNET_FTP_SOCKETS):
        # Three sockets is exactly what a Telnet session and one FTP transfer
        # need, so watching the directory over FTP as well leaves no margin:
        # any socket the device has not finished releasing makes the next one
        # the fourth, and it is reset. The rows still run and are still
        # checked, by the Menu, by Telnet and by the REST oracle, which is the
        # one that says what actually committed. What is lost on this machine
        # is the FTP column of the matrix, not the rows.
        ctx.ftp_observer = None
        detail(f"not watching over FTP: {ctx.machine.kind} serves three "
               "concurrent Telnet and FTP sockets, and a Telnet session plus "
               "one transfer already needs all three")
    else:
        ctx.ftp_observer = FtpObserver(ctx.host, ctx.password, ctx.fixture_path)


def close_observers(ctx: Context) -> None:
    if ctx.ftp_driver is not None:
        ftp_try(ctx.ftp_driver.quit)
    if ctx.ftp_observer is not None:
        ctx.ftp_observer.close()
    if ctx.telnet is not None:
        ctx.telnet.close()
    try:
        ctx.menu.close()
    except Exception:
        pass


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify that every browser converges on the committed file system.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument(
        "-p",
        "--password",
        default=os.environ.get("U64_PASS", ""),
    )
    parser.add_argument(
        "-t",
        "--timeout",
        type=float,
        # 30s, matching what run-tests passes. It was 5s, which is below
        # pacing.TELNET_SETTLE_GAP_SECONDS: a committed Telnet prompt is
        # settled by waiting six seconds of quiet, so every Telnet send_text
        # in this suite timed out before it could succeed. That made the suite
        # unrunnable by hand while passing under the runner, which is the worst
        # way for a default to be wrong.
        default=float(os.environ.get("U64_TIMEOUT", "30.0")),
    )
    parser.add_argument("--test-dir", default=default_test_dir())
    parser.add_argument(
        "-r",
        "--row",
        action="append",
        default=[],
        metavar="TEXT",
        help="run only rows whose label contains TEXT (case insensitive). Repeatable.")
    parser.add_argument("--list-rows", action="store_true", help="list the row labels and exit.")
    args = parser.parse_args()

    if args.list_rows:
        for label, _, _ in build_rows(Context(args)):
            print(label)
        return 0

    ctx = Context(args)
    try:
        with check("reset the machine to a clean starting state"):
            ctx.session.reset_to_clean_slate()
        with check("prepare fixture directories"):
            prepare_device(ctx)
        with check("open Menu, Telnet and FTP on the fixture directory"):
            open_observers(ctx)
        assert ctx.telnet is not None

        rows = build_rows(ctx)

        # A row whose browser cannot recover from a dropped event leaves that
        # browser stale for the rest of the run, so every later row compares
        # against a listing that never caught up. Skipped rather than failed
        # where the firmware lacks the fix, because one failure there costs
        # twelve.
        # Named one by one rather than matched on the label, because the row
        # that needs a fix is not always the row that says so: "failed write
        # creates nothing" blocks its STOR with a directory it creates first,
        # so it needs the directory notification like the rows that say
        # "directory" in their name.
        for fix, labels in ROWS_NEEDING_FIX.items():
            if not ctx.machine.missing_fix(fix):
                continue
            for label in labels:
                if any(row[0] == label for row in rows):
                    ctx.machine.skip_without_fix(fix, label)
            rows = [row for row in rows if row[0] not in labels]

        if args.row:
            wanted = [text.lower() for text in args.row]
            rows = [row for row in rows if any(text in row[0].lower() for text in wanted)]
            if not rows:
                raise Failure(f"no row label matches {args.row}; use --list-rows to see them")
            print(f"running {len(rows)} selected row(s)")

        problems = [problem for problem in
                    (run_row(ctx, label, action, fixtures) for label, action, fixtures in rows)
                    if problem is not None]

        if problems:
            raise Failure(f"{len(problems)} of {len(rows)} matrix rows did not converge; "
                          f"first was: {problems[0]}")
        print(ctx.matrix.report())
        suite_ok("browser_filesystem_refresh_test")
        return 0
    except Failure:
        print(ctx.matrix.report())
        raise
    finally:
        close_observers(ctx)
        cleanup_device(ctx)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("browser_filesystem_refresh_test", str(exc))
        raise SystemExit(1)
