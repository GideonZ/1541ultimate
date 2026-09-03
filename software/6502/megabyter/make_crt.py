#!/usr/bin/env python3
"""
Deterministic .crt packer for the Megabyter self-test cartridge.

64tass produces a flat, linear ROM image (BANK_COUNT banks x 8192 bytes -
256 banks / 2 MiB for real Megabyter hardware, or fewer for a VICE-testable
build; see megabyter.tas's BANK_COUNT/SKIP_UCI_TEST .weak overrides and
build.sh's vice target). 64tass has no notion of the .crt container or its
CHIP packet framing, so this script wraps whatever raw image it's given
into a valid Megabyter .crt: one 64-byte CRT header followed by exactly
one CHIP packet per bank, each with the correct bank number and $8000
load address for the 8K ROM mapping. The bank count is taken from the
input file's actual size, not hardcoded, so the same script handles both
the full 256-bank hardware image and a reduced VICE debug image.

CRT type 86 = "Protovision Megabyter" (see the VICE/1541-Ultimate cartridge
type table in software/io/c64/c64_crt.cc: c_recognized_c64_carts[]). EXROM=0,
GAME=1 in the header records the cartridge's initial hardware state (8K ROM
mode, see megabyter.tas for the full explanation) - the Megabyter's actual
runtime EXROM/GAME lines are driven by its own $DE02 register, not by this
header field.

Usage: make_crt.py [rom_file] [crt_file]
  defaults: megabyter.bin -> megabyter.crt
"""
import struct
import sys
import os

BANK_SIZE  = 0x2000            # 8 KiB

CRT_SIGNATURE   = b"C64 CARTRIDGE   "   # 16 bytes, fixed
CRT_HEADER_LEN  = 0x40                  # standard 64-byte CRT header
CRT_VERSION_HI  = 1
CRT_VERSION_LO  = 0
CRT_TYPE_MEGABYTER = 86                 # on-disk CRT hardware-type id
CRT_EXROM       = 0                     # /EXROM active at power-on (8K ROM mode)
CRT_GAME        = 1                     # /GAME inactive at power-on (8K ROM mode)
CRT_SUBTYPE     = 0
CRT_NAME        = b"MEGABYTER SELFTEST"

CHIP_SIGNATURE  = b"CHIP"
CHIP_HEADER_LEN = 0x10                  # CHIP packet header size
CHIP_TYPE_ROM   = 0
CHIP_LOAD_ADDR  = 0x8000


def build_crt_header(bank_count):
    name = CRT_NAME
    if bank_count != 256:
        name = CRT_NAME + b" (%d BANKS)" % bank_count
    header = bytearray()
    header += CRT_SIGNATURE
    header += struct.pack(">LBBHBBBBL",
                           CRT_HEADER_LEN,
                           CRT_VERSION_HI, CRT_VERSION_LO,
                           CRT_TYPE_MEGABYTER,
                           CRT_EXROM, CRT_GAME,
                           CRT_SUBTYPE, 0,
                           0)
    name = name[:32] + b"\x00" * (32 - len(name[:32]))
    header += name
    assert len(header) == CRT_HEADER_LEN
    return bytes(header)


def build_chip_packet(bank, data):
    assert len(data) == BANK_SIZE
    packet = bytearray()
    packet += CHIP_SIGNATURE
    packet += struct.pack(">LHHHH",
                           CHIP_HEADER_LEN + BANK_SIZE,
                           CHIP_TYPE_ROM,
                           bank,
                           CHIP_LOAD_ADDR,
                           BANK_SIZE)
    packet += data
    assert len(packet) == CHIP_HEADER_LEN + BANK_SIZE
    return bytes(packet)


def main():
    rom_file = sys.argv[1] if len(sys.argv) > 1 else "megabyter.bin"
    crt_file = sys.argv[2] if len(sys.argv) > 2 else "megabyter.crt"

    with open(rom_file, "rb") as f:
        rom = f.read()

    if len(rom) % BANK_SIZE != 0 or len(rom) == 0:
        sys.exit("ERROR: %s is %d bytes, not a whole number of %d-byte banks"
                  % (rom_file, len(rom), BANK_SIZE))
    bank_count = len(rom) // BANK_SIZE
    if bank_count > 256:
        sys.exit("ERROR: %s implies %d banks, Megabyter only has 256"
                  % (rom_file, bank_count))

    with open(crt_file, "wb") as f:
        f.write(build_crt_header(bank_count))
        for bank in range(bank_count):
            offset = bank * BANK_SIZE
            data = rom[offset:offset + BANK_SIZE]
            f.write(build_chip_packet(bank, data))

    expected_crt_size = CRT_HEADER_LEN + bank_count * (CHIP_HEADER_LEN + BANK_SIZE)
    actual = os.path.getsize(crt_file)
    if actual != expected_crt_size:
        sys.exit("ERROR: %s is %d bytes, expected exactly %d"
                  % (crt_file, actual, expected_crt_size))

    print("Wrote %s: %d bytes (%d banks x %d bytes ROM + %d bytes header/CHIP overhead)"
          % (crt_file, actual, bank_count, BANK_SIZE,
             actual - len(rom)))


if __name__ == "__main__":
    main()
