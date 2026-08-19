# Monitor Validation

The real-device validation harness for the machine-code monitor
(`software/monitor/`). One suite, `machine-code-monitor`, driven through the
shared UI facade in [`tests/e2e/lib/ui_backend.py`](../lib/ui_backend.py).

## How the monitor is reached

Open it with `C=+O` (`Ctrl+O` over Telnet) while the file browser is up, or
through `F5` -> `Developer` -> `Machine Code Monitor` from any menu screen.
The shortcut is a case in `TreeBrowser::handle_key`, so the file browser
answers it and the other menu screens do not. `C=+O`, `RUN/STOP` and
the top-left `←` key all leave it; see
[`doc/machine_code_monitor.md`](../../../doc/machine_code_monitor.md) for the
full Back hierarchy this suite verifies.

Two transports reach the same `UserInterface` object that draws it:

- **REST** (`--mode overlay`, the default, and `--mode freeze`): `RestBackend`
  injects keys with `machine:input` and reads the rendered screen with
  `machine:menu_screen`: one HTTP round trip per screen, with no protocol to
  parse. It switches `Interface Type` for the session and restores it on
  close, on a device that has that setting.
- **Telnet** (`--mode telnet`): the standard remote-menu session on port 23,
  parsed from its VT100 stream. Two checks are about that transport itself, a
  concurrent poll-mode connection and its per-keystroke output volume, and
  only run here. Everything else also runs over REST, which is markedly
  faster.

The two are not pixel-identical. The Telnet session is 60x24
(`Screen_VT100::get_size_x`), while the physical Overlay/Freeze screen is
40x25, so the monitor's right-hand header flags land in different columns and
a REST capture can show one extra content row at the bottom of a box.
Assertions locate content by searching rather than by row number, so the same
check logic holds against either.

`tests/e2e/lib/ui_backend_smoke_test.py` (suite `ui-backend-smoke`) validates
the facade itself before any suite built on it runs.

## Targets

```bash
./run-tests -H u64 -s machine-code-monitor
./run-tests -H u64 --manual -s machine-code-monitor-debug
```

A bare target is a whole machine. `cartridge@computer` is a cartridge under
test in the computer it is plugged into; see
[`tests/lib/targets.py`](../../lib/targets.py). The split matters here more
than anywhere else in the tree:

| What | Where it goes for `u2@c64u` |
| --- | --- |
| The monitor screen, `menu_button`, the machine reset | the cartridge |
| Keyboard input (`machine:input`) | the computer |
| The file picker's filesystem | the cartridge |
| The A/V multicast stream the video checks read | the computer |
| Memory verification under `--mode freeze` | the cartridge, whose freezer owns it |
| Memory verification otherwise | the computer's live view |

An Ultimate II answers `machine:input` with HTTP 501, which is why the keys go
to the computer and reach the cartridge over the expansion port. It also has
no `Interface Type` setting, so `--mode overlay` and `--mode freeze` drive the
same firmware path there and differ only in where memory is read from.

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
