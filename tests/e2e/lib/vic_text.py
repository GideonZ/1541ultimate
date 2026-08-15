"""What the C64 was showing, decoded from the frames the recorder already has.

The video stream carries a bitmap: 4-bit VIC colour indices, two to a byte, one
per pixel. That is what a person looks at and what a program cannot search. The
device's own screen memory would be searchable, but reading it means a
`machine:readmem` per screen against a device the suites are already driving,
which is load this layer is not allowed to add (OBS-15.1).

So the text is recovered from the picture. The C64's text mode draws each cell
as one of 256 fixed 8x8 shapes from the character ROM, in two colours, and the
recorder already holds that ROM to draw the harness pane with. Matching a cell's
8x8 bitmask against the ROM is therefore exact rather than approximate: a cell
either is one of the 256 shapes, in normal or reverse video, or the screen is
not in text mode and this says so rather than guessing.

What it cannot do, and does not pretend to: bitmap mode, sprites over text,
smooth scrolling that puts a cell off the 8-pixel grid, and the shifted
character set, which the ROM's second half holds and which `glyphs` does not
load. Each of those makes cells that match nothing, and enough of them makes the
frame undecodable, which is the honest answer.
"""

from __future__ import annotations

from typing import Dict, List, Optional, Tuple

import glyphs

# The C64's text screen inside the border: 40 columns by 25 rows of 8x8 cells.
COLUMNS = 40
ROWS = 25
CELL = glyphs.GLYPH_WIDTH
TEXT_WIDTH = COLUMNS * CELL
TEXT_HEIGHT = ROWS * glyphs.GLYPH_HEIGHT

# How many cells of a frame may match no ROM shape before the frame is not a
# text screen. A few cells can legitimately fail: a sprite over the text, a
# cell from the shifted set, a raster split. Half of them cannot.
MAX_UNMATCHED = (COLUMNS * ROWS) // 2


def _reverse_map() -> Dict[bytes, str]:
    """Every ROM shape, as its 8 row bitmasks, mapped back to a character.

    Built once. Where two screen codes draw the same shape the lower one wins,
    which is the printable half of the pair for every duplicate the unshifted
    set has.
    """
    found: Dict[bytes, str] = {}
    for character in [chr(code) for code in range(0x20, 0x60)]:
        index = glyphs.screen_code_for(character)
        if index == glyphs.NO_GLYPH:
            continue
        rows = bytes(glyphs.rom_rows_for_index(index))
        found.setdefault(rows, character)
    return found


_SHAPES: Dict[bytes, str] = _reverse_map()
_BLANK = bytes(glyphs.GLYPH_HEIGHT)


def picture_origin(width: int, height: int) -> Tuple[int, int]:
    """Where the 320x200 picture area starts inside a frame of this size.

    The VIC centres it in the border, and the two frame heights the hardware
    produces differ by 32 lines, so this is arithmetic rather than a constant.
    """
    return (max(0, (width - TEXT_WIDTH) // 2),
            max(0, (height - TEXT_HEIGHT) // 2))


def decode(pixels: bytes, width: int, height: int) -> Optional[List[str]]:
    """The 25 rows of 40 characters this frame is showing, or None.

    `pixels` is one colour index per byte, which is what `streams.unpack`
    produces. None means the frame is not a text screen this can read, which is
    a different answer from a screen of spaces.
    """
    if width < TEXT_WIDTH or height < TEXT_HEIGHT:
        return None
    if len(pixels) < width * height:
        return None
    left, top = picture_origin(width, height)

    # The background is the commonest colour in the picture area, sampled every
    # fourth pixel of every fourth line. A text screen is mostly background, so
    # the mode of a sample is it, and sampling costs a fortieth of counting.
    counts: Dict[int, int] = {}
    for y in range(top, top + TEXT_HEIGHT, 4):
        row = pixels[y * width + left:y * width + left + TEXT_WIDTH:4]
        for value in row:
            counts[value] = counts.get(value, 0) + 1
    if not counts:
        return None
    background = max(counts.items(), key=lambda item: (item[1], -item[0]))[0]

    # One pass over the picture area turning it into a bit per pixel: 1 where
    # the pixel is not the background. Done with translate over whole rows,
    # because a per-pixel loop over 64000 pixels is not affordable at the
    # output rate.
    table = bytes(0 if index == background else 1 for index in range(256))
    lines: List[bytes] = []
    for y in range(top, top + TEXT_HEIGHT):
        start = y * width + left
        lines.append(pixels[start:start + TEXT_WIDTH].translate(table))

    text: List[str] = []
    unmatched = 0
    for row in range(ROWS):
        characters: List[str] = []
        for column in range(COLUMNS):
            left_pixel = column * CELL
            shape = bytearray(glyphs.GLYPH_HEIGHT)
            for line in range(glyphs.GLYPH_HEIGHT):
                bits = lines[row * glyphs.GLYPH_HEIGHT + line][
                    left_pixel:left_pixel + CELL]
                value = 0
                for bit in bits:
                    value = (value << 1) | bit
                shape[line] = value
            key = bytes(shape)
            character = _SHAPES.get(key)
            if character is None:
                # Reverse video: the same shape with every bit flipped, which
                # is how the C64 marks a selection and how the cursor blinks.
                flipped = bytes(0xFF ^ value for value in key)
                character = _SHAPES.get(flipped)
            if character is None:
                unmatched += 1
                character = " " if key == _BLANK else "?"
            characters.append(character)
        text.append("".join(characters))
    if unmatched > MAX_UNMATCHED:
        return None
    return text
