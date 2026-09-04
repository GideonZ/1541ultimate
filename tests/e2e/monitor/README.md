# Monitor Validation

The real-device validation harness for the machine-code monitor
(`software/monitor/`), driven through the shared UI facade in
[`tests/e2e/lib/ui_backend.py`](../lib/ui_backend.py).

`machine-code-monitor` checks the monitor itself, and is what the rest of this
file describes. `machine-code-monitor-debug`, `machine-code-monitor-matrix` and
`machine-code-monitor-regression` check Debug mode; see
[Debugger suites](#debugger-suites). `machine-code-monitor-harness`
(`monitor_harness_test.py`) checks the debugger suites' own host-side contract,
with no device, and runs before them.

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
./run-tests u64 -s machine-code-monitor
./run-tests u2@c64u -s machine-code-monitor -m telnet
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

Keys that reach a cartridge that way arrive as taps on the computer's keyboard
matrix, which the cartridge polls. Two taps of the same key in a row can be read
as one key held down, so `RestBackend` splits a batch at every repeated event.
[tests/e2e/doc/key-injection-rate.md](../doc/key-injection-rate.md) measures the
loss and describes the split.

## What it checks

Monitor behaviour:

- KERNAL memory at `$E000` matching `machine:readmem`, and disassembly
  formatting
- view switching, ASCII row width, cursor versus scroll, and screen stability
  across paging
- inline edit in every editable view, including both hex nibbles and the
  screen-charset mappings
- CPU-bank cycling and the combined CPU/VIC status line, or the VIC-only
  footer on a target without monitor-selected CPU banking
- bookmarks: recall, set, label edit, and width restoration for memory and
  binary views
- follow and return navigation, assembly entry and its mnemonic validation
- the number popup: base conversion, arithmetic, and division by zero
- `G` execution: a finite loop, repeated runs updating a RAM sentinel, and
  preservation of visible VIC state
- save and load round trips to a top-level file and to a file inside a
  freshly created D64

Interaction contract:

- Back removes one layer at a time, from `RUN/STOP` and from the real `←` key,
  through help, the number expression, popups, edit mode and finally the
  monitor
- help opens with `?` and with the mapped help key, and closes on `?`,
  `RUN/STOP` and `←` without leaving the monitor
- `←` stays edit data in ASCII and Screen editing, and on the ASCII and Screen
  rows of the number popup
- every structured command prompt refuses a character no such command could
  contain, with no change to the field, and accepts the next valid one
- `C=+O` opens the monitor from the file browser, and is ignored by the task
  menu, the settings screens and a browser context menu, each of which is
  still open afterwards; inside the monitor the same key leaves it, and a
  bare `X` does not
- the Assembly view's source tag is three characters at one fixed column, so
  the column does not move when the bank changes
- `A` opens the Assembly view, and `D` on top of it opens Debug mode with the
  `Dbg` header flag set
- the monitor can be left by each of its three exits and reopened at the same
  view and address
- `Z` stops the C64 and releases it, with the KERNAL jiffy clock at `$00A2` as
  the oracle, or says the freezer is not reachable and stays. The run says
  which of the two happened, because they are different behaviour and both are
  worth holding; the stop-and-release path needs `--mode freeze`
- Hunt keeps the case of a quoted needle; Load accepts both `PRG,0,AUTO` and
  the all-empty `,,`
- `Transfer` given a fourth field moves the absolute operands that point inside
  the copied range, leaves a KERNAL call and a zero-page load alone, and
  reports the number it moved

Reliability sweeps:

- every character of a command argument reaches the monitor: each argument is
  typed once and the field is read back and compared in full before the prompt
  is left, so a missing character, a duplicate and a transposition each fail
- one-, two- and three-byte instructions each commit whole, with the device's
  own `machine:readmem` read back after every commit, so an instruction that
  wrote its opcode and not its operand fails at that commit

Every check that proves a monitor write reached memory separates a monitor
defect from one underneath it, through the one helper
`assert_monitor_write_landed`. When a write does not land, the device is asked
to make the same write at the same address through `machine:writemem`, which
reaches memory through the same `C64::dma_transfer_frozen` with the machine
stopped. If the device cannot manage it either, the loss belongs to that shared
path and is reported in the run.

If the device does manage it, that alone is not evidence about which path lost
the write. `C64::dma_transfer_frozen` loses a write occasionally, so a retry
through any path will usually succeed. Measured on `u2@c64u` over 60 writes at
six addresses, one monitor hex edit and one `machine:writemem` per address per
round, the monitor lost none of 50 and the device lost one of 50, once the ten
attempts at `$BFFF` are set aside, where BASIC ROM is banked over the address
and neither path can write at all. So the address is set to something else
again and the monitor is asked to redo its own write. Only a second failure is
the monitor's write path, and that is when the check fails. A first failure
that the retry places is reported as the shared path's intermittent.

An instruction that landed as its opcode without its operand fails on the first
attempt whatever any retry does, because the monitor writes an instruction as
one block and a block cannot land in part.

Every write that did not land on its first attempt is counted for the whole run
and listed at the end of it, with its address and why it was not attributed to
the monitor. A run that passes while the shared path lost writes says how many
and where, rather than only saying that each one individually was not the
monitor's fault.

One address is worth knowing about before reading a loss there.  Under the
freezer's banking, BASIC ROM sits over `$A000-$BFFF`, so a write anywhere in
that range reaches nothing and the read returns ROM. Neither the monitor nor
`machine:writemem` can write there, and that is not a lost write. The boundary
sweep does not include such an address; a measurement that does will see it
fail every time on both paths.

The checks that go through it are the two sweeps above, the two hex-edit
persistence checks, the Assembly-view edit, the left-arrow-as-data edit and
both save and load round trips.

Both sweeps run a small number of rounds in the gate. `MONITOR_STRESS_ROUNDS`
raises that count for a deliberate stress run and changes nothing about what a
single transaction asserts.

Nothing in this suite re-sends a key to make a check pass. A key press is sent
once and the screen is then re-read until it shows the result or a deadline
expires, so a lost keystroke is reported rather than covered by a second copy.
There are two deliberate exceptions, and neither is about the behaviour a check
is testing:

- A key that cycles through states, such as `o` for the CPU bank, is pressed
  repeatedly because each press is a further intended step rather than a repeat
  of the same one. The press count is bounded by the length of the cycle, so a
  press that is lost still fails the check.
- Typing an argument into a command prompt in order to reach an address is
  preparation for whatever is being checked there. `MonitorSession` reads the
  field back in full and, for those uses, may leave the prompt and type the
  same text again on its untouched template, at most twice. It reports in the
  run when it had to, so a device losing keystrokes is visible rather than
  absorbed. The check whose subject is the input path itself,
  `every character of a command argument reaches the monitor`, allows no
  retype at all.

The lexical space of what each prompt accepts is covered exhaustively by the
host tests in `software/test/monitor/`, so the hardware gate spends its time on
one representative case per prompt instead.

Screen fragments the checks compare against live in
[`snapshots/expected_snapshots.json`](snapshots/expected_snapshots.json).

## Skips

Some checks do not apply to every target, and say so rather than passing
quietly:

- monitor-selected CPU banking is a U64 capability; a target without it
  reports `supports_cpu_banking() == false` and those checks skip
- the VIC-only footer check is the other way round, and runs only where CPU
  banking is absent
- under `--mode freeze` on a U2 the cartridge banks its own RAM over
  `$1000-$3FFF`, so checks whose fixtures live there skip; `$0400` and
  `$C000` upwards are unaffected
- the poll-mode and dropdown-flood checks are Telnet-only by construction
- `G` preserving visible VIC state is only comparable over Telnet, where the
  on-device UI never touches the C64's display

The runner verifies the documented UI state after this suite like any other,
which for a cartridge target means backing out through the computer's keyboard
and reading the result from the cartridge.

## Debugger suites

Debug mode has its own suites, because it drives the real 6510 rather than only
reading memory. All three are registered `manual` for what they cost in hardware
time, so name them explicitly.

```bash
./run-tests u64 --manual -s machine-code-monitor-regression
./run-tests u2@c64u --manual -s machine-code-monitor-regression
```

### `machine-code-monitor-debug`

21 semantic groups, about 95 checks, over Telnet. Its liveness checks read the
C64's own screen and jiffy clock while the machine is meant to be running, so an
Overlay or Freeze UI holding the machine would invalidate them. It is `manual`
because of what it costs, about 40 minutes on an Ultimate 64, most of it spent
running programs on the 6510 and waiting for them to stop.

### `machine-code-monitor-matrix`

One cell per combination of memory mode and UI transport, repeated `--reps`
times. `--memory` and `--ui` narrow that to a rectangle; `--cells` replaces it
with a named set of intersections:

```bash
--cells "rom:telnet,rom:freeze:2,ram-under-rom:freeze:2"
```

Each term is `MEMORY:UI` or `MEMORY:UI:REPETITIONS`. A term naming an unknown
mode or UI is a usage error before the device is touched. The cell set is
recorded in the run ledger, so a selected run is not mistaken for a full one.

The axes are:

- **UI**: `telnet`, `freeze`, `overlay`
- **Memory**: `ram`, `ram-under-rom`, `rom`, plus the boundary-traversal modes
  `ram-rom-ram` and `ram-rur-rom-ram`
- **Flow, per cell**: Step Over, Step Into, Step Out, Continue To Cursor,
  Continue To Breakpoint, Continue, Reset

Two call shapes run inside each cell, because they fail differently:

- **Nesting**: a chain of `--required-step-into-depth` subroutines, each calling
  the next. 32 by default, 8 for a split U2+L/C64U session. Every Step Into
  pushes another frame, and the return address on the stack is checked at each
  level.
- **Straight calls**: a block of 32 consecutive `JSR` instructions to the same
  helper, stepped with Step Over. The stack pointer returns to the same value
  after every step, so this arms, parks, resumes and disarms from an identical
  state each time. Expected PC, SP and A are exact at every position, so a
  leaked breakpoint slot or a park and resume that drifts the stack fails here.
  Evidence lands in each cell's `straight-call-evidence.json`.

Steps are cross-checked against two independent oracles: the in-tree 6502
interpreter (`mcm6502.py`) and, where installed, VICE over its binary monitor.

### `machine-code-monitor-regression`

The suite to run before a merge. It owns no test logic: it selects lanes out of
the two suites above and drives them through their own runners. The selection is
14 cell runs against the matrix's 45, and 11 of the debug suite's 21 groups. All
five memory modes are run; what the selection cuts is how many transports a mode
is repeated on. About 60 minutes on a U64, against a little over two hours for
all three suites.

Every selected lane carries the reason it is there. Print the plan, with what
the gate does not cover, without touching a device:

```bash
python3 tests/e2e/monitor/monitor_debug_regression_test.py \
    --host u64 --rest-host u64 --list-plan
```

`--skip-cells` and `--skip-groups` run one half of the gate on its own, for
iterating on a single failure. A split `u2@c64u` session gets a smaller plan:
one memory mode, one UI, the groups whose checks are reachable, and the two
`--focus` scopes that only exist for a split session. A U2+L refuses
visible-ROM breakpoints and has one local UI rather than two.

The gate adds one check of its own. It reads the KERNAL and BASIC ROM heads
before it touches the debugger and again when it has finished, so a run that
leaves a displaced byte in the volatile ROM image is named by the run that
caused it rather than by the next suite to notice.

[tests/e2e/doc/machine-code-monitor-regression.md](../doc/machine-code-monitor-regression.md)
derives the selection: which firmware paths branch on the transport, which cells
the recorded matrix runs have seen fail, what each lane costs, and the harness
confounds that have been misread as debugger defects.

## Run history

Each matrix and gate run appends to a cross-run ledger, so progress is visible
without opening individual artifact directories:

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
`doc/research/machine-code-monitor/matrix-runs`. Use `--run-ledger DIR` to point
it elsewhere, or `--no-run-ledger` to skip recording.
