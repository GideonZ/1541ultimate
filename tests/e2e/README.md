# Hardware end-to-end tests

These tests exercise a deployed Ultimate device through its public interfaces
and verify the result on real firmware and hardware. A suite may cross REST,
FTP, Telnet, the on-device UI, C64 memory, mounted media, or physical-device
services in one scenario.

They are deliberately separate from host unit tests. Unit tests live beside
their production modules, such as `software/api/tests/` and
`software/filemanager/tests/`; hardware E2E tests live here and run against a
reachable device.

## Structure

Top-level folders name the primary firmware subsystem under test, not the
protocol used to drive it:

| Folder | Primary production owner | Coverage |
|---|---|---|
| `api/` | `software/api/` | REST contracts for input, menu screen, memory, and PRG runners |
| `filemanager/` | `software/filemanager/`, `software/userinterface/` | Browser actions, change notification, and managed `/Temp` lifecycle |
| `filesystem/` | `software/filesystem/` | Filesystem implementations, including the remote FTP filesystem |
| `io/` | `software/io/` | Device-facing I/O subsystems; nested by production package (`c64/`, `command_interface/`, `printer/`) |
| `monitor/` | `software/monitor/` | Machine-code monitor behavior over the normal Telnet UI |
| `network/` | `software/network/` | Network service and connection lifecycle |

Assets and narrowly scoped helpers stay beside the suite that owns them.
Larger subsystem-specific instructions may use a local README, as `monitor/`
and `io/printer/` do.

## First run

You need a current firmware build deployed to a supported device, with its
REST API reachable and the device otherwise idle. Most suites accept the
device through `-H`/`--host` and an optional REST/FTP password through
`-p`/`--password`. Individual suites may need additional host tools or device
configuration; check their local README or source before selecting a manual
suite.

### Device configuration

The suites expect a device in its default configuration with the Commodore
ROMs installed: `kernal.901227-03.bin`, `basic.901226-01.bin` and
`characters.901225-01.bin` under `C64 and Cartridge Settings`. Several suites
rely on the C64 reaching the BASIC prompt with KERNAL interrupts running.

Deviations from the default that the suites tolerate:

| Setting | Tolerated because |
|---|---|
| `U64 Specific Settings / System Mode` | No suite asserts on the video standard. Validated on NTSC. |
| `User Interface Settings / Auto Save Config` | Suites change settings over REST, not through the config menu, so they never reach the exit path that raises the save acknowledgement. Validated with `Ask`. |
| `User Interface Settings / Interface Type` | Suites that care set `Freeze` themselves and restore the original. |
| `Command Interface`, `RAM Expansion Unit`, `Printer Settings` | `uci-targets` and `printer` enable what they need and restore it on exit. |

`freeze-menu` additionally needs an FPGA core carrying the SID decoder fix for
issue #733. On an older core it stalls the whole Nios and needs a JTAG
recovery, or a full power removal if `nios2-download` can no longer pause the
CPU.

The repository-root runner is the supported entry point:

```sh
./run-e2e-tests --list
./run-e2e-tests -H <host> -p <password>
./run-e2e-tests -H <host> -s <suite>
```

Use `./run-e2e-tests --help` for the current options and suite selectors. Use
the runner's `-s` option for normal isolation so selection, arguments, status
reporting, and logs remain consistent. Direct harness invocation is reserved
for developing a suite or using specialized options documented in a subsystem
README; consult that script's own `--help` instead of copying its options here.

Each suite resets the machine in its own setup so it starts clean however it is
invoked. Do not reset between scenarios that intentionally share observers or
state. After all selected suites, the runner defensively releases injected
input, closes any active menu UI, and performs one final reset; a failure there
fails the run. These boundaries do not replace a suite's responsibility to
restore the settings and fixtures it owns.

Suites marked `manual` are excluded from the default sweep because they need
an operator decision, elevated host privileges, a long run, or can exercise a
known unsafe device condition. Read the runner comment and suite documentation
before selecting one. `--all` is not a routine smoke-test option.

