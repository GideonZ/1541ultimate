# Monitor Telnet Validation

This directory contains the real-device machine monitor validation harness.

## Service

The firmware exposes the machine monitor through the standard telnet session on port `23`.
The validation harness connects to the normal remote-menu session and enters the monitor with `Ctrl+O`.

## Run

```bash
python3 tools/developer/machine-code-monitor/monitor_test.py --host u64 --port 23
python3 tools/developer/machine-code-monitor/monitor_debug_test.py --host u64 --port 23
python3 tools/developer/machine-code-monitor/monitor_debug_soak.py --host u64 --port 23 --duration 600 --copy-roms-to-ram --yes-copy-roms
```

For U2 cartridge hardware (no monitor-side CPU/VIC bank selection, no KERNAL
ROM snapshot in the monitor view) pass `--target u2`. The U2 firmware does
ship the same `v1/machine` REST API as U64, so REST-backed helpers continue
to work; the harness only skips when the API is genuinely unreachable. The
harness automatically enables keep-going mode in `u2` so every failure is
logged with full context (last command, terminal snapshot, exception type)
and the run continues. The end-of-run summary lists every skipped and failed
check so a remote operator can capture the entire console output and hand it
to an LLM for autonomous fixes:

```bash
python3 tools/developer/machine-code-monitor/monitor_test.py --host <u2-host> --port 23 --target u2
python3 tools/developer/machine-code-monitor/monitor_debug_test.py --host <u2-host> --port 23 --target u2
```

Use `--keep-going` to enable the same behavior on U64 (useful for capturing
multiple regressions in one run rather than stopping at the first).

Optional environment variables:

- `U64_MONITOR_HOST`
- `U64_MONITOR_PORT`
- `U64_MONITOR_REST_HOST`
- `U64_MONITOR_PASSWORD`
- `U64_MONITOR_TIMEOUT`
- `U64_MONITOR_TARGET` (`u64` or `u2`)
- `U64_MONITOR_KEEP_GOING` (`1` to default-enable keep-going)

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
- Debug mode routing, footer layout, breakpoints, stepping, cleanup, and preserved Assembly navigation/bookmark shortcuts
- Debug soak coverage against BASIC/KERNAL ROM copies in RAM, with every supported step checked against an independent footer-state model

The harness parses the VT100 telnet stream into a deterministic `40x25` screen buffer and compares the captured output against the expected snapshot fragments in `snapshots/expected_snapshots.json`.

`monitor_debug_soak.py` is destructive: `--copy-roms-to-ram --yes-copy-roms` copies BASIC `$A000-$BFFF` and KERNAL `$E000-$FFFF` into the RAM underneath those ROM windows and forces the live C64 CPU port into all-RAM mode while it runs. Use `--yes-copy-roms` only on a disposable test session.

The U64 debugger firmware additionally supports breakpoints in volatile U64 ROM
image memory (no flash writes, restored on reboot), so non-soak debug sessions
can step KERNAL/BASIC/CHAR ROM without the destructive RAM-shadow step.

## Release Matrix Gate

`monitor_debug_matrix_gate.py` is the release-readiness gate for the debugger.
It drives the full UI x memory x repetition matrix (`telnet`/`overlay`/`freeze`
x `ram`/`ram-under-rom`/`rom`, 2 reps by default) through the documented flow
(Step Into -> Step Out -> Step Over -> Run to cursor -> Continue-to-breakpoint
-> Continue -> Reset), validating state and stack against the independent
`mcm6502.py` oracle, and writes a coverage ledger plus a `FINAL_REPORT.md` per
run:

```bash
python3 tools/developer/machine-code-monitor/monitor_debug_matrix_gate.py \
  --host <host> --rest-host <host> --memory all --ui all --reps 2 \
  --strict --fail-fast --artifact-dir <dir>
```

Any code change that touches the stepping engine, breakpoint engine, context
capture, reset state, or UI classification must rerun this full matrix before
being treated as release evidence; a prior "PASS" recorded against a dirty
tree or an older commit does not carry forward.

### 1000-opcode coverage strategy

The gate binds its large-volume opcode run to `ram` x `overlay`
(`monitor_debug_stress.py`, REST-driven, every step oracle-validated) because
parked-emulation stepping in `ram-under-rom`/`rom` is too slow per-step to
support a standalone 1000-op run within a practical time budget. Instead,
every `ram-under-rom`/`rom` matrix cell carries its own 100-opcode dual-oracle
Step Into trace (6 cells x 2 reps), so cumulative RUR/ROM opcode coverage
comes from the per-cell traces rather than a single dedicated volume cell.

### Independent oracle

`mcm6502.py` is a from-scratch 6510 interpreter (no shared code with the
firmware's stepping engine) used both as the matrix gate's live oracle and to
generate the parked-interpreter differential vectors in
`gen_interpreter_vectors.py` / `software/test/monitor/monitor_debug_interpreter_vectors.h`.
Run `python3 tools/developer/machine-code-monitor/mcm6502.py --selftest`
before trusting any run that depends on it.

## U2+L split-session support (`--c64-host`)

The matrix gate can drive a **U2+L cartridge plugged into a C64U host**. The U2+L
renders its MCM overlay only into its own `machine:menu_screen` and toggles its
own `machine:menu_button`, but its `machine:input` is compiled out (`#if U64`, so
it returns HTTP 501) and its C64 memory oracle is read from the C64U. Pass
`--c64-host <c64u>` (with `--host`/`--rest-host` = the U2+L). `mcm_split_rest.py`
then routes keystrokes/`readmem`/`writemem`/`reset` to the C64U while
`menu_screen`/`menu_button` stay on the U2+L. Deploy U2+L firmware over the
network with `tools/api/u2_flash.py` (there is no JTAG). U2-specific gate
adaptations (auto-enabled by `--c64-host`): skip the `Interface Type` config
(the U2+L has no such setting - freeze is its only UI), no-op monitor `select_bank`
(the U2 MCM shows `CPU BANK N/A`), treat the source tag as `[CPU]`, and set
breakpoints with `goto`+`R` (no bank view, no `[RAM]`/`[KRN]` tags).

**Known limitation on a C64U host:** U2 debug *stepping/entry* cannot complete on a
C64U, because the C64U does not forward the cartridge NMI to its internal 6510 in
any bus/interrupt configuration - so the debugger's NMI launch never redirects the
CPU. This is a host-level (C64U core) gap, not a U2+L firmware bug; on a real C64
(native cartridge NMI) it is expected to work. Full analysis + evidence:
`tools/developer/machine-code-monitor/U2_CARTRIDGE_NMI.md`. Plain
(non-debug) MCM features and the split-session plumbing itself are validated.
