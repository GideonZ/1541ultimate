# Machine Code Monitor

The Machine Code Monitor is a keyboard-driven tool for inspecting and editing live or frozen C64 memory.

It supports hexadecimal, ASCII, screen-code, binary, and assembly views, plus inline editing, bulk memory operations, file load/save, and execution from a selected address.

You can also debug an assembly program by setting breakpoints, stepping through its execution, and observing its effects on memory and CPU state.

## Entry and Exit

`C=` denotes the Commodore key. For example, `C=+O` means: hold the Commodore key, then press `O`.

To open the monitor, first open the device menu, then use one of the following:

- Press `C=+O`.
- Press `F5`, open `Developer`, then select `Machine Code Monitor`.

Open the built-in help with `F3` or `?`. It is laid out in three blocks separated by one blank row: the views and what modifies them, the commands that act on memory, and the keys that need `C=` or a named key. `F1`/`SH+SPACE` and `F7`/`SPACE` page it.

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

While the monitor is open, `C=+R` resets / breaks the machine. In Debug mode this reaches through any monitor mode, including Help, Edit, and popups; outside Debug it works from the ordinary monitor and memory views. There is no confirmation. On hardware that cannot reach a reset the monitor shows `RESET UNAVAILABLE` and leaves the machine, the view and edit mode unchanged.

To close the monitor:

- Press `C=+O` again.
- Press `RUN/STOP`, `ESC`, or the C64's top-left `←` key when no edit mode, Debug mode, or popup is active.

`RUN/STOP`, `ESC` and `←` are one Back action. Each press closes one active layer - help, a number expression, a popup, a command prompt, edit mode, Debug mode - and closes the monitor only once nothing is left. In Debug mode, `RUN/STOP` leaves Debug first; in Edit mode it leaves Edit first. Where `←` is data, in ASCII and Screen editing and in the ASCII and Screen rows of the Number popup, use `RUN/STOP` or `ESC` instead.

Two shortcuts act on the machine rather than the view, and both work from a memory view and from edit mode:

| Key | Action |
| --- | ------ |
| `C=+R` | Reset the C64. This is the same action as the task menu's `Reset C64`, so the on-device menu closes with the machine's screen where the interface is drawn there. |
| `C=+I` | Swap the interface between the freeze menu and the HDMI overlay, and close the menu. The setting takes effect the next time the menu opens. |

## Access Modes

The machine code monitor can be opened in three ways:

| Mode           | C64 while monitor is open | Video stream           | Use this when                                                                                |
| -------------- | ------------------------- | ---------------------- | -------------------------------------------------------------------------------------------- |
| **UI Freeze Mode**  | Frozen                    | Monitor is visible     | You want full-screen monitor use, automatic freezing, or monitor output in the video stream. |
| **UI Overlay Mode** | Running, but can be frozen with the `Z` shortcut or by stopping in Debug mode | Monitor is invisible | You want to use the monitor while the C64 keeps running.                                     |
| **Telnet**     | Ditto                   | Monitor is invisible         | You want to use the monitor from another machine or in an automated way.                                            |

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

The footer takes one of four forms, depending on what the hardware offers:

| Hardware                          | Footer                                                                        |
| --------------------------------- | ----------------------------------------------------------------------------- |
| CPU bank and VIC bank selection    | `CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000`, or `C7O5 ...` when the two banks differ |
| VIC bank selection only            | The same line once the live CPU port is known, and `CPU VIEW  VIC0 $0000` until then |
| CPU bank selection only            | `CPU7  VIC N/A`, or `C7O5  VIC N/A`                                            |
| Neither                            | `CPU VIEW  CPU BANK N/A  VIC N/A`                                              |

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

The highlighted address is the disassembly root: rows below it are decoded forward from that address, while rows above it are context only. Changing bytes before the highlighted address can refresh the context rows, but it does not change the instruction phase at the highlighted address. To inspect a different phase deliberately, move the root with the cursor keys or jump to the desired address.

The tag at the right end of each row names the memory source the byte was read from: `[RAM]`, `[BAS]`, `[KRN]`, `[CHR]`, or `[I/O]`. The tag is three characters inside the brackets, so the column stays aligned across a bank boundary.

