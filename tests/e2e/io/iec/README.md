# REL copying (#655)

Run against an idle Ultimate 64 with a standard C64 KERNAL and no image in drive A:

```sh
./run-tests 192.168.1.148 -s rel-copy --no-retry -o runs/rel-copy
```

The suite assembles `rel_agent.asm` with the repository's 64tass. The C64 runs
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
