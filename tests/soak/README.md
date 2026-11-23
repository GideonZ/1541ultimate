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
| `io/c64/assembly_search_leak_test.py` | The online search browser's two costly paths, which are reachable only by driving the menu; the service is the machine's (Assembly 64, or CommoServe on a C64 Ultimate) |
| `filemanager/menu_navigation_soak_test.py` | The browser's page-key cursor movement and one-keystroke field clearing, against a listing of files named for their own index |
| `filemanager/prg_context_menu_leak_test.py` | The browser's PRG context-menu load actions, which go through a different code path from the REST launcher |
| `filemanager/browser_refresh_leak_test.py` | The filesystem-refresh matrix, which needs a browser open on a directory while its contents change |
| `filemanager/mount_cache_leak_test.py` | Entering one disk image after another, which mounts each one and holds the image file open |
| `io/iec/softiec_soak_test.py` | The Software IEC drive driven from the C64 over the real bus while REST and FTP load the device (#877). `--mode` selects a realistic C64 OS soak, a hostile stress that edge-tests the command channel, OPEN names, x00 files, channels, transfers, direct access, the disk image types, the UCI target and the PC lanes racing the bus, or `both` in turn. `--log` runs each phase with logging off, on, both in turn, or toggled during the phase, and correlates the device's syslog against every command, open and close the suite made; `--syslog-spool` names a file an external collector writes, and without it the suite binds the UDP port the device's syslog setting names when it points at this host. Fails if the firmware stops answering, if fixture data read back differs, if the heap does not come back, or if the log contradicts what the suite did; `--profile stress` runs the stress mode for ten minutes and `--profile soak` runs the soak mode for four hours, both with logging off. The log rendering and correlation live in the sibling module `softiec_log.py` |
| `io/iec/jiffy_load_soak_test.py` | LOAD from the Software IEC drive by a C64 program, again and again, with the status, error channel, end address and a checksum of every load checked (#986). Optionally slows the JiffyDOS host on purpose to reproduce the failing rows. The report arithmetic is in the sibling `jiffy_load.py`, checked device-free by `jiffy_load_test.py` |

The heap suites all read `GET /v1/machine:heap`. Firmware predating that
endpoint answers 404 and their checks skip, so they are safe to run against any
image. What they measure is the slope over repeated operations after a warmup
rather than a single before-and-after reading; the reason is in
[tests/README.md](../README.md).

`network/`'s `*_probe.py` modules, `stream_monitor.py` and
`connection_runtime.py` are protocol drivers and shared runtime for
`connection_test.py`. `io/iec/softiec_log.py` is the device-log rendering and
correlation module `io/iec/softiec_soak_test.py` imports.

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

# 100 LOADs of a 16 KiB file under the KERNAL the device runs, each checked: about 3 min
# under JiffyDOS (1.8 s a load), about 45 min under the stock KERNAL (26 s a load).
tests/soak/io/iec/jiffy_load_soak_test.py -H u64 --iterations 100
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

## JiffyDOS LOAD soak

`io/iec/jiffy_load_soak_test.py` formalises the evidence of PR #1130 for #986: an
intermittent `?LOAD ERROR` with JiffyDOS on the Software IEC drive, ST=66 while the error
channel stays at `00, OK`. A small C64 program (`io/iec/jiffy_load_soak.asm`, assembled
from source when the suite starts, nothing binary is committed) LOADs one file with the
KERNAL's own LOAD, `--iterations` times. After each load it records ST and carry, the
error channel, the end address and a checksum of what arrived, over a load area it filled
with a pattern first, so a load that stops short and reports success cannot pass. The host
reads C64 memory only between batches of at most 100 loads: a memory read halts the 6510,
and one that lands in a serial transfer spoils it.

The suite runs under the KERNAL the device has, whatever it is, and reports which it
found: `JiffyDOS`, `JiffyDOS with the host delay patch`, or `no JiffyDOS marker (stock or
other)`. `--kernal` and `--command-interface` work as in the other suites.

```sh
# JiffyDOS or stock, whatever the device runs: 100 loads from a directory of partition 1.
tests/soak/io/iec/jiffy_load_soak_test.py -H u64 --iterations 100

# What run-tests runs: --profile stress is 25 loads (about 45 s under JiffyDOS, 11 min
# under the stock KERNAL), --profile soak is 500 (15 min, 3.6 h). --iterations overrides it.
tests/soak/io/iec/jiffy_load_soak_test.py -H u64 --profile stress

# The same from a D64 image, 8 KiB files, on bus id 10.
tests/soak/io/iec/jiffy_load_soak_test.py -H u64 --source d64 --size 8 --device 10

# Under your own JiffyDOS C64 KERNAL (a temporary copy is uploaded and removed again).
tests/soak/io/iec/jiffy_load_soak_test.py -H u64 --jiffydos-rom ~/roms/JiffyDOS_C64.bin

# The host delay rows of PR #1130: the host's first sample after the drive releases DATA is
# 63, 88 or 103 cycles late, net (the PR table calls them 61, 86 and 101). The red/green row
# is below.
tests/soak/io/iec/jiffy_load_soak_test.py -H u64 --jiffydos-rom ~/roms/JiffyDOS_C64.bin \
  --host-delay 63 --partition-path /USB1/jd986_scratch --iterations 100
```

JiffyDOS is licensed and no image is shipped. `--jiffydos-rom PATH` names your own: the
suite patches a copy in memory, writes it to a temporary directory under a name of its own,
uploads that to the device's ROM directory for the run and removes both afterwards. It never
prints or keeps the bytes. The patch replaces one `ORA #$20 / TAX` pair in the LOAD routine
with a call to a short delay loop, and it is applied only to an image that is 8192 bytes and
holds that routine's code at one place, offset `$1B06`; any other image is refused. Without
`--jiffydos-rom`, `--host-delay` skips with a message. The option is the net number of
cycles added: the call replaces `ORA #$20 / TAX` (4 cycles) and the routine costs `5 * n + 11`
plus the 6 of the JSR, so a loop of `n` passes adds `13 + 5 * n`. The PR table's rows of 10, 15
and 18 passes, which it calls 61, 86 and 101, are 63, 88 and 103 here; `jiffy_load_test.py`
counts the cycles from the routine's own opcodes.

The suite sets `IEC Drive` to `Enabled` and `Soft Drive Bus ID` to `--device`, selects
partition 1, and builds a folder with the file in partition 1's root. It records the
settings it finds and changes, and when it ends it restores them, puts the drive's
directory back, removes the folder and the temporary KERNAL, and resets the C64, also
after a failure. It needs a Software IEC partition 1 and a bus id nothing else uses.

The medium matters. On an Ultimate 64 Elite whose partition 1 is the `/Temp` RAM disk, JiffyDOS
loads passed on firmware with the 50 us window with a host delay of 61 cycles (100 loads) and
of 101 and 151 cycles (30 loads each); the same row with the file on a USB stick failed. `--partition-path
DIR` therefore serves the file from `DIR`: the suite enables the Command Interface (and puts
the setting back), adds an unused partition number for `DIR` through the UCI SoftIEC target,
builds its folder there, and removes the partition and the folder when it ends. `DIR` must
exist, and the suite writes only the one folder under it. Do not
open the Ultimate menu while it runs: that freezes the C64 mid-load, which breaks a serial
LOAD without any firmware defect (`--help` and the Safety section apply).

### The red/green row for #986

The `jiffy-load-soak` entry of `run-tests` is a plain JiffyDOS/SoftIEC LOAD soak, whatever
KERNAL is installed, with the file in partition 1. It checks that LOAD works over many loads and
**cannot** tell the fixed firmware from the unfixed: it passes on both, so do not read it as a
guard for #986. The row that can is run by hand:

```sh
tests/soak/io/iec/jiffy_load_soak_test.py -H u64 --jiffydos-rom ~/roms/JiffyDOS_C64.bin \
  --host-delay 63 --partition-path /USB1/jd986_scratch --iterations 100
```

with the file on a USB (or other non-RAM-disk) partition through `--partition-path`, a
`--host-delay` and `--jiffydos-rom`. It is expected to fail on firmware with the 50 us window and to
pass with 100 us. From the `/Temp` RAM disk the delay row passes on both, because the drive
answers late there: the failures are at the first chunk of a LOAD, where the load ends at
the load address, and only when the drive answers before the host samples the bus.

### What a result looks like

```
[04] the loads ... FAIL (72 of 100 loads were not clean, 290s SLOW)
     load 1: ST=66, end $2000 (want $6000), checksum WRONG, error channel '00, OK,00,00'
     load 2: ST=66, end $2000 (want $6000), checksum WRONG, error channel '00, OK,00,00'
     further loads that were not clean are counted, not listed
     100 loads: 28 clean, 72 ST failures (ST=66 x72), 0 wrong content, 0 with an error status, 0 hangs; error channel 00 x100
     seconds per load: min 0.0, median 0.1, max 1.9
     ST failures with 00, OK on the error channel are the signature of #986
```

The totals are clean loads; ST failures with each ST value (ST=66 is bit 6, end of file,
and bit 1, a read timeout); loads that reported success with the wrong end address or
checksum; loads whose error channel was not `00`; and hangs, a load that made no progress
for `--load-timeout` seconds, which is ended by resetting the C64 before the run goes on.
A run passes only when every load is clean. Expected run times on an Ultimate 64 Elite
with 16 KiB loads: about 1.8 s a load under JiffyDOS, so 100 loads take about 7 minutes,
and about 26 s a load under the stock KERNAL, so 50 loads take about 30 minutes. A failing load
ends at once, so a failing run is shorter.

## Safety

Run against a dedicated test device. These are active tests, whichever
duration they are given: they create and remove `u64test_*` files under `/Temp`, write reserved C64
page-3 bytes, change the `Printer Settings` `Ink density` between `Medium` and
`High` over HTTP and through the Telnet settings menu, open incomplete or
malformed sessions, and enable network streams on a machine that serves them.
The Telnet probe answers No when leaving the menu offers to save to flash; a
machine whose `Auto Save Config` is `Yes` saves without asking, so a long run
there writes the settings flash on every pass through the menu. Normal
shutdown disables streams, puts `Ink density` back in memory and in flash, and
cleans up tracked FTP files, but an abrupt host or device failure can interrupt
cleanup.

The probes adapt to the machine `/v1/info` names: the DMA debug register and
the streams exist only on the Ultimate 64 family and the C64 Ultimate, so a
run against an Ultimate II+ leaves them out. The streams go to the multicast
groups the E2E stream suites use (`tests/lib/targets.py`), so a run points the
device's streams there and stops them when it ends.

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
- Keep the host-side code passing
  `ruff check --config tests/ruff.toml tests run-tests`. The MicroPython under
  `io/usb/pico/` is excluded from that lint, because it runs on the
  microcontroller rather than on the host and keeps its own compact style;
  everything else here is held to the same rules as the rest of the tree.
