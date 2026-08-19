#!/usr/bin/env python3
"""
Deterministic .crt packer for the TwoMegabyter self-test cartridge.

64tass produces a flat, linear ROM image (BANK_COUNT banks x 16384 bytes -
128 banks / 2 MiB for real TwoMegabyter hardware, or fewer for a reduced
test image; see twomegabyter.tas's BANK_COUNT/SKIP_UCI_TEST .weak
overrides). 64tass has no notion of the .crt container or its CHIP packet
framing, so this script wraps whatever raw image it's given into a .crt:
one 64-byte CRT header followed by one CHIP packet per bank, each with the
correct bank number, $8000 load address, and 16 KiB size. The bank count
is taken from the input file's actual size, not hardcoded.

IMPORTANT - CRT_TYPE_TWOMEGABYTER below is a LOCAL PLACEHOLDER, not an
officially assigned VICE/1541-Ultimate cartridge type. As of this writing
TwoMegabyter is not registered in this repository's CRT loader
(software/io/c64/c64_crt.cc has no TwoMegabyter entry) and its hardware is
new enough that no confirmed VICE cartridge-type ID was available to check
against. The raw .bin is the artifact to actually flash onto real hardware
(e.g. via Protovision's own Flash API, see twomeg.pdf appendix A.1); treat
the .crt this script produces as best-effort, for once a loader exists,
and update CRT_TYPE_TWOMEGABYTER to match whatever ID is actually assigned.

Note this is purely a *CRT container* / C++ loader gap, not a register-
semantics one: fpga/cart_slot/vhdl_source/all_carts_v5.vhd's c_megabyter
case already implements this hardware correctly (a variant(0)='1' branch
alongside the original 256x8K variant(0)='0' one, with EXROM/GAME wired
exactly as twomeg.pdf documents - see twomegabyter.tas's header comment
for the full cross-check). What's still missing is C++-side plumbing in
c64_crt.cc to select variant(0)='1' for a TwoMegabyter CRT type once one
is registered; today loading any CRT type through CART_MEGABYTER drives
the FPGA with variant left at its default (0), i.e. the original 8K/256-
bank behaviour, not this one.

CRT EXROM=0/GAME=0 in the header records TwoMegabyter's initial hardware
state (Standard 16 KiB mode, both control bits 0 at boot/reset per
twomeg.pdf section 2.2) - the cartridge's actual runtime EXROM/GAME lines
are driven by its own $DE02 register, not by this header field.

Usage: make_crt_twomeg.py [rom_file] [crt_file]
  defaults: twomegabyter.bin -> twomegabyter.crt
"""
import struct
import sys
import os

BANK_SIZE  = 0x4000            # 16 KiB

CRT_SIGNATURE   = b"C64 CARTRIDGE   "   # 16 bytes, fixed
CRT_HEADER_LEN  = 0x40                  # standard 64-byte CRT header
CRT_VERSION_HI  = 1
CRT_VERSION_LO  = 0
CRT_TYPE_TWOMEGABYTER = 87              # PLACEHOLDER - not yet registered, see module docstring
CRT_EXROM       = 0                     # EXROM low at power-on (Standard 16 KiB mode)
CRT_GAME        = 0                     # GAME low at power-on (Standard 16 KiB mode)
CRT_SUBTYPE     = 0
CRT_NAME        = b"TWOMEGABYTER SELFTEST"

CHIP_SIGNATURE  = b"CHIP"
CHIP_HEADER_LEN = 0x10                  # CHIP packet header size
CHIP_TYPE_ROM   = 0
CHIP_LOAD_ADDR  = 0x8000


def build_crt_header(bank_count):
    name = CRT_NAME
    if bank_count != 128:
        name = CRT_NAME + b" (%d BANKS)" % bank_count
    header = bytearray()
    header += CRT_SIGNATURE
    header += struct.pack(">LBBHBBBBL",
                           CRT_HEADER_LEN,
                           CRT_VERSION_HI, CRT_VERSION_LO,
                           CRT_TYPE_TWOMEGABYTER,
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
    rom_file = sys.argv[1] if len(sys.argv) > 1 else "twomegabyter.bin"
    crt_file = sys.argv[2] if len(sys.argv) > 2 else "twomegabyter.crt"

    with open(rom_file, "rb") as f:
        rom = f.read()

    if len(rom) % BANK_SIZE != 0 or len(rom) == 0:
        sys.exit("ERROR: %s is %d bytes, not a whole number of %d-byte banks"
                  % (rom_file, len(rom), BANK_SIZE))
    bank_count = len(rom) // BANK_SIZE
    if bank_count > 128:
        sys.exit("ERROR: %s implies %d banks, TwoMegabyter only has 128 ($DE00 accepts $00-$7F)"
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
