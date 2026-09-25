# Software IEC: CMD DOS, sd2iec and C64 OS compatibility

**Status: the contract.** This describes what the Software IEC drive does and why.
Every requirement is either in force, and the tests in `target/pc/linux/parse`,
`target/pc/linux/iecdrive` and `tests/e2e/io/iec` hold the drive to it, or it is marked
**Deliberately unsupported** and says what the drive answers instead and why that is the
right answer. Section 18.1 lists the deliberate exclusions in one table, with the
requirements that answer differently from one of their sources; section 19 lists what is
out of scope, which is the capabilities this drive does not have at all.

"In force" means implemented in this repository and held to by those tests. A firmware
release answers as this document describes only from the release that carries the change,
so a requirement can be in force here and absent from the newest released build. The
release each requirement first ships in is not tracked here; `git log` on the files a
requirement names answers that, and the pull request that introduces a requirement says
so in its own description.

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

## Contents

- [1. Sources of truth and how conflicts are settled](#1-sources-of-truth-and-how-conflicts-are-settled)
  - [1.1 Source register](#11-source-register)
  - [1.2 Precedence](#12-precedence)
  - [1.3 Acceptance](#13-acceptance)
  - [1.4 Notation](#14-notation)
  - [1.5 Conformance and traceability](#15-conformance-and-traceability)
  - [1.6 Terminology](#16-terminology)
- [2. The model](#2-the-model)
  - [2.1 Device, partitions, directories](#21-device-partitions-directories)
  - [2.2 What the partition model must not do](#22-what-the-partition-model-must-not-do)
- [3. Command string grammar](#3-command-string-grammar)
  - [3.1 Path syntax](#31-path-syntax)
  - [3.2 The left arrow](#32-the-left-arrow)
  - [3.3 The command terminator](#33-the-command-terminator)
  - [3.4 Numeric parameters](#34-numeric-parameters)
  - [3.5 Command length](#35-command-length)
- [4. Error codes](#4-error-codes)
  - [4.1 The table](#41-the-table)
  - [4.2 The error codes that are not CBM DOS errors](#42-the-error-codes-that-are-not-cbm-dos-errors)
- [5. Partition commands](#5-partition-commands)
  - [5.1 Change partition](#51-change-partition)
  - [5.2 Get partition info](#52-get-partition-info)
  - [5.3 Partition directory](#53-partition-directory)
  - [5.4 Other partition commands](#54-other-partition-commands)
- [6. Directory commands](#6-directory-commands)
- [7. File commands](#7-file-commands)
  - [7.1 Open](#71-open)
  - [7.2 Scratch, rename, copy](#72-scratch-rename-copy)
- [8. Relative files](#8-relative-files)
- [9. Direct access](#9-direct-access)
- [10. Device commands](#10-device-commands)
- [11. Device identification](#11-device-identification)
- [12. Real time clock](#12-real-time-clock)
- [13. Directory listings](#13-directory-listings)
  - [13.1 Layout](#131-layout)
  - [13.2 Filters](#132-filters)
  - [13.3 Raw directory](#133-raw-directory)
  - [13.4 File types inside a disk image](#134-file-types-inside-a-disk-image)
- [14. File naming on the host file system](#14-file-naming-on-the-host-file-system)
  - [14.1 The scheme that already exists](#141-the-scheme-that-already-exists)
  - [14.2 x00 wrappers](#142-x00-wrappers)
  - [14.3 The shifted space](#143-the-shifted-space)
- [15. Limits, and what must not break](#15-limits-and-what-must-not-break)
  - [15.1 Limits](#151-limits)
  - [15.2 Behaviour that must not change](#152-behaviour-that-must-not-change)
  - [15.3 Consequences elsewhere](#153-consequences-elsewhere)
- [16. Conflicts between the sources, and how each is decided](#16-conflicts-between-the-sources-and-how-each-is-decided)
- [17. Tests](#17-tests)
- [18. The operation log](#18-the-operation-log)
  - [18.1 Deliberately unsupported, and the differences from the sources](#181-deliberately-unsupported-and-the-differences-from-the-sources)
- [19. Out of scope](#19-out-of-scope)
- [Appendix A. What C64 OS sends](#appendix-a-what-c64-os-sends)
- [Appendix B. Requirement index](#appendix-b-requirement-index)
- [Appendix C. The programs issue #917 reports](#appendix-c-the-programs-issue-917-reports)
- [Appendix D. Conformance against the two C64 OS reference articles](#appendix-d-conformance-against-the-two-c64-os-reference-articles)
  - [D.1 GAP, "Gaps in Software IEC"](#d1-gap-gaps-in-software-iec)
  - [D.2 GSD, "SD2IEC User's Manual"](#d2-gsd-sd2iec-users-manual)
- [Appendix E. Traceability](#appendix-e-traceability)

---

## 1. Sources of truth and how conflicts are settled

### 1.1 Source register

| Code | Source |
| --- | --- |
| **1541** | [*1541-II Disk Drive User's Guide*](https://www.zimmers.net/anonftp/pub/cbm/manuals/drives/1541-II_Users_Guide.pdf), DOS error message list. Where a point needs a Commodore drive other than the 1541, the *1571* and *1581 User's Guides* are cited by name. |
| **917** | Issue [#917](https://github.com/GideonZ/1541ultimate/issues/917), "More SoftIEC compatibility issues", opened by the reporter on 18 September 2026. It carries Greg Nacu's measurements of a CMD HD and an sd2iec against this drive, and photographs of the two BASIC programs he used. The programs are transcribed in appendix C. |
| **FD** | [*CMD FD-Series Disk Drives User's Manual*](https://archive.org/details/CMD_FD_Series_Disk_Drive_Users_Manual_1993-10_Creative_Micro_Designs). |
| **FDROM** | The CMD FD-2000 DOS V1.40 ROM, 32768 bytes mapping to `$8000..$FFFF`, SHA-256 `e6b9c7562cc51ebe5577840db29d10fb0a1b8540f7cd5fbee6cc2e15f49a9f72`. It is not in this repository. Quoted addresses are from a disassembly of that image. |
| **GAP** | Greg Nacu, ["Gaps in Software IEC"](https://c64os.com/post/softwareiecgap), 10 January 2023. The canonical statement of why C64 OS does not support this drive. Written against firmware 3.10a; several of its items are already fixed. |
| **GFN** | Greg Nacu, ["Understanding SD2IEC Filenaming"](https://c64os.com/post/sd2iecfilenames). |
| **GSD** | Greg Nacu, ["SD2IEC User's Manual" v1.3](https://c64os.com/post/sd2iecdocumentation). |
| **GUG** | [*C64 OS User's Guide*, File System chapter](https://c64os.com/c64os/usersguide/filesystem). |
| **HD** | *CMD HD Hard Drive User's Manual*, 4th edition, January 1991. The scan linked on issue #877, [primrosebank.net scan](http://www.primrosebank.net/computers/pet/documents/CMD-HDD-Manual_OCR.pdf). Page numbers are the manual's own, e.g. 9-15, B-2. A second copy is on [archive.org](https://archive.org/details/cmd-hd-series-user-manual-4th-ed-2nd-printing-feb-1993-creative-micro-designs-inc). |
| **HDR** | [*CMD HD Manual Remaster V0.2*](https://archive.org/details/cmd-hd-manual-remaster). Same text, cleaner typesetting. Used to resolve OCR damage in HD. |
| **IDE** | [*The IDE64 Project user's guide*](https://s3.amazonaws.com/com.c64os.resources/weblog/sd2iecdocumentation/manuals/IDE64_Users_Guide.pdf), IDEDOS 0.90, 24 February 2019. Section numbers as printed. Linked by the reporter on #877. |
| **RL** | [*CMD RAMLink User's Manual*](https://archive.org/details/CMD_RamLink_Users_Manual). |
| **ROM** | [`roms/1541.bin`](../roms/1541.bin) in this repository, 16384 bytes, mapping to `$C000..$FFFF`. Quoted disassembly was produced from that file. |
| **SD** | SDU and SDM together: a citation reads `SD` only where SDU and SDM carry the same behaviour at the commits their rows name, which was checked for every function, constant and README passage this document cites. Where they differ, the citation names SDU or SDM, and the requirement says what the other one does. |
| **SDBUGS** | [`markusC64/sd2iecCommonBugs`](https://github.com/markusC64/sd2iecCommonBugs), a list of defects the sd2iec family shares, kept by the reporter of #877 and #917. A report is on it when it is in the upstream firmware or in three independent forks. It is not a source of truth for what this drive does: it says what other implementations get wrong, and `Suite11-CommonBugs` checks this drive against it. |
| **SDM** | sd2iec as extended by the reporter of #877 and #917, [`markusC64/sd2iec`](https://github.com/markusC64/sd2iec) at commit `9087321` on its `devel` branch, 4 September 2026. A fork of SDU that tracks it and adds, among other things, the attribute, locking and header commands, the `$` image write lock and the extension mode 5 name mapping. Paths are `src/...`, and `SDM README` is its README, which the reporter named as this drive's sd2iec documentation. |
| **SDU** | sd2iec as it ships, [`thierer/sd2iec`](https://github.com/thierer/sd2iec) at release [`v1.0.0atentdead0-186-g069555f1`](https://github.com/thierer/sd2iec/releases/tag/v1.0.0atentdead0-186-g069555f1), commit `069555f1`, 13 June 2026. A clone of the original at `sd2iec.de`, and the line a user is most likely to be running. Paths are `src/...`, and `SDU README` is its README. |
| **TRACE** | [`log_boot.log.txt`](https://github.com/user-attachments/files/32097131/log_boot.log.txt), attached by the reporter to #877 on 11 September 2026. 601 lines of `SOFTIEC-TRACE` output from one successful C64 OS boot on a U64 II. |
| **U** | This firmware, [GideonZ/1541ultimate](https://github.com/GideonZ/1541ultimate). Paths are relative to the repository root. |

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
2a. Where SDU and SDM differ, SDU is normative, because that is the line a user is
   likely to be running, and the requirement says what SDM does. Three such conflicts were
   found. The hidden attribute inside a disk image, which SDU clears from the type byte
   and SDM keeps behind a setting: this drive follows SDU (SI-134). And `,M`, which SDU
   opens read and write on a FAT file system and SDM opens for reading: this drive
   follows SDM, for the reason SI-070 gives, which is the exception that proves the rule
   is about behaviour rather than about which line it came from. And the rename of a file
   that lives in an x00 wrapper, where SDU writes the header alone and SDM renames the
   host file to match it: this drive follows SDM, for the reason SI-144c gives. Where a
   command exists in SDM only, there is nothing to conflict with, and the requirement
   says that it rests on SDM alone: SI-064's optional id, SI-077, SI-077a and the name
   mapping of SI-140 to SI-148. SI-076's `L` is a CMD command (HD 9-30); only its
   sd2iec citation, for the toggle, is SDM's.
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
* **A2.** Every requirement in force is traceable to a test that names it, as section 1.5
  requires and appendix E shows. The exceptions are the requirements that state only what
  is out of scope and name no answer (SI-106, SI-115), and the retired SI-151.
* **A3.** Nothing in section 15.2 behaves differently.

### 1.4 Notation

**Normative text.** A requirement is a numbered paragraph beginning `SI-nnn`. Its
statements are normative and are written in the present indicative: "the drive answers
`31`" states what the drive does and what an implementation has to keep doing. `must` and
`must not` appear where a statement is a prohibition or where it is easy to read an
obligation as a description. Nothing here uses `should` or `may`: a requirement that is
optional would say nothing an implementation could be held to, and the deliberate
exclusions are listed instead (section 18.1).

**Rationale, sources and differences.** A requirement's first paragraph is the statement.
Anything after it explains why, records where the behaviour comes from, or says how it
differs from a source (`Difference from ...`). A source is cited either on a line of its
own, after `Source:` or `Sources:`, or inside the sentence it belongs to where the
citation is part of the statement. Those
paragraphs are not separate requirements, and an implementation cannot satisfy a
requirement by following its rationale rather than its statement.

**Identifiers.** `SI-nnn` identifiers are stable. A letter suffix, as in `SI-103b`, is a
further rule of the requirement it follows and is numbered that way so that a number
already cited elsewhere keeps its meaning. A retired identifier is not reused; SI-151 is
the only one.

**Status.** Every requirement has one of four statuses, and a requirement that is not in
force carries its status in its heading, as in `SI-054 (deliberately unsupported)`. *In
force* is the default and is not written out. *Deliberately unsupported* means the drive
refuses the command on purpose; the requirement states what it answers instead, and
section 18.1 indexes all of them. *Out of scope* means the drive does not have the
capability the requirement describes; section 19 indexes those. *Retired* applies to
SI-151 alone, whose number is not reused.

**Typography.** `code` marks a command, an identifier, a file name or a path. A byte is
`$hh`, an answer on the error channel is `NN,TEXT` as the C64 reads it, and a character
the C64 sends as a number is `CHR$(n)`. A citation is the register code of section 1.1
followed by the place in that source, as in `HD 9-15`, `SD file_open()` or `SDM
d64_set_attrib()`.

### 1.5 Conformance and traceability

An implementation conforms when, for every requirement that is in force, the tests that
name it pass, and each of those tests fails when the behaviour the requirement states is
reverted. The second half is what makes the first half worth anything, and it is the rule
every change to this drive is held to.

The tests name the requirement they check: a `Suite11` case is called after it, a parser
case carries it in a comment, and a hardware check puts it in the label a run prints.
Appendix E lists the mapping, which `tools/softiec_traceability.py` generates from the
test sources, so a requirement that no test names shows up there rather than being
believed. Section 17 says which layer a requirement belongs in and why.

**Changing a requirement.** A change to what the drive does is a change to the statement
here, to the tests that name it, and to the firmware, in one piece of work. A statement
that no longer matches the firmware is a defect in this document, not a description of an
exception. Where a change makes the drive answer differently from one of its sources, the
requirement says which source and why, and section 18.1 indexes it. Nothing is settled by
a note that behaviour drifted.

---

### 1.6 Terminology

These terms are used throughout with one meaning each. Where a requirement defines the
term, it is named.

| Term | Meaning |
| --- | --- |
| drive | The Software IEC drive: one device on the serial bus, with one device number (SI-001), serving files from the Ultimate's file systems rather than from a disk of its own. |
| host file system | The file system the Ultimate itself uses, FAT on a card, a stick or the internal flash, reached through the Ultimate's virtual file system. |
| medium | Whatever a directory ultimately sits on: a host file system, or a CBM disk image mounted as a directory (SI-003). A requirement says "medium" where the answer is the same for both. |
| image | A `.d64`, `.d71`, `.d81` or `.dnp` file holding a Commodore disk, which the Ultimate mounts as a directory so that a partition or a directory can sit inside it (SI-003). |
| partition | One of the drive's numbered areas, 1 to 255, each with a name, a root and its own current directory (SI-002). Partition 0 names the current partition and cannot be entered. |
| current directory | The directory a partition is in, which every command that takes no path acts on, and which survives a warm reset (SI-103). |
| entry | One item a directory holds, whether a file or a directory, as a listing shows it. |
| CBM name | The name a C64 program uses, up to 16 bytes of PETSCII, which may hold bytes a host file system cannot (section 14). |
| host name | The name the same entry has on the host file system, produced from the CBM name by the mapping of SI-140 to SI-148. |
| channel | One of the 16 addresses a C64 opens on the drive. Channel 15 is the command channel; 0 to 14 are data channels, which carry a file, a listing or a direct access buffer. |
| command channel | Channel 15: it takes commands and answers the error channel status, `NN,TEXT,TT,SS` (section 4). |
| error channel | What a read of the command channel returns: the answer to the last command (section 4). |
| splat | The `*` a listing puts in front of the type of an entry whose write never closed it (SI-132). |
| lock | The flag `L` turns over, shown as `<` after the type, which stops a scratch (SI-076). |
| record | One fixed-length unit of a relative file, addressed by number with `P` (section 8). |
| buffer | The 256 bytes a direct access channel holds, opened with `#` (SI-090). |
| terminator | The `CHR$(13)` a `PRINT#` appends to a command, which is not part of the command (SI-016). |
| pattern | A name holding `*` or `?`, matched as SI-136 states. |

---

## 2. The model

### 2.1 Device, partitions, directories

**SI-001.** The drive presents one IEC device number, configurable from 8 to 30, with
11 as the default, which is the range and default of the configuration item
(`CFG_IEC_BUS_ID` in `U software/io/iec/iec_drive.cc`). Test:
`Suite11-SI001-DeviceNumberRange`. GUG confirms that C64 OS expects storage devices in 8 to 30 and
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
Mode partition are shared between all directories within that partition". Test:
`Suite11-SI004-BlocksFree`, where the root and a subdirectory of a native image report
the same count.

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

**SI-011.** In `CD`, the colon is optional when the name is preceded by a slash.
Source: HD 9-19, "It is not required that you include the colon before the
subdirectory name, as long as the subdirectory name is preceded by a slash." The HD
states it for `CD` only. An OPEN name without a colon is a plain name, as in
`SD file_open()`, and `MD` and `RD` require the colon (SI-060, SI-063).

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
parsed, and a carriage return followed by a line feed at the end is removed as a pair.
CBM DOS also truncates a command at a carriage return that is its second to last byte,
whatever follows it; this drive does not (the difference below). ROM, at `$C2B3`,
reached from the command dispatcher at `$C160`:

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

**SI-017.** The binary Change Partition command and `U0>` are exempt from SI-016,
because their parameter byte is mandatory and cannot be a terminator: `C`+shifted `P`
followed by a 13 selects partition 13, and `U0>` followed by a 13 selects device 13
(SI-100), with or without a terminator after it. `SD parse_user()` reads the byte of
`U0>` without a length check, and the ROM reads it at `$0203` regardless of the length.

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
| wildcard, no `@` | `33` | 1541: "Pattern matching characters cannot be used in the Save command or when Opening files for the purpose of Writing new data"; `SDM file_open()`, which SDU does not do |
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

`SDM file_open()` produces the same four answers, by a different route for the last
row; SDU gives the same three but refuses no wildcard on create:

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
`01, FILES SCRATCHED,00,00`, with the space in front of the text that every CBM drive
sends for the codes below 20, as it sends `00, OK`. Sources: HD B-1, "01 FILES SCRATCHED (not an error).
The number of files scratched will be indicated in the track variable";
`SD parse_scratch()`, which ends unconditionally with
`set_error_ts(ERROR_SCRATCHED,count,0)`; IDE 15.2.2, which shows the count in the
track field.

**Difference from the CMD manuals.** A scratch whose path does not
exist answers `71,DIRECTORY ERROR` with the partition number, instead of
`01, FILES SCRATCHED,00,00`, so a mistyped path is reported rather than looking like an
empty directory. sd2iec also reports the path error (`SD parse_scratch()`).

**SI-034.** Selecting a partition that does not exist answers
`77,SELECTED PARTITION ILLEGAL`. Source: HD B-5.

**SI-035.** Opening a file for writing when one of that name exists, without `@`,
answers `63,FILE EXISTS`. Opening for reading when none exists answers
`62,FILE NOT FOUND`. Opening an existing REL file with a non-REL type answers `64`.
Sources: HD B-3 and B-4; `SD file_open()`. Covered by Suite5 of
`software/test/iecdrive/testdrive.cc`.

A name is one entry whatever its type, as on a CBM drive, although a host file system
keeps `NAME.SEQ` and `NAME.PRG` apart. So a write of a name that exists as another type
answers `63` without `@`, and `64` with `@`, which is the ROM's check at `$D8F5`
(SI-032) applied to a literal name as well as to a pattern; and a read of an existing
REL file under another type, `LOAD` included, answers `64`. Test:
`Suite11-SI035-TypeOfExistingName`.

### 4.2 The error codes that are not CBM DOS errors

**SI-036.** `69,FILESYSTEM ERROR` is an Ultimate code with no counterpart on any
other device. It must not be the answer to a condition that CBM DOS names.
`U IecDrive::set_error_fres()` maps the unmapped `FRESULT` values to it and puts the
raw `FRESULT` in the track variable. Every path that can reach a user must map to a
documented code first. The known case is SI-083.

`78,BLOCK ACCESS DENIED` is the other Ultimate code. It answers a block command on a
partition rooted in a host directory, which has no tracks or sectors (SI-091), with the
track and sector asked for. No CBM device has a medium without sectors, so no CBM code
names the condition. sd2iec uses 78 for `BUFFER TOO SMALL`, which this drive never
answers (SI-096), and answers `20,READ ERROR` for a sector it cannot reach. The table in
`U iec_drive.cc` also holds 75 and 76, which nothing answers.

**SI-154.** Reading the command channel clears the error it reported: a second read, with
no command between the two, answers `00, OK,00,00`. Every Commodore drive does this, and a
program that polls the status after each command relies on it, because an error left
standing would be read again as the answer to the next command. The drive clears the
status where it renders it, in `U IecCommandChannel::get_error_string()`. The answer to a
reset is the one exception a caller sees, because `UI` and the two `UJ` forms set `73`
again after the read that cleared it (SI-103). Test: `Suite11-SI154-StatusClears`.

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
| 3-18 | partition name as shown in the partition directory, padded with shifted spaces (`$A0`) as a CMD partition directory and `SD parse_getpartition()` pad it; all zero for a partition that does not exist |
| 19-21 | start address, high to low |
| 22-26 | reserved, `CHR$(0)` |
| 27-29 | size, high to low |
| 30 | `CHR$(13)` |

Bytes 19-21 and 27-29 are counted in 512-byte blocks on the HD and the FD and in
256-byte blocks on RAMLink (RL, same page). This drive follows the HD and uses 512. The
size is the size of the image file for a partition rooted in a disk image, and the size of
the volume for one rooted in a host directory, rounded up and clamped to `0xFFFFFF`,
which is what `SD parse_getpartition()` answers with. A D64 that carries error bytes is
therefore a few blocks larger than the sectors its file system holds. Tests:
`Suite11-SI041-PartitionSize`, `Suite11-SI041-NamePadding`.

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

**SI-054 (deliberately unsupported).** `V[n][:]` validates. **Deliberately unsupported**; the drive answers `31`,
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

**SI-055 (out of scope).** The 1581-style sub-partition commands `/[n]:name` and
`/[n]:name,`+`CHR$(st)CHR$(ss)CHR$(sl)CHR$(sh)`+`,C` are not implemented and are out
of scope. They address 1581 emulation partitions, which this drive does not have.
They answer `31,SYNTAX ERROR`, because `/` is not a command letter and SI-030 gives that
answer for anything that is not one. Sources: HD 9-9 and 9-11; 1581 User's Guide, which
gives the same syntax. Test: `Suite11-SI055-SubPartitions`.

---

## 6. Directory commands

**SI-060.** `MD[n][path]:name` creates a directory. A colon is required; without one,
or with nothing after it, the answer is `34`. A name that begins with a shifted space
answers `34`. A name with a wildcard answers `33`, as a file to be created does (SI-032),
and so does a name that ends in a dot or a space, which a FAT host drops from the name it
creates, so that the directory could not be found again under the name it was made with
(SI-141). Sources: HD 9-17, which states the colon rule as its first guideline;
`SDM parse_mkdir()` for the shifted space, a check SDU does not make; `SD parse_error()`,
which answers 33 for the name FatFs refuses. Tests: `Suite11-SI060-MdColon`,
`Suite11-SI063-RdOnlyDirectories`.

**SI-061.** `CD[n]{<-|[path][:]name}` changes directory, per SI-010 and SI-014. A
`CD` with nothing to change to, `CD` alone or `CD:`, leaves the directory where it is and
answers `00, OK`. `SD do_chdir()` answers 39 there; the no-op is kept because a program
that sends `CD:` means "stay here", and answering it with an error would fail that
program for no benefit.

**SI-062.** `CD` into a file whose name has a disk image extension mounts that image
and makes its root the current directory; `CD<-` from the root of a mounted image
unmounts it. Sources: SD README, "CD is also used to mount/unmount image files";
GSD "Mounting a Disk Image".

**SI-063.** `RD[n]:name` removes a directory, and nothing else: a file reached by the
same name, a disk image or a host file without an extension, answers `62,FILE NOT FOUND`
and stays, as `SD parse_rmdir()` matches only directories. A colon with nothing after it
is no name and answers `34`, so the directory the drive stands in is not removed from
under it. It takes no path: a `/` anywhere in the
command answers `34`. It refuses a directory that is not empty. Sources: HD 9-19,
"This command does not allow the use of paths in order to avoid problems with
removing a subdirectory which is a parent of the directory in which you are located";
`SD parse_rmdir()`, which rejects any `/` with `ERROR_SYNTAX_NONAME` and answers
`63,FILE EXISTS` for a directory that still has entries. Tests: `Suite11-SI063-RdNoPath`,
`Suite11-SI063-RdOnlyDirectories`.

**SI-064.** `R-H[n][path]:newname[,id]` renames the header of a directory. The name is
at most 16 characters and the id two. Sources: HD 9-15; IDE 15.6.5;
`SDM parse_set_header(3)`. The path selects the directory and the name behind the colon
is the new header; with no path it is the current directory of the partition. The name
follows a colon, as for `N`: without one the answer is `34`, so a command whose partition
digits would otherwise become part of the name renames nothing. An empty
name answers `34`, a name with a wildcard `33`, and a path that is not there
`71,DIRECTORY ERROR`. A name longer than sixteen characters keeps its first sixteen
wherever a header shows it. A directory on a host file system is renamed, and so is refused
what `R` refuses a directory: a name ending in a dot or a space answers `33` (SI-141), and a
name another entry lists under `63` (SI-074).

**Difference from the CMD manuals.** The id is an sd2iec extension. HD 9-15 gives the
syntax as `R-H[n][path]:newname` and names three arguments, the partition, the path and
the new name, so a CMD HD changes the id only when a disk is formatted.
`SDM parse_set_header()` takes an optional id after a comma and sets it, and the sd2iec
spellings `EH`, `XH` and `D` reach it (SI-077). This drive takes the id on all four
spellings, so a program written for either device gets what it asks for, and a command
without an id leaves the id and the DOS version as they are, which is what the CMD
manuals describe.

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
`00, OK`. It is an Ultimate extension, which precedence rule 5 (section 1.2) allows: no
CMD or sd2iec command has that spelling, and nothing else answers the question. The path
is the host spelling of the directory, upper cased, so a component whose CBM name holds
a byte the host escapes shows the escape (SI-141). It reports where the drive stands;
a program that wants to go back there sends the `CD` it went there with.

---

## 7. File commands

### 7.1 Open

**SI-070.** The type and access suffixes are `,P`, `,S`, `,U`, `,L`+`CHR$(rl)` and
`,R`, `,W`, `,A`, `,M`. Secondary address 0 reads and secondary address 1 writes,
whatever access the name asks for, and both take PRG when the name gives no type; any
other secondary address reads when the name gives no access, and a write there takes SEQ
when the name gives no type. Sources: HD 9-23 to 9-31 for `,P`, `,S`, `,U`, `,L`, `,R`,
`,W` and `,A`; `SD file_open()` for `,M` (`case 'M': /* Modify */`, which HD does not
document) and for "Force mode+type for secondaries 0/1", which sets the mode
unconditionally and the type only where none was given; `U setup_file_access()` applies
it. Test: `Suite11-SI070-SecondaryForcesMode`.

`,M` opens the file for reading. A modify reads a file that a write never closed, which
a listing marks with a splat (SI-132), and nothing here refuses to read such a file. The
two sd2iec lines differ: SDM maps modify to read for the same reason, while SDU opens the
file read and write on a FAT file system and says in its own comment that this differs
from the original hardware. This drive follows SDM here, because reading is what the
command means on a Commodore drive and because a write through `,M` would be a second
way to write a file that `,W` and `,A` already cover. Test: `Suite11-SI070-ModifyOpen`.

**SI-071.** `N[n]:name[,id]` creates or formats a disk image, as sd2iec does, because
this drive has no formattable medium of its own. Source: SD README under `N:` and
`SD fat_format_image()`:

* If the name ends in a known image extension and no such file exists, an image of
  that format is created. `.D64` and `.D41` mean a 1541 image, `.D71` a 1571, `.D81`
  a 1581, `.DNP` a native partition image. An id is required.
* For `.DNP` the id is a three digit track count and the image is
  `65536 * tracks` bytes, created but not formatted. This rule, the `63` for an existing
  DNP below and ids of two or three characters come from SDM (`SDM fat_format_image()`);
  SDU refuses to create a DNP, answering `62`, and requires an id of exactly two
  characters.
* If the name has no known image extension, `.D64` is appended, and in that case an
  existing file is **not** overwritten: the answer is `63,FILE EXISTS`.
* If the file exists and the extension was given explicitly, it is formatted, except
  that an existing `.DNP` is refused with `63` whether or not the extension was given
  (`SDM fat_format_image()`, `if (ext == NULL || imagetype == IMG_IS_DNP)`; the README
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
to the deepest directory that still exists. The SDU README says `N:` is ignored for a DNP
image unless the current directory is its root; this drive formats the image from any
directory in it, because the program asked for its disk to be formatted wherever it
stands.

**SI-072.** A PRG file whose name ends in a disk image extension, or in `.CRT` or
`.TCRT`, is written to the host file system under exactly that name, with no type
extension added. A file of another type with such a name gets its type extension like any
other name. Sources: `SD should_save_raw()`, which `SD fat_open()` consults only for
`TYPE_PRG`; SD README, "PRG files that have D64, D41, D71, D81, DNP or M2I as an
extension will always be written without an x00 header and without any additional PRG
file extension." A file written raw lists under its host name with the type an unknown
extension lists as (section 14.1). Test: `Suite11-SI072-RawNames`.

### 7.2 Scratch, rename, copy

**SI-073.** `S[n][path]:pattern[,[n][path]:pattern...]` scratches. Each element gets
its own partition and path, and the list is as long as the command makes it (SI-150).
Directories are skipped. The answer is
`01, FILES SCRATCHED,<count>,00` with the count in the track variable, per SI-033.
Sources: HD 9-27; `SD parse_scratch()`; IDE 15.2.2, whose examples include
`@S/STUFF/:*=OLD,*=BAK,/STUFF/BAK/:*`. Test: `Suite11-SI150-NameLists`.

**SI-074.** `R[n][path]:newname=[[n][path]:]pattern` renames a file or a subdirectory.
When the source path and the destination path differ, the entry moves. An empty new
name answers `34`. A new name that already exists
answers `63`, unless it differs from the old name only by case and the entry stays in its
directory; an entry that moves meets the names of the directory it moves into, its own
name included. A destination path that does not exist answers `71,DIRECTORY ERROR` with
the partition number. A wildcard in the new name answers `33`, and so does a name ending
in a dot or a space for a directory, for the reason SI-060 gives. Tests:
`Suite11-SI074-RenameChecks`, `Suite11-SI074-MoveChecks`. Sources: HD 9-26, whose section is headed "Renaming Files and
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
as well. An x00 file (SI-144) moved this way takes the host name of its new CBM name (SI-144c). A
subdirectory is renamed under its own name: `U do_rename()` resolves the source with
directories allowed, and `CreateIecName()` reports `e_folder` for one, so the
destination is built without a file type extension. See C14.
The directory half was reported on 917 as `@"MD:A` followed by `@"R:B=A` answering
`62,FILE NOT FOUND`, and the reporter's `finit` hits it when it renames `OS` through a
temporary name.

**SI-075.** `C[n][path]:new=[[n][path]:]name[,[[n][path]:]name...]` copies, and with
more than one source appends them into the target. Path parsing restarts for every
source, and the list is as long as the command makes it (SI-150). The target takes the
file type of the first source. A relative file copies its records: the target has the
record length of the first source, written once, and each source adds its records
without its own header, whatever its layout (SI-084). Relative files mixed with other
types, or relative files of different record lengths, answer `64,FILE TYPE MISMATCH`
and leave no target. Sources: HD 9-28, which caps the
sources at five; SD README under `C:`, which has no cap; `SD parse_copy()`, whose
`savedtype` takes the type from the first source (the README does not state the type
rule), which copies a relative file through `open_rel()` record by record and refuses to
mix it with other types with `ERROR_FILE_TYPE_MISMATCH`. Tests: `Suite11-SI150-NameLists`,
`Suite11-SI075-CopyRelative`.

**SI-076.** `L[n][path]:name` toggles the lock flag on one file or directory. A
locked file lists with `<` after its type and cannot be scratched; a locked directory
cannot be removed. Sources: HD 9-30; IDE 15.2.4; `SDM parse_lock()`, which toggles
`FLAG_RO`. The reporter raised it on #877.

`L` finds its entry through the directory, as a
scratch does, so it locks the entry a listing shows under that name rather than the
first host file matching `NAME.???`. On a host file system the lock is the FAT read-only
attribute, which the Ultimate's FTP server and file browser then also respect; in a disk
image it is bit 6 of the file type byte. A scratch skips a locked entry, and also an
unlocked entry of the same name and type that follows a locked one in a disk image,
because deleting by name removes the first entry of that name.

SDBUGS reports that a scratch of a locked file inside an image is carried out rather than
refused, and expects an error. This drive skips the entry and answers `01, FILES
SCRATCHED` with a count of none, which is what a 1541 answers and what SI-033 gives for a
scratch that matched nothing. An error code would tell a program that something failed
where a Commodore drive reports that nothing was scratched.

**SI-077.** The sd2iec spellings of the attribute commands, from
`SDM parse_elock()`, `parse_eunlock()`, `parse_ehide()`, `parse_attr()`,
`parse_set_header()` and the dispatch in `parse_doscommand()`.

**These spellings come from SDM only.** The CMD manuals document `L` (HD 9-30) and
`R-H` (HD 9-15) and no other attribute command, and none of the five functions above is
in SDU, which has no lock command at all: it reads the type byte's lock bit for a listing
and sets it from the FAT read-only attribute. `EL`, `EU`, `EH`, `A` and `XH` therefore
rest on SDM alone, as do the optional id of SI-064 and the image lock of SI-077a. What each of them does to a medium has a second source: the lock is the
type byte bit CMD documents (SI-076), and the image lock is the header's DOS version
byte, which a Commodore drive obeys by refusing to write with error 73 (1541).

| Command | Effect |
| --- | --- |
| `EL[n][path]:name[,name...]` | set the lock on every entry each name matches |
| `EU[n][path]:name[,name...]` | clear it on every entry each name matches |
| `EH[n][path]:name`, with something other than a colon after the partition number | turn the hidden flag of one entry over |
| `EH[n]:name[,id]`, `XH[n][path]:name[,id]`, `D:name[,id]` | set a directory header, which reaches the same place as `R-H` here (SI-064); all four take the optional id |
| `A:[R][H][A]=name[,name...]` | set exactly the attributes named on every entry each name matches, and clear the others |

The lock these set and clear is the one `L` turns over (SI-076), so a file `EL` locks is
a file `L` unlocks and a file a scratch skips. `R` in `A` is that lock, `H` is the hidden
flag of SI-134 and `A` is the archive flag, which the drive stores and nothing reads.

Two differences from the source are deliberate. `SDM parse_elock()` and `parse_eunlock()`
skip directories and `parse_attr()` acts on the first match only; here `EL`, `EU` and `A`
act on every entry the name matches, directories included, because `L` locks a directory
(HD 9-30) and a lock that `EL` and `L` disagreed about would be two locks. `EH`, like `L`,
turns over the flag of the one entry the name finds first. A name that
matches nothing answers `62,FILE NOT FOUND`, and a medium that does not carry the
attribute, such as the hidden flag inside a CBM disk image, answers `30`.

**SI-077a.** `EL:$` write protects the disk image the directory is in, and `EU:$` lifts
the protection. The lock is the DOS version byte, byte 2 of the header sector: `EL:$`
writes `$3C` into a D64 or D71, `$3D` into a D81 and `$3E` into a DNP, and `EU:$` writes
the format's own `$41`, `$44` or `$48` back. A header whose byte is below `$40` and not 0
write protects the image, whoever wrote it, so an image locked elsewhere arrives locked.
While it is, every write into the image answers `26,WRITE PROTECT ON` and changes
nothing: a file open for writing, appending or replacing, a relative file, which opens for
reading and writing, a scratch, a rename, `EL` and `EU` of an entry, `N`, `R-H`, `U2`,
`B-W`, `B-A` and `B-F`. Reads are unchanged. On a host directory, which records no such
lock, `EL:$` and `EU:$` answer `30`. Sources: `SDM d64_set_attrib()` for the command and
the values, and `SDM d64_mount()`, which marks an image read only on the same test; 1541,
error 73, for a Commodore drive refusing to write a disk whose version byte is not its own,
which is why the lock travels with the image. The protection belongs to the image's file
system, so the file browser, FTP and REST obey it as well, and a drive emulation that runs
the Commodore DOS refuses to write the disk. Test: `Suite11-SI077-ImageWriteLock`.

`XH+` and `XH-`, which turn hidden files on and off for every later listing, are one of
SDM's settings commands, which section 19 places out of scope; the filter `=H` asks
for them per listing instead. They answer `30`.

`D` is a command letter because of `D:`, so an unrecognised argument to it answers `30`
under SI-030. That covers the sd2iec direct sector commands `DI`, `DR` and `DW` of SI-096.
`A` followed by anything other than a colon, and `A:` without an `=`, answer `31`, as
`SD parse_attr()` answers them; a lone `A` is the reporter's SI-031 case, which answers
the same.

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

The record length is the byte after `,L,`, whatever byte that is, a comma or a colon
included, as `SD file_open()` reads it. A record past the end of the file is not there:
positioning to it grows nothing, and reading it gives one byte of 255 and answers `50`
without moving on, so reading it again answers the same, as `SD fat_file_seek()` does.
The file grows when a record past its end is written: a last record the file ends inside
is completed, every record between it and the one written is an empty record whose first
byte is 255, as a 1541 fills them, and the record written follows. A write protected drive
(SI-102) grows nothing because it writes nothing. Tests: `Suite11-SI080-PastTheLastRecord`,
`Suite11-SI080-RecordLengthBytes`.

A byte offset given with a record past the end is not kept, so the record written there
starts at its first byte, and an offset past the record length answers `51` whether or not
the record is there. `SD fat_file_seek()` clamps the position to the empty record it
returns, and `SD parse_position()` checks the offset before it seeks.

**SI-081.** The channel byte of `P` is masked to its low four bits from 19 up, so both
the documented BASIC form `PRINT#15,"P"CHR$(96+ch)` and the bare byte reach the same
channel, while a byte just outside the channel range is still refused. A byte that names
no data channel, and a channel with nothing open on it, answer `70,NO CHANNEL`. Sources:
`SD parse_position()`, `find_buffer(command_buffer[1] & 0x0f)`, which masks
unconditionally and answers `ERROR_NO_CHANNEL` when it finds no buffer; the ROM masks from
19 and answers 70 at `$E207`. Test: `Suite11-SI081-PositionChannel`.

**SI-082.** `P` also positions inside a plain file, to a 32-bit little-endian byte
offset, with the missing high bytes taken as zero. Sources: SD README under `P`;
GSD "Positioning (seeking) Within a File"; IDE 15.1.1, which documents both the
four-byte form and `F-P`. `U do_set_position()` implements it for `e_file`. A file read
to its end is still open and positions again. An offset past the end of a file open for
reading answers `00, OK` and the next read gives nothing; `SD fat_file_seek()` answers
`50` there, which this drive keeps for relative files, where the record structure makes
the difference meaningful. Test: `Suite11-SI082-PositionAfterEnd`.

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
a standard buffer is allocated instead, and so it is for `##0` and for `##` followed by
anything that is not a digit, which ask for no chain at all. `SD open_buffer()` opens
nothing for `##0` and computes a count from any byte; a standard buffer is the answer that
leaves the program a working channel. Sources: SD README "Large buffers", which is the
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

The long spellings the 1541 manual prints, `BLOCK-READ`, `BLOCK-WRITE`, `BLOCK-ALLOCATE`,
`BLOCK-FREE`, `BUFFER-POINTER` and `BLOCK-EXECUTE`, are the same commands: the letter after
the dash names the command, as the 1541 ROM and `SD parse_block()` read it. On a partition
rooted in a host directory, which has no tracks or sectors, `B-R`, `B-W`, `U1`, `U2`,
`B-A` and `B-F` answer `78,BLOCK ACCESS DENIED` with the track and sector asked for
(section 4.2). A partition rooted in a disk image serves them.

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
byte the drive can give out and answers `30`. So does `P` on a buffer channel whose
32-bit position (SI-082) is past that end, and the pointer stays where it was. Test:
`Suite11-SI090-BufferPointer`.

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

**SI-096 (out of scope).** The sd2iec direct sector commands `DI`, `DR` and `DW`, and the error
`78,BUFFER TOO SMALL`, are out of scope. They expose the raw storage device below the
file system, which is not something this firmware should offer over IEC. They answer
`30,SYNTAX ERROR`: `D` is a command letter here, for the header command of SI-077, and
anything after it that is not a colon is refused. Source: SD README under `D`. Tests:
`DI`, `DR` and `DW` in `target/pc/linux/parse`.

---

## 10. Device commands

**SI-100.** `U0>`+`CHR$(d)` changes the device number, for `d` in 8 to 30. Sources:
HD 9-49; IDE 15.4.1; `SD parse_user()`, which accepts 4 to 30 and recognises the `>`
by `(command_buffer[2] & 0x1f) == 0x1e`. GAP lists this as a gap: "Changing the
device address with u0> command. This is supported on all devices going back to
ancient times, before the 1541. This produces a syntax error in SoftIEC."

The number is written into the IEC processor's
device number slot without holding the processor in reset, because the command is still
on the bus, and it is not written to the configuration. The number byte is mandatory, so
a 13 in its place is device 13 with or without a terminator after it (SI-017). Tests:
`Suite11-SI100-DeviceNumber`, `Suite11-SI103b-SettingAfterU0`.

**SI-100a.** `M-W`+`CHR$(119)`+`CHR$(0)`+`CHR$(n)`+data, a memory write to `$0077` with
`n` at least 1, changes the device number to the low five bits of the first data byte,
for a number in 8 to 30, as `U0>` does (SI-100). The number is not written to the
settings. A number outside 8 to 30 answers `30` and leaves the device number unchanged.

A 1541 listens on the address in `$0077` and talks on the address in `$0078`. Its reset
stores `$48` plus the device jumpers in `$78` and that value EOR `$60` in `$77` (ROM
`$EB45` and `$EB49`), and its ATN handler compares a command byte with `$78` at `$E89B`
and with `$77` at `$E8A9`. Writing both cells is therefore the software way to change a
1541's device number, and it is what the SWAP button of a CMD drive sends to the drive
whose number it takes. The CMD FD asserts ATN itself and sends that drive LISTEN,
secondary address `$6F`, and then `M-W` `$77 $00 $02` followed by the FD's own listen
address and talk address, the last byte with EOI, and UNLISTEN (FDROM `$A57B` to
`$A5A6`, with the command bytes in a table at `$A5A8`). The swap back sends the same
command to the swapped number with the other drive's original addresses. HD 3-1
describes the SWAP buttons exchanging the HD's number with a 1541 at device 8. Issue
[#933](https://github.com/GideonZ/1541ultimate/issues/933) asks for Software IEC to take
part in that exchange. Sources: ROM as above; FDROM; `SD handle_memwrite()`, which
treats address 119 as "Change device address, 1541 style" and takes
`command_buffer[6] & 0x1f`.

Difference from the 1541 and sd2iec. The talk address in the second byte is not read:
the drive has one number, and the listen address gives it. A 1541 stores each byte where
it is written, so a write of `$0077` alone leaves it talking on its old number, where
here the whole drive moves. A number outside 8 to 30 answers `30`, the range of SI-100,
where `SD handle_memwrite()` takes any value. A write to `$0078` alone is an `M-W` like
any other and answers `30` (SI-105). Tests: `Suite11-SI100a-MemoryWriteDeviceNumber`,
which sends the FD's bytes on channel 15 without an OPEN, and `iec-dos-commands`, which
sends them over the bus from the C64 and addresses the drive on its new number.

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
| the twelve command handlers that change a medium | `MD`, `RD`, `C`, `N`, `R`, `S`, `R-H`, `R-P`, `L`, `EL`, `EU`, `EH`, `A`, `U2`, `B-W`, `B-A`, `B-F` |
| the file open | a write, an append and a replace, which answer `26` and open nothing |
| the sequential write | a channel opened for writing before `W-1` takes no more bytes, writes nothing more when it closes or is positioned with `P`, and answers `26` |
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

None of the three clears the write protect of `W-1` (SI-102) or returns the device number
`U0>`, `S-8` or `S-9` set (SI-100, SI-101): both last as long as the drive runs, and a
reset over the bus is not the drive stopping. `U`+shifted J on sd2iec is
`system_reset()`, which restarts the device and so loses both; here the drive's own Reset
returns the device number and keeps the write protect (SI-103b). Sources: HD 9-51; SD README under `UI/UJ` and `U<Shift-J>`; GSD
"Warm, Cold and Hard Reset".

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
not do it, and neither must a setting change after `U0>` moved the drive, which leaves it on
the number `U0>` gave. The drive's **Reset**, from the menu or from
`PUT /v1/drives/softiec:reset`, is meant to drop whatever is on the bus: it restarts the
processor in any case and puts the drive back on the device number the settings hold; the
write protect of `W-1` stays. Tests: `Suite11-ResetRestartsProcessor`,
`Suite11-OperationLogNoReconfigure`, `Suite11-SI103b-SettingAfterU0`.

**SI-104.** `U3` to `U8` and `UC` to `UH` jump into drive memory and are not
implemented; they answer `30`, for the same reason as SI-095. Source: HD 9-51.

**SI-105 (deliberately unsupported).** The memory commands are implemented as follows.

* `M-R`+`CHR$(lo)`+`CHR$(hi)`[+`CHR$(n)`] returns `n` bytes over the error channel,
  with `n` absent meaning 1 and `n` zero meaning 256. It does not read past a page
  boundary. Sources: HD 9-46 and 9-47 for the syntax, zero meaning 256 and the page
  boundary; ROM `$CB24` (`LDA $0274 / CMP #$06 / BCC`, one byte when the command is
  shorter than six bytes) and `SD handle_memread()` ("Read 1 Byte if no explicit
  length was provided") for the absent count. HD does not describe the absent case.
* `M-W`+`CHR$(lo)`+`CHR$(hi)`+`CHR$(n)`+data answers `30` and keeps nothing; the reason
  is below. The exception is a write to `$0077`, which changes the device number
  (SI-100a). Source for the syntax: HD 9-47.
* `M-E`+`CHR$(lo)`+`CHR$(hi)` answers `98,UNKNOWN DRIVE CODE` and runs nothing; the reason
  is below. Source for the syntax: HD 9-48.

A reply of `M-R` belongs to the command that asked for it: the next command replaces it,
whether or not the reply was read, as it replaces a status (SI-154). Test:
`Suite11-SI105-UnreadReply`.

What `M-R` returns is specified in section 11. The reporter's position on #877 is
that the count matters more than the content: "M-R should return the number of
queried bytes. I agree we have nothing good to return but we can return each byte to
be 42... That way, a software that does M-R to identify devices is syntactically
happy." C64 OS sends the four probes at every boot (TRACE).

**`M-W` and `M-E` are deliberately unsupported.** Nothing of what `M-W` writes is kept
and `M-E` runs nothing, so answering `00, OK` would tell a fast loader that its drive
code is in place and running. An `00, OK` means the work was done, which is also why a
clock write of a date that does not exist answers `30` (SI-122). The reporter's request
was for `M-R`, which C64 OS sends four times at boot (TRACE).

`M-W` answers `30`: the command is recognised and its bytes are not taken. A write to
`$0077` is not drive code: it asks for a change this drive can make, so it makes it and
answers `00, OK` (SI-100a). `M-E` answers
`98,UNKNOWN DRIVE CODE`, which says what is true here and is the answer sd2iec gives when
the code it was sent matches no fast loader it implements: `SD run_loader()` sets
`ERROR_UNKNOWN_DRIVECODE`, code 98, when it reaches the end of its handler table. No
drive code is ever known here, so every `M-E` answers it. A program that meets this code
on an sd2iec meets it here for the same reason. `98` is not a CBM DOS code, and no CMD
device answers it; a program that treats any non-zero code as a refusal is unaffected.

**SI-106 (out of scope).** `S-C`, the SCSI pass-through of HD 9-39, is out of scope.

**SI-107.** The setting "IEC Drive" in "SoftIEC Drive Settings" has three values, which
decide the bus and the UCI side of the drive separately:

| Value | On the serial bus | UCI target (target 5) | `$DF1B`, the number a UCI KERNAL sends there |
| --- | --- | --- | --- |
| Enabled | answers | answers | the device number |
| UCI Only | does not answer | answers | the device number |
| Disabled | does not answer | answers every command with status `05`, module not loaded | 31 |

UCI Only keeps a KERNAL or program that reaches the drive through the Command Interface
working while a hardware fast loader has the bus to itself, and it shows whether a program
uses UCI and not the bus. With Disabled, a UCI KERNAL finds no device of its own, sends
every device to the bus, and meets `DEVICE NOT PRESENT` on the drive's number. `$DF1B`
holds five bits, and 31 is no device a program opens. UCI Only is the first value and the
default. The task menu's Turn On sets Enabled, and Turn Off sets UCI Only. Source: issue
[#918](https://github.com/GideonZ/1541ultimate/issues/918), where the three values were
proposed and agreed. Tests: `Suite11-SI107-SettingModes`, and `uci-targets`, which reads
`$DF1B`, the drive list and the target's answer for each value.

---

## 11. Device identification

This section exists because it is the one place where copying another device exactly
would be wrong.

**SI-110.** The `UI` message is the primary identification and must name this device.
Source: SD README, "If you are the author of a program that needs to detect sd2iec
for some reason, DO NOT use M-R for this purpose. Use the UI command instead and
check the message you get for 'sd2iec' and 'uiec' instead"; GSD "Device Detection". The
message is `73,U64HD ULTIMATE DOS V2.0,00,00`, whose fixed form SI-114 gives.

**SI-111.** While the drive answers `M-R` out of itself, as SI-112 has it, `M-R` must not
return the signature of a 1541, a 1571, a 1581 or a CMD device, because a program that
reads one of those will then drive this device as that model, and none of those models'
drive code runs here. The rule is about that mismatch, not about the signatures
themselves: a drive that served a real ROM image and behaved as that model, which is what
GEOS and Wheels need and what SI-115 places out of scope, would answer the signature on
purpose. Adding such a mode replaces this requirement rather than breaking it. The
signatures are, read out of the ROM images in this repository:

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

**SI-112.** `M-R` returns the requested number of bytes, up to the end of the page
(SI-105), every byte `$00`, at every address. There is no address table and no exception.

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

**SI-115 (out of scope).** The sd2iec `XR` mechanism, which serves a real drive ROM image from a file
for `M-R` so that GEOS and Wheels can identify a drive, is out of scope. Sources:
SD README under `XR` and under GEOS and Wheels; GAP does not ask for it. The
reporter asked on #877 that the documentation state plainly that there is no GEOS and
no Wheels support, which belongs in `GideonZ/1541u-documentation` rather than here.

Out of scope means this document does not require the mechanism and no test holds the
drive to it. It does not mean it cannot be added; SI-111 says which requirement such a
mode would replace.

---

## 12. Real time clock

**SI-120.** `T-RA`, `T-RB`, `T-RD` and `T-RI` read the clock and `T-WA`, `T-WB`,
`T-WD` and `T-WI` set it. The `A` format is
`"dow. mo/da/yr hr:mi:se xM"+CHR$(13)` with the day of week four characters followed
by a space, from `SUN.`, `MON.`, `TUES`, `WED.`, `THUR`, `FRI.`, `SAT.`. The `B` and
`D` formats are nine bytes: day of week, year, month, day, hour in 12-hour form,
minute, second, an AM or PM flag, and `CHR$(13)`, BCD-coded for `B`. The year byte of
`D` counts from 1900, so 2026 is 126, as `SD parse_timeread()` writes it; a write takes a
two-digit year as well, by the rule of SI-121. The `I` format
is the ISO 8601 subset `"YYYY-MM-DDThh:mm:ss dow"+CHR$(13)`. Sources: HD 9-36 to
9-38; IDE 15.4.8; SD README under `T-R and T-W`; GSD "Realtime Clock".

The clock the drive answers with is its own, and a write changes nothing else. The drive
keeps the difference between the moment written and the system clock, in seconds, and a
read answers the system clock plus that difference, so the drive's clock runs on with the
system clock. The system clock is the Ultimate's real time clock, which the firmware sets
from the network by SNTP while the network setting "SNTP Enable" is on, as it is by
default (`software/network/sntp_time.cc`), in the time zone the network settings name.
The file browser, FTP, REST and the time stamps the file system writes keep reading the
system clock. A reset of the drive clears the difference: the Reset from the menu or the
drives route, `UJ` and `U`+shifted J. Until a write, and after a reset, a read answers the
system clock as it reads, its day of week included. A write that is refused answers
`30,SYNTAX ERROR` and keeps the difference as it was.

**SI-121.** Each write form carries the fields of the matching read form, at the same
offsets.

| Form | After `T-Wx` | Shortest accepted |
| --- | --- | --- |
| `A` | `dow. mo/da/yr hr:mi:se xM`, the day of week matched on its first two characters, the space and the marker optional | 26 bytes |
| `B` | day of week, year, month, day, hour in 12-hour form, minute, second, PM flag, all BCD | 12 bytes |
| `D` | the same eight fields in binary | 12 bytes |
| `I` | `YYYY-MM-DDThh:mm:ss`, with the day of week a `T-RI` answer ends in accepted and ignored | 23 bytes |

Source: `SD parse_timewrite()`, which is also the source for the rules below.

* The day of week of the `A`, `B` and `D` forms is checked for its range and not kept:
  a read after a write derives it from the date, because the drive keeps only a
  difference in seconds. A CMD drive stores the day of week it is sent without checking
  it against the date, so a program that sends one the date does not have reads back a
  different one here. The `I` form carries none.
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
check is added here because a 31 February would otherwise be read back as a different
date.

**SI-123.** Every field sits at a fixed offset and every separator is checked. A field
written to another width is refused, where `SD parse_timewrite()` reads a number of any
width and then skips one character. The `A` form's AM or PM marker is at a fixed offset
in that source as well, so a command whose earlier fields are of another width cannot be
read consistently in any case. A command is exactly as long as its form: the `A` form is
26 bytes without the marker and 29 with a space, `A` or `P`, and `M`; the `I` form is 23
bytes, or 27 with a space and the day of week a `T-RI` answer ends in. Anything else,
a marker that is not `AM` or `PM` included, answers `30`. `SD parse_timewrite()` ignores a
marker it does not recognise, which here would leave the clock twelve hours wrong for a
`PM` typed shifted. Test: `test_clock_commands` in `target/pc/linux/parse`.

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
SI-076 and the attribute is SI-134. The short time stamped format of SI-139 has no column
for the lock or the `H`: its stamp begins in the place they would take, as it does in
`SD createentry()`.

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
characters in the long format is the lock position of SI-132, and the second the `H`. The
splat stands in front of the type in both formats.

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

**Where the attribute lives.** A host file system records it, and this drive uses that
record. A CBM disk image has no field for it: the directory entry's type byte is defined
by CBM DOS, and a bit set there travels with the image to every drive, emulator and tool
that reads it. `EH` inside an image therefore answers `30`, the answer SI-077 gives for a
medium that does not carry an attribute, rather than writing a bit that only some drives
read. `SDM d64_set_attrib()` does write one: it puts the attribute bits into the type byte
of the directory entry, where bit 5 is a hidden flag CBM DOS does not define, and the
fork's author reports that upstream sd2iec dropped it for compatibility. An image this
drive writes is therefore listed the same way by any of them. Nothing in a listing or in
the name mapping depends on the choice, so it can be revisited if a report asks for
hidden files inside images, for example to keep a GEOS boot disk tidy.

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
the same in both. A stamp in a filter is refused with `30` when its marker is not `AM`
or `PM` or a field is out of its range: month 1 to 12, day 1 to 31, hour 1 to 12,
minute 0 to 59, since a field out of range would run into the bits of its neighbour in
the FAT time the filter compares. What the line they sit in looks like is SI-139. The two lines above are
the manual's examples as text extraction renders them, and that rendering drops spaces: it
puts the type two columns to the left of where a 1541 puts it, and the long format has one
space between the date and the time where three belong. SI-139 gives the column positions, taken from
the character positions in the scan rather than from the extracted text.

**SI-136.** Wildcard matching: `?` matches exactly one character and `*` matches any run
of characters, including none, wherever it stands in the pattern, as a shell glob does.
`*ED` matches `WALKED` and `MOVED` but not `EDIT`, and `W*D` matches `WALKED`. Characters
after a `*` are therefore matched against the end of the name, which is the 1581 rule and
sd2iec's default (`SD match_name()` with `POSTMATCH` set, SD README under `X*+/X*-`,
"the default value is enabled (+)"). Character classes such as `[A-Z]` are not supported.
A pattern is compared in full, so a pattern longer than sixteen characters matches a
name only as long as itself. `SD match_name()` stops after sixteen characters of name;
here a host name may be longer than sixteen characters and is reachable by its full name
(SI-141), which a cut at sixteen would lose. Test: `test_pattern_match` in
`software/io/iec/cbmdos_parser_test.cc`.

A letter matches in either ASCII case. A host name renders in upper case (SI-141), and a
client that sends a name in ASCII, a tool on a PC or a test harness, sends the lower case
letters `$61` to `$7A`; folding the case lets it find the name it sees. On a Commodore
those bytes are graphic characters, which a keyboard does not send for a letter, so the
only pattern this widens is one that holds a graphic character where a name has a letter.
Test: `Suite11-SI136-CaseFolding`.

The matcher is a full glob, so a second `*` matches in the middle of a name where CBM DOS
and `SD match_name()` stop at the first one. GAP notes the difference approvingly:
"SoftIEC even supports more than one * which the other devices do not." For one `*` it
agrees with sd2iec's default, and for more it narrows rather than widens a match; with the
case folding above, which widens it only for graphic characters, no command a Commodore
keyboard can type acts on more files than the other devices would. It is recorded so that
it is a known difference rather than an accident. It runs in time proportional to the product
of the two lengths, which matters because a command such as `S:****************Q` reaches
the matcher from the bus.

### 13.3 Raw directory

**SI-137 (deliberately unsupported).** `OPEN lf,dv,sa,"$"` with `sa` not 0 returns the raw directory sectors rather
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

Entries whose type is SEQ, PRG, USR, REL or DIR are listed. A closed DEL entry, which
directory art uses for its separator lines, and an entry of a type above DIR are not
listed, and a `CBM` partition entry of a 1581 image is listed as SEQ. `SD d64_readdir()`
lists DEL as `DEL`, CBM as `CBM` and the higher types as `???`; this drive has no DEL, CBM
or unknown type among the file types it serves, and the lines those would give are a
listing's decoration rather than files a program opens.

`FileInfo` carries the directory entry's type bits in `cbm_filetype`, which is zero on a
file system that has no CBM type. `U DirInCBM::get_entry()` fills it in, and
`U IecPartition::CreateIecName()` takes the type from it when it is set. The file browser,
the FTP server and the UCI target see a GEOS file with the extension `CVT`, the
interchange format the browser writes when it copies a GEOS file out to the host file
system; that extension does not reach the bus. The IEC read open passes
`FA_OPEN_FROM_CBM`, so `U FileInCBM::open()` reads the file's own chain rather than
building a CVT container. A VLIR file's chain is its record block, which is what a 1541
hands over. The type and the stream go together: the type alone would offer a `LOAD` a
stream it cannot run, and the stream alone would leave the type contradicting the disk.

The `C` command copies a GEOS file out of an image as a CVT container, because
`U IecCommandChannel::do_copy()` opens each source with a plain `FA_READ`. That keeps the
interchange format on a copy to the host file system, where it is what the receiving side
needs, and it is the one place where a copy and a read of the same file differ.

Measured against a reference 1541: VICE's `c1541` lists `geos-2.0r-cenbe.d64` as
`prg prg usr usr usr usr usr usr usr`, and so does this drive. `c1541` extracts
`DISK COPY` from `deskpack-plus-b.d64` as 4,335 bytes, and this drive hands out the same
4,335 bytes, beginning `$01 $08 $0D $08 $0A $00 $9E $28`, the load address `$0801` and a
`10 SYS(2064)` line. A CVT container would begin `$82 $00 $00` followed by `DISK `, the
header's copy of the directory entry, whose first two bytes a `LOAD` would take as a load
address of `$0082`. Of the 355 GEOS entries on the twenty GEOS disks scanned, `DISK COPY`
is the only one that loads and runs from BASIC. Test: `Suite11-SI149-GeosEntries`.

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
convergence: the functions are the same code in this firmware and SDM, down to the comment
`// '|' > 96 ;)` and the `reserved_names` table. A unified diff of `petscii_to_fat()`
between `U software/components/pattern.cc` and `SDM src/fatops.c` shows only whitespace,
the two divergences listed in SI-142, and one restructured assignment;
`fat_to_petscii()` differs only in how `hex2bin()` is called. GideonZ wrote
them here in 2020 (`3ec23bb9`, 4 October 2020); the reporter added them to sd2iec as
mode 5 in 2025 (`0f22587`, 6 June 2025) and documented them in that fork's README in
2026 (`ddb949c`). SDU has modes 0 to 4 only, and none of this mapping: it converts a name
with `pet2asc()` and has no `petscii_to_fat()`. A card written by an Ultimate therefore
reads back name for name on SDM, and on SDU only for names that need no escaping.

**SI-140.** This is a compatibility contract, not an implementation detail. A card
or stick written by an Ultimate must read back on SDM in extension mode 5, and the
reverse. SDU is not party to it, for the reason above. Nothing in this specification may change the mapping except
where SI-142 states a difference and its reason.

**SI-141.** The rules, from the shared implementation and SDM README:

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
* A directory takes its name without a type extension, and a FAT host drops a trailing
  `.` or space from the name it creates. `MD` and a rename of a directory therefore refuse
  a name ending in either with `33` (SI-060, SI-074), rather than create a directory that
  cannot be found again under the name it was made with. SDM maps such a name the same
  way and creates the unreachable directory; the mapping itself is shared with SDM and is
  not changed here.

Tests: `test_name_mapping` in `target/pc/linux/parse`, which maps every rule above both
ways, and `Suite11-CommonBugs`.

**Difference from sd2iec: a host name longer than 16 characters.** GSD reads, "Long
filenames (i.e names not within the 8.3 limits) are supported on FAT, but for
compatibility reasons the 8.3 name is used if the long name exceeds 16 characters."
This drive renders the first 16 characters of the long name instead. The 8.3 name is a
property of FAT alone, and `FileInfo` carries one name for every file system the
Ultimate mounts, CBM disk images and FTP among them, so presenting it would mean
plumbing a second name through all of them. The rendered name a listing shows is the
name every command here accepts, because `resolve_existing_iec_path()` matches against
the rendered names of a directory scan (SI-143), so a file is reachable under the name
it shows. What differs is the name the two devices print for the same file. A file this
drive creates has a name of at most sixteen characters (SI-150), so two files list under
one name only when another tool wrote them.

**SI-142.** `*` and `?` are escaped as SI-141 escapes the other characters, and a
create of a name containing either is refused per SI-032, as `SDM a76deb2` does.
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

The name in the header is the file's name on the bus. A trailing run of `$A0` in it is
padding, which some programs write in place of zeros, and is dropped as SI-148 drops it
(`SD fatops.c`, "Some programs pad the name with 0xa0 instead of 0"). The file answers to
that name only: its host name, `NAME.P00`, is not a second name it can be opened under.
Test: `Suite11-SI144-HeaderNameOnly`.

Reading is unconditional on sd2iec, in every extension mode. GFN's argument for it is
worth restating because it is the deciding one: an x00 file preserves the CBM name,
its case and its type exactly; two files may differ by case alone; the name may
contain every character CBM DOS allows; and every emulator and every sd2iec unwraps
it the same way regardless of configuration. It is the only mapping under which a
file moved between an Ultimate, an sd2iec and VICE keeps its identity.

An x00 file lists, opens (with or without a type), positions, appends, copies (the data
without the header, with the type from the header), renames (the name in the header, and the host
file to match it) and scratches under the CBM name in its header. A new file of a name
that an x00 file carries answers `63`, and with `@` the x00 file is removed and the new
file written in its place, as sd2iec's `file_open()` does. The UCI `GET_IECNAME` command
reads the header when it is given a full path. The reason for reading these files at all
is GAP's section on file names, which asks the drive to follow how sd2iec stores CBM file
types.

**SI-144c.** A rename of a file that lives in an x00 wrapper writes the new name into the
header and gives the host file the same name: the new CBM name rendered for the file
system by the mapping of SI-140, the type letter the wrapper already carries, and two
digits. The digits start at `00` and count up while another host file holds that spelling,
so a rename never writes over a file that is there; when none of the hundred spellings is
free the host name stays as it is, and the name in the header is the new one either way.
The file keeps its wrapper, so no file is left holding a header without the extension that
announces it. A rename into another directory moves the host file under the new name.
Source: `SDM fat_rename()`, which builds the host name with `build_name(name, type, 2)`
and increments the extension while `f_stat()` finds a file of that name.

*Difference from SDU.* `SDU fat_rename()` writes the header and leaves the host file
where it is, under its old name; its comment reads `/* [PSUR]00 rename, just change the
internal file name */`. SDBUGS asks for the host name to follow the header, under reduced
clarity, because two files renamed from the same CBM name are otherwise told apart only
with a hex editor. This drive follows SDM rather than SDU, which rule 2a of section 1.2
allows for a stated reason: the header name is the only name this drive shows for such a
file, on the bus (SI-144) and in the file browser (SI-144b), so a host name left behind is
a second name that no interface shows and no program reads, an x00 file being found by the
name in its header.

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
that rule leaves the row showing the host name. A rename in the browser edits the name the
row carries and takes the host file with it, the rename SI-144c describes, so the browser
and the drive give a wrapper the same name. A copy, a move and a delete act on the file of
the medium under its host name. A browser copy copies the host file whole,
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

**SI-145 (deliberately unsupported).** Writing x00 files is a configuration choice, default off, so that
existing users see no change. When on, it follows sd2iec mode 1 (x00 for SEQ, USR and
REL, plain for PRG) or mode 2 (x00 for everything). Source: SD README under `XEnum`.

**Deliberately unsupported.** It adds a user setting that no report asks for, and
reading x00 files (SI-144) already gives the interchange with files that VICE and sd2iec
write. New files are written plain.

**SI-146 (deliberately unsupported).** The x00 wrapper is the whole of the write side answer for relative files.
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

The code is, in this firmware and in SDM:

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
   back unchanged. Nothing rejects it on read. Inside a disk image the directory entry is
   the only record of a name, and a name there ends at its first `$A0`, as it does in
   `SD d64_readdir()`: a file written into an image as `A`+`$A0`+`B` is found again as `A`.
   Items 1 and 4 are about names on the host file system.
2. A **trailing** run of `$A0` is padding, not data, and is dropped before mapping. A
   CBM directory entry is a fixed 16-byte field padded with `$A0`, so a name arriving
   from one carries padding that was never part of the name.
3. A name that is empty, or whose first byte is `$A0`, is refused on create, with
   `33`, or `64` when `@` is given. This is the same branch as the wildcard rejection
   in SI-032: `SDM file_open()` tests `(*fname == 160)` alongside `*` and `?`, and
   `SDM parse_mkdir()` and `parse_rename()` refuse such a name with `34`. SDU has none
   of these three tests.
4. A directory listing ends the name at its terminator or at 16 characters, **not** at
   the first `$A0`. A 1541 puts the closing quote at the first `$A0` because that is
   where its fixed-width field stops carrying name, and reproducing that would make
   this drive and an sd2iec print different names for the same file. A `$22` inside a
   name is printed as it is, as a 1541 prints it, where `SD createentry()` ends the name
   there.

Together with SI-147 that is the whole of the shifted space question, and it needs
nothing further from anyone.

---

## 15. Limits, and what must not break

### 15.1 Limits

**SI-150.** Summary of the numbers this specification sets, with their sources.

| Limit | Required | Implemented | Source |
| --- | --- | --- | --- |
| command channel buffer | at least 254 | 254 | HD 4-6, `SD CONFIG_COMMAND_BUFFER_SIZE` |
| OPEN name buffer | at least 254 | 254 | GUG, 232-character paths |
| path components | no fixed limit below the buffer | unbounded | HD 4-6; GUG: 232 characters allows about 13 levels of 16-character names |
| scratch list elements | limited only by the command length | limited only by the command length | SD README under `S:`; HD 9-27 allows five |
| copy source elements | at least 5 | limited only by the command length | HD 9-28; SD README under `C:` |
| partitions | 1 to 255 | 1 to 255 | GUG |
| CBM name | 16 characters | 16 | all |

`U resolve_directory_path()` sizes its component list from the path, so a path of any
depth the buffer holds is followed to its end (`Suite11-SI150-DeepPath`). A scratch, a
copy and the attribute commands size their name lists from the command in the same way
(`Suite11-SI150-NameLists`).

A name a file is created under keeps its first sixteen characters, as on a 1541, whether
the file is written, saved or a new relative file: two long names that agree in their
first sixteen characters are one name, and the second create answers `63`. A name that is
only looked up is compared in full (SI-136), so a host file whose name is longer than
sixteen characters is still reachable by it. Test: `Suite11-SI150-CreatedNameLength`.

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

**SI-153.** A LOAD delivers every byte of the file under any KERNAL, JiffyDOS included,
whatever the file's length. The JiffyDOS LOAD protocol moves the file in runs of bytes and
ends it with a signal of its own. The IEC processor sends the byte that carries the end of
the file before it gives that signal, also when that byte is the only one it holds at the
start of a run. It is the only one whenever the drive's last 512 byte buffer of the file
holds a single byte, because the processor sends everything it has while the drive reads
that buffer. The drive counts as sent only the bytes a transfer pass sent, so a pass that
found the processor still busy and sent nothing leaves the last byte to be sent. Sources:
917, which reported corrupted JiffyDOS loads, measured on an Ultimate 64 with a JiffyDOS
KERNAL: every file of 512n + 1 bytes, load address included, came back one byte short,
and every other length came back whole. Tests: Suite11-JiffyLoadStream, and the LOAD
checks of `tests/e2e/io/iec/dos_command_test.py` run with `--kernal`.

### 15.3 Consequences elsewhere

**SI-151 (retired).** The number held a requirement to migrate the relative files this
firmware had written, from a two byte record length to a one byte one. A migration over
user data, by a heuristic, to gain an interchange the x00 wrapper already provides, is a
cost with no matching benefit; SI-084 reads both plain layouts instead. The number is
left retired rather than reused, so that a reference to it in an older note resolves
here.

**SI-152.** A log line carries one rendering of the bytes of a command, as text with
every byte that is not printable ASCII written as `\xNN`, which loses nothing. A
rendering that does not fit its 260 character buffer ends in `..`, and the line still
carries the command's real length, so a reader can tell a long command from a cut
rendering of one. `SOFTIEC_LOG_MAX_BYTES` in `software/io/iec/iec_log.h` sizes that
buffer, four characters for each of 64 bytes and four more, so a line holds all 254 bytes
of a command of printable text and at least 64 bytes of any command. The
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
| C6 | `=D` directory filter | `SD` and this firmware treat `D` as DIR. IDE 6.2 maps `D` to DEL. GSD warns that on other drives `D` matches everything. | `D` is a synonym for `B` (SI-134), and say in the user documentation that software should send `B`. Changing it would break the sd2iec software that already sends `D`, and no software can be relying on `D` meaning DEL here because this drive lists no DEL entries (SI-149). |
| C7 | Wildcards with more than one `*` | `SD match_name()` and CBM DOS stop at the first `*`. This firmware backtracks. GAP calls the difference harmless. | Backtracking stands (SI-136). For one `*` it agrees with sd2iec's default; for more it narrows rather than widens a match, so no command can act on more files than the other devices would. |
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

**T3a. The defects other implementations have, `Suite11-CommonBugs`.** SDBUGS records what
the sd2iec family gets wrong, and each of its reports that can apply to this drive is
checked here in one case: a one character directory inside a DNP image, a relative file
whose record length is the terminator byte, a second file whose name differs only by
shifted space padding, a name that is nothing but a shifted space, a wildcard in a name
being created, a replace that matches nothing, a rename of a file inside an x00 wrapper, a
name that starts with a dot and carries an extension, a rename that changes only case, a
scratch of a locked entry, and a write refused because the image is locked. None of them
is present here, and the case exists so that a later change cannot introduce one quietly.
The reports this drive cannot have are the GEOS speeder, which it does not implement
(SI-115), and the D80 and D82 partition types, which it does not serve.

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

Everything in sections 2 to 15 is in force except the five requirements in the first table.
Each of them carries the reason below the requirement itself, and a test asserts the
answer given here, so a later implementation has to change a test on purpose.

| Requirement | Why it is not implemented | What the drive answers |
| --- | --- | --- |
| SI-054 `V` | Validating means rebuilding the block map of an image from every directory, side sector chain and GEOS record chain in it, and a walk that misses one marks live blocks free; an OK without the walk would claim a check that did not happen | `31`, as sd2iec answers |
| SI-105 `M-W`, `M-E` | Nothing written is kept and nothing is run, so an OK would tell a fast loader its drive code runs; no drive code is ever known, which is what `M-E` answers | `30` for `M-W` to any address but `$0077`, which changes the device number (SI-100a); `98,UNKNOWN DRIVE CODE` for `M-E`, as SD answers for a code it does not know |
| SI-137 raw directory | Every program that reads a listing byte by byte opens `$` on a data channel, and the UCI target opens it on whatever channel its client sends; on a host file system the sectors would have to be synthesised from the listing in any case | the listing |
| SI-145 writing x00 files | A user setting that no report asks for; reading them (SI-144) already gives the interchange | new files are written plain |
| SI-146 creating R00 files | Follows from SI-145; a plain relative file keeps its two byte layout, which SI-084 reads on both devices | a new relative file is written in the plain two byte layout |

Section 19 lists what is out of scope: the commands that run 6502 code in drive memory,
the hardware a CMD device has and this one does not, and the sd2iec settings commands.

**Differences from the sources.** Each of these is a requirement in force that answers
differently from one of its sources, for a reason given below the requirement.

| Requirement | The difference |
| --- | --- |
| SI-016 | A carriage return second to last ends a command only when a line feed follows it, because the ROM's branch cuts a binary parameter of 13 short |
| SI-018 | The position in a plain file is read from the command without its terminator, where sd2iec reads it from the command as sent |
| SI-033 | A scratch whose path does not exist answers `71` rather than a count of zero |
| SI-061 | `CD` with nothing to change to answers `00, OK` and stays, where `SD do_chdir()` answers `39` |
| SI-071a | `N` formats a DNP image from any directory in it, where the SDU README ignores it outside the root |
| SI-082 | An offset past the end of a plain file open for reading answers `00, OK` and reads nothing, where `SD fat_file_seek()` answers `50` |
| SI-090 | `##0` and `##` with anything but a digit open a standard buffer, where `SD open_buffer()` opens nothing or counts any byte |
| SI-091 | A block command on a partition rooted in a host directory answers `78,BLOCK ACCESS DENIED`, an Ultimate code (section 4.2), where sd2iec answers `20` and uses 78 for another error |
| SI-064 | `R-H` takes an optional id and sets it, as `SDM parse_set_header()` does, where HD 9-15 gives `R-H` a new name only |
| SI-074 | A rename into another directory or partition moves the entry, where `SD parse_rename()` answers `62` |
| SI-077 | `EL`, `EU` and `A` act on every entry a name matches, directories included, where SDM skips directories and `A` takes the first match. SDU has none of these commands |
| SI-100a | An `M-W` to `$0077` moves the whole drive to the number in the low five bits of its first byte and does not read the talk address, where a 1541 stores each byte it is sent; a number outside 8 to 30 answers `30`, where `SD handle_memwrite()` takes any value |
| SI-103 | The resets over the bus keep the write protect of `W-1` and the device number of `U0>`, where `U`+shifted J on sd2iec restarts the device and loses both |
| SI-120 | A write sets the drive's own clock, an offset from the system clock that a reset clears, where a CMD drive and sd2iec set their clock chip; the day of week a write carries is not kept; a write is refused when the day is not a day of that month, which `SD parse_timewrite()` does not check, and every field is read at its documented width |
| SI-123 | A clock write is exactly as long as its form and its marker is `AM` or `PM`, where `SD parse_timewrite()` ignores what it does not recognise |
| SI-136 | A second `*` matches in the middle of a name, where CBM DOS and sd2iec stop at the first; a pattern is compared in full, where `SD match_name()` stops after sixteen characters; and a letter matches in either ASCII case, where CBM DOS and sd2iec compare bytes |
| SI-141 | A host name longer than 16 characters renders as its first 16 characters, where SDM prints the 8.3 name. SDU has no such mapping. A directory name ending in a dot or a space is refused, where SDM creates a directory it cannot find again |
| SI-142 | The length guard of `SDM` is not adopted, because it would change no host name the drive produces |
| SI-144c | A rename of a file in an x00 wrapper renames the host file to match the header, as `SDM fat_rename()` does, where `SDU fat_rename()` writes the header alone |
| SI-148 | A `$22` inside a name is listed as it is, as a 1541 lists it, where `SD createentry()` ends the name there |
| SI-149 | DEL entries and types above DIR in a disk image are not listed, and a `CBM` entry lists as SEQ, where `SD d64_readdir()` lists them as `DEL`, `CBM` and `???` |

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
  a device with physical buttons, and `U software/io/iec/cbmdos_parser.cc` already records the
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
  with `CP` (SI-040) and reads them with `$=P` (SI-044).
* sd2iec's EEPROM file system, the small partition it exposes from the spare space of
  the microcontroller's own EEPROM (SD README, "EEPROM file system"). It exists
  because that hardware has an EEPROM larger than its configuration needs. A
  partition of this drive is a directory of the Ultimate file system, and the
  Ultimate's own flash is reachable as one of those. The `!` partition alias goes with
  it: `CP!:`, `$!` and `!:NAME` address the EEPROM partition wherever it ended up, and
  with no such partition there is nothing for the alias to name.
* M2I files, which sd2iec itself has deprecated (SD README, Deprecation notices).
* sd2iec's firmware update, hot swapping of the card, card detection and sleep mode,
  which address its own hardware. The Ultimate updates, mounts and powers itself
  through its own menu and REST interface.
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
  * SD README: the settings commands `X`, `XE+`/`XE-`, `XI`, `X*+`/`X*-`, `X?`, and
    SDM's `XEL`/`XEU`, `XET`, `XN`, `XH+`/`XH-`, `XD`, `XW`, and `XL`/`XU`, which the
    README names in a heading and does not describe. The drive always matches as `X*+`
    sets it (SI-136). This drive keeps its settings in the Ultimate
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

| Command | Count | Answer |
| --- | --- | --- |
| `C`+`$D0`+`CHR$(2)` | 29 | `02,PARTITION SELECTED` |
| `CD//OS/DESKTOP/1/` | 11 | `00, OK` |
| `CD//OS` | 6 (plus one with a terminator) | `00, OK` |
| `CD//OS/SERVICES/{$C1}PP {$CC}AUNCHER/` | 6 | `00, OK` |
| `CD//OS/DRIVERS/` | 3 | `00, OK` |
| `CD//OS/SETTINGS/`, `CD//OS/DESKTOP/`, `CD//OS/CHARSETS/` | 1 each | `00, OK` |
| `CP2`, `CP2`+CR | 1 each | `02,PARTITION SELECTED` |
| `M-R` at `$FEA4`, `$E5C5`, `$A6E8`, `$0002`, 2 bytes each | 1 each | two bytes of `$00` (SI-112) |
| `UI` | 1 | `73,U64HD ULTIMATE DOS V2.0` |
| `S/TEMPORARY/:*` | 1 | `01, FILES SCRATCHED,<count>,00` (SI-033) |
| `CHR$(0)` | 1 | `31,SYNTAX ERROR` (SI-031) |

The answers are the ones this drive gives, each held by the test of the requirement named
beside it; the counts are from TRACE. Open names are of the forms `NAME`, `:NAME`,
`/PATH/:NAME`, `$` and `$:PATTERN`.
Data channels used are 0, 2, 3 and 14. Names carry shifted PETSCII and spaces, for
example `:{$C1}BOUT {$D4}HIS {$C1}PP`.

So the whole of a C64 OS boot needs: the binary Change Partition, `CD` with an
absolute path, `M-R`, `UI`, `S` with a path, a directory read with and without a
pattern, and file opens with a path. Everything else in this specification is for the
other software that already targets these devices.

## Appendix B. Requirement index

Sections 2 to 15 define the numbered paragraphs SI-001 to SI-154, with gaps, one of which
(SI-151) is retired. A paragraph whose number carries a letter, such as SI-103a, states a
further rule of the requirement it follows and is numbered that way so that the numbers
already cited elsewhere keep their meaning.

Section 18.1 is the index of the five deliberately unsupported requirements and of the
twenty-one that are in force and answer differently from one of their sources. Everything else
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
| Files: x00 wrappers, the header, the extension family, the internal name in a listing, renaming the internal name and the host name with it | SI-144, SI-144a, SI-144b, SI-144c, SI-145, SI-146 |
| Files: relative files, and the record length of a plain one | SI-080, SI-084, SI-146 |
| Files: positioning (seeking) within a file with `P` | SI-081, SI-082, SI-083 |
| Files: M2I | Section 19; the manual deprecates the format |
| Files: loading, saving, verifying, pattern matching | SI-032, SI-070, SI-136; `V` as a verify is BASIC's, not a command |
| Files: renaming files and subdirectories | SI-074 |
| Files: copying and combining between partitions | SI-075, SI-013 |
| Files: locking and unlocking | SI-076, SI-077, SI-077a, SI-132 |
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
| Device management: firmware update, hot swapping, card detection, sleep mode | Section 19, which names all four as sd2iec hardware |
| Device management: device detection and the `UI` identifier | SI-110, SI-111, SI-113, SI-114 |
| Device management: warm, cold and hard reset | SI-103, SI-103a |
| Device management: memory access, `M-R`, `M-W`, `M-E` | SI-105, SI-100a, SI-112, SI-115 |
| Device management: user commands `U1` to `UJ` | SI-091, SI-103, SI-104 |
| Device management: the device address, `U0>` and `S-8`/`S-9`/`S-D` | SI-100, SI-100a, SI-101 |
| Device management: the bus protocol setting | SI-103: `UI+` and `UI-` answer `00, OK` |
| Direct access: buffers and large buffers | SI-090 |
| Direct access: reading and writing data, `B-R`, `B-W`, `U1`, `U2` | SI-093, SI-094 |
| Direct access: the buffer pointer | SI-092 |
| Direct access: block execute | SI-095 |
| Direct access: `DI`, `DR`, `DW`, the direct sector commands | SI-096 |
| Realtime clock: the four read and four write forms, ASCII, BCD, decimal and ISO | SI-120, SI-121, SI-122, SI-123 |
| Settings: the `X` family | Section 19, except `XE`, which is SI-145 |
| Software fastloaders | Section 15.2 item 10 for JiffyDOS; section 19 for the others |
| Write protect, `26,WRITE PROTECT ON` | SI-102, SI-102a, SI-077a |

## Appendix E. Traceability

Which tests name each requirement, generated by `tools/softiec_traceability.py` from the
test sources. A case name is a `Suite` case in `target/pc/linux/iecdrive`; `parse` is a
case in `target/pc/linux/parse`; the remaining names are hardware suites under `tests/`.
A requirement with no test is one that only says what is out of scope, or is retired,
which section 1.3 allows and names.

| Requirement | Named by |
| --- | --- |
| SI-001 | Suite11-SI001-DeviceNumberRange |
| SI-002 | Suite3 |
| SI-003 | Suite11 |
| SI-004 | Suite11-SI004-BlocksFree |
| SI-005 | Suite3 |
| SI-010 | Suite3 |
| SI-011 | Suite3 |
| SI-011a | Suite10 |
| SI-012 | Suite11-SI012-WildcardPath |
| SI-013 | Suite11-SI093-BoundPartition |
| SI-014 | Suite11-SI014-LeftArrow, Suite8, iec-dos-commands |
| SI-015 | Suite11-SI014-LeftArrow |
| SI-016 | Suite11-SI016-SecondTerminator, parse |
| SI-017 | iec-dos-commands |
| SI-018 | Suite11-SI018-PositionExempt, parse |
| SI-019 | Suite11-CommonBugs, parse |
| SI-020 | parse |
| SI-021 | Suite11-SI021-LongNames, iec-dos-commands, parse |
| SI-022 | Suite10, Suite11-SI022-TooLong, Suite11-SI022-UciTooLong, iec-dos-commands, parse |
| SI-030 | Suite11-SI030-MissingName, Suite11-SI030-UnknownSubcommand, Suite11-SI030-WildcardTarget, Suite11-SI055-SubPartitions, parse |
| SI-031 | Suite10, Suite11-SI031-Unrecognised, iec-dos-commands, parse |
| SI-032 | Suite11-CommonBugs, Suite11-SI032-WildcardWrite, Suite11-SI035-TypeOfExistingName |
| SI-033 | Suite11-SI033-ScratchNothing, iec-dos-commands |
| SI-034 | iec-dos-commands |
| SI-035 | Suite11-CR8-ScratchScan, Suite11-CommonBugs, Suite11-SI035-TypeOfExistingName, Suite5 |
| SI-036 | Suite11-SI036-BlockRange, Suite11-SI083-SeekWriteImage |
| SI-154 | Suite11-SI105-UnreadReply, Suite11-SI154-StatusClears |
| SI-040 | Suite10 |
| SI-041 | Suite11-SI041-NamePadding, Suite11-SI041-PartitionSize, iecdrive, parse |
| SI-042 | Suite11-SI041-PartitionSize |
| SI-043 | Suite10 |
| SI-044 | Suite10 |
| SI-045 | Suite11-SI045-PartitionDirectory, iec-dos-commands |
| SI-046 | Suite11-SI045-PartitionDirectory, iec-dos-commands |
| SI-047 | Suite10 |
| SI-048 | Suite10 |
| SI-049 | Suite10, Suite11-SI045-PartitionDirectory |
| SI-050 | Suite10 |
| SI-051 | Suite11-SI051-RenamePartition, Suite11-SI064-RootHeaderLength, parse |
| SI-052 | Suite11-SI071-Format |
| SI-053 | Suite10, Suite11-SI053-Initialize, iec-dos-commands, parse |
| SI-054 | parse |
| SI-055 | Suite11-SI055-SubPartitions |
| SI-060 | Suite11-SI060-MdColon, parse |
| SI-061 | Suite10 |
| SI-062 | Suite10 |
| SI-063 | Suite10, Suite11-SI063-RdNoPath, Suite11-SI063-RdOnlyDirectories, Suite6, parse |
| SI-064 | Suite11-SI064-RenameHeader, Suite11-SI064-RootHeaderLength, parse |
| SI-065 | Suite11-SI064-RenameHeader, Suite11-SI065-HeaderName |
| SI-066 | Suite10 |
| SI-070 | Suite11-SI070-ModifyOpen, Suite11-SI070-SecondaryForcesMode, Suite3, parse |
| SI-071 | Suite11-CommonBugs, Suite11-SI071-Format, iec-dos-commands, parse |
| SI-071a | Suite11-SI071-Format |
| SI-072 | Suite11-SI072-RawNames |
| SI-073 | Suite11-SI150-NameLists, Suite8-T-RA |
| SI-074 | Suite11-SI074-MoveChecks, iec-dos-commands, iecdrive |
| SI-075 | Suite11-SI075-CopyRelative, Suite11-SI150-NameLists, Suite8-T-RA |
| SI-076 | Suite11-CommonBugs, Suite11-SI076-Lock, Suite11-SI077-AttributeCommands, parse |
| SI-077 | Suite11-SI077-AttributeCommands, parse |
| SI-077a | Suite11-CommonBugs, Suite11-SI077-AttributeCommands, Suite11-SI077-ImageWriteLock, iec-dos-commands |
| SI-080 | Suite11-SI080-PastTheLastRecord, Suite11-SI080-RecordLengthBytes, Suite11-SI084-RelInImage, Suite4, Suite4-CopyCreate, parse |
| SI-081 | Suite11-SI081-PositionChannel, Suite4 |
| SI-082 | Suite11-SI082-PositionAfterEnd, Suite4 |
| SI-083 | Suite11-SI083-SeekWrite, Suite11-SI083-SeekWriteImage |
| SI-084 | Suite11-SI084-RelInImage, Suite11-SI084-RelLayouts, iec-dos-commands |
| SI-090 | Suite11-SI090-BufferPointer, Suite9, parse |
| SI-091 | parse |
| SI-091a | Suite11-BlockAllocateAnswers |
| SI-092 | parse |
| SI-093 | Suite11-SI070-ModifyOpen, Suite11-SI093-BoundPartition, iecdrive |
| SI-094 | Suite11-SI094-BlockLength, iecdrive, parse |
| SI-095 | Suite11-SI030-UnknownSubcommand, parse |
| SI-096 | parse |
| SI-100 | Suite11-SI100-DeviceNumber, Suite11-SI103b-SettingAfterU0, iec-dos-commands, parse, softiec-soak |
| SI-100a | Suite11-SI100a-MemoryWriteDeviceNumber, iec-dos-commands, parse |
| SI-101 | iec-dos-commands, parse, softiec-soak |
| SI-102 | Suite11-SI102-WriteProtect, iec-dos-commands, parse |
| SI-102a | Suite11-SI102-WriteProtect, Suite11-SI102a-NewRelative |
| SI-103 | Suite11-SI103-Resets, iec-dos-commands, parse, softiec-soak |
| SI-103a | Suite11-SI103-Resets |
| SI-103b | Suite11-OperationLogNoReconfigure, Suite11-ResetRestartsProcessor, Suite11-SI103b-SettingAfterU0, iec-dos-commands, rel-copy |
| SI-104 | Suite10, Suite11-SI030-UnknownSubcommand, parse |
| SI-105 | Suite11-SI105-MemoryCommands, Suite11-SI105-UnreadReply, iec-dos-commands, parse |
| SI-106 | parse |
| SI-107 | Suite11-SI107-SettingModes, uci-targets |
| SI-110 | Suite11-SI105-MemoryCommands, rel-copy |
| SI-111 | Suite11-SI105-MemoryCommands |
| SI-112 | Suite11-SI105-MemoryCommands, parse |
| SI-113 | Suite11-SI105-MemoryCommands |
| SI-114 | Suite11-SI105-MemoryCommands |
| SI-115 | *(none: see section 1.3)* |
| SI-120 | Suite8-T-RA, iec-dos-commands, parse |
| SI-121 | parse |
| SI-122 | parse |
| SI-123 | parse |
| SI-130 | Suite11-SI130-ListingHeader, iec-dos-commands |
| SI-131 | Suite11-SI130-ListingHeader |
| SI-132 | Suite11-SI076-Lock, Suite11-SI132-Splat, Suite11-SI134-HiddenFlag |
| SI-133 | Suite11-SI133-SizeRemainder |
| SI-138 | Suite11-SI133-SizeRemainder, Suite11-SI138-ListingEof, iec-dos-commands |
| SI-139 | Suite11-SI139-StampedEntries, iec-dos-commands |
| SI-134 | Suite11-SI134-HiddenFlag, parse |
| SI-134a | Suite11-SI134-HiddenFlag |
| SI-135 | Suite11-SI139-StampedEntries, parse |
| SI-136 | Suite11-SI136-CaseFolding, parse |
| SI-137 | Suite11-DeliberateExclusions |
| SI-149 | Suite11-SI149-GeosEntries |
| SI-140 | parse |
| SI-141 | Suite11-CommonBugs, Suite11-SI074-MoveChecks, parse |
| SI-142 | Suite11-SI142-EscapedWildcards, Suite11-SI144c-RenameX00, parse |
| SI-143 | Suite11-SI150-CreatedNameLength, parse |
| SI-144 | Suite11-CommonBugs, Suite11-SI144-HeaderNameOnly, Suite11-SI144-ReadX00, Suite11-SI144-X00Paths, iec-dos-commands, prg-context-menu, softiec-soak, uci-targets |
| SI-144c | Suite11-CommonBugs, Suite11-SI144c-RenameX00, iec-dos-commands, prg-context-menu |
| SI-144a | Suite11-SI144-SharedHeader, prg-load-path-trim |
| SI-144b | prg-context-menu, prg-load-path-trim |
| SI-145 | Suite11-DeliberateExclusions |
| SI-146 | Suite11-SI084-RelLayouts |
| SI-147 | Suite11-SI147-ShiftedSpace, iec-dos-commands, parse |
| SI-148 | Suite11-CommonBugs, Suite11-SI032-WildcardWrite, Suite11-SI144-HeaderNameOnly, Suite11-SI147-ShiftedSpace, iec-dos-commands |
| SI-150 | Suite11-SI150-CreatedNameLength, Suite11-SI150-DeepPath, Suite11-SI150-NameLists |
| SI-153 | Suite11-JiffyLoadStream, iec-dos-commands |
| SI-151 | *(none: see section 1.3)* |
| SI-152 | Suite11-FailureLog |
