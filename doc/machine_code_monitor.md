# Machine Code Monitor

The Machine Code Monitor is a keyboard-driven tool for inspecting and editing live or frozen C64 memory.

It supports hexadecimal, ASCII, screen-code, binary, and assembly views, plus inline editing, bulk memory operations, file load/save, and execution from a selected address.

You can also debug an assembly program by setting breakpoints, stepping through its execution, and observing its effects on memory and CPU state.

## Entry and Exit

`C=` denotes the Commodore key. For example, `C=+O` means: hold the Commodore key, then press `O`.

To open the monitor, first open the device menu, then use one of the following:

- Press `C=+O`.
- Press `F5`, open `Developer`, then select `Machine Code Monitor`.

Open the built-in help with `F3` or `?`.

While the monitor is open, `C=+X` resets / breaks the machine from any monitor mode, including Help, Edit, Debug, and popups.

To close the monitor:

- Press `C=+O` again.
- Press `X`, `RUN/STOP`, or `ESC` when no edit mode, Debug mode, or popup is active. In Debug mode, `RUN/STOP` leaves Debug first; in Edit mode it leaves Edit first.

## Access Modes

The machine code monitor can be opened in three ways:

| Mode           | C64 while monitor is open | Video stream           | Use this when                                                                                |
| -------------- | ------------------------- | ---------------------- | -------------------------------------------------------------------------------------------- |
| **UI Freeze Mode**  | Frozen                    | Monitor is visible     | You want full-screen monitor use, automatic freezing, or monitor output in the video stream. |
| **UI Overlay Mode** | Running, but can be frozen with the `Z` shortcut or by stopping in Debug mode | Monitor is invisible | You want to use the monitor while the C64 keeps running.                                     |
| **Telnet**     | Ditto                   | Monitor is invisible         | You want to use the monitor from another machine or in an automated way.                                            |

### Switching between UI Freeze and UI Overlay Modes

Press `C=+I` to toggle between UI Freeze and UI Overlay mode.

Toggling automatically closes the menu. The next time you open the menu, it uses the newly selected mode.

## Screen Layout

The machine code monitor screen has three fixed regions: header, body, and footer.

```text
+--------------------------------------+
|MONITOR ASM $E011 Undc Frz Pl Dbg Edit|
|...                                   |
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

### Header

The header shows the current monitor view, the cursor address, and any active mode indicators.

Mode indicators may include any combination of the following:

| Indicator | Meaning                                              |
| --------- | ---------------------------------------------------- |
| `Undc`    | Undocumented opcodes are decoded (Assembly view only) |
| `Range`   | Range selection is active                            |
| `Frz`     | Freeze is active                                     |
| `Pl`      | Polling is active                                    |
| `Dbg`     | Debug mode is active                                 |
| `Edit`    | Edit mode is active                                  |

`Undc` and `Range` share one slot in the header, so only one of them is shown at a time.

### Body

The body shows the memory region around the current cursor address.

The active cursor position is highlighted in reverse. Depending on the current operation, the body may also show popups such as search results, load/save prompts, or bookmark lists.

### Footer

The footer shows the current memory-bank context and temporary status information.

It includes:

- The CPU memory configuration used by the monitor view.
- Any difference between the monitor view and the live CPU execution bank.
- The selected VIC bank and its base address.
- Temporary bookmark, follow, and Debug status messages.

Common footer values include:

| Value            | Meaning                                                                                                                                    |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `CPU0` to `CPU7` | The monitor view and live CPU execution bank match.                                                                                        |
| `CxOy`           | The monitor view and live CPU execution bank differ. `Cx` is the live CPU execution bank. `Oy` is the monitor view bank selected with `O`. |
| `$A`, `$D`, `$E` | Show how the monitor view maps the main ROM/RAM regions.                                                                                   |
| `VIC0` to `VIC3` | Identify the selected VIC bank. The following address shows its base address.                                                              |

On hardware without monitor-side bank selection the footer reads `CPU VIEW  CPU BANK N/A  VIC N/A`.

For full details, see [CPU and VIC Bank Display](#cpu-and-vic-bank-display).

## Views

The monitor provides five primary views:

| Key | View     | ID  | Purpose                       |
| --- | -------- | --- | ----------------------------- |
| `M` | Memory   | HEX | Hexadecimal byte view         |
| `A` | Assembly | ASM | (Dis)assembly with debug mode |
| `B` | Binary   | BIN | Bit-level byte view           |
| `I` | ASCII    | ASC | ASCII byte view               |
| `V` | Screen   | SCR | Screen code view              |

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

The highlighted address is the disassembly root: rows below it are decoded forward from that address, while rows above it are context only. Changing bytes before the highlighted address can refresh the context rows, but it does not change the instruction phase at the highlighted address. To inspect a different phase deliberately, move the root with the cursor keys or jump to the desired address.

The tag at the right end of each row names the memory source the byte was read from: `[RAM]`, `[BAS]`, `[KRN]`, `[CHR]`, or `[I/O]`. On the U2 cartridge, where the monitor reads whatever the CPU currently sees, every row is tagged `[CPU]`.

Assembly view also allows you to assemble instructions inline (in `E`dit mode) and to debug code (in `D`ebug mode).

See the **Edit Mode** and **Debug Mode** chapters below for more information.

Example:

```text
+--------------------------------------+
|MONITOR ASM $E011                     |
|DFF9 FF           ???            [I/O]|
|DFFA 00           BRK            [I/O]|
|DFFB 00           BRK            [I/O]|
|DFFC FF           ???            [I/O]|
|DFFD 00           BRK            [I/O]|
|DFFE 00           BRK            [I/O]|
|DFFF 00           BRK            [I/O]|
|E000 85 56        STA $56        [KRN]|
|E002 20 0F BC     JSR $BC0F      [KRN]|
|E005 A5 61        LDA $61        [KRN]|
|E007 C9 88        CMP #$88       [KRN]|
|E009 90 03        BCC $E00E      [KRN]|
|E00B 20 D4 BA     JSR $BAD4      [KRN]|
|E00E 20 CC BC     JSR $BCCC      [KRN]|
|E011 A5 07        LDA $07        [KRN]|
|E013 18           CLC            [KRN]|
|E014 69 81        ADC #$81       [KRN]|
|E016 F0 F3        BEQ $E00B      [KRN]|
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
| Other views | Shows the popup `UNDOC IN ASM, CASE IN SCR`                      |

