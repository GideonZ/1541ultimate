# Software IEC hardware suites

Two suites live here. Both drive the C64 through `iec_agent.asm`, a small KERNAL
program that performs the OPEN, CHKOUT/CHROUT, CHKIN/CHRIN and CLOSE calls itself,
so the bytes on the serial bus are the bytes a Commodore puts there. REST only
fills the agent's mailbox and reads its results back. `iec_agent.py` holds the
host side of that mailbox and is shared by both suites.

## Command channel, block commands and partition directory (#875, #876, #877)

```sh
./run-tests 192.168.1.13 -s iec-dos-commands --no-retry -o runs/iec-dos
```

These defects are about the bytes a Commodore sends rather than about the commands
themselves, which is why the host regression in `software/test/iecdrive` did not
catch them: it built its command strings by hand.

* `PRINT#` terminates every command with a carriage return. The firmware counted
  it as part of the command, so `CD//OS` looked for a directory whose name ends in
  a carriage return and answered `71, DIRECTORY ERROR`.
* BASIC prints a space before and after every number, so `PRINT#15,"U1:";2;0;18;0`
  arrives as `U1: 2  0  18  0 `. The parameter parser could not step over the
  colon, so the colon became the channel number and every value moved one place.
  The track number arrived as the partition number.
* The partition directory showed the path a partition is rooted at instead of its
  name, and typed every partition `DIR`. `G-P`, which answers the same question to a
  program rather than to a listing, had the same two defects and reported its type in
  the reserved byte.
* A command whose last parameter is a byte can carry a 13, which is the terminator
  byte. `C` followed by a shifted P selects a partition that way, so it keeps its
  parameter; the commands whose last parameter is optional behave as CMD DOS
  documents, which is that the terminator has to be sent as well.

Two of its checks exist only here, because they cannot fail on a host build. The
device uses the firmware's own `sscanf` in `software/system/small_printf.cc`, which
has no `%c` and counts a conversion it did not make, while a host build links the C
library's. A directory listing filtered by a time stamp therefore answered
`30, SYNTAX ERROR` on the device, and the year the time commands reported came
straight from the real time clock, which counts from 1980.

The suite sends those exact byte sequences, compares a block read against the image
bytes fetched over FTP, and parses the `$=P` stream. It needs one Software IEC
partition numbered 1, temporarily uses device 11, restores the Software IEC
settings and working directory, and deletes only its own fixtures.

Everything that a host build can reach is covered far more widely, and without a
device, by Suite10 of `target/pc/linux/iecdrive/result/testdrive` and by the parser
cases in `target/pc/linux/parse/result/parse`. Both run in well under a second.

## REL copying (#655)

Run against an idle Ultimate 64 with a standard C64 KERNAL and no image in drive A:

```sh
./run-tests 192.168.1.148 -s rel-copy --no-retry -o runs/rel-copy
```

The suite assembles `iec_agent.asm` with the repository's 64tass. The C64 runs
KERNAL OPEN, CHKOUT/CHROUT, CHKIN/CHRIN and CLOSE over the actual IEC bus; REST
only supplies the agent's commands and retrieves its results. FTP independently
checks the stored REL header, file size and every data byte.
Each transaction is left uninterrupted for as long as the bytes it carries need:
REST memory reads stop the C64 and can otherwise disturb IEC timing. See the timing
section below. Allow roughly three minutes for the suite.

Generated fixtures cover FAT, D64 and D81 destinations, record lengths 1, 31,
164 and 254, consecutive files on the same channel without a positioning command,
full records, zero-padding of short records, and reopening without specifying
the file type or record length, followed by a record-position command, as the
supplied E.DATA editor does. D64/D81 files are also read through emulated
1541/1581 drive A.

The suite temporarily uses devices 10 and 11, rejects conflicting devices, restores
drive configuration and the Software IEC working directory, and deletes only its
unique fixture directory. It returns failure for incomplete cleanup. It currently
requires one Software IEC partition, numbered 1. Other Ultimate models have not
been hardware-validated with this suite.

Raw expected/actual REL files and before/after device state are retained in
`rel-copy-evidence/`. For a direct diagnostic run, choose another evidence directory:

```sh
python3 tests/e2e/io/iec/rel_copy_test.py -H 192.168.1.148 --evidence-dir /tmp/rel-copy
```

The host regression is part of `target/pc/linux/iecdrive/result/testdrive`. Its
Makefile explicitly uses signed `char`, matching the Nios CPU, including when built
on an ARM host. This catches the high-bit record-length parsing failure that would
otherwise disappear on hosts whose plain `char` is unsigned.

The reporter's disk and copy utilities are not bundled; the deterministic suite
requires no external disk image. A separate reproduction using the issue attachment
can verify COPY-ALL and the supplied `edata edit 3.0` editor on the same firmware.

## Timing, and which profile runs these

`iec_agent.py` waits blind after every mailbox transaction and only then reads the
result. It has to: a REST memory read halts the C64 for the duration of the copy,
and one that lands inside a serial transfer corrupts it. Polling the mailbox every
20 ms through a 254 byte read spoiled one read in thirty on the test machine, so
there is no cheap way to learn early that a transaction has finished.

The wait is therefore an estimate of how long the transfer takes, not a poll. It is
a fixed part per operation, between 0.08 s and 0.22 s, plus 1.8 ms a byte, and a
further 1.2 s for a transaction addressed to an emulated drive, which seeks and
reads at the speed of the hardware it emulates. Those numbers were measured by
scaling all of them together and running the suites at each scale until transactions
began to overrun; the values here keep a little under twice that margin. A
transaction that needs longer still gets two seconds of grace, and each suite reports
how many needed it, so a wait that is too tight shows up as a number rather than as a
rare corrupted read.

`iec-dos-commands` takes about 24 s and `rel-copy` about 3 minutes. Both are tagged
`standard`, so they run in the merge gate and not in the default `run-tests` run.
