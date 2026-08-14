# Observability for an E2E gate run - specification

**Status: specification.** This describes what is to be built, not what the
harness does today. Nothing in it is a description of current behaviour except
where it cites existing code by file and symbol, and those citations are
labelled as such. A reader who wants to know what the harness does now should
read [tests/README.md](../../README.md),
[tests/e2e/README.md](../README.md) and
[tests/lib/README.md](../../lib/README.md).

As each priority step in section 12 lands, the behaviour it introduced belongs
in those files, and this document stays what it is: the record of what was
decided and why. It is not updated to describe the implementation, and it is not
a substitute for the documentation the implementation carries.

## Purpose

CI/CD runs the E2E gate against off-site Ultimate devices. The pass/fail log
answers neither of the two questions people actually have:

1. Did a check that reported OK do what it claims? A check that read the wrong
   row or seeded the wrong fixture produces the same log line as one that
   worked.
2. Is a device misbehaving? Glitches, spontaneous resets, corrupted screens, a
   listener getting slower, a heap that never comes back: none of these appear
   in a verdict line.

Two readers must be able to answer those questions from the same artefacts: a
person looking at a failed build, and a script or a language model reading the
run without a browser.

Everything here exists for occasional human inspection. Nothing here is an
assertion and nothing here may change a run's verdict.

## How to read this document

- Requirement identifiers are `OBS-<section>.<item>` and are stable. Do not
  renumber a requirement when the document is edited; add a new item instead.
- Numbering has gaps. A withdrawn requirement keeps its number and is listed
  once, with its reason, in section 10 under "Withdrawn requirements". It is
  never reused.
- Priorities are `P1` to `P6` and follow the build order in section 12. Build
  everything at `P1` before anything at `P2`, and so on. The `P` tag on a
  requirement is authoritative if it ever disagrees with the section 12 table.
- Dependencies are given by requirement number.
- A requirement whose basis is unverified carries an **UNVERIFIED** flag naming
  the open question in section 11 that covers it.

Five principles shaped the requirements. Where one forced a choice, the
requirement names it.

- **KISS.** Prefer the output needing the least machinery to produce and read.
- **DRY.** One authored artefact per fact. Everything else is derived from it by
  a program.
- **Consistency.** One directory layout, one naming convention, one identity key
  for a check, used by every component here.
- **Usable by a person and by a script.** Text a person can read in a terminal
  and a script can parse without a parser of its own.
- **Easy to use.** A developer looking at a red build reaches the evidence in as
  few hops as GitHub permits, and is told plainly where a hop is impossible.

This document lives at `tests/e2e/doc/observability-spec.md` and is part of the
repository. Paths in it are relative to the repository root, and code is cited
by file and symbol, because symbol names survive edits that line numbers do not.
Every citation was checked against the tree at the time of writing; a citation
that no longer resolves is a defect in this document, and the correction belongs
here rather than in a workaround.

One file this document cites is excluded from git and exists only in a local
checkout: `AGENTS.md`, which holds the repository's red/green rule. The
implementation prompt that goes with this document is local-only for the same
reason, under `doc/research/e2e-observability/`.

---

## 1. Cross-cutting constraints

These hold for every component in this document.

**OBS-1.1** [P1] No component specified here may change the exit status of
`./run-tests` or the verdict of any suite, check or health sweep. A component
that cannot do its job reports the fact and the run continues.

**OBS-1.2** [P1] A component that fails to start reports the reason once, at
startup, before any suite runs. It does not fail late in a run that has already
cost 15 to 30 minutes.

**OBS-1.3** [P1] Every observability feature that costs the device anything at
run time is opt-in through a `./run-tests` flag. With no flag passed, the
runner's behaviour and its device traffic are unchanged, except for the failure
capture (OBS-5.1) and the heap health check (OBS-6.1), which are cheap enough to
be unconditional. This governs what the runner does. It does not govern the
standing device configuration in section 7, whose cost is stated in OBS-7.1.

A flag is only half the protection. What a feature may do to a suite while it is
switched on is section 15, and OBS-15.1 is the requirement that pairs with
OBS-1.1: a component may not change a verdict, and it may not change how a suite
reaches one either.

**OBS-1.4** [P1] Nothing here introduces a new correlation identifier. Every
artefact joins on data the harness already writes: target, mode or category,
suite, check index, and the wall-clock interval of OBS-2.6. If something cannot
be joined, the fix is a field on an existing record shape, not a new identifier
scheme.

**OBS-1.5** [P1] All timestamps used for correlation are `time.time()` taken on
the host that runs `./run-tests`. The device's own clock is never used. There is
no clock synchronisation requirement and no NTP assumption anywhere in this
design.

**OBS-1.6** [P3] Every device request a component makes during a run is a `GET`,
with one exception: the recorder's `streams:start` and `streams:stop`
(OBS-8.3, OBS-8.16). `RestClient.request` in `tests/lib/rest.py` increments
`self.mutations` for every non-GET request, and `api.MachineApi.reset` compares
that counter against `self._reset_at` to decide whether a reset is needed, so a
capture made of GETs cannot change the runner's reset bookkeeping.

The counter is per `RestClient` instance, and the runner already holds two:
`Device.api` and `Device.probe` are separate `UltimateApi` objects with separate
counters, and only `Device.api` carries the reset bookkeeping. The GET rule is
therefore stricter than strictly necessary. It stays as written because a
component that acquires a client of its own later, or that is handed
`Device.api` by a future caller, would otherwise break the bookkeeping silently.
The recorder's exception is safe for the same reason: it holds its own client.

**OBS-1.8** [P1] No artefact this document specifies may contain the device
password. `build_command` in `run-tests` substitutes `@PASS@` into every suite's
argument vector, so a command line is a secret-bearing string. Any component
that records a command line, an environment, a URL or a query string replaces
the password value with `***`. The report generator applies the same rule to
anything it copies out of the JSONL or out of a captured console log. A run
whose password is empty, which is the common local case, is unaffected. This
matters because the artefacts leave the machine that produced them: a CI
artifact is downloadable by anyone who can see the build.

**OBS-1.7** [P1] There is exactly one authored report and every other format is
derived from it by a program. The authored report is `index.md` (OBS-3.1). The
GitHub job summary is a byte copy of part of that file (OBS-4.1). The optional
PDF is generated from that file by one command (OBS-3.16). No component may
render the same fact twice from the JSONL. DRY: two renderers of one fact drift,
and the drift is invisible until someone compares them. This is why section 4
contains no second document generator.

### Acceptance criteria for section 1

- A run with every observability flag enabled, and the collector, recorder and
  report generator all failing to start, produces the same suite verdicts and
  the same process exit status as a run with no flags.
- Each component's failure path is exercised by a device-free test: a busy UDP
  port, a missing `ffmpeg`, an unwritable output directory.
- A test asserts the GitHub job summary written for a fixture is a byte prefix
  of the `index.md` written for the same fixture, so OBS-1.7 holds by
  construction rather than by review.
- A test runs the report generator over a fixture whose captured console log and
  whose recorded command line both contain a password, and asserts the string
  does not appear anywhere in `index.md` (OBS-1.8).

---

## 2. Run output layout and correlation

**OBS-2.1** [P1] `./run-tests -j DIR` always writes per-target subdirectories.
The single-target path in `main` writes `run.jsonl` into `DIR` directly and the
multi-target path in `run_targets` writes into `DIR/<slug>/`. Both must produce
`DIR/<target.slug>/`, where the slug is `targets.Target.slug`. Consistency: one
tree shape means the report generator, the collector and the recorder each have
one path rule rather than two.

The slug is appended in exactly one place. `run_targets.start_ready` currently
joins `args.jsonl_dir` with `target.slug` and passes the result to
`child_command`, which forwards it as the child's `--jsonl-dir`. If the child
also appends its own slug the tree becomes `DIR/<slug>/<slug>/`. The child is
the process that knows its one target, so the child appends and the parent
passes `DIR` through unchanged.

**OBS-2.2** [P1] `./run-tests` exports `E2E_TARGET` and `E2E_ATTEMPT` to every
suite process it starts, beside the existing `E2E_SUITE`. Both are set in
`run_one_attempt`, which already builds that environment and already knows both
values: the target is `options.host` and the attempt is its own `attempt`
argument.

**OBS-2.3** [P1] `report._record` sets `target` and `attempt` on every record it
writes, from `E2E_TARGET` and `E2E_ATTEMPT`, in the same way it sets `suite`.
Depends on OBS-2.2. The fields are additive, so existing `jq` recipes in
`tests/lib/README.md` keep working, and OBS-2.5 documents them in the same
commit. A suite started by hand, with neither variable in its environment,
records neither rather than a guessed value. See OQ-7.

`attempt` on a `suite` record written by `run-tests` already exists and already
means this. The field keeps that meaning on every other kind rather than
acquiring a second one.

**OBS-2.4** [P2] The `run` record carries the CI run identifier taken from the
environment (`GITHUB_RUN_ID`, `GITHUB_RUN_ATTEMPT`), and only from the
environment. Absent variables mean absent fields. The runner generates no
identifier of its own, because an identifier that cannot be traced back to a
build page is worse than none. This is the only join from a downloaded artifact
back to the build that produced it.

**OBS-2.5** [P1] The record-shape table in `tests/lib/README.md` gains a row for
every new record kind and every new field introduced here: the `target` and
`attempt` fields (OBS-2.3), the run-identity fields on the `run` record
(OBS-2.4, OBS-2.11), the
`targets` and `exit_code` fields on a parent `run` record (OBS-2.12),
`kind=capture` (OBS-8.11), `kind=log` (OBS-7.9) and the `heap` entry in a
`health` record (OBS-6.4). Any `jq` recipe in that file that the new fields make
more useful is updated in the same edit.

**OBS-2.6** [P1] The report generator treats the interval of any `check`,
`scenario` or `suite` record as `[time - seconds, time]`. `report._record`
stamps `time` when the item **closes**, and `seconds` is its elapsed duration.
This is the only time key in the design.

**OBS-2.7** [P1] Time between one check's interval ending and the next one's
beginning is attributed to the suite, not to a check. Nothing is recorded when a
check starts, so that gap is suite setup, teardown, health sweeps and the
UI-state gate.

**OBS-2.8** [P1] A retried suite produces repeated check indices. The per-suite
JSONL is truncated on the first attempt and appended to afterwards
(`run_one_attempt`), so two records in one file can carry `index: 26`. The
`attempt` field of OBS-2.3 is what tells them apart, on the check record itself
rather than by matching intervals against the runner's `suite` records. An
interval join would be the alternative and it is worse: it depends on two files
agreeing about the clock, and it produces no answer at all for a suite that was
killed before its `suite` record was written, which is exactly the run whose
records most need reading. This is the OBS-1.4 rule applied: a field on an
existing shape rather than a reconstruction.

**OBS-2.10** [P1] The `-j` directory has one layout. Every component writes into
it and refers to files inside it by a path relative to its root:

```
DIR/
  index.md                        the report (OBS-3.1), the entry point
  index.pdf                       optional, derived from index.md (OBS-3.16)
  run.jsonl                       the parent's own record, multi-target runs only (OBS-2.12)
  syslog-unmapped.txt             optional, datagrams from an unmapped address (OBS-7.8)
  <slug>/
    run.jsonl                     this target's runner records
    run.log                       this target's runner console output (OBS-2.13)
    <label>-<suite>.jsonl         one file per suite run
    <label>-<suite>.log           that suite's console output (OBS-2.13)
    capture/<label>-<suite>-<attempt>-screen.txt
    capture/<label>-<suite>-<attempt>-screen.bin
    capture/<label>-<suite>-<attempt>-state.json
    capture/<label>-<suite>-<attempt>-<n>-<first|last|change>.png    optional (OBS-8.28)
    capture/<label>-<suite>-<attempt>-<n>-<first|last|change>.txt    optional (OBS-8.28)
    syslog.txt                    optional, this target's device log (OBS-7.8)
    syslog-<host>.txt             optional, a second machine's log (OBS-7.18)
    menu-screens.bin              optional, every distinct menu screen (OBS-8.22)
    video.mp4                     optional, both panes and audio (OBS-8.2, OBS-8.29)
    video-screen.mp4              optional, --record-layout separate (OBS-8.29)
    video-menu.mp4                optional, --record-layout separate (OBS-8.29)
    video.srt                     optional (OBS-8.2)
```

Names are lower case and hyphen separated, and every variable part of a name
comes from data already in the JSONL or from the target token: the target slug,
the mode or category label, the suite name, the attempt number and a host name.
No file name carries a timestamp, because the JSONL already carries the time and
a name a reader cannot predict cannot be looked up. Consistency: this replaces
the separate output-directory rule the recorder would otherwise need, and it is
why OBS-2.9 is withdrawn.

A per-suite `.jsonl` and its `.log` share a stem, so a reader who has one has the
other by changing the suffix.

**OBS-2.11** [P1] The `run` record carries the run's identity, so a downloaded
artifact says what it is a run of without a second file:

| Field | Source | Absent when |
|---|---|---|
| `commit` | `GITHUB_SHA`, else `git rev-parse HEAD` in the checkout | neither answers |
| `branch` | `GITHUB_REF_NAME`, else `git rev-parse --abbrev-ref HEAD` | neither answers |
| `dirty` | whether `git status --porcelain` is non-empty | git does not answer |
| `host` | `socket.gethostname()` on the machine running `./run-tests` | never |
| `python` | `platform.python_version()` | never |
| `argv` | the runner's own command line, password redacted per OBS-1.8 | never |
| `started` | `time.time()` when the run begins | never |

`dirty` on a `run` record is already taken by the count of suites that left the
UI outside its documented state (`report.run_result`), so the git field is named
`worktree_dirty`. Naming a field twice with two meanings is worse than a longer
name. Depends on OBS-2.5. Three `git` calls per run, all cheap, all on the host,
none reaching the device; a checkout where git does not answer loses the fields
rather than the run. The runner is a git worktree in the working checkout this
document was written against, so `git rev-parse` has to be run with the
repository root as its working directory rather than relying on the process's
own.

**OBS-2.12** [P1] A multi-target run's parent process writes its own `run`
record to `DIR/run.jsonl`, naming the targets it ran and the status
`combine_exit_codes` produced. `run_targets` returns before `summarise` today,
so a multi-target run records no overall verdict anywhere and the report would
have to re-implement `combine_exit_codes` to state one. DRY: the rule that
combines statuses has one implementation, and the report reads its answer.
The record carries `targets` as a list of tokens and the fields of OBS-2.11.
`DIR/run.jsonl` and `DIR/<slug>/run.jsonl` are distinct paths, so there is no
ambiguity about which process wrote which.

**OBS-2.13** [P1] With `-j DIR`, `./run-tests` writes every line a suite prints
to `DIR/<slug>/<label>-<suite>.log` as well as to its own stdout, and its own
console output to `DIR/<slug>/run.log`. This is the only artefact that carries
what `report.detail` printed, what a traceback said and what a suite wrote to
stderr, and question 1 in the Purpose section is mostly answered by those lines:
a check that reported `OK (20 rows)` is trusted or not on the detail lines under
it. The per-suite JSONL carries a check's verdict, its `extra` string and its
duration, and nothing else the suite said.

Three properties, each of which the implementation has to hold deliberately:

- The file is appended to across attempts, matching the per-suite JSONL rule in
  OBS-2.8, and truncated on the first attempt.
- ANSI escapes are stripped on the way to the file and left alone on the way to
  the console, so the saved log greps cleanly and the terminal keeps its colour.
- Stderr is merged into the same file, in order, because a traceback interleaved
  with the check line that produced it is the evidence, and two files would lose
  the interleaving.

Two consequences on the console, which are accepted rather than worked around.
Under `-j` a suite's stdout is a pipe rather than a terminal, so
`report._colour_enabled` chooses plain text unless `FORCE_COLOR` is set, and
`report.progress` prints nothing at all. `run_targets` already runs its children
this way and already sets `FORCE_COLOR` when the parent is on a terminal; the
single-target path adopts the same rule. Live progress lines are a terminal
affordance that a captured run does not have, and inventing a pty to keep them
is machinery this document does not need. A run with no `-j` is unchanged
(OBS-1.3).

Rejected: recording `report.detail` as a `kind=detail` JSONL record. It is a
smaller change, and it loses tracebacks, stderr, the runner's own health lines
and everything a suite killed by a signal had printed, which is the case where
the evidence matters most.

**OBS-2.14** [P1] The runner records its plan before it runs anything: a
`kind=plan` record naming every suite in the `SUITES` registry, its category,
its path, whether this run intended to run it, and when it did not, which of the
four reasons applies.

| Reason | Cause |
|---|---|
| `manual` | `Suite.manual` is set and `--manual` was not given |
| `not-selected` | `-s` named other suites |
| `category` | its category was not among the ones this run chose |
| `mode` | its category is `e2e` and this mode pass does not include it |

The same record carries the ordered sequence of suite runs this run intends to
make, as `(category, mode, suite)` triples. That is not the same list as the
selection: an `e2e` category runs its whole suite list once per mode in `modes`,
so a two-mode run of ten suites is twenty entries. The sequence is what the
progress bar of OBS-8.33 segments over and what the report's coverage section
counts against.

A green run that quietly ran 17 of 25 registered suites reads exactly like a
green run that ran all of them, and "are the tests working properly" is not
answerable without the difference. The registry is the authority for what exists
(`run-tests` states that every executable test under `tests/` is listed there),
and the selection is already computed by `selected_suites`, so this records a
decision the runner has already made rather than deriving a new one.

The path is in the record because the next question after "this check failed" is
"where is the code that produced it", and a suite name is not a path.

**OBS-2.15** [P1] The runner records what this run assumed rather than proved: a
`assumptions` field on the `run` record holding the fix names in force, from
`machine.parse_assumptions` via `--assume-fix`, and empty when none were given.

`tests/lib/machine.py` holds a table of firmware fixes that some machine kinds
lack, and `Machine.skip_without_fix` reports a check tagged with a missing fix as
`SKIP` rather than running it. `--assume-fix` turns those checks back on for a
run that is testing whether a backport has landed. Either way, what a run proved
depends on which assumptions were in force, and a reader who cannot see them
cannot tell a suite that passed from a suite that skipped the part that would
have failed. Depends on OBS-2.5.

### Acceptance criteria for section 2

- `./run-tests -j DIR u64` and `./run-tests -j DIR u64 c64u` both produce
  `DIR/<slug>/run.jsonl` and `DIR/<slug>/<label>-<suite>.jsonl`, and neither
  produces `DIR/<slug>/<slug>/`.
- Every record in every file carries a `target` field equal to the target token,
  and every record a suite wrote carries an `attempt` field.
- A retried suite's JSONL holds two records with the same `index` and different
  `attempt` values.
- A device-free test reads a recorded JSONL fixture and asserts the interval
  computed for a known check matches the fixture's `time` and `seconds`.
- A device-free test asserts every path the report names exists under `DIR` and
  matches the naming rule in OBS-2.10.
- A device-free test of the console capture runs a stub suite that prints a
  coloured line, a line to stderr and a line with no trailing newline, and
  asserts the log file holds all three, in order, with no escape bytes, and that
  the same three reached the runner's stdout.
- A device-free test asserts a second attempt appends to the log rather than
  truncating it, and that the first attempt truncates.
- `./run-tests -j DIR u64 c64u` produces `DIR/run.jsonl` carrying both target
  tokens and the combined exit status, and `./run-tests -j DIR u64` does not
  produce it.

---

## 3. Report generator

The first thing to build. It consumes data that already exists and needs no
device, no new capture and no new dependency.

### Why the report is Markdown

The entry point is `index.md`, not `index.html`, on four criteria.

- **Read by a person.** Markdown reads correctly unrendered in a terminal and an
  editor, and a GFM pipe table with padded columns is already aligned in `less`.
  HTML read unrendered is tag soup.
- **Read by a model or a script.** Markdown is roughly three to five times
  denser per fact than equivalent HTML table markup, and any contiguous slice of
  it still parses and still says what section it is in. An HTML file cut at 100
  lines is a `<style>` block, and a self-contained one pays for the run's data
  twice by inlining the JSON beside the rendering.
- **Converted to PDF.** One command over the authored file. Converting HTML
  needs print CSS somebody maintains, and a collapsed disclosure element prints
  collapsed, silently dropping content.
