# Tests

Tests that exercise a built firmware image on real hardware. Fast host-side
unit tests live next to their owning code, such as `software/api/tests/`,
`software/filemanager/tests/` and `software/io/usb/tests/`.

| Directory | Purpose |
| --- | --- |
| [`e2e/`](e2e/) | Deterministic functional and regression checks across complete device workflows. The hardware release gate. |
| [`perf/`](perf/) | Timing and throughput benchmarks that measure a number rather than assert a pass/fail outcome. Not the gate. |
| [`soak/`](soak/) | Time- and load-based checks for leaks, exhaustion, races and transport degradation. Not the gate. |
| [`lib/`](lib/) | Support code shared by all three categories, plus the two gate checks that need no device. |

`./run-tests -H <host>` runs the E2E gate. Add `--perf`, `--soak` or `--all`
to run more, and `-m` to repeat the E2E suites in more than one UI profile:
`./run-tests --all -m all` runs everything. See `./run-tests --help`.

## Host requirements

The suites run on the machine driving the device, so a few Python packages have
to be present there. The firmware needs none of them.

```sh
pip install -r tests/requirements.txt
```

| Package | Needed by |
| --- | --- |
| `Pillow` | `e2e/api/input_test.py`, `e2e/io/printer/printer_test.py` |
| `pyftpdlib` | `e2e/filesystem/ftp_client_test.py` |
| `pytesseract` | `e2e/io/printer/printer_test.py`, only under `--stage verify` |

`pytesseract` is a wrapper around a separate binary, so it also needs
`tesseract` on `PATH` (`brew install tesseract`, or `apt install
tesseract-ocr`).

A package that is missing either fails its suite with a message naming what to
install, or reports the coverage it had to leave out. Neither case passes
quietly.

## Measuring heap leaks

`tests/soak/api/heap_leak_test.py` asserts that repeated REST operations give
their memory back, using `GET /v1/machine:heap`. A device running firmware
older than that endpoint answers 404 and the suite skips, so it is safe to run
against any image.

It measures the *slope* over repeated operations after a warmup, not a single
before/after. A first run legitimately allocates one-time caches and lazy
singletons that never come back; only a cost that repeats every iteration is a
leak. Comparing one cold run against another warm one reads those one-time
costs as a leak and is the easiest way to get this wrong.

## Command-line conventions

The runner and every suite it starts take the same flags for the same things,
so a command line reads the same wherever it is typed:

| Flag | Meaning |
| --- | --- |
| `-H`, `--host` | Device host name or IP |
| `-p`, `--password` | REST and FTP password |
| `-t`, `--timeout` | Per-request REST timeout, in seconds |
| `-r`, `--rest-host` | REST address, when it differs from `--host` |
| `-P`, `--telnet-port` | Telnet port |
| `-m`, `--mode` | UI transport: `telnet`, `freeze` or `overlay` |
| `-n`, `--no-assertions` | Warn instead of failing, for suites that support it |

`--mode` means the UI transport and nothing else. Two suites used the same word
for something of their own and have been renamed: `printer_test.py` takes
`--print-mode` for bitmap or text, and the network soak takes `--correctness`.
The only other "profile" in the tree is `run-tests --soak-profile`, which
chooses between the two-minute and twelve-hour soak.

## Naming the device

The runner passes the device to every suite explicitly, so these matter only
when a suite is started by hand. One name each, used by every suite:

| Variable | Meaning | Default |
| --- | --- | --- |
| `U64_HOST` | Device host name or IP | `u64` |
| `U64_PASS` | REST and FTP password | empty |
| `U64_TIMEOUT` | Per-request REST timeout, in seconds | per suite |
| `U64_REST_HOST` | REST address, when it differs from `U64_HOST` | `U64_HOST` |
| `U64_TELNET_PORT` | Telnet port for the UI transport | `23` |
| `U64_MODE` | Default UI profile: `overlay`, `freeze` or `telnet` | `overlay` |

`tests/lib/pacing.py` documents the `U64_UI_*` variables that change how fast
the suites drive the on-device UI.

## When a run goes wrong

By default the run continues through every selected suite, so one failure does
not hide the rest. `-x/--stop-on-fail` stops at the first one.

Everything else the runner does when things go badly follows from one idea.

### Health

Before each E2E suite, and again after one fails, the runner establishes that
the device is **healthy**:

- **it answers** - REST replies, polled patiently, because a device that is
  briefly busy is far more common than one that has gone;
