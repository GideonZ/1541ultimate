"""How fast the tests drive the on-device UI, in one place.

Every suite used to carry its own copy of "how long to wait after a keystroke",
which is why the same idea appeared at 0.0, 0.045, 0.05, 0.10, 0.25, 0.30 and
0.35 seconds across the tree, with no record of which of them had been measured.
A suite that navigates a file listing was several times slower than one typing
into a field for no reason other than which constant it happened to inherit.

Change a value here and every suite changes with it. The values are measured on
hardware rather than chosen; each one below records what was swept and where it
started to fail. Two workloads were used, both verified every iteration against
a seeded 25-entry directory: walk the listing to a named entry and confirm the
cursor landed on it, and type a known string into a rename field and read it
back off the screen.

A suite whose subject *is* timing (key repeat, a modal that must not be raced,
a deliberately slow drain) passes its own number at the call site. That is not
a violation of this module; the point is that a suite doing ordinary UI work
should not have to think about pacing at all, and a suite that is testing
pacing should say so explicitly.

Every value can be overridden for one run without editing code, which is what
the calibration sweep itself uses:

    U64_UI_POLL_INTERVAL=0.05 ./run-tests u64
"""

from __future__ import annotations

import math
import os


def _seconds(name: str, default: float) -> float:
    raw = os.environ.get(name)
    if raw is None:
        return default
    try:
        value = float(raw)
    except ValueError:
        raise SystemExit(f"{name} must be a number, got {raw!r}")
    if value < 0:
        raise SystemExit(f"{name} must not be negative, got {value}")
    return value


def _count(name: str, default: int) -> int:
    raw = os.environ.get(name)
    if raw is None:
        return default
    try:
        value = int(raw)
    except ValueError:
        raise SystemExit(f"{name} must be a whole number, got {raw!r}")
    if value < 1:
        raise SystemExit(f"{name} must be at least 1, got {value}")
    return value


# How often a wait re-reads the menu screen while watching for a change or for
# a redraw to finish. One read costs about 17ms on the wire, so below roughly
# 0.01 the polling is transport-bound and lowering it further buys nothing.
#
# This is not the knob that made the suites slow, and it is the one with the
# least margin, so it is left where it was measured to be reliable. Together
# with SETTLE_STABLE_SAMPLES it sets how long the screen must be unchanged
# before a redraw counts as finished: about 134ms here, counting the read
# itself. Shortening that window is what breaks first. At 0.02 the sweep saw a
# context menu reported as "not drawn" because the gap between two frames of
# its own draw was longer than the window.
#
# Swept at 2 stable samples: 0.05 and 0.03 passed both workloads; 0.02 and 0.01
# each failed one of three iterations, with "no context menu appeared" and
# "could not select an entry". Left at 0.05, the value already in use, because
# it was never the reason the suites were slow.
POLL_INTERVAL_SECONDS = _seconds("U64_UI_POLL_INTERVAL", 0.05)

# Consecutive identical reads that mean the redraw has finished. At 1 a single
# slow frame is read as "settled", so this stays above it.
SETTLE_STABLE_SAMPLES = _count("U64_UI_STABLE_SAMPLES", 2)

# How long a redraw may take to finish once it has visibly started.
SETTLE_TIMEOUT_SECONDS = _seconds("U64_UI_SETTLE_TIMEOUT", 6.0)

# How long to wait for a keypress to produce any visible change at all, and how
# many reads of the screen to take before believing there was none.
#
# Both are needed, and the reason is worth keeping. A first attempt set this to
# a short wall-clock budget alone, measured only in Overlay, and it broke
# Freeze: ui-backend-smoke failed with "F5 had no visible effect on the screen",
# left the device in Freeze with the menu open because it failed before
# restoring the setting, and the run ended with the device off the network.
#
# The cause was not a slow redraw. Measured across Overlay and Freeze, over
# cursor keys, the F5 task menu, the context menu and directory descent, the
# first visible change arrives within 83ms, and the longest pause inside a
# redraw is 27ms. What a short budget could not survive was a busy device: with
# only four HTTP connections served at once, one read of the screen can take
# seconds, so a 0.6s budget expired having looked once and called a change that
# had happened "no change".
#
# So the wait ends only when both are satisfied: the budget has elapsed *and*
# this many reads have been taken. On a healthy device the reads cost about
# 17ms each and the wait is short; on a busy one it stretches itself in
# proportion to how slow the transport has become. SETTLE_TIMEOUT_SECONDS caps
# it either way.
KEY_CHANGE_TIMEOUT_SECONDS = _seconds("U64_UI_KEY_CHANGE_TIMEOUT", 0.3)
KEY_CHANGE_MIN_SAMPLES = _count("U64_UI_KEY_CHANGE_MIN_SAMPLES", 6)

