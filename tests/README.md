# Tests

Tests that exercise a built firmware image on real hardware. Fast host-side
unit tests live next to their owning code, such as `software/api/tests/`,
`software/filemanager/tests/` and `software/io/usb/tests/`.

| Directory | Purpose |
| --- | --- |
| [`e2e/`](e2e/) | Deterministic functional and regression checks across complete device workflows. The hardware release gate. |
| [`perf/`](perf/) | Timing and throughput benchmarks that measure a number rather than assert a pass/fail outcome. Not the gate. |
| [`soak/`](soak/) | Time- and load-based checks for leaks, exhaustion, races and transport degradation. Not the gate. |
| [`lib/`](lib/) | Support code shared by all three categories, plus the gate checks that need no device at all. |

`./run-tests <target>` runs the E2E gate. Add `--perf`, `--soak` or `--all`
to run more, and `-m` to repeat the E2E suites in more than one UI mode:
`./run-tests --all -m all` runs everything. See `./run-tests --help`.

## Getting started

Everything here runs on the machine driving the device rather than on the
device, and that machine needs three things: `python3`, the system `ping`
command, and a network route to the device. The suites reach it over REST on
port 80, and some of them also over FTP on 21, Telnet on 23 and the DMA control
port on 64. A missing `ping` is not fatal, but its health check then reports a
skip rather than a pass, so one fewer thing is being watched. Several suites
need host Python packages as well; they are listed under "Host requirements"
below, and a missing one fails its own suite with a message naming what to
install instead of spoiling the run.

Nothing is installed on the device. It has to be reachable, otherwise idle, and
running the firmware you mean to test.

A run is aimed at a target, which is either a host name or
`cartridge@computer`; "Targets" below explains why the second form exists. The
runner takes the target on the command line and falls back to `U64_HOST` when
none is given. `U64_PASS` carries the device's REST and FTP password, and is
empty on a device that has not been given one.

```sh
export U64_HOST=u64                   # the device this bench usually tests
export U64_PASS=secret                # omit it when the device has no password

./run-tests                           # the quick profile, against $U64_HOST
./run-tests u64                       # the same, naming the target
./run-tests --profile smoke u64       # is the device alive and drivable
./run-tests -s prg-context-menu u64   # one suite
./run-tests --list                    # every registered suite and its profile
./run-tests --list-profiles           # which suites each profile selects
```

Neither `--list` nor `--list-profiles` contacts a device, so either is a safe
first command on a bench you have not used before. When a run does not work,
`python3 tests/lib/health.py -H u64` is the smallest thing that talks to the
device and it says which of the device's listeners answered.

`./run-tests --help` is authoritative for every option. The rest of this file
is the background it has no room for.

## Targets

A target names what is being tested:

| Form | Meaning |
| --- | --- |
| `host` | a device that is also its own C64-side computer |
| `cartridge@computer` | a cartridge under test, in the computer that supplies its C64 keyboard and video |

An Ultimate II is a cartridge. It serves its own menu, memory and
configuration, but has no keyboard of its own: `machine:input` answers HTTP 501
there, so keys are injected into the computer it is plugged into and reach it
over the expansion port. Everything else stays with the cartridge.

```sh
./run-tests u64
./run-tests u2@c64u
./run-tests u64 c64u u2@u64-2
```

Naming several targets runs several ordinary runs of the runner, one child
process per target, and prefixes every output line with the target it came
from. A target occupies the machines it names, and two targets that share one
never run at the same time: `u64` and `u2@c64u` run together, while `c64u` and
`u2@c64u` take turns. `-o DIR` gives each target a subdirectory of its own.

A bench where a cartridge is permanently in one computer can say so once
instead of spelling it out on every command line:

```sh
export U64_COMPUTERS=u2@c64u
./run-tests u2 c64u          # u2 means the u2 in the c64u, so these take turns
```

Two tokens that name the same pair of machines are one target: with that
variable set, `./run-tests u2@c64u c64u u2` runs two targets and says it
dropped the third spelling.

The same token is accepted wherever a device is named, including by a suite
started by hand:

```sh
python3 tests/e2e/monitor/monitor_test.py -H u2@c64u -m telnet
```

[`tests/lib/targets.py`](lib/targets.py) is the one place the grammar and the
routing live.

## Host requirements

The suites run on the machine driving the device, so a few Python packages have
to be present there. The firmware needs none of them.

```sh
pip install -r tests/requirements.txt
```

