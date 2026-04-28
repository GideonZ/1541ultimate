# Monitor Telnet Validation

This directory contains the real-device machine monitor validation harness.

## Service

The firmware exposes a dedicated telnet machine monitor session on port `6510` when the telnet service is enabled.
The existing remote-menu telnet service on port `23` is unchanged.

## Run

```bash
python3 tools/developer/machine-code-monitor/monitor_test.py --host u64 --port 6510
```

Optional environment variables:

- `U64_MONITOR_HOST`
- `U64_MONITOR_PORT`
- `U64_MONITOR_REST_HOST`
- `U64_MONITOR_PASSWORD`
- `U64_MONITOR_TIMEOUT`

## What It Checks

- KERNAL memory visibility at `$E000`
- REST `v1/machine:readmem` consistency with monitor KERNAL bytes
- Disassembly byte-width formatting
- Screen stability after scrolling away and back
- ASCII row width
- Immediate inline edit visibility and commit
- Cursor vs scroll behavior in ASCII view
- CPU bank status changes and RAM-visible writes at `$A000`
- VIC bank status line format

The harness parses the VT100 telnet stream into a deterministic `40x25` screen buffer and compares the captured output against the expected snapshot fragments in `snapshots/expected_snapshots.json`.
