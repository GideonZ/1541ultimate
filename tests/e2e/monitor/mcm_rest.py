#!/usr/bin/env python3
"""Compatibility adapter for the monitor matrix's shared device fixtures.

The matrix predates ``tests/lib/api.py`` and ``tests/e2e/lib/ui_backend.py``.
Keep its small, monitor-oriented surface here, but let those two libraries own
HTTP retrying, request encoding, input events, memory transfers and screen
decoding.  New monitor checks should use the libraries directly when their
needs fit their public API.
"""

import os
import sys
import time
from collections.abc import Iterable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "lib"))
sys.path.insert(0, os.path.join(HERE, "..", "lib"))

from api import UltimateApi
from report import Failure
from ui_backend import SCREEN_CELLS, char_to_combo

SCREEN_W, SCREEN_H = 40, 25


class Rest:
    """Monitor-specific spelling of the shared :class:`UltimateApi` fixture."""

    def __init__(self, host: str = "u64", timeout: float = 10.0,
                 password: str | None = None) -> None:
        self.host = host
        self.timeout = timeout
        self.api = UltimateApi(host, password, timeout)

    def req(self, method: str, path: str, params=None, body=None, ctype=None):
        """Raw response access for the few monitor-only config assertions.

        The shared ``RestClient`` remains the sole transport owner.  This is
        deliberately not a second HTTP client; it only preserves the matrix's
        old ``(status, payload)`` return convention.
        """
        headers = {"Content-Type": ctype} if ctype else None
        status, _headers, payload = self.api.rest.request(
            method, path, params=params, body=body, headers=headers,
            idempotent=method.upper() in ("GET", "PUT"))
        if status >= 400:
            raise Failure(f"{method} {path} returned HTTP {status}: {payload[:160]!r}")
        return status, payload

    def alive(self, timeout: float = 3.0) -> bool:
        try:
            # A real machine read is more useful than a TCP connect: it proves
            # both the shared REST transport and the C64 endpoint answer.
            UltimateApi(self.host, None, timeout).machine.readmem(0x00A2, 1)
            return True
        except Failure:
            return False

    def reset(self) -> None:
        self.api.machine.reset(force=True, wait=False)

    def menu_button(self) -> None:
        self.api.machine.menu_button()

    def read_mem(self, addr: int, length: int) -> bytes:
        return self.api.machine.readmem(addr, length)

    def write_mem(self, addr: int, data: bytes) -> None:
        self.api.machine.writemem(addr, data, idempotent=True)

    def tap(self, inputs: Iterable[str]) -> None:
        self.api.machine.press(*inputs)

    def release_all(self) -> None:
        self.api.machine.release_all()

    def send_text(self, text: str, settle: float = 0.12) -> None:
        for ch in text:
            self.tap(char_to_combo(ch))
            time.sleep(settle)

    def menu_screen_raw(self) -> bytes:
        body = self.api.machine.menu_screen()
        if body is None:
            raise Failure("machine:menu_screen is unavailable because the menu is closed")
        if len(body) != 2 * SCREEN_CELLS:
            raise Failure(f"machine:menu_screen returned {len(body)} bytes")
        return body

    def screen_lines(self):
        rows = self.api.machine.menu_rows()
        if not rows:
            raise Failure("machine:menu_screen is unavailable because the menu is closed")
        return [row.rstrip() for row in rows]

    def screen_text(self) -> str:
        return "\n".join(self.screen_lines())


def wait_for(rest: Rest, needles, label: str, timeout: float = 6.0,
             interval: float = 0.2) -> str:
    expected = [needles] if isinstance(needles, str) else list(needles)
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        try:
            last = rest.screen_text()
        except Failure:
            time.sleep(interval)
            continue
        if all(needle in last for needle in expected):
            return last
        time.sleep(interval)
    raise Failure(f"{label}: timed out waiting for {expected}\n--- last screen ---\n{last}")
