# Machine Code Monitor

The Machine Code Monitor is a keyboard-driven tool for inspecting and editing live or frozen C64 memory. 

It supports hexadecimal, ASCII, screen-code, binary, and assembly views, plus inline editing, bulk memory operations, file load/save, and execution from a selected address.

## Entry and Exit

`C=` denotes the Commodore key. For example, `C=+O` means: hold the Commodore key, then press `O`.

To open the monitor, use one of the following:

- Press `C=+O`.
- Press `F5`, open `Developer`, then select `Machine Code Monitor`.

`C=+O` also closes the monitor from the memory views and from the Number,
Hunt-result and opcode popups. It is not read while a command prompt, the
Number calculator or the Bookmarks popup is open; leave those with Back first.

Open the built-in help with `?` or with the mapped help key, which is `F3` in
the default key mapping. `?`, the help key, `RUN/STOP` and `←` all close help
again without leaving the monitor. Any other command key closes help and then
runs that command.

### Back

`RUN/STOP` and the C64's top-left `←` key are the same Back action, and each
press removes exactly one active interaction layer:

| What is active | What Back does |
| --- | --- |
| Help | Closes help |
| A number expression | Returns to the Number popup |
| A popup: Number, Bookmarks, opcode completion, Hunt or Compare results | Closes that popup |
| A command prompt | Cancels the prompt |
| Inline edit mode | Leaves edit mode |
| Nothing of the above | Leaves the monitor |

There is one exception. Where `←` is valid data it stays data: in ASCII and
Screen edit mode it types its character, and on the ASCII and Screen rows of
the Number popup it types its value. `RUN/STOP` is still Back in those places.

## Screen Layout

The monitor screen has three fixed regions:

### Header

- Shows the current view, cursor address, and active modes.
- Mode indicators may include `Undoc`, `Frz`, `Poll`, or `EDIT`.

### Body

- Shows the memory region around the current cursor address.
- The active cursor position is highlighted in reverse.
- May show popups, such as search results, load/save prompts, completion pickers, or bookmarks.

### Footer

