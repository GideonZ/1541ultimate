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
  name, and typed every partition `DIR`.

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
Each transaction gets two uninterrupted seconds before its result is checked:
REST memory reads stop the C64 and can otherwise disturb IEC timing. A transaction
that is still busy then fails the test. Allow roughly ten minutes for the suite.

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
