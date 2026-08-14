# Monitor Validation

The real-device validation harness for the machine-code monitor
(`software/monitor/`). One suite, `machine-code-monitor`, driven through the
shared UI facade in [`tests/e2e/lib/ui_backend.py`](../lib/ui_backend.py).

## How the monitor is reached

Open it with `C=+O` (`Ctrl+O` over Telnet) while the on-device menu is up, or
through `F5` -> `Developer` -> `Machine Code Monitor`. The shortcut is handled
in `UserInterface::keymapper`, which every UI context passes its keys through;
a popup, a string box, an editor and the monitor itself own the screen while
they are up and do not answer it. `C=+O`, `RUN/STOP` and
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
- Hunt keeps the case of a quoted needle; Load accepts both `PRG,0,AUTO` and
  the all-empty `,,`

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