- **Browsable on CI.** The job summary renders GFM directly, so the Markdown is
  the summary (OBS-4.1) with no second generator. Inside a zipped artifact
  GitHub renders neither format, so both need a download.

Markdown loses one context, a browser opened on `file://.../index.md`, and
OBS-3.16 covers it. HTML would force either a second generator for the job
summary or a hand-maintained second document, the DRY violation this design
exists to avoid.

### Requirements

**OBS-3.1** [P1] The program `tools/e2e_report.py` reads a `-j DIR` tree and
writes one `DIR/index.md` covering the whole run, including every target. It
takes no device connection and runs after a run has finished. Its command line
is `python3 tools/e2e_report.py DIR`, with no required options. KISS: one
document means a multi-target run has one entry point to open, to paste and to
copy into the job summary, where N documents plus an index is N+1 files.

`tools/app_space.py` is the house pattern for a Python tool in this repository
that emits GFM and writes a job summary, and `tools/test_app_space.py` with the
`app_space_test` target in the root `Makefile` is the pattern for testing one.
The report generator follows the first and departs from the second, for the
reason in OBS-3.13.

**OBS-3.4** [P1] The report carries a verdict table with one row per target,
mode or category, and suite, giving verdict, duration, attempt count, recovery
count and the note the runner recorded. Source: the `suite` records in
`DIR/<slug>/run.jsonl`, whose `note` field carries `describe_exit`'s reason for
a suite that was killed rather than one that failed by itself. A suite that ran
more than once contributes one row per attempt.

**OBS-3.5** [P1] The report lists every check, grouped by target, suite and
scenario, with its index, label, verdict, duration, interval and its `extra`
string. Failing checks appear in the summary part of the document; the full
list, passing checks included, appears in the detail part. Question 1 in the
Purpose section is about checks that passed, so a passing check has to be in the
document, and a build page does not need 1300 rows to say a run was green.
Resolves OQ-6.

The `extra` string is not optional decoration. `report.check_ok("20 rows")`
records the measurement a check made, and it is the only per-check evidence in
the JSONL that a check did what its label claims: `OK` and `OK (0 rows)` are the
same verdict and different results. A check whose duration reached
`report.SLOW_CHECK_SECONDS` is marked `SLOW` in the same column the console
marks it, computed from `seconds` rather than stored, so the report and the
console agree.

**OBS-3.6** [P1] Two identity keys, both derived and neither stored:

- A suite run is `<target>/<label>/<suite>/<attempt>`, for example
  `u64/overlay/prg-context-menu/1`, where `<label>` is the UI mode for E2E
  suites or the category name for perf and soak.
- A check is that key plus its index: `u64/overlay/prg-context-menu/1/26`.

The attempt is part of the key because it is what makes it unique. OBS-2.8
records that a retried suite repeats its check indices in one file, so the four
part form names two different checks after a retry.

Every artefact reaches the same check from the same string. The verdict table
and the failing-check entries print the key. The recording's subtitles print the
key. The capture files and the per-suite log are named from the suite-run key by
one substitution, `/` to `-`, with the target dropped because the file already
sits under that target's directory: `overlay-prg-context-menu-1-screen.txt` and
`overlay-prg-context-menu.log`. Consistency: a person or a script that has found
a check in one artefact finds the rest by string match with no lookup table, and
the report's "Files in this run" section (OBS-3.14) states the substitution.

**OBS-3.7** [P1] The report renders the health sweep series as a table per
target, one row per sweep in wall-clock order, with a column per check name
carrying its latency in milliseconds, and a column for free heap once OBS-6.4
exists. Row labels are the sweep's `label` field, which names the suite the
sweep ran before or after, because a sweep runs before every E2E suite and again
after any that fails. Source: the `kind=health` records already written by
`report.health_result` via `Device.sweep_health`. KISS: a table of 30 to 50 rows
is readable, greppable and free, which is why the SVG chart generator was
withdrawn as OBS-3.8.

A check that was skipped renders `skip` and a failed one renders `FAIL`, matching
`health.Check.render`, so a reader comparing the table with a console line sees
the same words.

**OBS-3.10** [P3] For each failing check the report shows that check's screen
capture as a fenced 40x25 text block, plus the heap figure and the drive state,
when a failure capture exists for that suite. The block sits inline under the
failing check in the summary part, because that is what makes it visible on the
build page without a download (OBS-4.7). Depends on OBS-5.4.

**OBS-3.11** [P5] The report shows syslog lines received during a failing check's
interval, and lines received in the gap immediately before it, attributed to the
suite rather than to a check. The whole log stays as a sibling file named in the
file index; only the slices around failures are inlined. The report states that
the log is best-effort and incomplete by construction, for the reasons in
OBS-7.11 and OBS-7.12. Depends on OBS-7.8 and OBS-2.6. KISS: inlining a slice
for every check would multiply the document by the check count to answer a
question only asked about failures.

**OBS-3.13** [P1] The generator is device-free and testable against a recorded
`-j` tree checked into the repository at `tests/lib/fixtures/e2e-run/`. Every
rendering decision it makes is covered by a test that runs with no hardware.

The tests are a registered suite, `tests/lib/report_render_test.py`, listed in
the `SUITES` tuple in `run-tests` in the `e2e` category with an empty argument
string, beside `transport-usage` and `runner-policy`. Three reasons, in order:
the registry comment in `run-tests` states that every executable test under
`tests/` is listed there with no exception; a device-free suite at the head of
the gate costs nothing and fails with a clear message; and the report is the
artefact everyone reads when the gate goes red, so a generator that has stopped
rendering has to be found by the gate rather than by the person reading the
empty report. It imports the generator from `tools/` the way
`tests/lib/runner_policy_test.py` imports `run-tests`, by path.

The fixture is a reduced real tree, not a synthesised one: two targets, one of
them a `cartridge@computer` token, a retried suite, a failing suite, a skipped
suite, a suite with no closing record, a truncated final line, at least one
`health` record with a failed check and one with a skipped check, and a captured
console log holding a traceback. It is checked in with the JSONL byte for byte
as the runner wrote it, apart from the reduction, so a change in a record shape
shows up as a test failure rather than as a fixture nobody updated.

**OBS-3.14** [P1] `index.md` has one section order, and the boundary between
what belongs on a build page and what belongs in a download is the HTML comment
`<!-- detail -->` on a line of its own:

| Order | Section | Content |
|---|---|---|
| 1 | `# E2E gate run: <verdict>` | the status line of OBS-3.22, then run identity: commit, branch, CI run id and attempt, runner host, start time, duration, the device identity of OBS-3.19, and the completeness statement of OBS-3.18 |
| 2 | `## How to read this` | the fixed preamble of OBS-3.24 |
| 3 | `## Verdict` | the table of OBS-3.4 |
| 4 | `## Coverage` | what did not run and what was skipped, per OBS-3.25 |
| 5 | `## Changes since <run>` | the comparison of OBS-3.28, only with `--compare` |
| 6 | `## Failing checks` | one entry per failure, with its screen text (OBS-3.10), what the run knows about it (OBS-3.27), its reproduce command and its log tail (OBS-3.20) |
| 7 | `## Device health` | the table of OBS-3.7, one per target |
| 8 | `## Files in this run` | every file under `DIR` by relative path, with size and one line saying what it is and what produced it, and the name substitution of OBS-3.6 |
| - | `<!-- detail -->` | the marker, on a line of its own |
| 9 | `## Timeline` | the merged event list of OBS-3.26 |
| 10 | `## Checks` | every check, per OBS-3.5 |
| 11 | `## Where the time went` | the tables of OBS-3.29 |
| 12 | `## Screens` | each suite run's stills, per OBS-3.23 |
| 13 | `## Device log` | the slices of OBS-3.11 |

A section whose source does not exist in this run is omitted rather than
rendered empty. A run with no recorder has no `## Screens`, a run with no
collector has no `## Device log`, and the report's file index (section 5) is
what tells a reader that those artefacts were not produced rather than lost.

"Files in this run" is how a reader who has downloaded the artifact finds the
JSONL, the captures and the recording without listing the directory and
guessing. A marker rather than a byte budget, because where to cut is a content
decision and a size cut lands mid-table.

**OBS-3.19** [P1] The header names each target's product and firmware version.
The `ident` check in the health sweep already carries them: `health.probe`
builds its detail string as `f"{info.product} {info.firmware_version}"` and
`Device.sweep_health` writes that string into the `detail` field of the `ident`
entry of every `health` record. The report takes the first `ident` detail per
target and reports a target with none as `firmware unknown`. Question 2 in the
Purpose section is unanswerable without it: "a device is misbehaving" means
nothing until the reader knows which firmware was on it. DRY: no new device call
and no new record, because the sweep already asks.

**OBS-3.20** [P1] Each entry in `## Failing checks` carries two things a reader
would otherwise have to reconstruct:

- The command that runs that suite again on that target, built from the suite
  name, the target token and the mode on the `suite` record:
  `./run-tests -H <target> -s <suite> --mode <mode>`. The password is not in it,
  per OBS-1.8; the reader supplies their own.
- The last lines of that suite run's captured console log (OBS-2.13), bounded,
  ending at the end of the file, in a fenced block. A suite that failed by
  raising ends its log with the traceback, and a suite that was killed ends it
  mid-line, which is itself the answer.

The bound is 40 lines. Enough for a traceback and the checks around it; short
enough that ten failures do not turn the summary part into the whole log.
Depends on OBS-2.13.

**OBS-3.24** [P1] The report opens with a short, fixed preamble under the
heading `## How to read this`: what the identity key of OBS-3.6 means, what the
status line of OBS-3.22 means, which section answers which question, and which
file to open next for each. Fifteen lines at most, identical in every report,
written for a reader who has never seen one before.

Two readers arrive at a failed build with no context: a person who did not
write the harness, and a model that has been handed the file. Both spend their
first minutes working out what the document is. Fifteen fixed lines is a cheaper
answer than either of them guessing, and being fixed means it costs nothing to
maintain and nothing to check.

**OBS-3.25** [P1] The report has a `## Coverage` section stating what this run
did not do:

- how many suite runs were planned and how many completed, from OBS-2.14;
- every registered suite that did not run, with its reason;
- every check that reported `SKIP`, grouped by suite, with the reason from its
  `extra` string and a count;
- the assumptions in force from OBS-2.15, or that there were none.

This is the section that answers "are the tests working properly", and it is the
one a green run most needs. A run where every suite passed and a third of the
checks skipped has not proved what its verdict suggests, and nothing else in the
document says so: `tests/lib/machine.py` skips a check tagged with a fix the
machine lacks, a suite skips when a service it needs is unreachable, and both
report `SKIP` with a reason nobody reads.

Depends on OBS-2.14 and OBS-2.15.

**OBS-3.26** [P1] The report has a `## Timeline` section in the detail part: one
line per event, in wall-clock order, each with its time and its offset from the
run's start.

Every record in the tree already carries a `time`, so this is a merge and a
sort, and it is the only place the whole run appears as one narrative. The
events: a suite run starting and ending with its verdict, a health sweep with
its one-line result, a device recovery, a failing check, a capture, a device
restart seen in the syslog (OBS-7.14), and a warning.

"What happened during that test run" is a question about order, and it is
currently answerable only by opening five files and sorting by hand. One line
per event over a 30-minute run is a few hundred lines, which is why it is in the
detail part.

**OBS-3.27** [P1] Each failing check's entry carries what the run already knows
about the failure, as facts rather than as a diagnosis:

| Line | Condition, all read from records the run wrote |
|---|---|
| killed | the suite's `note` carries `describe_exit`'s reason, so it was signalled rather than failing by itself |
| unhealthy before | the health sweep before this suite reported a failed check, and which |
| recovered | the device was recovered around this suite, and how many times |
| repeated | this check failed on more than one attempt |
| passed on retry | this check failed on one attempt and passed on another |
| skipped elsewhere | the same check reported `SKIP` on another target in this run, and why |
| first failure | no other check in this suite failed before it |

Each line is present only when its condition holds, and each names the record it
came from. Nothing here guesses at a cause, ranks likelihood or suggests a fix:
those are the reader's job and a wrong guess printed in a fixed format is worse
than no guess, because it is read as a finding.

What it does is collapse the five files a reader would otherwise open to decide
whether a failure is about the firmware, the device or the harness. "Why did it
fail" starts with that distinction, and the run already recorded everything
needed to make it.

**OBS-3.28** [P2] The generator takes an optional second `-j` tree,
`--compare DIR`, and emits a `## Changes since` section listing every check whose
verdict differs between the two runs, grouped as newly failing, newly passing,
newly skipped and no longer run.

A reader looking at a red build asks "is this new" before anything else, and a
reader looking at a green one asks "did we lose coverage". Both are a comparison
of two trees, and the identity key of OBS-3.6 is what joins them. No storage, no
database and no history: two directories on disk, which is what a CI job already
has when it downloads the previous run's artifact.

The section is absent when the flag is not given. The compared run's identity
from OBS-2.11 is named in the heading, so it is clear what "since" means.

This is the closest this document comes to the dashboard section 10 rules out,
and it stays inside that rule: no service, no retention, no accumulation. One
command, two directories, one section.

**OBS-3.29** [P1] The report names where the time went: the slowest suite runs
and the slowest checks, each as a short table in the detail part, and any check
whose duration passed `report.SLOW_CHECK_SECONDS`.

The runner already prints the three slowest suites in its console summary, and
that line is thrown away with the console. A gate is judged on wall clock as
much as on its verdict, and a check that has quietly become slow is a defect
that no verdict reports.

**OBS-3.23** [P6] The report shows each suite run's stills (OBS-8.28) in the
detail part: the text form of each one inline in a fenced block, in the order
they were captured, each labelled with its kind and its `mm:ss` offset into the
recording, and the PNG beside it as a relative link for a reader who has
downloaded the artifact. A failing suite's stills also appear in the summary
part, under that suite's failing checks, because that is where somebody looking
at a build page is already reading.

This is what makes a recording useful to a reader who never opens it. Depends on
OBS-8.28, so it is absent from a run with no recorder and the report says
nothing rather than showing an empty section.

**OBS-3.21** [P1] The document is deterministic. Two runs of the generator over
the same tree produce identical bytes. That means no generation timestamp, no
absolute paths, no dictionary iteration order that depends on insertion, no
`repr` of a floating-point number, and durations formatted by
`report.format_duration` rather than by a format string of the generator's own.
The property is what makes the acceptance criterion in section 3 checkable and
what lets two reports be diffed against each other, which is how a reader
compares last night's run with this one.

**OBS-3.22** [P1] The first line under the title is one greppable status line
holding the whole run in machine-readable form:

```
RESULT: FAIL  targets=2  suites=30  ok=27  fail=2  warn=0  skip=1  recoveries=1  exit=1
```

The keys are fixed, the order is fixed, and the separator is two spaces. This is
what a script or a model reads first, and it is what a person greps a directory
of runs for. The counts come from the `run` records, not from a recount of the
`suite` records, so the line cannot disagree with the runner.

**OBS-3.15** [P1] The document is GitHub Flavored Markdown and nothing else: ATX
headings, pipe tables, fenced code blocks, bullet lists, and links relative to
`DIR`. No HTML elements, the `<!-- detail -->` marker excepted, since no
renderer displays a comment. No remote images, no remote stylesheets, no
scripts, no external references of any kind. Table columns are padded to a
common width so the raw file is aligned in a terminal. This is the property that
lets one file serve the job summary, an editor, `pandoc` and a model with no
per-target rendering, and it is checkable by a test.

**OBS-3.16** [P6, optional] A PDF is derived from `index.md` by one documented
command and by no report-generator code:

```
pandoc index.md -o index.pdf --pdf-engine=weasyprint
```

Off by default and never run in CI. The PDF answers neither question in the
Purpose section: it is not greppable, it cannot be pasted into a model at a
sensible token cost, it cannot carry the video, and inside a zipped artifact it
is exactly as undownloadable as the file it came from. Its one real use is
sending a run to somebody with no access to the repository or the build page.
The cost is two packages absent from this repository's build image: `pandoc` at
roughly 150 MB installed and `weasyprint` with its Python dependencies at
roughly 60 MB. A LaTeX engine would be about 1 GB and is not needed. Wide tables
wrap rather than clip, and the fenced screen captures get the monospace font the
default template provides.

**OBS-3.17** [P1] The generator renders any `-j` tree it is given and never
fails on missing or malformed input. A target directory with no `run.jsonl`, a
suite whose per-suite JSONL has no closing `suite` record, and a truncated final
line are each rendered as what they are rather than treated as an error. A
truncated or unparseable line is skipped and counted. The generator's own exit
status is zero whenever it wrote a document. A run killed mid-suite is exactly
when the evidence matters most, so refusing the tree removes the report at the
worst moment.

**OBS-3.18** [P1] The report states run completeness in its header: whether the
runner wrote a closing `run` record, which suites have no closing `suite`
record, and how many JSONL lines were skipped per OBS-3.17. A suite with no
closing record carries the verdict `incomplete` and no duration in the OBS-3.4
table. A suite the runner killed needs nothing new here: `run_one_attempt` already
closes it through `report.suite_fail` with the reason `describe_exit` produces,
which names the signal, so it renders as an ordinary `FAIL`.

`incomplete` is a word the report prints, not a verdict. The verdict vocabulary
in `tests/lib/report.py` is closed at `OK`, `FAIL`, `WARN` and `SKIP`
(`tests/lib/README.md`), and nothing here adds to it: no suite and no runner ever
emits `incomplete`, because the state it names is the absence of a record rather
than a result. Rendering it in lower case, where every real verdict is upper
case, keeps the two apart on the page as well as in the code.

### Acceptance criteria for section 3

- Running the generator against the checked-in fixture produces an `index.md`
  byte-for-byte reproducible across runs, and byte-for-byte equal to a copy of
  the expected document checked in beside the fixture.
- A test asserts the generated Markdown contains no HTML element, no `http://`
  or `https://` outside inert text content, and exactly one `<!-- detail -->`
  line.
- A test asserts every pipe table's columns are padded to a common width.
- A test asserts every relative link in the document resolves to a file that
  exists under `DIR`.
- A test asserts the status line of OBS-3.22 is present, is the first non-blank
  line after the title, and that its counts equal the `run` records' counts.
- A test asserts a failing check's entry carries a `./run-tests` command naming
  that suite, that target and that mode, and a fenced block ending with the last
  line of that suite's log.
- A test asserts a check's `extra` string appears in the detail part, and that a
  check whose `seconds` exceeds `report.SLOW_CHECK_SECONDS` is marked `SLOW`.
- A test asserts the header names the product and firmware version taken from an
  `ident` health record, and reads `firmware unknown` for a target with none.
- A test runs the generator against a fixture truncated mid-line and against a
  fixture whose last suite has no closing record, and asserts a document is
  written, the exit status is zero, the suite reads `incomplete`, and the header
  states the skipped-line count.
- `pandoc index.md -o index.pdf --pdf-engine=weasyprint` succeeds on the
  fixture, on a machine where those packages are installed. Not a CI test.

---

## 4. GitHub integration

**OBS-4.1** [P2] A step in the CI job appends the part of `DIR/index.md` above
the `<!-- detail -->` marker to `$GITHUB_STEP_SUMMARY`. It is a copy, not a
render: no second generator exists and the summary cannot say anything the
report does not. When the marker is absent the whole file is copied. When what
it would copy exceeds the limit in OBS-4.3, it truncates at a line boundary and
appends one line saying so. Depends on OBS-3.14 and OBS-1.7.

**OBS-4.3** [P2] The summary stays inside GitHub's limits: 1 MiB per step, and a
maximum of 20 step summaries displayed per job. Summaries are per step and
cannot be modified by a later step, so the whole summary is written once. A run
of three targets and fifteen suites produces a summary part of roughly 10 to
40 KB, so the truncation path in OBS-4.1 is a guard rather than the normal case.

**OBS-4.4** [P2] The summary does not attempt to deep-link into the artifact.
No URL serves one file out of a zipped GitHub artifact, so the click-through
terminates at the artifact page and the navigable report is inside the download.
Depends on OBS-3.1.

**OBS-4.5** [P2] The artifact link comes from the `artifact-url` output of
`actions/upload-artifact`, which this repository already uses at v7 in
`.github/workflows/build.yml`. The link requires the viewer to be logged in, and
the summary says so.

**OBS-4.7** [P2] Anything a developer must see without downloading the artifact
is text in the summary. Nothing binary is viewable inline, and the design does
not pretend otherwise.

