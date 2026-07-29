# Hardware end-to-end tests

These suites drive real firmware on a real Ultimate device over REST, FTP and
Telnet. They are not unit tests, they do not run in CI, and they need a
reachable, idle device to talk to.

## Prerequisites

- A reachable Ultimate 64/64e or Ultimate-II(+/L) with an idle menu.
- `U64_HOST` (or `-H`) for the device hostname/IP, `U64_PASS` (or `-p`) for
  its REST/FTP password.

## Running everything

```
./run-e2e-tests -H <host> -p <pass>
```

Add `--list` to see every suite and its `auto`/`manual` mode, `-s <name>`
(repeatable) to run just one or a few, `-x` to stop at the first failure, and
`-a` to also include the `manual` suites (they need extra privileges, a long
wall-clock wait, or a device-setting toggle, so they're opt-in).

## Running one suite directly

Each suite is a normal script; run it on its own exactly as `run-e2e-tests`
would, e.g. `python3 tests/e2e/api/menu_screen_test.py -H <host> -p <pass>`.

## What's under each folder

- `api/` - REST API contract tests: keyboard/joystick input, menu-screen
  decode, readmem/writemem, and the PRG-runner path-trimming check.
- `filemanager/` - the on-device file browser: context menus, long filenames,
  FTP client behaviour, and cross-observer file-system change notification.
- `monitor/` - the machine-code monitor/debugger, driven over Telnet.
- `printer/` - the virtual IEC printer, including the 6510 test workload and
  PNG output verification.
- `telnet/` - Telnet session lifecycle (stale-session reaping).
- `temp-auto-cleanup/` - the `/Temp` auto-cleanup housekeeping feature.
- `c64/` - C64-core specific regressions, e.g. the SID-mirroring freeze wedge.

Host-side unit tests are not here: `software/api/tests/` and
`software/filemanager/tests/` are C++ tests built next to the modules they
cover, and stay colocated with their source.
