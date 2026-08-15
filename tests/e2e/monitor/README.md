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
- `A` opens the Assembly view and `D` changes nothing, because `D` is reserved
  for a future Debug mode
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
