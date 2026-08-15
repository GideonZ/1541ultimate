#!/usr/bin/env python3
"""Run synthetic 1 MiB and 2 MiB Megabyter cartridges on an Ultimate 64.

The CRTs are deliberately made here instead of taken from a fixture.  Their
sizes exercise the complete REST upload and cartridge-loading path, while the
tiny bank-zero program makes an unambiguous result visible through the C64
DMA aperture: green ($05) at $D020 and success ($00) at $D7FF.
"""

import argparse
import os
import struct
import sys
import time
from typing import Tuple

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tests", "lib"))

import machine as machine_lib  # noqa: E402
from api import UltimateApi  # noqa: E402
from report import Failure, check, check_skip, detail, format_exception, suite_fail, suite_ok  # noqa: E402


CRT_HEADER_BYTES = 0x40
CHIP_HEADER_BYTES = 0x10
BANK_BYTES = 0x2000
MEGABYTER_TYPE = 86
BORDER = 0xD020
DEBUG = 0xD7FF
GREEN = 0x05
BEFORE_BORDER = 0x02
BEFORE_DEBUG = 0xA5


def success_program() -> bytes:
    """A cartridge cold-start program that leaves REST-observable success."""
    # $8000 holds the standard cold/NMI vectors and CBM80 signature.  The
    # program enables I/O, marks the border green, clears the debug register,
    # and remains in place so the test can read both values through DMA.
    start = 0x8009
    program = bytearray(struct.pack("<HH", start, start) + b"\xC3\xC2\xCD\x38\x30")
    program += bytes((
        0x78,                    # SEI
        0xA9, 0x37, 0x85, 0x01,  # LDA #$37 / STA $01: ROM and I/O visible
        0xA9, 0x2F, 0x85, 0x00,  # LDA #$2F / STA $00: normal processor-port DDR
        0xA9, GREEN, 0x8D, 0x20, 0xD0,
        0xA9, 0x00, 0x8D, 0xFF, 0xD7,
    ))
    loop = start + len(program)
    program += bytes((0x4C, loop & 0xFF, loop >> 8))  # JMP loop
    return bytes(program)


def megabyter_crt(banks: int) -> bytes:
    """Build a type-86 CRT with `banks` 8 KiB Megabyter ROM banks."""
    if banks not in (128, 256):
        raise ValueError(f"Megabyter test needs 128 or 256 banks, not {banks}")
    name = f"E2E MEGABYTER {banks * BANK_BYTES // (1024 * 1024)}M".encode()
    header = bytearray(b"C64 CARTRIDGE   ")
    header += struct.pack(">LBBHBBBBL", CRT_HEADER_BYTES, 1, 0, MEGABYTER_TYPE,
                          0, 1, 0, 0, 0)
    header += name[:32].ljust(32, b"\0")
    if len(header) != CRT_HEADER_BYTES:
        raise AssertionError("invalid CRT header length")

    blank = b"\xff" * BANK_BYTES
    first = success_program().ljust(BANK_BYTES, b"\xff")
    packets = [bytes(header)]
    for bank in range(banks):
        packets.append(b"CHIP" + struct.pack(">LHHHH", CHIP_HEADER_BYTES + BANK_BYTES,
                                               0, bank, 0x8000, BANK_BYTES))
        packets.append(first if bank == 0 else blank)
    image = b"".join(packets)
    expected = CRT_HEADER_BYTES + banks * (CHIP_HEADER_BYTES + BANK_BYTES)
    if len(image) != expected:
        raise AssertionError(f"CRT is {len(image)}, expected {expected} bytes")
    return image


def wait_for_result(device: UltimateApi, timeout: float) -> Tuple[int, int]:
    deadline = time.monotonic() + timeout
    observed = (None, None)
    while time.monotonic() < deadline:
        border = device.machine.readmem(BORDER, 1)[0]
        debug = device.machine.readmem(DEBUG, 1)[0]
        observed = (border, debug)
        if observed == (GREEN, 0):
            return observed
        time.sleep(0.10)
    return observed  # type: ignore[return-value]


def run_cartridge(device: UltimateApi, banks: int, timeout: float) -> None:
    size_mib = banks * BANK_BYTES // (1024 * 1024)
    image = megabyter_crt(banks)
    detail(f"synthetic {size_mib} MiB Megabyter CRT: {len(image)} bytes, {banks} banks")
    # These writes use machine:writemem, so success must be caused by the
    # cartridge rather than inherited state.  The reads below use that same
    # DMA REST route, not a menu screenshot or an FPGA-side shortcut.
    device.machine.writemem(BORDER, bytes((BEFORE_BORDER,)), idempotent=True)
    device.machine.writemem(DEBUG, bytes((BEFORE_DEBUG,)), idempotent=True)
    before = (device.machine.readmem(BORDER, 1)[0], device.machine.readmem(DEBUG, 1)[0])
    if before != (BEFORE_BORDER, BEFORE_DEBUG):
        raise Failure(f"could not seed DMA observables: got ${before[0]:02X}, ${before[1]:02X}")

    code, _, body = device.runners.upload("run_crt", image)
    if code != 200:
        raise Failure(f"runners:run_crt for {size_mib} MiB returned HTTP {code}: {body[:160]!r}")
    border, debug = wait_for_result(device, timeout)
    if debug != 0:
        raise Failure(f"{size_mib} MiB cartridge left debug register ${debug:02X}, expected $00")
    if border != GREEN:
        raise Failure(f"{size_mib} MiB cartridge left border ${border:02X}, expected green $05")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float, default=float(os.environ.get("U64_TIMEOUT", "30")))
    args = parser.parse_args()
    device = UltimateApi(args.host, args.password, args.timeout)
    try:
        info = device.info()
        machine = machine_lib.identify(device.host, lambda: (info.product, info.firmware_version))
        if machine.kind != machine_lib.U64:
            with check("Megabyter cartridges require an Ultimate 64"):
                check_skip(f"{info.product or device.host} is not an Ultimate 64")
            suite_ok("megabyter_cartridge_test")
            return 0
        for banks in (128, 256):
            with check(f"a synthetic {banks * BANK_BYTES // (1024 * 1024)} MiB Megabyter cartridge runs via REST"):
                run_cartridge(device, banks, args.timeout)
    except Failure as exc:
        suite_fail("megabyter_cartridge_test", format_exception(exc))
        return 1
    suite_ok("megabyter_cartridge_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
