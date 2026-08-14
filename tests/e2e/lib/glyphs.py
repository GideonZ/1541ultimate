"""Render C64 character-ROM glyphs into a 24-bit RGB pixel buffer.

Standard library only. Used to turn REST/Telnet screen payloads (the
machine:menu_screen 40x25 grid and a Telnet session's plain text rows) into
pixels for a video encoder, without pulling in Pillow.

The palette (`c64_rgb`), the menu payload's character-to-glyph table
(`menu_char_to_glyph`) and the colour-byte nibble split (`split_colour_byte`)
already live in tools/api/menu_screen_tool.py and are loaded from there by
path rather than re-typed here, so the two files cannot drift apart.
"""

from __future__ import annotations

import importlib.machinery
import sys
import types
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

GLYPH_WIDTH = 8
GLYPH_HEIGHT = 8

# Sentinel returned by screen_code_for() for a character the unshifted
# character ROM has no glyph for. Kept distinct from any real ROM index
# (0-255) so callers can tell "no glyph" apart from "glyph 0".
NO_GLYPH = -1

# Both the ROM image and menu_screen_tool.py are addressed from the repository
# root, so this works whatever the caller's current working directory is.
_REPO_ROOT = Path(__file__).resolve().parents[3]
_CHAR_ROM_PATH = _REPO_ROOT / "roms" / "characters.901225-01.bin"
_MENU_SCREEN_TOOL_PATH = _REPO_ROOT / "tools" / "api" / "menu_screen_tool.py"

_CHARS_PER_SET = 256
_BYTES_PER_CHAR = 8


def _load_module_by_path(module_name: str, path: Path) -> types.ModuleType:
    """Load a module by file path without adding it to sys.path.

    The module is registered in sys.modules before exec_module runs, as
    required for a SourceFileLoader-loaded module to behave like a normally
    imported one (self-references via sys.modules, dataclasses, etc. all
    expect the module to already be findable while its body executes).
    """
    loader = importlib.machinery.SourceFileLoader(module_name, str(path))
    module = types.ModuleType(loader.name)
    sys.modules[module_name] = module
    loader.exec_module(module)
    return module


_menu_screen_tool = _load_module_by_path("glyphs_menu_screen_tool", _MENU_SCREEN_TOOL_PATH)
c64_rgb = _menu_screen_tool.c64_rgb
menu_char_to_glyph = _menu_screen_tool.menu_char_to_glyph
split_colour_byte = _menu_screen_tool.split_colour_byte

# The menu payload's own grid geometry (machine:menu_screen is a fixed
# 40x25 char/colour pair of planes); reused from menu_screen_tool.py so the
# 1000-cell layout is stated once.
_MENU_COLUMNS = _menu_screen_tool.SCREEN_WIDTH
_MENU_ROWS = _menu_screen_tool.SCREEN_HEIGHT
_MENU_CELLS = _menu_screen_tool.SCREEN_CELLS


def _load_unshifted_rom_rows(path: Path) -> List[bytes]:
    """Load the unshifted (upper case + graphics) 256-glyph set.

    characters.901225-01.bin is 4096 bytes: two 2048-byte sets of 256
    characters at 8 bytes/char, one byte per pixel row, bit 7 leftmost.
    The first 2048 bytes are the unshifted set; only that set is used here,
    per the task's ROM-layout note. Each ROM byte already IS the row's
    pixel bitmask (bit 7 leftmost), so no further transform is needed once
    read; this happens once, at import time, so per-frame drawing never
    touches the file again.
    """
    try:
        data = path.read_bytes()
    except OSError:
        data = b""

    unshifted = data[: _CHARS_PER_SET * _BYTES_PER_CHAR]
    # Pad a short/missing ROM file with blank glyphs rather than raising,
    # matching the rest of this module's "draw what it has" behaviour.
    if len(unshifted) < _CHARS_PER_SET * _BYTES_PER_CHAR:
        unshifted = unshifted + bytes(_CHARS_PER_SET * _BYTES_PER_CHAR - len(unshifted))

    return [unshifted[i * _BYTES_PER_CHAR : (i + 1) * _BYTES_PER_CHAR] for i in range(_CHARS_PER_SET)]


_ROM_ROWS: List[bytes] = _load_unshifted_rom_rows(_CHAR_ROM_PATH)
_BLANK_GLYPH_ROWS: bytes = bytes(GLYPH_HEIGHT)

