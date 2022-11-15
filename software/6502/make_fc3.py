import struct
import os

os.system("../../tools/64tass/64tass fc3_wedge.tas -b -o bank0.bin")


def add_chip(fo, i):
    fo.write(b"CHIP")
    fo.write(struct.pack(">LHHHH", 0x4010, 0, i, 0x8000, 0x4000))
    try:
        fi = open("bank" + str(i) + ".bin", "rb")
        data = fi.read(0x4000)
    except FileNotFoundError:
        data = b'\xff' * 0x4000
    fo.write(data)

with open("fc3_wedge.crt", "wb") as fo:
    fo.write(b"C64 CARTRIDGE   ")
    fo.write(struct.pack(">LBBHBBHL", 0x40, 1, 0, 3, 1, 1, 0, 0))
    name = b"U64 DOS WEDGE V0.1"
    name += b"\0" * (32 - len(name))
    fo.write(name)
    for i in range(4):
        add_chip(fo, i)