In Assembly view, enabling undocumented opcodes affects how bytes are decoded and how assembly completion behaves.

In Screen view, `U` changes only the monitor-local interpretation of screen codes. It does not change the live C64 character set.

Inside Debug mode, `U` is Step Out. Leave Debug mode to use the toggle.

### `W`: Width Mode

`W` is view-dependent:

| View     | `W` behavior                                        |
| -------- | --------------------------------------------------- |
| Memory   | Cycles `8 <-> 16` bytes per row                     |
| Binary   | Cycles `1 -> 2 -> 3 -> 3S -> 4 -> 1`                |
| ASCII    | Fixed-width, 32 bytes per row                       |
| Screen   | Fixed-width, 32 bytes per row                       |
| Assembly | Variable-width, 1 to 3 bytes                        |

In the fixed-width and variable-width views, `W` shows the popup `WIDTH ONLY IN MEMORY/BINARY VIEW`.

Binary width details:

- `1`, `2`, and `3` show one, two, or three bytes as bit fields with a trailing hex preview.
- `3S` shows three bytes as one continuous 24-bit sprite-style row, with a hex preview.
- `4` shows four bytes as one continuous 32-bit row without a trailing hex preview.

## Navigation and Context

- `J`: jump to an address.
- `G`: exit the monitor and execute from an address.
- `F1` or `Shift+Space`: page up.
- `F7` or `Space`: page down.
- Assembly view, non-edit mode: `Up` / `Down` move to the previous / next instruction root; `Left` / `Right` move the decode root by one byte (`-1` / `+1`).
- `Enter`: in Assembly view, follow the target of a jumpable instruction, or return to the most recent saved source location when the current instruction is not jumpable and the follow stack is non-empty.
- `O`: cycle the monitor-view CPU port banking, `CPU0`..`CPU7`. This changes the monitor view only; it does not write `$0001`.
- `Shift+O`: cycle the VIC bank override.
- `Z`: toggle freeze.
- `P`: toggle poll mode in the local monitor. Poll mode is unavailable over telnet.

Addresses in command prompts are hexadecimal.

`Z` freezes the running machine so registers and I/O stay stable across many reads and writes, and unfreezes it again. It is available when the machine is not already held by the freezer. In UI Freeze mode the machine is already frozen and `Z` shows `FREEZE ONLY IN OVERLAY MODE`. On hardware without freeze support it shows `FREEZE UNAVAILABLE`.

### Follow/Return

Follow code flow in the Assembly view:

