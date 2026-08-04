# Tests

Tests that exercise a built firmware image on real hardware. Fast host-side
unit tests live next to their owning code, such as `software/api/tests/`,
`software/filemanager/tests/` and `software/io/usb/tests/`.

| Directory | Purpose |
| --- | --- |
| [`e2e/`](e2e/) | Deterministic functional and regression checks across complete device workflows. The hardware release gate. |
| [`perf/`](perf/) | Timing and throughput benchmarks that measure a number rather than assert a pass/fail outcome. Not the gate. |
| [`soak/`](soak/) | Time- and load-based checks for leaks, exhaustion, races and transport degradation. Not the gate. |
| [`lib/`](lib/) | Support code shared by all three categories. Not a suite. |

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
