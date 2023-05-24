#!/usr/bin/python
header = """
library ieee;
use ieee.std_logic_1164.all;

package bootrom_pkg is
    type t_boot_data    is array(natural range <>) of std_logic_vector(31 downto 0);

    constant c_bootrom : t_boot_data(0 to 1023) := (
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

with open(sys.argv[2], "w") as o:
    o.write(header)

    with open(sys.argv[1], "rb") as i:
        for _ in range(127):
            o.write(makeline(i) + ",\n")
        o.write(makeline(i) + "\n")

    o.write(footer)
    
