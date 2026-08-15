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
  syslog-unknown-sender.txt          log lines from an address no target claims
  <slug>/
    run.jsonl                  this target's runner records
    run.log                    this target's runner console output
    <label>-<suite>.jsonl      one file per suite run
    <label>-<suite>.log        that suite run's console output
    <label>-<suite>.telnet.log the raw Telnet session stream, telnet mode only
    screens.jsonl              every distinct screen the harness read
    interactions.jsonl         every interaction the harness had with the device
    transcript.txt             the same, one line each, sharing their seq numbers
    screen-text.jsonl          the C64 screen as text, decoded from the recording
    bodies/<digest>.bin        one response body, kept once, referred to by digest
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
It carries three kinds of its own: `menu` and `telnet` are the screens, one
record per distinct screen, and `stream` is a suite starting, stopping or
redirecting a device stream. They are here rather than in a suite's own file
because the spool is one file per target that every suite appends to. `raw` is
the payload `machine:menu_screen` returned, as hex, so a reader can decode the
colour plane the text lost; a Telnet screen has none.

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
| `menu` | `cols`, `rows`, `text[]`, `raw` as hex, and `check` when one was running; `screens.jsonl` only |
| `telnet` | the same, for a Telnet session's screen, which has no colour plane and so no `raw`; `screens.jsonl` only |
| `stream` | `stream`, `action`, `address`; `screens.jsonl` only |
| `vic` | `cols`, `rows`, `text[]`, `frame`, `position`, and the suite run that was open; `screen-text.jsonl` only |
| `interaction` | `seq`, `transport`, `op`, then whatever that transport knows: `ms`, `status`, `params`, `payload`, `retries`, `error`, `sent`, `received`, `reply`, `fault`, `connection`, `menu_open`, `screen`; plus `body`, `body_hex` or `body_sha256`, with `body_bytes`, and `repeat` and `until` on a collapsed run; `interactions.jsonl` only |
| `log` | `target`, `path`, `started`, `port`, `addresses[]`; the record written when collection ends also carries `senders` and `unknown_senders` |
| `capture` | `target`, `files[]`, `started`, `lead_in`, `fps`, `geometry`, `options`, `stills[]`, `stream_lifecycle`, `screen_texts`, `screens_unreadable`, and the counts below |
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

Three things about that setting have cost real runs, so they are here rather
than in anyone's notes.

It carries a port, and a bare address means the firmware's own default 514
while the collector binds 5514. The value to set is `<host>:5514`, and the
runner compares the two and names the value to set when they differ.

`Syslog::init` runs once, at boot, so setting the value changes nothing until
the Ultimate firmware restarts. `machine:reboot` does not do that: it reboots
the C64, not the Ultimate. On an Ultimate 64 a JTAG `nios2-download` restarts
the firmware and is enough. On an Ultimate II+ in a C64 Ultimate there is no
equivalent, so the host has to be power cycled through its own Power & Reset
menu.

A caller that sets any setting over REST must save it to flash as well. The
firmware otherwise holds an unsaved change, and backing out of the settings
screens then raises a `Save changes to Flash?` prompt that a suite navigating
back to the browser cannot answer, so it presses Back until it gives up.

One collector can bind the port, and a second run says so and carries on
without a log rather than taking an arbitrary share of the first one's
datagrams. Two collectors on one port used to leave each of them reporting a
device that had gone quiet.

Two `log` records are written per target: one when collection starts, which a
killed run still leaves behind, and one when it ends. `addresses` is where the
run expected that target's lines from, which is what its machines resolve to.
`senders` is where they actually arrived from, with a count each, and
`unknown_senders` is the same for addresses no target claimed. A device logging
from an address its name does not resolve to is the difference between the
first two, and without both it cannot be seen at all.

A datagram is attributed to a device by its source address, and nothing here
ever guesses one: an unrecognised address identifies nothing, so those lines go
to `syslog-unknown-sender.txt` with the address that sent them and the report
names every such sender, its line count and why it could not be attributed.
`U64_LOG_ADDRESSES="u64=192.168.1.71"` adds an address to a machine for the
run, for a machine outside this repository; the firmware here sends its log
from the wired interface when there is one.

`capture` carries `geometry`, the canvas the recorder composed, and
`output_geometry`, the frame size the file carries: `--record-scale`
multiplies the second and not the first, so a reader comparing the record
against `ffprobe` on the file needs both.

`capture` is the recording's own health. It carries every option in force and
every count the receive path kept: packets, packets dropped, packets malformed,
frames completed, frames lost, frames incomplete, frames shed because the host
could not keep up, frames decimated to reach the output rate, frames padded for
a geometry change, stream re-arms, and the same set for the audio. `timing` and
`audio_rate` say which video timing the device was in and therefore what sample
rate the audio track declares, since the audio clock is derived from the video
clock.

