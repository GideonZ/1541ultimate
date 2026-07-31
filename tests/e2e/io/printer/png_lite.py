"""Minimal pure-stdlib PNG decoder for virtual-printer output verification.

Handles what the printer's PNG encoder actually emits: non-interlaced,
grayscale or palette color type, 1/2/4/8-bit depth. Not a general PNG
decoder. Used to check that a "full page" bitmap print genuinely covers the
page, not just that the file happens to be a structurally valid PNG.
"""

import struct
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PngError(RuntimeError):
    pass


def parse_chunks(data):
    if data[:8] != PNG_SIGNATURE:
        raise PngError("missing PNG signature")
    offset = 8
    chunks = []
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        chunk_type = data[offset + 4:offset + 8]
        chunk_data = data[offset + 8:offset + 8 + length]
        if offset + 12 + length > len(data):
            raise PngError(f"truncated chunk {chunk_type!r}")
        crc_stored = struct.unpack(">I", data[offset + 8 + length:offset + 12 + length])[0]
        crc_calc = zlib.crc32(chunk_type + chunk_data) & 0xFFFFFFFF
        if crc_calc != crc_stored:
            raise PngError(f"bad CRC in chunk {chunk_type!r}")
        chunks.append((chunk_type, chunk_data))
        offset += 12 + length
        if chunk_type == b"IEND":
            break
    return chunks


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_indexed(data):
    """Decode a grayscale or palette PNG into (width, height, rows), where
    rows is a list of `height` lists of `width` per-pixel sample values
    (palette index, or grayscale level for color type 0)."""
    chunks = parse_chunks(data)
    ihdr = None
    idat = bytearray()
    for chunk_type, chunk_data in chunks:
        if chunk_type == b"IHDR":
            ihdr = chunk_data
        elif chunk_type == b"IDAT":
            idat += chunk_data
    if ihdr is None:
        raise PngError("no IHDR chunk")

    width, height, bitdepth, colortype, compression, filter_method, interlace = struct.unpack(">IIBBBBB", ihdr)
    if compression != 0 or filter_method != 0:
        raise PngError("unsupported compression/filter method")
    if interlace != 0:
        raise PngError("interlaced PNG not supported")
    if colortype not in (0, 3):
        raise PngError(f"unsupported color type {colortype} (expected grayscale or palette)")
    if bitdepth not in (1, 2, 4, 8):
        raise PngError(f"unsupported bit depth {bitdepth}")

    raw = zlib.decompress(bytes(idat))

    bytes_per_row = (width * bitdepth + 7) // 8
    bpp = max(1, bitdepth // 8)  # bytes-per-pixel for filter reconstruction

    rows = []
    prev = bytearray(bytes_per_row)
    pos = 0
    for _y in range(height):
        if pos >= len(raw):
            raise PngError("IDAT stream shorter than IHDR height implies")
        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + bytes_per_row])
        pos += bytes_per_row

        if filter_type == 0:
            pass
        elif filter_type == 1:  # Sub
            for i in range(len(line)):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + a) & 0xFF
        elif filter_type == 2:  # Up
            for i in range(len(line)):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filter_type == 3:  # Average
            for i in range(len(line)):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(len(line)):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(a, b, c)) & 0xFF
        else:
            raise PngError(f"unsupported filter type {filter_type}")

        rows.append(bytes(line))
        prev = line

    pixel_rows = []
    mask = (1 << bitdepth) - 1
    per_byte = 8 // bitdepth
    for line in rows:
        if bitdepth == 8:
            pixel_rows.append(list(line[:width]))
            continue
        pixels = [0] * width
        for x in range(width):
            byte_index = x // per_byte
            shift = 8 - bitdepth * ((x % per_byte) + 1)
            pixels[x] = (line[byte_index] >> shift) & mask
        pixel_rows.append(pixels)

    return width, height, pixel_rows


def ink_bounding_box(pixel_rows, threshold=1):
    """Return (min_x, min_y, max_x, max_y, ink_pixel_count) for samples whose
    value is >= threshold (index/level 0 is white in this printer's output).
    Returns None if no pixel meets the threshold."""
    min_x = min_y = None
    max_x = max_y = -1
    ink_count = 0
    for y, row in enumerate(pixel_rows):
        for x, value in enumerate(row):
            if value >= threshold:
                ink_count += 1
                if min_x is None or x < min_x:
                    min_x = x
                if x > max_x:
                    max_x = x
                if min_y is None or y < min_y:
                    min_y = y
                if y > max_y:
                    max_y = y
    if min_x is None:
        return None
    return min_x, min_y, max_x, max_y, ink_count
