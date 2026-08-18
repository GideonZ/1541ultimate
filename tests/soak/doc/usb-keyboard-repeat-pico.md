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
selection. Before the checks run, the test creates a directory of 60 files on
the volatile RAM disk over FTP and navigates into it, which gives a list deep
enough that a held key never reaches the end. Nothing is written to flash or to
attached storage, and the directory is removed on exit.

That navigation is done with menu keys injected over REST
(`machine:input`), which reach the same on-screen menu as the USB keyboard.
The Telnet remote menu is a separate 60-column user interface instance and does
not move the on-screen selection. In the on-screen menu, cursor left leaves a
directory and `run_stop` closes the menu altogether.

## What the checks assert

Before a soak begins, the test requires one keyboard HID interface, U64
`SET_IDLE(25)` followed by `GET_IDLE(25)` (25 × 4 ms = 100 ms), an open and
decodable menu, normal one-step navigation, genuine held-key repeat and stop,
and both injected faults. The faults are a missing immediate release with idle
reports still flowing, and complete report silence. Each must produce exactly
one menu navigation action. `idle_rate != 25`, a lost Pico, or an unresponsive
U64 is a failure, never a skip.

Exactly one movement is the correct expectation for both faults, including the
silent one, because the two timings do not overlap: the menu starts repeating
after 320 ms (`first_delay` of 16 ticks in `keyboard_usb.cc`), while a stale key
latches after three idle periods, or 300 ms
(`USB_REPEAT_STALE_IDLE_PERIODS * 100 ms`). The key goes stale before the
initial repeat delay expires, so no repeat is ever produced from a stale key.

Every fifth soak iteration runs a genuine held key as a positive control. It
proves the firmware has not satisfied the fault cases by disabling auto-repeat
altogether: a real hold must still produce more than one movement and must stop
when the key is released.

Menu reads are taken twice and must agree before a check uses them. A read that
lands in the middle of a redraw would otherwise report the previous selection
and fail a check the firmware passed.

The runner uses `try/finally` to request `release_all`, verify the key state,
and close the U64 menu. Preserve its output and the printed seed when reporting
a failure.
