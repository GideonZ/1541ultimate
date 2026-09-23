#!/usr/bin/env python3
"""A held cursor key must not stall or drop the Telnet session.

Holding a cursor key in the machine code monitor redraws the screen on every
keystroke, about 1.3 KB each. Keys that arrive faster than the device redraws
wait unread in the session's socket. On WiFi each received frame occupies one
of the interface's twelve UART receive buffers for as long as it is unread, so
that queue alone can take every buffer: the interface then receives nothing,
not even the acknowledgements the device's own output is waiting for, and the
session is lost together with everything else on that interface. Ethernet has
enough receive buffers that one session cannot take them all, so over a wired
link this check passes without exercising the path.

The keys go out faster than an Ultimate 64 Elite redraws, so the device always
falls behind rather than only when something else keeps it busy. The session
has to still answer afterwards. The check reads continuously while it sends,
so a failure here is the device giving up rather than the test refusing to
drain.

The defect is open as GideonZ/1541ultimate#820, so the check is gated on
machine.TELNET_SEND_TOLERATES_SLOW_PEER and skips on an Ultimate II+ rather
than failing every run. Whoever fixes #820 should delete that entry, which
turns this check back on everywhere; `--assume-fix telnet-send-tolerates-slow-peer`
runs it without editing the table first.

    python3 tests/e2e/network/telnet_sustained_input_test.py -H u2@c64u
"""
from __future__ import annotations

import argparse
import os
import socket
import sys
import threading
import time
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402

import api as api_lib  # noqa: E402
import machine as machine_lib  # noqa: E402
import targets  # noqa: E402
from report import Failure, check, check_ok, detail, suite_ok  # noqa: E402

SUITE = "telnet_sustained_input_test"

# 600 keys is twelve seconds of a held key. At 50 a second an Ultimate 64 Elite
# draws about 890 bytes a key of the 1321 a full redraw takes, so it is behind.
KEYS = 600
KEY_INTERVAL_S = 0.02
CURSOR_DOWN = b"\x1b[B"
ENTER_MONITOR = b"\x0f"         # Ctrl+O


class Reader(threading.Thread):
    """Drain continuously, so a stall is the device's and not ours."""

    def __init__(self, sock: socket.socket) -> None:
        super().__init__(daemon=True)
        self.sock = sock
        self.total = 0
        self.last = time.monotonic()
        self.stop = False
        self.error = None

    def run(self) -> None:
        self.sock.settimeout(0.2)
        while not self.stop:
            try:
                data = self.sock.recv(65536)
                if not data:
                    self.error = "the device closed the connection"
                    return
                self.total += len(data)
                self.last = time.monotonic()
            except TimeoutError:
                continue
            except OSError as exc:
                self.error = f"{type(exc).__name__}: {exc}"
                return


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser, colour=False, timeout=None)
    parser.add_argument("-P", "--telnet-port", type=int, default=23)
    parser.add_argument("--keys", type=int, default=KEYS)
    args = parser.parse_args()

    target = targets.parse(args.host)
    host = target.device

    label = "a held cursor key does not drop the Telnet session"
    if api_lib.identify_machine(args.host).skip_without_fix(
            machine_lib.TELNET_SEND_TOLERATES_SLOW_PEER, label):
        suite_ok(SUITE)
        return 0

    with check(label):
        sock = socket.create_connection((host, args.telnet_port), timeout=10)
        reader = Reader(sock)
        reader.start()
        try:
            time.sleep(2.0)                       # banner
            sock.sendall(ENTER_MONITOR)
            time.sleep(1.5)                       # the monitor draws itself
            before = reader.total

            sent = 0
            for _ in range(args.keys):
                if reader.error:
                    raise Failure(
                        f"the session died after {sent} of {args.keys} keys: "
                        f"{reader.error}. Holding a cursor key is ordinary use, "
                        f"and a full send buffer is a slow peer, not a gone one")
                sock.sendall(CURSOR_DOWN)
                sent += 1
                time.sleep(KEY_INTERVAL_S)

            emitted = reader.total - before
            detail(f"{sent} keys sent at {1 / KEY_INTERVAL_S:.0f}/s, "
                   f"{emitted} bytes drawn back, "
                   f"{emitted / max(sent, 1):.0f} bytes a keystroke")

            # The session has to still be there, and still answer.
            time.sleep(1.0)
            if reader.error:
                raise Failure(f"the session died just after the burst: {reader.error}")
            quiet_mark = reader.total
            sock.sendall(CURSOR_DOWN)
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                if reader.total > quiet_mark:
                    break
                if reader.error:
                    raise Failure(f"the session died while idle: {reader.error}")
                time.sleep(0.05)
            else:
                raise Failure(
                    f"the session stopped answering after {sent} keys: one more "
                    f"keystroke drew nothing within 5s, though the socket was "
                    f"still open")
            check_ok(f"{sent} keys, still answering")
        finally:
            reader.stop = True
            try:
                sock.close()
            except OSError:
                pass

    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
