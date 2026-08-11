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

# Telnet reads a byte stream rather than a frame, so it decides a redraw has
# finished by watching the stream go quiet. Three different questions, which
# used to share one 0.5s answer:
#
#   FIRST_BYTE  how long to wait for a redraw to start after a keystroke that
#               may or may not produce one (a keystroke that only ends an
#               input mode, e.g. CTRL_E, can legitimately draw nothing). U2+L
#               session-opening/switching keys (CTRL_O into the monitor,
#               CTRL_B into the bookmark picker) are the slow case: each one
#               calls begin_session(), which freezes the C64 through the ECP5
#               core before the first redraw byte is sent, and the pause grows
#               with how long the session has already been running. Measured
#               worst on U2+L hardware: 1.875s for CTRL_O in isolation,
#               growing past 9.7s under a rapid sequence of prompt commits,
#               and CTRL_B alone timed out at the previous 15.0s budget late
#               in a long-running session (17 monitor commands in). This has
#               to stay a bounded budget, not the caller's full timeout,
#               because a keystroke that legitimately draws nothing would
#               otherwise block for the whole timeout every time; 25.0s
#               leaves a real margin over the worst observed case while
#               staying under a -t 30 run.
#   IDLE_GAP    how long a silence means an already-complete redraw is over.
#               Measured worst gap between bytes inside a single redraw:
#               0.015s, so 0.15 is ten times the observed worst. This is the
#               one paid on every capture, which is why it is the short one.
#   SETTLE_GAP  how long a silence means a two-burst redraw (see below) is
#               really over, not just paused between its two bursts.
#
# A committed prompt (send_text: Jump/Fill/Compare/Go, a bookmark label, a
# save/load filename) or a settled key (send_char(ch, settle=True): W, the
# hex-width toggle) echoes almost instantly, then goes quiet for the same
# begin_session() pause as CTRL_O before the real redraw follows as a
# separate, later burst. IDLE_GAP alone reads the echo's own trailing quiet
# spot as "redraw over" and returns a snapshot from mid-transition.
#
# This used to be told apart by total bytes received (trust an idle gap only
# once enough had arrived to look like the real redraw), but the echo burst
# is not reliably smaller than the real one: it scales with whatever is
# already on screen, not with what was typed or pressed. A short Jump
# echoes 88 bytes before a 1337-byte redraw, and a Compare/Fill popup over a
# plain hex view echoes under 400 bytes before a ~1300-byte redraw, but W's
# own two bursts measured ~1323 bytes each (no split to gate on at all), and
# the number popup's echo (drawn over a busier ASM view) measured over 1800
# bytes on its own, past any fixed byte threshold that would still have to
# stay small enough not to misread a small key's single complete redraw as
# "more still coming" (e.g. a dialog-open alone, ~300-425 bytes, with
# nothing further arriving). Elapsed quiet time is the one signal that stays
# valid regardless of screen content: SETTLE_GAP requires a longer confirmed
# quiet period before trusting an idle gap for these two cases.
#
# Like FIRST_BYTE for a key that legitimately draws nothing, this has to be a
# bounded budget rather than being told apart with certainty; unlike
# FIRST_BYTE, it is not further bounded by a fixed ceiling of its own beyond
# the caller's own timeout (see _drain_until_idle), since a committed prompt
# or a settled key always redraws eventually. Measured worst gap before the
# real redraw: past 4s on U2+L under a rapid sequence of prompt commits, and
# around 3s for W's own two bursts; giving up sooner does not skip the wait,
# it just hands the still-arriving bytes to whichever capture runs next,
# corrupting that one instead. 6.0s roughly doubles the worst observed gap
# for margin while staying well under a -t 30 run.
TELNET_FIRST_BYTE_TIMEOUT_SECONDS = _seconds("U64_UI_TELNET_FIRST_BYTE", 25.0)
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
KEY_DRAIN_SECONDS = _seconds("U64_UI_KEY_DRAIN", 0.02)

# A fixed pause, used only where there is nothing observable to poll: the C64
# screen behind the menu, a config write with no readback, a popup that draws
# identically to what it replaced.
KEY_SETTLE_SECONDS = _seconds("U64_UI_KEY_SETTLE", 0.05)

# Opening or closing the on-device menu, which is a mode change rather than a
# redraw and is slower than a keystroke inside one.
MENU_TOGGLE_SETTLE_SECONDS = _seconds("U64_UI_MENU_TOGGLE_SETTLE", 0.25)
MENU_TOGGLE_TIMEOUT_SECONDS = _seconds("U64_UI_MENU_TOGGLE_TIMEOUT", 6.0)


def summary() -> str:
    """One line naming the pacing a run used, for a log that has to be read later."""
    return (f"ui pacing: poll={POLL_INTERVAL_SECONDS:g}s "
            f"samples={SETTLE_STABLE_SAMPLES} "
            f"key-change={KEY_CHANGE_TIMEOUT_SECONDS:g}s "
            f"settle={SETTLE_TIMEOUT_SECONDS:g}s")
