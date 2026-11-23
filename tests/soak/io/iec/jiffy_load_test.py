#!/usr/bin/env python3
# Device-free checks of the JiffyDOS LOAD soak's helpers.

"""Verify what jiffy_load_soak_test.py decides without a device.

The soak's verdicts are read as firmware defects, so the arithmetic that makes them is
checked here: the host delay's patch, its refusal of an image it was not written for, the
checksums the C64 program and the host must agree on, and how a record becomes a verdict.
The images are synthetic. No JiffyDOS byte beyond the instruction the patch replaces is
needed or kept.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import jiffy_load as jl  # noqa: E402
from assembler import assemble  # noqa: E402
from report import Failure, check, suite_ok  # noqa: E402
from selftest import expect  # noqa: E402


def image(signatures=1, size=jl.KERNAL_SIZE):
    """An image that holds the patch site and nothing else of a KERNAL."""
    data = bytearray(size)
    if signatures:
        data[jl.PATCH_OFFSET:jl.PATCH_OFFSET + len(jl.PATCH_SIGNATURE)] = jl.PATCH_SIGNATURE
    if signatures > 1:
        data[100:100 + len(jl.PATCH_SIGNATURE)] = jl.PATCH_SIGNATURE
    return bytes(data)


def record(carry=0, st=0x40, total=0, rotated=0, end=0x6000, code="00", t0=(0, 0, 0),
           t1=(0x29, 0x02, 0)):
    return bytes((carry, st, total & 255, total >> 8, rotated, end & 255, end >> 8,
                  ord(code[0]), ord(code[1]), *t0, *t1, 0))


def refused(data, cycles=61):
    try:
        jl.patched_kernal(data, cycles)
    except jl.RomRefused:
        return True
    return False


def main() -> int:
    cli.device_free_arguments(__doc__)

    with check("the host delay is counted as the #986 table counts it"):
        expect("none", jl.delay_passes(0), 0)
        for cycles, passes in ((61, 10), (86, 15), (101, 18)):
            expect(f"{cycles} cycles", jl.delay_passes(cycles), passes)
        for bad in (1, 62, 15, 11, 11 + 5 * 256):
            try:
                jl.delay_passes(bad)
            except ValueError:
                continue
            raise Failure(f"{bad} cycles were accepted")

    with check("the patch changes the three bytes after the STA and nothing else"):
        original = image()
        patched = jl.patched_kernal(original, 61)
        differ = [i for i, (a, b) in enumerate(zip(original, patched)) if a != b]
        start = jl.PATCH_OFFSET + 3
        expect("changed offsets", differ, [start, start + 1, start + 2])
        expect("the call", patched[start:start + 3], bytes((0x20, 0x40, 0x03)))
        expect("size", len(patched), jl.KERNAL_SIZE)
        expect("no delay is a plain copy", jl.patched_kernal(original, 0), original)

    with check("an image the patch is not written for is refused"):
        expect("wrong size", refused(image(size=4096)), True)
        expect("no patch site", refused(image(signatures=0)), True)
        expect("site twice", refused(image(signatures=2)), True)
        expect("no delay still checks", refused(image(signatures=0), 0), True)

    with check("the KERNAL is told apart by its patch site"):
        expect("unpatched", jl.kernal_kind(image()), "JiffyDOS")
        expect("patched", jl.kernal_kind(jl.patched_kernal(image(), 86)),
               "JiffyDOS with the host delay patch")
        expect("no site", jl.kernal_kind(image(signatures=0)),
               "no JiffyDOS marker (stock or other)")

    with check("the host's checksums are the C64 program's: sum and rotate-and-xor"):
        expect("two bytes", jl.checksums(b"\x01\x02"), (3, 0))
        expect("a high bit rotates round", jl.checksums(b"\x80\x00"), (0x80, 1))
        expect("the sum wraps at 16 bits", jl.checksums(b"\xff" * 258)[0], (258 * 255) & 0xFFFF)
        prg = jl.payload(1, 986)
        expect("load address", prg[:2], bytes((0, 0x20)))
        expect("end", jl.Expected.of(prg).end, 0x2400)
        expect("the same seed is the same file", jl.payload(1, 986), prg)

    with check("a record is a clean load, an ST failure, wrong data or an error status"):
        want = jl.Expected(end=0x6000, total=0x1234, rotated=0x56)
        good = jl.Load.decode(0, record(total=0x1234, rotated=0x56))
        expect("clean", good.verdict(want), "clean")
        expect("seconds", round(good.seconds, 1), 2.9)
        late = jl.Load.decode(1, record(st=0x42, total=0x1234, rotated=0x56, end=0x5A00))
        expect("ST=66 is an ST failure", late.verdict(want), "st")
        expect("with its status", (late.st, late.code), (66, "00"))
        expect("carry set is one too", jl.Load.decode(
            2, record(carry=1, total=0x1234, rotated=0x56)).verdict(want), "st")
        expect("short but ST 64 is wrong content", jl.Load.decode(
            3, record(total=0x1234, rotated=0x56, end=0x5F00)).verdict(want), "content")
        expect("a wrong sum is wrong content", jl.Load.decode(
            4, record(total=0x1235, rotated=0x56)).verdict(want), "content")
        expect("an error status on good data", jl.Load.decode(
            5, record(total=0x1234, rotated=0x56, code="74")).verdict(want), "status")
        expect("the clock wraps an hour", round(jl.Load.decode(
            6, record(t0=(0, 0x59, 0x59), t1=(0x05, 0x00, 0))).seconds, 1), 1.5)

    with check("the texts the program kept go to the first loads that were not clean"):
        want = jl.Expected(end=0x6000, total=0x1234, rotated=0x56)
        records = b"".join((record(total=0x1234, rotated=0x56),
                            record(st=0x42, total=0x1234, rotated=0x56, end=0x5A00),
                            record(total=0x1234, rotated=0x56),
                            record(carry=1, st=0x42, total=0x1234, rotated=0x56)))
        slots = (b"00, OK,00,00\r".ljust(32, b"\0") + b"20, READ ERROR,00,00\r".ljust(32, b"\0"))
        loads = jl.decode_batch(10, 4, records, slots, want)
        expect("indexes", [load.index for load in loads], [10, 11, 12, 13])
        expect("texts", [load.text for load in loads], ["", "00, OK,00,00", "", "20, READ ERROR,00,00"])

    with check("the tally counts every kind and keeps the ST values"):
        want = jl.Expected(end=0x6000, total=0x1234, rotated=0x56)
        tally = jl.Tally()
        for index, data in enumerate((record(total=0x1234, rotated=0x56),
                                      record(st=0x42, total=0x1234, rotated=0x56),
                                      record(st=0x42, total=0x1234, rotated=0x56),
                                      record(total=0x1, rotated=0x56))):
            tally.add(jl.Load.decode(index, data), want)
        expect("counts", (tally.clean, tally.st_failures, tally.wrong_content, tally.attempted),
               (1, 2, 1, 4))
        expect("ST values", tally.st_values, {66: 2})
        tally.hangs += 1
        expect("a hang is an attempt", tally.attempted, 5)

    with check("the parameter block puts each value where the program reads it"):
        block = jl.parameters(50, 11, 10, 16, "jiffy986", 1, "cd//jiffy1/disk.d64")
        expect("size", len(block), jl.PARAMETER_SIZE)
        expect("count, device, passes, pages", tuple(block[:4]), (50, 11, 10, 64))
        expect("name", bytes(block[5:5 + block[4]]), b"JIFFY986")
        expect("CP", bytes(block[0x16:0x16 + block[0x15]]), bytes((0x43, 0xD0, 1)))
        expect("CD", bytes(block[0x21:0x21 + block[0x20]]), b"CD//JIFFY1/DISK.D64")
        for bad in (dict(count=0), dict(count=101), dict(kib=0), dict(kib=33)):
            try:
                jl.parameters(**{**dict(count=1, device=11, passes=0, kib=16, name="A",
                                        partition=1, cd="CD//"), **bad})
            except ValueError:
                continue
            raise Failure(f"{bad} was accepted")

    with check("the C64 program assembles and stays clear of what it loads and records"):
        program = assemble(Path(__file__).with_name("jiffy_load_soak.asm"))
        expect("load address", program[:2], bytes((0x01, 0x08)))
        top = 0x0801 + len(program) - 2
        if top > jl.LOAD_ADDRESS:
            raise Failure(f"the program ends at ${top:04X}, inside the load area")

    suite_ok("jiffy_load_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