- `Enter` follows the resolved target when the cursor is on a jumpable instruction: `JSR`, `JMP` absolute, `JMP` indirect, or any of `BEQ`, `BNE`, `BCC`, `BCS`, `BMI`, `BPL`, `BVC`, `BVS`. For `JMP` indirect the monitor reads the vector and follows the address stored there.
- `Enter` returns to the most recent saved source location when the current Assembly instruction is not jumpable and the follow stack is non-empty.
- The follow stack holds up to 10 return locations. When it is full, the oldest entry is discarded and the newest 10 are kept.
- After each successful follow or return, the bottom row shows a compact zero-based follow-stack status for about 2 seconds, for example `F1 JMP $E000` and `F0 RET $A000`.

### CPU and VIC Bank Display

With the `O` and `Shift+O` keys, you can quickly toggle the CPU and VIC banks.

#### CPU Banking

The monitor shows two independent CPU banking states:

- **CPU execution bank**: The live bank used by the running 6510 CPU. It is derived from the lowest three bits of `$0001`, the 6510 on-chip port register. This is the bank from which the CPU fetches and executes instructions.
- **Monitor view bank**: The bank selected in the machine code monitor with the `O` key. This controls which memory mapping the monitor displays while you browse the 64 KiB address space.

When both banks are the same, the footer shows a single `CPUx` value, where `x` is a bank number from `0` to `7`:

```text
CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000
```

When the CPU execution bank and monitor view bank differ, the footer shows both values as `CxOy`:

- `Cx` is the live CPU execution bank.
- `Oy` is the monitor view bank selected with the `O` key.

Example:

```text
C7O5 $A:RAM $D:I/O $E:RAM VIC0 $0000
```

After a machine reset, the next fresh monitor open syncs its view bank to the live CPU execution bank, so re-entry shows the memory the CPU is actually running.

An ordinary monitor close/reopen with no reset in between preserves a manually selected `O` view bank.

In the normal no-cartridge configuration, the `$A`, `$D`, and `$E` fields describe how the selected monitor view maps the main ROM/RAM regions:

| Field | Address range | Possible values     |
| ----- | ------------- | ------------------- |
| `$A`  | `$A000-$BFFF` | `BAS`, `RAM`        |
| `$D`  | `$D000-$DFFF` | `I/O`, `CHR`, `RAM` |
| `$E`  | `$E000-$FFFF` | `KRN`, `RAM`        |

| Value | Meaning                     |
| ----- | --------------------------- |
| `BAS` | BASIC ROM                   |
| `I/O` | I/O registers and Color RAM |
| `CHR` | Character generator ROM     |
| `KRN` | KERNAL ROM                  |
| `RAM` | RAM                         |

Cartridges can further affect the CPU-visible memory map through the expansion-port `GAME` and `EXROM` lines.

#### VIC Banking

`VIC0` to `VIC3` show the selected VIC bank, controlled by CIA 2 port A at `$DD00`:

| Field     | Address range |
| --------- | ------------  |
| `VIC0`    | `$0000-$3FFF` |
| `VIC1`    | `$4000-$7FFF` |
| `VIC2`    | `$8000-$BFFF` |
| `VIC3`    | `$C000-$FFFF` |

Selecting a VIC bank writes `$DD00`, so the change is visible to the CPU and can affect a running program unless you are in freeze mode or stopped at a breakpoint.

## Edit Mode

All views support editing:

- `E`: enter edit mode.
- `C=+E` or `RUN/STOP`: leave edit mode.

Edit behavior is view-specific:

| View     | Edit behavior                                                               |
| -------- | --------------------------------------------------------------------------- |
| Memory   | Type two hex nibbles to write one byte                                      |
| ASCII    | Type printable ASCII characters directly                                    |
| Screen   | Type screen characters using the active Screen charset mode                 |
| Binary   | Type `0`, `.`, or `Space` to clear the selected bit; type `1` or `*` to set it |
| Assembly | Edit instructions inline with mnemonic completion and direct operand typing |

In edit mode, `Space` remains view-specific data entry and does not page.

In Assembly edit mode, `Left` / `Right` move between editable parts of the current instruction. They do not change the disassembly root unless the cursor is already at the first or last editable part, where the existing row-to-row edit navigation applies. `Up` / `Down` move to the previous / next instruction row, and `Return` commits the current line and advances.

Typing a letter in the mnemonic field opens the opcode picker. A branch operand is edited as its absolute target address; an edit that would need an offset outside `-128..127` is refused and the offset byte is left untouched.