A raster image cannot be shown on the build page. Three routes were considered:

- A `data:` URI in the summary Markdown. GitHub sanitises rendered Markdown and
  permits only `http` and `https` in an image source, so a base64 image is
  expected to render as nothing. **UNVERIFIED**, see OQ-9. Even if it worked, a
  PNG of one 384x272 VIC frame is roughly 5 to 20 KB raw and a third larger
  again as base64: workable for a handful of frames, not for a run's worth.
- Committing images to a branch so they have an `https` URL. Repository growth
  proportional to the number of runs, and the same commitment already declined
  for a hosted report in section 10.
- A self-contained HTML page inside the artifact with images inlined. It works,
  and the artifact still has to be downloaded and unzipped first.

The evidence this harness needs is not a photograph. `machine:menu_screen`
returns a 40x25 character plane (OBS-5.5) and the `readmem($0400, 1000)`
fallback returns the same shape. Its text rendering carries the same
information, costs about 1 KB in a fenced block, is greppable and diffable, and
appears inline with no download. That is why OBS-3.10 puts the screen text in
the summary part.

A recording cannot be viewed inline at all. A step summary does not embed video;
the `<video>` elements seen in issue comments come from GitHub's own
user-attachment upload, a browser flow a CI job cannot drive. Text is the
substitute: each failing check's entry carries its `mm:ss` timecode into the
recording (OBS-8.11), and the `.srt` sidecar (OBS-8.12) is searchable without
opening the video. A person who wants the frames downloads and seeks.

**OBS-4.8** [P2] The path from a build result to an artefact is three hops and
the summary states plainly where each one ends:

1. Build page to evidence: no hop. The step summary is the report's summary
   part, so the verdict table, every failing check with its screen text, and the
   health table are already on the page.
2. Build page to artifact page: one click on the `artifact-url` link. The viewer
   must be logged in (OBS-4.5).
3. Artifact page to a single file: a download and an unzip. GitHub serves no URL
   for one file inside a zipped artifact (OBS-4.4). `index.md` at the root of
   the download names every sibling file by relative path in its "Files in this
   run" section (OBS-3.14), so the unzip is the last navigation step required.

Hop 3 is a GitHub limitation, not a design choice, and the summary says so
rather than implying a link exists.

### The job that runs the gate

**OBS-4.9** [P2] The gate runs from `.github/workflows/e2e.yml`, a workflow of
its own rather than a job added to `.github/workflows/build.yml`. Four reasons:
it runs on different hardware, it takes 15 to 30 minutes against a build that
takes longer still, it must not run twice concurrently against one set of
devices, and a red gate has to be distinguishable from a red build at a glance.
The workflow's shape:

| Element | Value | Why |
|---|---|---|
| `runs-on` | `[self-hosted, e2e]` | `build.yml` uses the bare `self-hosted` label, so an unlabelled job would land on the build machine, which is not on the device LAN. The `e2e` label is applied to the runner that is. |
| trigger | `workflow_dispatch`, plus `schedule` | The devices are physical and shared. A push trigger would queue a run per commit against hardware that can serve one at a time. |
| `concurrency` | one group for the whole workflow, `cancel-in-progress: false` | Two runs of the gate would drive the same devices at once. Cancelling the running one mid-suite leaves a device in an unknown state, so the second waits. |
| `timeout-minutes` | set, and above the longest expected run | A hung suite otherwise holds the devices until someone notices. |
| password | a repository secret, passed as `U64_PASS` in the step environment | Never on a command line, per OBS-1.8. |
| targets | a `workflow_dispatch` input, defaulting to the standing set | The set of connected devices is an operator fact, not a repository fact. |

The steps, in order: check out; run
`./run-tests -j "$RUNNER_TEMP/e2e" <targets>` with `--recover-command` set to
the operator's recovery tool and `continue-on-error` so the following steps
still run; run `python3 tools/e2e_report.py "$RUNNER_TEMP/e2e"` under
`if: always()`; append the summary part under `if: always()` (OBS-4.1); upload
the artifacts under `if: always()` (OBS-4.10); and finally fail the job on the
runner's exit status.

`if: always()` on the report, the summary and the uploads is the whole point.
A run that was cancelled or that timed out is the run whose evidence is worth
most, and a step that only runs on success produces nothing exactly then.
OBS-3.17 is what makes that safe: the generator renders a half-written tree.

**OBS-4.10** [P2] The run uploads two artifacts, not one, because they have
different lifetimes and different sizes:

| Artifact | Contents | Retention |
|---|---|---|
| `e2e-report-<run id>` | `index.md`, every `.jsonl`, every `.log`, every capture, every `syslog*.txt` | the repository default, 90 days |
| `e2e-video-<run id>` | `video.mp4` and `video.srt` per target | 7 days |

Resolves OQ-2. The report and the JSONL are a few hundred kilobytes and are what
anyone looks at weeks later; the recordings are the bulk of the bundle and are
looked at within days of the run that produced them, if at all. Splitting them
also means the report artifact stays small enough to download over a slow link
while a failure is being investigated. The video artifact does not exist when
the recorder was not asked for, which is the default (OBS-8.1).

The artifact link in the summary is the report artifact's `artifact-url`
(OBS-4.5).

### Acceptance criteria for section 4

- A test runs the summary step against the fixture with `GITHUB_STEP_SUMMARY`
  pointed at a temporary file and asserts the written bytes are exactly the
  prefix of `index.md` up to the `<!-- detail -->` line, and are under 1 MiB.
- A test with a fixture that has no `<!-- detail -->` line asserts the whole
  file is copied.
- A test with an oversized fixture asserts the truncation lands on a line
  boundary and the note is appended.
- With no `GITHUB_STEP_SUMMARY` in the environment, the step exits zero and
  writes nothing.
- A run of the workflow that is cancelled part way still produces a report, a
  summary and an artifact, and the report's header says the run is incomplete
  and names the suite that was open.
- The password does not appear in the job log, in the step summary or in any
  uploaded file.

---

## 5. Failure capture in the runner

**OBS-5.1** [P3] `./run-tests` captures device state after any suite whose child
process exits non-zero. Unconditional, needing no flag, because on a green run
it costs nothing: no suite failed.

**OBS-5.2** [P3] The capture is taken in `run_one_attempt`, immediately after
`subprocess.run` returns and before the `ui_state_gate("verify", ...)` call in
the same function.

The `verify` gate, not the `ensure` gate, is the deadline. `ui_state.verify`
calls `clear_computer_menu`, presses the menu button, calls `enter_file_browser`
and then `describe_open_menu`, which presses RUN/STOP once per level and leaves
the menu closed. It runs unconditionally after every E2E suite, and by the time
`ui_state_gate("ensure", ...)` is reached the screen has already been replaced.
`Device.ensure_healthy` in the retry path runs later still.

**OBS-5.3** [P3] The capture takes exactly three device reads, all `GET`, all on
the runner's five-second probe client `Device.probe`
(`DEVICE_RECOVERY_PROBE_TIMEOUT_SECONDS` in `run-tests`):

- `GET /v1/machine:menu_screen` when a menu is open, otherwise
  `GET /v1/machine:readmem?address=0400&length=1000`.
  `api.MachineApi.menu_screen` returns `None` on HTTP 404, and on this endpoint
  404 means "no menu is open" rather than "this firmware has no such endpoint",
  so the fallback is the ordinary path rather than the rare one: most suites
  leave the menu closed and the interesting screen is the C64's. The capture
  records which of the two it took, because they are different screens and a
  reader must not have to guess.
  The fallback is best-effort and the report says so: the C64 screen matrix
  address depends on the VIC bank and on `$D018`, so a program that has moved it
  is not rendered correctly, and the bytes are screen codes rather than PETSCII.
  1000 is well inside `api.MAX_READ_LENGTH`, so the read needs no chunking.
- `GET /v1/machine:heap`, through the call OBS-6.3 adds. A device whose firmware
  predates it answers 404 and the capture records the absence.
- `GET /v1/drives`, via `api.DrivesApi.list`.

**OBS-5.4** [P3] The capture writes its artefacts into the target's `-j`
directory under the names given in OBS-2.10, so the report generator finds them
by target, suite and attempt with no lookup table. Depends on OBS-2.1 and
OBS-2.10.

**OBS-5.5** [P3] The screen capture is rendered as text as well as stored raw.
`machine:menu_screen` returns exactly 2000 bytes, a 40x25 character plane
followed by a 40x25 colour plane (`SCREEN_BYTES` in
`tests/e2e/lib/ui_backend.py`). `api.MachineApi.menu_rows` is the decoder to
use: it takes the character plane, keeps the printable range and maps everything
else to a space, and returns 25 rows of 40 characters. It is already in the
shared library, so the capture calls it rather than adding a fourth decoder
beside it, `RestBackend` and `tools/api/menu_screen_tool.py`. The tool's own
renderer is not reusable here: it emits ANSI colour for a terminal, which is the
wrong output for a fenced Markdown block.

The `readmem` fallback of OBS-5.3 returns 1000 screen codes rather than 2000
bytes, so it is decoded by the same printable-range rule applied to those 1000
bytes. Screen codes and the menu plane are not the same encoding; the capture
records which one it read and the report labels the block accordingly.

The text rendering is what makes the failure visible on a build page with no
download (OBS-4.7), so it is not an optional convenience.

**OBS-5.7** [P3] A capture that raises is recorded as a failed capture and
otherwise ignored. A suite that has just taken the device down is exactly the
case where these calls will not answer, and a capture that propagated would turn
a suite failure into a harness failure. Follows from OBS-1.1.

**OBS-5.8** [P3] Nothing is captured per check. Per-check capture would be twenty
to fifty extra requests per suite, issued from inside the scenarios the harness
is meant to observe from outside.

### Acceptance criteria for section 5

- A device-free test drives the capture function against a stub API that raises
  on every call and asserts the function returns normally and records the
  failure.
- A device-free test drives it against a stub returning a synthetic 2000-byte
  menu screen and asserts the rendered text matches the expected 40x25 grid.
- On hardware: a deliberately failing suite produces a capture directory with
  three artefacts, and the suite's verdict and the run's exit status are
  unchanged from the same failure without the capture.

---

## 6. Heap sample in the health sweep

**OBS-6.1** [P4] `heap` is added as a ninth check in `tests/lib/health.py`,
beside the existing eight. The sweep already runs before every E2E suite and
again after any that fails, it runs when the runner owns the device exclusively,
and its result already reaches the JSONL. One `GET` adds roughly 10ms to a sweep
costing about 150ms.

`health.Check.render` renders every passing check as `name=NNms`, and for this
one it renders `heap=NNNNNNB`, the free byte count. A latency for the heap check
says nothing anybody wants; the figure is the point. The special case is on the
check's name, and nothing else about `render` changes. In particular it does not
become "render `detail` when `detail` is set": `ident` and `dma` both carry a
detail string already, so that rule would rewrite two existing checks' output
and lengthen the one-line sweep as a side effect of adding a ninth check.

**OBS-6.2** [P4] The check reads `GET /v1/machine:heap`, whose handler in
`software/api/route_machine.cc` returns `free`, `min_ever_free` and `total`.
`free` is the figure to diff. `min_ever_free` is the low-water mark since boot
and never recovers, so it says whether a run came close to running out but
cannot distinguish a leak from a transient peak. `total` is
`configTOTAL_HEAP_SIZE` and is constant.

**OBS-6.3** [P4] A `machine:heap` call is added to `MachineApi` in
`tests/lib/api.py`, returning the three figures and distinguishing a 404 from a
transport failure, because the health check needs `SKIP` for the first and
`SKIP` for the second (OBS-6.5) while the soak suites need `SKIP` for the first
and a failure for the second.

Five suites define their own `HEAP_PATH = "/v1/machine:heap"` and read it
through `rest.json` today:

- `tests/soak/api/heap_leak_test.py`
- `tests/soak/filemanager/prg_context_menu_leak_test.py`
- `tests/soak/filemanager/browser_refresh_leak_test.py`
- `tests/soak/filemanager/mount_cache_leak_test.py`
- `tests/soak/io/c64/assembly_search_leak_test.py`

The rule in `tests/lib/README.md` is that a device endpoint belongs in `api.py`
rather than in a suite, so all five move onto the new call. Each keeps its own
settle behaviour: `mount_cache_leak_test.heap_free` sleeps
`SETTLE_SECONDS` before sampling and that sleep belongs to the suite, not to the
API call. Each also keeps its own availability check, which is `rest.status`
against the same path today and becomes the new call's 404 answer.

**OBS-6.4** [P4] The heap check's figures reach the JSONL inside the existing
`health` record, as a per-check entry alongside `name`, `state`, `ms` and
`detail`. No new record kind. Depends on OBS-2.5.

**OBS-6.5** [P4] The heap check can never fail the sweep. `health.Health.ok` is
`not self.failed` and a degraded sweep is what triggers the recovery command in
`Device.ensure_healthy`. The check reports `OK` with the figure, or `SKIP` when
the endpoint answers 404 on firmware that predates it. The precedent is the
jiffy check in `health.probe`, already downgraded from `FAIL` to `SKIP` when the
raster says the machine is alive.

**OBS-6.7** [P4] The heap series is never an assertion. A sample taken at a
suite boundary has no settle time.
`tests/soak/filemanager/mount_cache_leak_test.py` measures an FTP session
borrowing several kilobytes and giving them back shortly after it closes: 7568
bytes still outstanding immediately after twenty listings and zero a few seconds
later, which is why that suite sleeps 6 seconds before every sample. A boundary
sample will therefore sometimes read a transient borrowing as a step down. Leak
assertions stay in `tests/soak/`.

**OBS-6.8** [P4] No standalone heap sampler on a timer. One sample per suite
boundary gives every suite a before and an after by construction, because suite
N's before sample is suite N-1's after sample. The device serves about four
concurrent HTTP connections and reclaims an abandoned one after roughly 18
seconds, so a sampler taking a slot at the wrong moment can push a suite's
request into a retry.

### Acceptance criteria for section 6

- A device-free test of `health.probe` with a stub API asserts that a heap
  endpoint answering 404 produces `SKIP`, and that a heap endpoint raising any
  error still leaves `Health.ok` true.
- On hardware: a full sweep's console line carries a heap figure, and a run with
  the heap check present triggers the recovery command exactly as often as the
  same run without it.
- The `health` records in `run.jsonl` carry a `heap` entry with `free`,
  `min_ever_free` and `total`.
- The five soak suites named in OBS-6.3 pass unchanged in behaviour against the
  same device after moving onto the shared call, and none of them still defines
  `HEAP_PATH`.

---

## 7. Device log collection over syslog

The device's log reaches the network by exactly one route: remote syslog.
`software/network/syslog.cc` opens a UDP socket to an address taken from
`CFG_NETWORK_REMOTE_SYSLOG_SERVER`, which appears in the configuration tree as
the store `Network Settings`, item `Log to Syslog Server`
(`software/network/network_config.cc`). `syslog.cc` is built by six of the seven
ultimate makefiles; only `target/u2/zpu/ultimate/Makefile` omits it, so this
works on a U2 as well as on a U64.

There is no HTTP endpoint that returns a log, and no alternative log-retrieval
mechanism is in scope; see section 10.

### Device configuration

**OBS-7.1** [P5] Each CI device carries `Network Settings` / `Log to Syslog
Server` set permanently to `<collector-ip>:<port>`. This is a deployment step on
the devices, not a code change in this repository. It is the one device cost in
this document that no `./run-tests` flag controls, because it is boot-time state
(OBS-7.3): a configured device pays the forwarding cost stated in OBS-7.11
whether or not a run is in progress. OBS-7.16 establishes that the cost is
bounded and harmless when no collector is listening. **UNVERIFIED**: nobody has
yet agreed that the CI devices may carry this setting permanently. See OQ-3.

**OBS-7.2** [P5] The configured value is a literal IPv4 address, not a host
name. `Syslog::init` parses it with `ipaddr_aton` and falls back to
`INADDR_ANY`, which disables the syslog, when the parse fails. The whole string
is 8 to 31 characters (`software/network/network_config.cc`) and takes the form
`<ip>[:<port>]`, port 514 by default.

**OBS-7.3** [P5] The harness never enables or changes the syslog setting.
`Syslog::init` is called once from `ultimate_main`, so the setting takes effect
at boot and a `PUT /v1/configs/Network Settings/Log to Syslog Server` during a
run does nothing until the device is rebooted.

**OBS-7.4** [P5] The runner reads the setting at the start of the run to check
it points at the collector, using `api.ConfigsApi.get("Network Settings", "Log
to Syslog Server")`, and reads it again at the end. A mismatch, an empty value
or a value that changed during the run is reported once as a warning and the run
continues. Nothing is corrected; see OBS-15.10 for why, and for the measured
reason a suite can lose this setting without meaning to. Follows from OBS-1.1.

The start read catches the operational failure that otherwise produces no log
and no reason for it: a device that was reflashed and lost the setting. The end
read catches the one that produces no log in the *next* run, which is far harder
to trace back.

### The collector

**OBS-7.5** [P5] The collector is a plain UDP sink, not an RFC syslog daemon.
`Syslog::forwardLogging` sends the bare line text with `send(sockfd, line,
linelen, 0)`: no priority prefix, no version, no timestamp, no hostname, no
trailing newline. A conformant syslog daemon may refuse these datagrams.

**OBS-7.6** [P5] One datagram carries one log line, and the line text is the
whole payload. Empty lines never arrive: `Syslog::forwardLogging` skips a line
whose length is zero. Carriage returns never arrive: `Syslog::charout` discards
`\r`. The collector must not assume its line count matches the device's own
output.

**OBS-7.7** [P5] The collector stamps every datagram with `time.time()` at the
moment of receipt, on the host running `./run-tests`. That receive time is the
only time any log line carries; nothing in the payload carries a time. Follows
from OBS-1.5.

**OBS-7.8** [P5] The collector records the source IPv4 address per datagram and
writes each line, prefixed with its receive timestamp, to the file for the
target that address maps to: `DIR/<slug>/syslog.txt`. A datagram from an address
that maps to no target in this run goes to `DIR/syslog-unmapped.txt`, with its
source address on the line. Lines carry no device identity of any kind, so the
source address is the only discriminator when several devices log to one
collector, and a device nobody expected to be talking is itself the misbehaviour
question 2 asks about, so its lines are kept rather than dropped.

**OBS-7.9** [P5] The collector maps each source address to a target token and
emits a `kind=log` record naming the target, the output file and the collector's
start time. See OBS-7.18 for what a cartridge target maps.

**OBS-7.10** [P5] The collector runs for the duration of the run, starting
before the first suite and stopping after the last. Devices log continuously,
including between runs, and datagrams received outside the run window are still
written with their receive times.

### What the collector receives, and what it does not

These are properties of the firmware as it stands. The report and anyone reading
it must treat the log as best-effort evidence.

**OBS-7.11** [P5] The log is incomplete by construction and the loss is not
measurable from the receiving side. Four independent causes:

- UDP has no retransmission, so a datagram lost in the network is simply gone.
- The forwarding buffer is 16 KB (`syslog_bufsize` in
  `software/application/ultimate/ultimate.cc`). On overflow, `Syslog::charout`
  sets `overflow` and drops every subsequent character, and
  `Syslog::forwardLogging` then calls `rewind()` and discards the whole buffer.
  A burst loses an unbounded block.
- Output is throttled to about 200 lines per second by a 5ms delay after each
  sent line in `Syslog::forwardLogging`.
- `Syslog::failed_sends` counts send errors and is never read by anything, so a
  send failure leaves no trace anywhere.

**OBS-7.12** [P5] A line's receive time lags the moment the firmware printed it
by an unbounded amount, so attributing a line to a check by interval is
approximate. Three causes:

- The forwarding task polls every 100ms.
- The 5ms per-line throttle means a 200-line burst takes at least a second to
  drain, and the whole burst can be received after the check that produced it
  has closed.
- Everything printed before the network link comes up is buffered.
  `Syslog::forwardLogging` waits for link and then one further second before it
  opens the socket, so the entire early boot log arrives in one burst with
  receive times clustered at that moment.

The report presents a check's log slice as "lines received during this check",
never as "lines the device produced during this check".

**OBS-7.13** [P5] The log begins partway through boot. `custom_outbyte` is null
until `ultimate_main` assigns it, and `outbyte` (`software/system/itu.c`) calls
it only when it is set. Everything printed earlier goes to the hardware UART
alone: the product version banner, the FPGA capabilities line, `"Executing init
functions."` and everything `InitFunction::executeAll()` prints. The syslog
carries the firmware log from that assignment onward, starting with the RTC date
and time.

