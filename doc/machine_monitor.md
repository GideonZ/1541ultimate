# Machine Monitor

The machine monitor is a keyboard-driven memory and code inspector for live or frozen C64 state. It is intended for direct work on bytes, screen RAM, disassembly, and execution flow rather than symbolic debugging.

## Entry and Exit

- Open the monitor with `C=+O`.
- Close the monitor with `C=+O` or `ESC`.
- Open the built-in help with `F3`.

The header shows the current view and cursor address. The footer shows the active CPU port mapping and VIC bank.

## Views

The monitor provides five views:

| Key | View | Purpose |
| --- | --- | --- |
| `M` | Memory | Hex dump of memory at the current address |
| `I` | ASCII | 32-byte text view using printable ASCII only |
| `V` | Screen | 32-byte screen-code view rendered as C64 glyphs |
| `A` | Assembly | 6502 disassembly and inline assembly editing |
| `B` | Binary | Bit-level view and bit editing |

`U` toggles undocumented opcodes in assembly view.

ASCII and Screen details:

- ASCII view shows printable ASCII only. Non-printable bytes are shown as `.` characters.
- Screen view uses C64 screen-code rendering, not PETSCII text decoding.

### Width Modes

`W` is view-dependent:

- In Memory view, `W` cycles `8 <-> 16` bytes per row.
- In Binary view, `W` cycles `1 -> 2 -> 3 -> 3S -> 4 -> 1`.
- ASCII, Screen, and Assembly views are fixed-width.

Binary width details:

- `1`, `2`, and `3` show one, two, or three bytes as bit fields with a trailing hex preview.
- `3S` shows packed 24-bit rendering for three-byte sprite-style data, still with a hex preview.
- `4` shows packed 32-bit rendering for four bytes without a trailing hex preview.

`P` toggles poll mode on the local monitor. Over telnet, poll mode is unavailable.

## Navigation and Context

- `J`: jump to an address.
- `G`: leave the monitor and start execution at an address.
- `O`: cycle CPU port banking (`CPU0`..`CPU7`).
- `Shift+O`: cycle the VIC bank override.
- `Z`: toggle freeze when the backend supports it.

Addresses in command prompts are hexadecimal. The generic number parser used by the number tool also accepts `$1234`, `0x1234`, decimal, and `%binary` forms.

## Editing

- `E`: enter edit mode in Memory, ASCII, Screen, Assembly, and Binary views.
- `C=+E` or `ESC`: leave edit mode.

Edit behavior is view-specific:

- Memory: type two hex nibbles to write one byte.
- ASCII: type printable ASCII bytes directly.
- Screen: type screen characters directly.
- Binary: type `0` or `Space` to change the selected bit to `0`. Type `1` or `*` to change it to `1`.
- Assembly: edit instructions inline. The monitor provides mnemonic completion through the opcode picker and also accepts direct operand typing.

`DEL` is logical delete, not a raw backspace:

- Memory: writes `$00` and advances.
- ASCII/Screen: writes a space.
- Binary: clears the selected bit.
- Assembly: replaces the current instruction with `NOP` bytes. While typing an instruction, `DEL` first undoes the current line edit state.

## Clipboard, Range, and Number Tool

- Copy the current byte with `C=+C`.
- Paste the clipboard at the cursor with `C=+V`.
- Toggle range mode with `R`.
- Open the number tool with `N`.

Range mode anchors the current address. While range mode is active:

- `C=+C` copies the selected span.
- Pressing `R` again also copies the selected span and exits range mode.

The number tool is a compact base-conversion and overwrite popup for the current target. It shows the same value in the following forms:

- Hex
- Decimal
- Binary
- ASCII
- Screen code

In assembly view, the number tool targets the operand bytes of the current instruction when possible.

## Memory Operations

The monitor includes direct bulk memory commands:

| Key | Command | Syntax | Result |
| --- | --- | --- | --- |
| `F` | Fill | `start-end,value` | Fill an inclusive range with one byte |
| `T` | Transfer | `start-end,dest` | Copy a range to a destination |
| `C` | Compare | `start-end,dest` | Compare a range against another location and list differing addresses |
| `H` | Hunt | `start-end,bytes` or `start-end,"text"` | Search for a byte sequence or quoted string |

`Hunt` opens a result picker. `ENTER` jumps to the selected match and `ESC` closes the picker.

## File I/O

- `L`: load a file into memory.
- `S`: save memory to a file.

Load is a two-step flow: pick a file, then enter load parameters.

- Load template: `PRG,0000,AUTO`
- Field 1: `PRG` or a start address.
- Field 2: file offset.
- Field 3: `AUTO` or an explicit length.

Examples:

- `PRG` loads a PRG to its embedded load address.
- `0801` loads to `$0801`.
- `PRG,1000` skips `$1000` bytes after the PRG header.
- `0801,0002,0010` loads `$0010` bytes from offset `$0002` to `$0801`.

Save writes a normal PRG file with a two-byte load address header.

- Save template: `0800-9FFF`
- The range is inclusive.

## Bookmarks

The monitor has 10 persistent bookmark slots.

- List bookmarks with `C=+B`.
- Jump directly to a slot with `C=+0` .. `C=+9`.

Each bookmark stores:

- Address
- View
- CPU bank
- VIC bank
- View width mode where applicable

Bookmark popup controls:

- `Up`/`Down`: select a slot.
- `Return`: restore the selected slot.
- `S`: store the current location into the selected slot.
- `L`: edit the label.
- `DEL`: reset the slot to its default.
- `0`..`9`: jump directly to that slot.

Default slots are aimed at common C64 locations:

| Slot | Default |
| --- | --- |
| `0` | `ZP` at `$0000` in Memory view |
| `1` | `SCREEN` at `$0400` in Screen view |
| `2` | `BASIC` at `$0801` in Assembly view |
| `3` | `BASROM` at `$A000` in Assembly view |
| `4` | `HIRAM` at `$C000` in Assembly view |
| `5` | `VIC` at `$D000` in Memory view |
| `6` | `SID` at `$D400` in Memory view |
| `7` | `CIA1` at `$DC00` in Binary view |
| `8` | `CIA2` at `$DD00` in Binary view |
| `9` | `KERNAL` at `$E000` in Assembly view |

## Additional Notes

Switching between UI Freeze and UI Overlay modes:

- To switch modes, exit the monitor, press `C=+I`, then reopen the monitor.
- Use UI Freeze mode when the monitor output must be captured in the video stream.
- Use UI Overlay on HDMI mode when polling is needed to observe live changes.
