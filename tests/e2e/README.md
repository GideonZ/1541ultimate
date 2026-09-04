# Hardware end-to-end tests

Deterministic functional and regression checks against a deployed Ultimate
device, driven through its public interfaces. A suite may cross REST, FTP,
Telnet, the on-device UI, C64 memory, mounted media or physical-device services
in one scenario. These are the hardware release gate.

What a run records about itself, and why each artefact is the shape it is, is
in [doc/observability-spec.md](doc/observability-spec.md).

## Watching a recorded run

`./run-tests --record -o DIR <target>` writes `DIR/<slug>/video.mp4`: the
harness's screen on the left, the device's video on the right, the device's
audio over both. The left pane is what the harness was looking at, which under
`overlay` and `telnet` is not what the VIC stream carries.

```sh
mpv --sub-file=runs/u64/video.srt --scale=nearest runs/u64/video.mp4
```

`--scale=nearest` keeps 40-column text sharp when the window is larger than the
frame; a smooth scale blurs the glyphs, which is the one thing the recording is
for. The two panes do not share a clock: the right pane advances at the output
frame rate and the left advances when the harness read a different screen.

Three ways to find a test in it, and a reader who has the report needs none of
the others:

- the chapter list in the player, one per suite run and one per failing check,
  titled with the same identity key the report prints;
- `grep FAIL runs/u64/video.srt`, or grep for a suite name, which gives the
  timecodes to seek to without opening the video;
- the `mm:ss` the report prints beside every failing check.

A PDF of the report, for sending a run to somebody with no access to the
repository or the build page, is one command and is never run in CI:

```sh
pandoc runs/index.md -o runs/index.pdf --pdf-engine=weasyprint
```

`./run-tests -o DIR` keeps the run and `python3 tools/e2e_report.py DIR` turns
it into one Markdown document; see [tests/README.md](../README.md).

The gate runs in CI from [.github/workflows/e2e.yml](../../.github/workflows/e2e.yml),
on a self-hosted runner carrying the `e2e` label. Until a runner carries that
label the workflow is valid and never runs, which is the correct failure: the
file lands and the devices decide when it does anything. What such a machine
has to provide, and how the firmware under test gets onto the devices before a
run, is in [doc/self-hosted-runner.md](doc/self-hosted-runner.md).

## Structure

Folders name the primary firmware subsystem under test, not the protocol used
to drive it.

| Folder | Production owner | Coverage |
|---|---|---|
| `api/` | `software/api/` | REST contracts for input, menu screen, memory, disk-image creation and PRG runners, plus `rest-api-coverage`, which calls every registered operation |
| `drive/` | `software/drive/` | Emulated drive geometry and media access through the C64-facing drive protocol |
| `filemanager/` | `software/filemanager/`, `software/userinterface/` | Browser actions, change notification, and managed `/Temp` lifecycle |
| `filesystem/` | `software/filesystem/` | Filesystem implementations, including the remote FTP filesystem |
| `io/` | `software/io/` | Device-facing I/O subsystems, nested by production package (`c64/`, `command_interface/`, `printer/`) |
| `monitor/` | `software/monitor/` | Machine-code monitor behaviour |
| `network/` | `software/network/` | Network service and connection lifecycle |
| `web/` | `html/`, `software/httpd/` | The two pages the device serves, driven in an installed Chrome and Firefox: their light/dark theme, and what `index.html` puts on the wire against the device double |
| `u64ctrl/` | `software/u64ctrl/` | The ESP32 control module: what it does across a loss of input power, and waking the machine from off over Wi-Fi |
| `lib/` | - | Support code shared by E2E suites only: the UI backend, split by transport into `backend.py`, `rest_backend.py`, `telnet_backend.py` and `browser.py` and re-exported from `ui_backend.py`, its menu primitives (`menu.py`), the UI-state gate (`ui_state.py`), the spool of every screen the harness read (`screens.py`), the recorder that composes a run's video (`recorder.py`, with the stream, band, glyph and VIC-text modules beside it), the smoke test of the UI backend itself (`ui_backend_smoke_test.py`), and two device-free checks: the Telnet drain state machine (`telnet_drain_test.py`) and the screen parsers (`ui_backend_parse_test.py`) |

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
sharing a machine never run at the same time. `./run-tests u64 u2@c64u c64u`
runs all three supported machines: `u64` proceeds throughout, while `u2@c64u`
and `c64u` take turns because both need the C64 Ultimate.

