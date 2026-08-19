#!/usr/bin/env python3
# Gate check: the Telnet backend's redraw-drain state machine, without a device.

"""Verify how TelnetBackend decides a redraw is over, using a scripted socket.

Telnet is a byte stream rather than a frame, so the backend decides a redraw
has finished by watching the stream go quiet. Three different waits do that,
and the whole correctness of the Telnet transport rests on keeping them apart:

  first byte  how long a redraw may take to start. It is bounded because some
              keys legitimately draw nothing at all, and the wait is then spent
              proving a negative.
  IDLE_GAP    how long a silence means an ordinary redraw is over.
  SETTLE_GAP  the longer silence a committed prompt needs, because its echo and
              its redraw arrive as two bursts with a pause between them.

Getting any of them wrong shows up as a stale screen or as a run that takes
minutes longer than it should, neither of which names the cause. A real device
cannot produce these cases on demand, so the bursts here are scripted and the
rules are what is under test.
"""

import os
import socket
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "lib"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pacing  # noqa: E402  (needs tests/lib on sys.path first)
from report import Failure, check, suite_fail, suite_ok  # noqa: E402
from ui_backend import TelnetBackend, VT100Screen  # noqa: E402

# How far a measured wait may sit from the constant it is meant to be. Generous
# because this runs on a loaded host beside real hardware suites; the numbers
# being told apart differ by whole multiples, not by percentages.
TOLERANCE_SECONDS = 0.35

# A burst arrives well inside the first-byte budget, and the pause between a
# two-burst command's halves sits between IDLE_GAP and SETTLE_GAP, which is the
# whole reason those two are separate numbers.
FIRST_BURST_DELAY = 0.05
INTER_BURST_PAUSE = pacing.TELNET_IDLE_GAP_SECONDS * 4


def feed(sock, bursts):
    """Write each (delay, payload) in turn, then leave the socket open."""
    for delay, payload in bursts:
        time.sleep(delay)
        try:
            sock.sendall(payload)
        except OSError:
            return


def drain(bursts, expect_redraw, expect_settle=False, timeout=20.0):
    """Run one drain against a scripted stream. Returns (seconds, bytes)."""
    reader, writer = socket.socketpair()
    # Built without __init__, which would open a Telnet session: this is about
    # the drain's rules, not about connecting to anything.
    backend = TelnetBackend.__new__(TelnetBackend)
    backend.sock = reader
    backend.sock.setblocking(False)
    backend.timeout = timeout
    backend.screen = VT100Screen()
    backend.last_command = "fixture"
    backend._last_drain_bytes = 0
    backend._expect_redraw = expect_redraw
    backend._expect_settle = expect_settle

    thread = threading.Thread(target=feed, args=(writer, bursts), daemon=True)
    started = time.monotonic()
    thread.start()
    try:
        backend._drain_until_idle(timeout=timeout)
    finally:
        elapsed = time.monotonic() - started
        thread.join(timeout=5.0)
        writer.close()
        reader.close()
    return elapsed, backend._last_drain_bytes


def expect_near(label, actual, wanted):
    if abs(actual - wanted) > TOLERANCE_SECONDS:
        raise Failure(f"{label}: took {actual:.2f}s, expected about {wanted:.2f}s")


def expect(label, actual, wanted):
    if actual != wanted:
        raise Failure(f"{label}: got {actual!r}, expected {wanted!r}")


def run_checks():
    with check("a capture that sent nothing waits only the quiet check"):
        elapsed, drained = drain([], expect_redraw=False)
        expect("bytes", drained, 0)
        expect_near("elapsed", elapsed, pacing.TELNET_QUIET_CHECK_SECONDS)

    with check("a key known to draw nothing costs no more than that either"):
        # This is what `expect_redraw=False` on a send is for. A command prompt
        # refusing an impossible character draws nothing at all, and waiting
        # for a redraw that was never going to come is the whole first-byte
        # budget spent on every such keystroke.
        elapsed, drained = drain([], expect_redraw=False)
        expect("bytes", drained, 0)
        expect_near("elapsed", elapsed, pacing.TELNET_QUIET_CHECK_SECONDS)

    with check("a redraw that never starts gives up after the first-byte budget"):
        elapsed, drained = drain([], expect_redraw=True)
        expect("bytes", drained, 0)
        expect_near("elapsed", elapsed, pacing.TELNET_FIRST_BYTE_TIMEOUT_SECONDS)

    with check("one burst ends the drain an idle gap after it"):
        elapsed, drained = drain([(FIRST_BURST_DELAY, b"redraw")], expect_redraw=True)
        expect("bytes", drained, len(b"redraw"))
        expect_near("elapsed", elapsed,
                    FIRST_BURST_DELAY + pacing.TELNET_IDLE_GAP_SECONDS)

    with check("a settled command waits through the pause and takes both bursts"):
        # An echo, a pause, then the redraw. A byte count cannot tell the two
        # apart, because both vary with what is on screen; the quiet between
        # them is the only signal there is.
        elapsed, drained = drain(
            [(FIRST_BURST_DELAY, b"echo"), (INTER_BURST_PAUSE, b"redraw")],
            expect_redraw=True, expect_settle=True)
        expect("bytes", drained, len(b"echo") + len(b"redraw"))
        if elapsed < FIRST_BURST_DELAY + INTER_BURST_PAUSE:
            raise Failure(f"the drain returned after {elapsed:.2f}s, before the "
                          "second burst could arrive")

    with check("an ordinary command does not wait for a second burst"):
        # The other half of the same rule: paying the settle gap on every
        # keystroke would make the Telnet lane far slower than it needs to be,
        # so it is opt-in and an ordinary send stops at the first quiet.
        elapsed, drained = drain(
            [(FIRST_BURST_DELAY, b"echo"), (INTER_BURST_PAUSE, b"redraw")],
            expect_redraw=True, expect_settle=False)
        expect("bytes", drained, len(b"echo"))
        expect_near("elapsed", elapsed,
                    FIRST_BURST_DELAY + pacing.TELNET_IDLE_GAP_SECONDS)

    with check("a redraw arriving in pieces is drained as one"):
        pieces = [(FIRST_BURST_DELAY, b"one"),
                  (pacing.TELNET_IDLE_GAP_SECONDS / 3, b"two"),
                  (pacing.TELNET_IDLE_GAP_SECONDS / 3, b"three")]
        elapsed, drained = drain(pieces, expect_redraw=True)
        expect("bytes", drained, len(b"onetwothree"))


def main():
    try:
        run_checks()
    except Failure as exc:
        suite_fail("telnet_drain_test", str(exc))
        return 1
    suite_ok("telnet_drain_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
