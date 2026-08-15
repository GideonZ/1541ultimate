# Machine Code Monitor

The Machine Code Monitor is a keyboard-driven tool for inspecting and editing
live or frozen C64 memory. It offers hexadecimal, ASCII, screen-code, binary and
assembly views, inline editing in every view, bulk memory operations, file
load/save, bookmarks, and execution from a selected address.

## Entry and Exit

`C=` denotes the Commodore key. `C=+O` means: hold the Commodore key, then press
`O`.

To open the monitor, use one of the following:

- Press `C=+O` in the file browser, which is the screen the menu opens on.
- Press `F5`, open `Developer`, then select `Machine Code Monitor`. This works
  from every menu screen.

`C=+O` is a file-browser shortcut. The task menu, the settings screens, the
system-information screen and the context menus read the key and do nothing
with it; leave them with `RUN/STOP` first, or use `F5` instead.

To leave the monitor with nothing else open, press `X`, `C=+O`, `RUN/STOP`,
`ESC` on a USB keyboard, or the C64's top-left `←` key. Pushing the device's
menu button also closes it. With a popup, a prompt or edit mode open,
`RUN/STOP`, `ESC` and `←` close that layer first; see [Back](#back).

`C=+O` leaves the monitor outright from a memory view, from edit mode, from the
help screen, and from the Number, Hunt-result, Compare-result and
opcode-completion popups. It is not read while a command prompt, the Number
calculator or the Bookmarks popup is open; leave those with Back first.

Closing and reopening the monitor keeps the view, the cursor address, the CPU
bank, the undocumented-opcode and screen-charset settings, both row widths, and
the last Load, Save and Go parameters. It does not keep the clipboard. Powering
the machine off loses all of it.

### Help

Open the built-in help with `?` or `F3`. `?`, `F3`, `RUN/STOP` and `←` close
help again without leaving the monitor. `C=+B` and `C=+0` through `C=+9` also
close help without performing their bookmark action. Any other command key
closes help and then runs that command.

### Back

`RUN/STOP`, `ESC` and the C64's top-left `←` key are the same Back action, and
each press removes exactly one active interaction layer:

| What is active | What Back does |
| --- | --- |
| Help | Closes help |
| A number expression | Returns to the Number popup |
| A popup: Number, Bookmarks, opcode completion, Hunt or Compare results | Closes that popup |
| A command prompt | Cancels the prompt |
| Inline edit mode | Leaves edit mode |
| Nothing of the above | Leaves the monitor |

There is one exception. Where `←` is valid data it stays data: in ASCII and
Screen edit mode it types its character, and on the ASCII and Screen rows of the
Number popup it types its value. `RUN/STOP` and `ESC` are still Back in those
places.

## Screen Layout

The monitor screen has three fixed regions.

### Header

Shows the current view, the cursor address, and the active modes. The mode
fields sit in fixed columns at the right-hand end of the line:

| Field | Meaning |
| --- | --- |
| `Undoc` | Assembly view with undocumented opcodes enabled |
| `Range` | Range mode is active |
| `Frz` | The machine is frozen |
| `Poll` | Poll mode is active |
| `EDIT` | Edit mode is active |

`Undoc` and `Range` share one column. When both apply, `Undoc` is shown.

### Body

- Shows the memory region around the current cursor address.
- The active cursor position is highlighted in reverse.
- May show popups: Hunt and Compare results, the Number popup, the opcode
  completion list, and the Bookmarks list.

### Footer

- Shows the selected CPU memory configuration and VIC bank. See
  [CPU and VIC Bank Display](#cpu-and-vic-bank-display).
- After a bookmark or follow/return action, the footer is replaced for about
  two seconds by that action's status text.
- With help open, the footer is replaced by the paging keys.

Example layout:

```text
+--------------------------------------+
|MONITOR ASM $E011  Undoc Frz Poll EDIT|
|...                                   |
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

## Views

The monitor provides five views:

| Key | View     | ID  | Purpose                         |
| --- | -------- | --- | ------------------------------- |
| `M` | Memory   | HEX | Hexadecimal byte view           |
| `A` | Assembly | ASM | Disassembly and inline assembly |
| `B` | Binary   | BIN | Bit-level byte view             |
| `I` | ASCII    | ASC | ASCII byte view                 |
| `V` | Screen   | SCR | Screen code view                |

`D` is reserved for the debugger and does not switch views. `A` is the only key
that opens the Assembly view.

### Memory / Hex View

Memory view shows raw bytes in hexadecimal. At the default width of 8 bytes per
row it also shows a printable-character preview to the right of the hex
columns. At 16 bytes per row there is no preview column; the row is split into
two groups of eight hex bytes.

Example:

```text
+--------------------------------------+
|MONITOR HEX $0168                     |
|00E0 85 85 85 85 85 85 86 86 ........ |
|00E8 86 86 86 86 86 87 87 87 ........ |
|00F0 87 87 87 F0 DB 00 00 00 ........ |
|00F8 00 00 00 00 00 00 00 20 .......  |
|0100 33 38 39 31 31 00 30 30 38911.00 |
|0108 30 30 00 00 10 10 35 02 00....5. |
|0110 00 00 10 10 35 02 00 00 ....5... |
|0118 1C 10 35 02 00 00 22 10 ..5...". |
|0120 35 02 00 00 28 10 35 02 5...(.5. |
|0128 00 10 35 02 00 00 32 10 ..5...2. |
|0130 35 02 00 00 38 10 35 02 5...8.5. |
|0138 00 00 3E 10 35 02 00 00 ..>.5... |
|0140 44 10 35 02 00 00 44 10 D.5...D. |
|0148 35 02 00 00 50 10 35 02 5...P.5. |
|0150 00 00 56 10 35 02 00 00 ..V.5... |
|0158 5C 10 35 02 00 00 62 10 \.5...b. |
|0160 35 02 00 00 68 10 35 02 5...h.5. |
|0168 00 00 6E 10 35 02 00 00 ..n.5... |
|CPU1 $A:RAM $D:CHR $E:RAM VIC0 $0000  |
+--------------------------------------+
```

### Assembly View

Assembly view shows decoded 6510 instructions, their instruction bytes, and
what each row was read from. The source is shown in brackets at the right-hand
end of the row: `[RAM]`, `[BASIC]`, `[CHAR]`, `[IO]` or `[KERNAL]` where the
monitor selects the bank itself, and `[CPU]` on an Ultimate II+, where it reads
whatever the CPU currently sees.

An opcode that has no defined meaning is shown as `???` and consumes one byte
unless undocumented opcodes are enabled with `U`. An instruction whose operand
bytes would run past `$FFFF` is also shown as `???`.

#### I/O is data, not code

With I/O banked in, `$D000-$DFFF` is not memory: each read returns whatever the
register holds at that instant, and the same address answers differently from
one read to the next. Disassembling those bytes would give a different
instruction, and a different instruction length, on every redraw, so every row
below would move while you were only scrolling.

Assembly view therefore shows one row per address there, as `.BYTE $xx`, marked
`[IO]`. The rows stay where they are, and each shows what its register holds
now.

This follows what the address reads rather than where it is. With CHAR ROM or
RAM banked into `$D000-$DFFF` instead, those addresses hold stable bytes that
may well be code, and are disassembled normally. On an Ultimate II+ nothing is
treated this way and `$D000-$DFFF` is disassembled like any other region.

Example:

```text
+--------------------------------------+
|MONITOR ASM $E011                     |
|DFF9 FF           .BYTE $FF       [IO]|
|DFFA 00           .BYTE $00       [IO]|
|DFFB 12           .BYTE $12       [IO]|
|DFFC FF           .BYTE $FF       [IO]|
|DFFD 00           .BYTE $00       [IO]|
|DFFE 3C           .BYTE $3C       [IO]|
|DFFF 00           .BYTE $00       [IO]|
|E000 85 56        STA $56     [KERNAL]|
|E002 20 0F BC     JSR $BC0F   [KERNAL]|
|E005 A5 61        LDA $61     [KERNAL]|
|E007 C9 88        CMP #$88    [KERNAL]|
|E009 90 03        BCC $E00E   [KERNAL]|
|E00B 20 D4 BA     JSR $BAD4   [KERNAL]|
|E00E 20 CC BC     JSR $BCCC   [KERNAL]|
|E011 A5 07        LDA $07     [KERNAL]|
|E013 18           CLC         [KERNAL]|
|E014 69 81        ADC #$81    [KERNAL]|
|E016 F0 F3        BEQ $E00B   [KERNAL]|
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

### Binary View

Binary view shows each byte as eight bits, using `.` for a cleared bit and `*`
for a set bit. It is useful for inspecting registers, character glyphs, sprite
data and other bit-oriented memory.

Because C64 sprite data uses three bytes per row, binary view supports several
`W`idth modes for viewing bytes in different groupings.

The header shows the current byte address followed by the selected bit number,
for example `$DC00/7`. Bit 7 is the most significant bit on the left, and bit 0
is the least significant bit on the right. `Left` and `Right` move the cursor by
one bit and carry into the neighbouring byte.

Example:

```text
+--------------------------------------+
|MONITOR BIN $DC00/7                   |
|DC00 ........ 00                      |
|DC01 ******** FF                      |
|DC02 ******** FF                      |
|DC03 ........ 00                      |
|DC04 *.*..*.* A5                      |
|DC05 ...**.** 1B                      |
|DC06 ******** FF                      |
|DC07 ******** FF                      |
|DC08 ........ 00                      |
|DC09 ........ 00                      |
|DC0A ........ 00                      |
|DC0B *..*...* 91                      |
|DC0C ........ 00                      |
|DC0D *......* 81                      |
|DC0E .......* 01                      |
|DC0F ....*... 08                      |
|DC10 ........ 00                      |
|DC11 ******** FF                      |
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

### ASCII View

Use ASCII view when the bytes are intended to be printable ASCII rather than C64
screen codes. Rows are 32 bytes wide.

Behavior:

- Bytes `$20-$7E` are shown as their normal ASCII characters.
- All other bytes are shown as `.`.
- Typing a printable character in the range `$20-$7E` writes that character's
  byte value and moves the cursor right.
- Lowercase ASCII is preserved.

Example:

```text
+--------------------------------------+
|MONITOR ASC $A000                     |
|A000 .{.CBMBASIC0.A...................|
|A020 P........:..J.,.g.U.d...#....... |
|A040 U...j..}.....:Z.A.g.U.X...}...g. |
|A060 ....d.k......|.e..............g  |
|A080 yi.yR.{*.{...z.p..F..}...Z..d.EN |
|A0A0 .FO.NEX.DATA.INPUT.DIM.REA.LE    |
|A0C0 .GOT.RU.I.RESTOR.GOSU.RETUR.RE.S |
|A0E0 TO.O.WAI.LOA.SAVU.VERIF.DE.POK.PR|
|A100 INT.PRIN.CON.LIS.CLR.CM.SY.OPE.CL|
|A120 OS.GE.NE.TAB.T.F.SPC.THE.NO.STE. |
|A140 .....AN.O....SG.IN.AB.US.FR.PO.S |
|A160 Q.RN.LO.EX.CO.SI.TA.AT.PEE.LE.ST |
|A180 R.VA.AS.CHR.LEFT.RIGHT.MID.G..TO |
|A1A0 D.MANY FILE.FILE OPEN.FILE NOT OP|
|A1C0 E.FILE NOT FOUND.DEVICE NOT PRESE|
|A1E0 N.NOT INPUT FIL.NOT OUTPUT FIL.M |
|A200 ISSING FILE NAM.ILLEGAL DEVICE N |
|A220 UMBE.NEXT WITHOUT FO.SYNTA.RETUR |
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

### Screen View

Use Screen view when the bytes represent C64 screen codes, for example when
viewing screen RAM, which by default starts at `$0400`. Rows are 32 bytes wide.

Screen view is for screen-code bytes, not PETSCII text.

The header shows the active screen charset mode:

- `MONITOR SCR U/G $xxxx` for Uppercase/Graphics
- `MONITOR SCR L/U $xxxx` for Lowercase/Uppercase

The active mode is changed with `U`; see [View Modifiers](#view-modifiers).

Bit 7 of a byte is the reverse-video flag and is ignored for display, so `$81`
and `$01` show the same character.

Both modes display these codes the same way:

| Code | Shown as |
| --- | --- |
| `$00` | `@` |
| `$1B` | `[` |
| `$1C` | `#` |
| `$1D` | `]` |
| `$1E` | `^` |
| `$1F` | `<` |
| `$20-$3F` | the ASCII character of the same value |

The two modes differ in the letter ranges:

| Mode | Display | Typing |
| --- | --- | --- |
| `U/G` | `$01-$1A` as `A-Z` | `A-Z` and `a-z` write `$01-$1A` |
| `L/U` | `$01-$1A` as `a-z`, `$41-$5A` as `A-Z` | `a-z` writes `$01-$1A`, `A-Z` writes `$41-$5A` |

Typing in either mode also accepts:

- `@`, which writes `$00`
- `Space`, which writes `$20`
- the top-left `←` key, which writes `$1F`
- digits and punctuation in `$21-$3F`, which write their own value

Any other key is rejected and leaves memory unchanged.

Codes `$40-$5F` that have no letter mapping in the active mode are drawn with
readable fallback glyphs, and any remaining code is drawn as `.`.

Example:

```text
+--------------------------------------+
|MONITOR SCR U/G $0400                 |
|0400 @                                |
|0420           ***** COMMODORE 64 BA  |
|0440 SIC V3 *****                     |
|0460                         64K RAM  |
|0480  SYSTEM 38911 BASIC BYTES FREE   |
|04A0                                  |
|04C0             READY.               |
|04E0                                  |
|0500                                  |
|0520                                  |
|0540                                  |
|0560                                  |
|0580                                  |
|05A0                                  |
|05C0                                  |
|05E0                                  |
|0600                                  |
|0620                                  |
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

The monitor draws with the menu's own font rather than the C64 character set
the running program uses, so graphics codes appear as readable stand-in glyphs
rather than the exact shapes the C64 would display.

## View Modifiers

Some keys modify the current view instead of switching to another view.

### `U`: Undoc / Case

`U` is context-sensitive:

| View        | `U` behavior                                                     |
| ----------- | ---------------------------------------------------------------- |
| Assembly    | Toggles undocumented opcodes                                     |
| Screen      | Toggles the monitor-local screen charset between `U/G` and `L/U` |
| Other views | Shows `UNDOC IN ASM, CASE IN SCR`                                |

In Assembly view, enabling undocumented opcodes changes both decoding and
assembly completion. With them disabled, an undocumented opcode reads as `???`
and one byte; with them enabled, it decodes to its mnemonic and real length and
the opcode completion list offers it. The header shows `Undoc` while they are
enabled.

In Screen view, `U` changes only the monitor-local interpretation of screen
codes. It does not change the live C64 character set.

### `W`: Width Mode

`W` is view-dependent:

| View     | `W` behavior                         |
| -------- | ------------------------------------ |
| Memory   | Toggles between 8 and 16 bytes per row |
| Binary   | Cycles `1` -> `2` -> `3` -> `3S` -> `4` -> `1` |
| ASCII    | Fixed at 32 bytes per row; shows `WIDTH ONLY IN MEMORY/BINARY VIEW` |
| Screen   | Fixed at 32 bytes per row; shows the same message |
| Assembly | Variable, 1 to 3 bytes per row; shows the same message |

Binary width details:

- `1`, `2` and `3` show one, two or three bytes as separate bit fields with a
  trailing hex preview.
- `3S` shows three bytes as one continuous 24-bit sprite-style row, with a hex
  preview.
- `4` shows four bytes as one continuous 32-bit row without a trailing hex
  preview.

Changing the binary width resets the bit cursor to bit 7.

## Navigation and Context

- `Left` and `Right` move one byte, or one bit in Binary view.
- `Up` and `Down` move one row, or one instruction in Assembly view.
- `F1`, `F2` and `Shift+Space` page up. `F7` and `Space` page down.
- `J`: jump to an address.
- `G`: exit the monitor and execute from an address.
- `Enter`: in Assembly view, follow or return; see
  [Follow/Return](#followreturn).
- `O`: cycle the CPU port banking, `CPU0` through `CPU7`. On an Ultimate II+
  this shows `CPU BANK UNAVAILABLE`; there the monitor reads whatever the CPU
  currently sees.
- `Shift+O`: cycle the VIC bank override, `VIC0` through `VIC3`. Without
  VIC-bank support this shows `VIC BANK UNAVAILABLE`.
- `Z`: toggle freeze. Outside Overlay mode this shows
  `FREEZE ONLY IN OVERLAY MODE`, and where the machine has no freezer,
  `FREEZE UNAVAILABLE`.
- `P`: redraw the view periodically so live memory changes are visible. The
  refresh rate follows the machine's video format, 50 or 60 Hz. Over telnet
  this shows `POLL MODE UNAVAILABLE OVER TELNET`.

In edit mode `Space` is view-specific data entry and does not page.

Addresses in command prompts are hexadecimal and may carry a `$` prefix.

`G` prompts for an address, moves the cursor there, then closes the monitor,
releases the C64, and enters that address through an NMI trampoline in the
cassette buffer at `$033C`, restoring the previous `NMINV` vector before the
jump. On a target that cannot do this, `G` shows `GO UNAVAILABLE`.

### Assembly Baseline

Assembly view disassembles forward from the address it was last sent to. `J`,
`G`, a bookmark jump, a hunt or compare result, a return, a follow to a target
that is not already on screen, and an instruction assembled inline all set that
baseline. Moving with the cursor or the page keys does not, and neither does a
follow to a target that is already visible.

Two rules follow from it. No row may reach across the baseline. Everything above
it still disassembles normally; the exception is the one or two bytes
immediately in front of it, and only when they would decode as an instruction
whose last byte falls on or past the baseline. Those are shown as `.BYTE`,
because an instruction read from them would swallow the baseline row and shift
every row below it. Nothing further up is affected.

Moving up is measured rather than guessed: the view disassembles forward from
further back than the rows being moved over can span and counts the rows that
land on the current one, so moving back down retraces the same instruction
boundaries. Walking up from a baseline and back down therefore returns to it and
shows the same instructions it showed before.

### Follow/Return

Follow code flow in the Assembly view:

- `Enter` follows the resolved target when the cursor is on a jumpable
  instruction: `JMP` absolute, `JMP` indirect, `JSR`, and the branches `BPL`,
  `BMI`, `BVC`, `BVS`, `BCC`, `BCS`, `BNE` and `BEQ`. An indirect `JMP` is
  resolved by reading the vector it points at.
- `Enter` returns to the most recent saved source location when the current
  Assembly instruction is not jumpable and the follow stack is non-empty.
- The follow stack holds up to 10 return locations. When it is full, the oldest
  entry is discarded and the newest 10 are kept.
- After each successful follow or return, the footer shows a compact zero-based
  follow-stack status for about two seconds, for example `F1 JMP $E000` and
  `F0 RET $A000`.

A return restores the whole saved location, not just the address: the view, its
row width, the CPU bank, the VIC bank and the scroll position.

### Command prompt input

A command prompt refuses a character that could not appear in any command it
would accept. Nothing is inserted, the cursor does not move, and a pre-filled
template is left alone, so an impossible key has no effect at all rather than
producing an error after `Return`.

Anything that could still become a valid command stays typeable, so a partly
typed `0800-`, `PRG,` or `"unfinished text` is accepted while it is being
written. Range, value and length errors are still reported when the command is
submitted, because those depend on meaning rather than on spelling.

Every command prompt except `Hunt` opens with its field prefilled and the whole
field selected, so the first printable key, `DEL` or `CLEAR` replaces all of it.
The cursor keys move within the field without clearing it, and `Return` submits
whatever the field currently holds.

What is prefilled differs by prompt:

| Prompt | Prefill |
| --- | --- |
| `Jump`, `Fill`, `Transfer`, `Compare` | A placeholder template: `AAAA`, `AAAA-BBBB,DD`, `AAAA-BBBB,CCCC` |
| `Go` | The last address `G` was given, or the cursor address |
| `Load`, `Save` | The parameters last used, so `Return` repeats them. They start at `PRG,0000,AUTO` and `0800-9FFF` |
| `Hunt` | `0000-FFFF, ` with the cursor at the end and no template clearing, so the default range stays unless it is edited |

The placeholder templates are made of hex digits and would parse as real
addresses, so they are meant to be typed over rather than submitted.

Prompt errors are reported as `?ADDR`, `?SYNTAX`, `?VALUE` or `?RANGE`.

The `Save as` and `Label BM<n>` prompts are free text: they take any printable
character, uppercase typed letters, are prefilled with the previous value, and
edit it in place rather than clearing it on the first key.

### CPU and VIC Bank Display

The footer summarizes the selected CPU-visible memory configuration and VIC
bank, for example:

```text
CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000
```

`CPU0` through `CPU7` are shorthand for the three 6510 port memory-configuration
bits at `$0001`: `LORAM`, `HIRAM` and `CHAREN`.

In the normal no-cartridge configuration, the footer fields have these possible
values:

| Field | Address range | Values              |
| ----- | ------------- | ------------------- |
| `$A`  | `$A000-$BFFF` | `BAS`, `RAM`        |
| `$D`  | `$D000-$DFFF` | `I/O`, `CHR`, `RAM` |
| `$E`  | `$E000-$FFFF` | `KRN`, `RAM`        |

| Value | Meaning |
| ----- | ------- |
| `BAS` | BASIC ROM |
| `I/O` | I/O registers and Color RAM |
| `CHR` | Character generator ROM |
| `KRN` | KERNAL ROM |
| `RAM` | RAM |

`VIC0` through `VIC3` show the selected VIC bank, controlled through CIA 2 port
A at `$DD00`, with base address `$0000`, `$4000`, `$8000` or `$C000`. The
monitor tracks the live bank; it stops tracking once `Shift+O` overrides it, and
resumes when the live bank next matches the override.

Cartridges can further affect the CPU-visible memory map through the
expansion-port `GAME` and `EXROM` lines.

On targets that do not support one or both halves, the footer takes a different
form:

| Target capability | Footer |
| --- | --- |
| CPU banking and VIC bank | `CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000` |
| VIC bank only | `CPU VIEW  VIC0 $0000` |
| CPU banking only | `CPU7  VIC N/A` |
| Neither | `CPU VIEW  CPU BANK N/A  VIC N/A` |

`CPU VIEW` means the monitor shows the memory the CPU currently sees and cannot
select a different configuration. This is the case on the Ultimate II+, which
supports VIC-bank selection but not monitor-selected CPU banking.

## Editing

All five views support editing.

- `E`: enter edit mode.
- `C=+E`, `RUN/STOP`, `ESC` on a USB keyboard, or the top-left `←` key: leave
  edit mode. In ASCII and Screen views `←` types its character instead; use
  `RUN/STOP`, `ESC` or `C=+E` there.

Every edit is written to memory immediately. There is no separate commit step.

Edit behavior is view-specific:

| View     | Edit behavior                                                               |
| -------- | --------------------------------------------------------------------------- |
| Memory   | Type two hex nibbles to write one byte, then move right                     |
| ASCII    | Type a printable character to write its byte, then move right               |
| Screen   | Type a character using the active Screen charset mode, then move right      |
| Binary   | Type `0`, `.` or `Space` to clear the selected bit, `1` or `*` to set it, then move on by one bit |
| Assembly | Edit instructions inline; see [Assembly editing](#assembly-editing)         |

In Memory view the first hex nibble typed is held as the high nibble of the byte
and is not written until the second one arrives. Moving the cursor writes a
half-typed nibble as the whole byte value.

In Memory and Binary edit mode, a key that is not data for that view is still
dispatched as a monitor command. `M`, `I`, `A`, `V` and `B` switch views without
leaving edit mode, and `X` leaves the monitor. In ASCII and Screen edit mode
every printable key is data instead.

`DEL` is logical delete, not raw backspace:

| View         | `DEL` behavior                                  |
| ------------ | ----------------------------------------------- |
| Memory       | Writes `$00` and moves right                    |
| ASCII/Screen | Writes a space (`$20`) and moves left           |
| Binary       | Clears the selected bit and moves on by one bit |
| Assembly     | Replaces the current instruction with `NOP` bytes and steps to the next line |

`DEL` performs these same edits outside edit mode as well.

Two undo steps come before the logical delete in edit mode:

- In Memory view, `DEL` first discards a half-typed nibble on the current byte
  and leaves the byte itself alone.
- In Assembly view, `DEL` first undoes the last typed character on the current
  line, restoring the byte it overwrote. Up to 16 such steps are remembered per
  instruction; older ones are discarded. Only once nothing has been typed on the
  current instruction does `DEL` fall through to the `NOP` replacement.

### Assembly editing

An instruction is edited in parts. Part 0 is the mnemonic. Parts 1 and up are
the operand bytes in display order, so for a three-byte instruction part 1 is
the high byte and part 2 the low byte. A two-byte branch has three parts: the
mnemonic and the two bytes of its absolute target address, which the monitor
converts to and from the signed offset stored in memory. A branch edit that
would put the target out of the `-128..127` range is refused and memory is left
unchanged.

Keys in Assembly edit mode:

| Key | Effect |
| --- | --- |
| `A`-`Z` on part 0 | Opens the opcode completion list, seeded with that letter. A letter no supported mnemonic starts with is rejected and does nothing |
| Hex digit on part 1 and up | Writes one nibble of that operand byte, then advances to the next nibble, part, or line |
| `Left` / `Right` | Move one part back or forward, crossing into the neighbouring instruction at the ends |
| `Up` / `Down` | Move one instruction, back to part 0 |
| `Return` | Advance to the next instruction |

The opcode completion list takes these keys:

| Key | Effect |
| --- | --- |
| Letters | Extend the mnemonic, up to three characters. A character that would not keep the mnemonic valid is rejected |
| `0`-`9`, `A`-`F`, `X`, `Y`, `#`, `$`, `(`, `)`, `,` | Once the mnemonic is complete, build an operand directly, for example `LDA #FF` or `LDA (10),Y` |
| `Up` / `Down` | Select an addressing-mode candidate |
| `Return` | Assemble the typed operand, or commit the selected candidate when no operand has been typed. A typed operand that does not assemble leaves the list open |
| `Space` | Assemble the typed operand, if there is one |
| `DEL` | Remove the last operand character, then the last mnemonic character, then close the list |
| `RUN/STOP`, `ESC`, `←` | Close the list without assembling |

## Selection and Clipboard

- Copy the current byte with `C=+C`.
- Paste the clipboard at the cursor with `C=+V`. The cursor then advances by the
  number of bytes pasted.
- Toggle range mode with `R`.

Range mode anchors the current address, and the header shows `Range`. The
selected span runs between the anchor and the cursor and includes both, in
either direction. In Assembly view the span is widened to whole instructions.

While range mode is active:

- `C=+C` copies the selected span and leaves range mode.
- Pressing `R` again also copies the selected span and leaves range mode.

`R` is available outside edit mode in every view. In edit mode it works in
Memory and Binary view, and in Assembly view only while the cursor is on an
operand part; in ASCII and Screen edit mode it types its character instead.

A copy that cannot allocate memory shows `?MEM` and leaves range mode on. A
paste with an empty clipboard shows `?CLIP`.

## Number Tool

Open the Number tool with `N`. It is a base-conversion and overwrite popup for
the byte or word at the current target, and shows the same value in five rows:

```text
+----------------------------+
|MONITOR NUM $0400 BYTE      |
|Hex      $01                |
|Decimal  1                  |
|Binary   %00000001          |
|ASCII    .                  |
|Screen   A                  |
|Calc with +-*/ $Hex Dec %Bin|
+----------------------------+
```

The header names the target address and whether one byte or two are in play.

The target is normally the byte at the cursor. In Assembly view it is instead
the operand bytes of the current instruction: one byte for a one-byte operand,
two for a two-byte operand, in which case the popup works on a word. An
instruction with no operand targets the byte immediately after it. In those
cases the width is fixed and cannot be widened by typing.

Popup keys:

| Key | Effect |
| --- | --- |
| `Up` / `Down` | Select a row. Changing rows discards anything typed on the previous one |
| Digits and characters | Type a new value in the selected row's base. Only input that parses in that base is accepted |
| `DEL` | Remove the last typed character |
| `Return` | Write the previewed value to the target |
| `C=+C` | Copy the previewed value to the clipboard and close the popup |
| `+`, `-`, `*`, `/` | Open the calculator |
| `RUN/STOP`, `ESC`, `←` | Close the popup |
| `C=+O` | Close the popup and leave the monitor |

Input limits per row are 4 hex digits, 5 decimal digits, 16 binary digits, and
2 characters on the ASCII and Screen rows. Typing more than one byte's worth
widens the target to a word, unless the width is fixed by an Assembly operand.

The ASCII and Screen rows use the same mappings as the ASCII and Screen views,
including the top-left `←` key, which types its value on those two rows instead
of closing the popup.

### Calculator

In the Number popup, press `+`, `-`, `*` or `/` to open the calculator. The
expression is initialized with the current value, written in the selected row's
base (hex for the ASCII and Screen rows), followed by the operator pressed. The
bottom line of the popup becomes `Expr=` and shows what has been typed.

Press `Return` or `=` to evaluate the expression. Press `RUN/STOP`, `ESC` or `←`
to leave the expression and return to the Number popup. On success the popup
returns to the conversion layout and refreshes all rows with the result. On
failure the bottom line shows a short status instead:

| Status | Meaning |
| --- | --- |
| `SYNTAX` | The expression does not parse |
| `DIV/0` | Division by zero |
| `RANGE` | A subtraction that would go negative, an intermediate overflow, or a result too large for the target width |

Expressions may contain one or more values separated by `+`, `-`, `*` and `/`.
`*` and `/` bind more tightly than `+` and `-`. Division is unsigned integer
division. Intermediate values are computed in 32 bits; the final result must fit
the target width, which is `$FF` for a byte target and `$FFFF` for a word.

Values are written as `$` followed by up to four hex digits, `%` followed by
binary digits, or plain decimal digits. A value with no prefix is decimal.

Examples:

```text
42
$1000+4
$2000/16
%1010*3
1+2/3
2+3*4
```

Formal EBNF grammar:

```ebnf
expr     = term, { ("+" | "-"), term } ;
term     = value, { ("*" | "/"), value } ;
value    = hex | decimal | binary ;

hex      = "$", hex_digits ;
decimal  = decimal_digits ;
binary   = "%", binary_digits ;
```

## Memory Operations

The monitor includes four bulk memory commands:

| Key | Command  | Prompt title | Syntax | Result |
| --- | -------- | ------------ | ------ | ------ |
| `F` | Fill     | `Fill AAAA-BBBB,DD` | `start-end,value` | Fill the range with one byte |
| `T` | Transfer | `Transfer AAAA-BBBB,CCCC` | `start-end,dest` | Copy the range to a destination, overlap-safe in either direction |
| `C` | Compare  | `Compare AAAA-BBBB,CCCC` | `start-end,dest` | Compare the range against another location and list differing addresses |
| `H` | Hunt     | `Hunt AAAA-BBBB,BB/"text"` | `start-end,bytes` or `start-end,"text"` | Search the range for a byte sequence or quoted ASCII string |

Every range includes both of its ends, here and in `Save`: `C000-CFFF` is the
4096 bytes from `$C000` to `$CFFF`, and `C000-C000` is the single byte at
`$C000`. `0000-FFFF` is all 65536 bytes. An end below the start is refused with
`?RANGE`.

Every address and value is hexadecimal and may carry a `$` prefix, and spaces
around the fields are ignored. `Hunt` bytes are written as hex pairs separated
by spaces or run together; quoted text keeps the case it was typed in, while
everything outside the quotes is normalised to upper case. A hunt needle may be
up to 80 bytes long.

`Fill` and `Transfer` report nothing on success. `Compare` shows
`No differences` when the two regions match, and `Hunt` shows `No matches` when
the needle is not found. Otherwise both open a result picker:

| Key | Effect |
| --- | --- |
| `Up` / `Down` | Select a result |
| `Page Up` / `Page Down` | Move a screen at a time |
| `Home` / `End` | First or last result |
| `Return` | Jump to the selected match |
| `RUN/STOP`, `ESC`, `←` | Close the picker |
| `C=+O` | Close the picker and leave the monitor |

The picker holds at most 256 results; any beyond that are not listed. Its header
reads `Hunt results` or `Compare results` followed by the selected position and
the total.

## File I/O

- `L`: load a file into memory.
- `S`: save memory to a file.

Files may exist directly in the Ultimate filesystem or inside a disk image such
as a `.D64`. The picker reopens in the directory it was last left in.

In both pickers, `Right` descends into a directory or disk image, `Left` goes up
one level and exits the picker at the root, and `RUN/STOP` or `ESC` cancels.

### Load

Load is a two-step flow:

1. Pick a file, from the `MONITOR LOAD: Select File` browser.
2. Enter load parameters, at the `Load [PRG|AAAA],[Offs],[Len|AUTO]` prompt.

In the file picker, select an existing file by pressing `ENTER` on it, then
choosing `Select` from the context menu.

Load syntax:

```text
[PRG|AAAA],[Offset],[Len|AUTO]
```

The prompt is prefilled with the values last used, which start as:

```text
PRG,0000,AUTO
```

This loads the whole file to the start address stored in its first two bytes.

Fields:

| Field           | Meaning                                                                         |
| --------------- | ------------------------------------------------------------------------------- |
| `PRG` or `AAAA` | Use the two-byte load address from the PRG file, or load to an explicit address |
| `Offset`        | Number of bytes to skip. With `PRG` this counts from after the two-byte header; with an explicit address it counts from the start of the file |
| `Len` or `AUTO` | Load the rest of the file from the offset, or load an explicit byte count       |

Every field is optional, individually and as a trailing omission, so `,,0010`
loads `$10` bytes from offset 0 to the PRG's own address.

Examples:

| Input            | Meaning                                           |
| ---------------- | ------------------------------------------------- |
| `PRG`            | Load a PRG to its embedded load address           |
| `0801`           | Load to `$0801`                                   |
| `PRG,1000`       | Skip `$1000` bytes after the PRG header           |
| `0801,0002,0010` | Load `$0010` bytes from offset `$0002` to `$0801` |

An explicit length larger than what the file can supply from the offset is
refused rather than truncated. Errors are reported by popup: `OPEN FAILED`,
`NOT A PRG`, `READ FAILED`, `SEEK FAILED`, `LOAD TOO LARGE (>64K)`,
`LOAD WRAPS PAST $FFFF`, or one of the `?ADDR` / `?SYNTAX` / `?VALUE` /
`?RANGE` parse errors.

A load to `$0801` also rewrites the BASIC runtime pointers `VARTAB` (`$2D`),
`ARYTAB` (`$2F`), `STREND` (`$31`) and the last-load address (`$AE`) to the end
of the loaded data, so `RUN` and `LIST` work on the freshly loaded program.

On success a confirmation popup shows the filename, the address range written
and the byte count.

### Save

Save is a two-step flow:

1. Enter the byte range to save, at the `Save AAAA-BBBB` prompt. The prompt is
   prefilled with the range last used, which starts as `0800-9FFF`.
2. Pick or create the destination file, in the `MONITOR SAVE: Pick File/Dir`
   browser.

The range includes both ends. Save writes a normal PRG file with a two-byte
little-endian load address header, so the file loads back to the same address.

In the file picker, choose one of the following:

- Select an existing file by pressing `ENTER` on it, then choosing `Select` from
  the context menu. The file is overwritten after a
  `File already exists. Overwrite?` confirmation; declining reports
  `CREATE FAILED`.
- Create a new file by pressing `ENTER` or `Right` on `<< Create New File >>`,
  which appears at the top of the listing when the current directory is
  writable. A `Save as` prompt then asks for the filename, prefilled with the
  name last saved.

Errors are reported by popup: `CREATE FAILED` or `WRITE FAILED`. On success a
confirmation popup shows the filename, the address range and the byte count.

## Bookmarks

The monitor has 10 bookmark slots, persisted in the device configuration.

- List bookmarks with `C=+B`.
- Jump directly to a slot with `C=+0` through `C=+9`.

Both work from view mode and from edit mode. They are ignored while help, a
result picker, the opcode list, the Number popup or the Bookmarks popup is up;
from help they close help instead.

Each bookmark stores:

- Label, up to 6 characters
- Address
- View ID
- View width or width mode, where the view has one
- CPU bank
- VIC bank

Restoring a bookmark restores all of these. On an Ultimate II+, where the
monitor cannot select the CPU bank, a bookmark whose CPU bank differs from the
current one cannot be restored and the footer reports `RESTORE FAILED`.

Bookmark popup controls:

| Key         | Action                                            |
| ----------- | ------------------------------------------------- |
| `Up`/`Down` | Select a slot                                     |
| `Return`    | Restore the selected slot                         |
| `S`         | Store the current location into the selected slot, keeping its label |
| `L`         | Edit the label, at a `Label BM<n>` prompt         |
| `DEL`       | Reset the slot to its default, using the current CPU and VIC bank |
| `0`-`9`     | Restore that slot directly                        |
| `C=+B`, `RUN/STOP`, `ESC`, `←`, `?`, `F3` | Close the popup             |

A label is normalised when it is saved: it is folded to upper case, characters
outside `A-Z`, `0-9`, `_` and `-` become `_`, spaces are dropped, and an empty
label becomes `USER`.

Default slots are aimed at common C64 locations:

```text
+--------------------------------------+
|BOOKMARKS                             |
|                                      |
|0 ZP     $0000 HEX  8 CPU7 VIC0       |
|1 SCREEN $0400 SCR 32 CPU7 VIC0       |
|2 BASIC  $0801 ASM    CPU7 VIC0       |
|3 BASROM $A000 ASM    CPU7 VIC0       |
|4 HIRAM  $C000 ASM    CPU7 VIC0       |
|5 VIC    $D000 HEX  8 CPU7 VIC0       |
|6 SID    $D400 HEX  8 CPU7 VIC0       |
|7 CIA1   $DC00 BIN  1 CPU7 VIC0       |
|8 CIA2   $DD00 BIN  1 CPU7 VIC0       |
|9 KERNAL $E000 ASM    CPU7 VIC0       |
|                                      |
|0-9/RET Jmp  S Set  L Label  DEL Reset|
+--------------------------------------+
```

## Interface Modes

`Interface Type` decides where the menu, and so the monitor, is drawn.

| Mode | Use it when |
| --- | --- |
| Freeze | The monitor has to appear in the C64's own video output, for example to capture it |
| Overlay on HDMI | You want `P` poll mode to watch memory change, or the `Z` freeze toggle |

To change it, leave the monitor, press `C=+I` in the file browser or the
settings menu, then reopen the monitor. `C=+I` does nothing inside the
monitor.