On the U2+ cartridge the monitor reads through the CPU-visible aperture, so it can only name a source once it knows the 6510's port. Until then every row is tagged `[CPU]`.

`$D000-$DFFF` is shown as `DATA` rows of two bytes each while I/O or character ROM is banked in. An I/O read returns a live register, so a decoded instruction length there would change on every redraw, and character ROM holds bitmaps that never were code. With RAM banked in, the same addresses are disassembled normally, so the rule follows the banked source rather than the address range. `DATA` rows are grouped from the start of the region, so where a row begins does not depend on how the view arrived there.

A `DATA` row is edited like any other row. `E` enters edit mode with the cursor on the first byte, each displayed byte is its own edit position, two hex digits complete one byte, and `Left` / `Right` step from byte to byte and on into the row above or below. There is no mnemonic to pick on a `DATA` row, so a letter key does nothing there. `[I/O]` is writable; `[CHR]` is ROM and refuses the write. The same bytes can also be edited in Memory view with `M`. `DEL` clears a `DATA` row's bytes to `$00`, where on a decoded instruction it writes `NOP`.

The two-byte row is how the bytes are shown, not what a range is made of. A range anchored with `R` on a `DATA` byte covers the bytes between its ends: anchoring on `$D001`, moving right to `$D002` and pressing `R` copies those two bytes and nothing else. A range that starts on a decoded instruction still takes that instruction whole, so a range may cross between code and data without either end losing bytes.

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
- `G`: exit the monitor and execute from an address.
- `F1` or `Shift+Space`: page up.
- `F7` or `Space`: page down.
- Assembly view, non-edit mode: `Up` / `Down` move to the previous / next instruction root; `Left` / `Right` move the decode root by one byte (`-1` / `+1`).
- `Enter`: in Assembly view, follow the target of a jumpable instruction, or return to the most recent saved source location when the current instruction is not jumpable and the follow stack is non-empty.
- `O`: cycle the monitor-view CPU port banking, `CPU0`..`CPU7`. This changes the monitor view only; it does not write `$0001`.
- `Shift+O`: cycle the VIC bank override.
- `Z`: toggle freeze.
- `P`: toggle poll mode in the local monitor. Poll mode is unavailable over telnet, and entering Debug turns it off.

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
| Assembly     | Replaces the current instruction with `NOP` bytes; clears a `DATA` row to `$00` |

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

`Hunt` and `Compare` open a result picker with these controls:

| Key                       | Action                    |
| ------------------------- | ------------------------- |
| `Up`/`Down`               | Select a match            |
| `F1`/`F7`, `Home`/`End`   | Page or jump to the ends  |
| `Return`                  | Jump to the selected match |
| `RUN/STOP`                | Close the picker          |

Both pickers list at most 256 matches. A search with no result shows `No matches` or `No differences`.

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

Debug mode runs your program on the C64's own 6510 under your control. You can execute it one instruction at a time, stop it at addresses you choose (breakpoints), and see the CPU registers after every stop. Debug works in the Assembly view, and the rest of the monitor stays available, so you can look at and change memory between steps.

The 6510 has no built-in breakpoint support. To stop a program, the debugger writes a temporary `BRK` instruction at each address where execution should stop, lets the CPU run, and puts the original bytes back when it stops. You never see these `BRK` bytes in the monitor.