# Per-index RGB translate tables for blit_indices. c64_rgb only defines 16
# colours and itself masks its argument to 4 bits, so every one of the 256
# possible input byte values (a VIC index frame is not guaranteed to be
# pre-masked) resolves to a valid palette entry via the same masking rule.
_R_TABLE = bytes(c64_rgb(i & 0x0F)[0] for i in range(256))
_G_TABLE = bytes(c64_rgb(i & 0x0F)[1] for i in range(256))
_B_TABLE = bytes(c64_rgb(i & 0x0F)[2] for i in range(256))


def screen_code_for(character: str) -> int:
    """Return the character ROM index (0-255) that draws `character`.

    This implements the standard C64 screen-code arithmetic (documented on
    the c64-wiki "Screencodes" page): codes 0x20-0x3F are identical to
    ASCII (space through '?'), and '@' through '_' (0x40-0x5F) map to
    codes 0-31 by subtracting 0x40. Lower case a-z folds onto the same 26
    slots as upper case, because the unshifted set loaded by this module
    has no separate lower-case glyphs, only the shifted set does.

    Characters outside those ranges, including the Unicode box-drawing and
    Greek letters menu_char_to_glyph substitutes for firmware UI icons,
    have no corresponding shape in the character ROM at all (those
    substitutions exist for terminal display, not because the ROM contains
    them), so they return NO_GLYPH.
    """
    if len(character) != 1:
        return NO_GLYPH

    code = ord(character)

    if 0x20 <= code <= 0x3F:
        return code
    if 0x40 <= code <= 0x5F:
        return code - 0x40
    if 0x61 <= code <= 0x7A:
        return code - 0x60

    return NO_GLYPH


# One glyph's eight rows, already expanded to RGB, keyed by the character and
# the colour pair it is drawn in. Bounded by the character set times the pairs
# a caller actually uses, which for this module's callers is a handful.
_GLYPH_ROWS: "dict" = {}


def _glyph_rows(character: str, fg_rgb: bytes, bg_rgb: bytes) -> "list":
    key = (character, fg_rgb, bg_rgb)
    made = _GLYPH_ROWS.get(key)
    if made is None:
        made = []
        for row_bits in _rom_rows_for(character):
            row = bytearray()
            for col_offset in range(GLYPH_WIDTH):
                # Bit 7 is the leftmost pixel of the row, per the ROM layout
                # note: col_offset 0 must read bit 7, not bit 0.
                row += fg_rgb if (row_bits >> (7 - col_offset)) & 1 else bg_rgb
            made.append(bytes(row))
        _GLYPH_ROWS[key] = made
    return made


def _rom_rows_for(character: str) -> bytes:
    """Look up the pre-loaded 8-byte row bitmask for `character`."""
    index = screen_code_for(character)
    if index == NO_GLYPH:
        return _BLANK_GLYPH_ROWS
    return _ROM_ROWS[index]


