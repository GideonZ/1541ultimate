# Software IEC: CMD DOS, sd2iec and C64 OS compatibility

**Status: specification.** This describes what is to be built. It is not a
description of what the firmware does today, except where it cites current code by
file and symbol, and every such citation is labelled as current behaviour. A reader
who wants to know what the firmware does now should read
[software/test/iecdrive/doc.md](../software/test/iecdrive/doc.md),
[doc/filenames_design.txt](filenames_design.txt) and
[tests/e2e/io/iec/README.md](../tests/e2e/io/iec/README.md).

The goal is that an Ultimate 64 or Ultimate II+ can be the system drive of C64 OS,
and that in doing so it also becomes a closer replacement for an sd2iec, a CMD HD, a
CMD FD, a CMD RAMLink and an IDE64 for every other program that already targets
those. Issues [#877](https://github.com/GideonZ/1541ultimate/issues/877) and
[#890](https://github.com/GideonZ/1541ultimate/issues/890) are the immediate
drivers; this document covers the whole command surface those issues sit in, so the
work can be done once rather than one report at a time.

This is not a greenfield design. The Software IEC drive has shipped for years to a
large install base, and several of the behaviours below are already correct. Every
requirement therefore states the source of truth, what the firmware does now, and
whether anything has to change. Requirements that change nothing are kept because
they are the contract that later work must not break.

Where a statement about current behaviour says "measured", it was obtained by running
the command against the head of PR #881, using a probe linked against the same object
files `target/pc/linux/iecdrive` links. The rest were read from the source, and each
names the file and symbol so a reader can check it.

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
| **TRACE** | `log_boot.log.txt`, attached by the reporter to #877 on 11 September 2026. 601 lines of `SOFTIEC-TRACE` output from one successful C64 OS boot on a U64 II. |
| **U** | This firmware at PR #881 head `16f7e31b`. Paths are relative to the repository root. |

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
* **A3.** No behaviour marked "Unchanged" in section 15.2 has changed.

---

## 2. The model

### 2.1 Device, partitions, directories

**SI-001.** The drive presents one IEC device number, configurable from 8 to 30.
Current behaviour: `U software/io/iec/iec_drive.cc`, `CFG_IEC_BUS_ID`, range 8..30,
default 11. Unchanged. GUG confirms C64 OS expects storage devices in 8..30 and
supports up to five at a time.

**SI-002.** The drive presents partitions numbered 1 to 255. Each partition has a
name, a root directory on the host virtual file system, and its own current
directory. Partition 0 is not a partition a user can enter; as a parameter it means
"the current partition". Sources: HD 9-8 and 9-10 (1 to 254 on the HD, 0 means the
current partition), FD (1 to 31), RL (1 to 31), IDE 15.3.1 ("Partition 0 is not a
valid parameter"), GUG ("CMD devices support 255 partitions, numbered from 1 to 255.
Partition 0 is a special system partition that cannot be accessed directly").
Current behaviour: `U iec_channel.h`, `is_valid_partition_number()` accepts 1..255
and `IecFileSystem::GetTargetPartitionNumber()` maps any index below 1 to the
current partition. Unchanged.

**SI-003.** A partition's root may be a directory on the host file system or a
mounted CBM disk image, because the Ultimate virtual file system mounts `.d64`,
`.d71`, `.d81` and `.dnp` images as directories. Unchanged.

**SI-004.** The blocks free reported for any directory is the free space of the
partition, not of the directory. Source: HD 4-5, "all of the blocks within a Native
Mode partition are shared between all directories within that partition".
Current behaviour: `U IecChannel::setup_directory_read()` calls `fm->get_free()` on
the listed directory's own path. Unchanged in effect, because the host file system
reports per-volume free space.

### 2.2 What the partition model must not do

**SI-005.** The root of a partition is a real directory. Files can be created in it,
it can be listed, and its blocks free is that of the partition. GAP's principal
objection was that the drive presented its storage devices as directory entries of a
pseudo root that could hold no files. Current behaviour: a partition root is an
ordinary host directory, so this holds. Unchanged, and stated so that it is not
regressed.

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

Current behaviour: `U iec_path_to_fs_path()` in `software/io/iec/iec_channel.cc`
strips a single leading slash, rewrites `_` to `..`, then collapses `//` to `/`,
which produces exactly this. GAP's complaint that a single slash meant the root is
out of date. Unchanged.

**SI-011.** The colon is optional when the name is preceded by a slash. Source:
HD 9-18, "It is not required that you include the colon before the subdirectory
name, as long as the subdirectory name is preceded by a slash." Unchanged.

**SI-012.** A path component may contain wildcards, and the first match is used.
Sources: SD README under CD/MD/RD, "You can use wildcards anywhere in the path";
IDE 6.2, "These wildcards can be used in path elements or filename too, in this case
the first filename will be matched." Current behaviour: `U resolve_directory_path()`
calls `find_rendered_iec_child()` with `allow_wildcards` false for every component.
**Change required.** `.` and `..` as path components already work there and are a
deliberate extension beyond the CMD devices; GAP calls them out as "something that
none of the other devices can do".

**SI-013.** The partition number may replace the drive number in any command that
accepts one, except the direct access commands. A direct access channel is bound to
the partition that was current when the channel was opened, and the partition
parameter of `B-R`, `B-W`, `U1`, `U2`, `B-A`, `B-F` and `B-E` is therefore always
written as 0. Source: HD 9-8 and 9-44; IDE 15.5; GSD "Partition Numbers in Disk
Commands". **Change required:** the current implementation resolves the partition
parameter of the block commands against the partition table
(`U IecCommandChannel::do_block_read()` and its siblings call `GETPARTITION(part,...)`),
which means a program that correctly sends 0 gets the current partition only by
accident of `GetTargetPartitionNumber()`, and a program that sends a real number
addresses a partition the channel was not opened on.

### 3.2 The left arrow

**SI-014.** PETSCII `$5F`, the left arrow, means the parent directory when it stands
in the *name* position, that is directly after `CD[n]` or directly after a colon. It
is an ordinary character when it stands in a *path component* position, that is
between slashes.

| Command | Required result | Source |
| --- | --- | --- |
| `CD<-` | parent directory | HD 9-18, "you can include the back arrow immediately after `CD[n]` to move backwards one directory"; `SD do_chdir()`, `name[0]=='_' && !name[1]` |
| `CD:<-` | parent directory | `SD do_chdir()`, same branch reached through `parse_path`; Greg Nacu measured this on a real CMD HD and on an IDE64 and reported it through the reporter on #877: "cd:<- doesn't go into the directory, it goes up a directory" |
| `CD/<-` | change into the child directory literally named `<-` | Greg Nacu, same measurement, "cd/<- works to go into that directory"; `SD parse_path()` treats a component between slashes with `first_match()` |
| `MD:<-` | create a directory named `<-` | Greg Nacu, same measurement |
| `RD:<-` | remove it | Greg Nacu, same measurement |

Current behaviour: `U iec_path_to_fs_path()` rewrites every `_` to `..` wherever it
appears, so `CD/<-` goes to the parent instead of entering the directory, and a
directory named `<-` cannot be reached at all. **Change required.** The rewrite must
happen only in the name position, which `U IecCommandChannel::do_change_dir()`
already isolates for the `CD:<-` case.

This has one consequence for existing behaviour that has to be accepted
deliberately. `CD/<-/OTHERDIR`, going up and then down again in one command, works
today and is covered by `Suite8-CD-PARENT-OTHERDIR` in
`software/test/iecdrive/testdrive.cc`. After this change the left arrow in that
position is a directory name and the command fails. The capability itself is not
lost: `U resolve_directory_path()` also accepts `.` and `..` as path components, so
`CD/../OTHERDIR` does the same thing, and `..` is the spelling GAP singles out as
"something that none of the other devices can do". The existing test moves to `..`.

**SI-015.** `CD/:<-` goes to the parent. The colon introduces the name, so the arrow
is in the name position and SI-014 applies unchanged. Sources: `SD do_chdir()`, which
reaches its `name[0]=='_'` branch for this spelling; HD 9-18, "The back arrow cannot
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

Current behaviour: `U strip_terminator()` in `software/io/iec/cbmdos_parser.cc`
removes one trailing carriage return only. The second branch is missing.
**Change required**, and it is not cosmetic: it is the reason CMD's manual tells
programmers to append the terminator themselves when a parameter can be 13.

**SI-017.** The binary Change Partition command is exempt from SI-016, because its
parameter byte is mandatory and cannot be a terminator. Current behaviour:
`U strip_terminator()` already exempts `C` followed by `$D0`. Unchanged.

**SI-018.** The Position command is exempt from SI-016, because its last parameter
byte is data. Source: `SD parse_position()` begins `command_length =
original_length;`, restoring the length from before the strip. **Change required.**

**SI-019.** Where the last parameter is optional, the terminator wins, so partition
13 has to be asked for with the terminator sent explicitly. Source: HD 9-16,
"To avoid problems with reading information from Partition 13, the G-P command
should always be sent with a trailing carriage return (CHR$(13))." Unchanged.

### 3.4 Numeric parameters

**SI-020.** The numeric parameters of the block and user commands are separated by
any run of spaces, commas or cursor-right characters, and a colon may stand between
the command word and the first parameter. Source: HD 9-5 and 9-6, which show
`PRINT#15,"U1";2;0;1;34`, `PRINT#15,"U1 2 0 1 34"` and `PRINT#15,"U1:";2;0;1;34`
as equivalent. Unchanged; `U parse_block_parameters()` implements it.

### 3.5 Command length

**SI-021.** The command channel buffer holds at least 254 bytes, and the name buffer
of a data channel holds at least 254 bytes. Sources: HD 4-6, "the input buffer of
the HD is only 254 characters long"; SD `CONFIG_COMMAND_BUFFER_SIZE`, 120 on the
small AVR boards and 254 on uIEC; GUG, "The total length of the directory path,
including all the slashes is 232 characters", which is the binding number, because a
C64 OS path plus a partition number, a colon and a 16-character name does not fit in
anything smaller.

Current behaviour: `U iec_channel.h` declares `wr_buffer[65]` and both
`IecCommandChannel::push_data()` and `IecChannel::push_data()` stop storing at 64
bytes. **Change required.**

**SI-022.** A command longer than the buffer answers `32,SYNTAX ERROR` and is not
executed. Sources: HD B-2, error 32; `SD parse_doscommand()`, which returns
`ERROR_SYNTAX_TOOLONG` when the command fills the buffer; ROM `$C2CE`, which raises
error 32 when the command length reaches 42.

Current behaviour: the excess bytes are counted in `trace_cmd_dropped` and
discarded, and the truncated command is then executed. **Change required**, and this
is a data loss defect rather than a compatibility one: a `S` command truncated in the
middle of a path scratches files in the wrong directory.

---

## 4. Error codes

### 4.1 The table

The drive's own table in `U software/io/iec/iec_drive.cc` already carries the right
numbers and strings. What is wrong is the parser's private constants, which the
parser returns to the drive.

**SI-030.** `U software/io/iec/cbmdos_parser.h` defines

```c
#define ERR_SYNTAX        30
#define ERR_ILLEGAL_CHARS 31
#define ERR_ILLEGAL_NAME  32
#define ERR_UNKNOWN_CMD   33
```

The names and the numbers do not agree with CBM or CMD DOS. The correct meanings
are, from HD B-2 and 1541:

| Code | Meaning | Raised when |
| --- | --- | --- |
| 30 | SYNTAX ERROR (general) | the command was recognised but could not be parsed |
| 31 | SYNTAX ERROR (unrecognized command) | the first character is not a command letter |
| 32 | SYNTAX ERROR (command string too long) | over the command buffer |
| 33 | SYNTAX ERROR (illegal file name) | a wildcard where wildcards are not accepted |
| 34 | SYNTAX ERROR (missing file name) | no name, or a missing colon |

**Change required:** rename the constants to their real meanings and correct the
return sites. There are 35 of them in `cbmdos_parser.cc`; these are the ones whose
value changes. Every other site returns 30, which stays 30 because the command letter
was recognised and only its arguments were not.

| Site | Condition | Today | Required |
| --- | --- | --- | --- |
| `execute_command()` final `return` | first character is not a command letter | 33 | **31** (SI-031) |
| `parse_full_path()`, empty name | a colon with nothing after it | 32 | **34** |
| `parse_open()`, `contains_any(",=:\xA0\r")` | illegal character in a name | 31 | **33** |
| `copy_command()`, `dest.has_wildcard` | wildcard in the copy target | 32 | **33** |
| `rename_command()`, `dest.has_wildcard` | wildcard in the rename target | 32 | **33** |
| `block_command()` default | `B` followed by an unknown letter | 33 | **30** |
| `dir_command()` defaults | `C`, `M` or `R` followed by an unknown letter | 33 | **30** |
| `get_command()` default | `G` followed by an unknown letter | 33 | **30** |
| `user_command()` default | `U` followed by an unknown letter | 33 | **30** |
| `time_command()` final `return` | `T` followed by an unknown letter | 33 | **30** |
| `extended_command()` | `X` or `E` followed by an unknown word | 33 | **30** |

**SI-031.** An unrecognised command answers `31`. Verified in ROM at `$C160`:

```
C160  20 B3 C2  JSR $C2B3      ; strip the terminator first
C163  B1 A3     LDA ($A3),Y    ; first byte of the command
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

Current behaviour: `U IecParser::execute_command()` returns `ERR_UNKNOWN_CMD`, which
is 33. **Change required.**

Note the divergence: `SD parse_doscommand()` answers 30 here and reserves 31 for an
empty command. This specification follows HD, 1541, ROM and the reporter's stated
expectation rather than SD, and section 16 records it as C1.

**SI-032.** Opening a name for writing has four cases, and each has a different
answer.

| Case | Answer | Source |
| --- | --- | --- |
| wildcard, no `@` | `33` | 1541: "Pattern matching characters cannot be used in the Save command or when Opening files for the purpose of Writing new data"; `SD file_open()` |
| `@`, the pattern matches a file of the same type | replace it, keeping the **matched** file's name rather than the pattern | ROM `$D8FC` falls through on a type match; IDE 7.1; `SD file_open()` |
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
first entry matching `foo*` is not a PRG, which is what the reporter measured on
#877, and it replaces the file when it is. The same check guards the ordinary open
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

Current behaviour: `U parse_open()` does not reject a wildcard in a write name at
all. The 33 the reporter saw is produced further down, by the file system layer
refusing a host name that contains `*`, so none of the four cases can be told apart
where they have to be. **Change required**, in the parser and in the open path.

**SI-033.** Scratching nothing is not an error. The answer is
`01,FILES SCRATCHED,00,00`. Sources: HD B-1, "01 FILES SCRATCHED (not an error).
The number of files scratched will be indicated in the track variable";
`SD parse_scratch()`, which ends unconditionally with
`set_error_ts(ERROR_SCRATCHED,count,0)`; IDE 15.2.2, which shows the count in the
track field.

Current behaviour: `U IecCommandChannel::do_scratch()` returns
`ERR_FILE_NOT_FOUND` when the count is zero. TRACE shows C64 OS hitting exactly this
with `S/TEMPORARY/:*`. **Change required.**

**SI-034.** Selecting a partition that does not exist answers
`77,SELECTED PARTITION ILLEGAL`. Source: HD B-5. Unchanged.

**SI-035.** Opening a file for writing when one of that name exists, without `@`,
answers `63,FILE EXISTS`. Opening for reading when none exists answers
`62,FILE NOT FOUND`. Opening an existing REL file with a non-REL type answers `64`.
Sources: HD B-3 and B-4; `SD file_open()`. Unchanged; covered by Suite5 of
`software/test/iecdrive/testdrive.cc`.

### 4.2 Errors that must stop being reported as 69

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
Unchanged since #878 and #881; TRACE shows C64 OS using the binary form 29 times in
one boot and the ASCII form once.

### 5.2 Get partition info

**SI-041.** `G-P` with no parameter, or with 255, answers for the current partition.
With 0 it answers for the system partition, which this drive does not have, so byte 0
is 0, meaning "not created", and byte 2 is 0. With any other value it answers for
that partition, and again byte 0 is 0 if it does not exist. Current behaviour:
`U IecParser::get_command()` rewrites 255 to 0 and passes an absent parameter as 0,
and `U do_get_partition_info()` then resolves 0 to the current partition, so all
three cases give the same answer. **Change required:** 0 and "absent" are different
questions.
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
256-byte blocks on RAMLink (RL, same page). This drive follows the HD and uses 512.

Current behaviour: correct since PR #881, including the type in byte 0 and the
partition's own name in bytes 3 to 18. What remains wrong is that byte 27 is set to
`0xFF` as a placeholder, so every partition reports a size of 16711680 blocks.
**Change required:** report the real free-plus-used size of the partition's file
system, clamped to `0xFFFFFF`, which is what `SD parse_getpartition()` does.

**SI-042.** Byte 1 stays zero. The FD redefines it as a disk-information bit field
(FD, Getting Partition Information: bit 7 disk present, bit 6 formatted, bit 5 valid
CMD or CBM format, bit 4 true 1581, bits 3-0 density) and sd2iec writes `0xE2` there,
which decodes as an FD-2000 with a 1.6 MB disk inserted. This drive is not an FD and
must not claim to be one. IDE leaves the byte unused (IDE table 26).

**SI-043.** The partition type is decided from the file system at the partition's
root. Current behaviour, added in PR #881: `U cbm_partition_type_of()` reads the
geometry, mapping no track and sector addressing or 256 sectors per track to native,
40 sectors to 1581, and 21 sectors to 1541 or 1571 by track count. This reaches the
same mapping `SD pdir_refill()` reaches from the image type
(`(imagetype & D64_TYPE_MASK) + TYPE_NAT - 1` with `D64_TYPE_DNP == 1`, so a DNP
image lists as native). Unchanged.

### 5.3 Partition directory

**SI-044.** `LOAD"$=P[:pattern][=tp]"` lists the partitions. `tp` is one of `N`, `4`,
`7`, `8`, `C`. Sources: HD 9-14; FD and RL, which drop `C`; SD README. Unchanged
since PR #881.

**SI-045.** The listing is a header line, one line per partition, and the BASIC end
marker. **There is no blocks free line.** Source: issue #890; GAP, "The partition
directory does not list a blocks free count"; `SD pdir_refill()`, whose last act is

```c
buf->lastused = 1;
buf->sendeoi = 1;
memset(buf->data,0,2);
```

Current behaviour: `U IecChannel::read_dir_entry()` shares its end-of-listing path
with the file directory and emits `0 BLOCKS FREE.`. **Change required.** This is
issue #890.

**SI-046.** The number printed before the header name is the number of partitions.
Sources: GAP, "The first number before the partition directory's header line is a
count of how many user partitions there are"; `SD load_directory()`,
`buf->data[HEADER_OFFSET_DRIVE] = max_part`. Current behaviour:
`U IecChannel::setup_partition_read()` copies `c_header` and never sets byte 4, so
the number is 0. **Change required.**

**SI-047.** The block count column of each partition line is the partition number.
Sources: GAP, "The typical block size of a directory entry is used for the partition
number"; `SD pdir_refill()`, `dent.blocksize = part + 1`. Current behaviour:
`U read_dir_entry()` sets `info.size = part_idx * 254` so that the block conversion
yields the partition number. Unchanged.

**SI-048.** The type column is the three-character partition type: `NAT`, `41 `,
`71 `, `81 `. Sources: issue #877; SD `filetypes[]` entries 8 to 11. Unchanged since
PR #881.

**SI-049.** The header name is the drive's name, not a partition's. `SD` uses the
static string `SD2IEC` with id `IK`. This drive uses `ULTIMATE HD` with id `UL 64`,
which is already in `U setup_partition_read()`. Unchanged.

**SI-050.** A `SYSTEM` line is not emitted. `SD` emits one from `syspart_line[]`
immediately after the header, and IDE lists partition 255 as the disk label line.
This drive has no system partition, and SI-041 already answers `G-P 0` with type 0.
Recorded so the omission is deliberate rather than accidental.

### 5.4 Other partition commands

**SI-051.** `R-P:newname=oldname` renames a partition. Source: HD 9-14, FD, RL.
Not implemented. `U execute_command()` routes every `R` whose second character is not
`D` to `rename_command()`, which strips the leading letter and then hands `-P:WORK`
to `parse_full_path()`; that rejects the `-` and answers `30`. **Change required:**
recognise `R-P` and `R-H` before falling through to the file rename, as
`SD parse_doscommand()` does with `if (command_length > 2 && command_buffer[1] == '-')`.

**SI-052.** `N[n]:name[,id]` formats. Its full behaviour is specified in SI-071.

**SI-053.** `I[n][:]` initialises. On a device with no removable medium this is a
no-op that answers `00, OK` and frees the user buffers. Sources: HD 9-13, "This
function is performed automatically by the HD, but the command has been implemented
to retain compatibility"; `SD parse_initialize()`, which frees the user buffers.
Current behaviour, measured: `I` answers `73,U64HD ULTIMATE DOS V2.0,00,00`, because
`U IecCommandChannel::do_initialize()` returns `ERR_DOS` and `U user_command()`
routes `UI` to the same executer method. **Change required:** `I` and `UI` are different commands and must
answer differently. `I` answers `00, OK`; `UI` answers the 73 message.

**SI-054.** `V[n][:]` validates. On a host file system there is nothing to validate,
so it answers `00, OK` without doing anything, which is the same position HD 9-13
takes for `I`. Not implemented; **change required** (it currently answers 31 after
SI-031, or 33 today).

**SI-055.** The 1581-style sub-partition commands `/[n]:name` and
`/[n]:name,`+`CHR$(st)CHR$(ss)CHR$(sl)CHR$(sh)`+`,C` are not implemented and are out
of scope. They address 1581 emulation partitions, which this drive does not have.
Sources: HD 9-9 and 9-11; 1581 User's Guide, which gives the same syntax.

---

## 6. Directory commands

**SI-060.** `MD[n][path]:name` creates a directory. A colon is required; without one
the answer is `34`. A name that is a single shifted space answers `34`. Sources:
HD 9-17, which states the colon rule as its first guideline; `SD parse_mkdir()`.
Current behaviour is worse than accepting the colon-less form. `U dir_command()`
parses with `path_only = true`, so a command with no colon produces an empty name and
an empty path, and `MD` then tries to create the current directory itself: measured,
`MD NOCOLON` answers `63,FILE EXISTS`. **Change required.**

**SI-061.** `CD[n]{<-|[path][:]name}` changes directory, per SI-010 and SI-014.
Current behaviour: correct except for the left arrow and for wildcards in
intermediate components.

**SI-062.** `CD` into a file whose name has a disk image extension mounts that image
and makes its root the current directory; `CD<-` from the root of a mounted image
unmounts it. Sources: SD README, "CD is also used to mount/unmount image files";
GSD "Mounting a Disk Image". Current behaviour: the Ultimate virtual file system
mounts images as directories, so `CD:GAME.D64` already works and `CD<-` already
leaves. Unchanged, and recorded because it is the feature `XI` in sd2iec exists to
make discoverable.

**SI-063.** `RD[n]:name` removes a directory. It takes no path: a `/` anywhere in the
command answers `34`. It refuses a directory that is not empty. Sources: HD 9-19,
"This command does not allow the use of paths in order to avoid problems with
removing a subdirectory which is a parent of the directory in which you are located";
`SD parse_rmdir()`, which rejects any `/` with `ERROR_SYNTAX_NONAME` and answers
`63,FILE EXISTS` for a directory that still has entries. Current behaviour: `U do_remove_dir()` accepts a path and maps `FR_DENIED` to
`ERR_FILE_EXISTS`. Measured, `RD/PROBEDIR` removes the directory and answers
`00, OK`, which is the case HD 9-19 forbids in order to stop a user removing a parent
of the directory they are standing in. **Change required** for the path rejection.

**SI-064.** `R-H[n][path]:newname` renames a directory header. The name is at most
16 characters. Sources: HD 9-15; IDE 15.6.5; `SD parse_set_header(3)`, which also
accepts an id of at most five characters. On a host file system a directory has no
header separate from its name, so this renames the directory itself, which is what
GSD says sd2iec does with the equivalent operation. Not implemented;
**change required.** C64 OS depends on the header for a related reason: GAP says
"C64 OS in particular uses the directory header when creating a favorite... The name
of the favorite is taken from the directory header name."

**SI-065.** The header line of a directory listing carries the name of the directory
as it appears in its parent, and of the partition when the root is listed. Source:
GAP, "on SoftIEC it tries to show you the current path. This is inconsistent for the
sake of doing something clever, but it breaks the expectation... I would change this
behavior to SD2IEC's way, in the name of compatibility"; `SD fat_getdirlabel()`,
which returns the volume name for the root and otherwise walks `..` to find the entry
whose cluster is the current directory and returns that entry's name.

Current behaviour: `U setup_directory_read()` uses `partition->GetName()` for every
directory, truncated to its last 16 characters. That is no longer the full path GAP
complained about, but it is still not the directory's own name. **Change required.**

---

## 7. File commands

### 7.1 Open

**SI-070.** The type and access suffixes are `,P`, `,S`, `,U`, `,L`+`CHR$(rl)` and
`,R`, `,W`, `,A`, `,M`. Secondary address 0 forces read and PRG; secondary address 1
forces write and PRG; any other secondary address defaults an unspecified type to
SEQ. Sources: HD 9-23 to 9-31; `SD file_open()`, "Force mode+type for secondaries
0/1". Unchanged; `U setup_file_access()` implements it.

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
* If the file exists and the extension was given explicitly, it is formatted.
* The disk label is the name with the extension removed.

Current behaviour: `U IecParser::format_command()` parses a `name=type` form that
nothing sends, and `U do_format()` is a `printf` that answers `00, OK` without doing
anything. The reporter reported this on #877. **Change required.**

**SI-072.** A file whose name ends in a disk image extension, or in `.CRT` or
`.TCRT`, is written to the host file system under exactly that name, with no type
extension added. Source: `SD should_save_raw()`; SD README, "PRG files that have D64,
D41, D71, D81, DNP or M2I as an extension will always be written without an x00
header and without any additional PRG file extension."

Current behaviour: `U IecChannel::ConstructPath()` appends the type extension
unconditionally, so JiffyDOS's MD81 produces `FOO.D81.prg`. The reporter reported
this on #877. **Change required.**

### 7.2 Scratch, rename, copy

**SI-073.** `S[n][path]:pattern[,[n][path]:pattern...]` scratches. Each element gets
its own partition and path. Directories are skipped. The answer is
`01,FILES SCRATCHED,<count>,00` with the count in the track variable, per SI-033.
Sources: HD 9-27; `SD parse_scratch()`; IDE 15.2.2, whose examples include
`@S/STUFF/:*=OLD,*=BAK,/STUFF/BAK/:*`. Current behaviour: the parser splits on
commas and parses each element, which is right; the error code is wrong per SI-033
and the element count is capped at 8 by `filename_t filenames[8]` in
`U scratch_command()` where the sources have no limit below the command length.

**SI-074.** `R[n][path]:newname=[[n][path]:]pattern` renames. Source and destination
must be in the same directory; if they are not, the answer is `62,FILE NOT FOUND`.
An empty new name answers `34`. A new name that already exists answers `63`, unless
it differs from the old name only by case. A wildcard in the new name answers `33`.
Sources: HD 9-26; `SD parse_rename()`, including its note that "The 1541 renames the
file to '=' in this case, but I consider that a bug". Current behaviour:
`U do_rename()` does not check that the two directories agree and does not check for
an existing destination, and the wildcard rejection in `U rename_command()` answers
32 rather than 33. **Change required.**

**SI-075.** `C[n][path]:new=[[n][path]:]name[,[[n][path]:]name...]` copies, and with
more than one source appends them into the target. Path parsing restarts for every
source. The target takes the file type of the first source. Sources: HD 9-28, which
caps the sources at five; SD README, which has no cap and states the type rule.
Current behaviour: `U do_copy()` takes the type from the first source already, and
`U copy_command()` caps the sources at 8. GAP reported, against firmware 3.10a, that
the copy command produced `kernal.bin.bin` and ignored paths. Neither is still true.
Measured on the head of PR #881, by linking a probe against the same objects the host
suite uses: `C:PROBE2.BIN=PROBE.BIN` answers `00, OK` and the listing shows
`PROBE2.BIN  PRG`, one extension and not two; `C/SUB/:PROBE3.BIN=PROBE.BIN` answers
`00, OK` and the file appears in `SUB`. The requirement is therefore to keep a test
for both, not to fix anything.

**SI-076.** `L[n][path]:name` toggles the lock flag on one file or directory. A
locked file lists with `<` after its type and cannot be scratched; a locked directory
cannot be removed. Sources: HD 9-30; IDE 15.2.4; `SD parse_lock()`, which toggles
`FLAG_RO`. Not implemented; **change required.** The reporter raised it on #877.

**SI-077.** The sd2iec extensions `EL:`, `EU:`, `EH:`, `A:` and `XH:`/`D:` are
specified as follows, from `SD parse_ecommand()`, `parse_eunlock()`, `parse_attr()`
and `parse_set_header()`, and from SD README:

| Command | Effect |
| --- | --- |
| `EL:name[,name...]` | set read-only on each match; `EL:$` locks a whole mounted image |
| `EU:name[,name...]` | clear read-only on each match; `EU:$` unlocks a whole mounted image |
| `EH[path]:name` | toggle the hidden flag on one file |
| `EH:name,id` | with a colon straight after the partition, set the directory header |
| `A:[R][H][A]=name` | set exactly the named attributes and clear the others |
| `XH:name,id`, `D:name,id`, `R-H:name` | set the directory header |

They are a second priority behind the CMD commands, because C64 OS hides files by a
leading dot rather than by a device attribute (Greg Nacu, "Hidden Files": "hiding
files doesn't work on a CMD HD, nor a RAMLink, nor an FD2000 or FD4000"), and the
Ultimate's name mapping already escapes a leading dot as `{2E}` so such names round
trip.

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
Within a File". Unchanged; covered by Suite4 of `software/test/iecdrive/testdrive.cc`.

**SI-081.** The channel byte of `P` is masked to its low four bits. Source:
`SD parse_position()`, `find_buffer(command_buffer[1] & 0x0f)`. Current behaviour:
`U do_set_position()` masks only when the byte is 19 or above, which reaches the same
channel for both documented spellings but accepts bytes 15 to 18 that no drive
accepts. Harmless; recorded so that the next reader does not "fix" it into a
divergence.

**SI-082.** `P` also positions inside a plain file, to a 32-bit little-endian byte
offset, with the missing high bytes taken as zero. Sources: SD README under `P`;
GSD "Positioning (seeking) Within a File"; IDE 15.1.1, which documents both the
four-byte form and `F-P`. Unchanged in principle; `U do_set_position()` implements
it for `e_file`.

**SI-083.** `P` on a file opened for writing must be able to move beyond the current
end of the file, and a following write must extend the file. Sources: IDE 7,
"Unlike other systems it's possible to seek beyond the end of a file when writing or
modifying and create 'holes'"; the JiffyDOS MD81 sequence in TRACE, which is
`OPEN "FOO.D81,P,W"`, then `P`+`CHR$(4)`+`CHR$($FF)`+`CHR$($7F)`+`CHR$($0C)` to
position to 819199, then one byte, then `CLOSE`.

Current behaviour: `U do_set_position()` follows the seek with a 512-byte read to
refill the channel buffer, which fails on a file opened without read access; and
inside a mounted image `FileInCBM::seek()` cannot position a file that has no
allocated first sector. The answer the reporter saw was `69,FILESYSTEM ERROR,02,00`,
where the 2 is the raw `FRESULT` `FR_INT_ERR` put into the track variable by
`set_error_fres()`. **Change required:** do not read after seeking on a write
channel, extend the file where the medium allows it, and answer `72,DISK FULL` where
it does not.

**SI-084.** A relative file on the host file system is read in either of the two
layouts in use, and written in the one this firmware already writes. Nothing on a
user's medium changes.

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

**The two plain layouts are told apart deterministically, not by a heuristic.** Both
put the record length `r` in byte 0, so `r` is known before the layout is. This
firmware's files are `2 + n*r` bytes and sd2iec's are `1 + n*r`, so `size mod r` is
`2 mod r` for one and `1 mod r` for the other. Those two values coincide only when
`r` divides 1, that is only at `r == 1`, and HD 9-31 gives the legal record length as
2 to 254. So the test is exact for every file either device can legally have written.
A second, cheaper check comes first: byte 1 is zero in this firmware's layout,
because a record length below 256 has a zero high byte, so a non-zero byte 1 settles
it immediately.

**Change required**, and it is purely additive. Read both plain layouts and the
wrapped one; keep writing the two byte layout for a plain `.rel`. Interchange on the
write side is SI-146.

Current behaviour: `U setup_file_access()` reads the first two bytes as one little
endian record length and refuses a value of 256 or more with
`50,RECORD NOT PRESENT`. Against an sd2iec file the second byte is payload, so the
usual outcome is that refusal. The case that matters is the other one: when that
payload byte happens to be zero the value is accepted, the record length is right,
and every record is then read one byte late. Silent misreading is the behaviour to
replace, and it is why the discriminator above is on the file size rather than on the
record length alone.

---

## 9. Direct access

**SI-090.** `OPEN lf,dv,sa,"#"` allocates a 256-byte buffer on that channel, with the
buffer pointer at 1. `"##n"`, exactly three characters, allocates `n` chained
256-byte buffers with the pointer at 0; if the name is not exactly three characters a
plain buffer is allocated instead, and if there is not enough room the answer is
`70,NO CHANNEL`. Sources: HD 9-40; SD README "Large buffers"; GSD "Buffers and Large
Buffers". Current behaviour: `U setup_buffer_access()` gives a fixed 256-byte buffer
with the pointer at 0. **Change required** for the initial pointer of `#` and for
`##n`.

**SI-091.** The parameter forms are, from HD 9-43 to 9-46 and Appendix J:

| Command | Parameters |
| --- | --- |
| `B-A`, `B-F` | partition, track, sector |
| `B-R`, `B-W`, `B-E`, `U1`, `U2` | channel, partition, track, sector |
| `B-P` | channel, position |

Unchanged since PR #881, which fixed `B-A` and `B-F` from four parameters to three.

**SI-092.** `B-P` takes an optional third parameter, the high byte of a 16-bit buffer
position, defaulting to zero. Source: SD README, "The B-P command supports a third
parameter that holds the high byte of the buffer position, For example, 'B-P 9 4 1'
positions to byte 260"; GSD "The Buffer Pointer". Current behaviour:
`U block_command()` reads two parameters and `U do_buffer_position()` masks the
position to eight bits. **Change required** once SI-090 provides large buffers.

**SI-093.** The partition parameter of a direct access command is ignored; the
channel uses the partition that was current when it was opened. Source: HD 9-8 and
9-44, "The partition number should always be 0 (zero). This is because direct access
commands will always access the partition that was the current partition at the time
the direct access channel was opened"; GSD says the same. **Change required:** bind
the partition at `#` open time and use it, rather than resolving the parameter with
`GETPARTITION()` as `U do_block_read()` and its siblings do today.

**SI-094.** `B-R` and `B-W` differ from `U1` and `U2`: they use the first byte of the
block as a length. Sources: HD 9-44 and 9-45; `SD parse_block()`, which sets
`buf->position = 1; buf->lastused = buf->data[0];` for `B-R` and writes
`buf->data[0] = buf->position-1` for `B-W`. Current behaviour: `U block_command()`
maps `B-R` and `U1` to the same executer method. **Change required**, though it is
low priority: GSD recommends `U1` and `U2` for this reason and most software uses
them.

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
ancient times, before the 1541. This produces a syntax error in SoftIEC." Not
implemented; **change required.** The new number is not persisted unless the user
saves the settings.

**SI-101.** `S-8`, `S-9` and `S-D` are the typed aliases for swapping to device 8,
device 9 and back to the configured default. Sources: HD 9-34; IDE 15.4.1. On this
drive there is nothing to swap with, so `S-8` and `S-9` set the device number
directly and `S-D` restores the configured one. **Change required**, and note the
parsing hazard: `U execute_command()` routes every `S` to `scratch_command()`, so
`S-8` currently tries to scratch a file named `-8` and answers, measured,
`62,FILE NOT FOUND`. `SD parse_doscommand()` guards
this with `if (command_length == 3 && command_buffer[1] == '-')`.

**SI-102.** `W-0` and `W-1` clear and set a software write protect for the whole
drive. Sources: HD 9-35; IDE 15.4.10. While set, every write answers
`26,WRITE PROTECT ON`. Not implemented; **change required.** `U execute_command()`
has no `W` case at all, so today it answers 33 and after SI-031 it would answer 31;
either way the command has to be added rather than reclassified.

**SI-103.** The three resets are distinct.

| Command | Effect | Answer |
| --- | --- | --- |
| `UI` | nothing | `73,<dos version>,00,00` |
| `UI+`, `UI-` | select the serial timing | `00, OK` |
| `UJ` | close every open data channel; keep the current partition, every partition's current directory, and any mounted image | `73,...` |
| `U`+shifted J, `CHR$(202)` | close every open data channel, return every partition to its root, select the default partition | `73,...` |

Sources: HD 9-51; SD README under `UI/UJ` and `U<Shift-J>`; GSD "Warm, Cold and Hard
Reset". Current behaviour: `UI`, `UI+`, `UI-` and `UJ` are recognised but `UJ` only
sets the error code, closing nothing; `U`+`CHR$(202)` is not recognised at all.

**None of the three may reconfigure the IEC interface.** GAP reports that "cold reset
and hard reset 'uj' and 'uJ' seem to lock up the bus. Needs a STOP+RESTORE to
recover", against firmware 3.10a. The mechanism is visible in the present code and
must be designed out rather than re-measured. `IecDrive::reset()` begins with
`effectuate_registered_settings()`, which reaches `IecInterface::configure()`, and
that function opens with

```cpp
HW_IEC_RESET_ENABLE = 0;
```

holding the IEC processor in reset while it rewrites the device-number slots, then
releasing it. Command handlers run on the "IEC Server" task, inside the bus state
machine loop in `IecInterface::task()`, while the host still has the command channel
addressed. Resetting the IEC processor from there drops the drive off the bus in the
middle of a handshake, and a host waiting on a handshake that never completes is
exactly the symptom GAP describes.

**Change required**, in three parts. Implement `UJ` so that it closes the channels.
Implement `U`+`CHR$(202)`. And implement both so that the drive answers the current
transaction first and performs the state reset when the command channel has been
unlistened, never calling `IecInterface::configure()`, because nothing about the bus
configuration changes: the device number, the enable flag and the slot assignment are
all unchanged by a drive reset. `IecDrive::reset()` as it stands is the menu's reset,
not a command handler's, and must not be reused without removing that call.

**SI-104.** `U3` to `U8` and `UC` to `UH` jump into drive memory and are not
implemented; they answer `30`, for the same reason as SI-095. Source: HD 9-51.

**SI-105.** The memory commands are implemented as follows.

* `M-R`+`CHR$(lo)`+`CHR$(hi)`[+`CHR$(n)`] returns `n` bytes over the error channel,
  with `n` absent meaning 1 and `n` zero meaning 256. It does not read past a page
  boundary. Source: HD 9-46.
* `M-W`+`CHR$(lo)`+`CHR$(hi)`+`CHR$(n)`+data accepts 1 to 248 bytes and discards
  them. Source: HD 9-47.
* `M-E`+`CHR$(lo)`+`CHR$(hi)` answers `00, OK` and does nothing. Source: HD 9-48.

What `M-R` returns is specified in section 11. The reporter's position on #877 is
that the count matters more than the content: "M-R should return the number of
queried bytes. I agree we have nothing good to return but we can return each byte to
be 42... That way, a software that does M-R to identify devices is syntactically
happy." Not implemented; TRACE shows all four of C64 OS's probes answering
`33,SYNTAX ERROR` because `U execute_command()` routes `M` to `dir_command()`, which
accepts only `MD`. **Change required.**

**SI-106.** `S-C`, the SCSI pass-through of HD 9-39, is out of scope.

---

## 11. Device identification

This section exists because it is the one place where copying another device exactly
would be wrong.

**SI-110.** The `UI` message is the primary identification and must name this device.
Source: SD README, "If you are the author of a program that needs to detect sd2iec
for some reason, DO NOT use M-R for this purpose. Use the UI command instead and
check the message you get for 'sd2iec' and 'uiec' instead"; GSD "Device Detection".
Current behaviour: `73,U64HD ULTIMATE DOS V2.0`. Unchanged.

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
  of view, which is what TRACE shows happening to C64 OS today. The reporter's
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

Current behaviour: the four read forms are implemented in
`U IecParser::time_command()`. The write forms answer `00, OK` and do nothing.
**Change required** only in that the write forms should set the Ultimate's real time
clock, which is the device's clock, or answer `30,SYNTAX ERROR` as sd2iec does when
there is no clock to set. Silently answering OK and not setting the clock is the one
thing that must not remain.

---

## 13. Directory listings

### 13.1 Layout

**SI-130.** A listing is a BASIC program: a two-byte load address `$0401`, then per
line a two-byte link pointer, a two-byte line number carrying the block count, the
text, and a zero; the program ends with two zero bytes. Source: `SD dirheader[]`,
`{1, 4, /* BASIC start address */ 1, 1, /* next line pointer */ 0, 0, ...}`.

Current behaviour: `U iec_channel.h` `c_header[32] = { 1, 1, 4, 1, 0, 0, 18, 34, ...}`
transposes the first four bytes, so the drive sends load address `$0101` and link
`$0104`. TRACE confirms it on the wire:
`#48 CLOSE ... read=384 [01 01 04 01 02 00 12 22 4E 4F 20 4E 41 4D 45 20 ...]`.

This is invisible to `LOAD"$",dv`, which is a relocating load, and to `LIST`, which
uses the link only as an end marker, and to a program that reads `$` as a file and
skips two bytes. It is wrong for `LOAD"$",dv,1`, an absolute load, which would place
the listing at `$0101`. **Change required.**

**SI-131.** Byte 4 of the header, the low byte of the header line's BASIC line
number, is the partition number for a file listing and the number of partitions for a
partition listing. See SI-046. Current behaviour: correct for a file listing, zero
for a partition listing.

**SI-132.** The file type field is followed by `<` when the file is locked, and
preceded by `*` when the file was not closed. Sources: HD 9-30; `SD createentry()`.
Locking is SI-076; the splat has no counterpart on a host file system and is not
required.

**SI-133.** The low byte of the next-line link pointer is `(file size mod 254) + 2`,
so that a program can recover the exact byte length of a file. Sources: SD README,
"If known, the low byte of the next line link pointer of the directory listing will
be set to (filesize MOD 254)+2"; GSD "File Sizes and Blocks Free". Not implemented;
**change required.** It costs nothing and it is the only way a C64 program can learn
a file's true size from a listing.

### 13.2 Filters

**SI-134.** `LOAD"$[n][path][:pattern[=tp]]"` filters by name and type. `tp` is `P`,
`S`, `U`, `R`, or `B` for a directory. Sources: HD 9-20, "P for program (PRG), S for
sequential (SEQ), U for user (USR), R for relative (REL), and B for subdirectory
branch (DIR)". `D` is accepted as a synonym for `B`, which is an sd2iec extension;
GSD warns that "If filtering a directory programmatically, B should be used for
compatibility with CMD storage devices. D is only supported by sd2iec", and IDE 6.2
in fact maps `D` to DEL. `H` additionally shows hidden files.

Current behaviour: `U parse_dir_option()` accepts all of them, but it maps `H` to bit
6 of the type mask while `U read_dir_entry()` tests `1 << ftype` with `ftype` at most
5. Measured, `$:*=H` lists the header and no entries at all, where `$:*=P` and
`$:*=B` list correctly.
**Change required:** `H` is not a type, it is a flag that suppresses the hidden
filter, and it must be kept separate from the type bits.

**SI-135.** `LOAD"$=T..."` produces a time-stamped listing, with options `L`, `N`,
`>stamp` and `<stamp` and the stamp format `MM/DD/YY HH:MM xM`. The long line is
`112 "TESTFILE"       PRG   07/27/19 03.44 PM` and the short line is
`112 "TESTFILE"       P 07/27 03.44 P`. Sources: HD 9-21 and 9-22; GSD "Time and Date
Stamped Directory Listings". Unchanged; `U cbmdos_time()` produces both forms.

**SI-136.** Wildcard matching: `?` matches one character and `*` matches the rest.
Only one `*` is meaningful. Characters after the `*` are matched against the end of
the name, which is the 1581 rule and sd2iec's default (`SD match_name_str()` with
`POSTMATCH` set, SD README under `X*+/X*-`, "the default value is enabled (+)").
Matching stops after 16 characters.

Current behaviour: `U pattern_match()` is a full backtracking glob, so a second `*`
matches in the middle of a name. GAP noted it approvingly: "SoftIEC even supports
more than one * which the other devices do not." It is a superset for one `*` and a
divergence for more, and it narrows rather than widens a match, so it is safe for
`S`. Unchanged, and recorded so that it is a known difference rather than an
accident.

### 13.3 Raw directory

**SI-137.** `OPEN lf,dv,sa,"$"` with `sa` not 0 returns the raw directory sectors
rather than the BASIC listing. Sources: `SD load_directory()`, which branches on
`secondary != 0` into `d64_raw_directory()` or a synthesised BAM sector followed by
raw 32-byte entries; IDE 6.3, "To open a raw directory channel, use secondary address
2-14"; GSD notes the 1541 does the same. Not implemented; **change required**, at low
priority, because only tools that read the BAM directly need it.

---

## 14. File naming on the host file system

### 14.1 The scheme that already exists

The Ultimate maps a PETSCII name to a host file name by escaping the bytes a host
file system cannot carry, and by appending the CBM file type as an extension. The
design is `doc/filenames_design.txt` and `software/test/iecdrive/doc.md`; the code is
`petscii_to_fat()` and `fat_to_petscii()` in `software/components/pattern.cc`.

That same scheme is sd2iec's **extension mode 5**. It is not a coincidence and not a
convergence: the functions are character for character the same in both projects,
including the comment `// '|' > 96 ;)` and the `reserved_names` table. GideonZ wrote
them here in 2020 (`3ec23bb9`, 4 October 2020); the reporter added them to sd2iec as
mode 5 in 2025 (`0f22587`, 6 June 2025) and documented them in that fork's README in
2026 (`ddb949c`). Upstream sd2iec has modes 0 to 4 only.

**SI-140.** This is a compatibility contract, not an implementation detail. A card
or stick written by an Ultimate must read back on a markusC64 sd2iec in extension
mode 5, and the reverse. Nothing in this specification may change the mapping except
to close the two divergences below.

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

**SI-142.** Close the two divergences.

* sd2iec also escapes `*` and `?`; this firmware does not. sd2iec additionally
  refuses to create a name containing either (`SD a76deb2`). Adopt both: escape them,
  and refuse to create such a name per SI-032.
* The length guard differs by one: sd2iec tests `(i + 4) > maxlen`, this firmware
  tests `(i + 4) >= maxlen`, so it truncates one byte earlier. Adopt sd2iec's.

**SI-143.** Names that the forward mapping can never produce must still be readable.
`software/test/iecdrive/doc.md` sets this out under "Injectivity vs Accessibility"
and the code implements it with the rendered-name fallback in
`resolve_existing_iec_path()` and `find_rendered_iec_child()`. Unchanged. The
warning in that document stands: the fallback must not be used for `S`, because
scratching by a heuristic match deletes the wrong file.

### 14.2 x00 wrappers

**SI-144.** Add support for reading P00, S00, U00 and R00 files: a 26-byte header
beginning with `"C64File"` and a zero, the 16-character CBM name plus a terminator at
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

**SI-145.** Writing x00 files is a configuration choice, default off, so that
existing users see no change. When on, it follows sd2iec mode 1 (x00 for SEQ, USR and
REL, plain for PRG) or mode 2 (x00 for everything). Source: SD README under `XEnum`.

**SI-146.** The x00 wrapper is the whole of the write side answer for relative files.
With it the record length is at a fixed header offset that both devices already
agree on, so a relative file created while SI-145 is enabled is readable by an
sd2iec without either device changing its plain layout. A plain `.rel` keeps this
firmware's two byte layout for ever, which is why SI-084 needs no migration and why
no file a user already has is touched.

### 14.3 The shifted space defect

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
| scratch list elements | limited only by the command length | 8 | SD README under `S:` |
| copy source elements | at least 5 | 8 | HD 9-28 |
| partitions | 1 to 255 | 1 to 255 | GUG |
| CBM name | 16 characters | 16 | all |
| path components in one command | at least 16 | 16 | GUG: 232 characters allows about 13 levels of 16-character names |

The 16-component cap is `path.split('/', components, 16)` in
`U resolve_directory_path()`. It is adequate for the path length GUG specifies but
has no margin, and anything over it is silently truncated rather than refused.

### 15.2 Behaviour that must not change

These are stated so that the work below does not quietly regress them. Each is
already correct.

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

**SI-151. Retired.** An earlier revision of this document required the relative file
record length to change from two bytes to one, and this requirement carried the
migration for the files this firmware had already written. Both were wrong: a
migration over user data, by a heuristic, in order to gain an interchange that the
x00 wrapper already provides, is a cost with no matching benefit. SI-084 now reads
both plain layouts and keeps writing the existing one, and SI-146 provides the
interchange, so there is nothing to migrate. The number is left retired rather than
reused, so that a reference to it in an older note resolves to this paragraph.

**SI-152.** Raising the command buffer (SI-021) changes the `SOFTIEC_TRACE_MAX_BYTES`
assumption in `software/io/iec/iec_trace.h`, which is 64 because the buffers are 64.
The diagnostics must keep rendering a whole command or mark the cut.

**SI-153.** Three existing tests encode behaviour this specification changes, and
each must be updated in the same commit as the change, not separately.

| Test | Today | After |
| --- | --- | --- |
| `Suite8-CD-PARENT-OTHERDIR` in `software/test/iecdrive/testdrive.cc` | `CD/_/OTHERDIR` succeeds | rewritten as `CD/../OTHERDIR` (SI-014) |
| any test that asserts `62,FILE NOT FOUND` from a scratch that matched nothing | 62 | `01,FILES SCRATCHED,00,00` (SI-033) |
| any test that asserts `33` for an unrecognised command | 33 | `31` (SI-031) |

---

## 16. Conflicts between the sources, and how each is decided

Every row is decided. Where the sources disagreed, the row states the primary
evidence that settled it rather than which source was preferred. Nothing here is
left for someone else to answer before the work can start.

| # | Case | What the sources said | Decision and evidence |
| --- | --- | --- | --- |
| C1 | Error code for an unrecognised command | HD B-2 and 1541 say 31. `SD parse_doscommand()` says 30 and reserves 31 for an empty command. The reporter asked for 31. | **31** (SI-031). Settled by ROM `$C175`: the command-table miss loads `#$31`. sd2iec is the outlier, and the reporter's request agrees with the hardware. |
| C2 | `CD/:<-` | `SD do_chdir()` goes to the parent. The reporter expected it to enter a directory named `<-`, while noting he had no reference for the spelling beyond an emulator. | **Parent** (SI-015). One rule covers every measured case: the arrow is the parent in the name position and a literal character in a path component. HD 9-18 says the arrow "cannot be combined with any subdirectory path information", which is the same statement. `CD/<-` remains the way to enter such a directory. |
| C3 | `SAVE"@:foo*"` | The reporter measured `64` on a real drive. `SD file_open()` and IDE 7.1 replace the matched file; sd2iec answers 64 only when nothing matched. | **Settled by ROM `$D8F5`** (SI-032). Save-with-replace compares the found entry's type against the requested type and answers 64 on a mismatch or on a REL. `SAVE` asks for PRG, so a `foo*` that first matches a non-PRG answers 64 and one that matches a PRG replaces it. Every source is consistent once that check is known. |
| C4 | `$=P` footer | Issue #890 and `SD pdir_refill()` say no footer. IDE prints `n PARTITIONS.`. | **No footer** (SI-045). The issue is explicit, sd2iec agrees, and IDE64's footer is its own extension. |
| C5 | `$=P:*=C` | HD 9-14 says `C` selects 1581 CP/M. `SD load_directory()` maps `C` to internal type 12, which is `80 `, an 8050 image. | Accept `C` and match nothing, because this drive has neither kind of partition. Recorded so the sd2iec mapping is not copied by mistake. |
| C6 | `=D` directory filter | `SD` and this firmware treat `D` as DIR. IDE 6.2 maps `D` to DEL. GSD warns that on other drives `D` matches everything. | Keep `D` as a synonym for `B` (SI-134), and say in the user documentation that software should send `B`. Changing it would break the sd2iec software that already sends `D`, and no software can be relying on `D` meaning DEL here because this drive has no DEL entries. |
| C7 | Wildcards with more than one `*` | `SD match_name_str()` and CBM DOS stop at the first `*`. This firmware backtracks. GAP calls the difference harmless. | Keep the current behaviour (SI-136). For one `*` it agrees with sd2iec's default; for more it narrows rather than widens a match, so no command can act on more files than the other devices would. |
| C8 | G-P byte 1 | HD and RL say reserved zero. FD defines a disk-information bit field and `SD` writes `0xE2`, which decodes as an FD-2000 with a 1.6 MB disk. | **Zero** (SI-042). Byte 1 is a claim about the device model, and this drive is not an FD. |
| C9 | G-P block unit | HD and FD count 512-byte blocks; RL counts 256-byte blocks. | **512** (SI-041), following the HD, which is the reference text and the larger of the two devices this drive resembles. |
| C10 | What `M-R` should answer | Nothing documents what C64 OS concludes from each answer. SD README says not to use `M-R` for detection at all. The reporter proposed a constant 42. | **The requested count of `$00` bytes at every address, and no magic table** (SI-112, SI-113). `$00` matches no model signature, and it is the value sd2iec deliberately returns at `$FFFE` to make Action Replay 6 fall back to the KERNAL loader. Faking a 1541 signature would invite a loader to upload drive code this drive cannot run. Identification is the `UI` string, whose format SI-114 fixes. |
| C11 | Whether `UJ` and `U`+shifted J lock the bus | GAP reports it against firmware 3.10a. Nothing since has tested it. | **Settled from the code, not by re-measuring** (SI-103). `IecDrive::reset()` reaches `IecInterface::configure()`, which sets `HW_IEC_RESET_ENABLE = 0` and holds the IEC processor in reset while it rewrites the slots. Command handlers run on the IEC task inside the bus state machine with the host still addressed, so that call is a bus-lock by construction. The requirement is that no reset command touches the interface, which removes the mechanism whether or not 3.10a's symptom survives today. |
| C12 | Shifted space (`$A0`) inside a name | The reporter wrote that this "is a topic of its own, but I do not want to start that topic without having discussed that first". | **Decided** (SI-147 for the two defects, SI-148 for the policy): `$A0` is legal inside a name and maps to `{A0}`; a trailing run is padding and is dropped; a name that is empty or starts with `$A0` is refused on create; a listing ends the name at its terminator or at 16 characters rather than at the first `$A0`, because ending it earlier would make this drive and an sd2iec print different names for the same file. |
| C13 | GAP's report that copy produces `kernal.bin.bin` and ignores paths | Measured against firmware 3.10a. | **Already fixed.** Measured on the head of PR #881 with a probe linked against the host suite's objects: the target gets one extension and a target path is honoured (SI-075). The requirement is a regression test. |

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

The measurements marked "measured" in this document were taken by linking a small
probe against this target's objects, sending each command through
`IecCommandChannel::push_command()` and reading `IecDrive::get_error_string()` back.
Those cases belong in the suite as they are implemented, one assertion per
requirement, so that the answer recorded here becomes the answer the suite enforces.
The set is: the eleven commands of section 4.1, the scratch of SI-033, the `MD` and
`RD` forms of SI-060 and SI-063, the two copies of SI-075, the three filters of
SI-134, and the four left-arrow forms of SI-014.

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
* Anything whose behaviour depends on the signedness of `char`. The host builds are
  `-fsigned-char`, the U64 build is Nios II and also signed, and the U64 II and U2+L
  builds are RISC-V and unsigned. SI-147 is the known case and no host test can see
  it. A cheaper alternative for this one class is to compile the affected host suite
  a second time with `-funsigned-char` and run both, which would catch it without a
  device.

**T4. The C64 OS acceptance test.** Install C64 OS on a Software IEC partition, boot
it, and compare the resulting `SOFTIEC-TRACE` log against TRACE. The criterion is
that no line carries an error code that a CMD HD would not also produce. TRACE gives
the baseline: 68 commands, 107 opens, 108 closes and 80 status reads in one boot, and
of its 601 lines 11 carry `33,SYNTAX ERROR` and 9 carry `62,FILE NOT FOUND`.

---

## 18. Order of work

Grouped so that each group is independently shippable and independently testable.

**Group 1, the defects the reporter raised.** SI-031 error codes, SI-033 scratch,
SI-045 and SI-046 the partition directory, SI-053 `I`, SI-072 image names,
SI-083 `P` on a write channel, SI-105 memory commands, SI-071 `N`. These are the
contents of issues #877 and #890 and they are what unblocks the reporter's testing.

**Group 2, the limits.** SI-021 and SI-022 buffer size and the 32 error. This is a
data loss defect and it gates anything that sends a long path, which C64 OS does.

**Group 3, the missing commands.** SI-100 `U0>`, SI-101 `S-8`/`S-9`/`S-D`,
SI-102 `W-0`/`W-1`, SI-054 `V`, SI-051 `R-P`, SI-064 `R-H`, SI-076 `L`.

**Group 4, the grammar.** SI-012 wildcards in path components, SI-014 the left arrow,
SI-016 the second terminator branch, SI-060 the `MD` colon, SI-063 the `RD` path
rejection, SI-074 rename checks, SI-093 direct access partition binding.

**Group 5, the listings.** SI-130 header bytes, SI-133 the size remainder,
SI-065 the header name, SI-137 the raw directory.

**Group 6, naming.** SI-147 first, because it is the only requirement in this
document that makes two Ultimate models write different bytes to the same medium.
Then SI-142 the two divergences, then SI-144 to SI-146 x00 together with SI-084,
which is the relative file reader the wrapper completes.

**Group 7, the rest.** SI-090 to SI-092 large buffers, SI-094 `B-R` and `B-W`,
SI-077 the sd2iec attribute commands, SI-120 clock writes.

The `SOFTIEC-TRACE` diagnostics added by PR #881 stay until group 1 and group 4 are
finished and the reporter confirms that a C64 OS boot produces no error this document
does not allow, then are removed by the procedure their own header describes.

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
* M2I files, which sd2iec itself has deprecated (SD README, Deprecation notices).
* Mapping each Ultimate storage device to its own IEC device number, which GAP asks
  for. The partition model in section 2 answers the same need within one device
  number, and C64 OS supports at most five devices at once against 255 partitions.
* The firmware hang the reporter saw three times on #877 while starting C64 OS, where
  "the ultimate becomes unresponsive it's completely dead, just the c64 is still
  running. But not the soft iec. The menu button is dead." Nothing in this
  specification addresses it, because nothing here explains it: the reported sequence
  is power on, enable Software IEC, add a partition, `@"cp2"`, load the directory,
  run C64 OS, and the successful run of exactly that sequence is TRACE. It is a
  separate investigation and needs its own issue. What would move it forward is a
  syslog capture that ends at the hang rather than one from a successful boot, and
  whether REST and ping still answer while the menu button does not.

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

Sections 2 to 15 define SI-001 to SI-152. Requirements that change nothing are marked
"Unchanged" and exist as a contract. Requirements marked "Change required" are the
work, and section 18 orders them.
