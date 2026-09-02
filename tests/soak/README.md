# Soak and stress tests

Tests that exercise a real device repeatedly to expose failures short
deterministic checks do not reach: heap leaks, connection leaks, listener
exhaustion, transport degradation, concurrency races and broken stream
recovery. Duration and load are part of the result, so these are not part of
the release gate: they are registered in `run-tests` under the `soak` category
and run only when `--soak` or `--all` asks for them.

| Path | Scope |
| --- | --- |
| `api/heap_leak_test.py` | Repeated REST operations must give their memory back, measured through `GET /v1/machine:heap` |
| `network/connection_test.py` | Long-running soak and stress across ICMP, UDP/64 identity discovery, the TCP/64 DMA command channel, Telnet, FTP, REST, the optional modem listener, and audio/video UDP streams |
| `network/listener_soak_test.py` | Short soak (about two minutes) that churns abandoned Telnet/FTP connections while REST stays in use, checking no session slot is lost and REST latency does not degrade |
| `io/usb/usb_keyboard_repeat_soak_test.py` | Pico 2 W hardware regression soak for #797 / PR #796 USB keyboard repeat |
| `io/c64/assembly_search_leak_test.py` | The Assembly 64 search browser's two costly paths, which are reachable only by driving the menu |
| `filemanager/menu_navigation_soak_test.py` | The browser's page-key cursor movement and one-keystroke field clearing, against a listing of files named for their own index |
| `filemanager/prg_context_menu_leak_test.py` | The browser's PRG context-menu load actions, which go through a different code path from the REST launcher |
| `filemanager/browser_refresh_leak_test.py` | The filesystem-refresh matrix, which needs a browser open on a directory while its contents change |
| `filemanager/mount_cache_leak_test.py` | Entering one disk image after another, which mounts each one and holds the image file open |

The heap suites all read `GET /v1/machine:heap`. Firmware predating that
endpoint answers 404 and their checks skip, so they are safe to run against any
image. What they measure is the slope over repeated operations after a warmup
rather than a single before-and-after reading; the reason is in
[tests/README.md](../README.md).

`network/`'s `*_probe.py` modules, `stream_monitor.py` and
`connection_runtime.py` are protocol drivers and shared runtime for
`connection_test.py`.

## Running

`./run-tests -H <host> --soak` includes this stage. The network soak runs
under its bounded `stress` duration unless `--soak-duration soak` asks for the
twelve-hour one; `--soak-profile` is still accepted as an alias for that flag.
To invoke a suite directly, use an explicit host. The host requirements are in
[tests/README.md](../README.md), and `network/connection_test.py` additionally
needs the system `ping` command.

```sh
# Inspect the complete CLI without contacting a device.
tests/soak/network/connection_test.py --help

# Twelve-hour, one-runner concurrent soak with read/write probes and streams.
tests/soak/network/connection_test.py --profile soak -H u64

# Two-minute stress profile that saturates the four FTP and Telnet session slots.
tests/soak/network/connection_test.py --profile stress -H u64

# ~2 min: churns abandoned listener connections and checks REST does not degrade.
tests/soak/network/listener_soak_test.py -H u64 -p PASSWORD

# ~2 min: page-key jumps and field clears, checked against a known listing.
tests/soak/filemanager/menu_navigation_soak_test.py -H u2@c64u
```

## Choosing the injected-key drain

`menu_navigation_soak_test.py` takes `--key-drain`, which overrides
`pacing.SPLIT_KEY_DRAIN_SECONDS` for one run. That is how the constant is
chosen: its checks fail on a lost key rather than reporting a slow one, so
sweeping the rate downwards finds the point where they start failing, and the
constant is set one step back from it. The rates and what they cost are in
[`../e2e/doc/key-injection-rate.md`](../e2e/doc/key-injection-rate.md).

```sh
for rate in 0.03 0.04 0.05 0.06 0.08 0.10; do
  tests/soak/filemanager/menu_navigation_soak_test.py \
    -H u2@c64u --iterations 8 --key-drain $rate
done
```

`--help` is authoritative for profiles, probes, protocol surfaces, correctness
modes, ports, credentials and concurrency overrides. A run exits non-zero if
any probe or stream reports `FAIL`.

## USB keyboard repeat fixture

`usb-keyboard-repeat` uses a Raspberry Pi Pico 2 W as a real USB boot keyboard,
controlled over the local Wi-Fi network. It proves PR #796's `SET_IDLE(25)` /
`GET_IDLE` negotiation (25 HID units = 100 ms), normal held-key repeat, a lost
immediate release, and complete report silence, both against a short tap and
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

Run against a dedicated test device. These are active tests, whichever
duration they are given: they create and remove `u64test_*` files under `/Temp`, write reserved C64
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