# How long to keep looking for an overlay (context menu, task menu) to appear
# after the key that opens it.
#
# A settle only promises the screen stopped changing, which is not the same as
# the overlay having been drawn: a draw that starts after the settle window
# reads as "no overlay at all". A single screen sample therefore cannot tell
# "not yet" from "never", and the sweep reproduced exactly that as "no context
# menu appeared" once the settle window was shortened. Polling for it costs
# nothing when the overlay is already there, which is the normal case.
OVERLAY_DRAW_TIMEOUT_SECONDS = _seconds("U64_UI_OVERLAY_DRAW_TIMEOUT", 2.0)

# The same budget, for the one caller that does not depend on it being right:
# the browser's quick-seek.
#
# A seek is confirmed by reading the cursor back afterwards, through a fresh
# settled capture, so nothing it decides rests on the snapshot the settle
# returns. Concluding "no change" too early therefore costs nothing here, where
# for an ordinary keypress it would mean returning before the key took effect.
# It is worth separating because a seek legitimately changes nothing whenever
# the entry is already under the cursor, which is common, and paying the full
# budget for that would undo what seeking is for.
SEEK_CHANGE_TIMEOUT_SECONDS = _seconds("U64_UI_SEEK_CHANGE_TIMEOUT", 0.4)

# Telnet is a stream, so redraw completion uses quiet periods:
#
#   FIRST_BYTE waits for a redraw to start, and is bounded because some keys
#              legitimately draw nothing at all: a command prompt refusing an
#              impossible character emits not one byte. Measured over a whole
#              monitor suite on both an Ultimate 64 and an Ultimate II+L,
#              logging send-to-first-byte for every keystroke: the worst was
#              101ms, entering the monitor with C=+O, and ordinary keys were
#              25 to 60ms. 1s is ten times the worst measurement.
#
#              It was 25s for a while, to cover late-run U2+L transitions. That
#              was two separate mistakes. A send that draws nothing now says so
#              (Backend.send_key/send_char take expect_redraw=False), so it
#              costs the quiet check below instead of the whole budget; and a
#              caller waiting for a screen to reach a particular state polls
#              for that state rather than for a redraw, which is both faster
#              when it is already there and more patient when it is late.
#   IDLE_GAP   ends an ordinary redraw. 0.15s is well above its observed
#              intra-redraw byte gap.
#   SETTLE_GAP ends a committed prompt or selected two-burst command. Its echo
#              and redraw have content-dependent sizes, so elapsed quiet time,
#              not bytes received, distinguishes the bursts. It is bounded by
#              the caller timeout; 6s exceeds the observed inter-burst gap.
TELNET_FIRST_BYTE_TIMEOUT_SECONDS = _seconds("U64_UI_TELNET_FIRST_BYTE", 1.0)
TELNET_IDLE_GAP_SECONDS = _seconds("U64_UI_TELNET_IDLE_GAP", 0.15)
TELNET_SETTLE_GAP_SECONDS = _seconds("U64_UI_TELNET_SETTLE_GAP", 6.0)

# What a bare capture waits when nothing was sent and so no redraw is expected.
TELNET_QUIET_CHECK_SECONDS = _seconds("U64_UI_TELNET_QUIET_CHECK", 0.15)

# How long one keystroke of a batch takes to drain through the C64 matrix.
#
# REST accepts a batch of keystrokes in one request and answers immediately,
# but the firmware then feeds them through the matrix one at a time. A settle
# alone cannot tell "the batch is finished" from "the batch is between two
# keys": both look like a screen that is not changing. Waiting for the batch to
# have had time to drain is what distinguishes them, so a batched send is not
# considered complete until this much time per keystroke has passed.
#
# Measured at about 15ms a key (20 keys batched took 310ms), rounded up. This
# is what a "Save as" field in the machine monitor was intermittently read
# empty without: the screen was captured after the field had been cleared but
# before any of the typed name had arrived.
#
# That measurement is of a machine that is its own computer, where a key sent
# while the menu owns the keyboard is put straight into the firmware's own key
# queue (software/api/route_input.cc apply_keyboard_menu_event -> push_head)
# and never touches a keyboard matrix. See SPLIT_KEY_DRAIN_SECONDS for the
# cartridge case, which is five times slower.
KEY_DRAIN_SECONDS = _seconds("U64_UI_KEY_DRAIN", 0.02)

