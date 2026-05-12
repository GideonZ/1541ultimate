# Machine Monitor

The machine monitor is a keyboard-driven tool for inspecting and editing live or frozen C64 memory. It supports memory inspection, ASCII and screen-code views, binary editing, disassembly, inline assembly editing, memory operations, and execution.

## Entry and Exit

- Open the monitor with `C=+O`.
- Close the monitor with `C=+O` or `ESC`.
- Open the built-in help with `F3`.

## Screen Layout

The monitor screen consists of three main parts:

- **Header**
  - Shows the current view, cursor address, and active modes.
  - Mode indicators may include `Undoc`, `Freeze`, `Poll`, or `Edit`.

- **Body**
  - Shows the memory region around the current cursor address.
  - The active cursor position is highlighted in reverse.
  - May show popups, for example search results, load/save prompts, completion pickers, or bookmarks.

- **Footer**
  - Shows the active CPU port mapping and VIC bank.
  - When jumping to a bookmark, briefly shows bookmark information.

Example layout:

```text
+--------------------------------------+
|MONITOR ASM $E011                     |
|...                                   |
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
````

## Views

The monitor provides five primary views:

| Key | Name         | ID  | Purpose                             |
| --- | ------------ | --- | ----------------------------------- |
| `M` | **M**emory   | HEX | Hex byte view                       |
| `A` | **A**ssembly | ASM | Disassembly and instruction editing |
| `B` | **B**inary   | BIN | Bit-level byte view                 |
| `I` | ASC**I**I    | ASC | Printable ASCII byte view           |
| `V` | Screen       | SCR | C64 screen-code view                |

### Assembly View

Assembly view shows decoded 6510 instructions together with the backing bytes and the active memory source.

Example:

```text
+--------------------------------------+
|MONITOR ASM $E011                     |
|DFF9 FF           ???             [IO]|
|DFFA 00           BRK             [IO]|
|DFFB 00           BRK             [IO]|
|DFFC FF           ???             [IO]|
|DFFD 00           BRK             [IO]|
|DFFE 00           BRK             [IO]|
|DFFF 00           BRK             [IO]|
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

### Memory View

Memory view shows raw bytes in hexadecimal together with a compact ASCII preview.

Example:

