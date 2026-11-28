# Machine Code Monitor

The Machine Code Monitor is a keyboard-driven tool for inspecting and editing live or frozen C64 memory.

It supports hexadecimal, ASCII, screen-code, binary, and assembly views, plus inline editing, bulk memory operations, file load/save, and execution from a selected address.

You can also debug an assembly program by setting breakpoints, stepping through its execution, and observing its effects on memory and CPU state.

## Entry and Exit

`C=` denotes the Commodore key. For example, `C=+O` means: hold the Commodore key, then press `O`.

To open the monitor, first open the device menu, then use one of the following:

- Press `C=+O`.
- Press `F5`, open `Developer`, then select `Machine Code Monitor`.

Open the built-in help with `F3` or `?`. It is laid out in three blocks separated by one blank row: the views and what modifies them, the commands that act on memory, and the keys that need `C=` or a named key. `F3`, `?` or Back close it; any other key closes it and then acts as usual.

```text
M Memory     I ASCII      V Screen
A Assembly   B Binary     U Undoc/Case
W Width      O CPU Bank   SH+O VIC

E Edit       F Fill       T Transfer
C Compare    H Hunt       N Number
R Range      Z Freeze     P Poll
L Load       S Save       J Jump
G Go         D Debug

C=+B      Bkmrks   C=+0-9   Bkmrk Jmp
?/F3      Help     C=+O     Monitor
C=+E      Edit off C=+C/V   Copy/Paste
RSTOP/<-  Back     RETURN   Follow/Ret
C=+R      Reset    C=+I     Interface
F1/SH+SPC Page Up  F7/SPACE Page Down
```

While the monitor is open, `C=+R` resets the machine without confirmation. Outside Debug it works from the memory views, Edit and help, and closes the monitor. In Debug it also works from the monitor's lists and pickers, except the Load and Save file picker, and the monitor stays in Debug. It does not work while a command prompt or message box is open. On hardware that cannot reach a reset the monitor shows `RESET UNAVAILABLE` and leaves the machine, the view and edit mode unchanged.

To close the monitor:

- Press `C=+O` again.
- Press `RUN/STOP`, `ESC`, or the C64's top-left `←` key when no edit mode, Debug mode, or popup is active.

`RUN/STOP`, `ESC` and `←` are one Back action. Each press closes one active layer - help, a number expression, a popup, a command prompt, edit mode - and closes the monitor only once nothing is left. In Debug mode it leaves Debug and stays in the monitor; in Edit mode it leaves Edit first. The breakpoint list also closes with `C=+P`, and a message box with an `Ok` button closes with `RETURN`. Where `←` is data, in ASCII and Screen editing and in the ASCII and Screen rows of the Number popup, use `RUN/STOP` or `ESC` instead.

Two shortcuts act on the machine rather than the view, and both work from a memory view and from edit mode:

| Key | Action |
| --- | ------ |
| `C=+R` | Reset the C64. Outside Debug the monitor closes, and so does an on-device menu drawn on the C64 screen. |
| `C=+I` | Swap the interface between the freeze menu and the HDMI overlay, and close the menu. The setting takes effect the next time the menu opens. |

## Access Modes

The machine code monitor can be opened in three ways:

| Mode           | C64 while monitor is open | Video stream           | Use this when                                                                                |
| -------------- | ------------------------- | ---------------------- | -------------------------------------------------------------------------------------------- |
| **UI Freeze Mode**  | Frozen                    | Monitor is visible     | You want full-screen monitor use, automatic freezing, or monitor output in the video stream. |
| **UI Overlay Mode** | Running, but can be frozen with the `Z` shortcut or by stopping in Debug mode | Monitor is invisible | You want to use the monitor while the C64 keeps running.                                     |
| **Telnet**     | Ditto                   | Monitor is invisible         | You want to use the monitor from another machine or in an automated way.                                            |

UI Overlay Mode and the `Z` freeze exist on the Ultimate 64 only. UI Overlay Mode needs a connected HDMI display; without one the menu opens in UI Freeze Mode.

While the machine is frozen, the firmware's own menu is using screen RAM, the 2 KB above it and color RAM for its
display. The monitor reads and writes those three ranges, `$0400-$07FF`, `$0800-$0FFF` and `$D800-$DBFF`, in the copy
taken at freeze time, which is put back when the machine unfreezes. So what you see and edit there is the frozen
program's memory, not the menu that is on the screen in front of you, and an edit lands in the program when it
resumes.

## Screen Layout

The machine code monitor screen has three fixed regions: header, body, and footer.