| Package | Needed by |
| --- | --- |
| `openapi-python-client` | `e2e/api/openapi_contract_test.py`, generates a client from the OpenAPI document |
| `openapi-schema-validator` | `tests/lib/openapi_contract.py`, checks device answers against the document |
| `openapi-spec-validator` | `e2e/api/openapi_contract_test.py`, and `make openapi_validate`, which check the documents themselves against OpenAPI 3.1. Pinned in `tools/openapi/requirements.txt`, which this file includes, because the make target is a build gate |
| `Pillow` | `e2e/api/input_test.py`, `e2e/io/printer/printer_test.py` |
| `PyYAML` | `tests/lib/openapi_contract.py`, reads `doc/api/rest_api_openapi_*.yaml` |
| `pyftpdlib` | `e2e/filesystem/ftp_client_test.py` |
| `pytesseract` | `e2e/io/printer/printer_test.py`, only under `--stage verify` |
| `ffmpeg`, `ffprobe` | `./run-tests --record` only. Not Python packages. The lossless default needs the `libx264rgb` encoder, which keeps the frames pixel exact; a build without it is refused at startup rather than after half an hour of capture. |
| `pandoc`, `weasyprint` | Optional, never installed in CI: one command turns `index.md` into a PDF. |

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
| `-H`, `--host` | Target: a host, or `cartridge@computer` |
| `-p`, `--password` | REST and FTP password |
| `-t`, `--timeout` | Per-request REST timeout, in seconds |
| `-r`, `--rest-host` | REST address, when it differs from `--host` |
| `-P`, `--telnet-port` | Telnet port |
| `-m`, `--mode` | UI transport: `telnet`, `freeze` or `overlay` |
| `-n`, `--no-assertions` | Warn instead of failing, for suites that support it |

`--mode` means the UI transport and nothing else. Two suites used the same word
for something of their own and have been renamed: `printer_test.py` takes
`--print-mode` for bitmap or text, and the network soak takes `--correctness`.
"Profile" now means one thing throughout: the named bundle `--profile` selects,
described under "How much runs" below. The flag that chooses between the
two-minute and twelve-hour network soak is `--soak-duration`, which is what it
actually selects; `--soak-profile` still works as an alias.

## Naming the device

The runner passes the device to every suite explicitly, so these matter only
when a suite is started by hand. One name each, used by every suite:

| Variable | Meaning | Default |
| --- | --- | --- |
| `U64_HOST` | Target, when none is given on the command line | `u64` |
| `U64_PASS` | REST and FTP password | empty |
| `U64_TIMEOUT` | Per-request REST timeout, in seconds | per suite |
| `U64_REST_HOST` | REST address, when it differs from `U64_HOST` | `U64_HOST` |
| `U64_REST_PORT` | REST port | `80` |
| `U64_FTP_PORT` | FTP port | `21` |
| `U64_TELNET_PORT` | Telnet port for the UI transport | `23` |
| `U64_DMA_PORT` | DMA control port | `64` |
| `U64_MODE` | Default UI mode: `overlay`, `freeze` or `telnet` | `overlay` |
| `U64_COMPUTERS` | Which computer each cartridge is plugged into, as `u2@c64u[,...]` | none |

`tests/lib/pacing.py` documents the `U64_UI_*` variables that change how fast
the suites drive the on-device UI.

## Settings a run changes

Before the first suite the runner reads every setting each machine the target
occupies is running with, and when the run ends, however it ends, it writes
back the ones that differ. A cartridge target captures both machines. That is
what makes a suite's failure independent of what ran before it: the next run
starts from the configuration this one found.

Clock Settings is left alone, because it is a real-time clock rather than a
preference. `PUT /v1/configs/<store>/<item>` sets the value and effectuates it
without writing flash, so neither the capture nor the restore changes what the
machine boots with. `--no-restore-settings` turns the whole thing off.

The restore only warns. The run has produced its verdict by then, and a machine
that will not take a value back is something for the operator to see rather
than a reason to change what the suites decided.

## How much runs: profiles

A **profile** is a named bundle of configuration chosen when the run starts,
the way Maven's `-P` and Spring's `@Profile` are: it decides which suites and
scenarios run, which UI transports are swept, and whether the suites an
ordinary run leaves out are included. What an individual suite or scenario
declares about itself is a **tag**, the way JUnit 5's `@Tag`, TestNG's groups,
NUnit's categories, xUnit's traits, pytest's markers and CTest's labels are.

Profiles are ordered and cumulative, so a suite names the shallowest profile
that includes it and every deeper profile picks it up:

| `--profile` | Measured | Transports | When you reach for it |
| --- | ---: | --- | --- |
| `smoke` | 1 min | overlay | After a deploy, a reflash, or a wedge recovery |
| `quick` | 5 min | overlay | **The default.** Before pushing |
| `standard` | 15 min | overlay | The merge gate, and CI on a pull request |
| `deep` | 60 min | overlay, freeze | Nightly. Adds the manual suites |
| `exhaustive` | 90 min | all three | Before a release, or chasing a ghost |

The durations are measured rather than intended: every suite the profile
selects, at the duration it recorded on a full Ultimate 64 run on 2026-09-02,
multiplied by the transports swept.

