"""What jiffy_load_soak_test.py needs that does not touch a device.

The soak loads a file again and again through the KERNAL's own LOAD, and judges each
load by what the C64 program recorded: its status, the error channel, the end address
and a checksum of what arrived. Everything here is a pure function of those records, of
the fixture file, and of the KERNAL image the host delay is patched into, so the device
free checks in jiffy_load_test.py can run it all.

JiffyDOS is licensed. Nothing here carries a ROM: the patch is applied to a copy of the
user's own image, in memory, and refused unless the image is the one the patch is
written for.
"""
import random
from dataclasses import dataclass, field, replace

LOAD_ADDRESS = 0x2000
# The program counts what it loads in pages, in one byte, and the load area must stay
# below the I/O area's $D000 and the program's own results at $C000.
MAX_KIB = 32
# The program keeps 16 bytes per load from $C200, which holds 100 before $C840.
MAX_BATCH = 100
RECORD_SIZE = 16
FAILURE_SLOTS = 8
FAILURE_SLOT_SIZE = 32

KERNAL_SIZE = 8192
# The JiffyDOS LOAD routine, at the point it samples the serial bus once the drive has
# released DATA: STA $DD00, ORA #$20, TAX. The host delay replaces the last two
# instructions with a JSR to a short routine in the cassette buffer that waits first.
PATCH_OFFSET = 0x1B06
PATCH_SIGNATURE = bytes((0x8D, 0x00, 0xDD, 0x09, 0x20, 0xAA))
PATCH_TAIL = 3
DELAY_ROUTINE = 0x0340
# The host delay is the NET number of cycles the patch adds to the host's LOAD routine. The
# JSR replaces ORA #$20 / TAX, 4 cycles, and the routine at $0340 is LDX #passes (2), the
# DEX/BNE loop (5 per pass less 1, the last BNE falling through in 2), ORA #$20 / TAX (4) and
# RTS (6): 5 * passes + 11 for the routine, 6 for the JSR, 4 less for what it replaced, so
# 13 + 5 * passes cycles are added. 10, 15 and 18 passes, the rows of the #986 table that
# the PR calls 61, 86 and 101, are therefore 63, 88 and 103 cycles.
CYCLES_PER_PASS = 5
CYCLES_FIXED = 13
JSR_CYCLES = 6
REPLACED_CYCLES = 4
# The C64 program holds the routine as `delayrt` and copies it to DELAY_ROUTINE with the
# passes in the operand of its first instruction.
OPCODE_CYCLES = {0xA2: 2, 0xCA: 2, 0x09: 2, 0xAA: 2, 0x60: 6}


def routine_cycles(routine: bytes, passes: int) -> int:
    """Net cycles the patch adds, counted from the opcodes of the delay routine itself.

    Walks the routine as the 6502 would: LDX #n, the DEX/BNE loop (BNE taken 3, not taken
    2), ORA #imm, TAX, RTS; any other opcode is refused. The JSR that calls it is added and
    the ORA/TAX it replaces taken off.
    """
    total = JSR_CYCLES - REPLACED_CYCLES
    x = passes & 0xFF
    pc = 0
    while True:
        opcode = routine[pc]
        if opcode == 0xA2:                      # LDX #imm
            x = passes & 0xFF
            total += 2
            pc += 2
        elif opcode == 0xCA:                    # DEX
            x = (x - 1) & 0xFF
            total += 2
            pc += 1
        elif opcode == 0xD0:                    # BNE rel
            offset = routine[pc + 1] - 256 if routine[pc + 1] > 127 else routine[pc + 1]
            if x:
                total += 3
                pc += 2 + offset
            else:
                total += 2
                pc += 2
        elif opcode in (0x09, 0xAA, 0x60):
            total += OPCODE_CYCLES[opcode]
            pc += 2 if opcode == 0x09 else 1
            if opcode == 0x60:
                return total
        else:
            raise ValueError(f"opcode ${opcode:02X} is not one the delay routine uses")


class RomRefused(ValueError):
    """The image is not one the host delay patch is written for."""


def delay_passes(cycles: int) -> int:
    """Passes of the delay loop for a host delay of `cycles`, or 0 for none."""
    if cycles == 0:
        return 0
    passes, rest = divmod(cycles - CYCLES_FIXED, CYCLES_PER_PASS)
    if rest or not 1 <= passes <= 255:
        raise ValueError(f"a host delay is {CYCLES_FIXED} plus {CYCLES_PER_PASS} per pass, "
                         f"between {CYCLES_FIXED + CYCLES_PER_PASS} and "
                         f"{CYCLES_FIXED + 255 * CYCLES_PER_PASS} cycles, or 0; not {cycles}")
    return passes