# The same, for a cartridge target such as "u2@c64u".
#
# There the key is not delivered to the device under test at all. It is queued
# as a tap on the live keyboard matrix of the computer the cartridge is plugged
# into (route_input.cc apply_keyboard_event -> restQueueTap), and the cartridge
# picks it up on its next scan of that matrix. The firmware holds each tap for
# REST_KEYBOARD_TAP_HOLD_TICKS (60ms) and then waits REST_KEYBOARD_TAP_GAP_TICKS
# (40ms) before the next one, so 100ms a key is the floor the hardware sets.
#
# Measured on u2@c64u, from a drained queue, as the time until the cursor had
# moved by the whole batch: 1 key 69ms, 2 keys 186ms, 5 keys 458ms, 10 keys
# 986ms, 15 keys 1462ms. That is 100ms a key after the first.
#
# Charging a batch the 20ms figure above is what made a quick-seek read its own
# result before the seek had finished typing. The cursor moves on every
# character, so a prefix whose leading characters match a different entry parks
# the cursor on that entry until the rest of the prefix arrives, and the check
# that reads it there concludes the search failed.
#
# 60ms, not the 100ms the hardware paragraph above describes, because what this
# constant sets is how long the harness waits before reading, not how fast the
# firmware delivers. The whole batch is posted at once and drains at the rate
# the firmware sets whatever is charged here; the settle in
# RestBackend._settle spends real time of its own first, and this only tops it
# up. 60.7ms a key is also what this tree's own firmware ticks measure
# (REST_KEYBOARD_TAP_HOLD_QUEUE_TICKS 2 plus REST_TAP_GAP_TICKS 1, in
# tests/e2e/doc/key-injection-rate.md), so a computer flashed from this tree
# and one still on the released 1.2.0 are both covered.
#
# It is deliberately not tuned any finer, because a sweep says there is
# nothing there to tune. Six settings from 30ms to 100ms a key were run on
# u2@c64u through menu_navigation_soak_test, which reads back where a jump
# landed and so fails on a lost key rather than on a slow one. The two
# cleanest passes were the two fastest settings (30ms and 40ms, no wrong
# landings at all) and the slowest setting lost two. A key goes missing a few
# times in a thousand and the rate does not move with what is charged here.
#
# So the lever for both speed and reliability is how many keys a movement
# takes, not what each one is charged. See Browser.move_rows, which spends
# page keys on the bulk of a jump: on u2@c64u the same 38-row landing costs 8
# keys instead of 38, and its median fell from 2173ms to 820ms.
#
# That is also why no run measures this for itself. A probe at the start of a
# run could only measure how fast keys arrive, which the sweep shows does not
# decide whether they are read correctly, and it would charge every run that
# never touches a keyboard for the privilege. The measurement belongs in the
# soak, which has a real read-back oracle and is run when a machine's firmware
# changes; remember_key_drain below is how it holds a swept value for the
# length of one process.
#
# Measured end to end through the ordinary send path on u2@c64u: at 0.1 a
# 12-key batch cost 120ms a key, at 0.06 it cost 84ms, and typing a 12
# character string cost the same. The u64, which needs no matrix crossing,
# runs at 25ms a key for cursors and 31ms for text.
SPLIT_KEY_DRAIN_SECONDS = _seconds("U64_UI_SPLIT_KEY_DRAIN", 0.06)


def key_drain_seconds(split: bool, host: str | None = None) -> float:
    """What one key of a batch costs on this target, in seconds.

    Named here rather than chosen at each call site, because the two constants
    differ by a factor of five and picking the wrong one is not visible in the
    result: the keys all arrive, just later than the caller reads. Charging a
    cartridge the device rate truncated a 14-character form field by its last
    character, because the RETURN that committed the field was sent while that
    character was still crossing the computer's keyboard matrix.
    """
    measured = _measured.get(host)
    if measured is not None:
        return measured
    return SPLIT_KEY_DRAIN_SECONDS if split else KEY_DRAIN_SECONDS