Two things always win over the profile, because they are more specific
instructions: `-s/--suite` runs a suite whatever the profile says, and
`-m/--mode` overrides the transports the profile would sweep.

```sh
./run-tests                             # quick, the default
./run-tests --profile smoke u64         # is the device alive and drivable
./run-tests --profile deep u64          # nightly, both transports, manual too
./run-tests --profile smoke -s ftp-client u64   # -s wins over the profile
```

### What each profile selects

`run-tests --list` prints every registered suite with its own profile.
`run-tests --list-profiles` answers the other question, which suites each
profile picks up, as a matrix. Neither needs a device. Give
`--list-profiles smoke,quick` a comma-separated list to compare only those
profiles, and `--format json` to get the same answer for a script.

The matrix below is generated by `tools/docs/update_test_docs.py`; run that
script after adding or retagging a suite rather than editing the block by hand.

<!-- BEGIN: profile-matrix -->
```
Profiles, shallowest first. A suite runs from its own profile up.
'x' selected, '.' not. A profile sweeping two transports runs every suite twice.

                                smoke  quick standa   deep exhaus
                               ------ ------ ------ ------ ------
  transports                        1      1      1      2      3
  manual suites                     -      -      -    yes    yes
  default                           -    yes      -      -      -

e2e:
  assembly64                        .      x      x      x      x
  browser-filesystem-refresh        .      .      x      x      x
  browser-long-filename             .      x      x      x      x
  cfg-partial-effectuate            .      .      .      x      x
  cfg-single-group                  .      x      x      x      x
  cfg-unknown-items                 .      .      x      x      x
  cfg-whitespace                    .      .      x      x      x
  create-disk-image                 .      x      x      x      x
  doom-release                      .      .      .      x      x
  esp-depends                       x      x      x      x      x
  freeze-menu                       .      .      x      x      x
  ftp-client                        .      x      x      x      x
  ftp-server                        x      x      x      x      x
  input                             .      x      x      x      x
  input-batching                    x      x      x      x      x
  key-injection                     .      .      x      x      x
  machine-code-monitor              .      .      .      x      x
  menu-screen                       x      x      x      x      x
  navigation-keys                   x      x      x      x      x
  observability                     .      .      x      x      x
  openapi-contract                  .      x      x      x      x
  openapi-validator                 .      x      x      x      x
  power-cycle                       .      .      .      x      x
  prg-context-menu                  .      .      x      x      x
  prg-load-path-trim                .      x      x      x      x
  printer                           .      x      x      x      x
  readmem-writemem                  x      x      x      x      x
  rest-api-coverage                 .      x      x      x      x
  reu-turbo                         .      .      x      x      x
  runner-policy                     .      x      x      x      x
  telnet-drain                      .      x      x      x      x
  telnet-stale-session              .      .      .      x      x
  temp-auto-cleanup                 .      .      x      x      x
  transport-usage                   x      x      x      x      x
  uci-net-target                    .      .      .      x      x
  uci-targets                       .      .      x      x      x
  ui-backend-smoke                  x      x      x      x      x
  usb-bulk-out-integrity            .      .      .      x      x
  wake-on-wifi                      .      .      .      x      x

perf:
  rest-latency                      .      .      x      x      x
  telnet-key-latency                .      .      x      x      x
  temp-auto-cleanup-perf            .      .      x      x      x
  typing-speed                      .      .      x      x      x

soak:
  assembly-search-leak              .      .      x      x      x
  browser-refresh-leak              .      .      x      x      x
  heap-leak                         .      .      x      x      x
  listener-soak                     .      .      x      x      x
  menu-navigation                   .      .      x      x      x
  mount-cache-leak                  .      .      x      x      x
  network-connection                .      .      x      x      x
  prg-context-menu-leak             .      .      x      x      x
  usb-keyboard-repeat               .      .      .      x      x

                               ------ ------ ------ ------ ------
  suites                            8     21     43     52     52
  suite runs                        8     21     43    104    156

Scenario and check counts, and durations, are not shown here:
the registry does not know them. They depend on the machine and are
only knowable by running, so they come from --measured.
```
<!-- END: profile-matrix -->

