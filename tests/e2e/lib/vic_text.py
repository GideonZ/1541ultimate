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

# How many cells of a frame may match no ROM shape at all before the frame is
# not a text screen this can read. A few can legitimately fail: a sprite over
# the text, a cell from the shifted set, a raster split. A window that is one
# pixel out fails hundreds, because every glyph becomes a fragment of two.
MAX_UNMATCHED = 24


# What a cell that is a ROM shape with no character of its own reads as. Codes
# 64 to 127 of the unshifted set are the PETSCII graphics, which have no ASCII
# form at all; they are decoration on a screen a reader searches for words, so
# they are marked rather than named. A cell that matches no ROM shape is a
# different answer and is counted against the frame instead.
GRAPHIC = "?"


def _reverse_map() -> Tuple[Dict[bytes, str], set]:
    """Every ROM shape, mapped back to a character where it has one.

    Built once, over the whole unshifted set rather than over the characters
    with names: a screen with a logo on it is still a text screen, and a shape
    this can identify but not name has to count as read.
    """
    named: Dict[bytes, str] = {}
    for character in [chr(code) for code in range(0x20, 0x60)]:
        index = glyphs.screen_code_for(character)
        if index == glyphs.NO_GLYPH:
            continue
        named.setdefault(bytes(glyphs.rom_rows_for_index(index)), character)
    known = {bytes(glyphs.rom_rows_for_index(index)) for index in range(128)}
    return named, known


_SHAPES, _KNOWN = _reverse_map()
_BLANK = bytes(glyphs.GLYPH_HEIGHT)


def picture_origin(width: int, height: int) -> Tuple[int, int]:
    """Where a 40-column picture area sits in a frame of this size, centred.

    The VIC centres it in the border, and the two frame heights the hardware
    produces differ by 32 lines, so this is arithmetic rather than a constant.
    It is the starting point for `decode`, which then looks for the window the
    machine is actually using.
    """
    return (max(0, (width - TEXT_WIDTH) // 2),
            max(0, (height - TEXT_HEIGHT) // 2))


# How far right of the centred origin the character grid is looked for. The VIC
# has two registers that change what the picture looks like: `$D016` bit 3
# selects 38 columns instead of 40, and its bottom three bits are a fine scroll
# of 0 to 7 pixels. Both are ordinary state, set by the KERNAL while it scrolls
# the screen, so a frame taken during a scroll is a correct picture rather than
# a damaged one. Measured on a C64 Ultimate and on an Ultimate II+L in one:
# about a quarter of the frames a run keeps as stills are in that state.
#
# Only the fine scroll moves the grid, and it only ever moves it right, so the
# grid is one of eight positions and nowhere else. The 38-column bit does not
# move it at all: it blanks one cell at each side of a grid that stays put, so
# a cell it covers is a cell no decode can read, and reading the grid from the
# window edge instead would report every column one to the left of where it is.
ORIGIN_SEARCH = CELL - 1


def _background(pixels: bytes, width: int, left: int, top: int):
    """The commonest colour in the picture area, which a text screen mostly is.

    Sampled every fourth pixel of every fourth line, because the mode of a
    sample of a text screen is its background and counting all of it costs
    forty times as much.
    """
    counts: Dict[int, int] = {}
    for y in range(top, top + TEXT_HEIGHT, 4):
        for value in pixels[y * width + left:y * width + left + TEXT_WIDTH:4]:
            counts[value] = counts.get(value, 0) + 1
    if not counts:
        return None
    return max(counts.items(), key=lambda item: (item[1], -item[0]))[0]


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
    centred, top = picture_origin(width, height)
    found = _content(pixels, width, height)
    if found is not None:
        # Only the vertical edge is taken from the picture. `$D011` selects 24
        # rows instead of 25 and scrolls vertically, and a row of text starts
        # exactly where the picture starts, so that axis is read rather than
        # searched.
        #
        # The horizontal edge is not the grid. The first pixel that is not the
        # border is the left of whatever is drawn, and in 38-column mode
        # (`$D016` bit 3) it is the window edge, which sits one cell inside the
        # grid. Anchoring the columns there decoded every cell one column to
        # the left of where it really is, and the shifted decode still matched
        # the ROM, so it was returned as if it were right: a screen reading
        # "READY." came back as "EADY." with everything after it displaced.
        # The grid itself is centred in both column modes, and only the fine
        # scroll moves it, so the columns are searched from the centred origin
        # outwards and the nearest candidate that matches everything wins.
        top = found[1]
    best: Optional[List[str]] = None
    fewest = MAX_UNMATCHED + 1
    for left in _candidates(centred, width):
        unmatched, text = _at(pixels, width, left, top)
        if unmatched == 0:
            return text
        if unmatched < fewest:
            best, fewest = text, unmatched
    return best if fewest <= MAX_UNMATCHED else None


def _content(pixels: bytes, width: int, height: int):
    """The top left of the picture the machine is drawing, or None.

    Found by asking where the frame stops being the border, which is one
    comparison per pixel of two scans rather than a decode per candidate.
    """
    border = pixels[0]
    top = None
    for y in range(height):
        row = pixels[y * width:(y + 1) * width]
        if any(value != border for value in row):
            top = y
            break
    if top is None or top + TEXT_HEIGHT > height:
        return None
    row = pixels[top * width:(top + 1) * width]
    left = next((x for x, value in enumerate(row) if value != border), None)
    if left is None or left + TEXT_WIDTH > width:
        return None
    return left, top


def _candidates(centred: int, width: int):
    """Where to look for the window, centred first, then the VIC's own moves.

    `$D016` bit 3 selects 38 columns instead of 40, which blanks 8 pixels at
    each side, and its bottom three bits are a fine scroll of 0 to 7 pixels.
    Both are ordinary state that the KERNAL sets while it scrolls the screen,
    so a frame taken during a scroll sits up to 15 pixels from the centred
    origin and is a correct picture rather than a damaged one. Measured on a
    C64 Ultimate and on an Ultimate II+L in one, about a quarter of the frames
    a run keeps as stills are in that state.

    The eight fine scroll positions are the whole search, and they are tried
    from the centred origin outwards so the ordinary case costs one decode.

    Nothing outside that range is offered, and that is the point. Reading the
    grid one whole cell to the left or right of where it is also matches the
    ROM everywhere, because the cell it invents at the edge is blank and the
    first or last column of a screen usually is too. Both readings decode, so
    a search wide enough to reach the wrong one will sometimes return it, and
    the text then sits one column from where the machine put it. A screen
    showing READY. came back as EADY. that way.
    """
    for offset in range(ORIGIN_SEARCH + 1):
        left = centred + offset
        if 0 <= left <= width - TEXT_WIDTH:
            yield left


def _at(pixels: bytes, width: int, left: int, top: int):
    """One decode at one origin: how many cells matched nothing, and the text."""

    background = _background(pixels, width, left, top)
    if background is None:
        return COLUMNS * ROWS, []

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
            # Reverse video: the same shape with every bit flipped, which is
            # how the C64 marks a selection and how the cursor blinks.
            flipped = bytes(0xFF ^ value for value in key)
            character = _SHAPES.get(key) or _SHAPES.get(flipped)
            if character is None:
                if key in _KNOWN or flipped in _KNOWN:
                    character = GRAPHIC
                else:
                    unmatched += 1
                    character = " " if key == _BLANK else GRAPHIC
            characters.append(character)
        text.append("".join(characters))
    return unmatched, text