# What a machine was measured to need, by host, for this process only.
#
# The constants above are the rate a machine is charged when nothing has
# measured it, and they are deliberately the slower of the machines this bench
# has: 0.06 covers a computer on the released firmware as well as one flashed
# from this tree. A machine that is faster than that is only found by asking
# it, which is what remember_key_drain records.
#
# In-process and not persisted on purpose. A cached rate that outlived a
# reflash would be a rate that is too fast for the machine now in front of it,
# and too fast is the failure that loses keystrokes; a run pays the
# measurement once and a wrong answer cannot outlive the run.
_measured: dict = {}

# Never charge less than this, whatever a measurement says. A batch that
# measured faster than the firmware can deliver was measured while something
# else was quiet, not while it was busy.
MEASURED_FLOOR_SECONDS = _seconds("U64_UI_KEY_DRAIN_FLOOR", 0.02)


def remember_key_drain(host: str, seconds: float) -> float:
    """Record what `host` was measured to need, and answer what will be charged.

    The measurement is taken as an upper bound on the truth rather than the
    truth: it is rounded up to the next 10ms and floored, because the cost of
    being slightly slow is a slightly longer run and the cost of being slightly
    fast is a lost keystroke and a failure that names something else.
    """
    charged = max(MEASURED_FLOOR_SECONDS, math.ceil(seconds * 100) / 100)
    _measured[host] = charged
    return charged


def forget_key_drain(host: str | None = None) -> None:
    """Drop what was measured, for a test that measures its own."""
    if host is None:
        _measured.clear()
    else:
        _measured.pop(host, None)

# A fixed pause, used only where there is nothing observable to poll: the C64
# screen behind the menu, a config write with no readback, a popup that draws
# identically to what it replaced.
KEY_SETTLE_SECONDS = _seconds("U64_UI_KEY_SETTLE", 0.05)

# Opening or closing the on-device menu, which is a mode change rather than a
# redraw and is slower than a keystroke inside one.
MENU_TOGGLE_SETTLE_SECONDS = _seconds("U64_UI_MENU_TOGGLE_SETTLE", 0.25)
MENU_TOGGLE_TIMEOUT_SECONDS = _seconds("U64_UI_MENU_TOGGLE_TIMEOUT", 6.0)

# What the runner waits after resetting the machine back to a clean slate, so
# the next suite meets a C64 that has finished coming up rather than one still
# in its cold start. REST answers throughout the reset, so there is nothing on
# the wire to poll for: the state being waited for is on the C64 itself.
#
# A run driving the loopback device double has no C64 behind it and overrides
# this to near zero, which is the reason the value is here rather than being a
# constant in the runner.
RESET_SETTLE_SECONDS = _seconds("U64_UI_RESET_SETTLE", 0.5)


# Typing into BASIC through the C64's keyboard matrix. A tap the KERNAL scan
# misses produces no character, so a suite typing a command reads the echo back
# rather than assuming it landed; these bound that read.
#
# The per-key value is what one tap costs before the next is sent. Measured on
# an Ultimate 64 Elite: 0.20s a key typed a nine-character command whole on
# every one of twenty attempts, and the echo of the whole line then appeared
# within 0.2s. The timeout is generous against that because it is only reached
# when a key really was dropped, which is the case the retry exists for.
C64_TYPE_KEY_SECONDS = _seconds("U64_C64_TYPE_KEY", 0.20)
C64_ECHO_TIMEOUT_SECONDS = _seconds("U64_C64_ECHO_TIMEOUT", 3.0)
# Wiping a half-typed line is INST/DEL per character, and a delete needs less
# settling than a character that has to be echoed: the retry that follows reads
# the line back, so a delete that was dropped is caught there.
C64_DELETE_KEY_SECONDS = _seconds("U64_C64_DELETE_KEY", 0.06)

# After the menu closes it re-enables the C64 keyboard matrix, and the browser
# task has to unwind before the machine has its keyboard back. There is nothing
# on the wire that says so, which is why this is a sleep and not a poll.
C64_KEYBOARD_HANDBACK_SECONDS = _seconds("U64_C64_KEYBOARD_HANDBACK", 0.5)


def summary() -> str:
    """One line naming the pacing a run used, for a log that has to be read later."""
    return (f"ui pacing: poll={POLL_INTERVAL_SECONDS:g}s "
            f"samples={SETTLE_STABLE_SAMPLES} "
            f"key-change={KEY_CHANGE_TIMEOUT_SECONDS:g}s "
            f"settle={SETTLE_TIMEOUT_SECONDS:g}s")