**OBS-7.14** [P5] The report marks a device restart on its timeline from the line
`"All linked modules have been initialized and are now running."` in
`ultimate_main`, which is the first distinctive boot marker reaching the syslog
and is immediately followed by the FreeRTOS task list from `print_tasks`. There
is no uptime counter anywhere in the firmware and nothing on the REST surface
answers "has this device rebooted", so this marker is the only signal available.
A spontaneous reset is named in the Purpose section, which is why this earns a
requirement of its own.

**OBS-7.15** [P5] The collector must not be relied on to receive an assertion
failure. `vAssertCalled` calls `portENTER_CRITICAL()`, prints
`ASSERTION FAIL: <file>:<line>`, prints the task list, then spins forever.
Interrupts stay disabled and the syslog task never runs again, so the assertion
text sits in the forwarding buffer and is never sent. The collector sees the log
stop, up to roughly 100ms before the assertion. The report treats an abrupt end
of a device's log as a signal in its own right. Making the assertion text reach
the collector is firmware work; see OBS-9.1.

**OBS-7.16** [P5] A device logging to an address where nothing is listening is
harmless to the run. The socket is `connect`ed, so lwIP can surface an ICMP
port-unreachable as an error on a later `send`, which only increments the unread
`failed_sends` counter. The firmware deliberately does not log a send failure,
to avoid an infinite loop. This is what makes the permanent setting of OBS-7.1
safe to leave in place between runs.

**OBS-7.17** [P5] The collector binds a non-privileged UDP port, and the devices
are configured to that port rather than to 514. `Syslog::init` defaults to 514
only when the configured value carries no `:<port>` suffix, and it accepts any
port from 1 to 65535, so the configured value is `<ip>:<port>` with a port above
1023. Binding 514 needs root on Linux and on macOS, and a CI runner that has to
run the gate as root to collect a log is a worse trade than a port number. The
port is a `./run-tests` option with a documented default, so the same number is
in one place on the host side.

The collector binds `0.0.0.0`, not a chosen interface. The runner host may have
more than one, and the datagrams' source addresses are what identify a device
(OBS-7.8), so binding the wildcard costs nothing and removes an operator
decision. A firewall on the runner host has to admit that port; a collector that
starts, receives nothing and reports an empty log is the failure this produces,
which is why OBS-7.4 exists to catch its most common cause.

**OBS-7.18** [P5] For a cartridge target such as `u2@c64u`, both machines may be
logging, and both logs are kept. `syslog.cc` is built for six of the seven
ultimate makefiles, so a U2 cartridge logs, and the C64 Ultimate it is plugged
into logs as well. The rule:

- The device under test's log, which is `targets.Target.device`, goes to
  `DIR/<slug>/syslog.txt`. This is the log about the firmware being tested.
- The computer's log, when the target is split and that machine's address is
  also mapped, goes to `DIR/<slug>/syslog-<computer>.txt`.
- An address belonging to neither goes to `DIR/syslog-unmapped.txt` per OBS-7.8.

`targets.Target` gains one property for this, `log_hosts`, returning the device
alone for a whole-machine target and both machines for a split one. It sits
beside `input_host` and `resources`, which are the same kind of rule: which of a
target's machines owns a given thing. Resolves the log half of OQ-5.

Addresses are resolved once, at collector start, with
`socket.getaddrinfo(host, 0, socket.AF_INET, socket.SOCK_DGRAM)`, the same call
`av_stream.AvStreamCapture` uses to decide which packets are its device's. A
host name that does not resolve is reported once at startup per OBS-1.2 and its
datagrams land in the unmapped file, where they are still evidence.

### Acceptance criteria for section 7

- A device-free test feeds synthetic datagrams from two source addresses into
  the collector and asserts two output files, correct per-line receive
  timestamps, and correct target attribution for a `cartridge@computer` token:
  the cartridge's lines in `syslog.txt` and the computer's in
  `syslog-<computer>.txt` (OBS-7.18).
- A device-free test feeds a datagram from a third, unmapped address and asserts
  it lands in `DIR/syslog-unmapped.txt` with its source address on the line.
- A device-free test asserts the collector starting on a busy port produces one
  warning at startup and does not raise.
- A device-free test of the report's slicing asserts a line whose receive time
  falls in a gap between check intervals is attributed to the suite.
- On hardware: with one device configured and rebooted, a run produces a log
  file whose first line after a device restart is the RTC date and time and
  which contains the boot marker of OBS-7.14.

---

## 8. Recording the run

The whole of this section is optional. Nothing in sections 1 to 7 depends on it,
and a run with no recorder answers both questions in the Purpose section.