Because of this, a breakpoint needs memory the debugger can write to. That is RAM on every device, and on the Ultimate 64 also BASIC, KERNAL and character ROM (see [Hardware support](#hardware-support)). The debugger also borrows part of the cassette buffer while a session is active (see [Memory the debugger uses](#memory-the-debugger-uses)).

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

1. Open the monitor and press `D`. The monitor switches to the Assembly view and shows `Dbg` in the header.
2. Press `J`, type `C000` and press `RETURN`. The cursor is now on the first instruction.
3. Press `T`. The 6510 executes `LDA #$2A` and stops. The two rows above the footer now show the CPU registers, and the next instruction is marked `>LDX #$05<`.
4. Press `T` twice more to execute `LDX` and `LDY`. The program now stops on the `JSR`.
5. Press `T` to follow the `JSR` into the subroutine at `$C020`, or press `D` to run the whole subroutine and stop at `$C009` after it returns. After `T`, press `U` to run the rest of the subroutine and stop at the caller.
6. To skip ahead, move the cursor to a later instruction and press `P` to set a breakpoint there, then press `G` to run until the program reaches it. `K` runs to the cursor without setting a breakpoint.
7. Press `RUN/STOP` to leave Debug and stay in the monitor.

### Starting and leaving Debug

Press `D` to start Debug. Poll mode is switched off while Debug is active, because `P` sets breakpoints.

Starting Debug does not stop or change the C64. Until the program stops for the first time, the debugger does not know the CPU registers, so the register rows are blank. The first `T`, `D`, `G` or `K` therefore starts executing at the Assembly cursor address, much like `SYS`. It does not continue from where the C64 happened to be running when you opened the monitor. Once the program has stopped, at a breakpoint or after a step, every command continues from that point.

| Key                 | Effect                                                                                                  |
| ------------------- | ------------------------------------------------------------------------------------------------------- |
| `C=+D`              | Leave Debug and stay in the monitor.                                                                    |
| `RUN/STOP` or `ESC` | Leave Debug and stay in the monitor. If Edit mode is also on, the first press leaves Edit and the second leaves Debug. |
| `C=+O`              | Leave Debug and close the monitor.                                                                      |
| `C=+R`              | Reset the C64. Debug stays on, with blank registers, as when you first press `D`.                       |

When you leave Debug, the program continues from where it stopped. See [Leaving Debug](#leaving-debug).

Debug is available in UI Freeze, UI Overlay and Telnet mode, but only one Debug session can run at a time. If another session already has the debugger, pressing `D` shows `DEBUG IN USE`. A session that has not responded for 3 seconds is taken over.

### Debug keys

| Key        | Command            | What it does                                                                   |
| ---------- | ------------------ | ------------------------------------------------------------------------------ |
| `T`        | Step Into          | Execute one instruction. On a `JSR`, stop at the first instruction of the subroutine. |
| `D`        | Step Over          | Execute one instruction. On a `JSR`, run the whole subroutine and stop at the instruction after the `JSR`. |
| `U`        | Step Out           | Run until the current subroutine returns, and stop at the caller.              |
| `G`        | Continue           | Run until the program reaches an enabled breakpoint.                           |
| `K`        | Continue To Cursor | Run until the program reaches the Assembly cursor address.                     |
| `P`        | Breakpoint         | Set or clear a breakpoint at the Assembly cursor address.                      |
| `C=+P`     | Breakpoint list    | Open the list of all breakpoints.                                              |
| `RETURN`   | Follow / Return    | Show the target of a `JSR`, `JMP` or branch, or go back. Nothing is executed.  |
| `F3` / `?` | Help               | Show the Debug help screen.                                                    |

Outside Debug, `T`, `U`, `G` and `P` are Transfer, the undocumented-opcode toggle, Go and Poll. All other keys keep their normal meaning in Debug, so you can switch views, use bookmarks and edit memory between steps.

`RETURN` only moves the view. `T`, `D`, `U`, `G` and `K` move the real CPU.

### Reading the screen

While the program is stopped, the next instruction to execute is marked with brackets, for example `>LDA $07<`. The marker stays on that instruction while you scroll elsewhere.

- For a `JSR`, an absolute `JMP` and a branch that will be taken, the target address is shown in the accent color.
- An `RTS` row shows the address it will return to, read from the stack, for example `RTS $E5D2`. With an empty stack it shows `RTS $????`.
- Enabled breakpoints are shown in the accent color.

After each step, the view follows the program counter. If the program jumped elsewhere, the new instruction is shown three rows from the top.

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
| `NV-BDIZC`         | Status register, one digit per flag from bit 7 to bit 0                         |
| `IRQ`              | IRQ vector in RAM at `$0314/$0315`                                              |
| `NMI`              | NMI vector in RAM at `$0318/$0319`                                              |

In the example, `NV-BDIZC` is `00110100`. The `-` bit always reads as 1, `B` is 1 because the debugger stops the program with a `BRK`, and `I` is 1 because interrupts are disabled. All other flags are clear. The names of the set flags are also highlighted in the label row.

A value the debugger does not know yet is left blank. It is never shown as `00`.

### Stepping and running

The CPU always executes from the memory that is banked in through `$01`, not from the bank you selected with `O` for viewing. After each stop, the view switches to the bank the CPU is using.

`T` (Step Into) executes exactly one instruction.

`D` (Step Over) treats a `JSR` as one step: the subroutine runs at full speed and the program stops at the instruction after the `JSR`. This also works for calls into KERNAL or BASIC. For any other instruction, `D` does the same as `T`.

`U` (Step Out) runs until the current subroutine returns. It works after `T` and also when the program stopped inside a subroutine because of a breakpoint or `K`, and it works at any nesting depth. To find the caller, the debugger uses the `JSR` instructions it stepped into, or the return address on the stack. If neither shows that the CPU is inside a subroutine, for example because the code was reached with `JMP`, Step Out shows `NOT IN SUBROUTINE`. In that case, set a breakpoint at the return address and use `G` instead. The return address is shown on the `RTS` row.

`G` (Continue) runs the program until it reaches an enabled breakpoint. If the program is stopped on a breakpoint, `G` first executes that instruction, so the same breakpoint does not stop it again straight away. If no breakpoint is enabled, `G` lets the program run at full speed and ends Debug. On the C64 screen the monitor closes; a Telnet session stays open.

`K` (Continue To Cursor) runs until the program reaches the Assembly cursor address. An enabled breakpoint on the way stops it earlier.

A step gives the same result as running the program normally: the same registers, flags, stack pointer and memory. For example, a `JSR` lowers `SP` by 2 and the matching `RTS` raises it by 2, so a Step Over of a `JSR` leaves `SP` where it was.

If the program does not reach a breakpoint within 5 seconds, the debugger stops waiting and shows `DEBUG TIMEOUT`. The program keeps running. The limit is 900 ms while any breakpoint is in `$A000`-`$BFFF` or `$E000`-`$FFFF`. While the debugger is waiting, `RUN/STOP`, `ESC`, `C=+D` or `C=+O` stops waiting and shows `DEBUG CANCELLED`, and `C=+R` resets the C64.

### Where you can step

Plain RAM and I/O space can be stepped at any time. Code in ROM, or in the RAM underneath a ROM, can only be stepped once the debugger knows the CPU registers, which means once the program has stopped at least once:

| Program counter is in                  | Before the first stop                                    | After the first stop |
| -------------------------------------- | -------------------------------------------------------- | -------------------- |
| RAM or I/O space                       | All commands                                             | All commands         |
| RAM under BASIC, KERNAL or I/O         | All commands except Step Into                            | All commands         |
| BASIC, KERNAL or character ROM         | All commands except Step Into, and Step Over of anything but a `JSR` | All commands |

A command that is not available yet shows `Step Into: run to a breakpoint 1st` or `Step Over: run to a breakpoint 1st`. To get the first stop, set a breakpoint and press `G`, or Step Over a `JSR`.

When the debugger steps ROM code, or code in RAM under ROM, it completes the instruction itself while the CPU waits. This differs from a real run only when the instruction accesses I/O:

- An I/O access happens once. A read-modify-write instruction such as `INC $D019` writes the I/O register once instead of twice, and an indexed read that crosses a page does not make the extra dummy read.
- An instruction that writes `$01` still changes the banking, because it runs on the real 6510.

In UI Freeze mode, a Step Over of a `JSR` into ROM and a Step Out from ROM are completed one instruction at a time while the machine stays frozen. This stops early at an enabled breakpoint, at an instruction the debugger cannot step (`BRK` or an undocumented opcode), or after 8192 instructions. Press the same key, or `G`, to continue.

On an Ultimate II+ or II+L cartridge, the debugger can only stop and step code in RAM. See [Hardware support](#hardware-support).

### Breakpoints

There are 10 breakpoints, numbered `0` to `9`.

- `P` sets a breakpoint at the Assembly cursor address, or clears the one that is there. If all 10 are in use, `P` shows `NO FREE BRK SLOT`.
- An Assembly row with a breakpoint shows `[BRKn]` before its memory tag, for example `[BRK0][BAS]`. If you have given the breakpoint a label, the label is shown instead, for example `[LOOP][BAS]`.
- Only enabled breakpoints stop the program. A disabled breakpoint keeps its address but has no effect. All execution commands obey enabled breakpoints.
- Breakpoints stay set when you reset the C64 with `C=+R`, leave Debug or close the monitor. They are cleared when the device is switched off.

#### Breakpoints under ROM and I/O

At `$A000`-`$BFFF`, `$D000`-`$DFFF` and `$E000`-`$FFFF`, RAM and ROM or I/O share the same addresses. A breakpoint there belongs to the memory selected with `O` when you set it, as shown by the memory tag. `$E000` in KERNAL and `$E000` in RAM are two separate breakpoints, and both can be set at once. A breakpoint only stops the program when the program has that memory banked in. If it does not have it banked in when you set the breakpoint, the monitor shows `BRK <memory>, CPU <banking>; not mapped now`. The breakpoint is still set, and it stops the program once the program banks that memory in. `<banking>` is the banking the monitor last saw, at a reset or at the last stop.

#### Breakpoints in ROM

Breakpoints in ROM work on the Ultimate 64. The debugger patches its working copy of the ROM in the device's memory, never the flash, and puts the original bytes back when the breakpoint is removed or the session ends. On an Ultimate II+ or II+L cartridge the C64's ROM cannot be changed. If an enabled breakpoint is in ROM that is banked in, the debugger refuses to run and shows `BRK $xxxx IN ROM BLOCKS DEBUG`. Clear the breakpoint, or set it in RAM instead.

#### Breakpoint list

`C=+P` opens the breakpoint list. The help row at the bottom uses the short names shown in brackets.

| Key                  | Action                                                     |
| -------------------- | ---------------------------------------------------------- |
| `Up` / `Down`        | Select a breakpoint                                        |
| `RETURN`             | Show the selected breakpoint's address (`Jmp`)             |
| `0`-`9`              | Show that breakpoint's address (`Jmp`)                     |
| `S`                  | Set the selected breakpoint to the cursor address (`Set`)  |
| `L`                  | Give the breakpoint a label of up to 4 characters (`Lbl`)  |
| `E`                  | Enable or disable the breakpoint (`Enbl`)                  |
| `DEL`                | Clear the breakpoint (`Res`)                               |
| `RUN/STOP` or `C=+P` | Close the list                                             |

Jumping to a breakpoint only moves the view, and selects the memory bank the breakpoint was set in. The program stays stopped where it was, so `G` afterwards continues from there and not from the breakpoint address.

### Memory the debugger uses

While Debug is active, the debugger needs some low memory:

| Range           | Used for                                                                         |
| --------------- | -------------------------------------------------------------------------------- |
| `$0314`-`$0319` | IRQ, BRK and NMI vectors. The debugger points them at its own code.              |
| `$0340`-`$035C` | Work area for executing single instructions.                                     |
| `$035D`-`$03FB` | The debugger's own code and the saved CPU registers.                             |

These ranges are in the cassette buffer and the vector table. The debugger puts back the vectors and its code area when the session ends. Do not keep data you need in `$0340`-`$03FB` while you debug. A breakpoint, or a step that would stop, in `$0314`-`$0319` or `$035D`-`$03FB` is refused with `PATCH FAILED`. `$03FC`-`$03FF` is not used.

At most 16 addresses can be patched with `BRK` at once: your 10 breakpoints plus the temporary ones a step needs. If all 16 are in use, the step fails with `PATCH FAILED`.

### Debug messages

The two messages that start with `Step` appear on the bottom row. All others appear in a popup.

| Message                                      | Meaning and what to do                                                                                     |
| -------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| `Step Into: run to a breakpoint 1st`         | The program is in ROM or in RAM under ROM and has not stopped yet. Set a breakpoint and press `G`, or Step Over a `JSR`. |
| `Step Over: run to a breakpoint 1st`         | The same, for an instruction in ROM that is not a `JSR`.                                                   |
| `UNSUPPORTED OPCODE`                         | The next instruction is an undocumented opcode, which cannot be stepped. Set a breakpoint after it and press `G`. |
| `UNSAFE TARGET`                              | The next instruction is a `BRK`. Set a breakpoint after it and press `G`.                                  |
| `NOT IN SUBROUTINE`                          | Step Out could not find a caller. Set a breakpoint at the return address shown on the `RTS` row and press `G`. |
| `RETURN NOT REACHED`                         | Step Out did not stop at the caller. Set a breakpoint at the return address and press `G`.                 |
| `PATCH FAILED`                               | The address is in `$0314`-`$0319` or `$035D`-`$03FB`, or all 16 patch places are in use.                   |
| `NO FREE BRK SLOT`                           | All 10 breakpoints are in use. Clear one with `P` or in the `C=+P` list.                                   |
| `BRK <memory>, CPU <banking>; not mapped now` | The breakpoint is set in memory the program does not have banked in. It stops the program once that memory is banked in. |
| `DEBUG TIMEOUT`                              | The program did not reach a breakpoint in time. It keeps running.                                          |
| `DEBUG CANCELLED`                            | You stopped waiting for the program with a key.                                                            |
| `DEBUG NOT SUPPORTED`                        | This device cannot do it, for example stepping ROM code on an Ultimate II+ cartridge.                      |
| `BRK $xxxx IN ROM BLOCKS DEBUG`              | An enabled breakpoint is in ROM, which a cartridge cannot change. Clear it or set it in RAM. `A BRK IN ROM BLOCKS DEBUG` means the same when the address is not known. |
| `DEBUG IN USE`                               | Another session has the debugger. Close it there, or wait 3 seconds if it no longer responds.               |

### Leaving Debug

When you leave Debug, the program continues from where it stopped. The debugger first removes every `BRK` it wrote and restores the vectors, `$00`/`$01` and its code area.

Whether interrupts are enabled when the program continues depends on its banking:

- With KERNAL banked in, interrupts are enabled, so the cursor, keyboard and jiffy clock keep working.
- With KERNAL banked out (bit 1 of `$01` clear), interrupts stay disabled, because the KERNAL interrupt handler is not there to serve them.

If you leave Debug while a program that runs with KERNAL banked in has interrupts disabled on purpose, for example between `SEI` and `CLI` in a raster routine, it continues with interrupts enabled. To keep interrupts disabled, set a breakpoint after the `CLI` and press `G` instead of leaving Debug at that point.

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

| Capability                                        | U64 (Elite)                    | U2+ / U2+L cartridge                                         |
| ------------------------------------------------- | ------------------------------ | ------------------------------------------------------------ |
| Memory view, edit, fill, compare                  | Yes                            | Yes                                                          |
| `G` jump to address                               | Yes                            | Yes                                                          |
| Stepping and breakpoints in C64 RAM               | Yes                            | Yes                                                          |
| Stepping and breakpoints in BASIC / KERNAL / character ROM | Yes                   | No, the cartridge cannot change the C64's ROM                |
| Memory tag per row (`[KRN]`, `[RAM]`, ...)        | Yes                            | Yes, once the monitor has read `$01`; `[CPU]` until then      |
| CPU bank selection with `O`                       | Yes                            | No; the footer shows the CPU's banking instead               |
| VIC bank selection with `SH+O`                    | Yes                            | Yes                                                          |
| Freeze toggle (`Z`)                               | Yes                            | No                                                           |
| REST `/v1/machine` memory API                     | Yes                            | Yes                                                          |

On a cartridge, the debugger starts each run through the cartridge's NMI line. In a C64 Ultimate, set `C64 and Cartridge Settings` > `Cartridge Preference` to `External` and restart the C64 Ultimate; otherwise it does not pass the cartridge's NMI to the 6510, and steps do not start.
