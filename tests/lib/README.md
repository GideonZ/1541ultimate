# Shared test library

Support code used by more than one test category. Everything here is
category-neutral: `tests/e2e/`, `tests/perf/`, `tests/soak/` and the
repository-root `run-tests` all use it.

| File | Purpose |
| --- | --- |
| `report.py` | Console and JSONL reporting for every suite and for the runner |
| `rest.py` | HTTP transport for the device: password header, JSON encoding, retry policy |
| `api.py` | The device's REST API as typed calls, built on `rest.py` |
| `ftp.py` | FTP sessions, listings, transfers, deletion and purging |
| `wait.py` | Bounded polling and retry |
| `pacing.py` | How fast the suites drive the on-device UI, with the measurements behind each value |
| `health.py` | One bounded sweep of every listener the suites need, plus proof the C64 is running |

Two registered suites live here as well, because both check the test tree
itself rather than the device and so need no hardware. They run first, where a
failure lands as a clear message instead of as a confusing one later:

| Suite | Checks |
| --- | --- |
| `check_transport_usage.py` | No suite has grown its own HTTP client again |
| `runner_policy_test.py` | When `run-tests` may run the recovery command, and what it exits with |

Put this directory on `sys.path` before importing:

```python
# tests/lib holds the helpers every suite shares.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))
from report import Failure, check, detail, section, suite_fail, suite_ok
```

## Talking to the device

Use `api.UltimateApi` for anything the REST API models. It mirrors the
firmware's own OpenAPI description group for group, returns decoded objects,
and rejects out-of-range arguments by name rather than letting them become an
HTTP 400.

```python
device = UltimateApi(host, password)
device.machine.reset()
device.machine.readmem(0x0400, 8)                  # -> bytes
device.drives.get("a").mounted                     # -> bool
device.files.info("/Temp/x.prg")                   # -> FileInfo, or None
```

Drop to `device.rest` only when the check is about the HTTP contract itself:
status codes, headers, malformed bodies, authentication.

Use `ftp.session` for FTP; it always closes, however the body ends.

```python
with ftp.session(host, password) as client:
    ftp.store(client, "/Temp/x.prg", payload)
    names = ftp.names(client, "/Temp")
```

Use `wait.wait_until` rather than a bare sleep or a hand-rolled deadline, so a
timeout says what it was waiting for.

## Driving the UI at a consistent speed

Take settle and poll values from `pacing`, never from a fresh constant in the
suite. Before this module the same idea appeared at 0.0, 0.045, 0.05, 0.10,
0.25, 0.30 and 0.35 seconds across the tree, so a suite that walked a file
listing was several times slower than one typing into a field for no reason
other than which constant it had inherited.

```python
import pacing
time.sleep(pacing.KEY_SETTLE_SECONDS)
```

Every value can be overridden for one run without editing code:

```
U64_UI_POLL_INTERVAL=0.03 ./run-tests u64
```

A suite whose subject *is* timing - key repeat, a modal that must not be raced -
passes its own number at the call site and says why. That is the exception the
module is designed around, not a violation of it.

## Reporting rules

- **One check, one line.** A check prints `[NN] <label> ... <verdict>` and
  nothing else. Use `check(label)`, or `check_start` with one of the verdict
  calls. Nested checks print nothing; only the outermost one does.
- **The verdict vocabulary is closed:** `OK`, `FAIL`, `WARN`, `SKIP`. Never
  `PASS`, `SUCCESS`, `VERIFIED`, `WARNING` or a bracketed form such as `[OK]`.
  Extra information goes in parentheses after the verdict: `OK (20 rows)`.
- **Colour, indentation, check numbering and elapsed time are the library's
  job.** Do not hand-roll any of them. If something is missing, add it here.
- **Never clear the terminal** and never emit an erase sequence.
- **Everything goes to stdout, flushed.**
- `progress` is for terminals only and prints nothing when output is captured.

| Purpose | Call | Output |
|---|---|---|
| A numbered check | `check(label)` or `check_start` | `[07] label ... OK` |
| An unnumbered step, for a harness's own gates | `step_start` | `label ... OK` |
| A verdict | `check_ok` / `check_fail` / `check_warn` / `check_skip` | `OK (3 rows)` |
| A continuation line under a check | `detail` | five-space indent |
| A group heading inside a suite | `section` | blank line, blue title |
| A top-level heading, for a harness | `banner` | blank line, blue title, rule |
| A warning belonging to no check | `warn` | `WARN <message>` |
| A suite's closing line | `suite_ok` / `suite_fail` / `suite_skip` / `suite_warn` | `input_test: OK (48 checks)` |
| A whole run's result, for a harness | `run_result` | JSONL only |
| A live progress line | `progress` / `progress_done` | terminal only |

## How long a check may take

`report` marks a check's duration `SLOW`, in yellow, once it passes
`SLOW_CHECK_SECONDS` (ten seconds). That is feedback, not a verdict: the run
still passes. It exists so a check that has quietly become slow is visible
while the run is happening, rather than only to someone reading a log
afterwards. The word is printed as well as the colour, so the mark survives a
redirected log and can be grepped for.

The guidance for what to do about one is in
[tests/e2e/README.md](../e2e/README.md), under the rules for adding a suite.

## Structured results

Set `E2E_JSONL` to a path to append the run as JSONL, one object per line.
`E2E_SUITE` names the suite in those records. `run-tests -j DIR` sets both for
every suite it starts, writing one file per suite run into DIR, and writes its
own `run` record to `DIR/run.jsonl` through `set_jsonl_path`. A harness that
parses its arguments after importing this module needs that setter, because
`E2E_JSONL` is read at import.

Every record carries `kind`, `suite` and `time`. The rest depends on the kind:

| `kind` | Fields |
|---|---|
| `check` | `index`, `label`, `verdict`, `extra`, `seconds`, `scenario` |
| `scenario` | `title`, `verdict`, `checks`, `seconds` |
| `suite` | `name`, `verdict`, `note`, `checks`, `seconds` |
| `warning` | `message` |
| `run` | `verdict`, `suites`, `passed`, `failed`, `skipped`, `dirty`, `seconds`, `recoveries`, `exit_code` |

```sh
./run-tests -H u64 -j runs/
jq -r 'select(.kind=="check" and .verdict!="OK") | "\(.suite) \(.label) \(.verdict)"' runs/*.jsonl
```

## Rules for extending

- Add something here only once a second category needs it. A helper one
  category uses belongs with that category.
- Depend on the standard library and on other modules in this directory only,
  so any suite can use it without pulling in a UI model or a suite fixture.
- Add a device endpoint to `api.py` rather than building a path in a suite, and
  check the API's own parameter limits there.
- Keep it Python. There is no shell implementation to keep in step.