- Shows the active CPU port mapping and VIC bank. For more details, see [CPU and VIC Bank Display](#cpu-and-vic-bank-display)
- `CPU0`..`CPU7` identify the selected CPU memory configuration.
- `VIC0`..`VIC3` identify the selected VIC bank and its base address.
- When jumping to a bookmark, the footer briefly shows bookmark information.
- On a target without monitor-selected CPU banking, such as the Ultimate II+,
  the footer reads `CPU VIEW` instead of `CPU0`..`CPU7`: the monitor shows the
  memory the CPU currently sees and cannot select a different configuration.
  VIC-bank selection remains available there, so the `VIC0`..`VIC3` half of the
  footer is unchanged.
- A target without VIC-bank support shows `VIC N/A` in place of `VIC0`..`VIC3`,
  and one with neither shows `CPU VIEW  CPU BANK N/A  VIC N/A`.

Example layout:

```text
+--------------------------------------+
|MONITOR ASM $E011  Undoc Frz Poll EDIT|
|...                                   |
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

## Views

The monitor provides five primary views:

| Key | View     | ID  | Purpose                         |
| --- | -------- | --- | ------------------------------- |
| `M` | Memory   | HEX | Hexadecimal byte view           |
| `A`, `D` | Assembly | ASM | Disassembly and inline assembly |
| `B` | Binary   | BIN | Bit-level byte view             |
| `I` | ASCII    | ASC | ASCII byte view                 |
| `V` | Screen   | SCR | Screen code view                |

### Memory / Hex View

Memory view shows raw bytes in hexadecimal together with a compact printable-character preview.

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

Assembly view shows decoded 6510 instructions, their instruction bytes, and the memory source used for each row.

### I/O is data, not code

While I/O is banked in, `$D000-$DFFF` is not memory: each read returns whatever
the register holds at that instant, and the same address answers differently
from one read to the next. Decoding those bytes as instructions would give a
different instruction, and a different instruction length, on every redraw, and
every row below would move with it while you were only scrolling.

Assembly view therefore shows one row per address there, as `.BYTE $xx`, with
the source marked `[IO]`. The rows stay where they are and each one shows what
its register holds now.

This follows what the address reads rather than where it is: with CHAR ROM or
RAM banked into `$D000-$DFFF` instead, those addresses hold stable bytes that
may well be code, and are disassembled normally. Every other region is always
disassembled.

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

Binary view shows each byte as eight bits, using `.` for a cleared bit and `*` for a set bit. It is useful for inspecting registers, character glyphs, sprite data, and other bit-oriented memory.

Because C64 sprite data uses 3 bytes per row, binary view supports multiple `W`idth modes for viewing bytes in different groupings.

The top status line shows the current byte address followed by the selected bit number, for example `$DC00/7`. Bit 7 is the most significant bit on the left, and bit 0 is the least significant bit on the right.

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

Use ASCII view when the bytes are intended to be printable ASCII rather than C64 screen codes.

Behavior:

- Bytes `$20-$7E` are shown as their normal ASCII characters.
- All other bytes are shown as `.`.
- Typing a printable ASCII character writes that character's byte value.
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

Use Screen view when the bytes represent C64 screen codes, for example when viewing screen RAM, which by default starts at `$0400`.

Screen view is for screen-code bytes, not PETSCII text.

The header shows the active screen charset mode:

- `MONITOR SCR U/G $xxxx` for **Uppercase/Graphics**
- `MONITOR SCR L/U $xxxx` for **Lowercase/Uppercase**

The active mode is changed with `U`; see [View Modifiers](#view-modifiers).

#### Screen `U/G`

- Displays `$00` as `@`.
- Displays `$01-$1A` as `A-Z`.
- Typing `A-Z` or `a-z` writes `$01-$1A`.

#### Screen `L/U`

- Displays `$01-$1A` as `a-z`.
- Displays `$41-$5A` as `A-Z`.
- Typing `a-z` writes `$01-$1A`.
- Typing `A-Z` writes `$41-$5A`.

Example:

```text
+--------------------------------------+
|MONITOR SCR U/G $0400                 |
|0400 █                                |
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

Because the monitor is rendered with the firmware UI font rather than the live C64 character set, graphics bytes are shown with readable fallback glyphs instead of exact C64 glyph shapes.

## View Modifiers

Some keys modify the current view instead of switching to another view.

### `U`: View-Specific Toggle

`U` is context-sensitive:

| View        | `U` behavior                                                     |
| ----------- | ---------------------------------------------------------------- |
| Assembly    | Toggles undocumented opcodes                                     |
| Screen      | Toggles the monitor-local screen charset between `U/G` and `L/U` |
| Other views | Shows `UNDOC IN ASM, CASE IN SCR`                                |

In Assembly view, enabling undocumented opcodes affects how bytes are decoded and how assembly completion behaves.

In Screen view, `U` changes only the monitor-local interpretation of screen codes. It does not change the live C64 character set.

### `W`: Width Mode

`W` is view-dependent:

| View     | `W` behavior                         |
| -------- | ------------------------------------ |
| Memory   | Cycles `8 <-> 16` bytes per row      |
| Binary   | Cycles `1 -> 2 -> 3 -> 3S -> 4 -> 1` |
| ASCII    | Fixed-width, 32 bytes per row; shows `WIDTH ONLY IN MEMORY/BINARY VIEW` |
| Screen   | Fixed-width, 32 bytes per row; shows the same message |
| Assembly | Variable-width, 1 to 3 bytes; shows the same message |

Binary width details:

- `1`, `2`, and `3` show one, two, or three bytes as bit fields with a trailing hex preview.
- `3S` shows three bytes as one continuous 24-bit sprite-style row, with a hex preview.
- `4` shows four bytes as one continuous 32-bit row without a trailing hex preview.

## Navigation and Context

- `J`: jump to an address.
- `G`: exit the monitor and execute from an address.
- Use the configured page-up/page-down keys, `Shift+Space`, or `Space` to page.
- `Enter`: in Assembly view, follow the target of a jumpable instruction, or return to the most recent saved source location when the current instruction is not jumpable and the follow stack is non-empty.

Assembly view disassembles forward from the address it was last sent to. `J`,
`G`, a bookmark jump, a hunt result and a follow or return all set that
baseline; moving with the cursor or the page keys does not.

Two rules follow from it. No row may reach across the baseline. Everything
above it still disassembles normally; the exception is the one or two bytes
immediately in front of it, and only when they would decode as an instruction
whose last byte falls on or past the baseline. Those are shown as `.BYTE`,
because an instruction read from them would swallow the baseline row and shift
every row below it. Nothing further up is affected. And moving up is
measured rather than guessed: the view disassembles forward from further back
than the rows being moved over can span and counts the rows that land on the
current one, so moving back down retraces the same instruction boundaries.
Walking up from a baseline and back down therefore returns to it and shows the
same instructions it showed before.
- `O`: cycle CPU port banking, `CPU0`..`CPU7`. On a target without
  monitor-selected CPU banking this shows `CPU BANK UNAVAILABLE`.
- `Shift+O`: cycle the VIC bank override. Without VIC-bank support this shows
  `VIC BANK UNAVAILABLE`.
- `Z`: toggle freeze. Outside Overlay mode this shows
  `FREEZE ONLY IN OVERLAY MODE`, and on a target with no freezer at all,
  `FREEZE UNAVAILABLE`.
- `P`: toggle poll mode in the local monitor. Over telnet this shows
  `POLL MODE UNAVAILABLE OVER TELNET`.

Addresses in command prompts are hexadecimal.

### Command prompt input

A command prompt refuses a character that could not appear in any command it
would accept. Nothing is inserted, the cursor does not move, and a pre-filled
template is left alone, so an impossible key has no effect at all rather than
producing an error after `Return`.

Anything that could still become a valid command stays typeable, so a partly
typed `0800-`, `PRG,` or `"unfinished text` is accepted while it is being
written. Range, value and length errors are still reported when the command is
submitted, because those depend on meaning rather than on spelling.

`Save as` filenames and bookmark labels are free text and take any printable
character.

### Follow/Return

Follow code flow in the Assembly view:

- `Enter` follows the resolved target when the cursor is on a jumpable instruction such as `JMP`, `JSR`, `BEQ`, `BNE`, `BCC`, `BCS`, `BMI`, `BPL`, `BVC`, or `BVS`.
- `Enter` returns to the most recent saved source location when the current Assembly instruction is not jumpable and the follow stack is non-empty.
- The follow stack holds up to 10 return locations. When it is full, the oldest entry is discarded and the newest 10 are kept.
- After each successful follow or return, the bottom row shows a compact zero-based follow-stack status for about 2 seconds, for example `F1 JMP $E000` and `F0 RET $A000`.

### CPU and VIC Bank Display

The footer summarizes the selected CPU-visible memory configuration and VIC bank, for example:

```text
CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000
```

`CPU0`..`CPU7` are shorthand for the three 6510 port memory-configuration bits at `$0001`: `LORAM`, `HIRAM`, and `CHAREN`.

In the normal no-cartridge configuration, the footer fields have these possible values:

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

`VIC0`..`VIC3` show the selected VIC bank controlled through CIA 2 port A at `$DD00`, with base address `$0000`, `$4000`, `$8000`, or `$C000`.

Cartridges can further affect the CPU-visible memory map through the expansion-port `GAME` and `EXROM` lines.

## Editing

All views support editing:

- `E`: enter edit mode.
- `C=+E`, `RUN/STOP`, or the top-left `←` key: leave edit mode. In ASCII and Screen views `←` enters its character instead; use `RUN/STOP` or `C=+E` there.

Edit behavior is view-specific:

| View     | Edit behavior                                                               |
| -------- | --------------------------------------------------------------------------- |
| Memory   | Type two hex nibbles to write one byte                                      |
| ASCII    | Type printable ASCII characters directly                                    |
| Screen   | Type screen characters using the active Screen charset mode                 |
| Binary   | Type `0`, `.` or `Space` to clear the selected bit; type `1` or `*` to set it |
| Assembly | Edit instructions inline with mnemonic completion and direct operand typing |

In edit mode, `Space` remains view-specific data entry and does not page.

`DEL` is logical delete, not raw backspace:

| View         | `DEL` behavior                                  |
| ------------ | ----------------------------------------------- |
| Memory       | Writes `$00` and moves right                    |
| ASCII/Screen | Writes a space and moves left                   |
| Binary       | Clears the selected bit                         |
| Assembly     | Replaces the current instruction with `NOP` bytes |

In Assembly view, if an inline edit is already active, `DEL` first cancels the current line edit state.

## Selection and Clipboard

- Copy the current byte with `C=+C`.
- Paste the clipboard at the cursor with `C=+V`.
- Toggle range mode with `R`.

Range mode anchors the current address. The selected span runs from the anchor address to the current cursor address, inclusive.

While range mode is active:

- `C=+C` copies the selected span.
- Pressing `R` again also copies the selected span and exits range mode.

## Number Tool

- Open the number tool with `N`.

The number tool is a compact base-conversion and overwrite popup for the current target. It shows the same value in these forms:

- Hex
- Decimal
- Binary
- ASCII
- Screen code

In Assembly view, the number tool targets the operand bytes of the current instruction when possible.

The ASCII and Screen rows in the number tool use the same mappings as the ASCII and Screen views, including the top-left `←` key, which types its character on those two rows instead of closing the popup.

### Calculator

In the Number popup, press `+`, `-`, `*`, or `/` to open the calculator. The expression is initialized with the current value and the selected operator.

Press `Return` or `=` to evaluate the expression. Press `RUN/STOP` or `←` to leave the expression and return to the Number popup. On success, the popup returns to the compact conversion layout and refreshes all rows with the result.

Expressions may contain one or more values separated by `+`, `-`, `*`, or `/`.  * and / are evaluated before + and -. Division is unsigned integer division and truncates toward zero.

Examples:

```text
42
$1000+4
$2000/16
%1010*3
1+2/3
2+3*4
````

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

The monitor includes direct bulk memory commands:

| Key | Command  | Syntax                                   | Result                                                                |
| --- | -------- | ---------------------------------------- | --------------------------------------------------------------------- |
| `F` | Fill     | `start-end,value`                        | Fill an inclusive range with one byte                                 |
| `T` | Transfer | `start-end,dest`                         | Copy `end - start` bytes to a destination. Unlike Fill, Hunt and Save, the byte at `end` is not included |
| `C` | Compare  | `start-end,dest`                         | Compare `end - start` bytes against another location and list differing addresses, on the same exclusive end |
| `H` | Hunt     | `start-end,bytes` or `start-end,"text"` | Search for a byte sequence or quoted ASCII string                     |

Every address and value is hexadecimal and may carry a `$` prefix, and spaces
around the fields are ignored. `Hunt` bytes are written as hex pairs separated
by spaces or run together; quoted text keeps the case it was typed in, while
everything outside the quotes is normalised to upper case.

`Hunt` and `Compare` open a result picker:

- `Return`: jump to the selected match.
- `RUN/STOP` or `←`: close the picker.

## File I/O

- `L`: load a file into memory.
- `S`: save memory to a file.

Files may exist directly in the Ultimate filesystem or inside a disk image such as `.D64`.

### Load

Load is a two-step flow:

1. Pick a file.
2. Enter load parameters.

In the file picker, select an existing file by pressing `ENTER` on it, then choosing `Select` from the context-sensitive menu.

Load syntax:

```text
[PRG|AAAA],[Offset],[Len|AUTO]
````

Default:

```text
PRG,0000,AUTO
```

This loads the whole file to the start address stored in its first two bytes.

Fields:

| Field           | Meaning                                                                         |
| --------------- | ------------------------------------------------------------------------------- |
| `PRG` or `AAAA` | Use the two-byte load address from the PRG file, or load to an explicit address |
| `Offset`        | Number of bytes to skip after the PRG header                                    |
| `Len` or `AUTO` | Load the automatically determined length, or load an explicit byte count        |

Examples:

| Input            | Meaning                                           |
| ---------------- | ------------------------------------------------- |
| `PRG`            | Load a PRG to its embedded load address           |
| `0801`           | Load to `$0801`                                   |
| `PRG,1000`       | Skip `$1000` bytes after the PRG header           |
| `0801,0002,0010` | Load `$0010` bytes from offset `$0002` to `$0801` |

### Save

Save is a two-step flow:

1. Enter the byte range to save.
2. Pick or create the destination file.

Save syntax:

```text
0800-9FFF
```

The range is inclusive. Save writes a normal PRG file with a two-byte load address header.

In the file picker, choose one of the following:

- Select an existing file by pressing `ENTER` on it, then choosing `Select` from the context-sensitive menu.
- Create a new file by selecting `<< Create new file >>`.

## Bookmarks

The monitor has 10 persistent bookmark slots.

- List bookmarks with `C=+B`.
- Jump directly to a slot with `C=+0` .. `C=+9`.

Each bookmark stores:

- Label
- Address
- View ID
- View width or width mode where applicable
- CPU bank
- VIC bank

Bookmark popup controls:

| Key         | Action                                            |
| ----------- | ------------------------------------------------- |
| `Up`/`Down` | Select a slot                                     |
| `Return`    | Restore the selected slot                         |
| `S`         | Store the current location into the selected slot |
| `L`         | Edit the label                                    |
| `DEL`       | Reset the slot to its default                     |
| `0`..`9`    | Jump directly to that slot                        |
| `RUN/STOP` or `←` | Close the popup                             |

Default slots are aimed at common C64 locations:

```text
+--------------------------------------+
|BOOKMARKS                             |
|                                      |
|0 ZERO    $0000 HEX  8 CPU7 VIC0      |
|1 SCREEN  $0400 SCR 32 CPU7 VIC0      |
|2 BASIC   $0801 ASM    CPU7 VIC0      |
|3 BASROM  $A000 ASM    CPU7 VIC0      |
|4 HIRAM   $C000 ASM    CPU7 VIC0      |
|5 VIC     $D000 HEX  8 CPU7 VIC0      |
|6 SID     $D400 HEX  8 CPU7 VIC0      |
|7 CIA1    $DC00 BIN  1 CPU7 VIC0      |
|8 CIA2    $DD00 BIN  1 CPU7 VIC0      |
|9 KERNAL  $E000 ASM    CPU7 VIC0      |
|                                      |
|0-9/RET Jmp  S Set  L Label  DEL Reset|
+--------------------------------------+
```

## Additional Notes

Use **UI Freeze** mode when the monitor output must be captured in the video stream.

Use **UI Overlay on HDMI** mode when polling is needed to observe live changes.

To switch between UI Freeze and UI Overlay modes:

1. Exit the monitor.
2. Press `C=+I`.
3. Reopen the monitor.
