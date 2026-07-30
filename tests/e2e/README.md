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
| `io/` | `software/io/` | Device-facing I/O subsystems; nested by production package (`c64/`, `printer/`) |
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
   enough numbered context to identify the exact failing operation.
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
