#!/usr/bin/env python3
"""A held cursor key must not drop the Telnet session.

Scrolling the monitor by holding a key emits about 680 bytes a keystroke, which
fills the device's send buffer faster than a slow link drains it. SO_SNDTIMEO is
5s (socket_gui.cc), so `send` then returns EAGAIN, and treating that as an error
closed the connection: measured on an Ultimate II+L over WiFi, the session went
silent after about 250 keys and the socket was closed from the device end.

A slow peer is not a gone peer. This drives the same burst a held key produces
and requires the session to still answer afterwards. It reads continuously
while it sends, so a failure here is the device giving up rather than the test
refusing to drain.

    python3 tests/e2e/network/telnet_sustained_input_test.py -H u2@c64u
"""
from __future__ import annotations

import argparse
import os
import socket
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "lib"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "lib"))

import targets  # noqa: E402
from report import Failure, check, check_ok, detail, suite_ok  # noqa: E402

SUITE = "telnet_sustained_input_test"

# Enough to have dropped the session before the fix, with margin. The observed
# death was around 250; 600 keys at a terminal's repeat rate is a few seconds of
# holding the key down, which is an ordinary thing to do.
KEYS = 600
KEY_INTERVAL_S = 0.033          # about 30 a second, a typical repeat rate
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
            except socket.timeout:
                continue
            except OSError as exc:
                self.error = f"{type(exc).__name__}: {exc}"
                return


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-P", "--telnet-port", type=int, default=23)
    parser.add_argument("--keys", type=int, default=KEYS)
    args = parser.parse_args()

    target = targets.parse(args.host)
    host = target.device

    with check("a held cursor key does not drop the Telnet session"):
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
