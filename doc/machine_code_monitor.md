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

While the monitor is open, `C=+X` resets / breaks the U64 from any monitor mode, including Help, Edit, Debug, and popups.

To close the monitor:

- Press `C=+O` again.
- Press `RUN/STOP` when no edit operation or popup is active.

## Access Modes

The machine code monitor can be opened in three ways:

| Mode           | C64 while monitor is open | Video stream           | Use this when                                                                                |
| -------------- | ------------------------- | ---------------------- | -------------------------------------------------------------------------------------------- |
| **UI Freeze Mode**  | Frozen                    | Monitor is visible     | You want full-screen monitor use, automatic freezing, or monitor output in the video stream. |
| **UI Overlay Mode** | Running, but can be frozen via `Z` shortcut or in debug mode                  | Monitor is invisible | You want to use the monitor while the C64 keeps running.                                     |
| **Telnet**     | Dito                   | Monitor is invisible         | You want to use the monitor from another machine or in an automated way.                                            |

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

| Indicator | Meaning                          |
| --------- | -------------------------------- |
| `Undc`    | Undocumented opcodes are decoded |
| `Frz`     | Freeze is active                 |
| `Pl`      | Polling is active                |
| `Dbg`     | Debug mode is active             |
| `Edit`    | Edit mode is active              |

### Body

The body shows the memory region around the current cursor address.

The active cursor position is highlighted in reverse. Depending on the current operation, the body may also show popups such as search results, load/save prompts, or bookmark lists.

### Footer

The footer shows the current memory-bank context and temporary status information.

It includes:

- The CPU memory configuration used by the monitor view.
- Any difference between the monitor view and the live CPU execution bank.
- The selected VIC bank and its base address.
- Temporary bookmark information after jumping to a bookmark.

Common footer values include:

| Value            | Meaning                                                                                                                                    |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `CPU0` to `CPU7` | The monitor view and live CPU execution bank match.                                                                                        |
| `CxOy`           | The monitor view and live CPU execution bank differ. `Cx` is the live CPU execution bank. `Oy` is the monitor view bank selected with `O`. |
| `$A`, `$D`, `$E` | Show how the monitor view maps the main ROM/RAM regions.                                                                                   |
| `VIC0` to `VIC3` | Identify the selected VIC bank. The following address shows its base address.                                                              |

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

The highlighted address is the disassembly root: rows below it are decoded forward from that address, while rows above it are context only. Changing bytes before the highlighted address can refresh the context rows, but it will not silently change the instruction phase at the highlighted address. To inspect a different phase deliberately, move the root with the cursor keys or jump to the desired address.

It also allows you to assemble instructions inline (in `E`dit mode) as well as debug code (in `D`ebug mode).

Please see the **Edit Mode** respectively **Debug Mode** chapters below for more information.

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
- Assembly view, non-edit mode: `Up` / `Down` move to the previous / next instruction root; `Left` / `Right` move the decode root by one byte (`-1` / `+1`).
- `Enter`: in Assembly view, follow the target of a jumpable instruction, or return to the most recent saved source location when the current instruction is not jumpable and the follow stack is non-empty.
- `O`: cycle the monitor-view CPU port banking, `CPU0`..`CPU7`. This changes the monitor view only; it does not write `$0001`.
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

With the `O` and `Shift+O` keys, you can quickly toggle the CPU and VIC banks.

#### CPU Banking

The monitor shows two independent CPU banking states:

- **CPU execution bank**: The live bank used by the running 6510 CPU. It is derived from the lowest three bits of `$0001`, the 6510 on-chip port register. This is the bank from which the CPU fetches and executes instructions.
- **Monitor view bank**: The bank selected in the machine code monitor with the `O` key. This controls which memory mapping the monitor displays while you browse the 64 KiB address space.

When both banks are the same, the footer shows a single `CPUx` value, where `x` is a bank number from `0` to `7`:

