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

## Structured results

Set `E2E_JSONL` to a path to append the run as JSONL, one object per line.
`E2E_SUITE` names the suite in those records. `run-tests -j DIR` sets both for
every suite it starts, writing one file per suite run into DIR.

Every record carries `kind`, `suite` and `time`. The rest depends on the kind:

| `kind` | Fields |
|---|---|
| `check` | `index`, `label`, `verdict`, `extra`, `seconds`, `scenario` |
| `scenario` | `title`, `verdict`, `checks`, `seconds` |
| `suite` | `name`, `verdict`, `note`, `checks`, `seconds` |
| `warning` | `message` |
| `run` | `verdict`, `suites`, `passed`, `failed`, `skipped`, `dirty`, `seconds` |

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
