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
| `targets.py` | What a run is aimed at: which of a target's machines serves what, and where each surface is |
| `device_double.py` | One fake Ultimate on loopback, for the observability tests |
| `syslog_collector.py` | The devices' own log, collected off the network while a run happens |
| `fixtures/e2e-run.expected.md` | The report generated from a fixture the tests build for themselves; see below |

Two registered suites live here as well, because both check the test tree
itself rather than the device and so need no hardware. They run first, where a
failure lands as a clear message instead of as a confusing one later:

| Suite | Checks |
| --- | --- |
| `check_transport_usage.py` | No suite has grown its own HTTP client again |
| `runner_policy_test.py` | When `run-tests` may run the recovery command, and what it exits with |
| `observability_test.py` | The harness that watches a run: the report generator, the console capture and everything else the gate's own verdicts cannot exercise |

`observability_test.py` also runs as `make observability_test` and as a step in
`.github/workflows/build.yml`. One implementation, invoked three ways. It needs
no device and no network beyond loopback.

The golden tier of `observability_test.py` builds its own `-j` tree by driving
the runner against the device double, with stub suites scripted to fail, to be
retried, to be killed mid-line and to skip. Nothing about that tree is checked
in: it is scratch space, built once per process and thrown away afterwards, so
a generated run, including any binary a recording would add, never lives in
git. `fixtures/e2e-run.expected.md` is the one thing that is checked in: the
report generated from that tree. Comparing it against a document generated for
a fresh build first puts both through a fixed substitution for what a live
build cannot hold still (commit, host, timings, its own scratch directory), so
the comparison is stable however many times the fixture is rebuilt even though
the checked-in document itself is not. A change to a record shape or to a
rendering rule shows up as a diff of that document, which is exactly the diff
a reader of a real report would see. Re-record it with:

```sh
python3 tests/lib/observability_test.py --record-fixture
```

That is a deliberate act. Regenerating the expected document is how a rendering
change is reviewed, so it is never a side effect of running the suite.

## Where a device is

`targets.Target` is the one object that answers where every surface of a device
is: which of its machines serves a REST path, where keyboard injection goes,
which machines it occupies, and the REST, FTP, Telnet and DMA ports.
The defaults are the real device's, so a target parsed from a token needs
nothing set. `U64_REST_PORT`, `U64_FTP_PORT`, `U64_TELNET_PORT` and
`U64_DMA_PORT` move one for a caller addressing something else.

A library here takes either a token or a resolved handle, so a suite keeps
passing whatever its own `-H` gave it:

```python
targets.resolve("u2@c64u").host_for("/v1/machine:input")   # -> "c64u"
UltimateApi(target)                                        # a handle works too
```

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
`E2E_SUITE` names the suite in those records. `run-tests -o DIR` sets both for
every suite it starts. A harness that parses its arguments after importing this
module needs `set_jsonl_path`, because `E2E_JSONL` is read at import.

The tree has one shape whether the run was asked for one target or several:

```
DIR/
  index.md                     the report, written by tools/e2e_report.py
  run.jsonl                    the parent's own record, multi-target runs only
  run.log                      the parent's console output, multi-target only
  syslog-unmapped.txt          log lines from an address no target claims
  <slug>/
    run.jsonl                  this target's runner records
    run.log                    this target's runner console output
    <label>-<suite>.jsonl      one file per suite run
    <label>-<suite>.log        that suite run's console output
    <label>-<suite>.telnet.log the raw Telnet session stream, telnet mode only
    screens.jsonl              every distinct screen the harness read
    video.mp4                  the recording, with --record
    video.srt                  subtitles naming the suite and check
    capture/<key>-<n>-<kind>.png   a still, with its .txt beside it
    syslog.txt                 the device's own log, with --syslog
    syslog-<host>.txt          a cartridge target's computer's log
    capture/<label>-<suite>-<attempt>-screen.txt
    capture/<label>-<suite>-<attempt>-screen.bin
    capture/<label>-<suite>-<attempt>-state.json
```

A `.log` holds what a suite printed, stderr merged in and ANSI stripped, and is
appended to across attempts. A `capture/` set is written only for a suite that
failed: the screen it left as text and as the device's own bytes, and the free
heap and drive state beside it. `screens.jsonl` is every distinct screen any
suite read, from the fetches it was making anyway; `--no-screens` turns it off.

