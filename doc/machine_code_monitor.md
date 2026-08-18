# Machine Code Monitor

The Machine Code Monitor is a keyboard-driven tool for inspecting and editing live or frozen C64 memory. 

It supports hexadecimal, ASCII, screen-code, binary, and assembly views, plus inline editing, bulk memory operations, file load/save, and execution from a selected address.

## Entry and Exit

`C=` denotes the Commodore key. For example, `C=+O` means: hold the Commodore key, then press `O`.

To open the monitor, use one of the following:

- Press `C=+O`.
- Press `F5`, open `Developer`, then select `Machine Code Monitor`.

Open the built-in help with `F3` or `?`.

To close the monitor:

- Press `C=+O` again.
- Press `RUN/STOP`, `ESC`, or the C64's top-left `←` key when no edit operation or popup is active.

`RUN/STOP`, `ESC` and `←` are one Back action. Each press closes one active layer - help, a number expression, a popup, a command prompt, edit mode - and closes the monitor only once nothing is left. Where `←` is data, in ASCII and Screen editing and in the ASCII and Screen rows of the Number popup, use `RUN/STOP` or `ESC` instead.

Two shortcuts act on the machine rather than the view, and both work from a memory view and from edit mode:

| Key | Action |
| --- | ------ |
| `C=+R` | Reset the C64. This is the same action as the task menu's `Reset C64`, so the on-device menu closes with the machine's screen where the interface is drawn there. |
| `C=+I` | Swap the interface between the freeze menu and the HDMI overlay, and close the menu. The setting takes effect the next time the menu opens. |

Neither has a confirmation. A backend that cannot reach a reset reports `RESET UNAVAILABLE` and leaves the machine, the view and edit mode unchanged.

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
| `A` | Assembly | ASM | Disassembly and inline assembly |
| `B` | Binary   | BIN | Bit-level byte view             |
| `I` | ASCII    | ASC | ASCII byte view                 |
| `V` | Screen   | SCR | Screen code view                |

### Memory / Hex View

Memory view shows raw bytes in hexadecimal together with a compact printable-character preview.

Example:

```text
+--------------------------------------+
|MONITOR HEX $0168                     |
|0160 6400360500806A00 3605008070003605|
|0170 0080760036050080 7C00360500808200|
|0180 3605008088003605 00808E0036050080|
|0190 9400360500809A00 36050080A0003605|
|01A0 0080A60036050080 AC0036050080B200|
|01B0 36050080B8003605 BC0036050080C200|
|01C0 36050080C8003605 0080CE0036050080|
|01D0 D40036050080DA00 36050080E0003605|
|01E0 0080E600367DEA18 050E21DF7DEA0A00|
|01F0 0022CFE5000A14E1 64A585A479A69CE3|
|0200 0000000000000000 0000000000000000|
|0210 0000000000000000 0000000000000000|
|0220 0000000000000000 0000000000000000|
|0230 0000000000000000 0000000000000000|
|0240 0000000000000000 0000000000000000|
|0250 0000000000000000 0000000000000000|
|0260 0000000000000000 0000000000000000|
|0270 0000000000000000 0000000000000000|
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

### Assembly View

Assembly view shows decoded 6510 instructions, their instruction bytes, and the memory source used for each row.

The source tag occupies three characters inside the brackets, so the column stays aligned across bank boundaries: `[RAM]`, `[BAS]`, `[CHR]`, `[I/O]`, `[KRN]`, and `[CPU]` for the memory currently visible to the CPU on an Ultimate II+.

`$D000-$DFFF` is shown as `DATA` rows of two bytes each when either I/O or Character ROM is banked in. I/O reads live registers, so decoding it would change the instruction length, and with it the address of every row below, on each redraw. Character ROM is stable but holds character bitmaps that never were code. With RAM banked in, the same addresses are disassembled normally: the rule follows the banked source, not the address range. On an Ultimate II+, `$D000-$DFFF` is always disassembled.

The rows are grouped from the start of the region, so where a `DATA` row begins does not depend on how the view arrived there. `$D000-$DFFF` is 4096 bytes and divides into 2048 rows of two; a region whose length is odd ends with a row of one byte.

A `DATA` row is edited in Assembly view like any other row. `E` enters edit mode and the cursor sits on the first byte; each displayed byte is its own edit position, two hex digits complete one, and `LEFT`/`RIGHT` step from byte to byte and on into the row above or below. There is no opcode picker on a `DATA` row, because there is no mnemonic to pick, and a letter key does nothing there. `[I/O]` is writable; `[CHR]` is ROM and refuses the write as it does everywhere else. Editing the same bytes in Memory view with `M` works as before.

`DEL` clears a `DATA` row's bytes to `$00`. On a decoded instruction it still writes `NOP`, which is what keeps the code around it runnable; `NOP` means nothing in a region that is not code.

A region shown as `DATA`:

```text
+--------------------------------------+
|MONITOR ASM $D000                     |
|D000 00 00     DATA 00 00        [I/O]|
|D002 00 00     DATA 00 00        [I/O]|
|D004 00 00     DATA 00 00        [I/O]|
|D006 00 00     DATA 00 00        [I/O]|
|D008 00 00     DATA 00 00        [I/O]|
|D00A 00 00     DATA 00 00        [I/O]|
|D00C 00 00     DATA 00 00        [I/O]|
|D00E 00 00     DATA 00 00        [I/O]|
|D010 00 1B     DATA 00 1B        [I/O]|
|D012 AF 5E     DATA AF 5E        [I/O]|
|D014 9E 00     DATA 9E 00        [I/O]|
|D016 C8 00     DATA C8 00        [I/O]|
|D018 15 78     DATA 15 78        [I/O]|
|D01A F0 00     DATA F0 00        [I/O]|
|D01C 00 00     DATA 00 00        [I/O]|
|D01E 00 00     DATA 00 00        [I/O]|
|D020 FE F6     DATA FE F6        [I/O]|
|D022 F1 F2     DATA F1 F2        [I/O]|
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