Preserve combined stdout and stderr for hardware runs. When piping through
`tee`, keep the runner's failure status:

```sh
stamp=$(date +%Y%m%d-%H%M%S)
set -o pipefail
./run-e2e-tests -H <host> 2>&1 | tee "run-e2e-tests-$stamp.log"
```

## Logging rules

A run is one log written by many processes, so every suite reports through the
shared library rather than formatting its own lines: `tests/e2e/lib/report.py`
for Python, `tests/e2e/lib/report.sh` for shell. `run-e2e-tests` uses the shell
one too. Do not hand-roll a check counter, a verdict word, an indent or a colour
code; if something is missing, add it to the library.

**One check, one line.** A check prints `[NN] <label> ... <verdict>` and nothing
else. `check(label)` in Python and `check_start`/`check_ok` in shell own that
line. Two things could otherwise split it, and the library handles both:

- Checks nest. Helpers such as `open_menu` are themselves checks and are also
  called from inside scenario checks, so only the outermost one prints. A nested
  check produces no output and no number, and its failure still propagates with
  its own message.
- A check body may call `detail`. Those lines are held back until the check has
  printed its verdict, so narrating mid-check cannot push the verdict onto the
  next line.

**The verdict vocabulary is closed:** `OK`, `FAIL`, `WARN`, `SKIP`. Never
`PASS`, `SUCCESS`, `VERIFIED`, `WARNING` or a bracketed form such as `[OK]`.
Extra information goes in parentheses after the verdict: `OK (20 rows)`,
`FAIL (HTTP 500)`. A suite that builds a results table uses the same four words
in its verdict column.

**Colour is the library's job.** Green `OK`, red `FAIL`, yellow `WARN` and
`SKIP`, blue headings. Only the verdict word is coloured, never the label.
Setting `NO_COLOR` turns colour off for the whole run, harness included, so a
captured log looks like what was on screen.

Headings have two levels, so a reader can see structure without reading text. The
runner draws a `banner` for each suite: a title between two rules, which is the
one heavy marker in the log and marks where a new suite starts. A suite draws a
`section` for each scenario inside itself, which is a single blue line.

**Every result carries its own elapsed time**, formatted by `format_duration`
with fewer decimals as the number grows: `0.020s`, `1.002s`, `23.5s`, `264s`. At
a second the milliseconds separate a round trip from a redraw; at a minute they
are noise.

**A scenario reports its own verdict.** `section` opens one; the library closes
it when the next scenario, the next banner or the suite's closing line arrives,
and prints the worst verdict any of its checks produced together with the check
count and elapsed time. A heading that grouped no checks prints nothing, because
a verdict on nothing is noise.

```

============================================================
SUITE  assembly64
============================================================

--- a real query is sent to the Assembly 64 service
[03] open the form and enter the first field ... OK (2.100s)
[04] the typed term lands in the Name field ... OK (0.412s)
[05] the service answers and the results match ... OK (12.031s)
--- OK (3 checks, 14.6s)

assembly64_test: OK (16 checks, 35.2s)
```

| Purpose | Python | Shell | Output |
|---|---|---|---|
| A numbered check | `check(label)` or `check_start` | `check_start` | `[07] label ... OK` |
| An unnumbered step, for the harness's own gates | `step_start` | `step_start` | `label ... OK` |
| A verdict | `check_ok` / `check_fail` / `check_warn` / `check_skip` | same names | `OK (3 rows)` |
| A continuation line under a check | `detail` | `detail` | five-space indent |
| A group heading inside a suite | `section` | `section` | blank line, blue title |
| A top-level heading, for the runner | `banner` | `banner` | blank line, blue title, rule |
| A warning belonging to no check | `warn` | `warn` | `WARN <message>` |
| A suite's closing line | `suite_ok` / `suite_fail` / `suite_skip` / `suite_warn` | same names | `input_test: OK (48 checks)` |
| A live progress line | `progress` / `progress_done` | n/a | terminal only |

Three further rules:

