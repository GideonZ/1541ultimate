# Soak and stress tests

Tests that exercise a real device repeatedly to expose failures short
deterministic checks do not reach: connection leaks, listener exhaustion,
transport degradation, concurrency races and broken stream recovery. Duration
and load are part of the result, so these are not part of the release gate and
are not registered in `run-tests`.

| Path | Scope |
| --- | --- |
| `network/connection_test.py` | Long-running soak and stress across ICMP, UDP/64 identity discovery, the TCP/64 DMA command channel, Telnet, FTP, REST, the optional modem listener, and audio/video UDP streams |
| `network/listener_soak_test.py` | Short soak (about two minutes) that churns abandoned Telnet/FTP connections while REST stays in use, checking no session slot is lost and REST latency does not degrade |
| `io/usb/usb_keyboard_repeat_soak_test.py` | Pico 2 W hardware regression soak for #797 / PR #796 USB keyboard repeat |

`network/`'s `*_probe.py` modules, `stream_monitor.py` and
`connection_runtime.py` are protocol drivers and shared runtime for
`connection_test.py`.

## Running

`./run-tests -H <host> --soak` includes this stage, using the bounded
`stress` profile. To invoke a suite directly, use an explicit host. Python 3.11
or newer and the system `ping` command are required.

```sh
# Inspect the complete CLI without contacting a device.
tests/soak/network/connection_test.py --help

# Twelve-hour, one-runner concurrent soak with read/write probes and streams.
tests/soak/network/connection_test.py --profile soak -H u64

# Two-minute stress profile that saturates the four FTP and Telnet session slots.
tests/soak/network/connection_test.py --profile stress -H u64

# ~2 min: churns abandoned listener connections and checks REST does not degrade.
tests/soak/network/listener_soak_test.py -H u64 -p PASSWORD
```

`--help` is authoritative for profiles, probes, protocol surfaces, correctness
modes, ports, credentials and concurrency overrides. A run exits non-zero if
any probe or stream reports `FAIL`.

## USB keyboard repeat fixture

`usb-keyboard-repeat` uses a Raspberry Pi Pico 2 W as a real USB boot keyboard,
controlled over the local Wi-Fi network. It proves PR #796's `SET_IDLE(25)` /
`GET_IDLE` negotiation (25 HID units = 100 ms), normal held-key repeat, a lost
immediate release, and complete report silence -- both against a short tap and
against a hold long enough that genuine repeats are already in flight when the
release is lost. Detection reads the selected list row's exact number off the
screen and compares it against a baseline calibrated on the live device, not a
fixed guess, so it can tell one extra keystroke from several. It tests the
Ultimate menu, not the C64 matrix. Stress is about two minutes; soak is twelve
hours. Details: [`doc/usb-keyboard-repeat-pico.md`](doc/usb-keyboard-repeat-pico.md).

Put the Pico in BOOTSEL mode (hold BOOTSEL while connecting USB), then provision
it from the Linux test host. The setup command flashes **only the Raspberry Pi
Pico 2 W; it does not flash the Ultimate 64**. It downloads the pinned official
MicroPython UF2 to the user's cache at setup time; no firmware binary or Wi-Fi
credential is tracked.

```sh
PICO_WIFI_SSID='...' PICO_WIFI_PASSWORD='...' \
  tests/soak/io/usb/usb_keyboard_repeat_soak_test.py --setup-pico
```

When setup says so, unplug the Pico from Linux and connect it to a rear U64 USB
port, with both devices on the same network. The Pico must be the only USB
keyboard attached to the U64, and the U64 must run firmware containing PR #796;
the fixture reports `idle_rate: 0` otherwise. Then run:

```sh
./run-tests -H u64 --soak -s usb-keyboard-repeat
```

The suite always sends `release_all` and closes the menu on exit. Detailed
options, including `--duration`, `--pico-host`, and `--profile soak`, are in
`usb_keyboard_repeat_soak_test.py --help`.
The complete Pico setup and operating guide is
[`doc/usb-keyboard-repeat-pico.md`](doc/usb-keyboard-repeat-pico.md).

Preserve timestamped output for diagnosis:

```sh
mkdir -p logs/soak
set -o pipefail
tests/soak/network/connection_test.py --profile stress -H u64 2>&1 \
  | tee "logs/soak/u64-network-$(date -u +%Y%m%dT%H%M%SZ).log"
```

## Safety

Run against a dedicated test device. The default profiles are active tests:
they create and remove `u64test_*` files under `/Temp`, write reserved C64
page-3 bytes, change the `Vol UltiSid 1` setting between `0 dB` and `+1 dB`,
open incomplete or malformed sessions, and enable network streams. Normal
shutdown disables streams and cleans up tracked FTP files, but an abrupt host
or device failure can interrupt cleanup.

The Telnet `vanish` mode is more invasive. It fills the device's Telnet session
table and temporarily changes a host network alias; it needs an unused LAN
address, an interface name and passwordless `sudo -n ip`. Follow its CLI
constraints and never run it concurrently.

## Rules for extending

- Put a soak test under the subsystem it loads. Add a sibling of `network/`
  only when another production surface needs sustained testing.
- Name executable entry points `*_test.py` and keep reusable protocol logic in
  narrowly named helper modules. Python only.
- Add a new network probe to the runner's choices, supported surfaces,
  correctness modes and runner table together.
- Make generated resources unique per runner, verify every mutation, and clean
  up in a `finally` path or exit handler.
- Keep runs bounded with timeouts, emit timestamped protocol and result detail,
  and surface unexpected behaviour as `FAIL`. Retries must stay bounded and
  limited to documented transient protocol behaviour.
- Document destructive behaviour, privileges, target state and cleanup
  limitations here; keep flag-by-flag detail in `--help`.