`<slug>` is `targets.Target.slug`, the target token with `@` written `-at-`,
and `<label>` is the UI mode for an E2E suite or the category name for a perf
or soak suite.

Every record carries `kind`, `suite` and `time`, and, when a harness named
them, `target` and `attempt`. The rest depends on the kind:

| `kind` | Fields |
|---|---|
| `check` | `index`, `label`, `verdict`, `extra`, `seconds`, `scenario` |
| `scenario` | `title`, `verdict`, `checks`, `seconds` |
| `suite` | `name`, `verdict`, `note`, `checks`, `seconds`; from `run-tests` also `mode`, `attempt`, `recoveries` |
| `health` | `label`, `ok`, `checks[]` of `name`, `state`, `ms`, `detail`, and `heap` on the heap check |
| `warning` | `message` |
| `gap` | `component`, `started`, `ended` when the gap closed, plus whatever the component names it by: `target`, `machine`, `reason` |
| `log` | `target`, `path`, `started`, `port` |
| `capture` | `target`, `files[]`, `started`, `lead_in`, `fps`, `geometry`, `options`, `stills[]`, and the counts below |
| `plan` | `suites[]` of `name`, `category`, `path`, `run`, `reason`; `sequence[]` of `category`, `mode`, `label`, `suite` |
| `action` | `method`, `path`, and where each applies `check`, `params`, `status`, `ms`, `retries`, `error` |
| `run` | `verdict`, `suites`, `passed`, `failed`, `skipped`, `dirty`, `seconds`, `recoveries`, `exit_code`, plus the run identity below |

A `suite` record written by `run-tests` carries what only the harness knows:
the UI profile, which attempt it was, and how many times the device had to be
recovered around it. A suite writing its own closing line has none of those, so
they are absent rather than zero.

`target` is the target token the run was aimed at, from `E2E_TARGET`, and
`attempt` is which go at the suite this is, from `E2E_ATTEMPT`. `run-tests`
exports both for every suite it starts. A suite started by hand has neither in
its environment and records neither, rather than a guessed value.

`gap` is one interval a component could not observe anything: a device that
stopped answering, a stream that went quiet, a log that stopped arriving. One
shape for all of them, so the report's timeline can put each beside the suite
that was running, which is almost always the explanation. A gap still open when
the run ended carries no `ended`, and the report says so rather than inventing
one. The collector calls a device silent after ten seconds, which is far above
the tens of milliseconds between its ordinary lines and far below a suite.

`log` says where a device's own log is being collected and from when, so a
reader who finds no log file can tell a collector that never started from a
device that never sent anything. `--syslog` turns it on and needs each device's
`Network Settings` / `Log to Syslog Server` pointed at the runner host; that is
boot-time state on the device, so the runner reads it at both ends of a run and
corrects it at neither.

A datagram is attributed to a device by its source address, and a device with
two interfaces logs from whichever one its routing picked, which is not always
the address its name resolves to. Measured here: the Ultimate 64 answers REST
on its Ethernet address and sends its log from its WiFi address, and nothing on
its REST surface reports either. `U64_LOG_ADDRESSES="u64=192.168.1.71"` adds an
address to a machine for the run. Without it those lines are still kept, in
`syslog-unmapped.txt` with the address that sent them, which is what makes the
omission visible rather than silent.

`capture` is the recording's own health. It carries every option in force and
every count the receive path kept: packets, packets dropped, packets malformed,
frames completed, frames lost, frames shed because the host could not keep up,
frames decimated to reach the output rate, frames padded for a geometry change,
stream re-arms, and the same set for the audio. `timing` and `audio_rate` say
which video timing the device was in and therefore what sample rate the audio
track declares, since the audio clock is derived from the video clock.

A file with thousands of padded frames or hundreds of re-arms is telling a reader that the run fought
the recorder for the stream, which is worth knowing before drawing conclusions
from what it shows. `started` and `lead_in` are what convert a wall-clock time
into a position in the file.

