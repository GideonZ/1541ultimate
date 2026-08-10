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

## Matrix Coverage

`monitor_debug_matrix_test.py` runs one cell per combination of memory mode and
UI, repeated `--reps` times:

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