Loss and lifecycle are separate. `frames_lost`, `packets_dropped` and
`audio_packets_lost` are the network's: what the device sent that did not
arrive. `stream_lifecycle` counts the intervals across which the device's own
counters cannot be compared at all, per stream and per reason: `suite-stopped`
and `suite-started` for a suite taking the stream, `recorder-rearm` for the
recorder asking for it again, `stream-quiet` for a stream that delivered
nothing for seconds, and `device-restart` for a counter that jumped further
than any loss could account for. Nothing missing across one of those is counted
as loss. `audio_unavailable_bytes` is the audio written to keep the track the
same length as the video while the run had the stream stopped, which is
likewise not loss; `audio_concealed_bytes` is the same for a stream that should
have been running and was not.

`screen_texts` and `screens_unreadable` are the two halves of the C64 screen
decoder's own accounting: the frames it read back as text and wrote a record
for, and the frames it looked at and could not read as a text screen. A refused
frame writes nothing, so without the second number a device drawing a bitmap, a
device in the shifted character set and a device whose screen did not change all
leave the same absence.

`stills[]` is one entry per still, each naming its suite run, its kind, both of
its files, the frame of the recording it was taken from, with `position` in
seconds and `frame` as the slot index, and `interaction`, the reference of the
last interaction the run had recorded when that frame was composed. A still
carries no chrome of its own, because its whole value is that extracting the
video at `frame` reproduces it pixel for pixel, so the way back to the raw
record is that reference rather than something drawn over the picture. The
report reads that position rather than deriving one from the suite's timing,
which was wrong by up to 4.7 seconds.

A file with thousands of padded frames or hundreds of re-arms is telling a
reader that the run fought the recorder for the stream, which is worth knowing
before drawing conclusions from what it shows. `started` and `lead_in` are what convert a wall-clock time
into a position in the file.

The recording carries the interaction stream in the picture as well as in the
files. Under the two panes is a band of seven character rows: the suite and
check being run with a state word at the right, a fixed column header, the last
four interactions, and a row of cumulative counters. A ticker line is stamped
when its interaction is issued rather than when it answers and is then finalised
in place, so a device that has stopped answering shows the request that is
hanging at the moment it hangs, and a line never moves once a reader has found
it. Polling is counted and never shown, and consecutive identical interactions
become one line whose `ref` names the range, because `machine:menu_screen` alone
is several hundred calls in a sweep. Colour marks two things and nothing else: a
line held past the stall threshold, and a line that answered with a fault or a
status of 400 or more. `tests/e2e/lib/band.py` holds the layout, the formatting
and the ticker; the recorder only places it.

The band is on the frame and not on a still. A still is cropped to the panes, so
it stays a pixel-exact extract of the video at its own position, and the way
back into the interaction log is the `interaction` reference in its record.

`interaction` is the exhaustive log of what the harness did to the device, and
`action` is the curated subset of it that the report's timeline reads. The rule
for `action` drops a GET that answered 200 first time, because a run's reads are
its bulk and a narrative that carried them would be unreadable. `interaction`
has no such rule: it holds every REST request and its answer, every Telnet
exchange, every FTP command and reply and every listener probe, written from
inside the transports so a suite gains the coverage without a line of its own.

Two things keep it affordable. Consecutive identical interactions collapse into
one record with a `repeat` count and an `until` time, which is what a settle
loop reading the same screen thirty times becomes; the collapse is only ever of
consecutive interactions, so nothing is reordered. And a short answer is in the record
itself, as text when it is text and as hex when it is not, because a one-byte
read of memory is the byte; anything larger is written once to
`bodies/<digest>.bin` beside the log, with the record carrying `body_sha256` and
`body_bytes`, so the second and every later occurrence of one 2000-byte menu
screen costs a digest.

Every record carries a `seq`, and `transcript.txt` beside it carries one line
per record opening with the same number, so a reader who finds a line there and
wants every field of it looks that number up rather than matching on a
timestamp. Both files are written from one record, so they cannot disagree.

Three fields answer questions a bare request and response cannot. `fault` names
a connection-level failure in one word (`refused`, `reset`, `timeout`,
`broken-pipe`, `unreachable`), because a key that never reached the device and a
key the device ignored are different findings. `connection` says whether the
call opened a connection or used one that was already up. `menu_open` says
whether the device's overlay menu was open, taken from what `machine:menu_screen`
last answered, which is what tells a key the machine ignored from a key an open
menu swallowed while answering 200. `screen` is a digest of what the harness was
looking at, so two consecutive records showing different digests are the
observable effect of whatever happened between them.

`vic` records are the C64's own screen as 25 rows of 40 characters, decoded from
the frames the recorder already has by matching each 8x8 cell against the
character ROM. It costs the device nothing, which reading its screen memory
would not, and it is written only when the screen changed, at most once a
second. A frame that is not a text screen this can read produces no record
rather than a screen of question marks, and a cell that is a ROM shape with no
character of its own, which is what the PETSCII graphics are, is marked rather
than named so a screen with a logo on it still reads.

Records carry the suite, the attempt, the scenario and the check that were open,
so they join to the rest of the run with no correlation identifier of their own,
and the report converts their wall-clock time into a position in the recording
the same way it does for every other record. `run-tests -o DIR` exports
`E2E_INTERACTIONS` for every suite it starts, and writes its own health sweeps
and UI-state gate into the same file.

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