The two-byte row is how the bytes are shown, not what a range is made of. A range anchored with `R` on a `DATA` byte covers the bytes between its ends: anchoring on `$D001`, moving right to `$D002` and pressing `R` copies those two bytes and nothing else. A range that starts on a decoded instruction still takes that instruction whole, so a range may cross between code and data without either end losing bytes.

Example:

```text
+--------------------------------------+
|MONITOR ASM $E011                     |
|E011 A5 07     LDA $07           [KRN]|
|E013 18        CLC               [KRN]|
|E014 69 81     ADC #$81          [KRN]|
|E016 F0 F3     BEQ $E00B         [KRN]|
|E018 38        SEC               [KRN]|
|E019 E9 01     SBC #$01          [KRN]|
|E01B 48        PHA               [KRN]|
|E01C A2 05     LDX #$05          [KRN]|
|E01E B5 69     LDA $69,X         [KRN]|
|E020 B4 61     LDY $61,X         [KRN]|
|E022 95 61     STA $61,X         [KRN]|
|E024 94 69     STY $69,X         [KRN]|
|E026 CA        DEX               [KRN]|
|E027 10 F5     BPL $E01E         [KRN]|
|E029 A5 56     LDA $56           [KRN]|
|E02B 85 70     STA $70           [KRN]|
|E02D 20 53 B8  JSR $B853         [KRN]|
|E030 20 B4 BF  JSR $BFB4         [KRN]|
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
|DC00 .***********************........ |
|DC04 **.**..*...**...**************** |
|DC08 ........................*..*...* |
|DC0C .......................*....*... |
|DC10 .***********************........ |
|DC14 ***.****....*.****************** |
|DC18 ........................*..*...* |
|DC1C .......................*....*... |
|DC20 .***********************........ |
|DC24 *.***.*..*.....***************** |
|DC28 ........................*..*...* |
|DC2C .......................*....*... |
|DC30 .***********************........ |
|DC34 .*.**.....**.*..**************** |
|DC38 ........................*..*...* |
|DC3C .......................*....*... |
|DC40 .***********************........ |
|DC44 **.***.*..*..**.**************** |
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
|A000 ..{.CBMBASIC0.A................. |
|A020 p.'.......:...J.,.g.U.d...#..... |
|A040 V...]...).....z.A.9...X...}...q. |
|A060 ......d.k.......|.e.........,.7. |
|A080 yi.yR.{*.{...z.P..F..}..Z..d..EN |
|A0A0 .FO.NEX.DAT.INPUT.INPU.DI.REA.LE |
|A0C0 .GOT.RU.I.RESTOR.GOSU.RETUR.RE.S |
|A0E0 TO.O.WAI.LOA.SAV.VERIF.DE.POK.PR |
|A100 INT.PRIN.CON.LIS.CL.CM.SY.OPE.CL |
|A120 OS.GE.NE.TAB.T.F.SPC.THE.NO.STE. |
|A140 .....AN.O....SG.IN.AB.US.FR.PO.S |
|A160 Q.RN.LO.EX.CO.SI.TA.AT.PEE.LE.ST |
|A180 R.VA.AS.CHR.LEFT.RIGHT.MID.G..TO |
|A1A0 O MANY FILE.FILE OPE.FILE NOT OP |
|A1C0 E.FILE NOT FOUN.DEVICE NOT PRESE |
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
|MONITOR SCR L/U $0400                 |
|0400                                  |
|0420             **** commodore 64 ba |
|0440 sic v2 ****                      |
|0460                          64k ram |
|0480  system  38911 basic bytes free  |
|04A0                                  |
|04C0         ready.                   |
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
| Other views | Ignored                                                          |

In Assembly view, enabling undocumented opcodes affects how bytes are decoded and how assembly completion behaves.

In Screen view, `U` changes only the monitor-local interpretation of screen codes. It does not change the live C64 character set.

### `W`: Width Mode

`W` is view-dependent:

| View     | `W` behavior                         |
| -------- | ------------------------------------ |
| Memory   | Cycles `8 <-> 16` bytes per row      |
| Binary   | Cycles `1 -> 2 -> 3 -> 3S -> 4 -> 1` |
| ASCII    | Fixed-width, 32 bytes per row        |
| Screen   | Fixed-width, 32 bytes per row        |
| Assembly | Variable-width, 1 to 3 bytes         |

Binary width details:

- `1`, `2`, and `3` show one, two, or three bytes as bit fields with a trailing hex preview.
- `3S` shows three bytes as one continuous 24-bit sprite-style row, with a hex preview.
- `4` shows four bytes as one continuous 32-bit row without a trailing hex preview.

## Navigation and Context

- `J`: jump to an address.
- `G`: exit the monitor and execute from an address.
- `F1` or `Shift+Space`: page up.
- `F7` or `Space`: page down.
- `Enter`: in Assembly view, follow the target of a jumpable instruction, or return to the most recent saved source location when the current instruction is not jumpable and the follow stack is non-empty.
- `O`: cycle CPU port banking, `CPU0`..`CPU7`.
- `Shift+O`: cycle the VIC bank override.
- `Z`: toggle freeze when the backend supports it.
- `P`: toggle poll mode in the local monitor. Poll mode is unavailable over telnet.

Addresses in command prompts are hexadecimal.

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

An Ultimate II+ has no monitor-selectable CPU bank, so its footer reports the VIC bank alone:

```text
CPU VIEW  VIC0 $0000
```

## Editing

All views support editing:

- `E`: enter edit mode.
- `C=+E` or `RUN/STOP`: leave edit mode.

Edit behavior is view-specific:

| View     | Edit behavior                                                               |
| -------- | --------------------------------------------------------------------------- |
| Memory   | Type two hex nibbles to write one byte                                      |
| ASCII    | Type printable ASCII characters directly                                    |
| Screen   | Type screen characters using the active Screen charset mode                 |
| Binary   | Type `0` or `Space` to clear the selected bit; type `1` or `*` to set it    |
| Assembly | Edit instructions inline with mnemonic completion and direct operand typing |

In edit mode, `Space` remains view-specific data entry and does not page.

`DEL` is logical delete, not raw backspace:

| View         | `DEL` behavior                                  |
| ------------ | ----------------------------------------------- |
| Memory       | Writes `$00` and advances                       |
| ASCII/Screen | Writes a space                                  |
| Binary       | Clears the selected bit                         |
| Assembly     | Replaces the current instruction with `NOP` bytes; clears a `DATA` row to `$00` |

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

The ASCII and Screen rows in the number tool use the same mappings as the ASCII and Screen views.

### Calculator

In the Number popup, press `+`, `-`, `*`, or `/` to open the calculator. The expression is initialized with the current value and the selected operator.

Press `Return` or `=` to evaluate the expression. Press `RUN/STOP` to cancel. On success, the popup returns to the compact conversion layout and refreshes all rows with the result.

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
| `T` | Transfer | `start-end,dest[,code-start-code-end]`   | Copy a range to a destination, optionally relocating operands         |
| `C` | Compare  | `start-end,dest`                         | Compare a range against another location and list differing addresses |
| `H` | Hunt     | `start-end,bytes` or `start-end,"text"` | Search for a byte sequence or quoted ASCII string                     |

`Fill`, `Transfer`, `Compare`, `Hunt` and `Save` all treat `start-end` as inclusive of both ends, including the full `0000-FFFF` range.

`Transfer` takes an optional fourth field naming the range to scan for pointers into the block being copied:

```text
T C000-C0FF,C100,C000-C07F
```

Absolute, absolute-indexed and indirect operands pointing inside the copied source range are then adjusted to the corresponding destination address. Relative branches, zero-page operands, references outside the copied range and incomplete instructions are left unchanged. Without the fourth field, `Transfer` copies the bytes and changes nothing.

The scan range is independent of the range being copied. It may be shorter than the copy, longer than it, or somewhere else entirely, which is what lets a pointer that is not itself moving be brought with the block:

```text
T C000-C005,C010,C000-C008
```

Here the first two instructions are copied to `$C010` while the scan covers a third instruction that stays where it is. An instruction wholly inside the copy is rewritten in the copy, because that is the version being relocated. An instruction wholly outside it is rewritten where it stands. An instruction whose three bytes straddle the end of the copy is left alone, since writing its operand would put one byte in the copy and the other in the original.

`Hunt` opens a result picker:

- `Return`: jump to the selected match.
- `RUN/STOP`: close the picker.

A command prompt accepts only characters that can occur in the command being entered; other keys are ignored. Parsing and validation still happen on `Return`.

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

## Additional Notes

Use **UI Freeze** mode when the monitor output must be captured in the video stream.

Use **UI Overlay on HDMI** mode when polling is needed to observe live changes.

To switch between UI Freeze and UI Overlay modes:

1. Exit the monitor.
2. Press `C=+I`.
3. Reopen the monitor.
