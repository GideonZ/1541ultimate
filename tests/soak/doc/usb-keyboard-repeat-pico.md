# Pico 2 W USB keyboard repeat soak

This fixture is the real-hardware regression test for issue #797 and PR #796.
It uses a Raspberry Pi Pico 2 W as one conventional USB boot keyboard and
controls it over Wi-Fi. It is deliberately narrow: the Pico can send only a
small set of navigation keys and cannot execute arbitrary commands.

The setup command flashes **only the Pico 2 W**. It never flashes an Ultimate
64, C64 Ultimate, or any other device. The pinned official MicroPython UF2 and
pinned MicroPython `usb-device`/`usb-device-hid` sources download into the
user's cache when setup runs. No binary, cache, or Wi-Fi credential belongs in
the repository.

## 1. Prepare the Pico

Install host requirements once:

```sh
pip install -r tests/requirements.txt
```

Connect the Pico directly to the Linux computer with a known data-capable USB
cable. Hold **BOOTSEL** while plugging it in, keep holding for about one
second, then release. Linux should show an `RP2350 Boot` mass-storage device.

Set Wi-Fi credentials only in the environment. Do not put the password on a
command line or in a tracked file:

```sh
PICO_WIFI_SSID='your-network' \
PICO_WIFI_PASSWORD='your-password' \
tests/soak/io/usb/usb_keyboard_repeat_soak_test.py --setup-pico
```

Setup logs each download, copy, and reset without printing the password. It
also performs a Linux-side HID self-test: the fixture emits a benign **F13**
press and release, which Linux must observe exactly once. F13 is not used by
the U64 scenarios and is chosen to avoid typing text into the currently focused
application. Some desktops open a settings panel for F13; that is harmless.
Setup ends by printing the Pico ID and Wi-Fi IP.

The self-test reads `/dev/input/event*` where the user is allowed to (usually
through the `input` group). Where it is not, setup falls back to `xinput` on an
X11 session, which needs no extra privilege. If neither is available, setup
fails rather than claiming a self-test it could not observe.

Setup deliberately copies `boot.py` **last** and runs nothing after it. Every
`mpremote` command soft resets the board, and once `boot.py` exists that reset
configures the HID interface during early boot. Copying `boot.py` earlier
removes the CDC serial port that the remaining copies depend on, which leaves
the board reachable only through BOOTSEL.

Setup also accepts a board that already runs MicroPython and still exposes its
CDC serial port: it re-provisions over that port and skips the UF2 flash. Only
a board with no serial port needs BOOTSEL.

If BOOTSEL does not enumerate, stop firmware work. Try a known data cable and
one direct computer USB port, holding BOOTSEL for every reconnect.

Setup validates only what Linux can prove: that the HID interface is open and
that a single F13 press and release arrive. The idle rate reported at this
point is whatever Linux negotiated, which is 0. Only the Ultimate 64 sets 25.

## 2. Move to the Ultimate 64

Unplug the Pico from Linux and connect it to a rear USB port on the Ultimate
64 under test. Keep the Linux computer and U64 on the same network.

**The Pico must be the only USB keyboard attached to the Ultimate 64.** The
firmware passes the negotiated idle period on to the key repeat only when
exactly one attached keyboard accepted that rate
(`software/io/usb/usb_hid_selection.h`, `usb_hid_keyboard_idle_period_ms`).
`Keyboard_USB` sees one merged report stream, so the periodic reports of a
second keyboard would keep a stale key of the first looking fresh. With any
other keyboard or keyboard receiver plugged in, the repeat bound is disabled
by design and the fault cases report many menu movements instead of one.

The U64 must be running firmware that contains PR #796. A stock 3.15 does not
send `SET_IDLE`, and the fixture then reports `idle_rate: 0`.

The test discovers the Pico by UDP broadcast and, failing that, by connecting
to the fixture's control port on every address of the test host's /24. Access
points commonly drop broadcast between a wired host and a Wi-Fi client, so the
sweep is the path that usually succeeds. `--pico-host <IP>` skips discovery
entirely.

## 3. Run

Start with the two-minute stress profile:

```sh
./run-tests -H u64 --soak -s usb-keyboard-repeat
```

For a bounded developer run:

```sh
tests/soak/io/usb/usb_keyboard_repeat_soak_test.py -H u64 --duration 30s
```

The twelve-hour profile is explicit:

```sh
tests/soak/io/usb/usb_keyboard_repeat_soak_test.py -H u64 --profile soak
```

## What the test navigates

The root browser of the Ultimate menu holds six entries, so a held key reaches
its end in a fraction of a second and further presses stop moving the
selection. Before the checks run, the test creates a directory of 200 files
named `row000.txt` .. `row199.txt` on the volatile RAM disk over FTP and
navigates into it. The list is deep enough that even the longest mid-repeat
hold below cannot reach either end, and a `recentre_if_needed` step keeps the
live selection between rows 30 and 170 throughout the run so a real menu
boundary clamp is never mistaken for the repeat bound failing to move the
selection. Nothing is written to flash or to attached storage, and the
directory is removed on exit.