class Canvas:
    """A 24-bit RGB pixel buffer, addressed in pixels, written as bytes.

    Pixels are stored row major as flat RGB triples so `to_rgb()` can hand
    the whole buffer to an encoder without a copy-and-reshape step.
    """

    def __init__(self, width: int, height: int, colour: int = 0) -> None:
        """Create a width x height canvas filled with `colour` (a VIC index)."""
        self.width = width
        self.height = height
        r, g, b = c64_rgb(colour)
        self._pixels = bytearray((r, g, b)) * (width * height)

    def _clip_rect(self, x: int, y: int, w: int, h: int) -> Optional[Tuple[int, int, int, int]]:
        """Intersect (x, y, w, h) with the canvas, or None if it is empty.

        Every drawing entry point runs its target rectangle through this so
        text or blits placed partly or fully off-canvas draw what fits
        rather than raising (menu cells and glyph columns near an edge are
        the normal case, not an error case).
        """
        x0 = max(x, 0)
        y0 = max(y, 0)
        x1 = min(x + w, self.width)
        y1 = min(y + h, self.height)
        if x0 >= x1 or y0 >= y1:
            return None
        return x0, y0, x1, y1

    def fill(self, x: int, y: int, w: int, h: int, colour: int) -> None:
        """Fill the w x h rectangle at (x, y) with a solid VIC colour."""
        clipped = self._clip_rect(x, y, w, h)
        if clipped is None:
            return
        x0, y0, x1, y1 = clipped

        rgb = bytes(c64_rgb(colour))
        row = rgb * (x1 - x0)
        stride = self.width * 3
        row_len = len(row)
        for py in range(y0, y1):
            offset = py * stride + x0 * 3
            self._pixels[offset : offset + row_len] = row

    def blit_indices(self, x: int, y: int, w: int, h: int, indices: bytes) -> None:
        """Draw w*h one-byte VIC colour indices at (x, y).

        This is the per-frame VIC path (up to 384*272 = 104448 indices at
        25 fps), so the index-to-RGB expansion must avoid a per-pixel
        Python loop. Each of the R, G and B planes is produced with one
        `bytes.translate` call (a 256-byte table, C-level per input byte),
        and the three planes are interleaved with extended-slice
        assignment (also C-level), so the only Python-level loop left is
        over output rows, needed to place the blit inside a possibly
        larger and clipped canvas.
        """
        n = w * h
        data = bytes(indices[:n])
        if len(data) < n:
            # A short index buffer draws what it has; the remainder is
            # treated as colour 0 (black), same as an all-zero VIC frame.
            data = data + bytes(n - len(data))

        r = data.translate(_R_TABLE)
        g = data.translate(_G_TABLE)
        b = data.translate(_B_TABLE)
        rgb = bytearray(n * 3)
        rgb[0::3] = r
        rgb[1::3] = g
        rgb[2::3] = b

        clipped = self._clip_rect(x, y, w, h)
        if clipped is None:
            return
        x0, y0, x1, y1 = clipped

        # Common case: an unclipped full-canvas blit is one bulk copy.
        if x == 0 and y == 0 and w == self.width and h == self.height and (x0, y0, x1, y1) == (0, 0, w, h):
            self._pixels[:] = rgb
            return

        src_stride = w * 3
        dst_stride = self.width * 3
        copy_len = (x1 - x0) * 3
        src_x_offset = (x0 - x) * 3
        for row in range(y1 - y0):
            src_y = row + (y0 - y)
            src_offset = src_y * src_stride + src_x_offset
            dst_offset = (y0 + row) * dst_stride + x0 * 3
            self._pixels[dst_offset : dst_offset + copy_len] = rgb[src_offset : src_offset + copy_len]

    def _set_pixel(self, x: int, y: int, rgb: bytes) -> None:
        offset = (y * self.width + x) * 3
        self._pixels[offset : offset + 3] = rgb

    def draw_text(self, x: int, y: int, text: str, colour: int, background: Optional[int] = None) -> None:
        """Draw text at (x, y) in 8x8 glyphs from the character ROM.

        With `background` given, each glyph cell is fully opaque (every
        pixel written, foreground where the ROM bit is set, background
        elsewhere). With `background` left as None, only the foreground
        bits are written and the canvas underneath a glyph's gaps is left
        untouched, for overlaying text on an already-drawn frame.
        """
        fg_rgb = bytes(c64_rgb(colour))
        bg_rgb = bytes(c64_rgb(background)) if background is not None else None

        if bg_rgb is not None:
            self._draw_opaque_text(x, y, text, fg_rgb, bg_rgb)
            return

        cursor_x = x
        for character in text:
            rows = _rom_rows_for(character)
            for row_offset in range(GLYPH_HEIGHT):
                py = y + row_offset
                if py < 0 or py >= self.height:
                    continue
                row_bits = rows[row_offset]
                for col_offset in range(GLYPH_WIDTH):
                    px = cursor_x + col_offset
                    if px < 0 or px >= self.width:
                        continue
                    # Bit 7 is the leftmost pixel of the row, per the ROM
                    # layout note: col_offset 0 must read bit 7, not bit 0.
                    bit_set = (row_bits >> (7 - col_offset)) & 1
                    if bit_set:
                        self._set_pixel(px, py, fg_rgb)
                    elif bg_rgb is not None:
                        self._set_pixel(px, py, bg_rgb)
            cursor_x += GLYPH_WIDTH

    def _draw_opaque_text(self, x: int, y: int, text: str,
                          fg_rgb: bytes, bg_rgb: bytes) -> None:
        """The whole-cell path: one slice assignment per glyph row.

        A 60x24 harness screen is 1440 cells, and a per-pixel loop over it is
        about 92,000 interpreted writes on every frame that is recomposed. The
        rows of one glyph in one colour pair are the same every time they are
        drawn, so they are built once and cached, and drawing becomes one
        slice assignment per row.
        """
        stride = self.width * 3
        cursor_x = x
        for character in text:
            if (cursor_x >= self.width or cursor_x + GLYPH_WIDTH <= 0
                    or y >= self.height or y + GLYPH_HEIGHT <= 0):
                cursor_x += GLYPH_WIDTH
                continue
            if (cursor_x < 0 or cursor_x + GLYPH_WIDTH > self.width
                    or y < 0 or y + GLYPH_HEIGHT > self.height):
                # Partly outside the canvas, which the cached whole rows
                # cannot express. Rare enough to be worth no second cache.
                self._draw_clipped_glyph(cursor_x, y, character, fg_rgb, bg_rgb)
                cursor_x += GLYPH_WIDTH
                continue
            rows = _glyph_rows(character, fg_rgb, bg_rgb)
            offset = y * stride + cursor_x * 3
            for row in rows:
                self._pixels[offset:offset + GLYPH_WIDTH * 3] = row
                offset += stride
            cursor_x += GLYPH_WIDTH

    def _draw_clipped_glyph(self, x: int, y: int, character: str,
                            fg_rgb: bytes, bg_rgb: bytes) -> None:
        rows = _rom_rows_for(character)
        for row_offset in range(GLYPH_HEIGHT):
            py = y + row_offset
            if py < 0 or py >= self.height:
                continue
            row_bits = rows[row_offset]
            for col_offset in range(GLYPH_WIDTH):
                px = x + col_offset
                if px < 0 or px >= self.width:
                    continue
                bit_set = (row_bits >> (7 - col_offset)) & 1
                self._set_pixel(px, py, fg_rgb if bit_set else bg_rgb)

    def to_rgb(self) -> bytes:
        """Return the buffer as width*height*3 bytes, row major, for an encoder."""
        return bytes(self._pixels)


