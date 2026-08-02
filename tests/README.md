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
