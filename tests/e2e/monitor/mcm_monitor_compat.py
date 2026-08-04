"""Narrow bridge from the Debug scenarios to the shared monitor fixtures.

``monitor_debug_test.py`` has a large set of debugger scenarios that were
written before the common E2E reporting and UI backend existed.  Keeping this
bridge local to that suite lets those scenarios use the current
``MonitorSession``, REST helpers and report format without adding legacy
policy back into ``monitor_test.py``.
"""

from __future__ import annotations

import os
import sys
from contextlib import contextmanager
from dataclasses import dataclass, field
from typing import Iterator, Optional

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "lib"))
sys.path.insert(0, os.path.join(HERE, "..", "lib"))

import monitor_test as core
from api import UltimateApi
from report import Failure, check_fail, check_ok, check_skip, check_start, detail
from ui_backend import MODE_TELNET, Snapshot, add_mode_argument, make_backend

# The Debug scenarios inspect the debugger footer, whose original Telnet
# fixture is a 24-row session.  The suite is intentionally Telnet-only: its
# natural-exit liveness checks must observe the C64 without an Overlay/Freeze
# UI owning the live screen.
HEIGHT = 24
STATUS_LINE_RE = core.STATUS_LINE_RE
find_status_line = core.find_status_line
read_rest_memory = core.read_rest_memory
write_rest_memory = core.write_rest_memory
wait_for_rest_byte = core.wait_for_rest_byte
parse_memory_row = core.parse_memory_row
ensure_hex_width = core.ensure_hex_width
ensure_status = core.ensure_status


class SkipCheck(Exception):
    """A documented platform limitation, reported as a SKIP by ``check``."""


@dataclass
class _Config:
    target: str = "u64"
    keep_going: bool = False
    failures: list[tuple[int, str, str]] = field(default_factory=list)
    skipped: list[tuple[int, str, str]] = field(default_factory=list)
    session: Optional["MonitorSession"] = None


TestConfig = _Config()
CHECK_COUNT = 0


@contextmanager
def check(label: str, *, u2: bool = True, u2_reason: str = "") -> Iterator[None]:
    """Report legacy scenario checks through the shared report fixture."""
    global CHECK_COUNT
    CHECK_COUNT += 1
    number = CHECK_COUNT
    check_start(label)
    if TestConfig.target == "u2" and not u2:
        reason = u2_reason or "not supported on U2"
        TestConfig.skipped.append((number, label, reason))
        check_skip(reason)
        return
    try:
        yield
    except SkipCheck as exc:
        reason = str(exc)
        TestConfig.skipped.append((number, label, reason))
        detail(reason)
        check_skip(reason)
    except Failure as exc:
        TestConfig.failures.append((number, label, str(exc)))
        detail(str(exc))
        check_fail(str(exc))
        if not TestConfig.keep_going:
            raise
    except BaseException as exc:
        detail(f"{type(exc).__name__}: {exc}")
        check_fail(str(exc))
        raise
    else:
        check_ok()


class MonitorSession(core.MonitorSession):
    """The current monitor session with the Debug suite's old constructor."""

    def __init__(self, host: str, port: int, password: Optional[str], timeout: float,
                 mode: str = MODE_TELNET) -> None:
        backend = make_backend(mode, host, password, timeout,
                               telnet_host=host, telnet_port=port)
        super().__init__(backend)
        self.sock = _LegacySocket(self)


class _LegacySocket:
    """Translate the old suite's handful of raw control keys to Backend keys."""

    _KEYS = {
        b"\x04": "CTRL_D",
        b"\x12": "CTRL_R",
        b"\x18": "CTRL_X",
        b"\x1bB": "CBM_B",
    }

    def __init__(self, session: MonitorSession) -> None:
        self.session = session

    def sendall(self, payload: bytes) -> None:
        key = self._KEYS.get(payload)
        if key is None:
            raise Failure(f"unsupported legacy raw key sequence: {payload!r}")
        self.session.send_key(key)


def rest_available(host: str, timeout: float = 1.0) -> bool:
    try:
        UltimateApi(host, None, timeout).machine.readmem(0x00A2, 1)
        return True
    except Failure:
        return False


def rest_api(host: str, password: Optional[str] = None,
             timeout: float = 5.0) -> UltimateApi:
    """Create the shared API fixture for a Debug scenario helper."""
    return UltimateApi(host, password, timeout)


def wait_for_monitor_ready(host: str, port: int, password: Optional[str], timeout: float) -> None:
    """Check the shared device API before a session opens the monitor UI."""
    UltimateApi(host, password, timeout).machine.readmem(0x00A2, 1)
