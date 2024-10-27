#!/usr/bin/python2.7
import sys
import curses.ascii
import png

def rreplace(s, old, new, occurrence):
    li = s.rsplit(old, occurrence)
    return new.join(li)

if len(sys.argv) < 2:
    sys.stderr.write("Please specify the input PNG file\n")
    sys.exit(1)

reader = png.Reader(filename=sys.argv[1])
data = reader.asRGB()
size = data[:2] # get image width and height
char_size = (size[0] // 16, size[1] // 16) # 16 characters in a row, 16 rows of characters
bitmap = list(data[2]) # get image RGB values

sys.stdout.write("""
library ieee;
use ieee.std_logic_1164.all;

package font_pkg is
    type t_font_array is array(natural range <>) of std_logic_vector(35 downto 0);
    
    constant c_font : t_font_array(0 to 128*8 - 1) := (
""")

raster = []
for line in bitmap:
    raster.append([c == 255 and 1 or 0 for c in [line[k+1] for k in range(0, size[0] * 3, 3)]])

remap = [c for c in range(256)]
# 1 = corner lower right
# 2 = horizontal bar
# 3 = corner lower left
# 4 = vertical bar
# 5 = corner upper right
# 6 = corner upper left
# 7 = rounded corner lower right
# 8 = T
# 9 = rounded corner lower left
# A = |-
# B = +
# C = -|
# D = rounded corner upper right
# E = _|_
# F = rounded corner upper left
# 10 = alpha
# 11 = beta
# 12 = test grid
# 13 = diamond
#remap[1] = 0xda
#remap[2] = 0xc4
#remap[3] = 0xbf
#remap[4] = 0xb3
#remap[5] = 0xc0
#remap[6] = 0xd9
#remap[7] = 0xda
#remap[8] = 0xc2
#remap[9] = 0xbf
#remap[10] = 0xc3
#remap[11] = 0xc5
#remap[12] = 0xb4
#remap[13] = 0xc0
#remap[14] = 0xc1
#remap[15] = 0xd9
#remap[16] = 0xe0
#remap[17] = 0xe1
#remap[18] = 0xb2
#remap[19] = 0x04

# array of character bitmaps; each bitmap is an array of lines, each line
# consists of 1 - bit is set and 0 - bit is not set
char_bitmaps = [] 
for ci in range(256): # for each character
    c = remap[ci]
    char_bitmap = []
    raster_row = (c // 16) * char_size[1]
    offset = (c % 16) * char_size[0]
    for y in range(char_size[1]): # for each scan line of the character
        char_bitmap.append(raster[raster_row + y][offset : offset + char_size[0]])
    char_bitmaps.append(char_bitmap)
raster = None # no longer required

# how many bytes a single character scan line should be
num_bytes_per_scanline = (char_size[0] + 7) // 8

# convert the whole bitmap into an array of character bitmaps
char_bitmaps_processed = []
for c in range(len(char_bitmaps)//2):
    bitmap = char_bitmaps[c]
    encoded_lines = []
    for line in bitmap:
        encoded_line = []
        for b in range(num_bytes_per_scanline):
            offset = b * 8
            char_byte = 0
            mask = 0x80
            for x in range(8):
                if b * 8 + x >= char_size[0]:
                    break
                if line[offset + x]:
                    char_byte += mask
                mask >>= 1
            encoded_line.append(char_byte)
        encoded_lines.append([encoded_line, line])
    char_bitmaps_processed.append([c, encoded_lines])
char_bitmaps = None

outstring = ""
for c in char_bitmaps_processed:
# c is [char code, [[bytes], [bits]]]
    desc = f"code={c[0]:02x}, ascii={curses.ascii.unctrl(c[0])}"
    outstring += f"\n    -- {desc}\n\n"
    for n in range(len(c[1])//3):
        line0 = c[1][3*n+0]
        line1 = c[1][3*n+1]
        line2 = c[1][3*n+2]
        bitstr0 = ''.join([str(s) for s in line0[1]])
        bitstr1 = ''.join([str(s) for s in line1[1]])
        bitstr2 = ''.join([str(s) for s in line2[1]])
        bitstr = bitstr2 + bitstr1 + bitstr0
        outstring += f'    X"{int(bitstr,2):09x}", -- {bitstr0.replace("0", " ")}\n'
        outstring += f'                  -- {bitstr1.replace("0", " ")}\n'
        outstring += f'                  -- {bitstr2.replace("0", " ")}\n'


sys.stdout.write(f'{rreplace(outstring, ",", " ", 1)}')
sys.stdout.write("""
    );
end package;
""")
