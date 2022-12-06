import struct
import os

os.system("../../tools/64tass/64tass gmod2.tas -o gmod2.prg")


def add_chip(fo, fn, bank, load, size):
    fo.write(b"CHIP")
    fo.write(struct.pack(">LHHHH", size+16, 0, bank, load, size))
    try:
        fi = open(fn, "rb")
        data = fi.read(size)
        if len(data) != size:
            data += b'\xff' * (size - len(data))
    except FileNotFoundError:
        data = b'\xff' * size
    fo.write(data)

with open("gmod2.crt", "wb") as fo:
    fo.write(b"C64 CARTRIDGE   ")
    fo.write(struct.pack(">LBBHBBBBL", 0x40, 1, 0, 60, 1, 1, 0, 0, 0)) # Header Size, Cartridge version 1.0, HWTYPE, EXROM, GAME, Subtype, dummy, dummy  
    name = b"GMOD2 EEPROM TEST"
    name += b"\0" * (32 - len(name))
    fo.write(name)
    add_chip(fo, "gmod2.bin",  0, 0x8000, 0x2000)
    add_chip(fo, "gmod2.prg", 0, 0xDE00, 0x0800)