A check is a call made while a suite runs, and how many of them happen depends
on the machine: a declared firmware gap turns a scenario into one skip, and a
listing's length decides how many rows a matrix walks. So `--measured
PROFILE=DIR` supplies those numbers from a `-o` directory that profile
produced, adding a table per profile of what each suite ran on each machine and
how long it took. `--format markdown` renders those tables as Markdown; the
matrix above is aligned text whichever format is asked for.

```sh
./run-tests --profile quick -o runs/quick u64
./run-tests --list-profiles quick --measured quick=runs/quick
```

## When a run goes wrong

By default the run continues through every selected suite, so one failure does
not hide the rest. `-x/--stop-on-fail` stops at the first one.

Everything else the runner does when things go badly follows from one idea.

### Health

Before each E2E suite that is handed a device, and again after one fails, the
runner establishes that the device is **healthy**:

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

Eight of the E2E suites are handed no device at all: their entry in the
registry names no host, because they read the tree or check the runner's own
decisions rather than a device. The health sweep and the UI-state gate in front
of such a suite are skipped, and so is the state check after it. A suite with
no device cannot be affected by the device's state and cannot leave it dirty,
so both gates could only cost time and misattribute a fault. Four of those
suites are in the smoke profile.

Three rules follow, and there are no others:

| Situation | What happens |
| --- | --- |
| A suite fails | It runs again, up to `--attempts` executions in total. A failure that repeats every time is the suite's verdict; one that does not is a flake, and the run records which. |
| The device is unhealthy | It is recovered before the next attempt, when a recovery command was given. An attempt on a degraded device shows nothing. |
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
| `--recover-max-per-suite` | Recoveries allowed around one suite | 3 |
| `--recover-max-total` | Recoveries in the whole run | 10 |
| `--recover-timeout` | How long the command may take | 900s |
| `--attempts` | How many times a suite may run in total, counting the first | 3 |
| `--no-retry` | Run every suite exactly once. An alias for `--attempts 1` | off, so it does retry |
| `--no-health-check` | Health means "it answers", nothing more | off, so the sweep runs |
| `--profile` | How much of the tree to cover | `quick` |
| `--soak-duration` | How long the network soak runs (was `--soak-profile`) | `stress` |
| `--no-restore-settings` | Leave the settings however the run left them | off, so they are put back |

`--attempts` is one ceiling on executions, whatever mixture of reasons produced
them. A recovery does not buy an extra attempt, so a suite cannot run three
flake retries times three recovery retries. `--recover-max-per-suite` bounds
recoveries and can only reduce the number of executions a suite gets, never add
one.

**Neither retrying nor recovering is free.** A run in which a suite needed a
second attempt exits 1, and a run in which a device had to be brought back
exits 2, even when every suite passed in the end. Neither is the same result as
a run that needed nothing, and the exit status says so.

## Reading the result

The exit status carries the outcome, so a caller does not have to parse the
console output:

| Status | Meaning |
| --- | --- |
| `0` | Every suite passed first time, and nothing needed recovering |
| `1` | Every suite passed, but at least one needed more than one attempt |
| `2` | Every suite passed, but a device had to be recovered |
| `3` | A suite failed every attempt it was given |
| `4` | A device could not be made healthy, and the run was abandoned |
| `64` | The command line was invalid |

The first five are a severity scale in numeric order, so a caller may compare
the status with an ordering operator: `[ $? -le 2 ]` tolerates a retry and a
recovery and nothing worse. 64 is off that scale on purpose, because a usage
error is not an outcome of a run; the value is `EX_USAGE` from `sysexits.h`.

With several targets the run takes the worst of its children's statuses, on
that same scale. A child that exited with a status the runner does not produce
was killed or crashed, and counts as a failed suite.

`-o DIR` keeps the whole run under one directory per target:
`DIR/<slug>/run.jsonl` holds that target's `run` record with `passed`,
`failed`, `skipped`, `dirty`, `recoveries` and `exit_code`, beside one file per
suite run. A run of several targets also writes `DIR/run.jsonl` for the run as
a whole. See [tests/lib/README.md](lib/README.md) for the record shapes and the
tree.

```sh
./run-tests -H u64 --recover-command 'build u64' -o runs/
jq -r 'select(.kind=="run") | "\(.verdict) failed=\(.failed) recoveries=\(.recoveries)"' runs/u64/run.jsonl
```

`tools/e2e_report.py` turns that tree into one Markdown document covering the
whole run, every target included. It needs no device and runs afterwards:

```sh
python3 tools/e2e_report.py runs/
less runs/index.md
```

`--record` adds a video of the run: the harness's screen and the device's video
side by side with the device's audio, one file per target, subtitles naming the
check at each moment, chapters per suite run, and a handful of stills the report
inlines as text. It is off by default because it costs the device two streams
and the LAN their bandwidth for the length of the run.

```sh
mpv --sub-file=runs/u64/video.srt --scale=nearest runs/u64/video.mp4
grep FAIL runs/u64/video.srt
```

`runs/index.md` opens with one greppable status line, then the verdicts, what
the run did not run, every failing check with the facts the run recorded about
it and the command that runs it again, and every health sweep. Below the
`<!-- detail -->` marker it carries the whole timeline, every check and where
the time went. Why each part is the shape it is, is in
[tests/e2e/doc/observability-spec.md](e2e/doc/observability-spec.md).

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
