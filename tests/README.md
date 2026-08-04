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

A suite that fails is a result, not a problem with the run. A device that stops
answering is different: every suite after it fails for the same reason and says
nothing new. The runner waits for it to come back first, because the usual
cause is a device that is briefly busy rather than gone. If it is still not
answering after that wait, and only then, `--recover-command` runs:

```sh
./run-tests -H u64 --recover-command 'build u64'
```

The command is the operator's. What brings a device back differs per setup - a
JTAG download, a power switch, a flash tool - and none of that belongs in this
repository, which is why nothing here has a default. `--recover-attempts` caps
how many times it may run in one run, and `--recover-timeout` how long it may
take. The suite that lost the device is retried once after a successful
recovery. A failing suite never triggers the command.

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
