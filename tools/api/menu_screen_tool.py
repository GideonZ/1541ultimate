#!/usr/bin/env python3
import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from typing import Dict, Optional, Tuple


MENU_SCREEN_PATH = "/v1/machine:menu_screen"

SCREEN_WIDTH = 40
SCREEN_HEIGHT = 25
SCREEN_CELLS = SCREEN_WIDTH * SCREEN_HEIGHT
SCREEN_PLANES = 2
SCREEN_BYTES = SCREEN_CELLS * SCREEN_PLANES


class Failure(RuntimeError):
    pass


def format_exception(exc: BaseException) -> str:
    if isinstance(exc, urllib.error.URLError) and getattr(exc, "reason", None) is not None:
        return f"{exc} ({exc.reason})"
    return str(exc)


class RestSession:
    def __init__(self, host: str, password: Optional[str], timeout: float) -> None:
        self.host = host
        self.password = password
        self.timeout = timeout

    def url(self, path: str, params: Optional[Dict[str, object]] = None) -> str:
        query = ""
        if params:
            query = "?" + urllib.parse.urlencode(params)
        return f"http://{self.host}{path}{query}"

    def request(
        self,
        method: str,
        path: str,
        params: Optional[Dict[str, object]] = None,
        payload: Optional[Dict[str, object]] = None,
        use_password: bool = True,
    ) -> Tuple[int, Dict[str, str], bytes]:
        body = None if payload is None else json.dumps(payload).encode("utf-8")
        headers: Dict[str, str] = {}

        if self.password and use_password:
            headers["X-Password"] = self.password
        if body is not None:
            headers["Content-Type"] = "application/json"

        request = urllib.request.Request(
            self.url(path, params),
            data=body,
            headers=headers,
            method=method,
        )

        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                return response.status, dict(response.headers.items()), response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, dict(exc.headers.items()), exc.read()
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            raise Failure(f"{method} {self.url(path, params)} failed: {format_exception(exc)}") from exc


def header_value(headers: Dict[str, str], name: str) -> str:
    wanted = name.lower()
    for key, value in headers.items():
        if key.lower() == wanted:
            return value
    return ""


def c64_rgb(colour: int) -> Tuple[int, int, int]:
    # Approximate C64 palette.
    palette = {
        0x0: (0, 0, 0),          # black
        0x1: (255, 255, 255),    # white
        0x2: (104, 55, 43),      # red
        0x3: (112, 164, 178),    # cyan
        0x4: (111, 61, 134),     # purple
        0x5: (88, 141, 67),      # green
        0x6: (53, 40, 121),      # blue
        0x7: (184, 199, 111),    # yellow
        0x8: (111, 79, 37),      # orange
        0x9: (67, 57, 0),        # brown
        0xA: (154, 103, 89),     # light red
        0xB: (68, 68, 68),       # dark grey
        0xC: (108, 108, 108),    # grey
        0xD: (154, 210, 132),    # light green
        0xE: (108, 94, 181),     # light blue
        0xF: (149, 149, 149),    # light grey
    }
    return palette[colour & 0x0F]


def fg_ansi(colour: int) -> str:
    r, g, b = c64_rgb(colour)
    return f"\x1b[38;2;{r};{g};{b}m"


def bg_ansi(colour: int) -> str:
    r, g, b = c64_rgb(colour)
    return f"\x1b[48;2;{r};{g};{b}m"


MENU_GLYPHS = {
    0x00: " ",
    0x01: "┌",
    0x02: "─",
    0x03: "┐",
    0x04: "│",
    0x05: "└",
    0x06: "┘",
    0x07: "╭",
    0x08: "┬",
    0x09: "╮",
    0x0A: "├",
    0x0B: "▇",
    0x0C: "┤",
    0x0D: "┴",
    0x0E: "╰",
    0x0F: "╯",
    0x10: "α",
    0x11: "β",
    0x12: "▀",
    0x13: "◆",
}


