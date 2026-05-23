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