```text
+--------------------------------------+
|MONITOR ASM $E011  Undoc Frz  Dbg EDIT|
|...                                   |
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

### Header

The header shows the current monitor view, the cursor address, and any active mode indicators.

Mode indicators may include any combination of the following:

| Indicator | Meaning                                               |
| --------- | ----------------------------------------------------- |
| `Undoc`   | Undocumented opcodes are decoded (Assembly view only) |
| `Range`   | Range selection is active                             |
| `Frz`     | Freeze is active                                      |
| `Poll`    | Polling is active                                     |
| `Dbg`     | Debug mode is active                                  |
| `EDIT`    | Edit mode is active                                   |

While range selection (`R`) is active in the Screen or Binary view, the header omits the word `MONITOR`, so that the address fits beside the `Range` indicator.

Each indicator has a fixed slot, counted back from the right edge of the header. `Undoc` and `Range` share one slot, so only one of them is shown at a time. `Poll` and `Dbg` share a slot too, and entering Debug turns poll off, so only one of those is ever shown.

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

The footer takes one of two forms, depending on the hardware:

| Hardware       | Footer                                                                                       |
| -------------- | -------------------------------------------------------------------------------------------- |
| Ultimate 64    | `CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000`, or `C7O5 ...` when the two banks differ              |
| U2+ / U2+L     | The same line once the live CPU port is known, and `CPU VIEW  VIC0 $0000` until then         |

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
|MONITOR HEX $00E0                     |
|00E0 85 85 85 85 85 85 86 86 ........ |
|00E8 86 86 86 86 86 87 87 87 ........ |
|00F0 87 87 87 F0 D8 00 00 00 ........ |
|00F8 00 00 00 00 00 00 00 20 .......  |
|0100 33 38 39 31 31 00 30 30 38911.00 |
|0108 30 30 0E 00 36 05 00 85 00..6... |
|0110 14 00 36 05 00 85 1A 00 ..6..... |
|0118 36 05 00 85 20 10 34 05 6... .4. |
|0120 00 85 26 00 36 05 00 85 ..&.6... |
|0128 2C 00 36 05 00 85 32 00 ,.6...2. |
|0130 36 05 00 85 38 00 36 05 6...8.6. |
|0138 00 85 3E 00 37 05 00 85 ..>.7... |
|0140 44 00 36 05 00 85 4A 00 D.6...J. |
|0148 36 05 00 85 50 00 36 05 6...P.6. |
|0150 00 85 56 10 34 05 00 85 ..V.4... |
|0158 5C 00 36 05 00 85 62 00 \.6...b. |
|0160 36 05 00 85 68 00 36 05 6...h.6. |
|0168 00 85 6E 00 36 05 00 85 ..n.6... |
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

Each row shows the row address, then the bytes of the row in hexadecimal, then
the same bytes as printable characters. A byte that has no printable character
is shown as `.`.

The row holds eight bytes by default. `W` switches the row to sixteen bytes,
which uses the width the character preview occupies, so only the hexadecimal
bytes are shown in that mode.

### Assembly View

Assembly view shows decoded 6510 instructions, their instruction bytes, and the memory source used for each row.

Rows are decoded forward from the top row. The address you last jumped to always starts an instruction: an instruction above it that would reach across it is shown as `.BYTE`. To decode from a different byte, jump to it with `J`.

The tag at the right end of each row names the memory source the byte was read from: `[RAM]`, `[BAS]`, `[KRN]`, `[CHR]`, or `[I/O]`. The tag is three characters inside the brackets, so the column stays aligned across a bank boundary.

On the U2+ cartridge the monitor reads through the CPU-visible aperture, so it can only name a source once it knows the 6510's port. Until then every row is tagged `[CPU]`.

`$D000-$DFFF` is shown as `DATA` rows of two bytes each while I/O or character ROM is banked in. An I/O read returns a live register, so a decoded instruction length there would change on every redraw, and character ROM holds bitmaps that never were code. With RAM banked in, the same addresses are disassembled normally, so the rule follows the banked source rather than the address range. `DATA` rows are grouped from the start of the region, so where a row begins does not depend on how the view arrived there.

A `DATA` row is edited like any other row. `E` enters edit mode with the cursor on the first byte, each displayed byte is its own edit position, two hex digits complete one byte, and `Left` / `Right` step from byte to byte and on into the row above or below. There is no mnemonic to pick on a `DATA` row. `[I/O]` is writable. A write to a `[CHR]` row goes to the RAM underneath, so the displayed ROM byte does not change. The same bytes can also be edited in Memory view with `M`. `DEL` clears a `DATA` row's bytes to `$00`, where on a decoded instruction it writes `NOP`.

The two-byte row is how the bytes are shown, not what a range is made of. A range anchored with `R` on a `DATA` byte covers the bytes between its ends: anchoring on `$D001`, moving right to `$D002` and pressing `R` copies those two bytes and nothing else. A range that starts on a decoded instruction still takes that instruction whole, so a range may cross between code and data without either end losing bytes. In edit mode, `C=+C`, `N` and `R` on a `DATA` row take the byte under the cursor; `N` and `R` are available there from the second byte of a row.

Assembly view also allows you to assemble instructions inline (in `E`dit mode) and to debug code (in `D`ebug mode).

See the **Edit Mode** and **Debug Mode** chapters below for more information.

Example, spanning the boundary between banked-in I/O and KERNAL ROM:

```text
+--------------------------------------+
|MONITOR ASM $DFF6                     |
|DFF6 00 8D     DATA 00 8D        [I/O]|
|DFF8 02 92     DATA 02 92        [I/O]|
|DFFA C6 F7     DATA C6 F7        [I/O]|
|DFFC 00 A5     DATA 00 A5        [I/O]|
|DFFE 00 A5     DATA 00 A5        [I/O]|
|E000 85 56     STA $56           [KRN]|
|E002 20 0F BC  JSR $BC0F         [KRN]|
|E005 A5 61     LDA $61           [KRN]|
|E007 C9 88     CMP #$88          [KRN]|
|E009 90 03     BCC $E00E         [KRN]|
|E00B 20 D4 BA  JSR $BAD4         [KRN]|
|E00E 20 CC BC  JSR $BCCC         [KRN]|
|E011 A5 07     LDA $07           [KRN]|
|E013 18        CLC               [KRN]|
|E014 69 81     ADC #$81          [KRN]|
|E016 F0 F3     BEQ $E00B         [KRN]|
|E018 38        SEC               [KRN]|
|E019 E9 01     SBC #$01          [KRN]|
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000  |
+--------------------------------------+
```

### Binary View

Binary view shows each byte as eight bits, using `.` for a cleared bit and `*` for a set bit. It is useful for inspecting registers, character glyphs, sprite data, and other bit-oriented memory.

Because C64 sprite data uses 3 bytes per row, binary view supports multiple `W`idth modes for viewing bytes in different groupings.

The top status line shows the current byte address followed by the selected bit number, for example `$D00C/7`. Bit 7 is the most significant bit on the left, and bit 0 is the least significant bit on the right.

Example:

```text
+--------------------------------------+
|MONITOR BIN $D00C/7                   |
|D008 ...**... 18                      |
|D009 ..****.. 3C                      |
|D00A .**..**. 66                      |
|D00B .******. 7E                      |
|D00C .**..**. 66                      |
|D00D .**..**. 66                      |
|D00E .**..**. 66                      |
|D00F ........ 00                      |
|D010 .*****.. 7C                      |
|D011 .**..**. 66                      |
|D012 .**..**. 66                      |
|D013 .*****.. 7C                      |
|D014 .**..**. 66                      |
|D015 .**..**. 66                      |
|D016 .*****.. 7C                      |
|D017 ........ 00                      |
|D018 ..****.. 3C                      |
|D019 .**..**. 66                      |
|CPU1 $A:RAM $D:CHR $E:RAM VIC0 $0000  |
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
- `G`: execute from an address. Where the menu holds the machine frozen, the monitor closes; over Telnet and in an overlay on a running machine it stays open.
- `F1` or `Shift+Space`: page up.
- `F7` or `Space`: page down.
- Assembly view, non-edit mode: `Up` / `Down` move to the previous / next instruction root; `Left` / `Right` move the cursor by one byte; if it lands inside an instruction, disassembly restarts at that byte.
- `Enter`: in Assembly view, follow the target of a jumpable instruction, or return to the most recent saved source location when the current instruction is not jumpable and the follow stack is non-empty.
- `O`: cycle the monitor-view CPU port banking, `CPU0`..`CPU7`. This changes the monitor view only; it does not write `$0001`. Ultimate 64 only.
- `Shift+O`: cycle the VIC bank override.
- `Z`: toggle freeze.
- `P`: toggle poll mode in the local monitor. Poll mode is unavailable over telnet, and entering Debug turns it off.

