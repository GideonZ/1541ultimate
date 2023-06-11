#!/usr/bin/python
header = """
library ieee;
use ieee.std_logic_1164.all;

package bootrom_pkg is
    type t_boot_data    is array(natural range <>) of std_logic_vector(31 downto 0);

    constant c_bootrom : t_boot_data(0 to {0:d}) := (
"""

footer = """
    );
end package;
"""
import struct
import sys

def makeline(infile):
    data = i.read(32)
    if len(data) < 32:
        data += bytearray(32 - len(data))
    w = struct.unpack("<LLLLLLLL", data)
    return "        " + ", ".join([f'X"{x:08x}"' for x in w])

bytes = 4096
if len(sys.argv) > 3:
    bytes = int(sys.argv[3])

with open(sys.argv[2], "w") as o:
    words = bytes // 4
    rows = words // 8
    o.write(header.format(words - 1))

    with open(sys.argv[1], "rb") as i:
        for _ in range(rows - 1):
            o.write(makeline(i) + ",\n")
        o.write(makeline(i) + "\n")

    o.write(footer)
