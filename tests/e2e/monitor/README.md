# Monitor Validation

This directory contains the real-device machine monitor validation harness.

## Service

The firmware exposes the machine monitor from the root browser: press `Ctrl+O` (or
`CBM+O`) with the on-device menu open, and leave it the same way, or with
`RUN/STOP`. Two paths reach the same `UserInterface` object that draws it:

- **REST/Overlay** (the harness default): `tests/e2e/lib/ui_backend.py`'s
  `RestBackend` opens the on-device menu, switches `Interface Type` to
  `Overlay on HDMI` for the duration (restored on close), injects `Ctrl+O` via
  `machine:input`, and reads the rendered screen via `machine:menu_screen` --
  one HTTP round trip per screen, no protocol to parse.
- **Telnet**: the standard remote-menu session on port `23`. `TelnetBackend`
  parses the VT100 stream into the same `40x25` screen model. Kept for the
  handful of checks that are about the Telnet transport itself (a concurrent
  poll-mode connection, and Telnet's own per-keystroke output volume) --
  everything else runs through REST/Overlay, which is markedly faster since
  it skips Telnet's per-byte stream and its fixed post-keystroke quiet-window
  wait.

Both transports render the same UI content from the same row downward (the
firmware draws the monitor into whichever `UserInterface` screen object owns
the session), but they are not pixel-identical: the on-device Overlay/Freeze
screen is the full 25-row physical screen, while the Telnet remote session
only ever fills 24 of those rows, so a REST capture can show one extra memory
row at the bottom of a box. Assertions in `monitor_test.py` locate content by
searching rather than assuming a fixed row count, so the same check logic
holds against either transport.

`tests/e2e/lib/ui_backend_smoke_test.py` (suite `ui-backend-smoke`) validates
the shared facade itself -- Telnet, REST/Freeze, REST/Overlay -- independently
of this suite, so a broken facade fails there with a clear cause instead of as
confusing failures scattered across every suite built on it.

## Run

```bash
./run-tests -H u64 -s machine-code-monitor
./run-tests -H u64 --manual -s machine-code-monitor-debug
```

Before a merge, run the monitor suite and the debugger regression gate
together. That is the pair that has to be green:

```bash
./run-tests -H u64 --manual -s machine-code-monitor -s machine-code-monitor-regression
./run-tests -H u2@c64u --manual -s machine-code-monitor -s machine-code-monitor-regression
```

This suite takes its defaults from the same environment variables as every
other one; see [tests/README.md](../../README.md). `U64_TELNET_PORT` and
`U64_REST_HOST` matter here because the Telnet mode drives port 23 while the
REST calls can go to a second address on a dual-NIC device.

## What It Checks

- KERNAL memory visibility at `$E000`
- REST `v1/machine:readmem` consistency with monitor KERNAL bytes
- Disassembly byte-width formatting
- Screen stability after scrolling away and back
- ASCII row width
- Immediate inline edit visibility and commit
- Cursor vs scroll behavior in ASCII view
- CPU bank status changes and RAM-visible writes at `$A000`
- Combined CPU/VIC status line format
- Repeated finite `G 0810` execution using `LDA #$NN / STA $2000 / BRK`
- `G 0810` preserving visible VIC state while a finite `INC $D021 / BRK` program runs
- Bookmark jumping restoring memory-view width `16`
- Binary width cycling through `1 -> 2 -> 3 -> 3S -> 4` and bookmark restoration of width `4`

The harness parses the VT100 telnet stream into a deterministic `40x25` screen buffer and compares the captured output against the expected snapshot fragments in `snapshots/expected_snapshots.json`.

`machine-code-monitor-debug` is manual. It uses the shared Telnet backend and REST fixture
for the step, breakpoint, banked-memory and execution-handoff regressions. Its
natural-exit liveness checks need the C64 screen to be unowned, so it is the
intentional Telnet-only monitor suite; run it explicitly with
`./run-tests -H u64 --manual -s machine-code-monitor-debug`. The exhaustive
`machine-code-monitor-matrix` suite is manual: it repeats each
memory/UI lane and writes a diagnostic ledger, so run it explicitly with
`./run-tests -H u64 --manual -s machine-code-monitor-matrix`.

## The Regression Gate

`machine-code-monitor-regression` is the suite to run before a merge. It owns no
test logic: it selects lanes out of the two suites above and drives them through
their own runners. The selection is 14 cell runs instead of the matrix's 45 and
11 of the debug suite's 21 groups. All five memory modes are run -- plain RAM,
RAM under ROM, visible ROM, and both boundary traversals (`ram -> rom -> ram`
and `ram -> ram-under-rom -> ram -> rom -> ram`), because each was hard-won and
none is inferable from another's result. What the selection cuts is how many
*transports* a mode is repeated on, and it cuts on two grounds only: the places
where the firmware genuinely takes a different code path depending on which UI
owns the machine, so one transport's result says nothing about another's, and
the intersections that have actually failed in the recorded run history. About
60 minutes on a U64 against a little over two hours for all three suites.

Every selected lane carries the reason it is there and the evidence behind that
reason. Print them, with what the gate deliberately does not cover, without
touching a device:

```bash
python3 tests/e2e/monitor/monitor_debug_regression_test.py \
    --host u64 --rest-host u64 --list-plan
```

`--skip-cells` and `--skip-groups` run one half of the gate on its own, for
iterating on a single failure. Split `u2@c64u` sessions get a smaller plan --
one memory mode, one UI, the groups whose checks are reachable, and the two
`--focus` scopes that only exist for a split session -- because a U2+L refuses
visible-ROM breakpoints and has one local UI rather than two.

[tests/e2e/doc/machine-code-monitor-regression.md](../doc/machine-code-monitor-regression.md)
derives the selection: which firmware paths branch on the transport, which
cells the 36 recorded matrix runs have seen fail and on which repetition, what
each lane costs, and the harness confounds that have repeatedly been misread as
debugger defects.

The gate adds exactly one check of its own, because nothing else asserted it: it
reads the KERNAL and BASIC ROM heads before it touches the debugger and again
when it has finished, so a run that leaves a displaced byte in the volatile ROM
image is named by the run that caused it rather than by the next suite to notice.

## Matrix Coverage

`monitor_debug_matrix_test.py` runs one cell per combination of memory mode and
UI, repeated `--reps` times. `--memory` and `--ui` narrow that to a rectangle;
`--cells` replaces it with a named set of intersections, which is how the
regression gate above asks for the lanes it wants:

```bash
--cells "rom:telnet,rom:freeze:2,ram-under-rom:freeze:2"
```

Each term is `MEMORY:UI` or `MEMORY:UI:REPETITIONS`, and a term naming an
unknown mode or UI is a usage error before the device is touched. The cell set
is recorded in the cross-run history, so a selected run is not mistaken for a
full one.

The axes are:

- **UI**: `telnet`, `freeze`, `overlay`
- **Memory**: `ram`, `ram-under-rom`, `rom`, plus the boundary-traversal modes
  `ram-rom-ram` and `ram-rur-rom-ram`
- **Flow, per cell**: Step Over, Step Into, Step Out, Continue To Cursor,
  Continue To Breakpoint, Continue, Reset

Two call-shape stresses run inside each cell, because they fail differently:

- **Nesting**: a chain of `--required-step-into-depth` subroutines (32 by
  default; 8 for split U2+L/C64U sessions), each calling the next. Every Step
  Into pushes another frame, and the
  return address on the stack is checked at each level, so this exercises depth.
- **Straight calls**: a block of 32 consecutive `JSR` instructions to the same
  helper, stepped with Step Over. The stack pointer returns to the same value
  after every step, so this is what repeatedly arms, parks, resumes and disarms
  from an identical state. Expected PC, SP and A are exact at every position, so
  a leaked breakpoint slot or a park/resume that drifts the stack shows up here.
  Evidence lands in each cell's `straight-call-evidence.json`. Split U2+L/C64U
  sessions default to one repetition and eight straight calls; explicit options
  retain a longer run.

Steps are cross-checked against two independent oracles: the in-tree 6502
interpreter (`mcm6502.py`) and, where installed, VICE over its binary monitor.

## Run History

Each run appends to a cross-run ledger so progress is visible without opening
individual artifact directories:

```
<root>/HISTORY.md          every run as a table, newest first, plus the
                           failures seen across runs
<root>/history.jsonl       one JSON record per run
<root>/<run_id>/run.md     one run in prose
<root>/<run_id>/run.json   the same record, machine readable
```

`run_id` is `<UTC start>-<short commit>[-dirty]`, so the folder name identifies
when a run happened and what tree it ran against. Each record carries the git
commit, branch and uncommitted files, start and end time, duration, per-status
cell counts, the opcode-gate result, the depths reached, and one row per failing
cell with its classification and failing operation.

The root defaults to `$MCM_RUN_LEDGER`, else
`doc/research/machine-code-monitor/matrix-runs`; use `--run-ledger DIR` to point
it elsewhere or `--no-run-ledger` to skip recording.