It is also the longest section here, and its requirement numbers are not in
order, because a number never moves once it is written (see "How to read this
document"). Read it by subsection:

| Subsection | What it settles | Requirements |
|---|---|---|
| What the recording has to show | why there are two panes | - |
| (opening requirements) | sources, addressing, sockets, re-arming | OBS-8.1 to OBS-8.5, OBS-8.16 |
| (wire formats) | video and audio on the wire, geometry, network cost | OBS-8.6, OBS-8.17 to OBS-8.19 |
| (composition and its sources) | the two panes, the menu tap and its spool, the configuration surface | OBS-8.20 to OBS-8.23 |
| Edge conditions on the wire | loss, reordering, concealment, foreign senders, shedding, stills | OBS-8.24 to OBS-8.28 |
| Layout and stamping | one file or two, the stamp, the failure edge, the progress bar | OBS-8.29 to OBS-8.33 |
| How it looks | the visual system and the cards | OBS-8.35, OBS-8.36 |
| The menu payload, exactly | what `machine:menu_screen` returns | OBS-8.37 |
| From three sources to one or two files | the pipeline, stage by stage | OBS-8.38 |
| (encoding and output) | the encoder, degradation, the capture record, subtitles, chapters | OBS-8.7 to OBS-8.12, OBS-8.34 |
| (targets and tests) | cartridge targets, the device-free tests | OBS-8.14, OBS-8.15 |

If you are implementing this, read OBS-8.38 first: it is the map of everything
else here. Then read section 15, which says what happens when a suite wants the
same streams, and which requires the library this section's socket, decoding and
arming requirements all live in.

### What the recording has to show

The device's VIC output is not the whole picture, and on two of the three UI
modes it is not the interesting half.

- Under `freeze`, the menu is drawn into the C64's own screen, so the VIC stream
  carries it.
- Under `overlay`, the menu is a separate hardware layer composited after the
  VIC. The VIC stream carries what the C64 is showing, and not the menu the
  harness is driving.
- Under `telnet`, the menu is drawn into a Telnet session and reaches no video
  path at all.

So a recording of the VIC stream alone cannot answer "what was the harness
looking at when it decided this check failed" for two thirds of the gate. The
menu is available over REST, from `GET /v1/machine:menu_screen`, as the same
40x25 character plane and colour plane the failure capture uses (OBS-5.5).

The recording therefore has two panes side by side, the device's video on the
left and the menu on the right, with the device's audio over both. A reader
watching one file sees what the machine showed, what the menu showed, and hears
what the SID did, on one timeline.

**OBS-8.1** [P6] Recording is opt-in and off by default, through a new
`./run-tests` flag. Without the flag nothing is recorded and nothing about the
run changes. Each of the three sources has a flag of its own and each is on when
recording is on; see OBS-8.23 for the whole surface.

**OBS-8.2** [P6] For each target the recorder produces, under that target's
directory and named per OBS-2.10, one video file per the layout of OBS-8.29 and
one `video.srt` beside them: a subtitle track naming the suite, scenario and
check running at each moment. The audio of OBS-8.19 is muxed into every video
file the run produces, so each is playable on its own. Multiple targets record
concurrently, each to its own set.

**OBS-8.3** [P6] The recorder starts the device's video and audio streams with
`api.StreamsApi.start("video", ip=f"{group}:{port}")` and the same call for
`"audio"`, and stops both at the end of the run. `StreamsApi.start` and `stop`
are `PUT`, and together with the re-arms of OBS-8.16 they are the only non-GET
requests any component in this specification makes; see OBS-1.6.

`av_stream.AvStreamCapture.start` already does exactly this pair, including
stopping the video stream when starting the audio stream fails, and is the
behaviour to match.

**OBS-8.4** [P6] Every target streams to the one group and port pair the suites
already use, `239.0.1.64:11000` for video and `239.0.1.65:11001` for audio
(`MULTICAST_GROUP` and `VIDEO_PORT` in `tests/e2e/lib/vic_video.py`,
`VIDEO_GROUP`, `VIDEO_PORT`, `AUDIO_GROUP` and `AUDIO_PORT` in
`tests/e2e/lib/av_stream.py`), and the recorder separates targets by the source
IPv4 address of each packet.

Per-target ports would be the obvious design and they do not work here. Three
suites start the streams themselves at those fixed addresses -
`tests/e2e/api/input_test.py` through its own `start_video_stream`,
`tests/e2e/monitor/monitor_test.py` and `tests/e2e/av/stream_test.py` through
`av_stream.AvStreamCapture.start` - and `streams:start` sets where the device
sends. A recorder that had asked for a different port would lose the stream the
moment one of those suites ran, and would not get it back. The vendor's
documentation adds a second reason: multicast forwarding is decided by group
address alone and ignores the UDP port, so two ports on one group are not
separated by the network either.

Filtering by source address is what `AvStreamCapture` already does, using
`socket.getaddrinfo(host, 0, socket.AF_INET, socket.SOCK_DGRAM)` to build the
set of addresses that count as its device, so the recorder reuses that rule
rather than inventing an addressing scheme the suites do not share.

**OBS-8.5** [P6] Every capture socket sets `SO_REUSEADDR`, and `SO_REUSEPORT`
where the platform has it, before binding. Socket creation belongs to the
library of OBS-15.6, so this is set once rather than in each of the three places
that open one.
`VicStreamCapture.__init__` binds the multicast port with neither today, so a
recorder and a suite cannot both receive the stream. Multicast delivers to every
subscriber, so this is the only thing standing between a recording and a suite
that asserts on the same frames.
`tests/e2e/lib/av_stream._stream_socket` already sets `SO_REUSEADDR` and gains
`SO_REUSEPORT` on the same terms. Both are needed: on Linux `SO_REUSEADDR` alone
lets two sockets share a UDP port, while the BSD sockets in macOS require
`SO_REUSEPORT` for that, and `socket.SO_REUSEPORT` is absent on some platforms,
so it is set inside a `hasattr` guard. See OBS-14.3. Required for OBS-8.2.

This is a multicast property and it does not generalise. Two sockets sharing a
port receive every multicast datagram each; two sharing a port for unicast
traffic split the datagrams between them. OBS-15.8 is where that difference
matters, and it is why the syslog port is bound exactly once.

**OBS-8.16** [P6] The recorder re-arms a stream when it goes quiet. Three
suites stop or restart the device's video stream during a run (OBS-8.4), and
`av_stream.AvStreamCapture.stop` and `input_test` both call `streams:stop` when
they finish, which leaves the device sending nothing. Without a re-arm the
recording is a placeholder card from the first such suite to the end of the run,
which is most of it.

The rule, applied to the video and the audio stream independently: when no
packet has arrived on that stream for a bounded interval, issue `streams:start`
for it again and carry on. The interval is longer than the gap a suite leaves
between its own stop and start, so the recorder does not fight a suite for the
stream mid-check; a suite that has taken the stream keeps it, and the recorder
takes it back once that suite has gone quiet for the interval. Every
re-arm is one `PUT`, which is the OBS-1.6 exception already granted to
OBS-8.3, and each one is counted and reported in the recorder's `kind=capture`
record so a reader can see how contested the stream was.

Three bounds, because a device that has gone is the case this runs into most:

- **Back off.** Consecutive failed re-arms lengthen the interval to a ceiling,
  so a device that is off the network for ten minutes costs a handful of
  requests rather than hundreds. A re-arm that succeeds resets it.
- **Never block the slot loop.** The re-arm is issued from outside the loop of
  OBS-8.38, with a short timeout and no retry, so a device that accepts a
  connection and never answers cannot stall the recording.
- **Never re-arm into a suite.** When the spool of OBS-15.7 says a suite stopped
  or redirected the stream, the recorder waits for that suite to finish rather
  than taking it back mid-check. That is OBS-15.1: the suite wins.

**OBS-8.6** [P6] The wire format, from the vendor's data-streams documentation
and confirmed against `tests/e2e/lib/vic_video.py`:

| Property | Value |
|---|---|
| Packet | 780 bytes: a 12-byte header and 768 bytes of pixels |
| Header | sequence 16-bit LE, frame 16-bit LE, line 16-bit LE with bit 15 marking the last packet of a frame, pixels per line 384, lines per packet 4, bits per pixel 4, encoding 0 |
| Pixels | 4-bit VIC colour indices, low nibble first, 192 bytes per line |
| Frame | PAL 384x272 in 68 packets, NTSC 384x240 in 60 packets |
| Rate | one frame per VIC field: 50 Hz PAL, 60 Hz NTSC |

The consequences the recorder is designed around:

- **2.7 MB/s per device, 4.8 GB over a 30-minute run.** The recorder must drain
  its socket continuously whatever else it is doing, and must never accumulate
  frames (OBS-8.7). A datagram it does not read is a hole in the recording.
- **Four lines per packet, and the header says where they go.** See OBS-8.24:
  a packet's payload belongs at `line * 192` in the frame buffer, and
  concatenating payloads in arrival order is only correct while nothing is lost
  or reordered.
- **A frame is 52224 packed bytes, or 104448 pixels.** The per-pixel
  `putpixel` loop in `VicStreamCapture.capture_image` runs 104448 interpreted
  iterations per frame. That is affordable for the handful of frames a suite
  asserts on and is not affordable at 50 frames a second on a Raspberry Pi
  (OBS-14.1). The recorder unpacks the nibbles with whole-buffer operations
  instead: two `bytes.translate` passes over the payload with tables that select
  the low and the high nibble, then two strided slice assignments into one
  `bytearray`, which is three passes at C speed and no interpreted loop.
  `PIL.Image.frombytes` then takes the result directly.

`VicStreamCapture` stays the shared decoder for suites, and the recorder shares
its constants and its header layout rather than restating them. What the
recorder does not reuse is `capture_image`'s loop: it does not filter by source
address (OBS-8.4) and it raises after eight incomplete frames, and a recorder
that must run for half an hour beside another device on the same group needs
both of those to be different.

**OBS-8.17** [P6] The recording has one frame geometry for its whole length,
fixed at the first complete frame and recorded in the `kind=capture` record.
`ffmpeg` is fed raw frames and a raw stream has no way to say the size changed.
PAL and NTSC differ by 32 lines, and a device can change mode mid-run, so a
later frame of a different height is padded to the fixed geometry rather than
dropped, and each such frame is counted and the count reported. A recording that
silently switched geometry would desynchronise every timecode after it, and
timecodes are what OBS-8.11 promises.

**OBS-8.18** [P6] Multicast on this LAN is a shared cost, and it is stated here
so an operator can decide rather than discover. IGMP snooping is what keeps a
multicast stream off the ports that did not ask for it, and the vendor's
documentation records that switching is by group address alone, with UDP ports
not considered. On a switch without snooping, 21 Mbit/s of video plus 12 Mbit/s
of audio per recording device is flooded to every port for the length of the
run. This is the reason the recorder is off by default (OBS-8.1) and one of the
reasons it is the last thing built.

**OBS-8.19** [P6] The recording carries the device's audio, muxed into the same
`video.mp4`. The wire format, from the vendor's data-streams documentation and
confirmed against `tests/e2e/lib/av_stream.py`:

| Property | Value |
|---|---|
| Packet | 770 bytes: a 2-byte sequence number and 768 bytes of samples |
| Samples | 192 stereo frames per packet, 16-bit signed little-endian, left then right |
| Source | after the internal mixer, so it is what HDMI and the analogue codec get |
| Rate | 47982.8869047619 Hz on PAL, 47940.3408482143 Hz on NTSC |

Three things follow, and each is a way to get it wrong.

- **The rate is not 48000.** It is derived from the video clock and it differs
  between PAL and NTSC. The recorder declares the input rate to `ffmpeg` as the
  device's own rounded value and lets `ffmpeg` resample to a rate the container
  is comfortable with, rather than declaring 48000 and letting a 356 ppm error
  accumulate into a second of drift over a long run.
- **The sequence number is the clock, and loss is concealed rather than
  zero-filled.** See OBS-8.25, which is the whole of it. Zero-filling a gap is
  the obvious approach and it is wrong.
- **A stopped stream is a very long gap.** When a suite stops the audio stream
  the recorder conceals up to a cap and then re-anchors, so the audio track
  stays the same length as the video track without pretending the missing
  seconds were silence.

The recording's audio and video start at the same wall-clock moment, both are
gap-filled by their own sequence numbers, and neither is resynchronised
afterwards. That is the whole of the A/V sync design: two streams from one
device, each with its own exact packet counter, each extended rather than
shifted.

There is no better option available. The device embeds no source timestamp in
either stream, so there is no common clock to map the two onto and no way to
measure true source-to-file latency. Anything beyond "both start together and
neither drifts" would be a measurement this hardware cannot support, which is
why OBS-1.5 puts every timestamp in this document on the host's clock.

**OBS-8.20** [P6] Each output frame is two panes side by side on one canvas:

| Pane | Content | Size |
|---|---|---|
| Left | the most recent complete VIC frame | 384x272, native (OBS-8.6) |
| gutter | flat, one colour | 8x272 |
| Right | the most recent menu screen, rendered | 320x200 of glyphs, centred in 384x272 |

The canvas is 776x272, both dimensions even, no scaling of either pane. A frame
in one pane is never resampled to match the other, because both are pixel grids
whose whole value is being exact (OBS-8.8). The gutter is there so the two read
as two pictures rather than as one wide one; eight pixels because everything
here is on the 8-pixel grid (OBS-8.35).

The right pane is rendered from the payload of OBS-8.37, using the decode
already in `tools/api/menu_screen_tool.py` - `menu_char_to_glyph` for the
character mapping, `split_colour_byte` for the nibbles and `c64_rgb` for the
palette - so the recorder shares them rather than restating them. Glyphs are
drawn 8x8 from the C64 character ROM already in the repository at
`roms/characters.901225-01.bin`, which makes the right pane's text the same size
and shape as the left pane's.

Four states the right pane has to render, because each says something different:

| State | Rendered as |
|---|---|
| a menu screen was read | the menu |
| `menu_screen` answered 404 | a card saying no menu is open |
| the poll failed or timed out | the last menu read, marked stale |
| nothing has been read yet | a card saying so |

Under `freeze` the menu is in the VIC output as well, so both panes show it.
That is correct rather than redundant: it is what the mode does.

**OBS-8.21** [P6] The right pane is event-driven: one menu screen per navigation
step, taken from the screens the harness already reads, and not from a poll on a
timer.

The suites read the menu screen constantly and already read it at exactly the
moments that matter. `ui_backend.RestBackend._menu_screen_body` is called before
a key is sent, repeatedly by `wait_screen_changes` while the redraw starts, and
repeatedly again by `wait_screen_settled` until it stops, for every keystroke
under `overlay` and `freeze`. Under `telnet` the equivalent screen is the
`VT100Screen` the backend maintains from the session's own output. So a
recording that shows the menu after every navigation step needs no new device
traffic at all: it needs the screens the harness is already looking at.

That is what OBS-8.22 provides. Two consequences worth stating plainly:

- The right pane's temporal resolution is *every distinct menu screen the
  harness saw*, which is finer than any poll interval anyone would dare
  configure, and it is aligned to the harness's own decisions rather than to a
  clock.
- The two panes still do not share a resolution. The left pane advances at the
  output frame rate and the right pane advances when the menu changed. A reader
  who assumes the panes are simultaneous will misread a transition, so the
  report and `tests/e2e/README.md` both say what each pane's clock is.

The recorder makes a `menu_screen` request of its own only when both of these
hold: no screen has reached it from the tap for longer than a named interval,
and the run is not between suites with the device idle. That covers the gaps
where no suite is driving the UI, which is where a recording would otherwise
freeze on a stale pane for minutes.

**OBS-8.23** governs the rate of any request the recorder makes for itself:
`--record-menu-min-interval-ms`, a floor on the gap between two consecutive
`menu_screen` requests made for recording, default `0`, meaning no floor. The
knob exists because the device serves about four concurrent HTTP connections and
reclaims an abandoned one after roughly 18 seconds, and OBS-6.8 rejects a
standalone heap sampler on exactly that ground. The differences that make this
acceptable: the tap costs nothing, the recorder's own request happens only when
the tap is silent, it is one sequential request at a time with a short timeout
and no retry, and the floor is one flag away when a run turns out to be slower
with recording on than with it off.

Every screen is held until the next one arrives, so every output frame has a
right pane, and the `kind=capture` record carries how many screens came from the
tap, how many the recorder requested for itself, and how many requests failed.

**OBS-8.22** [P6] The shared UI backend publishes every distinct menu screen it
reads to a spool file, and the recorder consumes it.

| Property | Value |
|---|---|
| Path | `DIR/<slug>/menu-screens.bin`, per OBS-2.10 |
| Written by | `tests/e2e/lib/ui_backend.py`, in the suite process that read the screen |
| Enabled by | an environment variable the runner exports, the way `E2E_JSONL` is |
| Record | a magic marker, a length, a `time.time()` stamp, a kind, then the payload |
| Payload | the 2000 bytes `machine:menu_screen` returned, or the 25 rendered rows under `telnet` |
| Written when | the payload differs from the last one written |

Six things this shape is chosen for:

- **One place.** `RestBackend._menu_screen_body` and the `telnet` backend's
  screen accessor are the two functions every suite's screen reads pass through.
  Publishing there covers every suite without touching any of them.
- **Change only.** The settle loops read the same screen many times per
  keystroke. Writing only on a change collapses that to one record per redraw,
  which is both the volume control and exactly the "one per navigation step" the
  pane wants.
- **Append-only, one write per record.** Several suite processes never write the
  same file, because a suite owns its target's directory for its run, but the
  runner's own capture (section 5) may, so the format carries a length and a
  magic marker and a reader resynchronises on the marker rather than trusting
  the offset. This is the reasoning `report._record` already applies to the
  JSONL.
- **Wall clock from the host.** `time.time()` on the machine that read the
  screen, per OBS-1.5, which is the same clock every interval in this document
  uses, so a screen joins to a check by OBS-2.6 with no conversion.
- **Two payload kinds, named.** A `telnet` screen is text and has no colour
  plane; a REST screen is two planes. The kind says which, so a consumer never
  guesses from the length.
- **Useful without the recorder.** A timestamped sequence of every distinct
  menu screen the harness saw, joinable to check intervals, answers "what was on
  screen when check 26 failed" directly, and it answers it for a run nobody
  recorded video for. The spool is named in the report's file index (OBS-3.14)
  for that reason, and it is why the tap has a flag of its own in OBS-8.23
  rather than being buried inside the recorder.

The suite process is not the recorder, and nothing here makes it one: it appends
bytes it already had in memory to a file and carries on. It performs no device
call it was not already making, which is the whole point.

**OBS-8.23** [P6] Every source and every encoder decision is configurable, and
every one of them has a default that is right for the material.

Recording as a whole is off (OBS-8.1). With it on, all three sources are on:

| Flag | Default | Effect |
|---|---|---|
| `--record` | off | Enables recording at all |
| `--no-record-video` | video on | Drops the left pane and the video stream |
| `--no-record-audio` | audio on | Drops the audio track and the audio stream |
| `--no-record-menu` | menu on | Drops the right pane, the tap and the spool |
| `--record-menu-min-interval-ms MS` | `0` | Floor between two `menu_screen` requests the recorder makes for itself (OBS-8.21) |

Negative flags for the on-by-default sources, matching `--no-health-check` and
`--no-retry`, which is how this runner already spells "on unless you say
otherwise".

Dropping a source changes the canvas rather than leaving a blank pane: video
only is 384x272, menu only is 384x272, both is 768x272 (OBS-8.20). A run with
all three off is a usage error reported at startup, not a recorder that produces
an empty file.

The encoder:

| Flag | Default | Effect |
|---|---|---|
| `--record-layout` | `combined` | `combined` or `separate` (OBS-8.29) |
| `--no-record-stamp` | stamp on | Drops the burned-in timecode and names (OBS-8.30) |
| `--record-quality` | `lossless` | `lossless`, or a quality number for a lossy encode |
| `--record-scale N` | `1` | Integer upscale of the whole canvas, nearest-neighbour |
| `--record-fps N` | a named constant | Output frame rate (OBS-8.8, property 4) |
| `--record-keyint SECONDS` | a named constant | Keyframe interval during static stretches (OBS-8.8, property 3) |
| `--record-ffmpeg-args ARGS` | empty | Extra arguments appended to the encoder command |

Three rules on these, each of which the implementation enforces rather than
documents:

- **The defaults are the specified behaviour.** `--record-quality lossless`,
  scale 1 and scene-cut detection left on are what OBS-8.8 requires. A flag
  moving away from them is the operator's decision.
- **A non-default value is recorded.** The `kind=capture` record carries the
  effective value of every option above, so a reader who finds a blurry
  recording learns from the file why it is blurry rather than guessing.
- **`--record-ffmpeg-args` is an escape hatch and says so.** It is appended, so
  it can override anything the recorder chose, including the pixel-exactness of
  OBS-8.8. `--help` states that. It exists because "other encoder options" is an
  open-ended requirement and a flag per option is not, and because a bad encode
  on a Pi is diagnosed by trying arguments rather than by editing the tree.

`--record-scale` deserves its own note. It defaults to 1 because 384x272 already
carries every pixel the device sent and upscaling costs the square of the factor
in encoder work on the host that has the least of it. Only integer factors are
accepted, because a fractional scale resamples pixel art and gives up the
property the lossless encode was chosen for.

Every one of these flags is listed in `CHILD_FORWARDED_VALUES` or
`CHILD_FORWARDED_NEGATIVE` in `run-tests` in the same commit that adds it, or
`tests/lib/runner_policy_test.py` fails the gate. A multi-target run that
recorded one target differently from another because a flag was not forwarded is
exactly the failure that check exists to prevent.

### Edge conditions on the wire

These are the failure modes a UDP stream actually has, and the shape of the
handling each one needs. Every rule below is distilled from a working
implementation of these two streams in the sibling `c64commander` repository,
under `src/lib/streams/`, which ports the C64 Ultimate OBS plugin's own
(`c64stream`) handling of the same wire formats. That code exists to mirror a
device live on a phone and is far larger than anything needed here, so what
follows is the part that is about the wire rather than about live playback.

The distinction that keeps this proportionate: a live mirror must hide a defect
from a viewer in real time, and a recorder only has to not corrupt the file and
to say what it lost. Where the live implementation adapts, the recorder counts.

**OBS-8.24** [P6] Video frame assembly follows the header, not the arrival
order. Five rules, each of which is a defect if it is missing:

- **Write at the offset the header gives.** A packet's payload belongs at
  `line * 192` in the 52224-byte frame buffer, where `line` is the header's line
  field with bit 15 masked off. `VicStreamCapture.capture_image` concatenates
  payloads in arrival order and infers the height from the length, which is
  correct only while nothing is lost or reordered and shifts the whole rest of
  the frame upward when something is.
- **Take the height from the last packet**, as `line + lines per packet`, and
  clamp it to the two the hardware produces. A frame whose last packet was lost
  is a frame that never completes rather than a short one.
- **Validate the format fields.** Width 384, four lines per packet, four bits
  per pixel. A packet failing any of them is from something that is not this
  stream, and is counted and dropped rather than written into the buffer.
- **Count loss wrap-safely.** Both the packet sequence number and the frame
  number are 16-bit and both wrap, the sequence number roughly every 51
  seconds. A gap is the difference sign-extended to 16 bits: a raw subtraction
  reports a 65535-packet loss at every wrap. A negative difference is
  reordering, not loss.
- **Do not let a reordered frame move the baseline backwards.** Frame loss is
  the gap between consecutively completed frame numbers, and the baseline
  advances only on forward progress. A late frame that moved it back would make
  the next forward frame recompute an inflated gap and count a loss twice.

The counts go in the `kind=capture` record (OBS-8.11): packets, packets dropped,
packets ignored as foreign or malformed, frames completed, frames lost.

**OBS-8.25** [P6] Audio loss is concealed, on the packet sequence number, with
four outcomes per packet:

| Sequence delta | Outcome | Index |
|---|---|---|
| `+1` | write the packet | advances by 1 |
| `<= 0`, within the resync threshold | a late or duplicate packet, discarded | does not advance |
| a forward gap within the fill cap | synthesise the missing packets, then write the real one | advances by the true delta |
| a gap beyond the cap, or a large backward jump | re-anchor the timeline | reset |

Three things this gets right that the obvious implementation does not:

- **The fill is not zeros.** Real SID output carries a DC offset, so a
  zero-filled gap is a step away from the signal and back, which is audible as a
  click at both ends. The fill holds the last sample and fades it toward zero
  over the gap, with a short linear ramp into the first real sample after it, so
  both splices are step-free.
- **A late or duplicate packet must not advance the index.** Advancing on one
  shifts the audio against the video by that packet's 4 ms, permanently, once
  per occurrence.
- **A large backward jump is a device restart, not loss.** It re-anchors rather
  than concealing a gap of tens of thousands of packets.

The constants are the ones the reference implementation uses, and a file writer
tolerates a much larger gap than a live player does: concealing several seconds
into a file is better than a discontinuity, where the same delay in a live
mirror would be worse than a re-anchor. Each is a named constant with that
reasoning beside it. The `kind=capture` record carries packets lost, packets
concealed, late packets dropped, duplicates and resyncs.

**OBS-8.26** [P6] A second machine streaming into the same group is detected,
counted and reported, and is never stopped.

Measured in the sibling implementation with two Ultimates sending at once: twice
the packet rate, twice the byte rate, two independent 16-bit sequence counters
interleaved, and every packet arriving in order with zero apparent loss from
each sender's point of view. Nothing in the receive path looks wrong. That is
what makes the source-address filter of OBS-8.4 a correctness requirement rather
than a tidiness one.

The sibling implementation responds by asking the uninvited sender to stop,
because on a phone there is exactly one device the user chose. Here the opposite
holds: on a multi-target run the other sender is another target of the same run,
and stopping it would break that recording. So the recorder filters, counts the
foreign source addresses, and names them in the `kind=capture` record. An
operator reading that a third address was streaming has learned something worth
knowing; a recorder that silently stopped it would have caused the next failure.

**OBS-8.27** [P6] When the host cannot keep up, video frames are shed and audio
never is.

The recorder's per-frame cost is unpacking and encoding, and its per-packet cost
is a socket read. Falling behind means the socket buffer overflows, which loses
packets from both streams at once and corrupts frames rather than merely
thinning them. So the recorder sheds work at the one place where shedding is
harmless: it drops decoded video frames before composition.

Two rules:

- **Deterministic decimation.** The output rate of OBS-8.23 is reached with a
  phase accumulator: add the ratio of the output rate to the source rate on each
  source frame and emit when the accumulator crosses one. Reproducible, exact at
  simple ratios, and bounded at every other, where "take one in N" is only exact
  when N divides evenly and a threshold on elapsed time drifts.
- **Audio is never decimated.** Its packets are 4 ms each and dropping one is a
  concealment, not a saving.

Shed frames are counted in the `kind=capture` record. A recording that shed
heavily is a recording made on a host that was too slow, and a reader has to be
able to tell that from a device that was not sending.

Deliberately not built: the adaptive governor the live mirror needs, which
measures audio buffer depth and pipeline latency and continuously retargets the
presentation rate. It exists to protect a listener in real time. A recorder
writing to a file has no listener, no presentation deadline and no latency
budget, so its answer to pressure is a fixed rate and an honest count.

**OBS-8.28** [P6] Each suite run gets a small set of still images, and they are
the artefact most readers will actually look at.

A 30-minute recording answers "what happened" only if someone watches it. A
handful of stills per suite answers "what did this suite see" at a glance, in
the report, with no download and no player:

| Still | When |
|---|---|
| first | the first complete frame after the suite started |
| last | the last complete frame before the suite ended |
| transitions | up to a bounded number of the largest screen changes in between |

A transition is found by the same byte comparison the composition already needs:
consecutive packed frames differ in some number of sampled bytes, and the frames
with the most differing bytes are the transitions. Sampling every second byte is
enough to rank them and halves the cost. A threshold below which a change is not
a transition keeps a blinking cursor out of the list.

The bound is per suite run, so a long suite gets the same number as a short one
and the set stays readable. They are written under the target's `capture/`
directory, named from the suite-run key of OBS-3.6, as PNG for the VIC pane and
as text for the menu pane. The text form is what the report can inline
(OBS-4.7), which is why both are kept.

This is also what a suite that produced no video at all still gets: with
`--no-record-video` the menu stills are the whole set, and they cost nothing
beyond the spool that OBS-8.22 was already writing.

Stills are never stamped (OBS-8.30). They are evidence of what was on a screen,
and a caption drawn over the border is a caption drawn over evidence. Their
timing is in their file name, in the report entry beside them and in the JSONL.

### Layout and stamping

**OBS-8.29** [P6] The two panes go into one file or into two, and the choice is
`--record-layout`:

| Value | Files | Geometry each |
|---|---|---|
| `combined` (default) | `video.mp4` | 768x272, the composition of OBS-8.20 |
| `separate` | `video-screen.mp4` and `video-menu.mp4` | 384x272 |

`combined` is the default because one file is one thing to open, and the two
panes cannot drift apart when they are the same picture. `separate` exists
because a reader who wants to watch one of them full-screen, or diff two runs of
the same suite, is better served by a file that is only that pane.

Under `separate` the two files are aligned frame for frame, and the property is
established by construction rather than by correction afterwards:

- **The frame slot is the unit.** The recorder advances one output slot at the
  output frame rate (OBS-8.8), and every enabled output receives exactly one
  frame for that slot. A pane with nothing new to show repeats its last frame,
  which is what a static screen is anyway, and a pane with no source at all
  emits its placeholder card. No output ever skips a slot.
- **One start time.** Both files carry the same `capture.started`, so the
  timecode arithmetic of OBS-8.11 gives the same offset in both, and the report
  prints one `mm:ss` that is correct for either.
- **One audio track, muxed into both**, from the same concealment timeline
  (OBS-8.25). Two files with independently derived audio would drift; two files
  with the same samples cannot.
- **The stamp of OBS-8.30 is the visible proof.** A reader who seeks both files
  to the same position sees the same timecode drawn on both. That is a check
  anybody can do in a player, without trusting anything in this document.

Only the layout changes. Both files carry the same encoder settings, the same
frame count and the same duration, and dropping a source with
`--no-record-video` or `--no-record-menu` (OBS-8.23) removes that file from a
`separate` run and that pane from a `combined` one.

**OBS-8.30** [P6] Each video frame carries a stamp drawn into the top-left of
its border area: the timecode on the first row and the suite and scenario on the
second.

```
00:12:34.500
prg-context-menu / mount and run
```

The particulars:

- **Drawn at composition time, into the frame buffer, before the encoder sees
  it.** Not an `ffmpeg` `drawtext` filter, not a second pass, and no font
  dependency: the glyphs come from the same character ROM the menu pane uses
  (OBS-8.20). The frames are being built out of packed nibbles anyway, so
  drawing 96 characters into one is free next to the work already being done,
  and there is no re-encode because there was never a first encode to redo.
- **In the border, not over the picture.** The C64 border is 32 pixels left and
  right, 35 top on PAL and 20 top on NTSC. Two rows of 8-pixel glyphs need 16,
  which fits both. The stamp starts at the top-left corner of the frame.
- **Fixed colours, not the border's.** A high-contrast pair chosen once, so the
  stamp stays legible whatever colour the program set the border to, and so two
  runs of the same suite produce byte-identical stamps.
- **48 columns.** 384 pixels of 8-pixel glyphs. The timecode takes 12 of them.
  The suite and scenario are truncated to fit, from the end, with a marker.
- **The timecode is the slot index divided by the frame rate**, formatted
  `HH:MM:SS.mmm`, counting from the first frame of the file including the title
  card. It is therefore exactly the position a player reports for that frame,
  which is what makes it usable for seeking, and exactly the figure the report
  prints for a check (OBS-8.11). Those three agreeing is an acceptance criterion
  rather than an assumption.
- **The suite and scenario come from the JSONL tail** of OBS-8.31, not from a
  channel between the runner and the recorder.
- **`--no-record-stamp` turns it off**, along with the marking of OBS-8.32, for
  a reader who wants the frames untouched. See OBS-8.8 on what that changes
  about pixel exactness.

The stamp does not replace the `.srt` sidecar (OBS-8.12) and is not a
contradiction of it. The sidecar carries the full check name, is searchable
without opening the video, and regenerates without touching the file; the stamp
carries the two things a reader needs while looking at a frame, survives a
screenshot of that frame, and is the alignment proof for OBS-8.29. They cost
nothing to have both.

**OBS-8.31** [P6] The recorder learns what is happening by tailing the JSONL the
runner and the suites are already writing.

It needs the current suite, scenario and check for the stamp of OBS-8.30 and the
marking of OBS-8.32, and it needs them while the run is happening. Every one of
them is already being appended, with a wall-clock `time`, to
`DIR/<slug>/run.jsonl` and `DIR/<slug>/<label>-<suite>.jsonl`. So the recorder
opens those files and reads to the end of each on a low-rate tick, keeping the
last record of each kind.

Three properties this relies on, all of which already hold: the files are
append-only, each record is one complete line written under `O_APPEND`, and a
partial final line means the writer is mid-write rather than that anything is
wrong, so a reader keeps the partial line and retries. This is OBS-1.4 applied
again: no new channel between the processes, and nothing for the runner to know
about the recorder.

It also means a check's identity reaches the stamp only when the check closes,
which is what OBS-8.32 is written around.

The `.srt` sidecar needs none of this. It is generated after the run, from the
same JSONL, where every interval is already known.

**OBS-8.32** [P6] A failure is marked on the frame itself, so that dragging a
timeline finds it.

A run's video is mostly one static screen after another, and a reader scrubbing
30 minutes of it has no way to see where anything went wrong. The subtitle
requires the player to be showing subtitles and requires reading. A colour at
the edge of the frame is visible in a thumbnail, at any scrub speed, without
reading anything.

The rule, and it is deliberately a narrow one:

| State | Marking |
|---|---|
| a check has just failed | a red edge |
| the device is being recovered, or was reported unhealthy | an amber edge |
| anything else, including every passing check | nothing at all |

Restraint is what keeps this a diagnostic rather than a decoration, and each of
these is part of the requirement rather than a matter of taste:

- **The edge is two pixels, on the outermost rows and columns only.** The C64
  border is 32 pixels at the sides and at least 20 at the top, so the marking
  never touches the 320x200 picture. What OBS-8.8 guarantees about the picture
  area is unaffected.
- **A clean run is unmarked.** Nothing is drawn while checks are passing, so the
  marking cannot become wallpaper, and an unmarked video is itself a statement.
- **Two colours, not five.** Failure and device trouble are the two things a
  reader is scanning for. Marking `WARN` and `SKIP` as well would put a colour
  on most of the run.
- **It holds long enough to be found.** The marking covers the failing check's
  remaining time and a minimum dwell of a small number of seconds, so a reader
  dragging a timeline cannot step over it between two frames. The dwell is a
  named constant.
- **It starts when the failure is known**, which per OBS-8.31 is when the check
  closed. The frames it marks are the ones showing the state the check failed
  on, which is what a reader wants to look at. The precise start of the check is
  in the report's `mm:ss` (OBS-8.11) and in the subtitle.

`--no-record-stamp` removes the marking along with the stamp: both are
annotations drawn into the border area by the same code, and a reader who wants
untouched frames wants neither.

**OBS-8.33** [P6] The bottom border carries a progress bar: one segment per
planned suite run, in the order they will run, coloured by what has happened to
each so far.

The C64 border is 32 pixels at the sides, at least 20 at the top and at least 20
at the bottom, and almost no program uses any of it. The top-left holds the
stamp (OBS-8.30) and the outer two pixels hold the failure edge (OBS-8.32). The
bottom border is the remaining space, and a progress bar is what best uses it:
at any frame it answers "how far into the run am I" and "what has gone wrong so
far" without reading a word.

| Segment state | Meaning |
|---|---|
| dark | not run yet |
| neutral, bright | passed |
| red | failed |
| amber | warned, skipped, or the device had to be recovered around it |
| a brighter outline | the suite running at this frame |

Six rules, for the same reason OBS-8.32 has them:

- **Fixed geometry for the whole run.** Segment widths are equal and are
  computed once from the planned sequence in OBS-2.14. Sizing them by duration
  would be more truthful and would make the bar's shape change every suite,
  which is the opposite of glanceable.
- **Full width of the frame, a few pixels high, with a gap above it** so it
  never touches the picture area.
- **It fills left to right and never rewrites history.** A segment's colour
  changes once, when that suite run closes.
- **Segments to the right of the current one are always dark**, because their
  outcome is not known yet. This is a progress bar, not a summary, and the last
  frames of the recording are where the whole picture is.
- **The same four colours as OBS-8.32, plus the neutral.** No new palette.
- **Governed by `--no-record-stamp`**, with the stamp and the edge.

It shares OBS-8.31's JSONL tail for its state, so it costs one more thing to
draw per frame and nothing else.

At two targets and twenty suite runs each, this is what makes a 30-minute file
scannable in ten seconds: drag until a red segment appears, and the stamp on
that frame names the suite.

### How it looks

These recordings get watched, and some of them get shared. A file that is
legible and looks like it belongs to the machine it came from is worth having,
and it costs nothing beyond deciding once instead of per element. What follows
is not decoration for its own sake: every rule below is either about legibility
or about the annotations not looking like a modern tool's furniture bolted onto
a C64 screen.

**OBS-8.35** [P6] The composition has one visual system, and it is the C64's
own. Someone who grew up with this machine should read the result as a C64
screen with its border used, not as a capture tool's overlay.

| Element | Rule |
|---|---|
| Palette | the 16 VIC colours and nothing else, from `c64_rgb` in `tools/api/menu_screen_tool.py` |
| Type | the C64 character ROM at `roms/characters.901225-01.bin`, 8x8, one size |
| Grid | every element aligns to the 8-pixel character grid, at integer positions |
| Chrome | the darkest neutral in the palette, so annotations recede and screens dominate |
| Colour | reserved for state: the failure and warning colours of OBS-8.32, and nothing else coloured |
| Motion | none. No fades, wipes, blinks, animation or easing anywhere |

Why each of them:

- **The palette and the typeface are the machine's.** A modern sans-serif label
  or a colour outside the sixteen is instantly foreign, and a viewer reads it as
  something added rather than as part of the picture. Using the machine's own
  glyphs and colours is also the cheapest option, because both are already in
  the repository for the menu pane.
- **The 8-pixel grid** is what makes pixel work look deliberate. An element at
  an odd offset, or a glyph scaled to a non-integer size, reads as sloppy at any
  resolution and is the single most common way this kind of composition looks
  amateur.
- **Dark chrome, colour only for state.** The screens are the content. Chrome
  that competes with them is both worse to look at and worse to read. It also
  means a failure colour is the only colour on the frame, which is exactly what
  OBS-8.32 needs.
- **No motion.** The material is static text that changes in one frame. Anything
  that moves smoothly is the composition drawing attention to itself, and on a
  scrubbed timeline it is noise. This also keeps every frame reproducible, which
  the tests of OBS-8.15 depend on.

Two pane labels, `SCREEN` and `MENU`, sit in each pane's top border, right
aligned, on the same row as the stamp. A viewer who did not build this has no
other way to know which is which, and on a shared video that is most viewers.

Nothing here is a matter of taste that an implementer resolves at the keyboard:
the palette, the font, the grid, the two colours and the absence of motion are
requirements, and a test asserts that a composed frame uses no colour outside
the sixteen.

**OBS-8.36** [P6] The recording opens with a title card and closes with a
summary card, each held for a few seconds.

The title card names the run: target, product and firmware version (OBS-3.19),
commit and branch (OBS-2.11), date, and the number of suite runs planned. The
summary card names the outcome: the counts from the status line of OBS-3.22 and
the names of the suites that failed.

They are composed frames like any other, drawn with the system of OBS-8.35, so
they cost one frame each to build and the dwell to encode. They are what makes
the file a thing somebody can hand to somebody else: a viewer who opens it knows
within two seconds what they are watching, and a viewer who reaches the end
knows how it went without opening the report.

The cards carry no secret, per OBS-1.8. Beyond that they carry the run's own
identity, which is the point of them.

`--no-record-stamp` removes the cards along with the other annotations, for a
run whose frames must be nothing but the device's. Removing them sets
`capture.lead_in` to zero, which the timecode arithmetic of OBS-8.11 already
accounts for.

### The menu payload, exactly

**OBS-8.37** [P6] `GET /v1/machine:menu_screen` returns exactly 2000 bytes as a
binary attachment, and the shape is fixed in the firmware rather than negotiated:

| Bytes | Plane | Content |
|---|---|---|
| 0 to 999 | character | one byte per cell, 40 columns by 25 rows, row major |
| 1000 to 1999 | colour | one byte per cell, same order |

`UserInterface::ACTIVE_SCREEN_MATRIX_WIDTH` is 40,
`ACTIVE_SCREEN_MATRIX_HEIGHT` is 25, `ACTIVE_SCREEN_MATRIX_CELLS` is 1000 and
`ACTIVE_SCREEN_MATRIX_PLANES` is 2, in
`software/userinterface/userinterface.h`, and
`UserInterface::copy_active_screen_matrix` writes the character plane at the
start of the buffer and the colour plane at `dest + CELLS`.

Five properties the renderer has to honour, each of which is in the firmware or
was measured on hardware:

- **A character byte's bit 7 is reverse video**, and the glyph is bits 0 to 6.
  `ui_backend` masks with `0x7F` and collects the set bits separately as
  `reverse_cells`. This is how the selected row is marked on a machine whose
  colour plane carries no background, so a renderer that ignored bit 7 would
  show a menu with no selection.
- **Text is literal printable ASCII**, and values below 0x20 are firmware UI
  glyphs such as box drawing and icons. `menu_char_to_glyph` holds that mapping.
  These are not PETSCII screen codes, which is what the `readmem($0400)`
  fallback of OBS-5.3 returns, and the two must not share a decode path.
- **A colour byte packs two nibbles, and which is which is machine dependent.**
  `menu_screen_tool.py` exposes the choice as `swap_colour_nibbles` rather than
  assuming, and some machines carry no background nibble at all, which is
  measured and recorded in `ui_backend.find_selected_row_rest`. The renderer
  takes the same option and falls back to a fixed background when there is no
  usable one.
- **404 means no 40x25 UI is available**, not that the firmware is old.
  `copy_active_screen_matrix` walks the live user interfaces, skips any that is
  unavailable or whose screen is not exactly 40 by 25, and the route answers
  `HTTP_NOT_FOUND` with "Menu screen unavailable." when none matched.
- **A Telnet session wider than 40 columns is not returned by this endpoint.**
  `ui_backend.make_backend` lets a suite ask for a larger Telnet screen, and
  such a UI fails the size test above. That is the second reason the right
  pane's source under `telnet` is the tap of OBS-8.22 rather than this call.

### From three sources to one or two files

**OBS-8.38** [P6] The pipeline is fixed, and every boundary in it has one
format. This is the requirement that says how a video stream, a REST payload and
an audio stream become a file somebody can watch.

| Stage | Input | Output |
|---|---|---|
| 1 receive | UDP datagrams, filtered by source (OBS-8.4) | complete VIC frames (OBS-8.24), audio packets on a concealment timeline (OBS-8.25), menu payloads from the spool (OBS-8.22) |
| 2 hold | the newest of each | one current VIC frame, one current menu payload, an audio write cursor |
| 3 slot | the output frame rate (OBS-8.8) | one tick per output frame, the unit of alignment for everything downstream (OBS-8.29) |
| 4 compose | the held sources plus the JSONL tail (OBS-8.31) | one 24-bit RGB canvas per slot, annotated (OBS-8.30, OBS-8.32, OBS-8.33, OBS-8.35) |
| 5 encode | canvases on one pipe, PCM on another | one `ffmpeg` process per output file, muxing both |
| 6 finish | the finished file, the JSONL | chapters copied in (OBS-8.34) and the `.srt` written (OBS-8.12) |

The decisions inside that, and why each is what it is:

- **`ffmpeg` is fed raw, not encoded.** Video enters as `rawvideo` in `rgb24` at
  the composed geometry and the fixed frame rate; audio enters as `s16le`,
  stereo, at the device's own rate (OBS-8.19). Both are exactly what stages 2
  and 4 already have in memory, so nothing is converted twice and nothing is
  encoded twice. There is no intermediate file.
- **Two pipes, two file descriptors.** A single `stdin` cannot carry two
  streams, so the audio pipe is a second descriptor passed to the child and
  named as an input on the command line. This is the one piece of process
  plumbing here that is easy to get wrong and has no alternative.
- **One `ffmpeg` per output file.** Under `--record-layout separate` there are
  two, each fed the pane it wants from the same slot loop and the same audio
  bytes, which is what makes OBS-8.29's alignment true by construction rather
  than by synchronisation.
- **The slot loop never blocks on the encoder.** A write to a full pipe would
  stall the loop, and a stalled loop stops draining the sockets, which loses
  packets from both streams at once. The loop sheds frames instead (OBS-8.27),
  and audio is never shed because its packets are the timeline.
- **Colour conversion happens once, at composition.** The VIC pane is 4-bit
  indices and the menu pane is glyphs plus colour indices; both become RGB in
  stage 4 through the one palette (OBS-8.35). Handing `ffmpeg` an indexed format
  and a palette would save nothing and would put the palette in two places.
- **Chapters are a copy pass, not a re-encode.** Stage 6 rewrites the container
  with `-c copy` and the metadata file, so the frames written in stage 5 are the
  frames in the finished file, which is what keeps OBS-8.8's guarantee intact.
- **Every stage is a function over data.** Receive, hold, compose and the
  concealment timeline take bytes and return bytes; only stage 5 touches a
  process and only stage 6 touches the filesystem twice. That is what makes
  OBS-8.15's device-free tests possible at all.

What the reader gets from this, which is the point: one file per pane or one for
both, each opening on a title card that says what run it is, each playing the
device's audio, each carrying chapters named after the suites, each frame
stamped with its own position and the test that was running, a bar along the
bottom showing how far through the run it is and what has failed so far, and a
red edge on the frames where something went wrong.

**OBS-8.7** [P6] Decoded frames are fed to `ffmpeg` over a pipe rather than
buffered. A gate run is 15 to 30 minutes and the recorder's memory use must not
grow with the run.

**OBS-8.8** [P6] The encode is chosen for what this material actually is, which
is not video in the ordinary sense.

What a run looks like: the Ultimate menu for most of it, a C64 BASIC screen for
some of it, and under 5% anything else. The picture is static for seconds at a
time and then changes completely in one frame. There is no motion to interpolate
and no gradient to dither. What a reader does with the file is step through it,
forwards and backwards, looking at 40-column text.

Five properties follow, in order of how much they matter:

1. **Pixel exact.** The encode is lossless and has no chroma subsampling. A
   lossy encode spends its bits on the transitions and blurs the 8x8 glyphs,
   which destroys the one thing the artefact is for. `4:2:0` alone would smear
   the edge of every character. This is cheap here rather than expensive: the
   source is 16 palette colours on a screen that mostly does not change, which
   is close to the best case a lossless codec ever sees.
   Exactness is a property of the encode, so it holds for whatever the recorder
   composed. With the stamp of OBS-8.30 on, that is every pixel the device sent
   except the two glyph rows in the top-left border; with `--no-record-stamp` it
   is every pixel. The stills of OBS-8.28 are never stamped, so they are exact
   either way.
2. **A keyframe at every transition.** The frame where the screen changes is the
   frame a reader is looking for, and it must be decodable without reference to
   anything before it. `x264`'s scene-cut detection places an IDR frame exactly
   there and costs nothing to leave enabled; disabling it, which a fixed-GOP
   configuration does, is the mistake to avoid.
3. **Bounded backward seeking.** Stepping backwards makes a player decode from
   the previous keyframe, so a keyframe interval of about one second of recorded
   time bounds that work during a static stretch where no scene cut fires.
4. **Constant frame rate.** The timecode arithmetic in OBS-8.11 is a
   subtraction, and it is only correct if a recording's presentation time
   advances linearly with wall-clock time. A variable-rate output would need
   exact per-frame timestamps to preserve that, for no gain a reader can see.
   The rate is a named constant well below 50 Hz, because a static screen
   encoded 50 times a second costs a reader nothing and costs the Pi everything;
   an unchanged frame is a near-free P-frame at any rate.
5. **Seekable without reading the whole file.** The index goes at the front of
   the MP4, so a reader can seek in a partly downloaded artifact.

Native resolution, no upscaling. 384x272 carries every pixel the device sent,
and upscaling it multiplies the encoder's work and the file size by the square
of the factor while adding no information. If a viewer ever needs it larger, the
scale is an integer factor with nearest-neighbour interpolation, never a
fractional one, and it belongs in the playback command in `tests/e2e/README.md`
rather than in the file. `mpv`'s nearest-neighbour scaling is what OBS-8.12
documents.

Every one of these is a named constant or a named flag with the reason in the
comment beside it, per the repository's convention.

**OBS-8.9** [P6] Each pane and the audio track degrade on their own. When video
packets stop arriving the left pane becomes a placeholder card; when the menu is
unreadable the right pane shows one of the states in OBS-8.20; when audio
packets stop the track is concealed and then re-anchored (OBS-8.25). The
recorder carries on in every case.

The placeholder card says why, when the run knows why. The spool of OBS-15.7
carries every stream stop and redirect a suite made, with the suite's name, so a
card can read "the av suite stopped this stream" rather than "the stream is
unavailable". A gap the run cannot explain says that instead, which is itself
the answer: an unexplained gap is a device that went quiet on its own, and that
is question 2 in the Purpose section.

Every gap is recorded with a start and an end per OBS-15.11, so the report can
put it on the timeline beside the suite that caused it.

**OBS-8.10** [P6] `ffmpeg` and `ffprobe` are checked for at startup, along with
the encoders the chosen options need, and their absence is reported then. The
feature is opt-in, so their absence is only an error when recording was asked
for. A build of `ffmpeg` without the video encoder that OBS-8.8's lossless
default needs fails here rather than after 30 minutes of capture. Follows from
OBS-1.2.

**OBS-8.11** [P6] The recorder emits one `kind=capture` record per target,
naming the target, every output file it wrote, the wall-clock time the capture
started, and:

- the frame geometry, the pane layout, whether the stamp is on, and the lead-in
  of OBS-8.36 (OBS-8.17, OBS-8.20, OBS-8.29, OBS-8.30);
- which sources were enabled and the effective value of every option in
  OBS-8.23;
- the counts: frames written, frames padded for a geometry change, video and
  audio re-arms (OBS-8.16), silent audio frames inserted (OBS-8.19), menu
  screens from the tap, menu screens the recorder requested, failed requests
  (OBS-8.21).

Every subtitle interval, every chapter mark and every video timecode follows by
subtraction from the start time and the intervals of OBS-2.6. A check's position
in the file is

```
capture.lead_in + (check.time - check.seconds - capture.started)
```

where `lead_in` is the title card's dwell (OBS-8.36) and is zero when there is
no card. That figure is what the report prints as `mm:ss` beside a failing check
(OBS-4.7), what the burned-in stamp shows (OBS-8.30), and what a player reports
for the same frame, and the three agreeing is an acceptance criterion.

Every `mm:ss` anywhere in this design is a position in the file, not an elapsed
time in the run. The JSONL is where wall-clock time lives; the recording is
where file positions live; `capture.started` and `capture.lead_in` are the two
numbers that convert between them, and they are in the record for that reason.

The arithmetic is correct only because the output is constant rate (OBS-8.8),
which is why that property is a requirement rather than a preference. Depends on
OBS-2.5.

The counts are the recording's own health. A file with thousands of padded
frames or hundreds of re-arms is telling a reader that the run fought the
recorder for the stream, which is worth knowing before drawing conclusions from
what the recording shows.

**OBS-8.12** [P6] Subtitles are a sidecar `.srt`, never a burned-in overlay. The
sidecar regenerates without touching the video, costs no re-encode, and is plain
text, so it can be read and searched without opening the video at all.

Each cue carries the check's identity key (OBS-3.6) and its verdict, in that
order, so `grep` over the `.srt` for a suite name or for `FAIL` returns the
timecodes to seek to. A sidecar whose cues read "running prg-context-menu" would
be readable and not searchable; the identity key makes it both.

`tests/e2e/README.md` documents how to play the result on Linux: the subtitle
track selected explicitly for players that need it, nearest-neighbour scaling so
the text stays sharp when the window is larger than the frame, which pane is
which, and the three ways to find a test in the recording (OBS-8.34).

**OBS-8.34** [P6] The recording carries chapter markers, and they are the
primary way a reader finds anything in it.

Scrubbing a 30-minute file to find one suite is the problem this whole section
exists to make unnecessary, and MP4 chapters solve it outright: `mpv`, VLC and
QuickTime all list chapters and all bind a key to jump between them, so finding
a suite becomes picking it from a list rather than dragging a timeline.

| Chapter | Title |
|---|---|
| each suite run | its identity key and verdict: `overlay/prg-context-menu/1 FAIL` |
| each failing check | its identity key and its label |
| each device recovery | `recovery` |

Three properties:

- **Exact, because chapters are written after the run.** They are generated from
  the finished JSONL, where every interval of OBS-2.6 is known, so a failing
  check's chapter starts at the start of that check rather than when the
  recorder learned about it. This is what the live marking of OBS-8.32 cannot
  do, and the two are complementary: the marking is what a reader sees while
  scrubbing, the chapter is where a reader jumps to.
- **No re-encode.** The chapters go in with one stream-copy pass over the
  finished file, from a metadata file the recorder writes beside it. The frames
  are not touched, so nothing in OBS-8.8 is affected, and the pass costs seconds
  rather than the length of the run.
- **The same titles as everything else.** The identity key means a reader who
  has the report open and a reader who has the player open are looking at the
  same strings.

So there are three ways to find a test in the recording, and
`tests/e2e/README.md` names all three: the chapter list in the player, a `grep`
of the `.srt` for a suite name or for `FAIL`, and the `mm:ss` the report prints
beside every failing check (OBS-8.11). A reader who already has the report needs
none of the others.

The progress bar of OBS-8.33 and the edge of OBS-8.32 answer a different
question, and they answer it in the one place chapters cannot reach: a player's
thumbnail preview, where the frame is all a reader has. The keyframe policy of
OBS-8.8 is what makes those thumbnails sharp and representative, so a reader
dragging along the timeline sees each screen and its border annotations without
committing to a seek.

**OBS-8.14** [P6] For a cartridge target the video is the computer's. A U2 has
no `streams` route and no `U64_ETHSTREAM` hardware: `route_streams.cc` and
`software/io/network/data_streamer.cc` are absent from every U2 makefile, so
`streams:start` sent to the cartridge fails. The computer is the machine with
the VIC, and what it renders is what the cartridge under test is doing, so it is
also the right picture.

`targets.Target` gains `video_host`, returning `self.computer`, beside
`input_host`, which returns the same thing for the same underlying reason: the
C64-side facilities belong to the computer. `host_for` is left alone; it maps
REST paths and the recorder is not making a REST path decision, it is choosing a
machine. Resolves the video half of OQ-5.

A whole-machine target that is not a U64 family device has no `streams` route
either. The recorder reports that once, at startup, and records nothing for that
target, per OBS-1.2.

**OBS-8.15** [P6] Device-free tests cover every piece of logic in this section.
Every edge condition in OBS-8.24 to OBS-8.27 is reachable from synthetic packets
and none of them is reachable reliably from a device, which is the whole reason
they are specified rather than left to the implementation:

- frame unpacking from synthetic packets against a known image;
- a packet delivered out of order, a lost middle packet and a lost last packet,
  each asserted against the frame that results and the counts that go with it;
- the 16-bit wrap of both counters, asserted as one lost packet rather than
  65535;
- a reordered late frame, asserted as not counting a loss twice;
- a packet with a wrong width, line count or bit depth, asserted as ignored;
- the geometry change of OBS-8.17;
- each of the four audio outcomes in OBS-8.25, including that a duplicate does
  not advance the index and that a fill is not zeros;
- packets from a second source address, asserted as excluded from the frame and
  named in the counts;
- the phase-accumulator decimator at several ratios, asserted as reproducible;
- the two-pane composition for each of the four right-pane states and for each
  combination of enabled sources;
- the still selection of OBS-8.28 against a synthetic sequence with known
  transitions, asserted as picking them and not picking a blinking cursor;
- the spool format, including a reader resynchronising after a truncated record;
- subtitle text and interval arithmetic against the JSONL fixture, and
  per-target file naming.

Two tests need a real encoder: one asserts with `ffprobe` that the output has
the expected duration, geometry and stream count, and one asserts that a frame
decoded back out of a lossless encode is identical to the frame that went in,
which is the only way to prove OBS-8.8's first property rather than assert it.

### Acceptance criteria for section 8

- With the recording flag absent, `strace`-level device traffic and the run's
  console output are identical to a run before the recorder existed.
- A suite that asserts on VIC frames passes while a recorder is capturing the
  same multicast group, on both a Linux host and a macOS host (OBS-8.5).
- A run in which `av` and `input` both run produces a recording with no gap
  longer than the re-arm interval, and the `kind=capture` record names how many
  re-arms it took (OBS-8.16).
- A recorder capturing two devices on one group writes each device's frames to
  its own file and neither recording contains a frame from the other
  (OBS-8.4).
- `ffprobe video.mp4` reports a duration within a second of the run's wall
  duration, one video stream at the composed geometry, and one audio stream.
- A frame extracted from `video.mp4` at a known timecode is identical, pixel for
  pixel, to the VIC frame the device sent at that moment, outside the stamped
  rows, and identical everywhere under `--no-record-stamp` (OBS-8.8, OBS-8.30).
- Under `--record-layout separate` the two files have the same duration, the
  same frame count and the same audio, and a frame taken from each at the same
  timecode carries the same stamp (OBS-8.29).
- The stamp on a frame, the position a player reports for it, and the `mm:ss`
  the report prints for the check that was running then, all agree
  (OBS-8.11, OBS-8.30).
- A menu transition during a suite appears in the right pane within one output
  frame of the keystroke that caused it, taken from the tap rather than from a
  request the recorder made (OBS-8.21, OBS-8.22).
- A run under `--mode telnet` produces a right pane holding the Telnet screen,
  from the same spool, with no `menu_screen` request made for it.
- Playing `video.mp4` in `mpv` shows both panes, plays audio, and shows
  subtitles naming suites and checks that match the JSONL for the same
  timecodes.
- The `mm:ss` timecode the report prints for a known failing check seeks to the
  frame the subtitle names for that check.
- A run with `--no-record-audio` produces a file with no audio stream and the
  same video timecodes; a run with `--no-record-menu` produces a 384x272 file;
  a run with all three sources disabled is refused at startup (OBS-8.23).
- A run with a non-default `--record-quality` records that value in the
  `kind=capture` record (OBS-8.11).
- Every suite run in a recorded run has a first still, a last still and at most
  the bounded number of transition stills, and the report shows their text form
  without a download (OBS-8.28, OBS-3.23).
- A recorded run's `kind=capture` record accounts for every packet: written,
  dropped, ignored, concealed or shed, with no unexplained remainder
  (OBS-8.24, OBS-8.25, OBS-8.27).
- `ffprobe` lists one chapter per suite run and one per failing check, each
  titled with its identity key, and jumping to a failing check's chapter in
  `mpv` lands on that check (OBS-8.34).
- `grep FAIL video.srt` returns a timecode for every failing check, and
  `grep <suite name> video.srt` returns that suite's (OBS-8.12).
- A composed frame uses no colour outside the sixteen VIC colours, and every
  annotation begins at a multiple of 8 pixels (OBS-8.35).
- The file opens on a title card naming the target, firmware and commit, and
  ends on a summary card whose counts match the report's status line
  (OBS-8.36, OBS-3.22).
- The right pane renders the selected row correctly on a machine whose colour
  plane carries no background nibble, which is the reverse-video path of
  OBS-8.37.
- A run under `--record-layout separate` produces two files with two `ffmpeg`
  processes fed from one slot loop, and killing one encoder leaves the other's
  timecodes correct (OBS-8.38).
- A gate run with recording on takes no longer than the same run with recording
  off, on the same hardware, or the difference is measured and reported so the
  `--record-menu-min-interval-ms` default can be revisited (OBS-8.21).

---

## 9. Firmware work

Nothing in sections 1 to 8 requires a firmware change. Each item below removes a
named limitation, is optional, lands separately from the harness work, and
carries a code-size cost on targets that are already tight.

**OBS-9.1** [P6, optional] Make an assertion failure reach the collector.
`vAssertCalled` disables interrupts and spins, so the syslog task cannot forward
the assertion text (OBS-7.15). A fix flushes the syslog buffer synchronously
from inside `vAssertCalled` rather than relying on the task. Firmware work in
`software/system/assert.c` and `software/network/syslog.cc`.

**OBS-9.2** [P6, optional] Expose `Syslog::failed_sends`, so a run can tell a
silently lossy link from a quiet device (OBS-7.11). Needs a route or a periodic
log line to carry the counter.

**OBS-9.3** [P6, optional] Move the `custom_outbyte` assignment earlier in
`ultimate_main`, so the product version banner and the init-function output
reach the syslog (OBS-7.13). The constraint is that `Syslog::init` needs
`networkConfig.cfg`, which `InitFunction::executeAll()` establishes, so this is
not a free reordering.

### Acceptance criteria for section 9

- Each item, if built, is proven red then green on real hardware, one defect at
  a time, per the repository's rule in `AGENTS.md`.
- Each item is built for every target (`./build-tool -s u64 u64ii u2 u2pl`)
  before it is reported complete, not only the target it is deployed to.

---

## 10. Explicitly out of scope

Each of these is a decision already taken. The reason is there so an implementer
does not re-propose it.

- **A UI-driven log export (`Save Debug Log` in the `Developer` menu, or
  `Ctrl+L` in the file browser).** The log route is syslog. Driving the UI to
  collect a log is exactly the state the runner works to keep the device out of,
  the text carries no timestamps, and `StreamTextLog::charout` overwrites its
  own beginning without marking the seam. It is also a route the suites own: the
  four `cfg-*` suites drive it to read the loader diagnostics, and OBS-15.9
  keeps observability off it.
- **A Prometheus `/metrics` endpoint.** Firmware work with a size cost, and
  neither question in the Purpose section needs it. The existing design in
  `doc/research/prometheus-metrics/research.md` should land for its own reasons
  if it lands at all. If it does, the health sweep reads it instead of
  `machine:heap`.
- **The `debug` stream (`/v1/streams/debug`).** A raw FPGA bus trace at 6510 bus
  rate, U64-family only, and `streams:start` stops it before starting video
  because the two cannot run together.
- **`GET /v1/machine:measure`.** A cartridge bus timing measurement returning a
  64 KB VCD waveform and allocating 64 KB per call. A hardware bring-up tool.
- **FreeRTOS per-task CPU and stack statistics.** `configGENERATE_RUN_TIME_STATS`
  and `INCLUDE_uxTaskGetStackHighWaterMark` are both `0` in
  `software/FreeRTOS/Source/FreeRTOSConfig.h`, and reaching them over the
  network is firmware work with a size cost.
- **The port-64 developer commands (`SOCKET_CMD_READFLASH`,
  `SOCKET_CMD_DEBUG_REG`).** Explicitly marked developer-only in
  `software/network/socket_dma.cc`, and neither answers either question.
- **Capturing the whole configuration tree.** Several hundred items that change
  rarely; the ones that matter are the ones a suite changed, and suites already
  restore those.
- **Per-request HTTP timing in `tests/lib/rest.py`.** Tens of thousands of
  records per run, answering at high resolution what the health sweep already
  answers at the granularity anyone acts on.
- **Per-check screen captures.** See OBS-5.8.
- **A recording of the VIC stream alone.** It cannot show the menu under
  `overlay` or `telnet`, which is two thirds of the gate, so the recording has
  two panes and the menu is the second. See the opening of section 8.
- **An HTML report.** Section 3 records the decision and its reasoning. The
  entry point is `index.md`. HTML would win only a local browser and would cost
  either a second generator for the job summary or a hand-maintained second
  document.
- **Base64 images inside the report or the job summary.** See OBS-4.7. They do
  not render on GitHub and they do not survive a paste into a model at a
  sensible token cost. The character screen is text and needs no image.
- **GitHub Pages or any hosted report.** Publishing every gate run's video and
  device log to a branch, one commit per run, with retention becoming git
  history. A larger commitment than occasional inspection justifies.
- **A dashboard or a time-series database.** A run produces a few hundred
  kilobytes of JSONL. The artifact is the storage, and a text file is the
  viewer.
- **Making any of this a gate.** The heap series is not an assertion (OBS-6.7).
  Extending what the health sweep fails on is a separate decision with a higher
  bar, because a failing sweep is what fires the recovery command.
- **Recording `report.detail` as a `kind=detail` JSONL record.** A smaller change
  than the console capture and a strictly weaker one: it loses tracebacks,
  stderr, the runner's own health lines, and everything a suite killed by a
  signal had already printed. See OBS-2.13.
- **A stream port per target.** Three suites start the device's video stream at
  the fixed group and port themselves, so a recorder listening elsewhere loses
  the stream the moment one of them runs. Source-address filtering is what the
  existing `av_stream.AvStreamCapture` does and what the recorder does. See
  OBS-8.4.
- **A pty so suites keep their live progress lines under `-j`.** Machinery for a
  terminal affordance a captured run does not have, and it would put escape
  bytes in the saved log. See OBS-2.13.
- **An adaptive rate governor for the recorder.** The sibling `c64commander`
  implementation has one because it mirrors a device live and must protect a
  listener in real time. A recorder writing a file has no listener, no
  presentation deadline and no latency budget. Its answer to a slow host is a
  fixed rate and an honest count. See OBS-8.27.
- **Clock mapping, drift fitting or latency measurement between the device and
  the host.** Neither stream carries a source timestamp, so there is no common
  clock and no true source-to-file latency to measure. Every timestamp here is
  the host's, per OBS-1.5, and A/V alignment comes from the two packet counters
  rather than from a fit. See OBS-8.19.
- **Stopping a machine that is streaming into the same group.** The sibling
  implementation does, because a phone has one selected device. Here the second
  sender is usually another target of the same run. The recorder filters and
  reports it. See OBS-8.26.

### Withdrawn requirements

These numbers are defined nowhere in this document and are not reused. Each is
listed so an implementer who finds a gap in the numbering, or a reference in
another copy, does not build what it named.

| Number | What it was | Why it is gone |
|---|---|---|
| OBS-2.9 | The recording output directory and the `-j` directory are the same | Subsumed by the single layout in OBS-2.10 |
| OBS-3.2 | The report is one self-contained HTML file with no external requests | The report is Markdown; the no-external-references rule is now OBS-3.15 |
| OBS-3.3 | Video and screen captures stay as sibling files referenced by `<img>` and `<video>` | No HTML report; sibling files are named by relative path in OBS-3.14 |
| OBS-3.8 | All charts are SVG generated by the report generator | A 30 to 50 row table (OBS-3.7) answers the same question with no chart code, and is readable by a script |
| OBS-3.9 | Free heap on the same chart panel as listener latency | A column in the OBS-3.7 table, not a separate requirement |
| OBS-3.12 | Clicking a failing check seeks the page's `<video>` element | Needs HTML and JavaScript to do what the `mm:ss` timecode in OBS-4.7 does with no code |
| OBS-4.2 | The job summary's own content and its order | The summary is a copy of the report (OBS-4.1), so its content is defined once, in OBS-3.14 |
| OBS-4.6 | The summary writer is device-free and tested against the fixture | One generator, so OBS-3.13 covers it |
| OBS-5.6 | The `readmem($0400, 1000)` fallback is documented as best-effort | Folded into the bullet in OBS-5.3 that creates the fallback |
| OBS-6.6 | `health.Check.render` must render a byte count for the heap check | Folded into OBS-6.1, which adds the check |
| OBS-8.13 | No audio in the recording | Reversed. The recording carries audio, specified in OBS-8.19. The number stays withdrawn because it named the opposite decision, and reusing it for its own reversal would make every earlier reference to it read backwards |

---

## 11. Open questions and flagged uncertainty

These are unresolved. An implementer who reaches one stops and reports rather
than guessing.

**OQ-1** PARTLY RESOLVED. No CI job runs the gate today:
`.github/workflows/build.yml` builds firmware on a self-hosted runner and
uploads update packages, and nothing in it runs `./run-tests`. OBS-4.9 and
OBS-4.10 specify the workflow that does, including why it is a workflow of its
own and why its runner carries a label of its own.

What is still an operator decision, and cannot be settled in this repository:
who registers a self-hosted runner with the `e2e` label on the machine that is
on the device LAN, which set of targets that machine can reach, and what
`--recover-command` means there. Until a runner carries that label the workflow
is valid and never runs, which is the correct failure: the file lands, the
devices decide when it does anything.

**OQ-2** RESOLVED. Two artifacts with different retentions, per OBS-4.10: the
report, JSONL, logs and captures for 90 days, the recordings for 7. The size of
a real encode of a mostly static 40-column screen is still unmeasured, and the
first run with the recorder enabled is what measures it. If it comes to more
than the runner's disk or the artifact limit can take, the frame-rate cap in
OBS-8.8 is the value to change.

**OQ-3** May the CI devices carry the syslog setting permanently? It is a
boot-time decision (OBS-7.3), so it cannot be a per-run action, and the value
has to be the collector host's fixed IP address. OBS-7.16 establishes that
leaving it set is harmless to a run when no collector is listening. Referenced
by OBS-7.1.

**OQ-4** `actions/upload-artifact` has had an `archive: false` mode since
February 2026 that uploads without zipping, and files uploaded that way can be
downloaded individually. Its documentation says that mode uploads a single file
only and takes the artifact name from the file name, so it does not produce a
browsable directory. This was not tested, and this repository is on v7. If the
restriction is lifted, OBS-4.4 and hop 3 of OBS-4.8 are worth revisiting.

**OQ-5** RESOLVED. A cartridge target's video comes from the computer and its
log from both machines. `targets.Target` gains two properties beside
`input_host`: `video_host`, the computer (OBS-8.14), and `log_hosts`, both
machines with the device first (OBS-7.18). One target's video coming from one
machine and its logs from both is not an inconsistency to remove; it is what the
hardware is. The report labels each artefact with the machine it came from.

**OQ-6** RESOLVED. Passing checks are in the report, in the detail part of the
document, and only failing checks are in the summary part. See OBS-3.5 and
OBS-3.14.

**OQ-7** RESOLVED. Adding `target` to every record is acceptable. The contract
in `tests/lib/README.md` is a table of fields per kind, every documented `jq`
recipe selects fields by name, and a reader that ignores an unknown field is
unaffected. The condition is OBS-2.5: the field is documented in the same commit
that adds it, so the table stays the contract rather than becoming a stale copy
of one. Referenced by OBS-2.3. The same reasoning covers the run-identity fields
of OBS-2.11.

**OQ-8** Should `-j` be on by default? None of this exists without it and it is
currently opt-in. Turning it on in CI is enough; turning it on for everybody is
a separate question about writing files nobody asked for. OBS-2.13 raises the
stakes slightly: with `-j` on by default, every local run would lose its live
progress lines, so the answer stays no until somebody asks for it.

**OQ-10** Does the runner host have a fixed address on the device LAN? OBS-7.1
requires the devices to carry the collector's literal IPv4 address, and
`Syslog::init` will not take a host name. A runner host on DHCP breaks every
device's syslog setting the day its lease changes, silently, with the log
turning empty and nothing saying why. OBS-7.4 catches a device that lost the
setting; it does not catch a collector that moved. This is an operator decision
about the runner host and it blocks P5 rather than anything earlier.

**OQ-9** What does GitHub's step-summary renderer actually permit? OBS-4.7
assumes the sanitiser rejects a `data:` image source, which is the basis for
ruling out inline images. The same question covers whether a `mermaid` fenced
block renders in a step summary. Neither was tested, because testing needs a job
that actually runs, which is the operator half of OQ-1. The test is cheap once a
runner carries the `e2e` label: write one summary containing a one-pixel `data:`
image and one mermaid block, and look at the build page. If `data:` images do
render, OBS-4.7's first bullet changes and the size budget in OBS-4.3 becomes
the binding constraint instead. Nothing else here depends on the answer, so it
does not block the workflow landing.

---

## 12. Build order

Each step is useful on its own and each is a precondition for the next being
worth having. The `P` tag on a requirement is authoritative if this table ever
disagrees with it.

| Priority | Deliverable | Requirements |
|---|---|---|
| P1 | Markdown report over data that already exists, plus the layout, correlation and capture fixes it needs | OBS-1.1 to OBS-1.5, OBS-1.7, OBS-1.8, OBS-2.1 to OBS-2.3, OBS-2.5 to OBS-2.8, OBS-2.10 to OBS-2.15, OBS-3.1, OBS-3.4 to OBS-3.7, OBS-3.13 to OBS-3.15, OBS-3.17 to OBS-3.22, OBS-3.24 to OBS-3.27, OBS-3.29, OBS-14.1 to OBS-14.5, OBS-15.1 to OBS-15.5, OBS-15.11 |
| P2 | GitHub job summary, the workflow, the navigation path, and the run comparison | OBS-2.4, OBS-3.28, OBS-4.1, OBS-4.3 to OBS-4.5, OBS-4.7 to OBS-4.10 |
| P3 | Failure captures in the runner | OBS-1.6, OBS-3.10, OBS-5.1 to OBS-5.5, OBS-5.7, OBS-5.8 |
| P4 | Heap in the health sweep | OBS-6.1 to OBS-6.5, OBS-6.7, OBS-6.8 |
| P5 | Syslog collector | OBS-3.11, OBS-7.1 to OBS-7.18, OBS-15.8 to OBS-15.10 |
| P6 | Recorder, and optionally the PDF and the firmware items | OBS-3.16, OBS-3.23, OBS-8.1 to OBS-8.12, OBS-8.14 to OBS-8.38, OBS-15.6, OBS-15.7, OBS-9.1 to OBS-9.3 |

P6 is itself ordered, because it is the largest step in this document and its
pieces are independently useful:

0. **The stream library** (OBS-15.6, OBS-15.7), before anything that opens a
   socket. It absorbs `vic_video` and `av_stream` without changing any suite,
   and every later piece is a caller of it. Building the recorder first would
   leave a third implementation of the same wire format to fold back in.
1. **The menu-screen tap and its spool** (OBS-8.22). No encoder, no multicast,
   no `ffmpeg`, no device call that was not already being made. It answers "what
   was on screen when this check failed" on its own.
2. **The menu stills and their place in the report** (OBS-8.28 for the menu
   half, OBS-3.23). Built on the spool alone, and the first thing here that a
   reader sees without downloading anything.
3. **Video reception**: the socket, the assembler and its edge conditions
   (OBS-8.5, OBS-8.6, OBS-8.24, OBS-8.26), proven by the stills before any
   encoder exists.
4. **The encoder, the composition and the annotations** (OBS-8.7, OBS-8.8,
   OBS-8.17, OBS-8.20, OBS-8.30 to OBS-8.33, OBS-8.35). The visual system of
   OBS-8.35 is decided before the first annotation is drawn, not retrofitted
   across four of them.
5. **Navigation** (OBS-8.34), which is what makes the file usable, and the cards
   (OBS-8.36), which change the timecode arithmetic and so come before anything
   asserts on it.
6. **Audio** (OBS-8.19, OBS-8.25).
7. **The layout option and the configuration surface** (OBS-8.29, OBS-8.23),
   once there is something to configure, and the shedding rule (OBS-8.27) once
   there is a measurement of whether the host keeps up.

Steps 1 and 2 are worth having even if steps 3 to 6 are never built, which is
the same shape as P1 and P2 being worth having on their own.

Section 14 is at P1 because the `ping` defect in OBS-14.2 makes a macOS host
report every device as unhealthy, and because OBS-14.4 is the difference between
the tests running at all on the CI host and not.

P1 and P2 are worth doing even if nothing else here is ever built. P5 comes
after P3 and P4 because it needs a standing configuration change on the CI
devices and a reboot, a coordination cost the earlier steps do not have. P6 is
last because it is the largest piece, the only one with an external binary
dependency, and the only one that changes shared harness code before it can
coexist with the suites that assert on the same frames. Everything at P6 is
optional.

---

## 13. Where each piece lands

| Path | What lands there | Requirements |
|---|---|---|
| `tools/e2e_report.py` | The report generator. It consumes a finished run rather than being part of one, so it is neither a registered suite nor shared suite support, which is what `tests/` holds. `tools/app_space.py` is the house pattern for a Python tool here. | 3 |
| `tests/lib/report_render_test.py` | The generator's tests, as a registered suite in the `SUITES` tuple in `run-tests`, importing the generator by path. | OBS-3.13 |
| `tests/lib/fixtures/e2e-run/` | The checked-in `-j` tree the tests run against, and the expected `index.md` beside it. | OBS-3.13, OBS-3.21 |
| `run-tests` | The `E2E_TARGET` export, the slug-directory fix, the run-identity fields, the parent `run` record, the console capture, the failure capture, and the flags for the collector and the recorder. | OBS-2.1, OBS-2.2, OBS-2.4, OBS-2.11 to OBS-2.13, section 5, OBS-7.17, OBS-8.1 |
| `tests/lib/report.py` | The `target` field and the new record kinds and fields. | OBS-2.3, OBS-2.11, OBS-2.12 |
| `tests/lib/README.md` | The record-shape table and the `jq` recipes. | OBS-2.5 |
| `tests/lib/health.py` | The ninth check and its rendering. | OBS-6.1, OBS-6.5, OBS-14.2 |
| `tests/lib/api.py` | The `machine:heap` call, under the rule in `tests/lib/README.md`. | OBS-6.3 |
| `tests/lib/targets.py` | `video_host` and `log_hosts`, beside `input_host`. | OBS-7.18, OBS-8.14 |
| a new module in `tests/e2e/lib/` | The stream library: constants, sockets, source filtering, the frame assembler, the audio timeline, the arming discipline. | OBS-15.6, OBS-15.7 |
| `tests/e2e/lib/vic_video.py` | Becomes a caller of the library, keeping its public names. | OBS-8.5, OBS-8.6 |
| `tests/e2e/lib/av_stream.py` | The same, plus the arming discipline in its `start` and `stop`. | OBS-8.5, OBS-8.19, OBS-15.7 |
| `tests/e2e/lib/ui_backend.py` | The menu-screen tap, in `RestBackend._menu_screen_body` and the Telnet backend's screen accessor. | OBS-8.22 |
| `tools/api/menu_screen_tool.py` | Nothing changes; its `menu_char_to_glyph`, `split_colour_byte` and `c64_rgb` are shared by the right pane. | OBS-8.20 |
| `roms/characters.901225-01.bin` | Nothing changes; the right pane draws its glyphs from it. | OBS-8.20 |
| new modules the runner starts | The syslog collector and the recorder. | sections 7 and 8 |
| a reader beside the collector, in `tests/lib/` | Follows `DIR/<slug>/syslog.txt` for a suite that needs device log lines, so nothing else binds the syslog port. | OBS-15.8 |
| `src/lib/streams/` in the `c64commander` repository | Nothing here changes. A working implementation of these two wire formats, in a separate project, to read before writing the recorder. Not a dependency, not vendored, and not required to be present. | OBS-8.24 to OBS-8.27 |
| `.github/workflows/e2e.yml` | The job, the summary copy step and the uploads. | OBS-4.1, OBS-4.9, OBS-4.10 |
| `tests/e2e/README.md` | The playback instructions, the three ways to find a test in a recording, and the `pandoc` command. A link to this document, added in the first commit of P1. | OBS-3.16, OBS-8.12, OBS-8.34 |
| `tests/README.md` | The host requirements, including `ffmpeg`. | OBS-14.4 |
| `tests/e2e/doc/observability-spec.md` | This document. It is committed with the first priority step and is not edited afterwards to match the implementation; see the status note at the top. | - |

There is no second generator for the job summary: OBS-4.1 is a copy step in the
CI job. The `pandoc` command of OBS-3.16 is documented rather than wrapped in a
script, because it is one command and wrapping it would create the second
maintained path OBS-1.7 exists to prevent.

The five soak suites of OBS-6.3 change only where they read the heap from.

Nothing here changes the firmware or the monitor, except the optional items in
section 9, which land separately.

---

## 14. Host platforms

The host is the machine that runs `./run-tests`, which is not the device and is
not the machine that builds the firmware. Everything in this document runs
there.

**OBS-14.1** [P1] Linux on aarch64 is the supported host, and the CI host is a
Raspberry Pi 500. macOS is best-effort: it has to work for a developer driving a
device from a laptop, and it is not what the gate runs on. Nothing here may
require a package that has no aarch64 build.

Two consequences worth stating rather than discovering. The Pi has no hardware
video encoder on this path, which is why OBS-8.8 caps the frame rate and picks a
fast preset. And the Pi is on the device LAN, which is what makes it the
collector host in OBS-7.1 and the runner in OBS-4.9; the build runner in
`.github/workflows/build.yml` is a different machine and is not.

**OBS-14.2** [P1] `health._ping` is not portable, and on macOS it reports every
device as unreachable. It runs `ping -c 1 -W <seconds>`. On Linux `-W` is a
timeout in seconds, which is what the code intends. On macOS and the BSDs `-W`
is a wait in milliseconds, so `-W 2` waits two milliseconds and the ping fails
before any device could answer.

This is not cosmetic. `ping` is the first check in the sweep, a failed sweep
makes `Health.ok` false, and `Device.ensure_healthy` answers an unhealthy device
by running the operator's recovery command, which reboots or reflashes hardware.
A developer running the gate from a laptop would have every device recovered
before every suite. The fix is to pass the platform's own unit, decided from
`sys.platform` once, with the two meanings named in the comment.

This is the one item in this document that changes a verdict: on macOS a sweep
that failed starts passing. OBS-1.1 forbids an observability component from
changing a verdict, and this is not one; it is a defect in an existing check
that the macOS host requirement exposes. On Linux nothing about the sweep
changes, which is what the red/green proof has to show: the same argument list
before and after on Linux, a different one on macOS.

**OBS-14.3** [P1] Multicast reception differs. Two sockets bound to one UDP port
need `SO_REUSEADDR` on Linux and `SO_REUSEPORT` on the BSD stack macOS uses, and
`socket.SO_REUSEPORT` is not defined on every platform Python builds for. Both
are set, the second behind a `hasattr` guard. See OBS-8.5. Joining a group with
`INADDR_ANY` takes the interface from the routing table, which is correct on a
host with one LAN interface and is what both the runner host and a developer
laptop have; a host with more would need an explicit interface, and that is out
of scope until somebody has one.

**OBS-14.4** [P1] The host needs `ffmpeg` and `ffprobe` for the recorder only,
and nothing else new. `tests/requirements.txt` already lists `Pillow`, which the
recorder reuses through `vic_video`. `ffmpeg` is not a Python package, so it
belongs in the host-requirements table in `tests/README.md` beside `tesseract`,
which is already documented that way, and the entry names the video encoder the
lossless default of OBS-8.8 needs, because a build without it fails at
OBS-8.10's startup check rather than at install time. `pandoc` and `weasyprint`
are for OBS-3.16 alone, are never installed in CI, and are named in the same
table as optional.

**OBS-14.5** [P1] The floor is the Python version already required by the
harness. Nothing here raises it, and nothing here adds a Python dependency: the
report generator, the collector and the console capture are standard library
only. The recorder is the one exception, through `Pillow` and the `ffmpeg`
binary, and it is optional.

### Acceptance criteria for section 14

- A device-free test of `health._ping` asserts the argument list built for
  `sys.platform == "linux"` and for `sys.platform == "darwin"` differ in the
  value passed to `-W`, and that each is the platform's own unit for the same
  wall-clock timeout.
- On a macOS host, one health sweep against a reachable device reports `ping` as
  passing.
- The report generator and the syslog collector import nothing outside the
  standard library, asserted by a test that inspects their imports the way
  `tests/lib/check_transport_usage.py` inspects calls.

---

## 15. Shared device resources

A device has one video stream, one audio stream, one log, one menu screen and
about four HTTP connections. The suites want all of them and so does everything
in this document. Sections 1 to 14 say what each observability component does;
this section says what happens when a suite wants the same thing at the same
time, and it is the section that decides whether any of this can be turned on
during a real gate run.

The starting position is that a suite is allowed to do anything to the device,
including things that break observability on purpose. A suite that turns the
network off, loads a configuration file that rewrites nineteen stores, stops a
stream, reboots the machine or wedges the UI is doing its job. None of that is a
fault to be defended against, and none of it may be prevented.

### The two rules

**OBS-15.1** [P1] The suite wins. Where a suite and an observability component
want the same device resource, the component yields, records that it yielded,
and resumes when the resource is free. There is no case in this document where a
suite waits for an observability component, is refused a resource by one, or is
made to work differently because one is running.

This is OBS-1.1 pointed the other way. OBS-1.1 says a component may not change a
verdict; this says a component may not change how a suite reaches it either.
Together they mean a run with every observability feature on and a run with none
of them differ in what they record and in nothing else.

**OBS-15.2** [P1] Sabotage is an input, not an error. Every component here
treats a suite breaking the thing it depends on as ordinary and expected: it
degrades, keeps its own counters, records what it saw, and never fails the run,
never blocks, never retries in a tight loop, and never repairs anything.

Repair is the part worth being explicit about. A component that noticed the
syslog setting had changed and put it back would destroy the evidence of the
test that changed it, and would do it silently. A component that noticed the
video stream had been redirected and took it back mid-check would break the
suite that redirected it. The correct action in both cases is to write down what
happened and carry on, and the report shows it (OBS-3.26, OBS-3.27).

**OBS-15.3** [P1] No component here removes a capability a suite has today. The
list of what suites currently do to the device is in the register below, and
each row states what the observability side does instead of taking it.

### The register of contended resources

**OBS-15.4** [P1] These are the contended resources. Each row is a claim about
the current tree that an implementer can check, and the last column is the rule.

| Resource | Suites that use it | What contention does | Rule |
|---|---|---|---|
| Video and audio multicast streams | `av/stream_test.py`, `api/input_test.py`, `monitor/monitor_test.py` | `streams:start` sets one destination, last writer wins; `streams:stop` stops it for everyone | The recorder never stops a stream and re-arms only after silence (OBS-8.16, OBS-15.7) |
| HTTP connection slots, about four | every suite | a slot taken at the wrong moment pushes a suite's request into a retry | The recorder's own `menu_screen` request happens only when the tap is silent, one at a time, with a configurable floor (OBS-8.21) |
| The menu screen | every UI suite, through `ui_backend` | none: it is a read | Observability reads the suites' copy rather than making its own request (OBS-8.22) |
| The device debug log | the four `cfg-*` suites, through `Developer` / `Save Debug Log` | `Clear Debug Log` resets it | Observability never touches it (OBS-15.9) |
| The syslog forwarding buffer | none today | a suite could point the device elsewhere or disable it | The collector records the silence; nothing is repaired (OBS-15.10) |
| `Network Settings` | `cfg-*` suites can rewrite it as collateral | the syslog target can be lost | Checked and reported at both ends of the run, never corrected (OBS-15.10) |
| Device reachability | any suite may reboot, reset or power off | everything observable stops | Every component treats it as a gap with a start and an end (OBS-15.11) |
| The UI object stack | every UI suite, and the runner's own gate | a capture taken at the wrong moment sees the gate's screen | The failure capture runs before the gate (OBS-5.2) |

### One reader per resource

**OBS-15.5** [P1] Each device resource has one place that reads it, and
everything else consumes that reader's output. This is the same rule OBS-1.7
applies to the report, applied to device traffic instead of to documents: two
readers of one resource means two chances to disagree about it and two costs to
the device.

It already has three instances in this document, and naming the rule is what
makes the fourth obvious rather than inventive:

| Resource | The one reader | Everyone else |
|---|---|---|
| The REST API | `tests/lib/api.py` | calls it; a new endpoint goes there, per `tests/lib/README.md` |
| The menu screen | `ui_backend`, which the suites drive | the recorder taps what it read (OBS-8.22) |
| The device log | the syslog collector | a suite that needs log lines reads the collector's file (OBS-15.8) |
| The video and audio streams | the stream library of OBS-15.6 | `vic_video`, `av_stream` and the recorder are all callers |

**OBS-15.6** [P6] One stream library owns everything about the two multicast
streams, and `tests/e2e/lib/vic_video.py`, `tests/e2e/lib/av_stream.py` and the
recorder become its callers.

This is a consolidation rather than a new component. The duplication is already
there and it has already diverged:

| Concern | `vic_video.VicStreamCapture` | `av_stream.AvStreamCapture` |
|---|---|---|
| Group and port constants | its own pair | its own four |
| `SO_REUSEADDR` | not set | set |
| Source-address filter | none | `socket.getaddrinfo`, applied per packet |
| Frame assembly | concatenates payloads in arrival order | does not assemble frames |
| Arming | leaves it to the suite | starts video and audio, stops both |

Section 8 requires changing both files anyway (OBS-8.5), requires frame assembly
neither of them has (OBS-8.24), and requires an audio timeline neither of them
has (OBS-8.25). Writing those into a third place would leave three
implementations of the same wire format, which is the state
`tests/lib/check_transport_usage.py` exists because the HTTP client reached.

What the library owns:

- the group, port and wire constants of OBS-8.6, in one place;
- socket creation, including the options of OBS-8.5 and OBS-14.3;
- source-address filtering, from `AvStreamCapture`'s existing rule (OBS-8.4);
- the frame assembler of OBS-8.24 and the audio timeline of OBS-8.25, as
  functions over bytes so both are testable without a socket;
- arming, under the discipline of OBS-15.7.

What it does not own: what a caller does with a frame. A suite asserting that
two frames differ, and a recorder composing a pane, share the decode and share
nothing else.

It goes in `tests/e2e/lib/`, beside the two modules it absorbs, because it is
shared suite support rather than a device endpoint. The existing public names in
`vic_video` and `av_stream` keep working, so no suite changes in the same commit
that introduces the library.

The library gives no shared socket, and does not try to. Multicast is delivered
to every subscriber that joined, which is exactly why OBS-8.5 is a requirement,
so a suite and the recorder each having their own socket is correct and costs
the device nothing. What is shared is the code and the arming.

**OBS-15.7** [P6] Arming discipline: leave the streams as you found them, and
never stop one you did not start.

Four rules, in the library so that every caller gets them:

- **Ask before arming.** A caller that needs a stream running at the standard
  address, and finds packets already arriving from its device there, issues no
  `streams:start` at all. That is the "do not request it twice" rule applied to
  the one thing about a stream that is not free.
- **Stop only what you started.** `AvStreamCapture.stop` stops both streams
  today, whether or not it started them. A caller that found a stream already
  running leaves it running.
- **Restore a destination you changed.** A caller that points a stream somewhere
  else, which is a legitimate thing for a suite to do, puts it back to where it
  found it, in the same `finally` shape `temp_settings.py` uses for a
  configuration item.
- **Publish what you did.** Every arm, stop and redirect is recorded through the
  same spool the menu tap uses (OBS-8.22), with its wall-clock time and the
  caller's suite name. This is what lets the recorder say "the `av` suite took
  the stream here" instead of showing an unexplained placeholder card, and what
  lets the report attribute a gap in the recording to a suite rather than to the
  device.

None of this constrains a suite. A suite that wants the stream gets it
immediately, and the recorder is the only party that ever waits.

**OBS-15.8** [P5] Exactly one process binds the syslog port, and a suite that
needs device log lines reads the collector's file rather than opening a socket
of its own.

This is not a preference. The syslog datagrams are unicast, and on Linux two
sockets bound to one UDP port with `SO_REUSEPORT` do not both receive each
datagram: the kernel picks one of them per datagram. A suite that opened a
second socket would silently receive roughly half the lines and the collector
the other half, with nothing in either looking wrong. Multicast, where OBS-8.5
deliberately arranges for two sockets to share a port, behaves the opposite way,
and the difference is worth knowing before somebody copies the pattern across.

So the collector's `DIR/<slug>/syslog.txt` (OBS-7.8) is the interface. It is
append-only, line-oriented and timestamped, so a suite that needs to assert on a
device log line follows it the way `tail -f` does, through a small reader in the
shared library, and gets the same lines the report will show. When no collector
is running the reader says so and the suite decides whether that is a skip or a
failure; that decision belongs to the suite, never to the reader.

**OBS-15.9** [P5] The device debug log belongs to the suites, and nothing in
this document reads, writes or clears it.

`software/application/ultimate/ultimate.cc` keeps two independent sinks.
`outbyte_log_syslog` writes every character to `textLog`, a 96 KB
`StreamTextLog`, and to `syslog`, whose forwarding buffer is 16 KB.
`ConfigIO::S_reset_log` clears `textLog` alone and `ConfigIO::S_save_log` writes
`textLog` to a file; the syslog buffer is untouched by both, and the syslog
overflow of OBS-7.11 discards the syslog buffer alone.

That independence is what makes the two uses safe together, and it is why the
`cfg-*` suites can keep doing what they do: `cfg_single_group_test.load_fixture`
invokes `Developer` / `Clear Debug Log`, loads a configuration file, invokes
`Save Debug Log`, and `loading_stores` retrieves the saved file over FTP to read
which stores the loader effectuated. A collector that cleared the debug log to
get a clean slate would break that suite and would be the exact behaviour
OBS-15.1 forbids.

Section 10 rules the UI-driven log export out as a route for observability, for
reasons that stand. This requirement adds the other half: it is a route the
suites own, and observability stays off it.

**OBS-15.10** [P5] The syslog configuration is checked at both ends of a run and
corrected at neither.

OBS-7.4 has the runner read `Network Settings` / `Log to Syslog Server` at the
start and warn when it does not name the collector. The same read happens at the
end of the run, and a value that changed during the run is reported with both
values.

The reason is specific and measured. `cfg_partial_effectuate_test.py` records
that a configuration file naming one store had nineteen stores in its loader
diagnostics on a C64 Ultimate 1.2.0, so loading a partial `.cfg` can write
stores the file never mentioned. `Network Settings` is one of them, and
`Log to Syslog Server` is in it. A `cfg-*` suite can therefore lose the setting
as collateral damage, and because `Syslog::init` runs once from `ultimate_main`
(OBS-7.3) the loss is invisible until the device next reboots, which is usually
the next run. A run that quietly produced no log, for a reason set by the run
before it, is the worst version of this to debug.

Reporting it at the end is what makes it visible to the run that caused it. Not
correcting it is what keeps OBS-15.2 true: the suite that changed the setting
may have been testing exactly that, and a component that put it back would have
deleted the finding.

**OBS-15.11** [P1] Every component records the gaps it experienced, with a start
and an end, and the report shows them on the timeline.

A device that stopped answering, a stream that went quiet, a log that stopped
arriving and a menu that could not be read are all the same shape of event: a
resource was unavailable from time A to time B. Recorded that way they are
evidence, and the timeline of OBS-3.26 puts them beside the suite that was
running, which is almost always the explanation.

Recorded any other way they are noise: a component that logged a line per failed
attempt would fill the run with them, and one that logged nothing would leave a
reader wondering whether the file was empty because the device was quiet or
because the collector never started.

A gap that is still open when the run ends is recorded with no end, and the
report says so rather than inventing one.

### Acceptance criteria for section 15

- A run with every observability feature enabled and a run with none of them
  produce the same suite verdicts, the same exit status, and the same sequence
  of device requests from the suites themselves.
- A suite that stops the video stream mid-run leaves the recorder with a gap
  that the report attributes to that suite by name, and the recorder re-arms
  after it without the suite failing (OBS-15.7).
- A suite that starts the video stream while the recorder is running issues no
  second `streams:start`, and the recorder does not stop it afterwards.
- `cfg-single-group` passes unchanged with the collector running, and the
  collector's output is unaffected by `Clear Debug Log` (OBS-15.9).
- A run in which the syslog setting is changed by a suite reports both values at
  the end and leaves the setting as the suite left it (OBS-15.10).
- A device-free test asserts that a second reader of the collector's file sees
  every line the collector wrote, in order (OBS-15.8).
- A run against a device that is taken off the network mid-run finishes, and the
  report shows one gap per affected component with the same start time.
- A device-free test drives the arming discipline against a stub `StreamsApi`
  and asserts no `streams:stop` is sent for a stream the caller did not start.