- **its UI works** - the on-device UI reaches the documented state. A device can
  answer every request and still be impossible to drive: seen live, a browser
  stuck in a directory a killed run had deleted, with the UI task no longer
  reading injected keys, while REST, FTP, Telnet and the DMA port all answered
  normally;
- **its parts work** - the health sweep of every listener the suites need, and
  of the C64 underneath them.

They are checked in that order, because reaching the documented state shuts the
menu, and the jiffy and raster checks are skipped while it is open.

From that, three rules, and there are no others:

| Situation | What happens |
| --- | --- |
| A suite fails on a healthy device | It found something. The failure stands. |
| A suite fails on an unhealthy device | It showed nothing. Recover, run it again. |
| The device cannot be made healthy | Abandon the run rather than fail every remaining suite for the same reason. |

An unhealthy device is the **only** thing that triggers recovery. A failing suite
never triggers it on its own.

### The health sweep

```
prg-context-menu: health: ping=4ms rest=9ms ftp=8ms telnet=34ms ident=29ms \
    dma=6ms jiffy=20ms raster=20ms -> OK
```

`ping`, `rest`, `ftp`, `telnet`, `ident` and `dma` are the listeners the suites
depend on; `dma` is the control port on 64, a separate listener from the HTTP
server that has wedged on its own. `jiffy` (`$00A2`) and `raster` (`$D012`) are
read until they change, which is what separates "the services answer" from "the
machine under them is alive": a C64 stopped in Ultimax mode still serves REST
perfectly well. Those two are skipped, not failed, while the menu is open,
because under Freeze the menu has stopped the machine on purpose.

The sweep costs about 150ms and is on by default. `--no-health-check` reduces
health to "it answers", which is what you want when a listener is deliberately
off on a particular device and would otherwise read as unhealthy before every
suite.

`tests/lib/health.py` runs the sweep on its own, and takes `-c/--check` to run
only part of it:

```sh
python3 tests/lib/health.py -H u64
python3 tests/lib/health.py -H u64 -c rest -c dma
```

### Recovery and repetition

Recovering the device is the operator's command, because what brings one back
differs per setup - a JTAG download, a power switch, a flash tool - and none of
that belongs in this repository. Nothing here has a default, so recovery is off
until you ask for it:

```sh
./run-tests -H u64 --recover-command 'build u64'
```

| Option | Meaning | Default |
| --- | --- | --- |
| `--recover-command` | What to run when the device is unhealthy | none, so recovery is off |
| `--recover-max-per-suite` | Recoveries for one suite, which is also its extra attempts | 3 |
| `--recover-max-total` | Recoveries in the whole run | 10 |
| `--recover-timeout` | How long the command may take | 900s |
| `--no-retry` | Recover, but do not run the suite again | off, so it does retry |
| `--no-health-check` | Health means "it answers", nothing more | off, so the sweep runs |

A suite is repeated only while the device is what failed it, so repetition
needs no count of its own: `--recover-max-per-suite` is both how many times a
suite may have its device brought back and how many extra attempts it gets.

**Recovering is not free.** Every recovery counts against the run, and a run
that needed one exits 3 rather than 0 even when every suite passed afterwards.
A device that had to be brought back is not the same result as one that did
not, and the exit status says so.

## Reading the result

The exit status carries the outcome, so a caller does not have to parse the
console output:

| Status | Meaning |
| --- | --- |
| `0` | Every suite passed and the device never needed recovering |
| `1` | At least one suite failed |
| `2` | The command line was wrong |
| `3` | Every suite passed, but the device had to be recovered |
| `4` | The device could not be made healthy, and the run was abandoned |

`-j DIR` writes the same thing as JSONL: one file per suite run, plus
`run.jsonl` holding the run's own `run` record with `passed`, `failed`,
`skipped`, `dirty`, `recoveries` and `exit_code`. See
[tests/lib/README.md](lib/README.md) for the record shapes.

```sh
./run-tests -H u64 --recover-command 'build u64' -j runs/
jq -r 'select(.kind=="run") | "\(.verdict) failed=\(.failed) recoveries=\(.recoveries)"' runs/run.jsonl
```

## Rules

- Put a test in the narrowest matching category. Keep isolated logic tests
  beside the production component. Use `e2e/` only when the behaviour requires
  a real device or crosses subsystem boundaries, `soak/` when duration or
  repetition is essential to the result, and `perf/` when the result is a
  measurement rather than a verdict.
- Everything under `tests/` is Python. Do not add shell scripts.
- Report through `tests/lib/report.py`. Do not format result lines by hand.
- Shared support code goes in `lib/` only once a second category needs it.
  Until then it belongs with the category that uses it.

Each linked README is authoritative for running and extending that category.
