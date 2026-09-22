# Software IEC: CMD DOS, sd2iec and C64 OS compatibility

**Status: the contract.** This describes what the Software IEC drive does and why.
Every requirement is either in force, and the tests in `target/pc/linux/parse`,
`target/pc/linux/iecdrive` and `tests/e2e/io/iec` hold the drive to it, or it is marked
**Deliberately unsupported** and says what the drive answers instead and why that is the
right answer. Section 18.1 lists the deliberate exclusions in one table.

Two further documents describe the same drive from other angles:
[software/test/iecdrive/doc.md](../software/test/iecdrive/doc.md) on the name mapping and
[tests/e2e/io/iec/README.md](../tests/e2e/io/iec/README.md) on the hardware tests.
[doc/filenames_design.txt](filenames_design.txt) is the original design note for the
mapping.

The goal is that an Ultimate 64 or Ultimate II+ can be the system drive of C64 OS,
and that in doing so it also becomes a closer replacement for an sd2iec, a CMD HD, a
CMD FD, a CMD RAMLink and an IDE64 for every other program that already targets
those. Issues [#877](https://github.com/GideonZ/1541ultimate/issues/877) and
[#890](https://github.com/GideonZ/1541ultimate/issues/890) are the immediate
drivers, with [#917](https://github.com/GideonZ/1541ultimate/issues/917) added later; this document covers the whole command surface those issues sit in, so the
work can be done once rather than one report at a time.

The Software IEC drive has shipped for years to a large install base, so every
requirement states the source of truth it comes from, and the requirements that ask for
nothing unusual are written down as well, because they are the contract that later work
must not break.

Where a requirement cites a measurement, it was obtained by running the command through
`IecCommandChannel::push_command()` and reading `IecDrive::get_error_string()` back, in a
probe linked against the same object files `target/pc/linux/iecdrive` links.

Nothing in this document is left open. Where the sources disagreed, section 16 states
which primary source settled it, and for two of the cases that source is the 1541 ROM
in `roms/1541.bin` rather than any manual.

---

## 1. Sources of truth and how conflicts are settled

### 1.1 Source register

| Code | Source |
| --- | --- |
| **HD** | *CMD HD Hard Drive User's Manual*, 4th edition, January 1991. The scan linked on issue #877, `primrosebank.net/computers/pet/documents/CMD-HDD-Manual_OCR.pdf`. Page numbers are the manual's own, e.g. 9-15, B-2. |
| **HDR** | *CMD HD Manual Remaster V0.2*. Same text, cleaner typesetting. Used to resolve OCR damage in HD. |
| **FD** | *CMD FD-Series Disk Drives User's Manual*. |
| **RL** | *CMD RAMLink User's Manual*. |
| **IDE** | *The IDE64 Project user's guide*, IDEDOS 0.90, 24 February 2019. Section numbers as printed. Linked by the reporter on #877. |
| **SD** | sd2iec, the `markusC64/sd2iec` fork, at commit `9087321`. Paths are `src/...`. `SD README` is that fork's README, which the reporter named as its documentation. |
| **1541** | *1541-II Disk Drive User's Guide*, DOS error message list. Where a point needs a Commodore drive other than the 1541, the *1571* and *1581 User's Guides* are cited by name. |
| **ROM** | `roms/1541.bin` in this repository, 16384 bytes, mapping to `$C000..$FFFF`. Quoted disassembly was produced from that file. |
| **GAP** | Greg Nacu, "Gaps in Software IEC", c64os.com/post/softwareiecgap, 10 January 2023. The canonical statement of why C64 OS does not support this drive. Written against firmware 3.10a; several of its items are already fixed. |
| **GSD** | Greg Nacu, "SD2IEC User's Manual" v1.3, c64os.com/post/sd2iecdocumentation. |
| **GFN** | Greg Nacu, "Understanding SD2IEC Filenaming", c64os.com/post/sd2iecfilenames. |
| **GUG** | *C64 OS User's Guide*, File System chapter, c64os.com/c64os/usersguide/filesystem. |
| **917** | Issue [#917](https://github.com/GideonZ/1541ultimate/issues/917), "More SoftIEC compatibility issues", opened by the reporter on 18 September 2026. It carries Greg Nacu's measurements of a CMD HD and an sd2iec against this drive, and photographs of the two BASIC programs he used. The programs are transcribed in appendix C. |
| **TRACE** | `log_boot.log.txt`, attached by the reporter to #877 on 11 September 2026. 601 lines of `SOFTIEC-TRACE` output from one successful C64 OS boot on a U64 II. |
| **U** | This firmware. Paths are relative to the repository root. |

### 1.2 Precedence

The reporter set the rule on #877, 11 September 2026: "If the sources differ, then
you have to decide on a case-by-case basis, but most of the time it will come down
to the decision I made in my SD2IEC branch, because that's where I made the
decision."

This specification applies it as follows.

1. Where CMD DOS defines a behaviour and every later device copied it, that
   behaviour is normative. HD is the reference text, with FD and RL used for the
   points where the three CMD devices differ from each other.
2. Where a behaviour exists only because a device has a host file system underneath
   it, **SD** is normative, because that is the device the Ultimate most resembles
   and the one the reporter maintains.
3. Where IDE and SD disagree, SD wins, and the difference is recorded in section 16.
4. Where a source disagrees with the reporter's own measurement on real hardware,
   neither wins on authority. The disagreement is resolved from a primary source that
   can settle it, normally the 1541 ROM in `roms/1541.bin`, and section 16 states the
   evidence. Nothing in this document is left open.
5. Ultimate extensions are allowed where they cannot be mistaken for a documented
   command and do not change the answer to one.

### 1.3 Acceptance

The specification is met when all of the following hold.

* **A1.** C64 OS installs on, boots from and runs from a Software IEC partition, with
  no error on the command channel that a CMD HD would not also produce.
* **A2.** Every requirement below has at least one automated test, in
  `target/pc/linux/parse`, `target/pc/linux/iecdrive` or `tests/e2e/io/iec`, and the
  test fails when the behaviour is reverted.
* **A3.** Nothing in section 15.2 behaves differently.

---

## 2. The model

### 2.1 Device, partitions, directories

**SI-001.** The drive presents one IEC device number, configurable from 8 to 30, with
11 as the default. GUG confirms that C64 OS expects storage devices in 8 to 30 and
supports five at a time.

**SI-002.** The drive presents partitions numbered 1 to 255. Each partition has a
name, a root directory on the host virtual file system, and its own current
directory. Partition 0 is not a partition a user can enter; as a parameter it means
"the current partition". Sources: HD 9-8 and 9-10 (1 to 254 on the HD, 0 means the
current partition), FD (1 to 31), RL (1 to 31), IDE 15.3.1 ("Partition 0 is not a
valid parameter"), GUG ("CMD devices support 255 partitions, numbered from 1 to 255.
Partition 0 is a special system partition that cannot be accessed directly").

**SI-003.** A partition's root may be a directory on the host file system or a
mounted CBM disk image, because the Ultimate virtual file system mounts `.d64`,
`.d71`, `.d81` and `.dnp` images as directories.

**SI-004.** The blocks free reported for any directory is the free space of the
partition, not of the directory. Source: HD 4-5, "all of the blocks within a Native
Mode partition are shared between all directories within that partition".

### 2.2 What the partition model must not do

**SI-005.** The root of a partition is a real directory. Files can be created in it,
it can be listed, and its blocks free is that of the partition. GAP's principal
objection was that the drive presented its storage devices as directory entries of a
pseudo root that could hold no files.

---

## 3. Command string grammar

### 3.1 Path syntax

**SI-010.** The grammar of a name or command argument is

```
[@] [partition] [path] [:] name
```

with `partition` a decimal number, and `path` a sequence of `/`-delimited
components. A path beginning with `//` starts at the root of the partition; a path
beginning with a single `/` starts at the current directory of the partition; a
missing path means the current directory. Every component is followed by a slash.
The final slash before a name is written, and the name is introduced by a colon.
Sources: HD 9-2 to 9-4 with the worked examples `LOAD"1/UTILITIES/COPIERS/:COPY",12`,
`LOAD"//UTILITIES/COPIERS/:COPY",12`, `LOAD"1//UTILITIES/COPIERS/:COPY",12` and
`LOAD"1//:BOOTQ",12` (HD 7-2); IDE 6.1; GUG, "The root directory is specified by two
leading front slashes. Each subdirectory is added to the path by specifying the
subdirectory name followed by a single trailing slash. The trailing slash is always
required."

**SI-011.** The colon is optional when the name is preceded by a slash. Source:
HD 9-19, "It is not required that you include the colon before the subdirectory
name, as long as the subdirectory name is preceded by a slash."

**SI-011a.** A name behind the colon is one component. A slash inside it is a
character of the name, not a path separator, so `CD:SUB/DEEP` looks for a name
containing a slash and answers `71,DIRECTORY ERROR` when no such directory exists.
The path part of SI-010 is the only place a slash separates components. Source:
HD 9-19, where every descent through more than one level writes the components in
the path in front of the colon.

**SI-012.** A path component may contain wildcards, and the first match is used.
Sources: SD README under CD/MD/RD, "You can use wildcards anywhere in the path";
IDE 6.2, "These wildcards can be used in path elements or filename too, in this case
the first filename will be matched."

**SI-013.** The partition number may replace the drive number in any command that
accepts one, except the direct access commands. A direct access channel is bound to
the partition that was current when the channel was opened, and the partition
parameter of `B-R`, `B-W`, `U1`, `U2`, `B-A`, `B-F` and `B-E` is therefore always
written as 0. Source: HD 9-8 and 9-44; GSD "Partition Numbers in Disk Commands".
(IDE 15.5 does not state this rule: IDEDOS direct access addresses sectors by LBA and
has no partition parameter.)

### 3.2 The left arrow

**SI-014.** PETSCII `$5F`, the left arrow, means the parent directory when it stands
in the *name* position, that is directly after `CD[n]` or directly after a colon. It
is an ordinary character when it stands in a *path component* position, that is
between slashes.

| Command | Required result | Source |
| --- | --- | --- |
| `CD<-` | parent directory | HD 9-19, "you can include the back arrow immediately after `CD[n]` to move backwards one directory"; `SD do_chdir()`, `name[0]=='_' && !name[1]` |
| `CD:<-` | parent directory | `SD do_chdir()`, same branch reached through `parse_path`; Greg Nacu measured this on a real CMD HD and on an IDE64 and reported it through the reporter on #877: "cd:<- doesn't go into the directory, it goes up a directory" |
| `CD/<-` | change into the child directory literally named `<-` | Greg Nacu, same measurement, "cd/<- works to go into that directory"; `SD parse_path()` treats a component between slashes with `first_match()` |
| `MD:<-` | create a directory named `<-` | Greg Nacu, same measurement |
| `RD:<-` | remove it | Greg Nacu, same measurement |

The rule has one consequence worth stating. `CD/<-/OTHERDIR`, going up and then down
again in one command, is not a way up, because the arrow between slashes is a directory
name. The spelling for that is `CD/../OTHERDIR`: `U resolve_directory_path()` accepts `.`
and `..` as path components, and `..` is the spelling GAP singles out as "something that
none of the other devices can do". `Suite8-CD-PARENT-OTHERDIR` in
`software/test/iecdrive/testdrive.cc` covers it.

**SI-015.** `CD/:<-` goes to the parent. The colon introduces the name, so the arrow
is in the name position and SI-014 applies unchanged. Sources: `SD do_chdir()`, which
reaches its `name[0]=='_'` branch for this spelling; HD 9-19, "The back arrow cannot
be combined with any subdirectory path information", which says the arrow is not a
path element and therefore only ever a name. The reporter wrote on #877 that this
form should enter the directory, but also that he had "no reference for `cd/:<-`
except trying in an emulator". One rule that explains both of Greg Nacu's hardware
measurements is worth more than a second rule for a spelling nothing sends: to enter
a directory named `<-` the command is `CD/<-`. See section 16, C2.

### 3.3 The command terminator

**SI-016.** One trailing carriage return is removed from a command before it is
parsed, and a command whose second to last byte is a carriage return is truncated at
that carriage return. This is what CBM DOS does. ROM, at `$C2B3`, reached from the
command dispatcher at `$C160`:

```
C2B3  A4 A3     LDY $A3        ; bytes received
C2B5  F0 14     BEQ $C2CB      ; 0 bytes: keep 0
C2B7  88        DEY
C2B8  F0 10     BEQ $C2CA      ; 1 byte: keep 1
C2BA  B9 00 02  LDA $0200,Y    ; last byte
C2BD  C9 0D     CMP #$0D
C2BF  F0 0A     BEQ $C2CB      ; last byte is CR: length becomes len-1
C2C1  88        DEY
C2C2  B9 00 02  LDA $0200,Y    ; second to last byte
C2C5  C9 0D     CMP #$0D
C2C7  F0 02     BEQ $C2CB      ; second to last is CR: length becomes len-2
C2C9  C8        INY
C2CA  C8        INY
C2CB  8C 74 02  STY $0274      ; the length the parser uses
```

**Difference from the ROM.** The second branch drops a carriage return
second to last only when a line feed follows it, which is what BASIC's `PRINT#` to a
logical file number of 128 or more sends (C64 ROM `$AAD7`). The ROM's branch also cuts a
binary parameter of 13 short, for example the high byte of an `M-R` address or a count
of 13, and the reporter of #877 wrote that "needing an additional CR for some commands
do not need to be reproduced". A lone carriage return, which is an empty command after
the strip, answers `31`, as the ROM does at `$C175` and as sd2iec does.

**SI-017.** The binary Change Partition command is exempt from SI-016, because its
parameter byte is mandatory and cannot be a terminator.

**SI-018.** The Position command is exempt from SI-016, because its last parameter
byte is data. Source: `SD parse_position()` begins `command_length =
original_length;`, restoring the length from before the strip.

**Difference from sd2iec.** The record number
and the offset of a relative file are read from the command as sent, so a 13 in the
offset's place is the offset, as the ROM reads it at `$E23E`. The position in a plain
file (SI-082) is read from the command without its terminator. sd2iec reads it from
the command as sent, which makes the documented BASIC form `"P"+CHR$(ch)+CHR$(lo)`
position to `lo + 13*256`, where here it positions to `lo`, which is what the form GSD
documents means. A
record number sent without an offset and without a terminator, as a machine language
caller sends it, takes both of its bytes.

**SI-019.** Where the last parameter is optional, the terminator wins, so partition
13 has to be asked for with the terminator sent explicitly. Source: HD 9-16,
"To avoid problems with reading information from Partition 13, the G-P command
should always be sent with a trailing carriage return (CHR$(13))."

### 3.4 Numeric parameters

**SI-020.** The numeric parameters of the block and user commands are separated by
any run of spaces, commas or cursor-right characters, and a colon may stand between
the command word and the first parameter. Source: HD 9-5 and 9-6, which show
`PRINT#15,"U1";2;0;1;34`, `PRINT#15,"U1 2 0 1 34"` and `PRINT#15,"U1:";2;0;1;34`
as equivalent; `U parse_block_parameters()` reads them.

### 3.5 Command length

**SI-021.** The command channel buffer holds at least 254 bytes, and the name buffer
of a data channel holds at least 254 bytes. Sources: HD 4-6, "the input buffer of
the HD is only 254 characters long"; SD `CONFIG_COMMAND_BUFFER_SIZE`, 120 on the
small AVR boards and 254 on uIEC; GUG, "The total length of the directory path,
including all the slashes is 232 characters", which is the binding number, because a
C64 OS path plus a partition number, a colon and a 16-character name does not fit in
anything smaller.

**SI-022.** A command longer than the buffer answers `32,SYNTAX ERROR` and is not
executed. Sources: HD B-2, error 32; `SD parse_doscommand()`, which returns
`ERROR_SYNTAX_TOOLONG` when the command fills the buffer; ROM `$C2CE`, which raises
error 32 when the command length reaches 42.

The rule covers names as well as commands. A command of 254 bytes or
more answers `32` and is not executed. An OPEN name of 254 bytes or more answers `32` as
well and opens nothing, rather than creating or reading a file under its first bytes.

---

## 4. Error codes

### 4.1 The table

**SI-030.** The five syntax errors are distinct, and each is raised for one condition.
From HD B-2 and 1541:

| Code | Meaning | Raised when |
| --- | --- | --- |
| 30 | SYNTAX ERROR (general) | the command letter was recognised and its arguments were not |
| 31 | SYNTAX ERROR (unrecognized command) | the first character is not a command letter |
| 32 | SYNTAX ERROR (command string too long) | the command or the name fills the buffer (SI-022) |
| 33 | SYNTAX ERROR (illegal file name) | a wildcard, or a character a name cannot carry, where one is not accepted |
| 34 | SYNTAX ERROR (missing file name) | no name, or a colon with nothing after it |

The drive's table of codes and strings is in `U software/io/iec/iec_drive.cc` and the
parser's constants are in `U software/io/iec/cbmdos_parser.h`, where each carries the
condition it stands for as a comment.

An Ultimate extension is allowed where it cannot be mistaken for a documented command and
does not change the answer to one (precedence rule 5). `XPWD` (SI-066) is the only one.

**SI-031.** An unrecognised command answers `31`. Verified in ROM at `$C160`:

```
C160  20 B3 C2  JSR $C2B3      ; strip the terminator first
C163  B1 A3     LDA ($A3),Y    ; first byte of the command
C165  8D 75 02  STA $0275
C168  A2 0B     LDX #$0B       ; twelve command letters
C16A  BD 89 FE  LDA $FE89,X
C16D  CD 75 02  CMP $0275
C170  F0 08     BEQ $C17A      ; found: dispatch
C172  CA        DEX
C173  10 F5     BPL $C16A
C175  A9 31     LDA #$31       ; not found: error 31
C177  4C C8 C1  JMP $C1C8
```

and stated in 1541 ("31: SYNTAX ERROR (invalid command). The DOS does not recognize
the command. It must begin with the first character sent.") and HD B-2. The reporter
asked for 31 on #877 on 11 September 2026, having measured `33` for a one-byte
command `CHR$(0)` and for `A`.

Note the divergence: `SD parse_doscommand()` answers 30 here and reserves 31 for an
empty command. This specification follows HD, 1541, ROM and the reporter's stated
expectation rather than SD, and section 16 records it as C1.

**SI-032.** Opening a name for writing has four cases, and each has a different
answer.

| Case | Answer | Source |
| --- | --- | --- |
| wildcard, no `@` | `33` | 1541: "Pattern matching characters cannot be used in the Save command or when Opening files for the purpose of Writing new data"; `SD file_open()` |
| `@`, the pattern matches a file of the same type | replace it, keeping the **matched** file's name rather than the pattern | ROM `$D8FC` falls through on a type match (the ROM says nothing about which name is kept); IDE 7.1; `SD file_open()` for the name rule |
| `@`, the pattern matches a file of a different type, or matches a REL file | `64,FILE TYPE MISMATCH` | ROM `$D8F5` |
| `@`, the pattern matches nothing | `64` | `SD file_open()` |

The third row is the one that reconciles the sources, and it comes straight out of
the 1541 ROM. The save-with-replace path compares the type of the entry it found
against the type the open asked for:

```
D8E1  AD 00 02  LDA $0200      ; first byte of the name
D8E4  C9 40     CMP #$40       ; '@' ?
D8E6  F0 0D     BEQ $D8F5      ; yes: the replace path
...
D8F5  A5 E7     LDA $E7        ; the found entry's type byte
D8F7  29 07     AND #$07       ; 0 DEL, 1 SEQ, 2 PRG, 3 USR, 4 REL
D8F9  CD 4A 02  CMP $024A      ; the requested type
D8FC  D0 67     BNE $D965      ; different -> error 64
D8FE  C9 04     CMP #$04       ; REL?
D900  F0 63     BEQ $D965      ; also error 64
...
D965  A9 64     LDA #$64
D967  4C C8 C1  JMP $C1C8      ; CMDERR
```

`SAVE` asks for PRG. So `SAVE"@:foo*"` answers `64` on a real drive whenever the
first entry matching `foo*` is not a PRG, which is what the reporter reported on
#877 ("real floppy return 64", 11 September 2026), and it replaces the file when it is. The same check guards the ordinary open
at `$D95C`, which is where the `64` of SI-035 comes from.

`SD file_open()` produces the same four answers, by a different route for the last
row:

```c
if (ustrchr(fname, '*') || ustrchr(fname, '?') || (*fname == 160)) {
   if (command_buffer[0] == '@') set_error(64);
   else                         set_error(33);
   return;
}
```

(`src/fileops.c` at `9087321`; commit `a76deb2` introduced the `*` and `?` tests and
the `160` test was added later.)

**SI-033.** Scratching nothing is not an error. The answer is
`01,FILES SCRATCHED,00,00`. Sources: HD B-1, "01 FILES SCRATCHED (not an error).
The number of files scratched will be indicated in the track variable";
`SD parse_scratch()`, which ends unconditionally with
`set_error_ts(ERROR_SCRATCHED,count,0)`; IDE 15.2.2, which shows the count in the
track field.

**Difference from the CMD manuals.** A scratch whose path does not
exist answers `71,DIRECTORY ERROR` with the partition number, instead of
`01,FILES SCRATCHED,00,00`, so a mistyped path is reported rather than looking like an
empty directory. sd2iec also reports the path error (`SD parse_scratch()`).

**SI-034.** Selecting a partition that does not exist answers
`77,SELECTED PARTITION ILLEGAL`. Source: HD B-5.

**SI-035.** Opening a file for writing when one of that name exists, without `@`,
answers `63,FILE EXISTS`. Opening for reading when none exists answers
`62,FILE NOT FOUND`. Opening an existing REL file with a non-REL type answers `64`.
Sources: HD B-3 and B-4; `SD file_open()`. Covered by Suite5 of
`software/test/iecdrive/testdrive.cc`.

### 4.2 The error code that is not a CBM DOS error

**SI-036.** `69,FILESYSTEM ERROR` is an Ultimate code with no counterpart on any
other device. It must not be the answer to a condition that CBM DOS names.
`U IecDrive::set_error_fres()` maps the unmapped `FRESULT` values to it and puts the
raw `FRESULT` in the track variable. Every path that can reach a user must map to a
documented code first. The known case is SI-083.

---

## 5. Partition commands

### 5.1 Change partition

**SI-040.** `CPn`, with the number in ASCII, and `C` followed by shifted P (`$D0`)
with the number as one binary byte, both select a partition and answer
`02,PARTITION SELECTED,<n>,00`. Sources: HD 9-10, FD, RL, IDE 15.3.1, SD README.
TRACE shows C64 OS using the binary form 29 times in
one boot and the ASCII form once.

### 5.2 Get partition info

**SI-041.** `G-P` with no parameter, or with 255, answers for the current partition.
With 0 it answers for the system partition, which this drive does not have, so byte 0
is 0, meaning "not created", and byte 2 is 0. With any other value it answers for
that partition, and again byte 0 is 0 if it does not exist.
The answer is 30 bytes followed by `CHR$(13)`, laid out as HD 9-15 and 9-16 give it:

| Byte | Content |
| --- | --- |
| 0 | partition type: 0 not created, 1 native, 2 1541, 3 1571, 4 1581, 5 1581 CP/M, 6 print buffer, 7 foreign, 255 system |
| 1 | reserved, `CHR$(0)` |
| 2 | partition number |
| 3-18 | partition name as shown in the partition directory |
| 19-21 | start address, high to low |
| 22-26 | reserved, `CHR$(0)` |
| 27-29 | size, high to low |
| 30 | `CHR$(13)` |

Bytes 19-21 and 27-29 are counted in 512-byte blocks on the HD and the FD and in
256-byte blocks on RAMLink (RL, same page). This drive follows the HD and uses 512. The
size is the free plus the used space of the partition's file system, clamped to
`0xFFFFFF`, which is what `SD parse_getpartition()` answers with.

**SI-042.** Byte 1 stays zero. The FD redefines it as a disk-information bit field
(FD, Getting Partition Information: bit 7 disk present, bit 6 formatted, bit 5 valid
CMD or CBM format, bit 4 true 1581, bits 3-0 density) and sd2iec writes `0xE2` there,
which decodes as an FD-2000 with a 1.6 MB disk inserted. This drive is not an FD and
must not claim to be one. IDE leaves the byte unused (IDE table 26).

**SI-043.** The partition type is decided from the geometry of the file system at the
partition's root: no track and sector addressing, or 256 sectors on track 1, is native;
40 sectors is a 1581; and 21 sectors is a 1541, or a 1571 above 42 tracks. That reaches
the same mapping `SD pdir_refill()` reaches from the image type
(`(imagetype & D64_TYPE_MASK) + TYPE_NAT - 1` with `D64_TYPE_DNP == 1`, so a DNP image
lists as native). `$=P` and `G-P` read it from the same function, so they cannot
disagree.

### 5.3 Partition directory

**SI-044.** `LOAD"$=P[:pattern][=tp]"` lists the partitions. `tp` is one of `N`, `4`,
`7`, `8`, `C`. Sources: HD 9-14; FD and RL, which drop `C`; SD README.

**SI-045.** The listing is a header line, one line per partition, and the BASIC end
marker. **There is no blocks free line.** Source: issue #890; GAP, "The partition
directory does not list a blocks free count"; `SD pdir_refill()`, whose last act is

```c
buf->lastused = 1;
buf->sendeoi = 1;
memset(buf->data,0,2);
```

**SI-046.** The number printed before the header name is the number of partitions.
Sources: GAP, "The first number before the partition directory's header line is a
count of how many "user" partitions there are"; `SD load_directory()`,
`buf->data[HEADER_OFFSET_DRIVE] = max_part`.

**SI-047.** The block count column of each partition line is the partition number.
Sources: GAP, "The typical block size of a directory entry is used for the partition
number"; `SD pdir_refill()`, `dent.blocksize = part + 1`.

**SI-048.** The type column is the three-character partition type: `NAT`, `41 `,
`71 `, `81 `. Sources: issue #877; SD `filetypes[]` entries 8 to 11.

**SI-049.** The header name is the drive's name, not a partition's. `SD` uses the
static string `SD2IEC` with id `IK`. This drive uses `ULTIMATE HD` with id `UL 64`,
which `U setup_partition_read()` holds.

**SI-050.** A `SYSTEM` line is not emitted. `SD` emits one from `syspart_line[]`
immediately after the header, and IDE lists partition 255 as the disk label line.
This drive has no system partition, and SI-041 already answers `G-P 0` with type 0.
Recorded so the omission is deliberate rather than accidental.

### 5.4 Other partition commands

**SI-051.** `R-P:newname=oldname` renames a partition. Source: HD 9-14, FD, RL. The
old name is a partition's name and not a path, so the partition list is searched for it,
without regard to case; a name no partition carries answers `77,SELECTED PARTITION
ILLEGAL`. An empty name on either side answers `34` and a command with no `=` answers
`30`. The new name keeps its first sixteen characters, the length of a CMD partition
name, and is what the partition directory, `G-P` and the header of a listing of that
partition's root then show.

The dash in the second character is what separates `R-P` and `R-H` (SI-064) from a file
rename, as `SD parse_doscommand()` separates them with
`if (command_length > 2 && command_buffer[1] == '-')`.

The name lives as long as the drive runs. The partition list is written to an `.IPR`
file only when the menu saves it, which is equally true of a partition renamed from the
menu, so the command is as durable as the user interface it shares the list with.

**SI-052.** `N[n]:name[,id]` formats. Its full behaviour is specified in SI-071.

**SI-053.** `I[n][:]` initialises. On a device with no removable medium this is a
no-op that answers `00, OK` and frees the user buffers. Sources: HD 9-13, "This
function is performed automatically by the HD, but the command has been implemented
to retain compatibility"; `SD parse_initialize()`, which frees the user buffers.
`I` answers `00, OK`; `UI` answers the 73 message.

**SI-054.** `V[n][:]` validates. **Deliberately unsupported**; the drive answers `31`,
which is also what `SD parse_doscommand()` answers, having no `V` in its command switch.

On a host file system there is nothing to validate: the file system keeps its own
allocation and no program can put it out of step through this drive. Inside a mounted
CBM disk image there is something to validate, and doing it means rebuilding the block
availability map from every directory in the image, following the chain of each entry,
the side sector chain of each relative file and the record chain of each GEOS file, and
recursing into the subdirectories a native image can hold. A walk that misses one of
those chains marks live blocks free, and the damage appears later, when the next write
hands those blocks out again; nothing at the time of the command would show it. Answering
`00, OK` without rebuilding the map would tell a program that the image was checked, which
SI-030 and the position taken for `M-W` (SI-105) both refuse.

The reporter wrote on #877 that "V not implemented, but that's ok for the time being".

**SI-055.** The 1581-style sub-partition commands `/[n]:name` and
`/[n]:name,`+`CHR$(st)CHR$(ss)CHR$(sl)CHR$(sh)`+`,C` are not implemented and are out
of scope. They address 1581 emulation partitions, which this drive does not have.
Sources: HD 9-9 and 9-11; 1581 User's Guide, which gives the same syntax.

---

## 6. Directory commands

**SI-060.** `MD[n][path]:name` creates a directory. A colon is required; without one
the answer is `34`. A name that is a single shifted space answers `34`. Sources:
HD 9-17, which states the colon rule as its first guideline; `SD parse_mkdir()`.

**SI-061.** `CD[n]{<-|[path][:]name}` changes directory, per SI-010 and SI-014.

**SI-062.** `CD` into a file whose name has a disk image extension mounts that image
and makes its root the current directory; `CD<-` from the root of a mounted image
unmounts it. Sources: SD README, "CD is also used to mount/unmount image files";
GSD "Mounting a Disk Image".

**SI-063.** `RD[n]:name` removes a directory. It takes no path: a `/` anywhere in the
command answers `34`. It refuses a directory that is not empty. Sources: HD 9-19,
"This command does not allow the use of paths in order to avoid problems with
removing a subdirectory which is a parent of the directory in which you are located";
`SD parse_rmdir()`, which rejects any `/` with `ERROR_SYNTAX_NONAME` and answers
`63,FILE EXISTS` for a directory that still has entries.

**SI-064.** `R-H[n][path]:newname[,id]` renames the header of a directory. The name is
at most 16 characters and the id two. Sources: HD 9-15; IDE 15.6.5;
`SD parse_set_header(3)`. The path selects the directory and the name behind the colon
is the new header; with no path it is the current directory of the partition. An empty
name answers `34`, a name with a wildcard `33`, and a path that is not there
`71,DIRECTORY ERROR`.

What a header is depends on what the directory sits in, and in each case it is what a
listing of that directory shows (SI-065).

| Directory | Header | What `R-H` changes |
| --- | --- | --- |
| inside a CBM disk image | the disk name in the BAM of the root, or in the header block of a subdirectory | that name; the disk id and the DOS version stay as they are unless an id is given |
| a subdirectory on the host file system | the name the parent holds, which is all a host directory has | the directory's name, and the drive's current directory follows it when it is standing in or below that directory |
| the root of a partition | the partition's name | the partition's name, as `R-P` does (SI-051) |

C64 OS reads the header for a related reason: GAP says "C64 OS in particular uses the
directory header when creating a favorite... The name of the favorite is taken from the
directory header name."

The drive asks the file system for the label first and renames the directory only where
the file system has none, so a medium that gains a separate label later needs no change
here. GSD states that "The R-H command is not supported by sd2iec"; the CMD manuals
describe it and this drive follows them.

**SI-065.** The header line of a directory listing carries the name of the directory
as it appears in its parent, and of the partition when the root is listed. Source:
GAP, "on SoftIEC it tries to show you the current path. This is inconsistent for the
sake of doing something clever, but it breaks the expectation... I would change this
behavior to SD2IEC's way, in the name of compatibility"; `SD fat_getdirlabel()`,
which returns the volume name for the root and otherwise walks `..` to find the entry
whose cluster is the current directory and returns that entry's name.

**SI-066.** `XPWD` answers with the current partition and its current directory, as
`<n>:<path>`, in upper case and ending in a slash, and it leaves the error channel at
`00, OK`. It is an Ultimate extension, which SI-030's fifth rule allows: no CMD or
sd2iec command has that spelling, and nothing else answers the question.

---

## 7. File commands

### 7.1 Open

**SI-070.** The type and access suffixes are `,P`, `,S`, `,U`, `,L`+`CHR$(rl)` and
`,R`, `,W`, `,A`, `,M`. Secondary address 0 forces read and PRG; secondary address 1
forces write and PRG; any other secondary address defaults an unspecified type to
SEQ. Sources: HD 9-23 to 9-31 for `,P`, `,S`, `,U`, `,L`, `,R`, `,W` and `,A`;
`SD file_open()` for `,M` (`case 'M': /* Modify */`, which HD does not document) and
for "Force mode+type for secondaries 0/1"; `U setup_file_access()` applies it.

**SI-071.** `N[n]:name[,id]` creates or formats a disk image, as sd2iec does, because
this drive has no formattable medium of its own. Source: SD README under `N:` and
`SD fat_format_image()`:

* If the name ends in a known image extension and no such file exists, an image of
  that format is created. `.D64` and `.D41` mean a 1541 image, `.D71` a 1571, `.D81`
  a 1581, `.DNP` a native partition image. An id is required.
* For `.DNP` the id is a three digit track count and the image is
  `65536 * tracks` bytes, created but not formatted.
* If the name has no known image extension, `.D64` is appended, and in that case an
  existing file is **not** overwritten: the answer is `63,FILE EXISTS`.
* If the file exists and the extension was given explicitly, it is formatted, except
  that an existing `.DNP` is refused with `63` whether or not the extension was given
  (`SD fat_format_image()`, `if (ext == NULL || imagetype == IMG_IS_DNP)`; the README
  text omits the DNP exception).
* The disk label is the name with the extension removed. It keeps sixteen characters
  and the id two, as CBM DOS takes them, so a longer label or id never runs into the
  separator and the DOS type the header holds behind them.

The name buffer holds a 16 character label and a four character extension, so
`N:ABCDEFGHIJKLMNOP.D81,AB` creates a D81 image.

**SI-071a.** When the directory `N` addresses is inside a mounted disk image, `N` formats
that image rather than creating an image inside it, which is what a program formatting
its disk asks for. The format goes through the file system that holds the image open, so
the block map and the directory that file system caches are the ones rewritten and the
image file keeps its size. A file open on the image answers `60,WRITE FILE OPEN`, because
that file would go on reading and writing blocks the format has handed back. A
subdirectory a native image carries is gone with the format, so the partition falls back
to the deepest directory that still exists.

**SI-072.** A file whose name ends in a disk image extension, or in `.CRT` or
`.TCRT`, is written to the host file system under exactly that name, with no type
extension added. Source: `SD should_save_raw()`; SD README, "PRG files that have D64,
D41, D71, D81, DNP or M2I as an extension will always be written without an x00
header and without any additional PRG file extension."

### 7.2 Scratch, rename, copy

**SI-073.** `S[n][path]:pattern[,[n][path]:pattern...]` scratches. Each element gets
its own partition and path. Directories are skipped. The answer is
`01,FILES SCRATCHED,<count>,00` with the count in the track variable, per SI-033.
Sources: HD 9-27; `SD parse_scratch()`; IDE 15.2.2, whose examples include
`@S/STUFF/:*=OLD,*=BAK,/STUFF/BAK/:*`.

**SI-074.** `R[n][path]:newname=[[n][path]:]pattern` renames a file or a subdirectory.
When the source path and the destination path differ, the entry moves. An empty new
name answers `34`. A new name that already exists
answers `63`, unless it differs from the old name only by case. A wildcard in the new
name answers `33`. Sources: HD 9-26, whose section is headed "Renaming Files and
Subdirectories" and which reads "Filenames and Native Mode subdirectory names may be
changed by using either the DOS RENAME or the BASIC 7.0 RENAME command", with
appendix J listing the command as "RENAME (Files and Subdirectories)";
`SD parse_rename()`, which passes `FLAG_HIDDEN` and no type to `first_match()`, so an
entry of any type is matched, and whose `fat_rename()` renames the host entry, a
directory included; and `SD parse_rename()`'s note that "The 1541 renames the file to
'=' in this case, but I consider that a bug".

**Difference from the CMD manuals and sd2iec.** `SD parse_rename()` answers `62,FILE
NOT FOUND` when the two paths differ and HD 9-26 requires one partition, where this
drive moves the entry, as IDE 15.2.3 documents. `Suite8-RENAME-P1-TO-P2` in
`software/test/iecdrive/testdrive.cc` asserts the move, and no program has been named
that needs the `62`. A directory cannot move inside itself, where its only entry would
be reached through the directory it names; that answers `71,DIRECTORY ERROR`, and the
check is in `FileManager::rename_impl`, so the browser's Move, FTP and REST are held to it
as well. An x00 file (SI-144) moved this way keeps its host name. A
subdirectory is renamed under its own name: `U do_rename()` resolves the source with
directories allowed, and `CreateIecName()` reports `e_folder` for one, so the
destination is built without a file type extension. See C14.
The directory half was reported on 917 as `@"MD:A` followed by `@"R:B=A` answering
`62,FILE NOT FOUND`, and the reporter's `finit` hits it when it renames `OS` through a
temporary name.

**SI-075.** `C[n][path]:new=[[n][path]:]name[,[[n][path]:]name...]` copies, and with
more than one source appends them into the target. Path parsing restarts for every
source. The target takes the file type of the first source. Sources: HD 9-28, which
caps the sources at five; SD README under `C:`, which has no cap; `SD parse_copy()`,
whose `savedtype` takes the type from the first source (the README does not state
the type rule).

**SI-076.** `L[n][path]:name` toggles the lock flag on one file or directory. A
locked file lists with `<` after its type and cannot be scratched; a locked directory
cannot be removed. Sources: HD 9-30; IDE 15.2.4; `SD parse_lock()`, which toggles
`FLAG_RO`. The reporter raised it on #877.

`L` finds its entry through the directory, as a
scratch does, so it locks the entry a listing shows under that name rather than the
first host file matching `NAME.???`. On a host file system the lock is the FAT read-only
attribute, which the Ultimate's FTP server and file browser then also respect; in a disk
image it is bit 6 of the file type byte. A scratch skips a locked entry, and also an
unlocked entry of the same name and type that follows a locked one in a disk image,
because deleting by name removes the first entry of that name.

**SI-077.** The sd2iec spellings of the attribute commands, from
`SD parse_elock()`, `parse_eunlock()`, `parse_ehide()`, `parse_attr()`,
`parse_set_header()` and the dispatch in `parse_doscommand()`.

| Command | Effect |
| --- | --- |
| `EL[n][path]:name[,name...]` | set the lock on every entry each name matches |
| `EU[n][path]:name[,name...]` | clear it on every entry each name matches |
| `EH[n][path]:name`, with something other than a colon after the partition number | turn the hidden flag of one entry over |
| `EH[n]:name[,id]`, `XH[n][path]:name[,id]`, `D:name[,id]` | set a directory header, which is `R-H` (SI-064) |
| `A:[R][H][A]=name[,name...]` | set exactly the attributes named on every entry each name matches, and clear the others |

The lock these set and clear is the one `L` turns over (SI-076), so a file `EL` locks is
a file `L` unlocks and a file a scratch skips. `R` in `A` is that lock, `H` is the hidden
flag of SI-134 and `A` is the archive flag, which the drive stores and nothing reads.

Two differences from the source are deliberate. `SD parse_elock()` and `parse_eunlock()`
skip directories and `parse_attr()` acts on the first match only; here all five commands
act on every entry the name matches, directories included, because `L` locks a directory
(HD 9-30) and a lock that `EL` and `L` disagreed about would be two locks. A name that
matches nothing answers `62,FILE NOT FOUND`, and a medium that does not carry the
attribute, such as the hidden flag inside a CBM disk image, answers `30`.

`EL:$` and `EU:$`, which write-protect a whole mounted image in sd2iec, are not
implemented and answer `62`, because the name `$` matches no entry. Write-protecting a
mounted image means changing the writability of a file system that the file browser, FTP
and the REST interface share, and this drive has a per-entry lock that covers what a
program asks `EL` for.

`XH+` and `XH-`, which turn hidden files on and off for every later listing, are one of
the sd2iec settings commands that section 19 places out of scope; the filter `=H` asks
for them per listing instead. They answer `30`.

`A` and `D` are command letters because of `A:` and `D:`, so an unrecognised argument to
either answers `30` under SI-030. That covers the sd2iec direct sector commands `DI`,
`DR` and `DW` of SI-096.

---

## 8. Relative files

**SI-080.** `OPEN lf,dv,sa,"[[n][path]:]name,L,"+CHR$(rl)` creates or opens a
relative file. The comma before the record length byte is part of the syntax: the
1581 User's Guide gives `OPEN 2,8,2,"GRADES,L," + CHR$(100)`, and `SD file_open()`
reads the byte after the next comma. HD 9-31 renders the same form, with OCR damage
that can be read either way. `P`+`CHR$(ch)`+`CHR$(lo)`+`CHR$(hi)`[+`CHR$(of)`] positions to a
record. `ch` is the channel, which programs written to HD 9-32 send as the secondary
address plus 96. Record numbers and byte offsets are 1-based. Positioning beyond the
end answers `50,RECORD NOT PRESENT`, which is not an error when the intent is to
extend the file. Sources: HD 9-31 to 9-33; IDE 15.1.1; GSD "Positioning (seeking)
Within a File". Covered by Suite4 of `software/test/iecdrive/testdrive.cc`.

**SI-081.** The channel byte of `P` is masked to its low four bits from 19 up, so both
the documented BASIC form `PRINT#15,"P"CHR$(96+ch)` and the bare byte reach the same
channel, while a byte just outside the channel range is still refused. Sources:
`SD parse_position()`, `find_buffer(command_buffer[1] & 0x0f)`, which masks
unconditionally; the ROM masks from 19.

**SI-082.** `P` also positions inside a plain file, to a 32-bit little-endian byte
offset, with the missing high bytes taken as zero. Sources: SD README under `P`;
GSD "Positioning (seeking) Within a File"; IDE 15.1.1, which documents both the
four-byte form and `F-P`. `U do_set_position()` implements it for `e_file`.

**SI-083.** `P` on a file opened for writing must be able to move beyond the current
end of the file, and a following write must extend the file. Sources: IDE 7,
"Unlike other systems it's possible to seek beyond the end of a file when writing or
modifying and create 'holes'"; the JiffyDOS MD81 sequence the reporter posted on #877
on 11 September 2026 as a separate `SOFTIEC-TRACE` excerpt (lines #259 to #262; it is
not in TRACE, which is the boot log), which is
`OPEN "FOO.D81,P,W"`, then `P`+`CHR$(4)`+`CHR$($FF)`+`CHR$($7F)`+`CHR$($0C)` to
position to 819199, then one byte, then `CLOSE`.

A seek on a write channel writes out what the channel holds at the old position and then
moves, without reading the file back, because the file is not open for reading. A medium
that cannot put the file there answers `72,DISK FULL`, which CBM DOS names, rather than
`69` (SI-036).

**SI-084.** A relative file on the host file system is read in either of the two layouts
in use and in the `R00` wrapper, and written in the two byte layout. Nothing on a user's
medium changes. Reading only one layout silently misreads the other: where the second
byte of an sd2iec file happens to be zero, the record length is right and every record is
then read one byte late, which is why the discriminator below is on the file size rather
than on the record length alone.

| | header | first record at | file size |
| --- | --- | --- | --- |
| this firmware | two bytes, record length little endian | 2 | `2 + n*r` |
| sd2iec, no wrapper | one byte, the record length | 1 | `1 + n*r` |
| either, with an x00 wrapper | 26 bytes, record length at offset 25 | 26 | `26 + n*r` |

Sources: `U setup_file_access()` writes `uint16_t wrd = record_size` and
`U seek_record()` uses `const uint32_t c_header = 2`; `SD create_file()` sets
`buf->pvt.fat.headersize = 1` and writes that one byte, and `SD fat_file_seek()`
adds `headersize` to every position; `SD P00_RECORDLEN_OFFSET` is 25, and SD README
says "When x00 support is disabled the first byte of a REL file is assumed to be the
record length".

**The two plain layouts are told apart by the file size, under two stated
assumptions.** Both put the record length `r` in byte 0, so `r` is known before the
layout is. This firmware's files are `2 + n*r` bytes and sd2iec's are `1 + n*r`, so
`size mod r` is `2 mod r` for one and `1 mod r` for the other. The reader must compare
against `2 mod r`, not against the constant 2: for `r = 2` the residues are 0 and 1.
The two residues coincide only when `r` divides 1, that is only at `r == 1`. HD 9-31
gives the legal record length as 2 to 254, and for every `r` in that range the test is
exact. A second, cheaper check comes first: byte 1 is zero in this firmware's layout,
because a record length below 256 has a zero high byte, so a non-zero byte 1 settles
it immediately; a zero byte 1 settles nothing, because it may be sd2iec payload.

The assumptions, and what breaks them:

1. Both writers pad every record to `r` bytes, so a plain file is never left with a
   partial last record. This firmware does so in `U IecChannel::write_record()`
   (`memset(buffer + pointer, 0, recordSize - pointer)` then `f->write(buffer,
   recordSize, ...)` at a record-aligned `recordOffset`), and sd2iec does so in
   `SD fat_file_write()` (`memset(buf->data + buf->lastused + 1, 0, ...)` and
   `buf->lastused = buf->recordlen + 1`). A file truncated by something else, for
   example a copy tool that stopped early, has an arbitrary residue and is read as
   whichever layout the residue happens to select, and no size-based rule can do
   better without a wrapper.
2. `r = 1` is outside HD's legal range, but both writers accept it: `U
   setup_file_access()` refuses only 0 and values of 256 and above, and
   `SD file_open()` refuses only 0. For `r = 1` both residues are 0 and the size
   cannot decide. The reader then takes this firmware's two byte layout, because that
   is the only plain layout this firmware has ever written, and an sd2iec `r = 1`
   file is read one byte late unless it carries an `R00` wrapper. This is a recorded
   limitation, not a guarantee.

An `R00` wrapper is recognised by its extension and `"C64File"` signature (SI-144)
before the size test is applied, so the wrapped layout never enters this comparison.
Empty files are covered: a fresh file is 2 bytes from this firmware and 1 byte from
sd2iec, residues `2 mod r` and 1.

Inside a disk image the file
system presents a relative file with a two byte header whatever its size, so the one
byte test is not applied there. A relative file opened with a record length is looked up
by its CBM name first, so an existing `R00` file is opened rather than created again
beside it (SI-146).

---

## 9. Direct access

**SI-090.** `OPEN lf,dv,sa,"#"` allocates a 256-byte buffer on that channel, with the
buffer pointer at byte 1. `"##n"`, exactly three characters, asks for `n` chained
256-byte buffers with the pointer at byte 0; if the name is not exactly three characters
a standard buffer is allocated instead. Sources: SD README "Large buffers", which is the
source for `##n`, for the `70` answer and for the statement that a standard buffer starts
"with the read/write pointer set to byte 1"; GSD "Buffers and Large Buffers". HD 9-40
documents only `#[bu]`, where `bu` selects a drive buffer number 0 to 29, and says
nothing about the initial pointer.

`##1` is the standard buffer with its pointer at byte 0. A chain of more than one answers
`70,NO CHANNEL`, which is the answer SD README gives when there is not enough room: a
buffer here is 256 bytes because that is the sector size of every disk image this drive
serves, and the commands that fill a buffer from a medium, `B-R`, `B-W`, `U1` and `U2`,
each move one such sector. sd2iec has larger buffers because its direct sector commands
`DR` and `DW` move 512 bytes, and those are out of scope (SI-096). Answering `70` tells a
program that asked for eight buffers that it did not get them, where handing it one would
not.

**SI-091.** The parameter forms are, from HD 9-43 to 9-46 and Appendix J:

| Command | Parameters |
| --- | --- |
| `B-A`, `B-F` | partition, track, sector |
| `B-R`, `B-W`, `B-E`, `U1`, `U2` | channel, partition, track, sector |
| `B-P` | channel, position |

**SI-091a.** `B-A` of a block that is already allocated answers `65,NO BLOCK` with the
next higher free track and sector, or track 0 when no higher block is free, which is the
answer the 1541-II User's Guide gives for error 65 and HD 9-43 describes; a caller reads
that pair and asks again. `B-F` of a block that is already free answers OK and changes
nothing, because neither manual lists an error for it. sd2iec implements neither command.


**SI-092.** `B-P` takes an optional third parameter, the high byte of a 16-bit buffer
position. Source: SD README, "The B-P command supports a third parameter that holds the
high byte of the buffer position, For example, 'B-P 9 4 1' positions to byte 260"; GSD
"The Buffer Pointer". With two parameters the position is the 1541's eight bit one, which
keeps the low byte of what it is given, so `B-P:2,300` positions to byte 44. With three,
the position is sixteen bits, and one past the end of a 256 byte buffer (SI-090) names no
byte the drive can give out and answers `30`.

**SI-093.** The partition parameter of a direct access command is ignored; the
channel uses the partition that was current when it was opened. Source: HD 9-8 and
9-44, "The partition number should always be 0 (zero). This is because direct access
commands will always access the partition that was the current partition at the time
the direct access channel was opened"; GSD says the same.

**SI-094.** `B-R` and `B-W` differ from `U1` and `U2`: they use the first byte of the
block as a length. Sources: HD 9-44 and 9-45; `SD parse_block()`, which sets
`buf->position = 1; buf->lastused = buf->data[0];` for `B-R` and writes
`buf->data[0] = buf->position-1` for `B-W`.

**SI-095.** `B-E` is not implemented and will not be: it executes 6502 code in drive
memory, which this drive does not have. It answers `30`, because `B` is a recognised
command letter and only the sub-command is not (SI-030).

**SI-096.** The sd2iec direct sector commands `DI`, `DR` and `DW`, and the error
`78,BUFFER TOO SMALL`, are out of scope. They expose the raw storage device below the
file system, which is not something this firmware should offer over IEC. Source:
SD README under `D`.

---

## 10. Device commands

**SI-100.** `U0>`+`CHR$(d)` changes the device number, for `d` in 8 to 30. Sources:
HD 9-49; IDE 15.4.1; `SD parse_user()`, which accepts 4 to 30 and recognises the `>`
by `(command_buffer[2] & 0x1f) == 0x1e`. GAP lists this as a gap: "Changing the
device address with u0> command. This is supported on all devices going back to
ancient times, before the 1541. This produces a syntax error in SoftIEC."

The number is written into the IEC processor's
device number slot without holding the processor in reset, because the command is still
on the bus, and it is not written to the configuration.

**SI-101.** `S-8`, `S-9` and `S-D` are the typed aliases for swapping to device 8,
device 9 and back to the configured default. Sources: HD 9-34; IDE 15.4.1. On this drive
there is one drive behind the number, so `S-8` and `S-9` set the device number to 8 and
to 9 and `S-D` returns it to the one the settings hold. The number is not written to the
settings, as `U0>` does not write it (SI-100).

The command is exactly three characters with a dash in the second, which is how
`SD parse_doscommand()` tells it from a scratch: `S:-8` scratches a file named `-8` and
`S-88` scratches one named `-88`. A third character that is none of `8`, `9` and `D`
answers `30`, because `S` is a command letter and its argument is not (SI-030); that
covers `S-C`, the SCSI pass-through of SI-106. sd2iec answers the whole group `31`,
recognising the form without implementing the swap.

**SI-102.** `W-1` sets a software write protect for the whole drive and `W-0` clears
it. Sources: HD 9-35; IDE 15.4.10. While it is set, everything that would change a
medium answers `26,WRITE PROTECT ON` and changes nothing, and everything that only reads
works as it does otherwise. The command is exactly three characters, and `W` is a command
letter only for those two, so any other `W` answers `30`. The protection lasts as long as
the drive runs and is not written to the settings, as the device number of `U0>` is not
(SI-100).

**SI-102a.** The gates the protection is enforced at, which together are every path from
the bus to a medium.

| Gate | Commands that reach it |
| --- | --- |
| the eleven command handlers that change a medium | `MD`, `RD`, `C`, `N`, `R`, `S`, `R-H`, `R-P`, `L`, `EL`, `EU`, `EH`, `A`, `U2`, `B-W`, `B-A`, `B-F` |
| the file open | a write, an append and a replace, which answer `26` and open nothing |
| the relative file | it opens for reading, and the record write answers `26`, drops the record and leaves the channel open, because the file can still be read |
| the record seek | a record past the end of the file is not created; the answer is `50,RECORD NOT PRESENT` |

`Suite11-SI102-WriteProtect` sends one command through each gate and then checks that
the medium is unchanged and that reads still work, so a gate left out fails a test rather
than leaving a hole.

**SI-103.** The three resets are distinct.

| Command | Effect | Answer |
| --- | --- | --- |
| `UI` | nothing | `73,<dos version>,00,00` |
| `UI+`, `UI-` | select the serial timing | `00, OK` |
| `UJ` | close every open data channel; keep the current partition, every partition's current directory, and any mounted image | `73,...` |
| `U`+shifted J, `CHR$(202)` | close every open data channel, return every partition to its root, select the default partition | `73,...` |

Sources: HD 9-51; SD README under `UI/UJ` and `U<Shift-J>`; GSD "Warm, Cold and Hard
Reset".

**SI-103a. None of the three reconfigures the IEC interface.** A reset answers the
transaction it arrived in and performs the state reset once the command channel has been
unlistened, and it never calls `IecInterface::configure()`: nothing about the bus
configuration changes, because the device number, the enable flag and the slot assignment
are the same after a drive reset as before it.

The reason is that `IecInterface::configure()` opens with

```cpp
HW_IEC_RESET_ENABLE = 0;
```

which holds the IEC processor in reset while it rewrites the device-number slots. Command
handlers run on the "IEC Server" task, inside the bus state machine loop in
`IecInterface::task()`, while the host still has the command channel addressed, so a
reset of the IEC processor from there drops the drive off the bus in the middle of a
handshake and leaves the host waiting on a handshake that never completes. That is the
symptom GAP reports against firmware 3.10a: "cold reset and hard reset 'uj' and 'uJ' seem
to lock up the bus. Needs a STOP+RESTORE to recover". `IecDrive::reset()` is the menu's
reset, which does reconfigure, and a command handler must not reuse it.

**SI-103b.** A change in the Software IEC settings reconfigures the IEC processor only
when the device number or the enable flag changes. Holding the processor in reset drops a
transfer on the bus, so turning a setting such as **Log Every Operation** on or off must
not do it. The drive's **Reset**, from the menu or from `PUT /v1/drives/softiec:reset`, is
meant to drop whatever is on the bus: it restarts the processor in any case and puts the
drive back on the device number the settings hold. Test: `Suite11-ResetRestartsProcessor`.

**SI-104.** `U3` to `U8` and `UC` to `UH` jump into drive memory and are not
implemented; they answer `30`, for the same reason as SI-095. Source: HD 9-51.

**SI-105.** The memory commands are implemented as follows.

* `M-R`+`CHR$(lo)`+`CHR$(hi)`[+`CHR$(n)`] returns `n` bytes over the error channel,
  with `n` absent meaning 1 and `n` zero meaning 256. It does not read past a page
  boundary. Sources: HD 9-46 and 9-47 for the syntax, zero meaning 256 and the page
  boundary; ROM `$CB24` (`LDA $0274 / CMP #$06 / BCC`, one byte when the command is
  shorter than six bytes) and `SD handle_memread()` ("Read 1 Byte if no explicit
  length was provided") for the absent count. HD does not describe the absent case.
* `M-W`+`CHR$(lo)`+`CHR$(hi)`+`CHR$(n)`+data accepts 1 to 248 bytes and discards
  them. Source: HD 9-47.
* `M-E`+`CHR$(lo)`+`CHR$(hi)` answers `00, OK` and does nothing. Source: HD 9-48.

What `M-R` returns is specified in section 11. The reporter's position on #877 is
that the count matters more than the content: "M-R should return the number of
queried bytes. I agree we have nothing good to return but we can return each byte to
be 42... That way, a software that does M-R to identify devices is syntactically
happy." C64 OS sends the four probes at every boot (TRACE).

**`M-W` and `M-E` are deliberately unsupported** and answer `30`. Nothing of what
`M-W` writes is kept and `M-E` runs nothing, so answering `00, OK` would tell a fast
loader that its drive code is in place and running. An `00, OK` means the work was done,
which is also why a clock write that the clock does not accept answers `30` (SI-120). The
reporter's request was for `M-R`, which C64 OS sends four times at boot (TRACE).

**SI-106.** `S-C`, the SCSI pass-through of HD 9-39, is out of scope.

---

## 11. Device identification

This section exists because it is the one place where copying another device exactly
would be wrong.

**SI-110.** The `UI` message is the primary identification and must name this device.
Source: SD README, "If you are the author of a program that needs to detect sd2iec
for some reason, DO NOT use M-R for this purpose. Use the UI command instead and
check the message you get for 'sd2iec' and 'uiec' instead"; GSD "Device Detection". The
message is `73,U64HD ULTIMATE DOS V2.0,00,00`, whose fixed form SI-114 gives.

**SI-111.** `M-R` must not return the signature of a 1541, a 1571, a 1581 or a CMD
device, because a program that reads one of those will then drive this device as that
model. The signatures are, read out of the ROM images in this repository:

| Address | 1541 | 1571 | 1581 | CMD |
| --- | --- | --- | --- | --- |
| `$FEA4` | `CA CC` | `CA CC` | `FF FF` | `"HD"`, the fifth and sixth bytes of `"CMD HD"` at `$FEA0` |
| `$E5C5` | `35 34` = `"54"` | `35 37` = `"57"` | `FF FF` | |
| `$A6E8` | outside the ROM | `48 C9` | `35 38` = `"58"` | |

The two bytes at `$E5C5` and `$A6E8` are the model digits inside the drive's power-up
message. The CMD signature is documented by its own manual: HD 9-47 gives the
detection example `PRINT#15,"M-R"CHR$(160)CHR$(254)CHR$(6)` followed by
`IF A$="CMD HD"`, that is six bytes from `$FEA0`.

TRACE shows C64 OS reading exactly `$FEA4`, `$E5C5`, `$A6E8` and `$0002`, two bytes
each, before falling back to `UI`. The probe strings are literal bytes inside
`OS/LIBRARY/IEC.LIB.R`.

**SI-112.** `M-R` returns the requested number of bytes, every byte `$00`, at every
address. There is no address table and no exception.

Three things follow from that choice and each is a reason for it.

* The byte count is what makes a probe well formed. A device that answers a syntax
  error to `M-R` is not merely unidentifiable, it is broken from the caller's point
  of view, which is what TRACE shows happening to C64 OS. The reporter's
  position on #877 was the same, and he proposed 42; the count is the part both
  positions agree on.
* `$00` matches none of the model signatures in SI-111, so no caller can conclude
  1541, 1571, 1581 or CMD.
* `$00` is the one value sd2iec deliberately returns for a detection address:
  `{ 0xfffe, { 0x00, 0x00 }, 0xff }` in `SD drive_magics`, commented "Disable AR6
  fastloader". Returning it everywhere gives every fastloader that probes the same
  answer that makes Action Replay 6 fall back to the KERNAL loader.

**SI-113.** This drive does **not** implement sd2iec's `drive_magics` table, which
returns bytes chosen to put DreamLoad, ULoad Model 3 and Krill's loader into 1541 or
1571 mode. Those values are correct for sd2iec because sd2iec reimplements those
loaders. This drive implements none of them, so a loader that concluded "1541" from a
faked byte would go on to upload drive code with `M-W` and call it with `M-E`, and
then wait for a response that is never coming. Answering `$00` makes the same loader
fall back to the standard serial protocol, which works.

**SI-114.** Identification of this drive is the `UI` string, and its format is fixed
here so that software can rely on it: `73,U64HD ULTIMATE DOS V2.0,00,00`. The
leading token is the device family and the trailing token is the DOS version. A
future revision may raise the version; it may not change the family token, and it may
not introduce a memory signature at `$FEA0` or anywhere else. Software that needs to
recognise this drive matches on the string, which is also what SD README tells
authors to do for sd2iec: "DO NOT use M-R for this purpose. Use the UI command
instead." That C64 OS already copes with this is not an assumption: the boot in TRACE
falls through all four `M-R` probes to `UI` and succeeds.

**SI-115.** The sd2iec `XR` mechanism, which serves a real drive ROM image from a file
for `M-R` so that GEOS and Wheels can identify a drive, is out of scope. Sources:
SD README under `XR` and under GEOS and Wheels; GAP does not ask for it. The
reporter asked on #877 that the documentation state plainly that there is no GEOS and
no Wheels support, which belongs in `GideonZ/1541u-documentation` rather than here.

---

## 12. Real time clock

**SI-120.** `T-RA`, `T-RB`, `T-RD` and `T-RI` read the clock and `T-WA`, `T-WB`,
`T-WD` and `T-WI` set it. The `A` format is
`"dow. mo/da/yr hr:mi:se xM"+CHR$(13)` with the day of week four characters followed
by a space, from `SUN.`, `MON.`, `TUES`, `WED.`, `THUR`, `FRI.`, `SAT.`. The `B` and
`D` formats are nine bytes: day of week, year, month, day, hour in 12-hour form,
minute, second, an AM or PM flag, and `CHR$(13)`, BCD-coded for `B`. The `I` format
is the ISO 8601 subset `"YYYY-MM-DDThh:mm:ss dow"+CHR$(13)`. Sources: HD 9-36 to
9-38; IDE 15.4.8; SD README under `T-R and T-W`; GSD "Realtime Clock".

A write sets the Ultimate's own real time clock, which is the clock every other
interface reads and writes: the drive has no clock of its own. The parser validates the
command, converts it to a calendar date and time, and passes that to
`set_current_time()`, the accessor each real time clock driver defines beside
`get_current_time()`; the control interface's `DOS_CMD_SET_TIME` passes through the same
accessor. A write that is refused, and a write the clock does not accept, answer
`30,SYNTAX ERROR` and leave the clock alone, so an `00, OK` means the clock was set. A
clock driver writes its chip without learning whether the chip took the bytes, so the
drive reads the clock back after the write, and a clock that does not then show the
written moment, within the two seconds a read can come after it, answers `30` as well.

**SI-121.** Each write form carries the fields of the matching read form, at the same
offsets.

| Form | After `T-Wx` | Shortest accepted |
| --- | --- | --- |
| `A` | `dow. mo/da/yr hr:mi:se xM`, the day of week matched on its first two characters, the space and the marker optional | 26 bytes |
| `B` | day of week, year, month, day, hour in 12-hour form, minute, second, PM flag, all BCD | 12 bytes |
| `D` | the same eight fields in binary | 12 bytes |
| `I` | `YYYY-MM-DDThh:mm:ss`, with the day of week a `T-RI` answer ends in accepted and ignored | 23 bytes |

Source: `SD parse_timewrite()`, which is also the source for the rules below.

* The day of week is stored as the `A`, `B` and `D` forms send it, without being checked
  against the date, as a CMD drive stores it. The `I` form carries none and derives it.
* A twelve in a 12-hour hour field is midnight or noon, and the PM flag adds half a day.
  The `A` form without its marker is a 24-hour time.
* A two-digit year below 80 is in this century and from 80 in the last one.
* A `CHR$(13)` inside the binary fields of `B` and `D` is data. Only a terminator after
  the last field of a form is dropped (SI-016).

**SI-122.** A write is refused with `30` when a field is out of range: a month outside 1
to 12, a day outside the length of that month in that year, a day of week above 6, an
hour above 23, a minute or a second above 59, a BCD field whose nibbles are not digits,
or a year outside 1980 to 2079, which is what two BCD digits counted from the epoch hold.
`SD parse_timewrite()` checks the same ranges apart from the length of the month; that
check is added here because a 31 February would otherwise reach the clock chip and be
read back as a different date.

**SI-123.** Every field sits at a fixed offset and every separator is checked. A field
written to another width is refused, where `SD parse_timewrite()` reads a number of any
width and then skips one character. The `A` form's AM or PM marker is at a fixed offset
in that source as well, so a command whose earlier fields are of another width cannot be
read consistently in any case.

---

## 13. Directory listings

### 13.1 Layout

**SI-130.** A listing is a BASIC program: a two-byte load address `$0401`, then per
line a two-byte link pointer, a two-byte line number carrying the block count, the
text, and a zero; the program ends with two zero bytes. Source: `SD dirheader[]`,
`{1, 4, /* BASIC start address */ 1, 1, /* next line pointer */ 0, 0, ...}`.

This is invisible to `LOAD"$",dv`, which is a relocating load, and to `LIST`, which
uses the link only as an end marker, and to a program that reads `$` as a file and
skips two bytes. It is wrong for `LOAD"$",dv,1`, an absolute load, which would place
the listing at `$0101`.

**SI-131.** Byte 4 of the header, the low byte of the header line's BASIC line
number, is the partition number for a file listing and the number of partitions for a
partition listing. See SI-046.

**SI-132.** The file type field is followed by `<` when the entry is locked, then by `H`
when it carries the hidden attribute, and it is preceded by `*` when the entry was not
closed. A line for a hidden entry reads `PRG<H` or `PRG H`. Sources: HD 9-30 for `<`;
`SD createentry()` for all three (the splat is CBM DOS behaviour and is not on HD 9-30);
GSD, "sd2iec marks hidden files with an H after the lock mark, which comes after the file
type. If the file is not locked, a space is left where the lock mark would go." Locking is
SI-076 and the attribute is SI-134.

The splat is the closed bit of a CBM directory entry, bit 7 of its type byte, so it
appears for an entry inside a mounted image and never for a host file, which has no such
bit. `FileInfo::cbm_filetype` carries the bit from the image's file system to the
listing.

**SI-133.** The low byte of the next-line link pointer is `(file size mod 254) + 2`,
so that a program can recover the exact byte length of a file. Sources: SD README,
"If known, the low byte of the next line link pointer of the directory listing will
be set to (filesize MOD 254)+2"; GSD "File Sizes and Blocks Free". It is the only way a
C64 program can learn a file's true size from a listing.

**SI-138.** The last byte of a listing is sent with EOI, whatever the size of the reads
that bring the listing in. A BASIC program that reads a listing one byte at a time with
`GET#` must therefore see the KERNAL status become 64 on that byte and 0 on every byte
before it. Sources: 917, where the same program ended against a CMD HD and an sd2iec and
ran for ever against this drive, reading zeroes with a status of 0; `SD dir_footer()`,
which sets `sendeoi` on the blocks free line.

**SI-139.** A time stamped directory line has a fixed length: 64 bytes in the long format
and 42 bytes in the short format, counting the link pointer, the block count and the zero
that ends the BASIC line. The bytes between the end of the stamp and that zero are `$01`.
The stamp begins four characters behind the three character type of the long format and
two characters behind the single type letter of the short format; the first of the three
characters in the long format is the lock and splat position of SI-132.

Sources: `SD createentry()`, which clears the line to index 63 for `DIR_FMT_CMD_LONG` and
index 41 for `DIR_FMT_CMD_SHORT`, writes the type at `data + 1`, the long stamp at
`data + 7` and the short stamp at `data + 3`, and then fills what is left with `1` up to
the zero. HD 9-22 agrees: measuring the character positions in the scan rather than
reading its extracted text, the type starts 19 columns behind the opening quote of the
name in both formats, the date starts 6 columns behind the start of the type in the long
format and 2 in the short format, and the time starts 11 columns behind the start of the
date in the long format and 6 in the short format. 917 reports the same two properties as
measurements against a CMD HD and an sd2iec: "each directory entry is precisely 64 bytes"
in the long format, and "there are 3 spaces 0x20 characters following the CBM file type
before the start of the date ... Except, on SoftIEC there's only one space. And there are
none of the $01 padding bytes after the time."

### 13.2 Filters

**SI-134.** `LOAD"$[n][path][:pattern[=tp]]"` filters by name and type. `tp` is `P`,
`S`, `U`, `R`, or `B` for a directory. Sources: HD 9-20, "P for program (PRG), S for
sequential (SEQ), U for user (USR), R for relative (REL), and B for subdirectory
branch (DIR)". `D` is accepted as a synonym for `B`, which is an sd2iec extension;
GSD warns that "If filtering a directory programmatically, B should be used for
compatibility with CMD storage devices. D is only supported by sd2iec", and IDE 6.2
in fact maps `D` to DEL. `H` additionally shows hidden files.

`H` is not a type but a flag: a listing leaves out every entry that carries the hidden
attribute unless `H` asks for them, and it sets no type bit, so `$:*=H` lists what `$:*`
lists and the hidden entries as well.

**SI-134a.** A hidden entry is left out of a listing and still answers to its name. Every
command that names an entry finds it: an open, a scratch, a rename, `L`, and the `EH`
that turns the flag back. `SD` reaches the same place from the other side, by passing
`FLAG_HIDDEN` to `first_match()` and `next_match()` in every command that names a file.
Without this a file could be hidden over the bus and never reached again.

C64 OS hides files by a leading dot rather than by an attribute, and the name mapping
escapes a leading dot as `{2E}` (SI-141), so such a name round trips and is not touched
by this requirement.

**SI-135.** `LOAD"$=T..."` produces a time-stamped listing, with options `L`, `N`,
`>stamp` and `<stamp` and the stamp format `MM/DD/YY HH:MM xM`. The long line is
`112 "TESTFILE"       PRG   07/27/19 03.44 PM` and the short line is
`112 "TESTFILE"       P 07/27 03.44 P`. Sources: HD 9-21 and 9-22; GSD "Time and Date
Stamped Directory Listings". The options, the filter and the two stamp formats are
the same in both. What the line they sit in looks like is SI-139. The two lines above are
the manual's examples as text extraction renders them, and that rendering drops spaces: it
puts the type two columns to the left of where a 1541 puts it, and the long format has one
space between the date and the time where three belong. SI-139 gives the column positions, taken from
the character positions in the scan rather than from the extracted text.

**SI-136.** Wildcard matching: `?` matches exactly one character and `*` matches any run
of characters, including none, wherever it stands in the pattern, as a shell glob does.
`*ED` matches `WALKED` and `MOVED` but not `EDIT`, and `W*D` matches `WALKED`. Characters
after a `*` are therefore matched against the end of the name, which is the 1581 rule and
sd2iec's default (`SD match_name_str()` with `POSTMATCH` set, SD README under `X*+/X*-`,
"the default value is enabled (+)"). Character classes such as `[A-Z]` are not supported.
Matching stops after 16 characters. Test: `test_pattern_match` in
`software/io/iec/cbmdos_parser_test.cc`.

The matcher is a full glob, so a second `*` matches in the middle of a name where CBM DOS
and `SD match_name_str()` stop at the first one. GAP notes the difference approvingly:
"SoftIEC even supports more than one * which the other devices do not." For one `*` it
agrees with sd2iec's default, and for more it narrows rather than widens a match, so no
command can act on more files than the other devices would; it is recorded so that it is
a known difference rather than an accident. It runs in time proportional to the product
of the two lengths, which matters because a command such as `S:****************Q` reaches
the matcher from the bus.

### 13.3 Raw directory

**SI-137.** `OPEN lf,dv,sa,"$"` with `sa` not 0 returns the raw directory sectors rather
than the BASIC listing. Sources: `SD load_directory()`, which branches on `secondary != 0`
into `d64_raw_directory()` or a synthesised BAM sector followed by raw 32-byte entries;
IDE 6.3, "To open a raw directory channel, use secondary address 2-14".
**Deliberately unsupported**; `$` on any secondary address returns the listing.

The reason is not the cost of building the sectors, it is what the change would take
away. Every program that opens `$` on a data channel to read the listing byte by byte
uses a secondary address other than 0, because a secondary address of 0 is a LOAD; both
programs the reporter of #917 sent use `OPEN 2,8,0` only because they LOAD nothing else,
and the CMD manual's own examples use 2. Turning those opens into raw sectors would give
every one of them the directory of a medium it cannot read, and on a host file system
there are no directory sectors to give: they would have to be synthesised, entry by
entry, from the same listing the drive already produces. The UCI Software IEC target
opens `$` on whatever channel number its client sends, so the same change would reach
every client of that interface as well.

A program that wants the block map of a disk image has the block commands (SI-090 to
SI-094), which read the real sectors of the image rather than a synthesis of them.

### 13.4 File types inside a disk image

**SI-149.** A file in a mounted CBM disk image is listed with the type its directory entry
carries, and a read of it over the bus delivers the file itself. A GEOS file is an ordinary
entry whose type bits say SEQ, PRG or USR, with the GEOS info block pointer at offset `$15`
and the GEOS file structure at offset `$17` filled in as well, and CBM DOS lists it by the
type bits like any other file. Sources: 917, where a GEOS disk mapped as a partition listed
every file as SEQ, which the reporter confirmed on "all Geos disks" including a Geos 2.0
boot disk converted to D64; the 1541 directory entry layout, which has no GEOS type;
`SD d64ops.c`, whose `d64_readdir()` takes `typeflags` from the entry's type byte and which
has no GEOS or CVT case anywhere in `d64ops.c` or `fatops.c`.

Previous behaviour: `U FileSystemCBM` gives every GEOS entry the extension `CVT`, because
CVT is the interchange format the Ultimate's file browser writes when it copies a GEOS
file out to the host file system, and the browser and the FTP server read the same
directory. `U IecPartition::CreateIecName()` recognised `PRG`, `SEQ`, `REL` and `USR` and
nothing else, so `read_dir_entry()` fell back to SEQ. An open of the same file over the
bus also delivered the CVT stream, which puts a header block in front of the file, rather
than the file itself, and an open of a VLIR file answered nothing at all and logged a
channel fault.

**The read is a regression introduced in 3.15, the listed type is not.** The two halves
have different histories, and the reporter raised the first of them on 917.

* The read worked in 3.14 and 3.14d. `FA_OPEN_FROM_CBM` was added in November 2020 by
  commit `8ee46e83`, "Opening from IEC should not do CVT conversion. Fixed.", which both
  defined the flag and passed it from `IecChannel::open_file()`. The call
  `fm->fopen(partition->GetPath(), fs_filename, flags | FA_OPEN_FROM_CBM, &f)` is present
  at tag `v3.14` and at tag `v3.14d`. Commit `76d887bd`, "Pulled in the iec_compatibility
  branch" of 23 June 2026, rewrote that open and dropped the argument. `76d887bd` is an
  ancestor of `v3.15` and is not an ancestor of `v3.14d`, so every release from 3.15 on
  hands out the CVT container where 3.14d handed out the file. The flag itself was left in
  `fs_errors_flags.h` and still tested by `FileInCBM::open()`, with no caller anywhere in
  the tree.
* The listed type has never matched a CBM drive. At 3.14d `CreateIecName()` copied the
  extension straight into the three type characters for a name that is already in CBM
  form, so a GEOS file listed as `CVT`, which is not a CBM file type at all. 3.15 lists it
  as `SEQ`. Neither is what the entry's type bits say, so this half is a defect of long
  standing whose symptom changed in 3.15, not a regression.

The type and the stream go together: the type alone would offer a `LOAD` a stream it
cannot run, and the stream alone would leave the type contradicting the disk.

* `FileInfo` carries the directory entry's type bits in a new field, `cbm_filetype`, which
  is zero on a file system that has no CBM type. `U DirInCBM::get_entry()` fills it in, and
  `U IecPartition::CreateIecName()` uses it when it is set, in front of the extension
  comparison it did before. The `CVT` extension itself is unchanged, so the file browser,
  the FTP server and the UCI target still see and write `.CVT` files as they did.
* The IEC read open passes `FA_OPEN_FROM_CBM`, which `U FileInCBM::open()` already tested
  and which no caller had ever set. The CVT header branch is skipped and the file's own
  chain is read. A VLIR file's chain is its record block, which is what a 1541 hands over.

The `C` command still copies a GEOS file out of an image as a CVT container, because
`U IecCommandChannel::do_copy()` opens each source with a plain `FA_READ`. That keeps the
interchange format on a copy to the host file system, where it is what the receiving side
needs, and it is the one place where a copy and a read of the same file differ.

Measured against a reference 1541: VICE's `c1541` lists `geos-2.0r-cenbe.d64` as
`prg prg usr usr usr usr usr usr usr`, which is what this drive now lists and is neither
the nine `SEQ` of the previous behaviour nor the nine `PRG` a type-only change would give.
`c1541` extracts `DISK COPY` from `deskpack-plus-b.d64` as 4,335 bytes; this drive now
hands out the same 4,335 bytes. That file has a load address of `$0801` and a `10 SYS(2064)`
line, so it loads and runs from BASIC. It is the only one of the 355 GEOS entries on the
twenty GEOS disks that were scanned for which that is true, so the change is a correctness
fix first and an enabling one only incidentally.

Measured on an Ultimate 64 Elite, with `deskpack-plus-b.d64` mounted as the Software IEC
partition and the same BASIC program run from the C64 on each firmware. The program sends
`CD:DESKPACK.D64`, prints the printable bytes of `$:DISK COPY`, then prints the first eight
bytes an open of `DISK COPY` returns.

| Firmware | Listed type | First eight bytes |
| --- | --- | --- |
| 3.14d (`40a41caa`), FPGA 122, core 1.49 | `CVT` | `1 8 13 8 10 0 158 40` |
| 3.15 as this PR found it (`bc3f2dc4`), FPGA 125, core 1.50 | `SEQ` | `130 0 0 68 73 83 75 32` |
| This change (`79a53425`), FPGA 125, core 1.50 | `PRG` | `1 8 13 8 10 0 158 40` |

`1 8 13 8 10 0 158 40` is `$01 $08 $0D $08 $0A $00 $9E $28`, the load address and first
BASIC line of the file, and is what `c1541` extracts. `130 0 0 68 73 83 75 32` is
`$82 $00 $00` followed by `DISK `, which is the CVT header's copy of the directory entry:
its first two bytes are read as a load address of `$0082`. Each firmware was run from its
own matching bitstream over JTAG, so 3.14d ran on the FPGA core it shipped with.

GEOS itself still cannot start from this drive, because the GEOS speeder is not
implemented; that is out of scope here and belongs in its own issue.


---

## 14. File naming on the host file system

### 14.1 The scheme that already exists

The Ultimate maps a PETSCII name to a host file name by escaping the bytes a host
file system cannot carry, and by appending the CBM file type as an extension. The
design is `doc/filenames_design.txt` and `software/test/iecdrive/doc.md`; the code is
`petscii_to_fat()` and `fat_to_petscii()` in `software/components/pattern.cc`.

That same scheme is sd2iec's **extension mode 5**. It is not a coincidence and not a
convergence: the functions are the same code in both projects, down to the comment
`// '|' > 96 ;)` and the `reserved_names` table. A unified diff of `petscii_to_fat()`
between `U software/components/pattern.cc` and `SD src/fatops.c` shows only whitespace,
the two divergences listed in SI-142, and one restructured assignment;
`fat_to_petscii()` differs only in how `hex2bin()` is called. GideonZ wrote
them here in 2020 (`3ec23bb9`, 4 October 2020); the reporter added them to sd2iec as
mode 5 in 2025 (`0f22587`, 6 June 2025) and documented them in that fork's README in
2026 (`ddb949c`). Upstream sd2iec has modes 0 to 4 only.

**SI-140.** This is a compatibility contract, not an implementation detail. A card
or stick written by an Ultimate must read back on a markusC64 sd2iec in extension
mode 5, and the reverse. Nothing in this specification may change the mapping except
where SI-142 states a difference and its reason.

**SI-141.** The rules, from the shared implementation and SD README:

* A byte below 32, at or above 96, or one of `: / \ " < >`, or a leading `.`, is
  written as two upper case hex digits. A run of such bytes shares one pair of
  braces: `ABC` in shifted PETSCII becomes `{C1C2C3}`.
* A name that matches a reserved DOS name (`CON`, `PRN`, `AUX`, `NUL`, `COM1` to
  `COM9`, `LPT1` to `LPT9`) is prefixed with an empty `{}`.
* A name that would otherwise end in `.prg`, `.seq`, `.usr` or `.rel` gets an empty
  `{}` appended, so that the type extension stays unambiguous.
* A trailing run of shifted spaces (`$A0`) is dropped, because a CBM directory entry
  is padded with them. See SI-147, which is about the state this rule is actually in.
* The CBM file type is the host extension `.prg`, `.seq`, `.usr` or `.rel`, added on
  create and hidden on read.

**Difference from sd2iec: a host name longer than 16 characters.** GSD reads, "Long
filenames (i.e names not within the 8.3 limits) are supported on FAT, but for
compatibility reasons the 8.3 name is used if the long name exceeds 16 characters."
This drive renders the first 16 characters of the long name instead. The 8.3 name is a
property of FAT alone, and `FileInfo` carries one name for every file system the
Ultimate mounts, CBM disk images and FTP among them, so presenting it would mean
plumbing a second name through all of them. The rendered name a listing shows is the
name every command here accepts, because `resolve_existing_iec_path()` matches against
the rendered names of a directory scan (SI-143), so a file is reachable under the name
it shows. What differs is the name the two devices print for the same file.

**SI-142.** `*` and `?` are escaped as SI-141 escapes the other characters, and a
create of a name containing either is refused per SI-032, as `SD a76deb2` does.
Scratch, `RD` and an open by pattern match through a directory scan, so a pattern
still reaches the files it names.

**Difference from sd2iec: the length guard.** sd2iec tests `(i + 4) > maxlen` where
this firmware tests `(i + 4) >= maxlen`, and the two are not the same test because
`maxlen` does not mean the same thing: here it is the size of the buffer, in sd2iec it
is the characters before the terminator. Every name this drive produces is identical
either way, and adopting sd2iec's form without also changing every call site would let
`FileInfo::generate_fat_name()` write one byte past its buffer, so the guard stays as
it is. The `{}` appended to a name ending in `.prg`, `.seq`, `.usr` or `.rel` is added
only when it fits.

**SI-143.** Names that the forward mapping can never produce must still be readable.
`software/test/iecdrive/doc.md` sets this out under "Injectivity vs Accessibility"
and the code implements it with the rendered-name fallback in
`resolve_existing_iec_path()` and `find_rendered_iec_child()`. The
warning in that document stands: the fallback must not be used for `S`, because
scratching by a heuristic match deletes the wrong file.

### 14.2 x00 wrappers

**SI-144.** P00, S00, U00 and R00 files are read as the CBM files they hold: a 26-byte
header beginning with `"C64File"` and a zero, the 16-character CBM name plus a terminator at
offset 8, the record length at offset 25, then the unmodified data. The host
extension is `P00`, `S00`, `U00` or `R00`, the two digits incremented only to break
an 8.3 collision. Sources: `SD src/fatops.c`, `P00_HEADER_SIZE 26`,
`P00_CBMNAME_OFFSET 8`, `P00_RECORDLEN_OFFSET 25`, `p00marker[] = "C64File"`; GFN.

Reading is unconditional on sd2iec, in every extension mode. GFN's argument for it is
worth restating because it is the deciding one: an x00 file preserves the CBM name,
its case and its type exactly; two files may differ by case alone; the name may
contain every character CBM DOS allows; and every emulator and every sd2iec unwraps
it the same way regardless of configuration. It is the only mapping under which a
file moved between an Ultimate, an sd2iec and VICE keeps its identity.

An x00 file lists, opens (with or without a type), positions, appends, copies (the data
without the header, with the type from the header), renames (the name in the header; the
host name is kept) and scratches under the CBM name in its header. A new file of a name
that an x00 file carries answers `63`, and with `@` the x00 file is removed and the new
file written in its place, as sd2iec's `file_open()` does. The UCI `GET_IECNAME` command
reads the header when it is given a full path. The reason for reading these files at all
is GAP's section on file names, which asks the drive to follow how sd2iec stores CBM file
types.

**SI-144a.** The header is read in one place, `software/filetypes/x00_wrapper.cc`, which
the drive, the file browser and the C64 loader all use. The loader needs it because a file
it DMA loads is read from its first byte: the first two bytes are the load address, so an
x00 file whose header is not skipped loads its wrapper to the address the letters `C6`
spell. That is every path that reaches `C64_DMA_LOAD`, `C64_DMA_LOAD_MNT` and
`C64_DMA_LOAD_RAW`: Run, Load and DMA in the file browser, and the control and REST routes
that start a program. The header is skipped only for a file whose name is an x00 name and
whose signature is there, so a `.PRG` file and a `.P00` file that is not a wrapper load as
they are.

**SI-144b.** The file browser treats an x00 file as the CBM file it holds. A `P00` file
offers the actions a `.PRG` file offers, and each acts on the program inside. The row
shows the name from the header, because the host name of such a file is an 8.3 rendering
that does not identify it, while the extension column still says `P00` so that the
wrapper is visible. That name ends at its first shifted space or control byte, as the
name of an entry read from a CBM disk image does, and a header whose name is empty by
that rule leaves the row showing the host name; every operation that names the file on the medium, a rename, a copy
and a delete among them, uses the host name. A browser copy copies the host file whole,
wrapper included, because it copies a file of the medium and the copy is an x00 file of
the same name. `FileManager::fcopy` is a byte copy for every caller, and teaching it to
unwrap would change what the ROM and cartridge installers copy as well. The drive's own
`C` command is the path that writes the CBM file without its header (SI-075, SI-144), so
a copy into a mounted disk image is made from the C64 rather than from the browser. `S00`, `U00` and `R00` files show their
header name in the same way and offer what a `.SEQ`, a `.USR` and a `.REL` file offer,
which is nothing beyond the operations every file has.

Reading these files is only worth having if they can be used from the device itself, so
this is a part of SI-144 rather than an extra: `tests/e2e/filemanager/prg_context_menu_test.py`
drives every context menu action of the real browser against a `P00` wrapper and checks
that the program inside is what runs.

**SI-145.** Writing x00 files is a configuration choice, default off, so that
existing users see no change. When on, it follows sd2iec mode 1 (x00 for SEQ, USR and
REL, plain for PRG) or mode 2 (x00 for everything). Source: SD README under `XEnum`.

**Deliberately unsupported.** It adds a user setting that no report asks for, and
reading x00 files (SI-144) already gives the interchange with files that VICE and sd2iec
write. New files are written plain.

**SI-146.** The x00 wrapper is the whole of the write side answer for relative files.
With it the record length is at a fixed header offset that both devices already
agree on, so a relative file created while SI-145 is enabled is readable by an
sd2iec without either device changing its plain layout. A plain `.rel` keeps this
firmware's two byte layout for ever, which is why SI-084 needs no migration and why
no file a user already has is touched.

**Creating a wrapper is deliberately unsupported**, because SI-145 is not. An existing `R00` file is read and written through its header; a new
relative file is created in the plain two byte layout.

### 14.3 The shifted space

**SI-147.** The shifted space rule is broken in two independent ways, and the second
makes the same file name map to different host names on different Ultimate models.

The code is, in both projects:

```c
while (*pet) {
    char p = *(pet++);
    if (p == 160) {
        const char *q = pet;
        while (*(q++) == 160)
            ;
        if (!*q)
            break;
    }
    ...
```

**First defect, an off-by-one.** After the inner loop, `q` points one past the first
byte that is not `$A0`. "Everything from here to the end is `$A0`" is therefore
`*(q-1) == 0`, not `*q == 0`. As written the test reads one byte beyond the string
terminator, so whether a trailing shifted space is dropped depends on memory outside
the name. Compiled with unsigned `char`, the function gives `AB` for `AB`+`$A0` when
the byte after the terminator is zero and `AB{A0}` when it is not. The same
off-by-one also truncates a name at an *interior* shifted space: `A`+`$A0`+`B` comes
out as `A`, losing everything from the shifted space onwards.

**Second defect, `char` signedness.** `p` is a plain `char`. Where `char` is signed,
`(char)0xA0` is -96 and `p == 160` is never true, so the whole branch is dead code
and every `$A0` is escaped as `{A0}`. Where `char` is unsigned the branch is live.
This is not theoretical:

| Build | `char` | Evidence | Result |
| --- | --- | --- | --- |
| U64, Nios II | signed | `target/u64/nios2/ultimate/output/pattern.lst` contains no constant 160 anywhere in `petscii_to_fat`; the compiler removed the branch | every `$A0` escaped |
| U64 II and U2+L, RISC-V | unsigned | `target/u64ii/riscv/ultimate/output/pattern.lst` contains `li t1,160` | branch live, with both symptoms above |
| host test builds | signed | `-fsigned-char` appears in the flags recorded in `target/pc/linux/iecdrive/output/pattern.lst` | branch dead, so **the host tests cannot see any of this** |
| sd2iec on AVR | unsigned | avr-gcc default | branch live, same as the RISC-V build |

So a file whose name ends in a shifted space gets one host name on a U64 and
another on a U64 II, and the host unit tests agree with neither in the way that
matters.

**Required:** compare on `(uint8_t)p` so the rule is the same on every target, and
test `*(q-1)` so that only a trailing run is dropped and an interior shifted space is
escaped. With both changes the function gives `AB` for `AB`+`$A0`, `A` for
`A`+`$A0`+`$A0`, and `A{A0}B` for `A`+`$A0`+`B`, on every target and regardless of
surrounding memory. The same change belongs in sd2iec, because SI-140 only holds if
both sides agree.

**SI-148.** The policy the fixed rule serves, which the reporter asked to have
settled before anyone changes the code:

1. `$A0` is a legal byte inside a CBM name. It maps to the host as `{A0}` and maps
   back unchanged. Nothing rejects it on read.
2. A **trailing** run of `$A0` is padding, not data, and is dropped before mapping. A
   CBM directory entry is a fixed 16-byte field padded with `$A0`, so a name arriving
   from one carries padding that was never part of the name.
3. A name that is empty, or whose first byte is `$A0`, is refused on create, with
   `33`, or `64` when `@` is given. This is the same branch as the wildcard rejection
   in SI-032: `SD file_open()` tests `(*fname == 160)` alongside `*` and `?`, and
   `SD parse_mkdir()` and `parse_rename()` refuse such a name with `34`.
4. A directory listing ends the name at its terminator or at 16 characters, **not** at
   the first `$A0`. A 1541 puts the closing quote at the first `$A0` because that is
   where its fixed-width field stops carrying name, and reproducing that would make
   this drive and an sd2iec print different names for the same file.
   `SD createentry()` ends at `$22`, `$00` or 16, and this drive does the same.

Together with SI-147 that is the whole of the shifted space question, and it needs
nothing further from anyone.

---

## 15. Limits, and what must not break

### 15.1 Limits

**SI-150.** Summary of the numbers this specification sets, with their sources.

| Limit | Required | Today | Source |
| --- | --- | --- | --- |
| command channel buffer | at least 254 | 64 | HD 4-6, `SD CONFIG_COMMAND_BUFFER_SIZE` |
| OPEN name buffer | at least 254 | 64 | GUG, 232-character paths |
| path components | no fixed limit below the buffer | unbounded | HD 4-6 |
| scratch list elements | limited only by the command length | 8 | SD README under `S:`; HD 9-27 allows five |
| copy source elements | at least 5 | 8 | HD 9-28 |
| partitions | 1 to 255 | 1 to 255 | GUG |
| CBM name | 16 characters | 16 | all |
| path components in one command | at least 16 | 16 | GUG: 232 characters allows about 13 levels of 16-character names |

The 16-component cap is `path.split('/', components, 16)` in
`U resolve_directory_path()`. It is adequate for the path length GUG specifies but
has no margin, and anything over it is silently truncated rather than refused.

### 15.2 Behaviour that must not change

These are stated so that later work does not quietly regress them.

1. `//` is the partition root and a single leading `/` is the current directory
   (SI-010).
2. The binary Change Partition keeps a parameter byte of 13 (SI-017).
3. `B-A` and `B-F` take three parameters (SI-091).
4. `G-P` reports the type in byte 0 and the partition's own name in bytes 3 to 18
   (SI-041).
5. The partition directory prints `NAT`, `41 `, `71 `, `81 ` (SI-048).
6. Block command parameters may be separated by the spaces BASIC prints (SI-020).
7. Relative file behaviour as covered by Suite4 of `software/test/iecdrive`
   (SI-080).
8. The `{HH}` name mapping (SI-140).
9. The two byte record length header of a plain `.rel` file (SI-084). This one is
   newly on the list: an earlier revision of this document proposed changing it, and
   the reason it is here is that changing it would alter the meaning of every
   relative file this firmware has already written.
10. JiffyDOS acceleration, which is implemented in the IEC processor microcode
    `software/io/iec/iec_code.iec` and is what GAP, writing in 2023, reported
    missing.

### 15.3 Consequences elsewhere

**SI-151. Retired.** The number held a requirement to migrate the relative files this
firmware had written, from a two byte record length to a one byte one. A migration over
user data, by a heuristic, to gain an interchange the x00 wrapper already provides, is a
cost with no matching benefit; SI-084 reads both plain layouts instead. The number is
left retired rather than reused, so that a reference to it in an older note resolves
here.

**SI-152.** A log line carries one rendering of the bytes of a command, as text with
every byte that is not printable ASCII written as `\xNN`, which loses nothing. A
rendering that does not fit its 260 character buffer ends in `..`, and the line still
carries the command's real length, so a reader can tell a long command from a cut
rendering of one. `SOFTIEC_LOG_MAX_BYTES` in `software/io/iec/iec_log.h` is how many
command bytes a line renders, and it is bounded by the command buffer of SI-021. The
lines of the setting **Log Every Operation** (section 18) render the directory, the host
path and a reply the same way.

---

## 16. Conflicts between the sources, and how each is decided

Every row is decided. Where the sources disagreed, the row states the primary
evidence that settled it rather than which source was preferred. Nothing here is
left for someone else to answer before the work can start.

| # | Case | What the sources said | Decision and evidence |
| --- | --- | --- | --- |
| C1 | Error code for an unrecognised command | HD B-2 and 1541 say 31. `SD parse_doscommand()` says 30 and reserves 31 for an empty command. The reporter asked for 31. | **31** (SI-031). Settled by ROM `$C175`: the command-table miss loads `#$31`. sd2iec is the outlier, and the reporter's request agrees with the hardware. |
| C2 | `CD/:<-` | `SD do_chdir()` goes to the parent. The reporter expected it to enter a directory named `<-`, while noting he had no reference for the spelling beyond an emulator. | **Parent** (SI-015). One rule covers every measured case: the arrow is the parent in the name position and a literal character in a path component. HD 9-19 says the arrow "cannot be combined with any subdirectory path information", which is the same statement. `CD/<-` remains the way to enter such a directory. |
| C3 | `SAVE"@:foo*"` | The reporter measured `64` on a real drive. `SD file_open()` and IDE 7.1 replace the matched file; sd2iec answers 64 only when nothing matched. | **Settled by ROM `$D8F5`** (SI-032). Save-with-replace compares the found entry's type against the requested type and answers 64 on a mismatch or on a REL. `SAVE` asks for PRG, so a `foo*` that first matches a non-PRG answers 64 and one that matches a PRG replaces it. Every source is consistent once that check is known. |
| C4 | `$=P` footer | Issue #890 and `SD pdir_refill()` say no footer. IDE prints `n PARTITIONS.`. | **No footer** (SI-045). The issue is explicit, sd2iec agrees, and IDE64's footer is its own extension. |
| C5 | `$=P:*=C` | HD 9-14 says `C` selects 1581 CP/M. `SD load_directory()` maps `C` to internal type 12, which is `80 `, an 8050 image. | Accept `C` and match nothing, because this drive has neither kind of partition. Recorded so the sd2iec mapping is not copied by mistake. |
| C6 | `=D` directory filter | `SD` and this firmware treat `D` as DIR. IDE 6.2 maps `D` to DEL. GSD warns that on other drives `D` matches everything. | `D` is a synonym for `B` (SI-134), and say in the user documentation that software should send `B`. Changing it would break the sd2iec software that already sends `D`, and no software can be relying on `D` meaning DEL here because this drive has no DEL entries. |
| C7 | Wildcards with more than one `*` | `SD match_name_str()` and CBM DOS stop at the first `*`. This firmware backtracks. GAP calls the difference harmless. | Backtracking stands (SI-136). For one `*` it agrees with sd2iec's default; for more it narrows rather than widens a match, so no command can act on more files than the other devices would. |
| C8 | G-P byte 1 | HD and RL say reserved zero. FD defines a disk-information bit field and `SD` writes `0xE2`, which decodes as an FD-2000 with a 1.6 MB disk. | **Zero** (SI-042). Byte 1 is a claim about the device model, and this drive is not an FD. |
| C9 | G-P block unit | HD and FD count 512-byte blocks; RL counts 256-byte blocks. | **512** (SI-041), following the HD, which is the reference text and the larger of the two devices this drive resembles. |
| C10 | What `M-R` should answer | Nothing documents what C64 OS concludes from each answer. SD README says not to use `M-R` for detection at all. The reporter proposed a constant 42. | **The requested count of `$00` bytes at every address, and no magic table** (SI-112, SI-113). `$00` matches no model signature, and it is the value sd2iec deliberately returns at `$FFFE` to make Action Replay 6 fall back to the KERNAL loader. Faking a 1541 signature would invite a loader to upload drive code this drive cannot run. Identification is the `UI` string, whose format SI-114 fixes. |
| C11 | Whether `UJ` and `U`+shifted J lock the bus | GAP reports it against firmware 3.10a. Nothing since has tested it. | **Settled from the code, not by re-measuring** (SI-103). `IecDrive::reset()` reaches `IecInterface::configure()`, which sets `HW_IEC_RESET_ENABLE = 0` and holds the IEC processor in reset while it rewrites the slots. Command handlers run on the IEC task inside the bus state machine with the host still addressed, so that call is a bus-lock by construction. The requirement is that no reset command touches the interface, which removes the mechanism whether or not 3.10a's symptom survives today. |
| C12 | Shifted space (`$A0`) inside a name | The reporter wrote that this "is a topic of its own, but I do not want to start that topic without having discussed that first". | **Decided** (SI-147 for the two defects, SI-148 for the policy): `$A0` is legal inside a name and maps to `{A0}`; a trailing run is padding and is dropped; a name that is empty or starts with `$A0` is refused on create; a listing ends the name at its terminator or at 16 characters rather than at the first `$A0`, because ending it earlier would make this drive and an sd2iec print different names for the same file. |
| C13 | GAP's report that copy produces `kernal.bin.bin` and ignores paths | Measured against firmware 3.10a. | **Not reproducible.** Measured with a probe linked against the host suite's objects: the target gets one extension and a target path is honoured (SI-075). The requirement is a regression test. |
| C14 | `R` across directories | IDE 15.2.3 renames or moves a file between directories. `SD parse_rename()` answers `62,FILE NOT FOUND` when the two paths differ. HD 9-26 says the two names must be in the same partition. | **The move wins** (SI-074). Precedence rule 3 would put SD over IDE, but the drive already moved the entry, a test asserts it, and no program has been named that needs the `62`; taking the move away would break working programs to satisfy a rule of precedence. |
| C15 | The two identification bytes in a listing header | 917 reports that this drive prints `00 2a` where a CMD HD prints `hd 1h`, and calls the difference harmless. `SD dirheader[]` prints the disk id `IK` and the DOS version `2A`. | **Keep `00 2a`.** The field is a claim about the device: `1H` is the CMD HD's own DOS version, and a drive that answers `UI` with its own identification string (SI-114) should not print a CMD HD's version in its listings. sd2iec prints a made up id and `2A` for the same reason, so the shape of the current answer is the one an sd2iec-like device gives. |
| C16 | The low byte of a line's link pointer | 917 reports that a CMD HD prints `01 01` on every line and that this drive prints something else. SD README defines the low byte as `(filesize MOD 254)+2`, and GSD documents it. | **Keep the remainder** (SI-133). It is what sd2iec does, its author documented it, and it is the only way a program can learn a file's exact length from a listing. A program that reads the link as an end-of-program marker is unaffected, because the byte is never 0 and the high byte stays 1. |


## 17. Tests

Every requirement gets a test at the cheapest layer that can fail for it.

**T1. Parser, `target/pc/linux/parse`.** Command recognition, parameter splitting,
error codes, path and name grammar, the terminator rules of SI-016 to SI-019, the
name mapping of SI-141 and SI-142. These run in well under a second and need no file
system.

**T2. Drive, `target/pc/linux/iecdrive`.** Everything that needs a file system: the
listing layouts of section 13, the partition directory of section 5.3, file and
directory commands, relative files, direct access, the x00 wrapper. The existing
suites 3 to 10 are the model, and Suite10 already covers the #875 to #877 work.

Suite11 holds one case per requirement, named after the paragraph it checks, and each
case sets up a partition of its own so that it can be run alone:
`./result/testdrive SI031` runs the Suite11 cases whose name contains SI031 and skips
every other suite. That is how a requirement is shown to fail before its change and to
pass after it. The listing layout of SI-138 and SI-139 needs a reader that addresses the
channel to talk for every byte, because a whole stream read cannot fail for either of
them. For the requirements section 18.1 marks deliberately unsupported, the tests assert
the answer given there, so that a later implementation has to change a test on purpose.

`target/pc/linux/iecdrive_unsigned` builds the same suite with `-funsigned-char`, because
the U64 II and U2+L are RISC-V, where `char` is unsigned, and SI-147 depended on it.
`target/pc/linux/iecdrive_asan` builds it under AddressSanitizer; run it with
`ASAN_OPTIONS=detect_leaks=0`, because the suite does not free what it set up at exit.

**T3. Hardware, `tests/e2e/io/iec`.** Only what a host build cannot reach. Two
classes qualify and both have already caught defects:

* Anything that goes through the firmware's own `sscanf` in
  `software/system/small_printf.cc` rather than the C library's. That implementation
  supports only `%d`, `%x`, `%c` and `%s`, has no field width, no conversion
  suppression and no length modifiers, and its `%s` writes an unbounded word. A host
  build links glibc and will not see the difference. The time-stamped listing filter
  of SI-135 is parsed with it, and that is how the `30,SYNTAX ERROR` recorded in
  `tests/e2e/io/iec/README.md` reached a device that a host build said was fine.
* Anything whose input is the byte sequence a Commodore actually puts on the bus,
  which is what `iec_agent.asm` produces. The #875 to #877 defects were all of this
  kind: a host test that builds its command strings by hand cannot fail for them.
* Anything whose behaviour depends on the signedness of `char`, where the second host
  build above does not settle it. The U64 build is Nios II, where `char` is signed, and
  the U64 II and U2+L builds are RISC-V, where it is not. SI-147 is the known case.

**T4. The file browser and the run routes, `tests/e2e/filemanager` and
`tests/e2e/api`.** SI-144b is about what a person sees and does on the device, so it is
checked there: `prg_context_menu_test.py` drives every context menu action of the real
browser against a plain `PRG`, a `P00` wrapper and a `PRG` inside a `D64`, and
`prg_load_path_trim_test.py` runs a wrapper through the REST routes that start a
program and reads the name the boot cart prints.

**T5. The C64 OS acceptance test.** Install C64 OS on a Software IEC partition, boot
it, and compare the `SoftIEC:` lines of the resulting log against TRACE. The drive
logs a line only for a failed command, a failed open and a channel fault, so the
criterion is that no such line carries an error code that a CMD HD would not also
produce. TRACE gives
the baseline: 68 commands, 107 opens, 108 closes and 80 status reads in one boot, and
of its 601 lines 11 carry `33,SYNTAX ERROR` and 9 carry `62,FILE NOT FOUND`.

---

## 18. The operation log

The drive writes two kinds of `SoftIEC:` line to the firmware's log. It always writes one
for a command that leaves an error, an open that fails and the first failure of a channel. With the setting **Log
Every Operation** in the SoftIEC Drive Settings, which is off by default, it also writes
one for every other command, open and close. Such a line adds the reply of a command
that answers with data, such as `M-R`, the host file an open reached or the first 32
bytes of a listing, and a listing writes its last line as well. Every line carries the
bytes the host sent, the error channel's answer, and the current partition and its
directory as they are once the operation has run, so the line of a `CD` shows the
directory it entered. Every line ends in ` #` and a sequence number that counts the lines
the drive has written, so a reader can tell a line lost or delivered twice on the way to
the log from one the drive wrote twice. With the setting off, a successful command costs one read of the setting and
writes nothing. The setting exists so that a trace like TRACE, of what a program sends
and what the drive answers, can be recorded with a release build.

Each part of a line is rendered into a fixed buffer of 260 characters that the rendering
never passes, and the lines are written while the drive lock is held.
`Suite11-OperationLogBounds` sends the longest command, name, reply, directory and host
path the drive accepts with the setting on, and the AddressSanitizer build fails on a
byte outside those buffers. The serial port takes each line one character at a time, so
with the setting on an operation also takes as long as its line needs to be sent. The
firmware's `printf` does not keep a line together, so a message another task prints at
the same moment can split a `SoftIEC:` line in two, and the rest of the line then
arrives with the next syslog message. With the firmware also printing its REST and FTP
requests, this happened to 1 line of 732 during a Software IEC soak on an Ultimate 64
Elite, and to 15 lines of 668 during `iec-dos-commands` and a soak on a U2+L.

### 18.1 Deliberately unsupported, and the differences from the sources

Everything in sections 2 to 15 is in force except the five commands in the first table.
Each of them carries the reason below the requirement itself, and a test asserts the
answer given here, so a later implementation has to change a test on purpose.

| Requirement | Why it is not implemented | What the drive answers |
| --- | --- | --- |
| SI-054 `V` | Validating means rebuilding the block map of an image from every directory, side sector chain and GEOS record chain in it, and a walk that misses one marks live blocks free; an OK without the walk would claim a check that did not happen | `31`, as sd2iec answers |
| SI-077 `EL:$`, `EU:$` | Write-protecting a whole mounted image changes the writability of a file system the file browser, FTP and REST share; the per-entry lock covers what the command is used for | `62`, the name matching no entry |
| SI-105 `M-W`, `M-E` | Nothing written is kept and nothing is run, so an OK would tell a fast loader its drive code runs | `30` |
| SI-137 raw directory | Every program that reads a listing byte by byte opens `$` on a data channel, and the UCI target opens it on whatever channel its client sends; on a host file system the sectors would have to be synthesised from the listing in any case | the listing |
| SI-145 writing x00 files | A user setting that no report asks for; reading them (SI-144) already gives the interchange | new files are written plain |

Section 19 lists what is out of scope: the commands that run 6502 code in drive memory,
the hardware a CMD device has and this one does not, and the sd2iec settings commands.

**Differences from the sources.** Each of these is a requirement in force that answers
differently from one of its sources, for a reason given below the requirement.

| Requirement | The difference |
| --- | --- |
| SI-016 | A carriage return second to last ends a command only when a line feed follows it, because the ROM's branch cuts a binary parameter of 13 short |
| SI-018 | The position in a plain file is read from the command without its terminator, where sd2iec reads it from the command as sent |
| SI-033 | A scratch whose path does not exist answers `71` rather than a count of zero |
| SI-074 | A rename into another directory or partition moves the entry, where `SD parse_rename()` answers `62` |
| SI-077 | `EL`, `EU` and `A` act on every entry a name matches, directories included, where sd2iec skips directories and `A` takes the first match |
| SI-120 | A write is refused when the day is not a day of that month, which `SD parse_timewrite()` does not check, and every field is read at its documented width |
| SI-136 | A second `*` matches in the middle of a name, where CBM DOS and sd2iec stop at the first |
| SI-141 | A host name longer than 16 characters renders as its first 16 characters, where sd2iec prints the 8.3 name |
| SI-142 | The length guard of `SD` is not adopted, because it would change no host name the drive produces |

---

## 19. Out of scope

Named so that the boundary is explicit rather than implied.

* Burst commands and the C128 fast serial protocol (HD 9-52 to 9-64).
* The job queue (HD 9-65 to 9-67), block execute (SI-095) and user jumps (SI-104):
  all of them run 6502 code in drive memory.
* `S-C`, the SCSI pass-through (SI-106).
* 1581-style sub-partitions (SI-055).
* The sd2iec direct sector commands `DI`, `DR`, `DW` (SI-096).
* Serving a drive ROM image for `M-R` so that GEOS and Wheels identify a drive
  (SI-115). GEOS and Wheels support needs gateware work and is a separate project;
  the reporter asked that the documentation say so.
* Swap lists, `XS` and the disk change buttons: they are a user interface feature of
  a device with physical buttons, and `U cbmdos_parser.cc` already records the
  reasoning, "Swaplists are not part of the drive, they are part of the user
  interface."
* The fastloaders other than JiffyDOS. sd2iec accelerates Turbo Disk, Final
  Cartridge III, ULoad Model 3, GEOS, Wheels, Action Replay 6, Epyx Fastload,
  Burst Loaders and Dreamload by detecting the loader's upload and answering in its
  protocol (SD README, "Fastloaders"). Each one is a protocol in the IEC processor
  microcode `software/io/iec/iec_code.iec`, not firmware, so each is its own piece
  of work. JiffyDOS is implemented there and is item 10 of section 15.2.
* Creating a partition. sd2iec's partitions are the primary partitions of the
  medium's MBR and it does not create them either (SD README, "Partitions"). The
  partitions of this drive are the entries of the **Software IEC** configuration,
  which the user edits in the Ultimate menu, and the command channel selects one
  with `CP` (SI-016) and reads them with `$=P` (SI-047).
* sd2iec's EEPROM file system, the small partition it exposes from the spare space of
  the microcontroller's own EEPROM (SD README, "EEPROM file system"). It exists
  because that hardware has an EEPROM larger than its configuration needs. A
  partition of this drive is a directory of the Ultimate file system, and the
  Ultimate's own flash is reachable as one of those. The `!` partition alias goes with
  it: `CP!:`, `$!` and `!:NAME` address the EEPROM partition wherever it ended up, and
  with no such partition there is nothing for the alias to name.
* M2I files, which sd2iec itself has deprecated (SD README, Deprecation notices).
* Mapping each Ultimate storage device to its own IEC device number, which GAP asks
  for. The partition model in section 2 answers the same need within one device
  number, and C64 OS supports at most five devices at once against 255 partitions.
* Commands that appear in the reference documents and are not specified above. They
  are listed so that the omission is deliberate. None of them gets a requirement here;
  where the command's first letter is a command letter, the general rule of SI-030
  applies and the answer is `30`, and where it is not, SI-031 applies and the answer
  is `31`.
  * HD Appendix J: `BOOT` (a BASIC 7.0 statement, not a command-channel command);
    the `U0` utility forms other than `U0>`+`CHR$(d)`, that is `U0`, `U0+`, `U0-`,
    `U0>B0`, `U0>B1`, `U0>R`, `U0>S`, `U0>T`, `U0>V0`, `U0>V1`, `U0>M0`, `U0>M1`,
    `U0>MR` and `U0>MW` (HD 9-49), which select serial timing, retries, interleave
    and 1571 modes that this drive does not have. The 1541 ROM's command table at
    `$FE89` also carries `&` (execute utility file), which HD does not list.
  * IDE section 15: 15.2.5 hide, whose IDE64 spelling is its own; the sd2iec
    spelling `EH` is in force under SI-077; 15.4.2 get disk change; 15.4.5, 15.4.6 and 15.4.7 (`U0>P`, `U0>E`,
    `U0>L`, power, eject and medium lock); 15.4.9 format disk; 15.5.1 direct access
    identify and the LBA forms of buffer read and write; 15.6.2 change root
    directory; 15.7 CD-ROM commands. All of them address IDE64 hardware or its CFS
    file system.
  * SD README: the settings commands `X`, `XE+`/`XE-`, `XEL`/`XEU`, `XET`, `XI`,
    `XN`, `XH+`/`XH-`, `XD`, `XW`, and `XL`/`XU`, which the README names in a heading
    and does not describe. This drive keeps its settings in the Ultimate
    configuration; SI-145 is the one sd2iec setting (`XE`) that gets a counterpart.
    `XS` and `XR` are listed above.
* The firmware hang the reporter saw three times on #877 while starting C64 OS, where
  "the ultimate becomes unresponsive it's completely dead, just the c64 is still
  running. But not the soft iec. The menu button is dead." Nothing in this
  specification addresses it, because nothing here explains it: the reported sequence
  is power on, enable Software IEC, add a partition, `@"cp2"`, load the directory,
  run C64 OS, and the successful run of exactly that sequence is TRACE. It is a
  separate investigation and needs its own issue. What would move it forward is a
  syslog capture that ends at the hang rather than one from a successful boot, and
  whether REST and ping still answer while the menu button does not. The IEC task's
  stack was measured after a soak on a U2+L, with 3,352 of 6,400 bytes left, and the soak
  of the drive over the real bus, `tests/soak/io/iec/softiec_soak_test.py`, has not
  reproduced the hang.

---

## Appendix A. What C64 OS sends

From TRACE, one successful boot: 68 commands, 107 opens, 208 data addressings, 108
closes, 80 status reads. Eighteen distinct command strings, eighty-four distinct open
names, longest open name 26 bytes.

| Command | Count | Answer today |
| --- | --- | --- |
| `C`+`$D0`+`CHR$(2)` | 29 | `02,PARTITION SELECTED` |
| `CD//OS/DESKTOP/1/` | 11 | `00, OK` |
| `CD//OS` | 6 (plus one with a terminator) | `00, OK` |
| `CD//OS/SERVICES/{$C1}PP {$CC}AUNCHER/` | 6 | `00, OK` |
| `CD//OS/DRIVERS/` | 3 | `00, OK` |
| `CD//OS/SETTINGS/`, `CD//OS/DESKTOP/`, `CD//OS/CHARSETS/` | 1 each | `00, OK` |
| `CP2`, `CP2`+CR | 1 each | `02,PARTITION SELECTED` |
| `M-R` at `$FEA4`, `$E5C5`, `$A6E8`, `$0002`, 2 bytes each | 1 each | `33,SYNTAX ERROR` |
| `UI` | 1 | `73,U64HD ULTIMATE DOS V2.0` |
| `S/TEMPORARY/:*` | 1 | `62,FILE NOT FOUND` |
| `CHR$(0)` | 1 | `33,SYNTAX ERROR` |

Open names are of the forms `NAME`, `:NAME`, `/PATH/:NAME`, `$` and `$:PATTERN`.
Data channels used are 0, 2, 3 and 14. Names carry shifted PETSCII and spaces, for
example `:{$C1}BOUT {$D4}HIS {$C1}PP`.

So the whole of a C64 OS boot needs: the binary Change Partition, `CD` with an
absolute path, `M-R`, `UI`, `S` with a path, a directory read with and without a
pattern, and file opens with a path. Everything else in this specification is for the
other software that already targets these devices.

## Appendix B. Requirement index

Sections 2 to 15 define the numbered paragraphs SI-001 to SI-152, with gaps, one of which
(SI-151) is retired. A paragraph whose number carries a letter, such as SI-103a, states a
further rule of the requirement it follows and is numbered that way so that the numbers
already cited elsewhere keep their meaning.

Section 18.1 is the index of the five deliberately unsupported requirements and of the
nine that are in force and answer differently from one of their sources. Everything else
in sections 2 to 15 is in force as written. Section 19 is what is out of scope, which is
a different thing: those are capabilities this drive does not have rather than commands
it declines to implement.

## Appendix C. The programs issue #917 reports

Both programs were reported as photographs of a C64 screen. They are transcribed here so
that they can be run and turned into tests without reading the pictures again.

The first program copies a time stamped listing byte by byte onto a second device. Greg
Nacu ran it with the listing on a Software IEC partition and the output file on an
sd2iec, and it never ended.

```basic
10 OPEN2,8,2,"@//:DIRDATA.D,S,W"
20 OPEN3,8,0,"$=T:*"
30 GET#3,A$:S1=ST:PRINT#2,A$;
40 IFS1=0THEN30
50 CLOSE2:CLOSE3
```

Line 30 saves the status of the read in `S1` before the write to the other device
overwrites `ST`. Line 40 loops while that status is 0, so the program ends on the first
read whose status is not 0, which on a CMD HD and on an sd2iec is the 64 of the last byte.

The second program is the reduced case, with no second device involved. It prints the
value of each byte and the status that came with it.

```basic
10 OPEN2,8,0,"$=T:*"
20 GET#2,A$:A$=A$+CHR$(0):S1=ST:PRINTASC(A$),S1
30 IFS1=0THEN20
40 CLOSE2
```

The photograph of its output ends with a line reading `0` and `64`: the last byte of the
listing is a zero and it arrives with end of file set. Against this drive the same program
printed `0` and `0` without stopping. SI-138 is the requirement, and the screen above the
first program also shows the directory it was reading, whose header is the CMD HD's
`1 "TESTS            " HD 1H` (C15).

## Appendix D. Conformance against the two C64 OS reference articles

The two articles GAP and GSD are the reasons this specification exists, and they are
prose rather than lists, so this appendix maps each thing they raise onto the
requirement that settles it. A reader checking whether an article is answered reads a
row here and then the requirement it names. Nothing in either article is left without a
row.

### D.1 GAP, "Gaps in Software IEC"

| What the article raises | Settled by |
| --- | --- |
| Storage devices should each get their own IEC device number | Section 19: the partition model of section 2 answers the same need inside one device number |
| The pseudo root holds no files and its blocks free differs from the directories below it | SI-005, SI-003, SI-004 |
| A device entry of the pseudo root cannot be renamed and answers `69,FILESYSTEM ERROR` | SI-005, SI-074, SI-036 |
| The digit in front of the header name is 0 while the listing holds `DIR` entries | SI-131, SI-046 |
| Path structure: `//` for the root, a trailing slash on every component, a colon before the name | SI-010, SI-011 |
| A single leading slash must mean the current directory | SI-010 |
| `CD:AUDIO/TUNEFUL 8` descends two levels | SI-011a |
| The left arrow must mean the parent directory, not a configured default | SI-014, SI-015 |
| `..` as a path component, and a directory actually named `..` | SI-014, whose closing paragraph gives `CD/../OTHERDIR` as the way up and down again in one command |
| `C` with a relative path in the source does nothing | SI-075 |
| `C` writes the copy into the current directory and appends the host extension a second time | SI-075, C13 |
| `C` with an absolute path in the target does nothing | SI-075 |
| Filtering a listing by CBM file type, in both the `=P` and the `=B`/`=D` spellings | SI-134 |
| The directory header shows a fragment of the path instead of the directory's name | SI-065, SI-064 |
| `$=P`, the partition directory, and its type filter | SI-044 to SI-050 |
| `CP`n and `C`+shifted P select the current partition | SI-040, SI-016, SI-017 |
| The partition number in front of a path, in `C`, `S` and `R` | SI-013 |
| Partitions rendered as `PART0`, `PART1` directory entries with no free space | SI-002, SI-005 |
| Partition names are not used | SI-002, SI-041, SI-049 |
| PETSCII names are unreadable once the medium is read on a PC | SI-140 to SI-148 |
| The RTC commands over IEC | SI-120 to SI-123 |
| Time stamped listings and their filters | SI-135, SI-139 |
| Seeking within a file with `P` | SI-081, SI-082, SI-083 |
| `U0>`+`CHR$(d)` changes the device number | SI-100 |
| `UJ` locks up the bus | SI-103, SI-103a |
| Direct access: buffers, the buffer pointer, block read and write | SI-090 to SI-094 |
| JiffyDOS acceleration | Section 15.2 item 10; the other fastloaders are in section 19 |

### D.2 GSD, "SD2IEC User's Manual"

| Section of the manual | Settled by |
| --- | --- |
| Files: long filenames, and the 8.3 name when a long one exceeds 16 characters | SI-140 to SI-143; the 8.3 fallback is a stated difference under SI-141 |
| Files: x00 wrappers, the header, the extension family, the internal name in a listing, renaming the internal name | SI-144, SI-144a, SI-144b, SI-145, SI-146 |
| Files: relative files, and the record length of a plain one | SI-080, SI-084, SI-146 |
| Files: positioning (seeking) within a file with `P` | SI-081, SI-082, SI-083 |
| Files: M2I | Section 19; the manual deprecates the format |
| Files: loading, saving, verifying, pattern matching | SI-032, SI-070, SI-136; `V` as a verify is BASIC's, not a command |
| Files: renaming files and subdirectories | SI-074 |
| Files: copying and combining between partitions | SI-075, SI-013 |
| Files: locking and unlocking | SI-076, SI-077, SI-132 |
| Files: the file allocation table, `B-A` and `B-F` | SI-091, SI-091a |
| Directories: loading a directory, sizes, blocks free | SI-130, SI-133, SI-004 |
| Directories: pattern matching and the type filters | SI-134, SI-136 |
| Directories: time and date stamped listings and their filters | SI-135, SI-139 |
| Directories: `MD`, `CD`, `RD`, and `CD` into a disk image | SI-060, SI-061, SI-062, SI-063 |
| Partitions: the partition model, the default partition, partition numbers in names and in commands | SI-002, SI-013 |
| Partitions: disk images, mounting and unmounting | SI-003, SI-062, SI-072 |
| Partitions: creating and deleting partitions in the MBR | Section 19 |
| Partitions: the EEPROM file system | Section 19 |
| Partitions: changing partitions, `CP` and `C`+shifted P | SI-040 |
| Partitions: formatting | SI-052, SI-071, SI-071a |
| Partitions: the partition directory | SI-044 to SI-050 |
| Partitions: renaming partitions and partition headers | SI-051, SI-064 |
| Partitions: swap lists and the disk change buttons | Section 19 |
| Device management: firmware update, hot swapping, card detection, sleep mode | Section 19; all four address sd2iec hardware |
| Device management: device detection and the `UI` identifier | SI-110, SI-111, SI-113, SI-114 |
| Device management: warm, cold and hard reset | SI-103, SI-103a |
| Device management: memory access, `M-R`, `M-W`, `M-E` | SI-105, SI-112, SI-115 |
| Device management: user commands `U1` to `UJ` | SI-091, SI-103, SI-104 |
| Device management: the device address, `U0>` and `S-8`/`S-9`/`S-D` | SI-100, SI-101 |
| Device management: the bus protocol setting | Section 19, with the settings commands |
| Direct access: buffers and large buffers | SI-090 |
| Direct access: reading and writing data, `B-R`, `B-W`, `U1`, `U2` | SI-093, SI-094 |
| Direct access: the buffer pointer | SI-092 |
| Direct access: block execute | SI-095 |
| Direct access: `DI`, `DR`, `DW`, the direct sector commands | SI-096 |
| Realtime clock: the four read and four write forms, ASCII, BCD, decimal and ISO | SI-120, SI-121, SI-122, SI-123 |
| Settings: the `X` family | Section 19, except `XE`, which is SI-145 |
| Software fastloaders | Section 15.2 item 10 for JiffyDOS; section 19 for the others |
| Write protect, `26,WRITE PROTECT ON` | SI-102, SI-102a |