That navigation is done with menu keys injected over REST
(`machine:input`), which reach the same on-screen menu as the USB keyboard.
The Telnet remote menu is a separate 60-column user interface instance and does
not move the on-screen selection. In the on-screen menu, cursor left leaves a
directory and `run_stop` closes the menu altogether.

Because the list entries are numbered, the selected file's row number is read
directly off the screen instead of comparing marker text: `parse_row_index`
turns "row042.txt ..." into the integer 42, so every check gets an exact
movement count rather than only "did the text change".

## Fault design: forcing the bug, not guessing at it

The fault durations and hold durations are chosen from the firmware's own
timing, not swept blindly:

- `first_delay` in `keyboard_usb.cc` (16 poll ticks) puts the first genuine
  repeat at roughly 320 ms of real held-key time.
- `USB_REPEAT_STALE_IDLE_PERIODS` (3) times the negotiated 100 ms `SET_IDLE`
  period gives a 300 ms staleness cutoff: a held key whose reports go silent
  longer than that must have its repeat bound engage.
- Those two numbers sit only 20 ms apart. `MID_REPEAT_HOLD_DURATIONS_MS`
  sweeps exactly that boundary (300, 310, 320, 330 ms, ...), in addition to
  longer holds where several genuine repeats are already in flight when the
  release is lost -- the scenario the original field bug actually reported,
  not a fresh tap whose repeat never started.

Each iteration picks one of two phases at random:

- **pre-repeat**: a short tap (12-60 ms), well under the first-repeat delay.
  The release is lost before any repeat could have fired, so exactly one
  keystroke is the only correct outcome, checked with zero tolerance.
- **mid-repeat**: a hold from `MID_REPEAT_HOLD_DURATIONS_MS`, long enough that
  one or more genuine repeats have already fired by the time the release is
  lost. What counts as "correct" here cannot be a fixed number, so it is
  measured against this device's own calibrated baseline instead (below).

Both `drop_release_once` and `silence_after_press` are used with either
phase, and `FAULT_DURATIONS_MS` (400-1200 ms, the fixture protocol's own
floor and ceiling) is randomised on top.

## Calibrated baselines, not hand-derived constants

A faulted mid-repeat hold cannot be compared against a fixed expected count,
because how many genuine repeats a given hold duration produces depends on
real USB polling and task-scheduling jitter that a constant would not track.
Instead, `calibrate_repeat_counts` measures it directly on the live device
before the soak begins, holding each duration in `MID_REPEAT_HOLD_DURATIONS_MS`
with a normal, un-faulted release several times and recording the maximum
repeat count observed.

Two baselines are kept, because the two faults extend the "genuine" window
differently:

- `drop_release_once` keeps idle reports flowing straight through the fault,
  so the true release reaches the firmware within about one idle period of
  `duration_ms` regardless. Its allowed count is `hold_baseline[duration_ms] + 1`.
- `silence_after_press` does not suppress idle reports until *after*
  `duration_ms`, so the key was still being refreshed right up to that point
  and stays legitimately "live" for `SILENCE_GRACE_MS` (400 ms) longer before
  the staleness bound has to engage. Its baseline is calibrated at the longer,
  effective duration (`duration_ms + SILENCE_GRACE_MS`) instead of at
  `duration_ms` itself, and this is the difference an earlier version of this
  fixture got wrong: comparing a silenced hold against the un-extended
  baseline produced a reproducible false failure (delta 5 against an allowed
  4, on a 560 ms hold with a 1200 ms silent fault) that a 15-trial repeat of
  the same *genuine, unfaulted* hold showed was expected behaviour, not a
  regression -- the accepted "one repeat decision" race applies on top of the
  correct, extended baseline, not the nominal one.

The one extra step added on top of either baseline is the single "a report
landing mid-check can cost one repeat decision" race the firmware comments
accept as a known, bounded cost (`keyboard_usb.cc`, `repeatIsLive`). Anything
past that is the runaway repeat this fixture exists to catch.

## What the checks assert

Before a soak begins, the test requires one keyboard HID interface, U64
`SET_IDLE(25)` followed by `GET_IDLE(25)` (25 × 4 ms = 100 ms), an open and
decodable menu, normal one-step navigation, genuine held-key repeat and stop,
both injected faults at a short pre-repeat duration, and the calibration pass
above. `idle_rate != 25`, a lost Pico, or an unresponsive U64 is a failure,
never a skip.

Detection does not take one before/after snapshot. The Pico executes a whole
press/fault/release sequence synchronously before its control call returns,
so nothing is observable mid-fault; from the moment the call returns,
`watch_bounded` polls the settling screen and raises the instant a sample
exceeds the allowed step count, rather than waiting for a fixed window to
finish. That is what ends a run on the first sign of a runaway instead of
only noticing it afterwards.

Every fifth soak iteration runs a genuine held key as a positive control. It
proves the firmware has not satisfied the fault cases by disabling auto-repeat
altogether: a real hold must still produce more than one movement and must stop
when the key is released.

Menu reads that are expected to have settled are taken twice and must agree
before a check uses them. A read that lands in the middle of a redraw would
otherwise report the previous selection and fail a check the firmware passed.

The runner uses `try/finally` to request `release_all`, verify the key state,
and close the U64 menu. Preserve its output and the printed seed when reporting
a failure.