Addresses in command prompts are hexadecimal.

`Z` freezes the running machine so registers and I/O stay stable across many reads and writes, and unfreezes it again. It is available when the machine is not already held by the freezer. In UI Freeze mode the machine is already frozen and `Z` shows `FREEZE ONLY IN OVERLAY MODE`. On hardware without freeze support it shows `FREEZE UNAVAILABLE`.

### Follow/Return

Follow code flow in the Assembly view:

- `Enter` follows the resolved target when the cursor is on a jumpable instruction: `JSR`, `JMP` absolute, `JMP` indirect, or any of `BEQ`, `BNE`, `BCC`, `BCS`, `BMI`, `BPL`, `BVC`, `BVS`. For `JMP` indirect the monitor reads the vector and follows the address stored there; as on the 6510, a vector at `$xxFF` takes its high byte from `$xx00`.
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

After `Reset C64` from the task menu, the file browser or the REST API, the next monitor open syncs its view bank to the live CPU execution bank. `C=+R` inside the monitor keeps the view bank.

An ordinary monitor close/reopen also preserves a manually selected `O` view bank.

`CPU0` to `CPU7` are shorthand for the three 6510 port memory-configuration bits at `$0001`: `LORAM`, `HIRAM`, and `CHAREN`.

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

Selecting a VIC bank writes `$DD00`. A running program sees the change at once, and a frozen or stopped program when it resumes.

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

