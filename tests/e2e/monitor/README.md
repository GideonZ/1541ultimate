# Machine code monitor E2E tests

These suites drive the machine code monitor and its step/breakpoint debugger on
a real device and check the result against independent models. The firmware
exposes the monitor through the standard telnet session on port `23`, which the
telnet harness enters with `Ctrl+O` from the normal remote-menu session, and
through the local UI (overlay or freeze) on the device itself.

## Suites

| Suite selector | Script | Drives | Mode |
| --- | --- | --- | --- |
| `monitor-harness` | `monitor_harness_test.py` | Nothing; host-side checks of the harness sources | auto |
| `machine-code-monitor` | `monitor_test.py` | Telnet monitor without Debug mode | manual |
| `machine-code-monitor-debug` | `monitor_debug_test.py` | Telnet Debug mode, all `DBG-*` behaviours | manual |
| `machine-code-monitor-matrix` | `monitor_debug_matrix_test.py` | Full release matrix over telnet, overlay and freeze | manual |

```sh
./run-e2e-tests -H u64 -s monitor-harness
./run-e2e-tests -H u64 -s machine-code-monitor
./run-e2e-tests -H u64 -s machine-code-monitor-debug
./run-e2e-tests -H u64 -s machine-code-monitor-matrix
```

The three device suites are `manual` because each one holds the device for a
long time and drives the debugger hard enough to need a known-good starting
state. Run one per fresh firmware deploy: repeated back-to-back telnet runs
degrade the firmware's telnet stack independently of the debugger.

Optional environment variables accepted by the telnet suites:

- `U64_MONITOR_HOST`
- `U64_MONITOR_PORT`
- `U64_MONITOR_REST_HOST`
- `U64_MONITOR_PASSWORD`
- `U64_MONITOR_TIMEOUT`
- `U64_MONITOR_TARGET` (`u64` or `u2`)
- `U64_MONITOR_KEEP_GOING` (`1` to default-enable keep-going)

## Harness modules

Everything without `_test` in its name is support code, not a suite:

| Module | Role |
| --- | --- |
| `mcm6502.py` | From-scratch NMOS 6510 interpreter used as the step oracle |
| `mcm_rest.py` | REST primitives: `machine:input`, `menu_screen`, `menu_button`, `readmem`, `writemem`, `reset` |
| `mcm_localui.py` | Overlay/freeze monitor driver built on `mcm_rest` |
| `mcm_split_rest.py` | Routes machine ops and overlay ops to two different devices (U2+L in a C64U) |
| `overlay_lifecycle.py` | Local-UI monitor navigation: interface type, bank select, goto, breakpoints |
| `monitor_debug_stress.py` | REST-transport step engine; the matrix suite's opcode-volume lane |
| `freeze_reentry_guard.py` | Matrix preflight: hammers freeze re-entry paths that once hard-halted the Nios |
| `gen_interpreter_vectors.py` | Regenerates `software/test/monitor/monitor_debug_interpreter_vectors.h` |
| `snapshots/expected_snapshots.json` | Expected screen fragments for `monitor_test.py` |

Each module's `--help` is the authority for its own options.

## What the suites check

`monitor_test.py` covers the monitor itself:

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

It parses the VT100 telnet stream into a deterministic `40x25` screen buffer and
compares the captured output against the fragments in
`snapshots/expected_snapshots.json`.

`monitor_debug_test.py` covers Debug mode over the same telnet entry point:
header and footer composition, key routing, breakpoint toggle and list popup,
RETURN navigation preservation, Edit+Debug composition, the BRK trampoline
orchestrator (Go, Over, Trace, Out), and patch hygiene on cleanup. Its CLI
options mirror `monitor_test.py` so the same automation hooks work.

`monitor_harness_test.py` needs no device. It runs the structural anti-masking
check over the gate harnesses, the matrix suite's own unit checks, and executes
the boundary-traversal fixtures through `mcm6502.py` to prove they are valid
6502 that really crosses the regions their walk claims.

## Release matrix

`monitor_debug_matrix_test.py` is the release-readiness gate for the debugger.
It drives the full UI x memory x repetition matrix through the documented flow
(Step Into -> Step Out -> Step Over -> Run to cursor -> Continue-to-breakpoint
-> Continue -> Reset), validating state and stack against `mcm6502.py`, and
writes a coverage ledger plus a `FINAL_REPORT.md` per run.

The memory modes fall into two groups. `ram`, `ram-under-rom` and `rom` each
enter their region cold from a bootstrap and then work inside it. The
boundary-traversal modes instead start with a live context in the developer's
own RAM program and step across a region boundary mid-trace, which is what a
developer actually does: they debug RAM code and step into ROM from there.