def menu_char_to_glyph(code: int) -> str:
    # The menu endpoint returns raw Screen_MemMappedCharMatrix bytes. Text is
    # stored as literal printable characters; low values are firmware UI glyphs.
    base = code & 0x7F

    if 0x20 <= base <= 0x7E:
        return chr(base)

    return MENU_GLYPHS.get(base, "?")


def fetch_menu_screen(session: RestSession) -> bytes:
    status, headers, body = session.request("GET", MENU_SCREEN_PATH)

    if status != 200:
        raise Failure(f"expected HTTP 200, got HTTP {status}: {body[:160]!r}")

    content_type = header_value(headers, "Content-Type")
    if "application/octet-stream" not in content_type:
        raise Failure(f"expected application/octet-stream, got {content_type!r}")

    if len(body) != SCREEN_BYTES:
        raise Failure(f"expected {SCREEN_BYTES} response bytes, got {len(body)}")

    return body


def split_colour_byte(
    colour_byte: int,
    swap_colour_nibbles: bool,
) -> Tuple[int, int]:
    low = colour_byte & 0x0F
    high = (colour_byte >> 4) & 0x0F

    if swap_colour_nibbles:
        foreground = high
        background = low
    else:
        foreground = low
        background = high

    return foreground, background


def render_menu_screen(
    body: bytes,
    clear: bool,
    use_colour: bool,
    swap_colour_nibbles: bool,
) -> None:
    chars = body[:SCREEN_CELLS]
    colours = body[SCREEN_CELLS:]

    if clear:
        sys.stdout.write("\x1b[0m\x1b[2J\x1b[H")

    last_fg: Optional[int] = None
    last_bg: Optional[int] = None

    for row in range(SCREEN_HEIGHT):
        for col in range(SCREEN_WIDTH):
            idx = row * SCREEN_WIDTH + col
            screen_code = chars[idx]
            colour_byte = colours[idx]

            foreground, background = split_colour_byte(
                colour_byte,
                swap_colour_nibbles,
            )
            glyph = menu_char_to_glyph(screen_code)

            # The matrix stores reverse-video cells by setting bit 7.
            if screen_code & 0x80:
                foreground, background = background, foreground

            if use_colour:
                if foreground != last_fg:
                    sys.stdout.write(fg_ansi(foreground))
                    last_fg = foreground
                if background != last_bg:
                    sys.stdout.write(bg_ansi(background))
                    last_bg = background

            sys.stdout.write(glyph)

        if use_colour:
            sys.stdout.write("\x1b[0m")
            last_fg = None
            last_bg = None
        sys.stdout.write("\n")

    if use_colour:
        sys.stdout.write("\x1b[0m")

    sys.stdout.flush()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render /v1/machine:menu_screen as a 40x25 ANSI terminal view."
    )
    parser.add_argument("-H", "--host", default=os.environ.get("U64_INPUT_HOST", "u64"))
    parser.add_argument("-r", "--rest-host", default=os.environ.get("U64_INPUT_REST_HOST"))
    parser.add_argument(
        "-p",
        "--password",
        default=os.environ.get("U64_INPUT_PASSWORD", os.environ.get("C64U_PASSWORD")),
    )
    parser.add_argument(
        "-t",
        "--timeout",
        type=float,
        default=float(os.environ.get("U64_INPUT_TIMEOUT", "5.0")),
    )
    parser.add_argument(
        "--no-clear",
        action="store_true",
        help="Do not clear the terminal before rendering.",
    )
    parser.add_argument(
        "--no-color",
        action="store_true",
        help="Render glyphs only, without ANSI foreground/background colours.",
    )
    parser.add_argument(
        "--swap-color-nibbles",
        action="store_true",
        help="Interpret high nibble as foreground and low nibble as background.",
    )

    args = parser.parse_args()

    rest_host = args.rest_host or args.host
    session = RestSession(rest_host, args.password, args.timeout)

    body = fetch_menu_screen(session)
    render_menu_screen(
        body,
        clear=not args.no_clear,
        use_colour=not args.no_color,
        swap_colour_nibbles=args.swap_color_nibbles,
    )

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        print(f"menu_screen_tool: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
