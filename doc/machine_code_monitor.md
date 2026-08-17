# Machine Code Monitor

The Machine Code Monitor is a keyboard-driven tool for inspecting and editing C64 memory. It provides hexadecimal, assembly, binary, ASCII, and screen-code views, with inline editing, memory operations, file load/save, bookmarks, and execution from a selected address.

## Opening and Closing the Monitor

`C=` denotes the Commodore key. For example, `C=+O` means hold the Commodore key and press `O`.

Open the monitor in either of these ways:

- From the file browser, press `C=+O`.
- Press `F5`, open `Developer`, and select `Machine Code Monitor`. This works from any menu screen.

`C=+O` is only a file-browser shortcut. From another menu screen, use the `F5` route or press `RUN/STOP` to return to the file browser first.

With no popup, prompt, or edit mode open, any of these actions closes the monitor:

- `C=+O`
- `RUN/STOP`
- `ESC` on a USB keyboard
- the C64's top-left `←` key
- the device menu button
- `C=+I`, on targets that support interface swapping, after changing the interface mode

`C=+R` resets the machine and closes the monitor. See [Resetting the Machine](#resetting-the-machine).

When a popup, prompt, or edit mode is active, `RUN/STOP`, `ESC`, and `←` perform the Back action first. See [Back](#back).

`C=+O` closes the monitor directly from:

- a memory view
- edit mode
- Help
- the Number popup
- Hunt results
- Compare results
- opcode completion

From a command prompt, the Number calculator, or the Bookmarks popup, press Back once before using `C=+O`.

Closing and reopening the monitor preserves:

- the current view
- the cursor address
- the CPU bank
- the undocumented-opcode setting
- the Screen-view charset setting
- Memory and Binary row widths
- the most recent Load, Save, and Go parameters

Closing the monitor clears the clipboard. Powering the machine off clears the preserved session state listed above. Bookmarks remain stored in the device configuration.

### Help

Press `?` or `F3` to open Help.

These keys close Help without leaving the monitor:

- `?`
- `F3`
- `RUN/STOP`
- `←`
- `C=+B`
- `C=+0` through `C=+9`

The bookmark keys do not perform their bookmark action while Help is open.

Any other monitor command key closes Help and executes the command.

### Back

`RUN/STOP`, `ESC`, and the C64's top-left `←` key perform the Back action. Each press closes one active interaction layer.

| Active layer | Back action |
| --- | --- |
| Help | Close Help |
| Number expression | Return to the Number popup |
| Number, Bookmarks, opcode completion, Hunt results, or Compare results | Close the popup |
| Command prompt | Cancel the prompt |
| Edit mode | Leave edit mode |
| None | Close the monitor |

`←` is data in these locations:

- ASCII edit mode
- Screen edit mode
- the ASCII row of the Number popup
- the Screen row of the Number popup

In those locations, use `RUN/STOP` or `ESC` for Back.

## Screen Layout

The monitor screen has three fixed regions: Header, Body, and Footer.

### Header

The Header shows the current view, cursor address, and active modes. Mode indicators use fixed columns at the right of the line.

| Field | Meaning |
| --- | --- |
| `Undoc` | Undocumented opcodes are enabled in Assembly view |
| `Range` | Range mode is active |
| `Frz` | The machine is frozen |
| `Poll` | Poll mode is active |
| `EDIT` | Edit mode is active |

`Undoc` and `Range` share one column. If both are active, the Header shows `Undoc`.

### Body

The Body shows memory around the cursor address. The cursor position is highlighted in reverse video.

The Body can also display:

- Hunt results
- Compare results
- the Number popup
- opcode completion
- Bookmarks

### Footer

The Footer normally shows the CPU memory configuration and VIC bank. See [CPU and VIC Bank Display](#cpu-and-vic-bank-display).

After a bookmark or Follow/Return action, the Footer shows a short status message for about two seconds.

While Help is open, the Footer shows the Help paging keys.

Example:

```text
+--------------------------------------+
|MONITOR ASM $E011  Undoc Frz Poll EDIT|
|...                                   |
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000 |
+--------------------------------------+
```

## Views

The monitor provides five views.

| Key | View | ID | Purpose |
| --- | --- | --- | --- |
| `M` | Memory | HEX | Hexadecimal bytes |
| `A` | Assembly | ASM | Disassembly and inline assembly |
| `B` | Binary | BIN | Bit-level byte display |
| `I` | ASCII | ASC | Printable ASCII |
| `V` | Screen | SCR | C64 screen codes |

### Memory / Hex View

Memory view shows bytes in hexadecimal.

With 8 bytes per row, each row also shows a printable-character preview. With 16 bytes per row, the row contains two groups of eight hexadecimal bytes.

Example:

```text
+--------------------------------------+
|MONITOR HEX $0168                     |
|00E0 85 85 85 85 85 85 86 86 ........|
|00E8 86 86 86 86 86 87 87 87 ........|
|00F0 87 87 87 F0 DB 00 00 00 ........|
|00F8 00 00 00 00 00 00 00 20 ....... |
|0100 33 38 39 31 31 00 30 30 38911.00|
|0108 30 30 00 00 10 10 35 02 00....5.|
|0110 00 00 10 10 35 02 00 00 ....5...|
|0118 1C 10 35 02 00 00 22 10 ..5...".|
|0120 35 02 00 00 28 10 35 02 5...(.5.|
|0128 00 10 35 02 00 00 32 10 ..5...2.|
|0130 35 02 00 00 38 10 35 02 5...8.5.|
|0138 00 00 3E 10 35 02 00 00 ..>.5...|
|0140 44 10 35 02 00 00 44 10 D.5...D.|
|0148 35 02 00 00 50 10 35 02 5...P.5.|
|0150 00 00 56 10 35 02 00 00 ..V.5...|
|0158 5C 10 35 02 00 00 62 10 \.5...b.|
|0160 35 02 00 00 68 10 35 02 5...h.5.|
|0168 00 00 6E 10 35 02 00 00 ..n.5...|
|CPU1 $A:RAM $D:CHR $E:RAM VIC0 $0000 |
+--------------------------------------+
```

### Assembly View

Assembly view shows decoded 6510 instructions, instruction bytes, and the source used for each row.

The source tag appears at the right edge:

- `[RAM]` - RAM
- `[BAS]` - BASIC ROM
- `[CHR]` - Character ROM
- `[I/O]` - I/O registers
- `[KRN]` - KERNAL ROM
- `[CPU]` - memory currently visible to the CPU on an Ultimate II+

Each tag occupies three characters inside the brackets, keeping the source column aligned across bank boundaries.

An undefined opcode is shown as `???` and consumes one byte. Press `U` to enable undocumented opcodes. An instruction whose operand would cross `$FFFF` is also shown as `???`.

#### I/O Registers and Character ROM in Assembly View

Assembly view shows `$D000-$DFFF` as one byte per row when either I/O or Character ROM is banked in:

```text
.BYTE $xx   [I/O]
.BYTE $xx   [CHR]
```

The two sources qualify for different reasons. I/O reads live registers, so the same address can answer differently between accesses; decoding it would change the instruction length, and with it the address of every row below, on each redraw. Character ROM is stable, but it holds character bitmaps that never were code, so any instruction decoded from it is meaningless.

Each row retains its address while scrolling and shows the value read at that address.

With RAM banked into `$D000-$DFFF`, the monitor disassembles the bytes in that region normally. The rule follows the banked source, not the address range.

On an Ultimate II+, `$D000-$DFFF` is always disassembled.

Example:

```text
+--------------------------------------+
|MONITOR ASM $E011                     |
|DFF9 FF           .BYTE $FF      [I/O]|
|DFFA 00           .BYTE $00      [I/O]|
|DFFB 12           .BYTE $12      [I/O]|
|DFFC FF           .BYTE $FF      [I/O]|
|DFFD 00           .BYTE $00      [I/O]|
|DFFE 3C           .BYTE $3C      [I/O]|
|DFFF 00           .BYTE $00      [I/O]|
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
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000 |
+--------------------------------------+
```

### Binary View

Binary view shows each byte as eight bits:

- `.` - cleared bit
- `*` - set bit

This view is useful for registers, character glyphs, sprite data, and other bit-oriented memory.

Press `W` to select the number of bytes shown in each row. Sprite data commonly uses the three-byte modes. See [`W`: Width Mode](#w-width-mode).

The Header shows the byte address and selected bit, for example `$DC00/7`.

Bit 7 is shown at the left. Bit 0 is shown at the right. `Left` and `Right` move by one bit and continue into the adjacent byte.

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
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000 |
+--------------------------------------+
```

### ASCII View

ASCII view shows bytes as printable ASCII text. Rows contain 32 bytes.

Display rules:

- `$20-$7E` show their ASCII characters.
- All other byte values show `.`.
- Lowercase ASCII remains lowercase.

In edit mode, typing a printable character from `$20-$7E` writes its byte value and moves the cursor right.

Example:

```text
+--------------------------------------+
|MONITOR ASC $A000                     |
|A000 .{.CBMBASIC0.A...................|
|A020 P........:..J.,.g.U.d...#........|
|A040 U...j..}.....:Z.A.g.U.X...}...g.|
|A060 ....d.k......|.e..............g  |
|A080 yi.yR.{*.{...z.p..F..}...Z..d.EN|
|A0A0 .FO.NEX.DATA.INPUT.DIM.REA.LE    |
|A0C0 .GOT.RU.I.RESTOR.GOSU.RETUR.RE.S|
|A0E0 TO.O.WAI.LOA.SAVU.VERIF.DE.POK.PR|
|A100 INT.PRIN.CON.LIS.CLR.CM.SY.OPE.CL|
|A120 OS.GE.NE.TAB.T.F.SPC.THE.NO.STE. |
|A140 .....AN.O....SG.IN.AB.US.FR.PO.S|
|A160 Q.RN.LO.EX.CO.SI.TA.AT.PEE.LE.ST|
|A180 R.VA.AS.CHR.LEFT.RIGHT.MID.G..TO|
|A1A0 D.MANY FILE.FILE OPEN.FILE NOT OP|
|A1C0 E.FILE NOT FOUND.DEVICE NOT PRESE|
|A1E0 N.NOT INPUT FIL.NOT OUTPUT FIL.M|
|A200 ISSING FILE NAM.ILLEGAL DEVICE N|
|A220 UMBE.NEXT WITHOUT FO.SYNTA.RETUR|
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000 |
+--------------------------------------+
```

### Screen View

Screen view shows C64 screen codes. It is useful for screen RAM, which starts at `$0400` in the default C64 configuration. Rows contain 32 bytes.

The Header shows the active Screen charset mode:

- `MONITOR SCR U/G $xxxx` - Uppercase/Graphics
- `MONITOR SCR L/U $xxxx` - Lowercase/Uppercase

Press `U` to switch the Screen charset mode. This setting belongs to the monitor and does not change the live C64 character set.

Bit 7 is the reverse-video flag and is ignored for display. For example, `$81` and `$01` display the same character.

Both charset modes use these mappings:

| Code | Display |
| --- | --- |
| `$00` | `@` |
| `$1B` | `[` |
| `$1C` | `#` |
| `$1D` | `]` |
| `$1E` | `^` |
| `$1F` | `<` |
| `$20-$3F` | ASCII character with the same value |

Letter mappings depend on the active charset mode:

| Mode | Display | Typing |
| --- | --- | --- |
| `U/G` | `$01-$1A` as `A-Z` | `A-Z` and `a-z` write `$01-$1A` |
| `L/U` | `$01-$1A` as `a-z`; `$41-$5A` as `A-Z` | `a-z` writes `$01-$1A`; `A-Z` writes `$41-$5A` |

Typing also accepts:

- `@` -> `$00`
- `Space` -> `$20`
- top-left `←` -> `$1F`
- digits and punctuation in `$21-$3F` -> their byte value

Other keys are rejected and do not change memory.

Codes in `$40-$5F` without a letter mapping use readable fallback glyphs. Other unmapped codes show `.`.

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
|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000 |
+--------------------------------------+
```

The monitor uses the menu font. Graphics codes therefore use readable stand-in glyphs for the C64 shapes.

## View Modifiers

### `U`: Undocumented Opcodes / Screen Charset

`U` depends on the current view.

| View | `U` action |
| --- | --- |
| Assembly | Toggle undocumented opcodes |
| Screen | Toggle `U/G` and `L/U` charset modes |
| Other views | Show `UNDOC IN ASM, CASE IN SCR` |

In Assembly view, enabling undocumented opcodes affects both disassembly and opcode completion. The monitor decodes supported undocumented opcodes using their mnemonic and instruction length. The Header shows `Undoc` while the setting is enabled.

With undocumented opcodes disabled, those byte values show as `???` and consume one byte.

### `W`: Width Mode

`W` depends on the current view.

| View | `W` action |
| --- | --- |
| Memory | Toggle 8 or 16 bytes per row |
| Binary | Cycle `1` -> `2` -> `3` -> `3S` -> `4` -> `1` |
| ASCII | Show `WIDTH ONLY IN MEMORY/BINARY VIEW` |
| Screen | Show `WIDTH ONLY IN MEMORY/BINARY VIEW` |
| Assembly | Show `WIDTH ONLY IN MEMORY/BINARY VIEW` |

Binary width modes:

- `1`, `2`, `3` - show one, two, or three separate bit fields with a hexadecimal preview.
- `3S` - show three bytes as one continuous 24-bit sprite row with a hexadecimal preview.
- `4` - show four bytes as one continuous 32-bit row without a hexadecimal preview.

Changing Binary width resets the bit cursor to bit 7.

## Navigation and Context

| Key | Action |
| --- | --- |
| `Left`, `Right` | Move one byte; move one bit in Binary view |
| `Up`, `Down` | Move one row; move one instruction in Assembly view |
| `F1`, `F2`, `Shift+Space` | Page up |
| `F7`, `Space` | Page down |
| `J` | Jump to an address |
| `G` | Execute from an address and leave the monitor |
| `Enter` | Follow or return in Assembly view |
| `O` | Cycle CPU bank `CPU0` through `CPU7` |
| `Shift+O` | Cycle VIC bank `VIC0` through `VIC3` |
| `Z` | Toggle freeze |
| `P` | Toggle periodic redraw |

In edit mode, `Space` is view-specific input and does not page.

Command-prompt addresses are hexadecimal and may include a `$` prefix.

### CPU Bank

Press `O` to cycle `CPU0` through `CPU7`.

On an Ultimate II+, the monitor shows `CPU BANK UNAVAILABLE`. The monitor reads the memory currently visible to the CPU.

### VIC Bank

Press `Shift+O` to cycle `VIC0` through `VIC3`.

A target without VIC-bank support shows `VIC BANK UNAVAILABLE`.

### Freeze

Press `Z` to toggle the freezer in Overlay mode.

Outside Overlay mode, the monitor shows `FREEZE ONLY IN OVERLAY MODE`.

A target without a freezer shows `FREEZE UNAVAILABLE`.

### Poll Mode

Press `P` to redraw the current view continuously so live memory changes remain visible.

The refresh rate follows the machine video format: 50 Hz or 60 Hz.

Poll mode is unavailable over Telnet. The monitor shows `POLL MODE UNAVAILABLE OVER TELNET`.

### Go

Press `G` and enter an address. The monitor moves the cursor to that address, closes, releases the C64, and enters the address through an NMI trampoline in the cassette buffer at `$033C`. The previous `NMINV` vector is restored before execution jumps to the selected address.

A target without this capability shows `GO UNAVAILABLE`.

### Assembly Baseline

Assembly view needs a fixed address from which to determine instruction boundaries. The monitor calls this address the baseline.

These actions set a new baseline:

- `J`
- `G`
- restoring a bookmark
- selecting a Hunt result
- selecting a Compare result
- returning from a Follow action
- following a target that is outside the visible screen
- assembling an instruction inline

These actions keep the current baseline:

- cursor movement
- page movement
- following a target that is already visible

The monitor never lets an instruction that starts before the baseline consume bytes at or beyond the baseline. If one of the one or two bytes immediately before the baseline would decode into such an instruction, the monitor shows that byte as `.BYTE`.

Bytes farther above the baseline are disassembled normally.

When moving upward, the monitor starts disassembly far enough before the current view to recover the instruction boundaries for the rows being entered. Moving down follows those recovered boundaries. Returning to the baseline therefore restores the instruction layout associated with that baseline.

### Follow / Return

In Assembly view, `Enter` follows these control-flow instructions:

- `JMP` absolute
- `JMP` indirect
- `JSR`
- `BPL`
- `BMI`
- `BVC`
- `BVS`
- `BCC`
- `BCS`
- `BNE`
- `BEQ`

For `JMP` indirect, the monitor reads the vector and follows the resolved address.

A successful Follow saves the source location. If the current instruction cannot be followed and the Follow stack is not empty, `Enter` returns to the most recently saved location.

The Follow stack stores up to 10 locations. When an eleventh location is added, the oldest entry is discarded.

After a Follow or Return, the Footer shows the zero-based stack depth for about two seconds. Examples:

```text
F1 JMP $E000
F0 RET $A000
```

A Return restores:

- address
- view
- row width or width mode
- CPU bank
- VIC bank
- scroll position

### Command Prompt Input

A command prompt accepts characters that can form a valid command for that prompt. Other keys are ignored.

Partially entered input remains accepted while it can still become valid. Examples include:

```text
0800-
PRG,
"unfinished text
```

Address, syntax, value, and range checks occur when the command is submitted.

All command prompts except Hunt open with their field prefilled and fully selected. The first printable character, `DEL`, or `CLEAR` replaces the selection. Cursor keys move within the field without clearing it. `Return` submits the displayed value.

| Prompt | Initial value |
| --- | --- |
| Jump | `AAAA` |
| Fill | `AAAA-BBBB,DD` |
| Transfer | `AAAA-BBBB,CCCC` |
| Compare | `AAAA-BBBB,CCCC` |
| Go | most recent Go address, or the cursor address |
| Load | most recent parameters; initially `PRG,0000,AUTO` |
| Save | most recent range; initially `0800-9FFF` |
| Hunt | `0000-FFFF, ` with the cursor at the end |

The placeholder templates use hexadecimal characters and therefore parse as real values. Type over them with the required addresses and values.

Prompt parse errors use these status codes:

- `?ADDR`
- `?SYNTAX`
- `?VALUE`
- `?RANGE`

`Save as` and `Label BM<n>` are free-text prompts. They accept printable characters, convert typed letters to uppercase, and open with the previous value ready for editing.

### CPU and VIC Bank Display

The Footer summarizes the selected CPU-visible memory configuration and VIC bank.

Example:

```text
CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000
```

`CPU0` through `CPU7` represent the three 6510 memory-configuration bits at `$0001`:

- `LORAM`
- `HIRAM`
- `CHAREN`

With the standard no-cartridge memory map, the Footer uses these fields:

| Field | Address range | Values |
| --- | --- | --- |
| `$A` | `$A000-$BFFF` | `BAS`, `RAM` |
| `$D` | `$D000-$DFFF` | `I/O`, `CHR`, `RAM` |
| `$E` | `$E000-$FFFF` | `KRN`, `RAM` |

| Value | Meaning |
| --- | --- |
| `BAS` | BASIC ROM |
| `I/O` | I/O registers and Color RAM |
| `CHR` | Character ROM |
| `KRN` | KERNAL ROM |
| `RAM` | RAM |

Cartridges can also change the CPU-visible memory map through the expansion-port `GAME` and `EXROM` lines.

`VIC0` through `VIC3` identify the selected VIC bank. CIA 2 port A at `$DD00` controls the bank, with these base addresses:

| VIC bank | Base address |
| --- | --- |
| `VIC0` | `$0000` |
| `VIC1` | `$4000` |
| `VIC2` | `$8000` |
| `VIC3` | `$C000` |

The monitor tracks the live VIC bank until `Shift+O` selects an override. Tracking resumes when the live bank matches the selected override.

The Footer format follows target capabilities:

| Target capability | Footer |
| --- | --- |
| CPU banking and VIC bank | `CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000` |
| VIC bank only | `CPU VIEW  VIC0 $0000` |
| CPU banking only | `CPU7  VIC N/A` |
| Neither | `CPU VIEW  CPU BANK N/A  VIC N/A` |

`CPU VIEW` means the monitor reads the memory currently visible to the CPU. The Ultimate II+ uses this mode and also supports VIC-bank selection.

## Editing

All five views support editing.

Press `E` to enter edit mode.

Leave edit mode with:

- `C=+E`
- `RUN/STOP`
- `ESC` on a USB keyboard
- the top-left `←` key

In ASCII and Screen edit modes, `←` is data. Use `C=+E`, `RUN/STOP`, or `ESC` to leave edit mode there.

Edits are written to memory as they are completed.

| View | Edit behavior |
| --- | --- |
| Memory | Enter two hexadecimal nibbles to write one byte, then move right |
| ASCII | Enter a printable character, then move right |
| Screen | Enter a character using the active Screen charset, then move right |
| Binary | `0`, `.`, or `Space` clears the selected bit; `1` or `*` sets it; then move one bit |
| Assembly | Edit the instruction inline |

### Memory Editing

The first hexadecimal digit is shown as a pending high nibble. The byte is written when the second digit is entered.

Moving the cursor after entering one digit writes that digit as a full byte value. For example, entering `A` and moving right writes `$0A`.

### Commands While Editing

In Memory and Binary edit modes, a key that is not data can still invoke a monitor command. For example, `M`, `I`, `A`, `V`, and `B` switch views while edit mode remains active.

Keys that are neither valid data nor monitor commands are ignored.

In ASCII and Screen edit modes, printable keys are treated as data.

### `DEL`

`DEL` edits the current location in every view, including outside edit mode.

| View | `DEL` action |
| --- | --- |
| Memory | Write `$00` and move right |
| ASCII / Screen | Write a space (`$20`) and move left |
| Binary | Clear the selected bit and move one bit |
| Assembly | Replace the current instruction with `NOP` bytes and move to the next line |

While editing, `DEL` first removes pending input:

- Memory view - discard a pending nibble and restore the byte.
- Assembly view - remove the most recently typed character on the current instruction and restore the byte changed by that character. Up to 16 steps are retained per instruction.

When no pending input remains, `DEL` performs the action from the table above.

### Assembly Editing

Assembly editing treats an instruction as a sequence of parts.

- Part 0 is the mnemonic.
- Parts 1 and onward are operand bytes in display order.
- For a three-byte instruction, part 1 is the high byte and part 2 is the low byte.

A two-byte branch is edited as three parts: mnemonic, target high byte, and target low byte. The monitor converts the displayed absolute target to and from the signed branch displacement stored in memory.

A branch target must fit the 6502 relative range of `-128..127`. If it does not, the edit is rejected and memory is unchanged.

Assembly edit keys:

| Key | Action |
| --- | --- |
| `A`-`Z` on part 0 | Open opcode completion, seeded with that letter, when a supported mnemonic begins with it |
| Hex digit on part 1 or later | Enter one operand nibble and advance to the next nibble, part, or instruction |
| `Left`, `Right` | Move between parts; continue into the adjacent instruction at either end |
| `Up`, `Down` | Move one instruction and select part 0 |
| `Return` | Move to the next instruction |

#### Opcode Completion

| Key | Action |
| --- | --- |
| Letters | Extend the mnemonic to a maximum of three characters while it remains valid |
| `0`-`9`, `A`-`F`, `X`, `Y`, `#`, `$`, `(`, `)`, `,` | Build an operand after the mnemonic is complete, for example `LDA #FF` or `LDA (10),Y` |
| `Up`, `Down` | Select an addressing-mode candidate |
| `Return` | Assemble the typed operand, or commit the selected candidate when no operand is present |
| `Space` | Assemble the typed operand |
| `DEL` | Remove operand characters, then mnemonic characters, then close the list |
| `RUN/STOP`, `ESC`, `←` | Close the list without assembling |

If a typed operand cannot be assembled, the completion list stays open.

## Selection and Clipboard

| Key | Action |
| --- | --- |
| `C=+C` | Copy the current byte, or the active range |
| `C=+V` | Paste at the cursor |
| `R` | Toggle range mode |

Pasting advances the cursor by the number of bytes written.

### Range Mode

Press `R` to anchor the current address. The Header shows `Range`.

The selection includes the anchor, the cursor, and every address between them. The cursor may move in either direction from the anchor.

In Assembly view, the selection expands to complete instructions.

While Range mode is active:

- `C=+C` copies the selected bytes and leaves Range mode.
- `R` copies the selected bytes and leaves Range mode.

`R` is available outside edit mode in all views.

In edit mode:

- Memory - `R` controls Range mode.
- Binary - `R` controls Range mode.
- Assembly - `R` controls Range mode while the cursor is on an operand part.
- ASCII - `R` is entered as data.
- Screen - `R` is entered as data.

If a copy cannot allocate memory, the monitor shows `?MEM` and keeps Range mode active.

Pasting with an empty clipboard shows `?CLIP`.

## Number Tool

Press `N` to open the Number tool. It converts and edits the byte or word at the current target.

The popup shows the value in five forms:

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

The Header shows the target address and whether the target is a byte or word.

### Target Selection

In Memory, Binary, ASCII, and Screen views, the target is the byte at the cursor.

In Assembly view:

- a one-byte operand targets one byte
- a two-byte operand targets one word
- an instruction without an operand targets the byte immediately after the instruction

Assembly-derived target width remains fixed by the instruction.

### Number Popup Keys

| Key | Action |
| --- | --- |
| `Up`, `Down` | Select a row; pending input on the previous row is discarded |
| Digits / characters | Enter a value using the selected row's format |
| `DEL` | Remove the last entered character |
| `Return` | Write the previewed value to memory |
| `C=+C` | Copy the previewed value and close the popup |
| `+`, `-`, `*`, `/` | Open the calculator |
| `RUN/STOP`, `ESC`, `←` | Close the popup |
| `C=+O` | Close the popup and leave the monitor |

Only input valid for the selected row is accepted.

Input limits:

| Row | Maximum input |
| --- | --- |
| Hex | 4 digits |
| Decimal | 5 digits |
| Binary | 16 digits |
| ASCII | 2 characters |
| Screen | 2 characters |

For cursor-byte targets, entering a value that requires two bytes widens the target to a word.

The ASCII and Screen rows use the mappings from their corresponding views. The top-left `←` key enters its byte value on those rows.

### Calculator

From the Number popup, press `+`, `-`, `*`, or `/` to open the calculator.

The expression starts with:

1. the current value, written in the selected row's base
2. the operator that opened the calculator

ASCII and Screen rows use hexadecimal for the initial value.

The popup's bottom line changes to `Expr=` and displays the expression.

Press `Return` or `=` to evaluate it.

Press `RUN/STOP`, `ESC`, or `←` to cancel the expression and return to the Number popup.

A successful calculation returns to the conversion display and updates all rows with the result.

Errors appear on the bottom line:

| Status | Meaning |
| --- | --- |
| `SYNTAX` | The expression cannot be parsed |
| `DIV/0` | Division by zero |
| `RANGE` | Negative subtraction, intermediate overflow, or a result outside the target width |

Expressions support `+`, `-`, `*`, and `/`.

Evaluation order:

1. multiplication and division
2. addition and subtraction

Division uses unsigned integer arithmetic. Intermediate values use 32 bits. The final result must fit the current target:

- byte - `$00-$FF`
- word - `$0000-$FFFF`

Number syntax:

- hexadecimal - `$` followed by up to four hexadecimal digits
- binary - `%` followed by binary digits
- decimal - digits without a prefix

Examples:

```text
42
$1000+4
$2000/16
%1010*3
1+2/3
2+3*4
```

Formal grammar:

```ebnf
expr     = term, { ("+" | "-"), term } ;
term     = value, { ("*" | "/"), value } ;
value    = hex | decimal | binary ;

hex      = "$", hex_digits ;
decimal  = decimal_digits ;
binary   = "%", binary_digits ;
```

## Memory Operations

The monitor provides four bulk memory commands.

| Key | Command | Syntax | Action |
| --- | --- | --- | --- |
| `F` | Fill | `start-end,value` | Fill a range with one byte |
| `T` | Transfer | `start-end,dest` | Copy a range to another address |
| `T` | Transfer with relocation | `start-end,dest,code-start-code-end` | Copy a range and relocate eligible absolute operands in the code subrange |
| `C` | Compare | `start-end,dest` | Compare a range with another memory region |
| `H` | Hunt | `start-end,bytes` or `start-end,"text"` | Search for bytes or ASCII text |

Prompt titles show the expected form:

```text
Fill AAAA-BBBB,DD
Transfer AAAA-BBBB,CCCC[,DDDD-EEEE]
Compare AAAA-BBBB,CCCC
Hunt AAAA-BBBB,BB/"text"
```

### Address Ranges

All ranges include both endpoints.

Examples:

- `C000-CFFF` - 4096 bytes
- `C000-C000` - one byte
- `0000-FFFF` - all 65536 bytes

An end address below the start address reports `?RANGE`.

Addresses and values are hexadecimal and may include a `$` prefix. Spaces around fields are ignored.

### Hunt Input

Hunt accepts either bytes or quoted ASCII text.

Hexadecimal bytes may be separated by spaces or written together:

```text
A9 00 8D 20 D0
A9008D20D0
```

Quoted text preserves its typed case. Text outside quotes is normalized to uppercase.

A Hunt pattern may contain up to 80 bytes.

### Transfer

Transfer is overlap-safe in either direction.

Basic copy:

```text
T C000-C0FF,C100
```

This copies `$C000-$C0FF` to `$C100-$C1FF`.

### Relocating a Routine

Transfer can also adjust absolute operands when code moves to a new address. The code range is specified using source addresses.

Syntax:

```text
T source-start-source-end,dest,code-start-code-end
```

Example:

```text
T C000-C0FF,C100,C000-C07F
```

This command:

1. copies `$C000-$C0FF` to `$C100-$C1FF`
2. treats `$C000-$C07F` as code
3. scans that code after the copy
4. rewrites eligible absolute operands that point inside the copied source range

The relocation applies to absolute addresses used by instructions such as:

- `JMP $nnnn`
- `JSR $nnnn`
- `LDA $nnnn,X`
- `JMP ($nnnn)`

An operand is relocated only when its address points inside the copied source range.

These operands keep their original value:

- zero-page operands
- relative branches
- addresses outside the copied source range
- operands belonging to an instruction that extends beyond the code range

Relative branches already store a displacement, so moving the branch and its target together preserves the relationship.

The code scan proceeds linearly from the start of the code range. A byte that does not decode advances the scan by one byte. This allows the scan to pass undocumented opcodes and padding.

Data embedded in the declared code range can make the scanner lose instruction alignment. Define the code range to contain code only where practical.

After relocation, Transfer reports the number of rewritten operands. Check that count against the routine's expected relocations. An unexpected count can indicate that the declared code range contains data or begins at the wrong instruction boundary.

### Command Results

Successful Fill and basic Transfer commands produce no confirmation message.

Compare reports `No differences` when the regions match.

Hunt reports `No matches` when the pattern is absent.

Compare and Hunt open a result picker when results exist.

| Key | Action |
| --- | --- |
| `Up`, `Down` | Select a result |
| `Page Up`, `Page Down` | Move one screen |
| `Home`, `End` | Select the first or last result |
| `Return` | Jump to the selected result |
| `RUN/STOP`, `ESC`, `←` | Close the picker |
| `C=+O` | Close the picker and leave the monitor |

The picker stores up to 256 results. Additional results are not listed.

Its Header shows `Hunt results` or `Compare results`, followed by the selected position and total result count.

## File I/O

Press:

- `L` to load a file into memory
- `S` to save memory to a file

Files may be stored directly in the Ultimate filesystem or inside a disk image such as a `.D64`.

The file picker reopens in the directory used during the previous file operation.

Picker navigation:

| Key | Action |
| --- | --- |
| `Right` | Enter a directory or disk image |
| `Left` | Go up one level; close the picker when already at the root |
| `RUN/STOP`, `ESC` | Cancel |

### Load

Loading has two steps:

1. Select a file in `MONITOR LOAD: Select File`.
2. Enter parameters at `Load [PRG|AAAA],[Offs],[Len|AUTO]`.

To select an existing file, press `ENTER` on it and choose `Select` from the context menu.

Load syntax:

```text
[PRG|AAAA],[Offset],[Len|AUTO]
```

The initial parameters are:

```text
PRG,0000,AUTO
```

These parameters load the complete PRG payload at the load address stored in the file's first two bytes.

#### Load Fields

| Field | Meaning |
| --- | --- |
| `PRG` | Use the two-byte load address stored in the PRG file |
| `AAAA` | Load at the specified hexadecimal address |
| `Offset` | Skip bytes before loading |
| `Len` | Load the specified number of bytes |
| `AUTO` | Load all available bytes after the offset |

Offset interpretation depends on the first field:

- `PRG` - offset starts after the two-byte PRG header.
- explicit address - offset starts at the beginning of the file.

Every field is optional. Trailing fields may also be omitted.

For example:

```text
,,0010
```

This loads `$10` bytes from offset 0 using the PRG's embedded load address.

Examples:

| Input | Action |
| --- | --- |
| `PRG` | Load a PRG at its embedded address |
| `0801` | Load the file at `$0801` |
| `PRG,1000` | Skip `$1000` bytes after the PRG header, then load the remainder |
| `0801,0002,0010` | Load `$0010` bytes from file offset `$0002` at `$0801` |

An explicit length must fit within the data available after the selected offset.

Load errors include:

- `OPEN FAILED`
- `NOT A PRG`
- `READ FAILED`
- `SEEK FAILED`
- `LOAD TOO LARGE (>64K)`
- `LOAD WRAPS PAST $FFFF`
- `?ADDR`
- `?SYNTAX`
- `?VALUE`
- `?RANGE`

Loading at `$0801` also updates the BASIC runtime pointers to the end of the loaded data:

- `VARTAB` at `$2D`
- `ARYTAB` at `$2F`
- `STREND` at `$31`
- last-load address at `$AE`

This prepares a newly loaded BASIC program for `RUN` and `LIST`.

After a successful load, a confirmation popup shows:

- filename
- written address range
- byte count

### Save

Saving has two steps:

1. Enter the range at `Save AAAA-BBBB`.
2. Select or create the destination in `MONITOR SAVE: Pick File/Dir`.

The Save prompt remembers the previous range. Its initial value is:

```text
0800-9FFF
```

The range includes both endpoints.

Save creates a PRG file with a two-byte little-endian load address header. Loading that PRG without an explicit address writes it back to the saved start address.

#### Saving to an Existing File

1. Press `ENTER` on the file.
2. Choose `Select` from the context menu.
3. Confirm `File already exists. Overwrite?`.

Declining the overwrite confirmation reports `CREATE FAILED`.

#### Creating a File

`<< Create New File >>` appears at the top of the listing when the current directory is writable.

Press `ENTER` or `Right` on it. The `Save as` prompt opens with the most recently used filename.

Save errors:

- `CREATE FAILED`
- `WRITE FAILED`

After a successful save, a confirmation popup shows:

- filename
- saved address range
- byte count

## Bookmarks

The monitor provides 10 bookmark slots. They are stored in the device configuration.

| Key | Action |
| --- | --- |
| `C=+B` | Open Bookmarks |
| `C=+0` through `C=+9` | Restore a bookmark directly |

Bookmark keys work in view mode and edit mode.

While Help is open, bookmark keys only close Help.

The following popups keep control of the keyboard until they are closed with Back:

- result picker
- opcode completion
- Number popup
- Bookmarks popup

### Stored Bookmark State

Each bookmark stores:

- label, up to 6 characters
- address
- view ID
- row width or width mode, when applicable
- CPU bank
- VIC bank

Restoring a bookmark restores all stored fields.

On an Ultimate II+, CPU memory follows the live machine state. Restore a bookmark whose stored CPU bank matches the current CPU bank. Other CPU-bank values report `RESTORE FAILED`.

### Bookmark Popup

| Key | Action |
| --- | --- |
| `Up`, `Down` | Select a slot |
| `Return` | Restore the selected slot |
| `S` | Store the current location in the selected slot and keep its label |
| `L` | Edit the label using `Label BM<n>` |
| `DEL` | Reset the slot to its default using the current CPU and VIC bank |
| `0`-`9` | Restore that slot directly |
| `C=+B`, `RUN/STOP`, `ESC`, `←`, `?`, `F3` | Close the popup |

### Bookmark Labels

When a label is saved:

- letters are converted to uppercase
- `A-Z`, `0-9`, `_`, and `-` are preserved
- spaces are removed
- other characters become `_`
- an empty label becomes `USER`

### Default Bookmarks

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

`Interface Type` controls where the menu and monitor are drawn.

| Mode | Purpose |
| --- | --- |
| Freeze | Draw the monitor in the C64 video output, including capture workflows |
| Overlay on HDMI | Enable Poll mode and the `Z` freeze toggle |

Press `C=+I` from the file browser, Settings, or the monitor to change the interface mode.

When used inside the monitor, `C=+I` changes the setting and closes the monitor. The current monitor screen remains in the mode in which it was opened. The selected mode is used when the monitor opens again.

From the file browser, press `C=+O` to reopen it.

## Resetting the Machine

Press `C=+R` to reset the C64.

Key mapping:

```text
C64 keyboard: C=+R
USB keyboard: Ctrl+R
Telnet:       Ctrl+R
```

`C=+R` performs the same action as the task menu's `Reset C64` and the `machine:reset` REST route: the on-device user interface lets go of the machine, the machine is unfrozen, and the reset is pulsed. Letting go is what makes the reset reach the C64; a machine still held by the menu keeps its CPU parked and never runs the KERNAL.

The monitor closes with the rest of the user interface, because the interface no longer holds the machine. The C64 then performs its normal reset and boot sequence. Reopen the monitor with `C=+O` when the machine has booted.

### Where Reset Works

`C=+R` and `C=+I` act on the machine, so they work from a memory view and from edit mode.

If one of these layers is open, close it with Back first:

- popup
- command prompt
- Bookmarks
- Number calculator
- Hunt results
- Compare results
- opcode completion

`R` without the Commodore modifier controls Range mode.

Reset has no confirmation. Pressing `C=+R` performs the reset immediately. Pressing it again performs another reset.

### Unsupported Targets

If the monitor backend cannot reset the target, `C=+R` shows:

```text
RESET UNAVAILABLE
```

The machine, view, and edit-mode state remain unchanged.

`C=+I` requires a target with an `Interface Type` setting. On a cartridge it shows:

```text
INTERFACE SWAP UNAVAILABLE
```

and keeps the monitor open.

On targets that support interface swapping, `C=+I` closes the user interface after changing the setting. The selected interface mode is used the next time the menu opens.

## Trace Lines in the Device Log

The monitor writes trace lines to the device console for actions that change its view or memory state.

Every trace line starts with `MCM`. Each line records one action.

| Trace line | Written when |
| --- | --- |
| `MCM view HEX` | A view is selected |
| `MCM jump $C000` | `J` jumps to an address |
| `MCM fill $C000-$C0FF` | Fill is accepted, recording the range |
| `MCM fill value $C000 $AA` | The same Fill records the written byte |
| `MCM transfer $C000-$C0FF` | Transfer is accepted, recording the source range |
| `MCM transfer dest $D000` | The same Transfer records the destination |
| `MCM relocate $C000-$C010` | Transfer includes a code range |
| `MCM compare $C000-$C0FF` | Compare is accepted, recording the source range |
| `MCM compare dest $D000` | The same Compare records the destination |
| `MCM hunt $0000-$FFFF` | Hunt is accepted, recording the search range |
| `MCM number commit $C000 $1234` | Number writes a value |
| `MCM freeze on` / `MCM freeze off` | `Z` changes freezer state |
| `MCM cpu bank 7` | `O` selects a CPU bank |
| `MCM vic bank 3` | `Shift+O` selects a VIC bank |
| `MCM picker open Hunt results 12` | A result picker opens with its label and result count |
| `MCM picker close` | A result picker closes |

View names used in trace lines:

- `HEX`
- `ASM`
- `ASCII`
- `SCREEN`
- `BINARY`

CPU- and VIC-bank trace lines are written once for each keypress. The logged value is the bank selected by that keypress.

A view trace line is written only when the view changes.