`plan` is what the run intended before it ran anything: every suite the
registry names, whether this run meant to run it, and one of `manual`,
`not-selected` or `category` when it did not. `sequence` is the ordered list of
suite runs, which is longer than the selection because an e2e category runs its
whole suite list once per mode. A multi-target run's parent writes one too,
carrying `targets`, so a target still waiting for a machine another one holds
is on record before it has a directory of its own.

`action` is what the harness did to the device. `tests/lib/rest.py` writes one
for every non-GET request, every request that was retried and every request
that did not answer 200, whichever of its three entry points the caller used;
a GET that answered 200 first time is a run's bulk and is dropped. `params` is
the query string or the JSON payload the request carried, truncated, and `ms`
is how long the attempt that produced the outcome took rather than how long
the retries and their pauses did. `check` is the index of the check the request
happened inside and is absent outside one, which includes every request the
runner itself makes, because the runner reports its own gates as unnumbered
steps.

Without these records a reader watching a screen go blank cannot tell a reset
the run performed from a crash it observed.

```sh
# what the run changed on the device
jq -r 'select(.kind=="action" and .method!="GET") | "\(.suite) \(.method) \(.path)"' runs/u64/*.jsonl

# every request that did not answer 200, with the device's own words. Some of
# these are answers a check was asserting on: machine:menu_screen answers 404
# when no menu is open, which is the ordinary state between suites.
jq -r 'select(.kind=="action" and .error) | "\(.suite) \(.path) \(.status) \(.error)"' runs/u64/*.jsonl
```

A `run` record also says what the run is a run of, so a downloaded tree needs
no second file to identify itself: `commit`, `branch` and `worktree_dirty` from
git or from `GITHUB_SHA` and `GITHUB_REF_NAME`, `host`, `python`, `argv` with
the device password masked, `started` as a wall-clock time, and `assumptions`
naming the firmware fixes in force. Those come from `E2E_ASSUME_FIX`, which is
what `--assume-fix` sets and what a child run and every suite are told through,
so a child's record says the same thing as its parent's.

A multi-target run's parent writes `DIR/run.jsonl` naming `targets` and the
combined `exit_code`. It carries no counts: its children each counted their
own, and a zero there would be summed as if it were a result.

`attempt` is what tells two records with the same check index apart. A per-suite
file is truncated on the first attempt and appended to afterwards, so a retried
suite's file holds two records carrying `index: 26`, and only this field
distinguishes them.

The `heap` check carries `free`, `min_ever_free` and `total` rather than a
latency, and it can never make a sweep degraded: a degraded sweep is what fires
the recovery command, and free heap moves for a dozen ordinary reasons. It
reports `OK` with the figure, or `SKIP` on firmware without the endpoint.

`health` is one device sweep, the same one the console shows as a single line,
with a latency per check. A run consumed programmatically would otherwise have
no way to see why a device was called unhealthy, or to watch a listener getting
slower across a week of runs.

```sh
./run-tests -H u64 -o runs/

# every check that did not pass
jq -r 'select(.kind=="check" and .verdict!="OK") | "\(.suite) \(.label) \(.verdict)"' runs/u64/*.jsonl

# the run's own result, including whether the device had to be recovered
jq -r 'select(.kind=="run") | "\(.verdict) failed=\(.failed) recoveries=\(.recoveries) exit=\(.exit_code)"' runs/u64/run.jsonl

# which suites were slowest, and which needed the device recovered
jq -r 'select(.kind=="suite") | "\(.seconds)s \(.name) attempt=\(.attempt) recoveries=\(.recoveries)"' runs/u64/run.jsonl | sort -rn | head

# every degraded health sweep, with the failing check named
jq -r 'select(.kind=="health" and .ok==false) | "\(.label) " + ([.checks[] | select(.state=="fail") | .name] | join(","))' runs/u64/run.jsonl
```

## Rules for extending

- Add something here only once a second category needs it. A helper one
  category uses belongs with that category.
- Depend on the standard library and on other modules in this directory only,
  so any suite can use it without pulling in a UI model or a suite fixture.
- Add a device endpoint to `api.py` rather than building a path in a suite, and
  check the API's own parameter limits there.
- Keep it Python. There is no shell implementation to keep in step.