def render_menu_screen(
    payload: bytes,
    canvas: Canvas,
    x: int,
    y: int,
    swap_colour_nibbles: bool = False,
    background: int = 0,
) -> None:
    """Draw a machine:menu_screen payload as 40x25 cells of 8x8 glyphs.

    The payload is 2000 bytes when complete: bytes 0-999 are the character
    plane (40x25, row major) and bytes 1000-1999 are the matching colour
    plane. Bit 7 of a character byte is reverse video (the firmware's only
    way to mark the selected row on a machine whose colour plane carries no
    background nibble); bits 0-6 are the glyph, decoded through
    menu_char_to_glyph and then screen_code_for to find the ROM shape,
    since the payload carries literal ASCII, not character-ROM indices.
    A payload shorter than 2000 bytes draws only the cells it has data
    for; a colour byte missing for an otherwise-present character cell
    falls back to the caller-supplied `background` for both halves of that
    cell, since split_colour_byte has no byte to read a background nibble
    from in that case.
    """
    for row in range(_MENU_ROWS):
        for col in range(_MENU_COLUMNS):
            idx = row * _MENU_COLUMNS + col
            cell_x = x + col * GLYPH_WIDTH
            cell_y = y + row * GLYPH_HEIGHT

            if idx >= len(payload):
                canvas.fill(cell_x, cell_y, GLYPH_WIDTH, GLYPH_HEIGHT, background)
                continue

            char_byte = payload[idx]
            colour_idx = _MENU_CELLS + idx
            if colour_idx < len(payload):
                foreground, cell_background = split_colour_byte(payload[colour_idx], swap_colour_nibbles)
            else:
                foreground, cell_background = background, background

            # Reverse video (bit 7) swaps foreground and background for
            # this cell; it does not select a different ROM glyph.
            if char_byte & 0x80:
                foreground, cell_background = cell_background, foreground

            glyph_char = menu_char_to_glyph(char_byte)
            canvas.draw_text(cell_x, cell_y, glyph_char, foreground, cell_background)


def render_text_screen(
    rows: Sequence[str],
    canvas: Canvas,
    x: int,
    y: int,
    colour: int,
    background: int,
) -> None:
    """Draw plain rows of text, for a Telnet session's 60x24 screen.

    Unlike the menu payload, a Telnet screen carries no per-cell colour
    plane, so every row shares the one `colour`/`background` pair.
    """
    for row_index, text in enumerate(rows):
        canvas.draw_text(x, y + row_index * GLYPH_HEIGHT, text, colour, background)