def check_jiffydos(image: bytes) -> None:
    """Refuses an image that is not a JiffyDOS C64 KERNAL this patch is written for."""
    if len(image) != KERNAL_SIZE:
        raise RomRefused(f"the ROM holds {len(image)} bytes, a KERNAL {KERNAL_SIZE}")
    at = image[PATCH_OFFSET:PATCH_OFFSET + len(PATCH_SIGNATURE)]
    if at != PATCH_SIGNATURE or image.count(PATCH_SIGNATURE) != 1:
        raise RomRefused("the ROM is not a JiffyDOS C64 KERNAL this test knows: the LOAD "
                         f"routine's code at offset ${PATCH_OFFSET:04X} is not what the "
                         "host delay is written for")


def patched_kernal(image: bytes, cycles: int) -> bytes:
    """A copy of the JiffyDOS `image` whose LOAD samples the bus `cycles` late."""
    check_jiffydos(image)
    if not delay_passes(cycles):
        return bytes(image)
    start = PATCH_OFFSET + len(PATCH_SIGNATURE) - PATCH_TAIL
    jsr = bytes((0x20, DELAY_ROUTINE & 0xFF, DELAY_ROUTINE >> 8))
    return image[:start] + jsr + image[start + PATCH_TAIL:]


def kernal_kind(image: bytes) -> str:
    """What the C64 is running, from its KERNAL image read at $E000."""
    if len(image) != KERNAL_SIZE:
        return "unknown"
    at = image[PATCH_OFFSET:PATCH_OFFSET + 3 + PATCH_TAIL]
    head, tail = at[:3], at[3:]
    if head == PATCH_SIGNATURE[:3]:
        if tail == PATCH_SIGNATURE[3:]:
            return "JiffyDOS"
        if tail[0] == 0x20 and tail[1] | tail[2] << 8 == DELAY_ROUTINE:
            return "JiffyDOS with the host delay patch"
    return "no JiffyDOS marker (stock or other)"


def payload(kib: int, seed: int) -> bytes:
    """The fixture PRG: its load address, then kib KiB of seeded bytes."""
    data = random.Random(seed).randbytes(kib * 1024)
    return LOAD_ADDRESS.to_bytes(2, "little") + data


def checksums(data: bytes) -> tuple[int, int]:
    """The 16 bit sum and the rotate-and-xor byte the C64 program computes over `data`."""
    total = 0
    rotated = 0
    for byte in data:
        total = (total + byte) & 0xFFFF
        rotated = (((rotated << 1) & 0xFF) | (rotated >> 7)) ^ byte
    return total, rotated


@dataclass(frozen=True)
class Expected:
    end: int
    total: int
    rotated: int

    @classmethod
    def of(cls, prg: bytes) -> "Expected":
        body = prg[2:]
        return cls(LOAD_ADDRESS + len(body), *checksums(body))


PARAMETER_SIZE = 0x70
NAME_MAX = 16
CP_MAX = 8
CD_MAX = 79


def parameters(count: int, device: int, passes: int, kib: int, name: str, partition: int,
               cd: str) -> bytes:
    """The block the program reads at $C000; see the header of jiffy_load_soak.asm."""
    if not 1 <= count <= MAX_BATCH:
        raise ValueError(f"a batch is 1 to {MAX_BATCH} loads, not {count}")
    if not 1 <= kib <= MAX_KIB:
        raise ValueError(f"the file is 1 to {MAX_KIB} KiB, not {kib}")
    text = name.upper().encode("ascii")
    command = cd.upper().encode("ascii")
    if len(text) > NAME_MAX or len(command) > CD_MAX:
        raise ValueError("the file name or the CD command is too long for the program")
    cp = bytes((0x43, 0xD0, partition))
    block = bytearray(PARAMETER_SIZE)
    block[0:4] = bytes((count, device, passes, kib * 4))
    block[4] = len(text)
    block[5:5 + len(text)] = text
    block[0x15] = len(cp)
    block[0x16:0x16 + len(cp)] = cp
    block[0x20] = len(command)
    block[0x21:0x21 + len(command)] = command
    return bytes(block)


PARAMETERS_ADDRESS = 0xC000
# Everything the host reads back: the header and status lines from $C080, the failure texts
# and the records, up to the last record of a full batch.
RESULTS_ADDRESS = 0xC080
RESULTS_END = 0xC200 + MAX_BATCH * RECORD_SIZE


def batch_writes(count: int, device: int, passes: int, kib: int, name: str, partition: int,
                 cd: str) -> list[tuple[int, bytes]]:
    """The memory writes that start a batch, in order: the results region zeroed first, so
    that a program that never starts leaves nothing a stale DONE flag or record could be
    counted from, then the parameters."""
    return [(RESULTS_ADDRESS, bytes(RESULTS_END - RESULTS_ADDRESS)),
            (PARAMETERS_ADDRESS, parameters(count, device, passes, kib, name, partition, cd))]