In Assembly edit mode, `Left` / `Right` move between editable parts of the current instruction. They do not change the disassembly root unless the cursor is already at the first or last editable part, where the existing row-to-row edit navigation applies. `Up` / `Down` move to the previous / next instruction row. Typed digits are written at once, and `Return` advances to the next instruction.

Typing a letter in the mnemonic field opens the opcode picker. A branch operand is edited as its absolute target address; an edit that would need an offset outside `-128..127` is refused and the offset byte is left untouched.

`DEL` is logical delete, not raw backspace:

| View         | `DEL` behavior                                  |
| ------------ | ----------------------------------------------- |
| Memory       | Writes `$00` and advances                       |
| ASCII/Screen | Writes a space                                  |
| Binary       | Clears the selected bit                         |
| Assembly     | Replaces the current instruction with `NOP` bytes; clears a `DATA` row to `$00` |

In Memory view, `DEL` first rolls back a half-typed nibble. In Assembly view, `DEL` first undoes the characters typed on the current instruction. Moving to another address discards a half-typed digit; in Assembly view, a digit that is already written stays in memory.

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

The popup title shows the target address and whether the target is a `BYTE` or a `WORD`. In Assembly view the target is the operand of the current instruction; for an instruction without an operand it is the byte after the instruction. On a `DATA` row the target is the byte under the cursor.

The ASCII and Screen rows in the number tool use the same mappings as the ASCII and Screen views.

Number tool controls:

| Key         | Action                                                      |
| ----------- | ----------------------------------------------------------- |
| `Up`/`Down` | Select the row to type in                                   |
| Typing      | Build a new value in the selected row's notation            |
| `DEL`       | Remove the last typed character; with none left, preview the target |
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
| `T` | Transfer | `start-end,dest[,code-start-code-end]`   | Copy a range to a destination, optionally relocating operands         |
| `C` | Compare  | `start-end,dest`                         | Compare a range against another location and list differing addresses |
| `H` | Hunt     | `start-end,bytes` or `start-end,"text"` | Search for a byte sequence or quoted ASCII string                     |

`Fill`, `Transfer`, `Compare`, `Hunt` and `Save` all treat `start-end` as inclusive of both ends, including the full `0000-FFFF` range.

`Transfer` takes an optional fourth field naming the range to scan for pointers into the block being copied:

```text
T C000-C0FF,C100,C000-C07F
```

Absolute, absolute-indexed and indirect operands pointing inside the copied source range are then adjusted to the corresponding destination address. Relative branches, zero-page operands and references outside the copied range are left unchanged. Without the fourth field, `Transfer` copies the bytes and changes nothing.

The scan range is independent of the range being copied. It may be shorter than the copy, longer than it, or somewhere else entirely, which is what lets a pointer that is not itself moving be brought with the block:

```text
T C000-C005,C010,C000-C008
```

Here the first two instructions are copied to `$C010` while the scan covers a third instruction that stays where it is. An instruction wholly inside the copy is rewritten in the copy, because that is the version being relocated. An instruction wholly outside it is rewritten where it stands. An instruction whose three bytes straddle the end of the copy is left alone, since writing its operand would put one byte in the copy and the other in the original.

`Hunt` and `Compare` open a result picker. Its header shows the selected match and the number of matches, for example `Hunt results  1/20`. The picker has these controls:

| Key                       | Action                    |
| ------------------------- | ------------------------- |
| `Up`/`Down`               | Select a match            |
| `F1`/`F7`, `Home`/`End`   | Page or jump to the ends  |
| `Return`                  | Jump to the selected match |
| `RUN/STOP`                | Close the picker          |