## How much runs

`--profile` selects a named bundle: which suites and scenarios run, which UI
transports are swept, and whether the manual suites are included. The ladder is
`smoke`, `quick` (the default), `standard`, `deep`, `exhaustive`, and it is
cumulative, so a suite names the shallowest profile that runs it and every
deeper profile picks it up. `--list` prints the ladder and each suite's
profile, `--list-profiles` prints the matrix of which profile selects which
suite, and [tests/README.md](../README.md) has the measured durations and the
same matrix.

A suite declares its profile in the `SUITES` table in `run-tests`. A scenario
inside a suite declares its own with one line, the same shape as a firmware-fix
tag:

```python
if profiles.skip_below(profiles.STANDARD, LABEL):
    return
```

An untagged suite is `standard`, so a new suite is covered by the merge gate
without anyone having to remember to tag it.

## How the machines differ

Suites do not test for product names. [`tests/lib/machine.py`](../lib/machine.py)
identifies the machine once from `/v1/info` and answers questions about it along
two separate axes.

**Capability** is what a machine is. A C64 Ultimate opens its menu on a launcher
above the file browser, reaches the task menu with `F1` rather than `F5`, needs
two Back presses to close its menu, and searches CommoServe from that launcher
where the other two search Assembly 64 from the task menu. Ask `machine` for the
property rather than branching on the product.

**Configuration** is what a person has set, and is read from the device rather
than derived from the product. "Navigation Style" is the one that changes how
the menu reads a typed letter: under `WASD Cursors`, which a C64 Ultimate ships
with, `w`, `a`, `s` and `d` are cursor keys and `A` to `Z` are folded back to
lowercase, so the way to type a literal letter is to send it shifted.
[`tests/lib/navigation.py`](../lib/navigation.py) reads the setting and
`Browser.type_menu_char` applies it, which covers a quick-seek prefix, a
context-menu prefix and a popup's button key. Text typed into a field is not
touched, because the string editor never sees the key mapper.

The runner also reads every setting each machine is running with before the
first suite and writes the differing ones back when the run ends, so a suite
that changes one does not decide what the next run starts from.

**Firmware vintage** is what a release lacks. The C64 Ultimate runs a separate
firmware line that lags the Ultimate 64, so a check can be correct and still be
unrunnable there. `FIXES` in the same module names each outstanding gap after
the behaviour a machine gains from it, and lists the machines that do not have
it yet. A check declares its dependency in one line:

```python
if ctx.machine.skip_without_fix(machine.BROWSER_REFRESH_ON_DIRECTORY_CHANGE,
                                label):
    return
```

The check then reports SKIP with the machine and version in the reason, for
example `needs the browser-refresh-on-directory-change fix, which C64 Ultimate
1.2.0 does not have`.

Those names are the amendment point. When a fix is backported, delete its `FIXES`
entry and every check tagged with it runs again; nothing else changes. To confirm
a backport before editing the table, run the tagged checks anyway:

```sh
./run-tests c64u --assume-fix browser-refresh-on-directory-change
./run-tests c64u --assume-fix all
```

`--help` is authoritative for options. `-m/--mode` selects the UI transport
(`telnet`, `freeze`, `overlay`) for suites that support switching. With no
`-m`, the transports the profile sweeps are used, which is `overlay` up to
`standard`. Use `-s` for isolation rather than invoking a suite directly, so
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

The runner establishes the documented UI state before each suite that is
handed a device, and performs one final release, menu close and reset
afterwards. A failure in that teardown fails the run. A suite whose registry
entry names no host is exempt from both the state gate and the health sweep:
it is handed no device, so it can neither be affected by the device's state nor
leave it dirty.

