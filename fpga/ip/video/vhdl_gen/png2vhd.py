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
#data = reader.asRGB()
w, h, pixels, meta = reader.read()
char_size = (w // 16, h // 16) # 16 characters in a row, 16 rows of characters
bitmap = list(pixels) # get image RGB values
writer_meta = {k: meta[k] for k in (
    "greyscale", "alpha", "bitdepth", "palette", "transparent",
    "background", "gamma", "interlace", "planes", "chroma"
) if k in meta}

take_cbm = [0x0b, 0x12, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f]
dest_pos = [0xd0, 0xd2, 0x80, 0x81, 0x91, 0x82, 0x90, 0x92, 0xd4, 0xd6, 0xd8, 0xda, 0xdc, 0xde]

cbm_chars = bytearray(2048)
with open("../../../../roms/chars.bin", "rb") as f:
    cbm_chars = f.read(2048)

for z,c in enumerate(take_cbm):
    dest = dest_pos[z]
    out_y = dest // 16 * 24
    char = cbm_chars[c*8:c*8+8]
    for i in range(8):
        out_x = (dest % 16) * 12 * 3
        write = []
        for p in range(8):
            if char[i] & (1 << p) != 0:
                write = [255, 255, 255] + write
                if p & 1 == 1:
                    write = [255, 255, 255] + write
            else:
                write = [0, 0, 0] + write
                if p & 1 == 1:
                    write = [0, 0, 0] + write

        write[0] = 128
        bitmap[out_y + 3*i + 0][out_x : out_x + len(write)] = write
        bitmap[out_y + 3*i + 1][out_x : out_x + len(write)] = write
        bitmap[out_y + 3*i + 2][out_x : out_x + len(write)] = write

with open("test.png", "wb") as output:
    png.Writer(w, h, **writer_meta).write(output, bitmap)

sys.stdout.write("""
library ieee;
use ieee.std_logic_1164.all;

package font_pkg is
    type t_font_array is array(natural range <>) of std_logic_vector(35 downto 0);
    
    constant c_font : t_font_array(0 to 128*8 - 1) := (
""")

raster = []
for line in bitmap:
    raster.append([c == 255 and 1 or 0 for c in [line[k+1] for k in range(0, w * 3, 3)]])

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

#buffer.raw("\x14\x15\x17\n\x18\x16\x19\n");
remap[0x14] = 0x80
remap[0x15] = 0x81
remap[0x17] = 0x82
remap[0x18] = 0x90
remap[0x16] = 0x91
remap[0x19] = 0x92

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

remap[11] = 0xd0
remap[12] = 0x0b
remap[13] = 0x0e
remap[14] = 0x0d
remap[0x12] = 0xd2
remap[0x1a] = 0xd4
remap[0x1b] = 0xd6
remap[0x1c] = 0xd8
remap[0x1d] = 0xda
remap[0x1e] = 0xdc
remap[0x1f] = 0xde

#define CHR_SOLID_BAR_LOWER_7   0x0B
#define CHR_ROW_LINE_RIGHT      0x0C
#define CHR_COLUMN_BAR_BOTTOM   0x0D // not usable!
#define CHR_ROUNDED_UPPER_RIGHT 0x0E
#define CHR_ROUNDED_UPPER_LEFT  0x0F
#define CHR_ALPHA               0x10
#define CHR_BETA                0x11
#define CHR_SOLID_BAR_UPPER_7   0x12
#define CHR_DIAMOND             0x13

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