Both pickers list at most 256 matches. A search with no result shows `No matches` or `No differences`.

A command prompt accepts only characters that can occur in the command being entered; other keys are ignored. Parsing and validation still happen on `Return`. On Fill and Transfer, `Return` on the untouched placeholder closes the prompt without running the command.

## File I/O

- `L`: load a file into memory.
- `S`: save memory to a file.

Files may exist directly in the Ultimate filesystem or inside a disk image such as `.D64`.

The Load and Save file picker ignores `C=+O`, `C=+R` and `C=+I`. Load and Save use the whole file name, up to 127 characters.

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
| `Offset`        | Number of bytes to skip: after the PRG header with `PRG`, else from the file start |
| `Len` or `AUTO` | Load the automatically determined length, or load an explicit byte count        |

Examples:

| Input            | Meaning                                           |
| ---------------- | ------------------------------------------------- |
| `PRG`            | Load a PRG to its embedded load address           |
| `0801`           | Load the whole file, header included, to `$0801`  |
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

- Select an existing file by pressing `ENTER` on it, then choosing `Select` from the context-sensitive menu. The monitor asks before it overwrites the file; answering `No` leaves the file unchanged and returns to the monitor.
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

## Debug Mode

Debug mode runs your program on the C64's own 6510 under your control. You can execute it one instruction at a time, stop it at breakpoints, and see the CPU registers after every stop. Debug works in the Assembly view, and the other monitor commands stay available between stops.

