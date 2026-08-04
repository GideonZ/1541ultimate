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

### The health sweep

Before each E2E suite, and again after one fails, the runner sweeps the device
and logs the result as one line:

```
prg-context-menu: health: ping=4ms rest=9ms ftp=8ms telnet=34ms ident=29ms \
    dma=6ms jiffy=20ms raster=20ms -> OK
```

`ping`, `rest`, `ftp`, `telnet`, `ident` and `dma` are the listeners the suites
depend on; `dma` is the control port on 64, which is a separate listener from
the HTTP server and has wedged on its own. `jiffy` (`$00A2`) and `raster`
(`$D012`) are read until they change, which is what separates "the services
answer" from "the machine under them is alive": a C64 stopped in Ultimax mode
still serves REST perfectly well. Those two are skipped, not failed, while the
menu is open, because under Freeze the menu has stopped the machine on purpose.
The whole sweep costs about 150ms. `tests/lib/health.py` runs it on its own:

```sh
python3 tests/lib/health.py -H u64
```

### Recovery

A suite that fails is a result, not a problem with the run. A degraded device
is different: every suite after it fails for the same reason and says nothing
new. Recovery therefore acts on what the sweep found, never on a suite that
merely failed, and it also runs when the device is still unreachable after the
ordinary wait for it to come back.

```sh
./run-tests -H u64 --recover-command 'build u64'
```

The command is the operator's. What brings a device back differs per setup - a
JTAG download, a power switch, a flash tool - and none of that belongs in this
repository, which is why nothing here has a default.

| Option | Meaning | Default |
| --- | --- | --- |
| `--recover-command` | The command to run | none, so recovery is off |
| `--recover-max-per-suite` | Recoveries around one suite before giving up on it | 3 |
| `--recover-max-total` | Recoveries in the whole run before giving up | 10 |
| `--recover-timeout` | How long the command may take | 900s |

After a successful recovery the suite is retried once, so its failure is only
believed once it has had a run on a healthy device. **Recovering is not free:**
every recovery counts against the run, and a run that needed one exits 3 rather
than 0 even when every suite passed. A device that had to be brought back is
not the same result as one that did not, and the exit status says so.

## Reading the result

The exit status carries the outcome, so a caller does not have to parse the
console output:

| Status | Meaning |
| --- | --- |
| `0` | Every suite passed and the device never needed recovering |
| `1` | At least one suite failed |
| `2` | The command line was wrong |
| `3` | Every suite passed, but the device had to be recovered |
| `4` | The device stopped answering and could not be recovered |

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