- **Never clear the terminal.** A suite that runs `clear`, or emits an erase
  sequence, destroys the output of every suite before it.
- **Everything goes to stdout, flushed.** Sending results to stderr lets them
  arrive out of order relative to stdout when the run is piped, which is how a
  verdict ends up under the wrong check.
- **Rewriting a line is for terminals only.** `progress` overwrites the current
  line on a TTY and prints nothing when the output is captured, so a carriage
  return never lands in a log file.

### Structured results

`run-e2e-tests -j <path>` writes the same run as JSONL, one object per line, for
a reader that is not a person. The file is truncated at the start of the run, so
it always describes exactly one run. The runner passes the path to each suite in
`E2E_JSONL` and the suite's own name in `E2E_SUITE`, so both libraries append to
one file; records are short and written with O_APPEND, so lines from concurrent
suites do not interleave.

Every record carries `kind`, `suite` and `time`. The rest depends on the kind:

| `kind` | Fields |
|---|---|
| `check` | `index`, `label`, `verdict`, `extra`, `seconds`, `scenario` |
| `scenario` | `title`, `verdict`, `checks`, `seconds` |
| `suite` | `name`, `verdict`, `note`, `checks`, `seconds` |
| `warning` | `message` |
| `run` | `verdict`, `suites`, `passed`, `failed`, `skipped`, `dirty`, `seconds` |

```sh
./run-e2e-tests -H u64 -j run.jsonl
jq -r 'select(.kind=="check" and .verdict!="OK") | "\(.suite) \(.label) \(.verdict)"' run.jsonl
jq -r 'select(.kind=="check") | [.seconds, .suite, .label] | @tsv' run.jsonl | sort -rn | head
```

## Adding or changing a suite

Use these conventions so the tree can grow without inventing a new layout for
each feature:

1. Put the suite under the folder for the production subsystem that owns the
   behavior. Mirror a meaningful production package boundary where one exists:
   a REST-driven printer test belongs in `io/printer/`, matching
   `software/io/printer/`. Add a new top-level folder only for a real
   production subsystem that does not fit an existing one.
2. Use lowercase snake case for directories and files. Executable suites end
   in `_test.py` or `_test.sh`; put qualifiers before `_test`
   (`feature_perf_test.py`). Helpers and assets use descriptive snake-case
   names without `_test`. Keep registered suite scripts executable.
3. Give the runner a stable kebab-case selector and register every executable
   suite in `run-e2e-tests`. New suites are automatic unless there is a
   concrete reason they require operator opt-in; document any `manual` reason
   next to the runner entry.
4. Keep the default scenario deterministic and bounded. Assert externally
   visible outcomes rather than private implementation timing, and print
   enough numbered context to identify the exact failing operation. Report
   through `tests/e2e/lib/report.py` or `report.sh`; see the logging rules
   above.
5. Return non-zero for every failed assertion, setup failure, lost device, or
   incomplete cleanup. Do not turn firmware failures into skips or passes.
   Retries must represent an explicit protocol allowance, remain bounded, and
   preserve the original failure in diagnostics.
6. Capture and restore settings, release injected input, close sessions, and
   remove only fixtures created by the suite. Cleanup belongs in a `finally`
   path where the language permits it.
7. Reuse existing harness code for shared protocol decoding or screen models
   instead of copying it. Keep support code local until more than one
   subsystem has a proven need for the same abstraction.
8. State supported targets and unusual dependencies in the suite docstring or
   its subsystem README. Keep this file structural; do not copy per-suite CLI
   help into it.

Before submitting a structural or test-only change:

```sh
python3 -m py_compile $(find tests/e2e -name '*.py' -type f)
find tests/e2e -name '*.sh' -type f -exec bash -n {} +
bash -n run-e2e-tests
./run-e2e-tests --list
```

For behavior changes, deploy the affected firmware and run the narrowest
relevant selector first, followed by the default automatic sweep. Treat a
hardware E2E failure as a regression to diagnose, not as a reason to relax the
test.