| Memory mode | What the cell exercises |
|---|---|
| `ram` | Fixture entirely in RAM |
| `ram-under-rom` | Fixture in the RAM hidden under KERNAL, entered with KERNAL banked out |
| `rom` | Cold entry into the live KERNAL/BASIC image |
| `ram-rom-ram` | RAM program calls BASIC `$BC0F`: Step Into crosses RAM -> ROM, Step Out crosses ROM -> RAM |
| `ram-rur-rom-ram` | RAM -> RAM-under-ROM -> RAM -> ROM -> RAM -> RAM-under-ROM -> RAM, switching `$01` between legs |

A bank switch cannot execute from the window it is switching, so a direct
RAM-under-ROM to visible-ROM step is not expressible on a 6510: the `STA $01`
that maps KERNAL back in would change the bytes the CPU is fetching. The
traversal therefore returns to RAM between the two banked regions, which is what
real code does as well.

`monitor_harness_test.py` executes both traversal fixtures through `mcm6502.py`
on the host, so a broken fixture fails in under a second rather than halfway
through an hour-long hardware run.

With five memory modes and three UI modes a run is 15 cells at one repetition:

```sh
python3 tests/e2e/monitor/monitor_debug_matrix_test.py \
  --host <host> --rest-host <host> --memory all --ui all --reps 2 \
  --strict --fail-fast --artifact-dir <dir>
```

Any code change that touches the stepping engine, breakpoint engine, context
capture, reset state, or UI classification must rerun this full matrix before
being treated as release evidence. A prior "PASS" recorded against a dirty tree
or an older commit does not carry forward.

### Opcode coverage strategy

The matrix binds its large-volume opcode run to `ram` x `overlay`
(`monitor_debug_stress.py`, REST-driven, every step oracle-validated) because
parked-emulation stepping in `ram-under-rom`/`rom` is too slow per step to
support a standalone 1000-opcode run within a practical time budget. Instead,
every `ram-under-rom`/`rom` matrix cell carries its own 100-opcode dual-oracle
Step Into trace (6 cells x 2 reps), so cumulative coverage for those memory
modes comes from the per-cell traces rather than one dedicated volume cell.

### Independent oracle

`mcm6502.py` is a from-scratch 6510 interpreter. It shares no code with the
firmware's stepping engine (`monitor_debug*.cc`) or its host predictor
(`monitor_debug_predictor.cc`), so it can serve as a truth source. It is both
the matrix's live oracle and the generator input for the parked-interpreter
differential vectors in `gen_interpreter_vectors.py` and
`software/test/monitor/monitor_debug_interpreter_vectors.h`. Run
`python3 tests/e2e/monitor/mcm6502.py --selftest` before trusting any run that
depends on it.

## U2 cartridge targets

For U2 cartridge hardware (no monitor-side CPU/VIC bank selection, no KERNAL ROM
snapshot in the monitor view) pass `--target u2` to the telnet suites. U2
firmware ships the same `v1/machine` REST API as U64, so REST-backed helpers
still work; the harness skips only when the API is genuinely unreachable. On
`u2` the harness enables keep-going mode, so every failure is logged with its
last command, terminal snapshot and exception type, and the run continues. Use
`--keep-going` for the same behavior on U64.

### Split session (`--c64-host`)

The matrix suite can drive a U2+L cartridge plugged into a C64U host. The U2+L
renders its monitor overlay only into its own `machine:menu_screen` and toggles
its own `machine:menu_button`, but its `machine:input` is compiled out (`#if
U64`, so it returns HTTP 501), and its C64 memory oracle has to be read from the
C64U. Pass `--c64-host <c64u>` with `--host`/`--rest-host` set to the U2+L.
`mcm_split_rest.py` then routes keystrokes, `readmem`, `writemem` and `reset` to
the C64U while `menu_screen` and `menu_button` stay on the U2+L. Deploy U2+L
firmware over the network; there is no JTAG.

`--c64-host` auto-enables the U2-specific adaptations: skip the `Interface Type`
config (the U2+L has no such setting, freeze is its only UI), no-op the monitor
`select_bank` (the U2 monitor shows `CPU BANK N/A`), treat the source tag as
`[CPU]`, and set breakpoints with `goto`+`R` (no bank view, no `[RAM]`/`[KRN]`
tags).

**Known limitation on a C64U host:** U2 debug stepping and entry cannot complete
on a C64U, because the C64U does not forward the cartridge NMI to its internal
6510 in any bus or interrupt configuration, so the debugger's NMI launch never
redirects the CPU. This is a host-level (C64U core) gap, not a U2+L firmware
bug; on a real C64 with a native cartridge NMI it is expected to work. Full
analysis and evidence: [U2_CARTRIDGE_NMI.md](U2_CARTRIDGE_NMI.md). Plain
(non-debug) monitor features and the split-session plumbing itself are
validated.
