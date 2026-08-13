# Hardware end-to-end tests

Deterministic functional and regression checks against a deployed Ultimate
device, driven through its public interfaces. A suite may cross REST, FTP,
Telnet, the on-device UI, C64 memory, mounted media or physical-device services
in one scenario. These are the hardware release gate.

## Structure

Folders name the primary firmware subsystem under test, not the protocol used
to drive it.

| Folder | Production owner | Coverage |
|---|---|---|
| `api/` | `software/api/` | REST contracts for input, menu screen, memory, and PRG runners |
| `filemanager/` | `software/filemanager/`, `software/userinterface/` | Browser actions, change notification, and managed `/Temp` lifecycle |
| `filesystem/` | `software/filesystem/` | Filesystem implementations, including the remote FTP filesystem |
| `io/` | `software/io/` | Device-facing I/O subsystems, nested by production package (`c64/`, `command_interface/`, `printer/`) |
| `monitor/` | `software/monitor/` | Machine-code monitor behaviour |
| `network/` | `software/network/` | Network service and connection lifecycle |
| `lib/` | - | Support code shared by E2E suites only: the UI backend (`ui_backend.py`), its menu primitives (`menu.py`), the UI-state gate (`ui_state.py`), and the device-free check of the Telnet drain state machine (`telnet_drain_test.py`) |

Assets and narrowly scoped helpers stay beside the suite that owns them.
Reporting is shared beyond E2E and lives in [`tests/lib/`](../lib/).
`monitor/` and `io/printer/` carry their own README for subsystem detail.

## Running

The repository-root runner is the supported entry point.

```sh
./run-tests --list
./run-tests <target> -p <password>
./run-tests <target> -s <suite>
./run-tests <target> -m telnet
./run-tests u64 u2@c64u
```

A target is a host, or `cartridge@computer` for a cartridge under test in the
computer that supplies its C64 keyboard and video; see
[tests/README.md](../README.md) and [`tests/lib/targets.py`](../lib/targets.py).
Naming several runs one child process per target, scheduled so that two targets
sharing a machine never run at the same time.

`--help` is authoritative for options. `-m/--mode` selects the UI transport
(`telnet`, `freeze`, `overlay`; default `overlay`) for suites that support
switching. Use `-s` for isolation rather than invoking a suite directly, so
selection, arguments and logs stay consistent.

Preserve combined stdout and stderr, keeping the runner's exit status:

```sh
set -o pipefail
./run-tests <target> 2>&1 | tee "run-tests-$(date +%Y%m%d-%H%M%S).log"
```

### Device requirements

- Reachable REST API, an otherwise idle device, and a current firmware build.
- A cartridge target needs both machines reachable: the cartridge serves its
  own REST, and the computer takes the C64 keyboard injection the cartridge
  answers with HTTP 501.
- Commodore ROMs installed under `C64 and Cartridge Settings`:
  `kernal.901227-03.bin`, `basic.901226-01.bin`, `characters.901225-01.bin`.
  Several suites need the C64 to reach the BASIC prompt with KERNAL interrupts
  running.
- `freeze-menu` needs an FPGA core carrying the SID decoder fix for issue #733.
  An older core stalls the whole Nios and needs a JTAG recovery.
- These settings may differ from the default: `System Mode`, `Auto Save
  Config`, `Interface Type`, `Command Interface`, `RAM Expansion Unit`,
  `Printer Settings`. Suites that depend on any of them set and restore it.
- Suites marked `manual` need an operator decision, elevated host privileges or
  a long run. `--all` is not a routine smoke-test option.

The runner establishes the documented UI state before each suite and performs
one final release, menu close and reset afterwards. A failure in that teardown
fails the run.

## Rules for adding or changing a suite

1. Put the suite under the folder for the production subsystem that owns the
   behaviour. Add a top-level folder only for a real production subsystem that
   does not fit an existing one.
2. Use lowercase snake case. Executable suites end in `_test.py`, with
   qualifiers before `_test`. Keep them executable. Python only.
3. Give the runner a stable kebab-case selector and register every executable
   suite in `run-tests`. New suites are automatic unless they need operator
   opt-in; document any `manual` reason next to the runner entry.
4. Keep the default scenario deterministic and bounded. Assert externally
   visible outcomes, not private implementation timing. Report through
   `tests/lib/report.py`; see [its rules](../lib/README.md).
5. Return non-zero for every failed assertion, setup failure, lost device or
   incomplete cleanup. Never turn a firmware failure into a skip or a pass.
   Retries must represent an explicit protocol allowance, stay bounded, and
   preserve the original failure in diagnostics.
6. Capture and restore settings, release injected input, close sessions, and
   remove only fixtures the suite created. Clean up in a `finally` path.
7. Reuse `lib/` for shared protocol decoding or screen models instead of
   copying it. Keep support code local until a second suite needs it.
8. State supported targets and unusual dependencies in the suite docstring.
   Keep this file structural; do not copy per-suite CLI help into it. A suite
   that needs a host Python package adds it to [`tests/requirements.txt`](../requirements.txt)
   and to the table in [`tests/README.md`](../README.md) in the same change, so
   a fresh checkout can run the gate without guessing.
9. Keep each check under ten seconds. Above that `tests/lib/report.py` marks
   the duration `SLOW` in yellow, which is a prompt to look rather than a
   failure. The whole gate is run repeatedly by people waiting for it, so a
   slow check has to earn its time.

   Ten is the threshold because some checks cannot go below a few seconds
   whatever they do: one that resets the C64 pays about 2.4s for the machine's
   cold start before it does anything of its own. Aim well under the threshold
   anyway; most checks finish in under a second.

   Before accepting a slow check, establish that it is slow for a reason that
   cannot be removed:

   - Waiting on a fixed sleep where the outcome could be observed instead. Poll
     for the state the check is actually waiting for.
   - Sending keystrokes one at a time. The batched paths in
     `lib/ui_backend.py` send a whole string or run of keys in one request;
     `Browser.select_entry` uses the browser's own quick-seek rather than
     walking the listing.
   - Moving more data than the assertion needs. Size a fixture for what is
     being proved: if only the rendered size has to differ, a few kilobytes
     does that as well as a few hundred.
   - Repeating setup an earlier check already established.

   Some checks are legitimately slow, and those keep their time: a key repeat
   rate, a drive reaping a session, a real 1541 load. Say so in a comment next
   to the wait, so the next reader does not have to rediscover it.

Before submitting a structural or test-only change:

```sh
python3 -m py_compile $(find tests -name '*.py' -type f -not -path '*/__pycache__/*')
./run-tests --list

# Proves every suite's imports resolve, which py_compile alone does not.
for f in $(find tests -name '*_test.py' -not -path '*/__pycache__/*'); do
    python3 "$f" --help >/dev/null || echo "FAILED $f"
done

# Neither of the above sees a name that is only resolved when the line runs, so
# a helper used on a branch the gate does not reach stays broken until someone
# hits it. pyflakes finds those, and is worth installing for this one check.
python3 -m pyflakes $(git ls-files tests | grep '\.py$') run-tests
```

For behaviour changes, deploy the affected firmware and run the narrowest
relevant selector first, then the default sweep. Treat a hardware failure as a
regression to diagnose, not a reason to relax the test.