`DEL` is logical delete, not raw backspace:

| View         | `DEL` behavior                                  |
| ------------ | ----------------------------------------------- |
| Memory       | Writes `$00` and advances                       |
| ASCII/Screen | Writes a space                                  |
| Binary       | Clears the selected bit                         |
| Assembly     | Replaces the current instruction with `NOP` bytes |

In Memory view, `DEL` first rolls back a half-typed nibble. In Assembly view, `DEL` first undoes the characters typed on the current instruction.

## Selection and Clipboard

- Copy the current byte with `C=+C`.
- Paste the clipboard at the cursor with `C=+V`.
- Toggle range mode with `R`.

Range mode anchors the current address. The selected span runs from the anchor address to the current cursor address, inclusive.

While range mode is active:

- `C=+C` copies the selected span and leaves range mode.
- Pressing `R` again also copies the selected span and exits range mode.

Paste writes the clipboard bytes from the cursor address onwards and moves the cursor past the pasted data.

## Number Tool

- Open the number tool with `N`.

The number tool is a compact base-conversion and overwrite popup for the current target. It shows the same value in these forms:

- Hex
- Decimal
- Binary
- ASCII
- Screen code

The popup title shows the target address and whether the target is a `BYTE` or a `WORD`. In Assembly view, the number tool targets the operand bytes of the current instruction when possible.

The ASCII and Screen rows in the number tool use the same mappings as the ASCII and Screen views.

Number tool controls:

| Key         | Action                                                      |
| ----------- | ----------------------------------------------------------- |
| `Up`/`Down` | Select the row to type in                                   |
| Typing      | Build a new value in the selected row's notation            |
| `DEL`       | Remove the last typed character                             |
| `Return`    | Write the previewed value to the target                     |
| `C=+C`      | Copy the previewed value to the clipboard and close         |
| `+ - * /`   | Open the calculator with the current value and that operator |
| `RUN/STOP`  | Close without writing                                       |

### Calculator

In the Number popup, press `+`, `-`, `*`, or `/` to open the calculator. The expression is initialized with the current value and the selected operator.

Press `Return` or `=` to evaluate the expression. Press `RUN/STOP` to cancel. On success, the popup returns to the compact conversion layout and refreshes all rows with the result.

Expressions may contain one or more values separated by `+`, `-`, `*`, or `/`. `*` and `/` are evaluated before `+` and `-`. Division is unsigned integer division and truncates toward zero.

A failed evaluation shows `SYNTAX`, `RANGE`, or `DIV/0` in place of the expression.

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

The monitor includes direct bulk memory commands:

| Key | Command  | Syntax                                   | Result                                                                |
| --- | -------- | ---------------------------------------- | --------------------------------------------------------------------- |
| `F` | Fill     | `start-end,value`                        | Fill an inclusive range with one byte                                 |
| `T` | Transfer | `start-end,dest[,program-start-program-end]` | Copy a range; the optional program range amends absolute operands that point into the moved range |
| `C` | Compare  | `start-end,dest`                         | Compare a range against another location and list differing addresses |
| `H` | Hunt     | `start-end,bytes` or `start-end,"text"` | Search for a byte sequence or quoted ASCII string                     |

`Transfer` normally copies bytes only. When the optional program range is supplied, the monitor treats that range as 6502 code and amends every 16-bit absolute operand whose value points into the moved source range so it points at the destination copy. This applies to all absolute operands, not just `JMP` and `JSR`.

`Hunt` and `Compare` open a result picker with these controls:

| Key                       | Action                    |
| ------------------------- | ------------------------- |
| `Up`/`Down`               | Select a match            |
| `F1`/`F7`, `Home`/`End`   | Page or jump to the ends  |
| `Return`                  | Jump to the selected match |
| `RUN/STOP`                | Close the picker          |