def next_estimate(previous: float | None, clean_seconds: list[float], overhead: float) -> float | None:
    """The per-load time to wait for, from the loads of a batch that were clean.

    A load that failed ends at its first chunk, in no time at all, and says nothing about
    how long a load takes, so only clean loads count. A batch with none leaves the estimate
    as it was, which is the first guess until a clean load has been timed.
    """
    if not clean_seconds:
        return previous
    return max(clean_seconds) + overhead


def _bcd(value: int) -> int:
    return (value >> 4) * 10 + (value & 15)


def _tod(raw: bytes) -> float:
    """Seconds on the CIA's time of day clock from tenths, seconds and minutes in BCD."""
    return _bcd(raw[2]) * 60 + _bcd(raw[1]) + (raw[0] & 15) / 10


@dataclass(frozen=True)
class Load:
    """What the program recorded about one LOAD."""

    index: int
    carry: int
    st: int
    total: int
    rotated: int
    end: int
    code: str
    seconds: float
    text: str = ""

    @classmethod
    def decode(cls, index: int, record: bytes, text: str = "") -> "Load":
        if len(record) != RECORD_SIZE:
            raise ValueError(f"a record is {RECORD_SIZE} bytes, not {len(record)}")
        elapsed = (_tod(record[12:15]) - _tod(record[9:12])) % 3600
        return cls(index, record[0], record[1], record[2] | record[3] << 8, record[4],
                   record[5] | record[6] << 8, bytes(record[7:9]).decode("latin-1"), elapsed,
                   text)

    def bad(self, expected: Expected) -> bool:
        """What the program counts as not clean, and so keeps the error text of."""
        return (self.st_failed() or self.end != expected.end or self.code != "00")

    def st_failed(self) -> bool:
        """The KERNAL said the load failed: carry set, or ST not just end of file."""
        return bool(self.carry) or self.st != 0x40

    def content_wrong(self, expected: Expected) -> bool:
        return (self.end, self.total, self.rotated) != (
            expected.end, expected.total, expected.rotated)

    def verdict(self, expected: Expected) -> str:
        """clean, st (the KERNAL reported a failure), content (it reported success and the
        data is wrong) or status (the data is right and the error channel is not 00)."""
        if self.st_failed():
            return "st"
        if self.content_wrong(expected):
            return "content"
        if self.code != "00":
            return "status"
        return "clean"

    def describe(self, expected: Expected) -> str:
        what = f"ST={self.st}" if not self.carry else f"carry set, ST={self.st}"
        sum_ok = (self.total, self.rotated) == (expected.total, expected.rotated)
        status = f"error channel {self.text!r}" if self.text else f"error channel {self.code}..."
        return (f"load {self.index + 1}: {what}, end ${self.end:04X} (want ${expected.end:04X}), "
                f"checksum {'ok' if sum_ok else 'WRONG'}, {status}")


def decode_batch(first: int, count: int, records: bytes, slots: bytes,
                 expected: Expected) -> list[Load]:
    """The loads of one batch, each with the error channel text the program kept for it.

    The program keeps the text of the first FAILURE_SLOTS loads it counted as not clean, in
    order, so the slots are handed out here in the same order.
    """
    loads = []
    kept = 0
    for i in range(count):
        load = Load.decode(first + i, records[i * RECORD_SIZE:(i + 1) * RECORD_SIZE])
        if load.bad(expected) and kept < FAILURE_SLOTS:
            raw = slots[kept * FAILURE_SLOT_SIZE:(kept + 1) * FAILURE_SLOT_SIZE]
            load = replace(load, text=raw.split(b"\r")[0].split(b"\0")[0].decode("latin-1"))
            kept += 1
        loads.append(load)
    return loads


@dataclass
class Tally:
    clean: int = 0
    st_failures: int = 0
    wrong_content: int = 0
    status_errors: int = 0
    hangs: int = 0
    st_values: dict[int, int] = field(default_factory=dict)
    error_codes: dict[str, int] = field(default_factory=dict)

    @property
    def attempted(self) -> int:
        return (self.clean + self.st_failures + self.wrong_content + self.status_errors
                + self.hangs)

    def add(self, load: Load, expected: Expected) -> str:
        kind = load.verdict(expected)
        if kind == "clean":
            self.clean += 1
        elif kind == "st":
            self.st_failures += 1
            self.st_values[load.st] = self.st_values.get(load.st, 0) + 1
        elif kind == "content":
            self.wrong_content += 1
        else:
            self.status_errors += 1
        self.error_codes[load.code] = self.error_codes.get(load.code, 0) + 1
        return kind

    def summary(self) -> str:
        st = ", ".join(f"ST={value} x{count}" for value, count in sorted(self.st_values.items()))
        errors = ", ".join(f"{code} x{count}" for code, count in sorted(self.error_codes.items()))
        return (f"{self.attempted} loads: {self.clean} clean, {self.st_failures} ST failures"
                f"{f' ({st})' if st else ''}, {self.wrong_content} wrong content, "
                f"{self.status_errors} with an error status, {self.hangs} hangs; error channel {errors or 'not read'}")