```text
CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000
````

When the CPU execution bank and monitor view bank differ, the footer shows both values as `CxOy`:

- `Cx` is the live CPU execution bank.
- `Oy` is the monitor view bank selected with the `O` key.

Example:

```text
C7O5 $A:RAM $D:I/O $E:RAM VIC0 $0000
```

After a machine reset, the next fresh monitor open syncs its view bank to the live CPU execution bank, so
re-entry shows the memory the CPU is actually running.

An ordinary monitor close/reopen with no reset in between still preserves a manually selected `O` view bank.

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

Please note that cartridges can further affect the CPU-visible memory map through the expansion-port `GAME` and `EXROM` lines.

#### VIC Banking

`VIC0` to `VIC3` show the selected VIC bank, controlled by CIA 2 port A at `$DD00`:

| Field     | Address range |
| --------- | ------------  |
| `VIC0`    | `$0000-$3FFF` |
| `VIC1`    | `$4000-$7FFF` |
| `VIC2`    | `$8000-$BFFF` |
| `VIC3`    | `$C000-$FFFF` |

Any change to the VIC bank selection is visible to the CPU and can affect a running program unless you are in freeze mode or stopped at a breakpoint.

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
| Binary   | Type `0` or `Space` to clear the selected bit; type `1` or `*` to set it    |
| Assembly | Edit instructions inline with mnemonic completion and direct operand typing |

In edit mode, `Space` remains view-specific data entry and does not page.

In Assembly edit mode, `Left` / `Right` move between editable parts of the current instruction. They do not change the disassembly root unless the cursor is already at the first or last editable part, where the existing row-to-row edit navigation applies. `Up` / `Down` move to the previous / next instruction row.

`DEL` is logical delete, not raw backspace:

| View         | `DEL` behavior                                  |
| ------------ | ----------------------------------------------- |
| Memory       | Writes `$00` and advances                       |
| ASCII/Screen | Writes a space                                  |
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
| `T` | Transfer | `start-end,dest[,program-start-program-end]` | Copy a range; optional program range amends absolute operands that point into the moved range |
| `C` | Compare  | `start-end,dest`                         | Compare a range against another location and list differing addresses |
| `H` | Hunt     | `start-end,bytes` or `start-end,"text"` | Search for a byte sequence or quoted ASCII string                     |

`Hunt` opens a result picker:

`Transfer` normally copies bytes only. When the optional program range is
supplied, the monitor treats that range as 6502 code and amends every 16-bit
absolute operand whose value points into the moved source range so it points at
the destination copy. This applies to all absolute operands, not just `JMP` and
`JSR`.

- `Return`: jump to the selected match.
- `RUN/STOP`: close the picker.

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

## Debug Mode

Debug is a modal state layered on the Assembly view.

Entering Debug does not execute CPU instructions by itself; execution only happens when an explicit Debug command (`D` for Over, `T` for Trace, `U` for Out, `G` for Go) is pressed while Debug is active.

| Key | Outside Debug | Inside Debug |
| --- | --- | --- |
| `D` | Enter Debug, no execution | Debug (aka Step Over) |
| `T` | Transfer memory | Trace (aka Step Into) |
| `U` | Undoc/Case toggle | Step Out |
| `O` | CPU bank cycle | CPU bank cycle |
| `G` | Go / execute | Go |
| `R` | Range mode | Toggle breakpoint |
| `C=+R` | (unassigned) | Breakpoint list |
| `RUN/STOP` | Normal monitor close | Leave Edit first, then Debug |
| `C=+D` | (unassigned) | Exit Debug |
| `C=+X` | Reset / break the machine | Reset / break the machine |
| `RETURN` | Assembly follow / return | Assembly follow / return |

Notes:

- Important distinction between `RETURN` vs. `T`/`U`:
  - `RETURN` is non-executing subroutine navigation. This is to explore the code without executing it.
  - `T` (step into) and `U` (step out) is executing subroutine navigation. This is to follow the CPU's call stack.
- Inside Debug, `U` is Step Out, overriding the Assembly-view undocumented-opcode toggle. `O` remains the monitor view CPU bank cycle so the view bank can still be changed while Debug is active.
- In Debug + Edit, `RUN/STOP` / `ESC` unwind one mode at a time: the first press leaves `Edit` and keeps `Dbg`, the second leaves `Dbg`.
- `B` keeps Binary view and `C=+B` keeps the bookmark overview. Neither is repurposed for breakpoints.
- Current-PC disassembly, branch or jump prediction, stepping, and temporary step breakpoints follow the live CPU bank from `$0001`, not the monitor view selected by `O`.

### CPU footer

The bottom two rows of the monitor are reserved for a fixed-position CPU state table while Debug is active:

```text
PC   AC XR YR SP NV-BDIZC IRQ  NMI
C003 01 00 FF F7 00100100 C123 EA31
```

| Field | Meaning |
| --- | --- |
| `PC` | Program counter from the captured debug context |
| `AC` / `XR` / `YR` | Accumulator and index registers |
| `SP` | Stack pointer |
| `NV-BDIZC` | Status register bits 7..0 as an 8-character binary string |
| `IRQ` | RAM IRQ vector at `$0314/$0315`, when valid |
| `NMI` | RAM NMI vector at `$0318/$0319`, when valid |

Notes:

- The values between program counter and status register (inclusive) are highlighted in the same color as the `Dbg` or `Edit` header flags to improve visibility.
- Unknown values render as blank spaces in their reserved fixed-width columns; they never appear as zeros, `?`, or placeholder text. Field positions stay put when values become known.

### Breakpoints

There are 10 breakpoint slots:

- `R` toggles a breakpoint at the current Assembly address in the memory source enabled through `O`.
- Opcode rows with a breakpoint show `[BRKx]` immediately before the memory source marker, for example `[BRK0][BAS]`.
- Breakpoints are kept in volatile RAM. They survive a `C=+X` reset and an ordinary monitor close/reopen, but power-cycling the device clears them.
- Breakpoints refer to an address in a specific memory source. For example,`$E000 KRN` and `$E000 RAM` are distinct breakpoints and can coexist.
- A breakpoint can be valid but invisible to the live CPU. If you see the popup `BRK <target>, CPU <current>; not mapped now`, it means the breakpoint was set in `<target>`, but the running program must first activate the relevant bank so that the breakpoint will pause the CPU.
- On U64, breakpoints in BASIC, KERNAL, and character ROM use temporary patches in the volatile U64 ROM image, so ROM code remains step-capable without copying ROMs into C64 RAM or writing flash.
- RAM-under-KERNAL breakpoints work when KERNAL is banked out.

`C=+R` opens the breakpoint list which offers these shortcuts (the popup help row uses abbreviated forms in parentheses to fit the line):

| Key | Action |
| --- | --- |
| `Up` / `Down` | Select slot |
| `Return` | Jump to the selected slot |
| `0`-`9` | Jump directly to a slot (`Jmp`) |
| `S` | Store the current address into the selected slot (`Sto`) |
| `L` | Change the label, up to 4 chars (`Lab`) |
| `E` | Toggle slot enable / disable (`En`) |
| `DEL` | Reset the selected slot (`Res`) |
| `RUN/STOP` | Close the popup |

### Help screen

`F3` or `?` shows the Debug help screen while Debug is active.

It keeps the normal help layout, replaces the keys Debug owns with Debug actions, and highlights those Debug shortcuts with the same accent color used for the `Dbg` and `Edit` header flags.

`RETURN` remains non-executing follow / return navigation; `U` is executing step-out. `C=+X Reset` is the emergency reset / break shortcut.

### Hardware support

| Capability                              | U64 (Elite) | U2 / U2+ cartridge |
| --------------------------------------- | ----------- | ------------------ |
| Memory view, edit, fill, compare        | Yes         | Yes                |
| `G` jump to address                     | Yes         | Yes                |
| BRK-based step / over / trace / out     | Yes         | Yes                |
| Breakpoints in C64 RAM                  | Yes         | Yes                |
| Breakpoints in BASIC / KERNAL / CHAR ROM | Yes (volatile U64 ROM-image patch) | Not available - C64 ROM is read-only from the cartridge |
| Monitor-side CPU bank selection (`O`)    | Yes         | Not available - status line shows `CPU BANK N/A` |
| Monitor-side VIC bank selection (`SH+O`) | Yes         | Not available - status line shows `VIC N/A` |
| Freeze toggle (`Z`)                      | Yes         | Not available |
| REST `/v1/machine` memory API            | Yes         | Yes |