Both pickers list at most 256 matches. A search with no result shows `No matches` or `No differences`.

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
```

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

The monitor remembers the last load parameters and offers them as the default next time.

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

- Select an existing file by pressing `ENTER` on it, then choosing `Select` from the context-sensitive menu. The file is overwritten.
- Select `<< Create New File >>` at the top of a writable directory, then type the filename at the `Save as` prompt.

The `<< Create New File >>` entry only appears in directories that can be written to.

## Bookmarks

The monitor has 10 bookmark slots. They are stored in the device configuration and survive a power cycle.

- List bookmarks with `C=+B`.
- Jump directly to a slot with `C=+0` .. `C=+9`.

Each bookmark stores:

- Label, up to 6 characters
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
| `RUN/STOP`  | Close the popup                                   |

Default slots are aimed at common C64 locations:

```text
+--------------------------------------+
|BOOKMARKS                             |
|                                      |
|0 ZP      $0000 HEX  8 CPU7 VIC0      |
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
|0-9/RET:Jmp  S:Set  L:Label  DEL:Reset|
+--------------------------------------+
```

## Debug Mode

Debug is a modal state layered on the Assembly view. It adds breakpoints, single stepping, and a CPU register footer.

### Starting and ending a Debug session

Press `D` outside Debug. The monitor switches to Assembly view, shows `Dbg` in the header, and reserves the bottom two rows for the CPU footer.

Entering Debug executes nothing and does not stop the C64. There is no captured CPU state yet, so the footer is blank and the first execution command starts at the Assembly cursor address, not at the address the C64 is currently executing. To attach to running code, set a breakpoint and press `G`.

To end the session:

| Key                 | Effect                                                                                                     |
| ------------------- | ---------------------------------------------------------------------------------------------------------- |
| `C=+D`              | Leave Debug, stay in the monitor                                                                           |
| `RUN/STOP` or `ESC` | Leave Debug, stay in the monitor. With Edit also active, the first press leaves Edit and the second Debug   |
| `X` or `C=+O`       | Leave Debug and close the monitor                                                                          |
| `C=+X`              | Reset the machine. Debug is re-entered afterwards with no captured context                                 |

Debug is available in UI Freeze, UI Overlay, and Telnet mode. Only one Debug session can be active at a time across all front ends. If another front end already owns the debugger, entering Debug shows `DEBUG IN USE`. An owner that has not been seen for 3 seconds is cleaned up and its ownership taken over.

### Debug keys

| Key           | Outside Debug                            | Inside Debug                   |
| ------------- | ---------------------------------------- | ------------------------------ |
| `D`           | Enter Debug, no execution                | Step Over                      |
| `T`           | Transfer memory                          | Step Into                      |
| `U`           | Undoc / Case toggle                      | Step Out                       |
| `G`           | Go / execute                             | Go                             |
| `K`           | (unassigned)                             | Run to cursor                  |
| `R`           | Range mode                               | Toggle breakpoint at the cursor |
| `C=+R`        | Breakpoint list, if any breakpoint exists | Breakpoint list                |
| `C=+D`        | (unassigned)                             | Leave Debug                    |
| `RUN/STOP`    | Close the monitor                        | Leave Edit first, then Debug   |
| `X`, `C=+O`   | Close the monitor                        | Close the monitor              |
| `C=+X`        | Reset / break the machine                | Reset / break the machine      |
| `O`           | CPU bank cycle                           | CPU bank cycle                 |
| `RETURN`      | Assembly follow / return                 | Assembly follow / return       |

Every key Debug does not own keeps working, so you can navigate, switch views, use bookmarks, and edit memory with Debug active. `B` still selects Binary view and `C=+B` still opens the bookmark list.

`RETURN` and `T`/`U` are different kinds of navigation. `RETURN` follows a `JSR`/`JMP` target, or returns from one, without executing anything. `T` and `U` move the real CPU.

Inside Debug, `U` is Step Out instead of the Assembly-view undocumented-opcode toggle. `O` still cycles the monitor view bank, but it never changes which instruction stream the CPU executes.

### The Assembly view in Debug

While Debug holds a captured CPU context, the instruction that will run next is bracketed, for example `>LDA $07<`. The bracket is independent of the movable cursor, so you can scroll away and still see what runs next.

- For `JSR`, absolute `JMP`, and a branch that will be taken, the target operand is drawn in the accent color.
- For `RTS`, the row shows the return address read from the live stack, for example `RTS $E5D2`, also in the accent color. With an empty stack (SP `$FF`) it shows `RTS $????`.
- The instruction bytes, the memory source tag, and the temporary step breakpoints follow the live CPU bank from `$0001`, not the inspection bank selected with `O`.
- Enabled breakpoints are drawn in the accent color while Debug is active. Disabled breakpoints, and any breakpoint shown while Debug is off, use the regular foreground color.

After each step the cursor follows the new program counter, the view bank is synced to the live CPU bank, and the view scrolls so the program counter stays visible. A step that jumps somewhere else leaves the program counter three rows from the top.

### CPU footer

The bottom two rows of the monitor hold a fixed-position CPU state table while Debug is active:

```text
PC   AC XR YR SP NV-BDIZC IRQ  NMI
C003 01 00 FF F7 00100100 C123 EA31
```

| Field              | Meaning                                                 |
| ------------------ | ------------------------------------------------------- |
| `PC`               | Program counter from the captured debug context         |
| `AC` / `XR` / `YR` | Accumulator and index registers                         |
| `SP`               | Stack pointer                                           |
| `NV-BDIZC`         | Status register bits 7..0 as an 8-character binary string |
| `IRQ`              | RAM IRQ vector at `$0314/$0315`, when valid             |
| `NMI`              | RAM NMI vector at `$0318/$0319`, when valid             |

The program counter through the status register are highlighted in the same color as the `Dbg` and `Edit` header flags. In the header row, the name of each set status flag is highlighted too.

Unknown values render as blank spaces in their reserved fixed-width columns. They never appear as zeros, `?`, or placeholder text, and field positions stay put when values become known.

### Breakpoints

There are 10 breakpoint slots, numbered `0` to `9`.

- `R` toggles a breakpoint at the Assembly cursor address, in the memory source selected with `O`. With all 10 slots in use, `R` reports `NO FREE BRK SLOT`.
- A breakpoint is an address plus a memory source, so `$E000 KRN` and `$E000 RAM` are distinct breakpoints and can coexist.
- Rows with a breakpoint show `[BRKn]` immediately before the memory source tag, for example `[BRK0][BAS]`. A slot with a label shows the label instead, for example `[LOOP][BAS]`.
- Only enabled breakpoints stop execution. `G`, `K`, Step Over, Step Into, and Step Out all honour them. A disabled slot is remembered but inert.
- Breakpoints are held in volatile RAM. They survive a `C=+X` reset, leaving Debug, and closing and reopening the monitor. Powering the device off clears them.
- At most 16 breakpoint patches can be armed at once. That covers the 10 user slots plus the temporary landing patches a step installs.

Two address ranges cannot hold a breakpoint:

| Range           | Used for                                                            |
| --------------- | ------------------------------------------------------------------- |
| `$0314`-`$0319` | RAM IRQ, BRK, and NMI vectors, redirected to the debugger            |
| `$035D`-`$03FB` | Debug handler, trampolines, and register store in the cassette buffer |

A breakpoint or a step landing in either range is refused with `PATCH FAILED`. `$03FC`-`$03FF` is left alone. `$0340` upwards is the scratch area for single instructions executed from RAM, so do not keep data you care about there while debugging.

A breakpoint can be valid but invisible to the CPU. Setting one where the live banking does not map that source shows `BRK <target>, CPU <current>; not mapped now`. The breakpoint is in `<target>`, and the running program has to bank `<target>` in before it can be hit. `<current>` reflects the machine's last known banking, taken at a reset or at the latest Debug stop, so a free-running program that changes `$01` afterwards is only picked up at its next stop.

On the Ultimate 64, breakpoints in BASIC, KERNAL, and character ROM are patched into the volatile U64 ROM image, so ROM code is step-capable without copying ROMs into C64 RAM or writing flash. The patched bytes are restored when the breakpoint is removed and when the session ends. RAM-under-KERNAL breakpoints work when KERNAL is banked out. On an Ultimate II cartridge, C64 ROM is read-only, and a breakpoint there is refused with `DEBUG NOT SUPPORTED`.

`C=+R` opens the breakpoint list. The popup help row uses the abbreviations in parentheses to fit the line:

| Key                  | Action                                              |
| -------------------- | --------------------------------------------------- |
| `Up` / `Down`        | Select slot                                         |
| `Return`             | Jump to the selected slot                           |
| `0`-`9`              | Jump directly to a slot (`Jmp`)                     |
| `S`                  | Store the current address into the selected slot (`Set`) |
| `L`                  | Change the label, up to 4 chars (`Lbl`)             |
| `E`                  | Toggle slot enable / disable (`Enbl`)               |
| `DEL`                | Clear the selected slot (`Res`)                     |
| `RUN/STOP` or `C=+R` | Close the popup                                     |

Jumping to a slot also restores the CPU view bank the breakpoint was set in.

### What each Debug command does

| Command       | Key | Behavior                                                                                                                                                                                                                       |
| ------------- | --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Step Over     | `D` | Executes the instruction at the program counter. For a `JSR` it plants a breakpoint at the return site and lets the whole subroutine run, so a call into ROM or RAM under ROM completes without any manual breakpoint. Any other instruction is a single step, exactly like Step Into. |
| Step Into     | `T` | Executes exactly one instruction. A `JSR` lands on the first instruction of the callee.                                                                                                                                        |
| Step Out      | `U` | Runs to the caller of the current subroutine and stops there.                                                                                                                                                                  |
| Go            | `G` | Resumes the program. With at least one enabled breakpoint it stops at the first one hit and Debug stays open. With no enabled breakpoint the CPU is handed back to full-speed execution; the local UI closes the monitor as it does so, while a Telnet session stays open on the running machine. |
| Run to cursor | `K` | Plants a temporary breakpoint at the Assembly cursor address and runs until it is reached. Enabled breakpoints on the way still stop the run.                                                                                   |

All five follow the live CPU bank from `$0001`.

`G` pressed while stopped on a breakpoint steps past that breakpoint first, so the same one does not fire again immediately. Other enabled breakpoints still apply.

A run that does not reach a breakpoint gives up after 5 seconds and reports `DEBUG TIMEOUT`. The budget is 900 ms when a ROM-image patch is armed. While a run is in progress, `RUN/STOP`, `ESC`, `C=+D`, or `C=+O` abandons it with `DEBUG CANCELLED`, and `C=+X` resets the machine.

Step Out returns to the caller of the frame the CPU is really in, so it works both after a Step Into and after arriving inside a subroutine with `G` or `K`. Two sources describe that frame: the frames Step Into recorded, and the return address on the live `$0100` stack. The live stack is only trusted when a `JSR` really sits three bytes before what its top two bytes point at. When neither source yields an active frame, Step Out reports `NOT IN SUBROUTINE`. The disassembler still shows the live `RTS` target for that row, so you can set a breakpoint there and use `G` instead.

Step Out is not limited to shallow nesting. It tracks the full hardware call depth up to the 128-frame limit of the `$0100` stack.

The live stack pointer stays coherent with an undebugged run. A `JSR` moves SP down by exactly 2 and the matching `RTS` up by 2, and a Step Over of a `JSR` returns with SP net unchanged.

### Where you can step

Every step lands on the architecturally correct next instruction, with the registers, flags, stack pointer, and memory side effects an undebugged run would have produced. What is available depends on where the program counter is, and on whether the debugger already holds a captured CPU context.

| Program counter is in | Without a captured context                                                             | With a captured context |
| --------------------- | -------------------------------------------------------------------------------------- | ----------------------- |
| Plain RAM             | All commands                                                                             | All commands            |
| I/O space             | All commands. A byte in I/O space stepped as code behaves like RAM                       | All commands            |
| RAM under a ROM window | Step Into stops with `Step Into: run to a breakpoint 1st`. Step Over is available        | All commands            |
| Visible BASIC / KERNAL / character ROM | Step Into, and Step Over of anything that is not a `JSR`, stop with `run to a breakpoint 1st` | All commands |

To obtain a context, set a breakpoint and press `G`, or Step Over a `JSR`. From then on every command works in every region.

Two side effects are worth knowing when a step is completed while the CPU is parked, which is what happens for RAM under ROM and visible ROM:

- A data access to I/O is performed as one clean read or write. The NMOS bus quirks (the double write of a read-modify-write instruction, the dummy read on an indexed page cross) are not replayed.
- Code that flips `$01` still changes banking exactly as an undebugged run would, because such an instruction runs on the real 6510.

In UI Freeze mode a Step Over of a `JSR` into visible ROM, and a Step Out out of visible ROM, are completed instruction by instruction while the CPU stays parked rather than free-running the frozen machine. The walk stops early, reporting the context it actually reached, if it hits an enabled breakpoint, an instruction it cannot step (`BRK` or an undocumented opcode), or its budget of 8192 instructions. Press Step Over, Step Out, or `G` again to continue.

On an Ultimate II cartridge, `BRK` breakpoints and steps only work where the code is in writable RAM. Stepping visible ROM code is not available. See [Hardware support](#hardware-support).

### Debug messages

Messages fit within 38 characters. The two that offer guidance appear on the bottom status row; the rest are popups.

| Message                             | Meaning and what to do                                                                                                      |
| ----------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `Step Into: run to a breakpoint 1st` | The program counter is in RAM under ROM or visible ROM and no CPU context is captured. Set a breakpoint and press `G`, or Step Over a `JSR`. |
| `Step Over: run to a breakpoint 1st` | Same situation in visible ROM, for an instruction that is not a `JSR`.                                                      |
| `UNSUPPORTED OPCODE`                | The instruction to step is an undocumented opcode. Set a breakpoint past it and use `G`.                                     |
| `UNSAFE TARGET`                     | The instruction at the program counter is a `BRK`. Move the program counter past it, or set a breakpoint past it and use `G`. |
| `PATCH FAILED`                      | A breakpoint or step landing site falls in `$0314`-`$0319` or `$035D`-`$03FB`, or all 16 patch slots are in use.             |
| `NOT IN SUBROUTINE`                 | Step Out found no active call frame. Set a breakpoint at the `RTS` target shown on the row and use `G`.                      |
| `RETURN NOT REACHED`                | The Step Out run did not stop at the caller. Set a breakpoint at the return address and use `G` instead.                     |
| `DEBUG TIMEOUT`                     | No breakpoint was reached within the run budget. The program was released and the debugger stopped waiting for it.           |
| `DEBUG CANCELLED`                   | A run was abandoned from the keyboard.                                                                                       |
| `DEBUG NOT SUPPORTED`               | The hardware cannot do this, for example a visible-ROM patch on an Ultimate II cartridge.                                    |
| `DEBUG IN USE`                      | Another front end owns the debugger. Close its session, or wait 3 seconds if it is unresponsive.                             |
| `NO FREE BRK SLOT`                  | All 10 breakpoint slots are used. Clear one with `R` or from the `C=+R` list.                                                |
| `BRK <target>, CPU <current>; not mapped now` | The breakpoint is set in a memory source the live banking does not map. It only fires once the program banks `<target>` in. |

### Leaving Debug and interrupt state

Leaving Debug always hands the CPU back to a live runtime. The debugger restores everything it patched: `BRK` opcodes in RAM and in the volatile U64 ROM image, the BRK, IRQ, and NMI vectors, the `$00`/`$01` banking registers, and the cassette-buffer region used by the handler and trampolines.

Interrupt state on resume follows the banking of the resumed program:

- A program running with KERNAL mapped resumes with interrupts enabled, so the jiffy clock, cursor, and keyboard stay alive.
- A program running with KERNAL banked out (`$01` HIRAM clear) resumes with interrupts left masked, because there is no KERNAL IRQ handler at `$FFFE` and forcing interrupts on would wedge it. Liveness for such a program shows as program progress, not as a running jiffy clock.

There is one boundary worth knowing. A program that runs with KERNAL mapped and intentionally keeps interrupts disabled, for example a raster effect that has executed `SEI` and has not yet reached its `CLI`, resumes with interrupts enabled if you leave the debugger inside that window. The machine stays live and never needs a power cycle. To preserve a disabled-interrupt state across a resume, set a breakpoint past the critical section and use `G` rather than leaving the debugger inside it.

### Help screen

`F3` or `?` shows the Debug help screen while Debug is active.

It keeps the normal help layout, replaces the keys Debug owns with Debug actions, and highlights those Debug shortcuts with the same accent color used for the `Dbg` and `Edit` header flags.

### Hardware support

| Capability                                        | U64 (Elite)                       | U2 / U2+ cartridge                                    |
| ------------------------------------------------- | --------------------------------- | ------------------------------------------------------ |
| Memory view, edit, fill, compare                  | Yes                               | Yes                                                    |
| `G` jump to address                               | Yes                               | Yes                                                    |
| BRK-based step / over / into / out                | Yes                               | Yes, in writable RAM                                   |
| Breakpoints in C64 RAM                            | Yes                               | Yes                                                    |
| Breakpoints in BASIC / KERNAL / CHAR ROM          | Yes, volatile U64 ROM-image patch | Not available, C64 ROM is read-only from the cartridge  |
| Per-row memory source tag (`[KRN]`, `[RAM]`, ...) | Yes                               | Not available, every row is tagged `[CPU]`             |
| Monitor-side CPU bank selection (`O`)             | Yes                               | Not available, footer shows `CPU BANK N/A`             |
| Monitor-side VIC bank selection (`SH+O`)          | Yes                               | Not available, footer shows `VIC N/A`                  |
| Freeze toggle (`Z`)                               | Yes                               | Not available                                          |
| REST `/v1/machine` memory API                     | Yes                               | Yes                                                    |

On the cartridge, the debugger launches a step by pulsing the cartridge NMI line. A U2+L plugged into a C64U host does not step, because that host does not forward the cartridge NMI to its internal 6510. On a real C64 the NMI arrives and stepping works.