To stop a program, the debugger writes a temporary `BRK` where execution should stop, and puts the original byte back at the stop. A breakpoint therefore needs memory the debugger can write to: RAM, and on the Ultimate 64 also BASIC and KERNAL ROM (see [Hardware support](#hardware-support)).

### Quick start

This example uses a short program at `$C000`:

```text
C000  LDA #$2A
C002  LDX #$05
C004  LDY #$03
C006  JSR $C020
C009  NOP
C00A  JMP $C000
...
C020  INX
C021  RTS
```

1. Open the monitor and press `D`. The header shows `Dbg`.
2. Press `J`, type `C000` and press `RETURN`.
3. Press `T`. `LDA #$2A` runs. The two rows above the footer show the CPU registers, and the next instruction is marked `>LDX #$05<`.
4. Press `T` twice. The program stops on the `JSR`.
5. Press `D` to run the subroutine and stop at `$C009`. To go through it instead, press `T` to enter it and `U` to return.
6. Move the cursor to `$C00A` and press `P` to set a breakpoint. Move the cursor back to the marked instruction and press `G`.
7. Press `RUN/STOP` to leave Debug.

### Starting and leaving Debug

`D` starts Debug in the Assembly view. It turns Poll off, because `P` sets breakpoints. Starting Debug runs nothing.

Until the first stop the CPU registers are unknown and shown blank. `T`, `D` and `G` start at the cursor address. `K` starts where Debug started and runs to the cursor. Moving the view with the cursor keys, `J`, a bookmark or a follow does not change where execution starts. After a stop, `T`, `D`, `U`, `G` and `K` continue from the stop.

| Key                 | Effect                                                                   |
| ------------------- | ------------------------------------------------------------------------ |
| `C=+D`              | Leave Debug.                                                             |
| `←`, `RUN/STOP`, `ESC` | Leave Debug. In Edit mode, the first press leaves Edit.               |
| `C=+O`              | Leave Debug and close the monitor.                                       |
| `C=+R`              | Reset the C64. Debug stays on, with blank registers.                     |

The menu button, and disconnecting the Telnet client, close the monitor and leave Debug.

Only one session can debug at a time. Another session gets `DEBUG IN USE` until the owner leaves Debug or closes its monitor.

### Debug keys

| Key        | Command            | What it does                                                                   |
| ---------- | ------------------ | ------------------------------------------------------------------------------ |
| `T`        | Step Into          | Execute one instruction. On a `JSR`, stop at the first instruction of the subroutine. |
| `D`        | Step Over          | Execute one instruction. On a `JSR`, run the subroutine and stop at the instruction after the `JSR`. |
| `U`        | Step Out           | Run until the current subroutine returns.                                      |
| `G`        | Continue           | Run until an enabled breakpoint.                                               |
| `K`        | Continue To Cursor | Run until the Assembly cursor address.                                         |
| `P`        | Breakpoint         | Set or clear a breakpoint at the cursor address.                               |
| `C=+P`     | Breakpoint list    | Open the list of all breakpoints.                                              |
| `RETURN`   | Follow / Return    | Show the target of a `JSR`, `JMP` or branch, or go back. Nothing is executed.  |
| `F3` / `?` | Help               | Show the Debug help screen.                                                    |

Outside Debug, `T`, `U`, `G` and `P` are Transfer, the undocumented-opcode toggle, Go and Poll. All other keys keep their normal meaning. In Edit mode, keys are data or ordinary monitor commands, not Debug keys.

### Reading the screen

While the program is stopped, the next instruction is marked with brackets, for example `>LDA $07<`. The marker stays on that instruction while you scroll.

- On a marked `JSR`, absolute `JMP` or taken branch, the target address is shown in the accent color.
- A marked `RTS` shows its return address, for example `RTS $E5D2`, or `RTS $????` with an empty stack.
- The `[BRKn]` marker of an enabled breakpoint is shown in the accent color.

After each step the view follows the program counter. After a jump, the new instruction is shown three rows from the top.

The two rows above the footer show the CPU state:

```text
PC   AC XR YR SP NV-BDIZC IRQ  NMI
C006 2A 05 03 F3 00110100 EA31 FE47
```

| Field              | Meaning                                                                         |
| ------------------ | ------------------------------------------------------------------------------- |
| `PC`               | Program counter: the address of the next instruction                            |
| `AC` / `XR` / `YR` | Accumulator, X register and Y register                                          |
| `SP`               | Stack pointer. The stack is at `$0100` + `SP`                                   |
| `NV-BDIZC`         | Status register, one digit per flag from bit 7 to bit 0. Set flags are highlighted in the label row. |
| `IRQ`              | IRQ vector at `$0314/$0315`                                                     |
| `NMI`              | NMI vector at `$0318/$0319`                                                     |

In the example, `-` and `B` are 1 because the program is stopped by a `BRK`, and `I` is 1 because interrupts are disabled.

### Stepping and running

The CPU executes from the memory banked in through `$01`, not from the bank selected with `O`. After each stop on an Ultimate 64, the view switches to the bank the CPU is using.

- `T` executes one instruction. An `RTS` goes to the address on the stack. Outside a subroutine entered with `T`, it is refused when that address does not follow a `JSR`.
- `D` runs a `JSR` as one step, including calls into KERNAL or BASIC. For any other instruction it does the same as `T`.
- `U` needs a stop first. After `T` into subroutines it works at any depth. After a breakpoint or `K` inside a subroutine it works while the return address is on top of the stack. Otherwise it shows `NOT IN SUBROUTINE`.
- `G` runs until an enabled breakpoint. With no enabled breakpoint, the program runs freely and Debug ends; on the C64 or HDMI screen the monitor also closes.
- `K` runs until the cursor address, or an earlier enabled breakpoint.
- `U`, `G` and `K` step past a breakpoint the program is stopped on. It stays set, and stops the program the next time it gets there. If it is on a `BRK` or an undocumented opcode, it stays unarmed for that whole run, and `G` is refused when no other breakpoint is enabled.

On the Ultimate 64, a `BRK` in the program also stops `G` and `K`.

If no breakpoint is reached within a few seconds, the debugger shows `DEBUG TIMEOUT`.

While the debugger waits, `RUN/STOP`, `ESC`, `C=+D` or `C=+O` shows `DEBUG CANCELLED` and Debug stays on. `C=+R` resets the C64. After a timeout or cancel the program runs on; while the menu on the C64 screen holds the machine frozen, it runs once the menu closes.

### Where you can step

Code in ROM, and in RAM under ROM, is limited until the program has stopped once:

| Program counter is in          | Available before the first stop                  |
| ------------------------------ | ------------------------------------------------ |
| RAM or I/O space               | All but `U`, and `T` / `D` on `RTS` or `RTI`     |
| RAM under BASIC, KERNAL or I/O | All but `T` and `U`, and `D` on `RTS` or `RTI`   |
| BASIC or KERNAL ROM            | `G`, `K`, and `D` on a `JSR`                     |

`T`, `D`, `U` and `K` write their own `BRK` into I/O space. Cartridge RAM at `$DE00-$DFFF` can execute it. A register changes instead, and shows `PATCH FAILED` if it does not read back `$00`.

To get the first stop, set a breakpoint, move the cursor to where execution should start and press `G`, or use `D` on a `JSR`.

In ROM and in RAM under ROM, the debugger carries out simple instructions itself while the CPU waits. No interrupt is taken during such a step. An I/O access happens once, so `INC $D019` writes the register once instead of twice, and an indexed read that crosses a page makes no extra dummy read. An instruction that writes `$01` runs on the 6510 and changes the banking.

While the menu on the HDMI or C64 screen holds the machine frozen, `D` on a `JSR` into ROM and `U` from ROM are completed one instruction at a time. This stops early at an enabled breakpoint, at a `BRK` or undocumented opcode, or after 8192 instructions. Press `U` to finish the subroutine, or `G`.

On an Ultimate II+ or II+L cartridge, the debugger can only stop and step code in RAM.

### Breakpoints

There are 10 breakpoints, numbered `0` to `9`.

- `P` sets a breakpoint at the cursor address, or clears the one that is there. If all 10 are in use, it shows `NO FREE BRK SLOT`.
- A row with a breakpoint shows `[BRKn]`, or the breakpoint's label, before its memory tag, for example `[BRK0][BAS]` or `[LOOP][BAS]`.
- Only enabled breakpoints stop the program.
- Breakpoints stay set through a reset, leaving Debug and closing the monitor. They are cleared when the device is switched off.

#### Breakpoints under ROM and I/O

At `$A000-$BFFF`, `$D000-$DFFF` and `$E000-$FFFF`, a breakpoint belongs to the memory selected with `O` when you set it, as shown by the memory tag. `$E000` in KERNAL and `$E000` in RAM are separate breakpoints. A breakpoint stops the program only while the program has that memory banked in. A breakpoint in I/O is refused with `PATCH FAILED` when the program is run.

An Ultimate II+ or II+L cartridge has no `O` selection: at `$A000-$BFFF` and `$E000-$FFFF`, a breakpoint belongs to the memory the CPU has banked in when you set it. A breakpoint in RAM there is also refused with `PATCH FAILED` if the program is run while ROM is banked in. RAM under I/O cannot have a breakpoint on a cartridge.

If it is not banked in when you set the breakpoint, the monitor shows `BRK <memory>, CPU <memory>; not mapped now`: the memory of the breakpoint, then the memory the CPU has at that address.

#### Breakpoints in ROM

On the Ultimate 64 the debugger patches the ROM copy in the device's memory, never the flash, while the program runs, and restores the bytes at each stop.

An Ultimate II+ or II+L cartridge cannot change the C64's ROM. While a breakpoint in ROM is enabled, `D`, `U`, `G` and `K` show `BRK $xxxx IN ROM BLOCKS DEBUG`. Clear or disable it.

#### Breakpoint list

`C=+P` opens the list, in Debug and, once a breakpoint is set, outside it. Each slot shows `SET`, `OFF` or `EMPTY`. The help row uses the short names shown in brackets.

| Key                           | Action                                                     |
| ----------------------------- | ---------------------------------------------------------- |
| `Up` / `Down`                 | Select a slot                                              |
| `RETURN`, `0`-`9`             | Show the breakpoint's address (`Jmp`)                      |
| `S`                           | Set the slot to the cursor address and enable it (`Set`)   |
| `L`                           | Give the breakpoint a label of up to 4 characters (`Lbl`)  |
| `E`                           | Enable or disable the breakpoint (`Enbl`)                  |
| `DEL`                         | Clear the breakpoint (`Res`)                               |
| `←`, `RUN/STOP`, `ESC`, `C=+P` | Close the list                                            |
| `C=+O`                        | Close the monitor                                          |

Showing a breakpoint moves the view and selects its memory bank. The program stays stopped where it was.

### Memory the debugger uses

| Range         | Used for                                                                          |
| ------------- | --------------------------------------------------------------------------------- |
| `$0314-$0319` | Interrupt vectors. The debugger redirects BRK and NMI while it needs them.        |
| `$0340-$035C` | Work area for executing single instructions.                                      |
| `$035D-$03FB` | The debugger's own code and the saved CPU registers.                              |

The debugger also changes the vectors at `$FFFA` and `$FFFE` temporarily and uses a few stack bytes below `SP`. Keep no data in `$0340-$03FB`. Leaving Debug while the program is stopped puts back what the debugger wrote there, except `$035D-$0377` and `$0387-$0389`, which the CPU runs to return to the program. A breakpoint in `$0314-$0319` or `$0340-$03FB` is refused with `PATCH FAILED` when the program is run.

### Debug messages

The two messages that start with `Step` appear on the bottom row. All others appear in a popup.

| Message                                      | Meaning                                                                                                    |
| -------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| `Step Into: run to a breakpoint 1st`         | `T` in ROM or RAM under ROM before the first stop. Set a breakpoint and press `G`.                          |
| `Step Over: run to a breakpoint 1st`         | `D` on an instruction in ROM, other than a `JSR`, before the first stop.                                    |
| `UNSUPPORTED OPCODE`                         | `T` or `D` met an undocumented opcode. The program stays stopped.                                             |
| `UNSAFE TARGET`                              | The next instruction is a `BRK`, or an `RTS` / `RTI` before the first stop.                                 |
| `NOT IN SUBROUTINE`                          | `U` found no caller, or `T` / `D` met an `RTS` with no matching `JSR` outside a subroutine entered with `T`. Set a breakpoint at the return address and press `G`. |
| `RETURN NOT REACHED`                         | `U` did not stop at the caller.                                                                            |
| `PATCH FAILED`                               | A breakpoint is in I/O, the address is in a reserved range or is RAM under a banked-in ROM on a cartridge, or memory did not accept the `BRK`. |
| `NO FREE BRK SLOT`                           | All 10 breakpoints are in use.                                                                             |
| `BRK <memory>, CPU <memory>; not mapped now` | The breakpoint is set in memory the CPU does not have banked in.                                           |
| `DEBUG TIMEOUT`                              | No breakpoint was reached in time.                                                                         |
| `DEBUG CANCELLED`                            | You stopped waiting with a key.                                                                            |
| `DEBUG NOT SUPPORTED`                        | This device cannot do it, for example stepping ROM code on a cartridge.                                    |
| `BRK $xxxx IN ROM BLOCKS DEBUG`              | A cartridge has an enabled breakpoint in ROM. Clear or disable it.                                         |
| `DEBUG IN USE`                               | Another session has the debugger.                                                                          |

### Leaving Debug

When you leave Debug, the program continues from where it stopped. The debugger removes every `BRK` it wrote and restores the vectors and `$00` / `$01`.

- With KERNAL and I/O banked in, interrupts are enabled, so the cursor, keyboard and jiffy clock keep working.
- With KERNAL banked out (bit 1 of `$01` clear) or the character ROM banked in (bit 2 clear), the interrupt flag stays as the program had it.

To keep interrupts disabled in a program with KERNAL and I/O banked in, for example between `SEI` and `CLI` in a raster routine, do not leave Debug there. Set a breakpoint after the `CLI` and press `G`.

### Help screen

`F3` or `?` shows the Debug help screen while Debug is active.

```text
D Step Over  T Step Into  U Step Out
G Continue   K Cont Crsr  RET Follow
P Breakpt    C=+P Brkpts  C=+R Reset

M Memory     I ASCII      V Screen
A Assembly   B Binary     O CPU Bank
W Width      SH+O VIC     R Range
E Edit       F Fill       N Number
C Compare    H Hunt       Z Freeze
L Load       S Save       J Jump

C=+B      Bkmrks   C=+0-9   Bkmrk Jmp
?/F3      Help     C=+O     Monitor
C=+E      Edit off C=+C/V   Copy/Paste
C=+D      Dbg off  C=+I     Interface
RSTOP/<-  Back
F1/SH+SPC Page Up  F7/SPACE Page Down
```

### Hardware support

The monitor is built into the Ultimate II+, the Ultimate II+L, the Ultimate 64 and the Ultimate 64 II. The original Ultimate II does not have it.

| Capability                                        | Ultimate 64 / 64 II            | U2+ / U2+L cartridge                                         |
| ------------------------------------------------- | ------------------------------ | ------------------------------------------------------------ |
| Memory view, edit, fill, compare                  | Yes                            | Yes                                                          |
| `G` jump to address                               | Yes                            | Yes                                                          |
| Stepping and breakpoints in C64 RAM               | Yes                            | Yes                                                          |
| Stepping and breakpoints in BASIC / KERNAL ROM             | Yes                   | No, the cartridge cannot change the C64's ROM                |
| Memory tag per row (`[KRN]`, `[RAM]`, ...)        | Yes                            | Yes, once the monitor has read `$01`; `[CPU]` until then      |
| CPU bank selection with `O`                       | Yes                            | No; the footer shows the CPU's banking instead               |
| VIC bank selection with `SH+O`                    | Yes                            | Yes                                                          |
| Freeze toggle (`Z`)                               | Yes                            | No                                                           |
| REST `/v1/machine` memory API                     | Yes                            | Yes                                                          |

The debugger cannot stop on a condition, watch a memory address for changes, or record the instructions it ran.

On a cartridge, the debugger starts a program that is not stopped through the cartridge's NMI line. In a C64 Ultimate, set `C64 and Cartridge Settings` > `Cartridge Preference` to `External` and restart the C64 Ultimate; otherwise it does not pass the cartridge's NMI to the 6510, and steps do not start.