```text
+--------------------------------------+
|MONITOR HEX $E011                     |
|DFF8 66 00 FF 66 00 FF 00 18 f..f.... |
|E000 85 56 20 0F BC A5 61 C9 .V ...a. |
|E008 88 90 03 20 D4 BA 20 CC .. . ..  |
|E010 BC A5 07 18 69 81 F0 F3 .. .i..  |
|E018 38 E9 01 48 A2 05 B5 69 8..H..i  |
|E020 B4 61 95 61 94 69 CA 10 .a.a.i.  |
|E028 F5 A5 56 85 70 20 53 B8 ..V.p S. |
|E030 20 B4 BF A9 C4 A0 BF 20  ....... |
|E038 59 E0 A9 00 85 6F 68 20 Y....oh  |
|E040 B9 BA 60 85 71 84 72 20 ..`.q.r  |
|E048 CA BB A9 57 20 28 BA 20 ...W (.  |
|E050 5D E0 A9 57 A0 00 4C 28 ]..W..L( |
|E058 B4 85 71 84 72 20 C7 BB ..q.r .. |
|E060 B1 71 85 67 A4 71 C8 98 .q.g.q.  |
|E068 D0 02 E6 72 85 71 A4 72 ...r.q.r |
|E070 20 28 BA A5 71 A4 72 18  (..q.r. |
|E078 69 05 90 01 C8 85 71 84 i.....q. |
|E080 72 20 67 B8 A9 5C A0 00 r g..\.. |
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

### ASCII View

Use ASCII view when the bytes are intended to be printable ASCII rather than C64 screen codes.

Behavior:

* Bytes `$20-$7E` are shown as their normal ASCII characters.
* All other bytes are shown as `.`.
* Editing writes the exact printable ASCII byte.
* Lowercase ASCII is preserved.

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

Use Screen view when the bytes represent VIC screen codes, e.g. when viewing the screen RAM which by default starts at `$0400`.

The header shows the active screen charset mode:

* `MONITOR SCR U/G $xxxx` for **Uppercase/Graphics**
* `MONITOR SCR L/U $xxxx` for **Lowercase/Uppercase**

The active mode is changed with `U`; see [View Modifiers](#view-modifiers).

#### Screen `U/G`

* Displays `$00` as `@`.
* Displays `$01-$1A` as `A-Z`.
* Typing `A-Z` or `a-z` writes `$01-$1A`.

#### Screen `L/U`

* Displays `$01-$1A` as `a-z`.
* Displays `$41-$5A` as `A-Z`.
* Typing `a-z` writes `$01-$1A`.
* Typing `A-Z` writes `$41-$5A`.

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

The monitor menu uses the UI font, not the live C64 character set. For this reason, Screen view uses readable fallback glyphs for graphics bytes rather than exact C64 character shapes.

### Binary View

Binary view displays bytes as editable bit fields.

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

## View Modifiers

Some keys modify the current view instead of switching to another view.

### `U`: View-Specific Toggle

`U` is context-sensitive:

| View        | `U` behavior                                                     |
| ----------- | ---------------------------------------------------------------- |
| Assembly    | Toggles undocumented opcodes                                     |
| Screen      | Toggles the monitor-local screen charset between `U/G` and `L/U` |
| Other views | No view-specific effect                                          |

In Assembly view, enabling undocumented opcodes affects how bytes are decoded and how assembly completion behaves.

In Screen view, `U` changes only the monitor-local interpretation of screen codes. It does not change the live C64 character set.

### `W`: Width Mode

`W` is view-dependent:

| View     | `W` behavior                         |
| -------- | ------------------------------------ |
| Memory   | Cycles `8 <-> 16` bytes per row      |
| Binary   | Cycles `1 -> 2 -> 3 -> 3S -> 4 -> 1` |
| ASCII    | Fixed-width (32 bytes)               |
| Screen   | Fixed-width (32 bytes)               |
| Assembly | Variable-width (1 to 3 bytes)        |

Binary width details:

* `1`, `2`, and `3` show one, two, or three bytes as bit fields with a trailing hex preview.
* `3S` shows packed 24-bit rendering for three-byte sprite-style data, still with a hex preview.
* `4` shows packed 32-bit rendering for four bytes without a trailing hex preview.

## Navigation and Context

* `J`: jump to an address.
* `G`: leave the monitor and start execution at an address.
* `F1` or `Shift+Space`: page up.
* `F7` or `Space`: page down.
* `O`: cycle CPU port banking (`CPU0`..`CPU7`).
* `Shift+O`: cycle the VIC bank override.
* `Z`: toggle freeze when the backend supports it.
* `P`: toggle poll mode in the local monitor. Poll mode is unavailable over telnet.

Addresses in command prompts are hexadecimal.

The generic number parser used by the number tool also accepts:

* `$1234`
* `0x1234`
* Decimal
* `%binary`

## Editing

* `E`: enter edit mode in Memory, ASCII, Screen, Assembly, and Binary views.
* `C=+E` or `ESC`: leave edit mode.

Edit behavior is view-specific:

| View     | Edit behavior                                                               |
| -------- | --------------------------------------------------------------------------- |
| Memory   | Type two hex nibbles to write one byte                                      |
| ASCII    | Type printable ASCII bytes directly                                         |
| Screen   | Type screen characters using the active Screen charset mode                 |
| Binary   | Type `0` or `Space` to clear the selected bit; type `1` or `*` to set it    |
| Assembly | Edit instructions inline with mnemonic completion and direct operand typing |

In edit mode, `Space` remains view-specific data entry and does not page.

`DEL` is logical delete, not raw backspace:

| View         | `DEL` behavior                                                                                             |
| ------------ | ---------------------------------------------------------------------------------------------------------- |
| Memory       | Writes `$00` and advances                                                                                  |
| ASCII/Screen | Writes a space                                                                                             |
| Binary       | Clears the selected bit                                                                                    |
| Assembly     | Replaces the current instruction with `NOP` bytes; while typing, first cancels the current line edit state |

## Clipboard, Range, and Number Tool

* Copy the current byte with `C=+C`.
* Paste the clipboard at the cursor with `C=+V`.
* Toggle range mode with `R`.
* Open the number tool with `N`.

Range mode anchors the current address. While range mode is active:

* `C=+C` copies the selected span.
* Pressing `R` again also copies the selected span and exits range mode.

The number tool is a compact base-conversion and overwrite popup for the current target. It shows the same value in these forms:

* Hex
* Decimal
* Binary
* ASCII
* Screen code

In Assembly view, the number tool targets the operand bytes of the current instruction when possible.

The ASCII and Screen rows in the number tool use the same mappings as the ASCII and Screen views.

## Memory Operations

The monitor includes direct bulk memory commands:

| Key | Command  | Syntax                                  | Result                                                                |
| --- | -------- | --------------------------------------- | --------------------------------------------------------------------- |
| `F` | Fill     | `start-end,value`                       | Fill an inclusive range with one byte                                 |
| `T` | Transfer | `start-end,dest`                        | Copy a range to a destination                                         |
| `C` | Compare  | `start-end,dest`                        | Compare a range against another location and list differing addresses |
| `H` | Hunt     | `start-end,bytes` or `start-end,"text"` | Search for a byte sequence or quoted ASCII string                     |

`Hunt` opens a result picker:

* `ENTER`: jump to the selected match.
* `ESC`: close the picker.

## File I/O

* `L`: load a file into memory.
* `S`: save memory to a file.

Load is a two-step flow:

1. Pick a file.
2. Enter load parameters.

Load template:

```text
PRG,0000,AUTO
```

Fields:

| Field            | Meaning                                                         |
| ---------------- | --------------------------------------------------------------- |
| `PRG` or address | Use PRG load address, or load to an explicit start address      |
| File offset      | Number of bytes to skip                                         |
| `AUTO` or length | Load automatically determined length, or an explicit byte count |

Examples:

| Input            | Meaning                                           |
| ---------------- | ------------------------------------------------- |
| `PRG`            | Load a PRG to its embedded load address           |
| `0801`           | Load to `$0801`                                   |
| `PRG,1000`       | Skip `$1000` bytes after the PRG header           |
| `0801,0002,0010` | Load `$0010` bytes from offset `$0002` to `$0801` |

Save writes a normal PRG file with a two-byte load address header.

Save template:

```text
0800-9FFF
```

The range is inclusive.

## Bookmarks

The monitor has 10 persistent bookmark slots.

* List bookmarks with `C=+B`.
* Jump directly to a slot with `C=+0` .. `C=+9`.

Each bookmark stores:

* Label
* Address
* View ID
* View width in bytes where applicable
* CPU bank
* VIC bank

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

To switch between UI Freeze and UI Overlay modes:

1. Exit the monitor.
2. Press `C=+I`.
3. Reopen the monitor.

Use **UI Freeze** mode when the monitor output must be captured in the video stream.

Use **UI Overlay on HDMI** mode when polling is needed to observe live changes.