## Rules for adding or changing a suite

1. Put the suite under the folder for the production subsystem that owns the
   behaviour. Add a top-level folder only for a real production subsystem that
   does not fit an existing one.
2. Use lowercase snake case. Executable suites end in `_test.py`, with
   qualifiers before `_test`. Keep them executable. Python only.
3. Give the runner a stable kebab-case selector and register every executable
   suite in `run-tests`. New suites are automatic unless they need operator
   opt-in; document any `manual` reason next to the runner entry.
   `tests/lib/registry_test.py` fails the gate when a suite is missing from the
   registry, when a registered path does not exist, or when a registration
   names an argument the suite would refuse.
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
9. A new REST operation is not finished until `rest-api-coverage` knows about
   it. That suite reads the operation list from
   `doc/api/rest_api_openapi_*.yaml` when the tree has it and from the
   `API_CALL` macros otherwise, and fails while an operation is neither
   exercised by a case nor written into one of its three tables: `EXCLUDED`
   (never call it, and why), `HAPPY_ELSEWHERE` (which suite owns the happy
   path) or `NEGATIVE_ONLY` (why there cannot be one). The gate exists because
   a device-halting bug in `files:create_d64` shipped while no suite called
   that route at all.

   Prefer a case that checks what the call did over one that checks the status
   code: read the drive listing back after mounting, read the item back after
   writing it. Where the API exposes no outcome, say so in the case rather than
   letting a 200 stand in for a result. A case that changes machine state is
   marked `exclusive`, records what it found, and restores it.

10. Keep each check under ten seconds. Above that `tests/lib/report.py` marks
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
   - Sending more keystrokes than the movement needs. On a cartridge target
     every injected key costs a fixed 100ms crossing the host's keyboard
     matrix, and that rate belongs to the host's own released firmware, so it
     cannot be shortened from this tree. What can change is how many keys a
     movement takes. `Browser.move_rows` spends page keys on the bulk of a
     jump and single steps on the remainder, in one request, which takes a
     22-row advance from 22 keys to 12; `Browser.fill_edit_field` empties a
     string field with one KEY_CLEAR rather than a counted run of BACKSPACE
     taps. Prefer both over `press_many` for anything in the file browser.
   - Naming a fixture at length. A generated name is typed into a field one
     key at a time, so `pm45535.prg` costs a third of what
     `prgmenu45535.prg` did. Keep names short enough to be cheap and long
     enough to be unmistakable in a listing.
   - Moving more data than the assertion needs. Size a fixture for what is
     being proved: if only the rendered size has to differ, a few kilobytes
     does that as well as a few hundred.
   - Repeating setup an earlier check already established.

   Some checks are legitimately slow, and those keep their time: a key repeat
   rate, a drive reaping a session, a real 1541 load. Say so in a comment next
   to the wait, so the next reader does not have to rediscover it.

11. Keep the suite passing `ruff check --config tests/ruff.toml tests run-tests`.
    `lint_test.py` runs that at the front of every profile.
    [`tests/lib/README.md`](../lib/README.md) says what belongs in that
    configuration and what belongs in a `# noqa` comment at the site; both
    need the reason written next to them.

Before submitting a structural or test-only change:

```sh
# Syntax, unused imports, dead assignments and names that are only resolved
# when the line runs. ruff's F rules are pyflakes, so this covers the check
# that used to be a separate pyflakes invocation, and it reads every file
# rather than only the ones a run reaches.
python3 tests/lib/lint_test.py
./run-tests --list

# The lint parses each file; this imports it, which is a stronger statement:
# it proves the sys.path bootstrap resolves and the module-level code runs.
for f in $(find tests -name '*_test.py' -not -path '*/__pycache__/*'); do
    python3 "$f" --help >/dev/null || echo "FAILED $f"
done
```

For behaviour changes, deploy the affected firmware and run the narrowest
relevant selector first, then the default sweep. Treat a hardware failure as a
regression to diagnose, not a reason to relax the test.
