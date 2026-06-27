#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from contextlib import contextmanager
from typing import Dict, List, Optional, Tuple


CHECK_COUNT = 0
MENU_SCREEN_PATH = "/v1/machine:menu_screen"
MENU_BUTTON_PATH = "/v1/machine:menu_button"
INPUT_PATH = "/v1/machine:input"
MENU_SCREEN_UNAVAILABLE = "Menu screen unavailable."
FORBIDDEN = "Forbidden."
SCREEN_WIDTH = 40
SCREEN_HEIGHT = 25
SCREEN_CELLS = SCREEN_WIDTH * SCREEN_HEIGHT
SCREEN_PLANES = 2
SCREEN_BYTES = SCREEN_CELLS * SCREEN_PLANES
# Structural bounds that any genuinely-rendered menu satisfies, independent of
# the specific menu, firmware version, or colour scheme. They reject blank or
# garbage snapshots without pinning the test to exact menu text or layout.
# Observed across root/context/settings/help screens: printable 176-347,
# distinct glyphs 57-63, distinct colours 4-7; bounds keep wide margins.
MENU_MIN_PRINTABLE_CELLS = 20
MENU_MAX_DISTINCT_COLOURS = 32
MENU_MAX_DISTINCT_GLYPHS = 160
MENU_TOGGLE_SETTLE_SECONDS = 0.25
MENU_CLOSE_TIMEOUT_SECONDS = 2.0
DEFAULT_SOAK_STAGES = (10.0, 30.0, 120.0, 300.0)
SOAK_NAVIGATION_INTERVAL_SECONDS = 0.20
SELECTED_ROW_MIN_MARKED_CELLS = 12
SOAK_TOP_LEVEL_SECTION_LIMIT = 64


class Failure(RuntimeError):
    pass


@contextmanager
def check(label: str):
    global CHECK_COUNT
    CHECK_COUNT += 1
    print(f"[{CHECK_COUNT:02d}] {label} ... ", end="", flush=True)
    try:
        yield
    except Exception:
        print("FAIL", flush=True)
        raise
    print("OK", flush=True)


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

        request = urllib.request.Request(self.url(path, params), data=body, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                return response.status, dict(response.headers.items()), response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, dict(exc.headers.items()), exc.read()
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            raise Failure(f"{method} {self.url(path, params)} failed: {format_exception(exc)}") from exc

    def menu_button(self, label: str) -> None:
        with check(label):
            status, _, body = self.request("PUT", MENU_BUTTON_PATH)
            if status != 200:
                raise Failure(f"menu_button failed with HTTP {status}: {body[:160]!r}")
        time.sleep(MENU_TOGGLE_SETTLE_SECONDS)

    def menu_screen_unavailable(self) -> bool:
        status, headers, body = self.request("GET", MENU_SCREEN_PATH)
        if status != 404:
            return False
        content_type = header_value(headers, "Content-Type")
        if "application/json" not in content_type:
            raise Failure(f"expected application/json, got {content_type!r}")
        require_error("menu_screen unavailable", body, MENU_SCREEN_UNAVAILABLE)
        return True

    def menu_screen_available(self) -> bool:
        status, headers, body = self.request("GET", MENU_SCREEN_PATH)
        if status == 404:
            require_error("menu_screen unavailable", body, MENU_SCREEN_UNAVAILABLE)
            return False
        verify_binary_contract(status, headers, body)
        return True

    def wait_menu_closed(self) -> bool:
        deadline = time.monotonic() + MENU_CLOSE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if self.menu_screen_unavailable():
                return True
            time.sleep(MENU_TOGGLE_SETTLE_SECONDS)
        return False

    def wait_menu_open(self) -> bool:
        deadline = time.monotonic() + MENU_CLOSE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if self.menu_screen_available():
                return True
            time.sleep(MENU_TOGGLE_SETTLE_SECONDS)
        return False

    def close_menu(self) -> None:
        self.menu_button("close menu")
        if not self.wait_menu_closed():
            raise Failure("menu remained available after REST menu_button close")

    def open_menu(self) -> None:
        self.menu_button("open menu")
        if not self.wait_menu_open():
            raise Failure("menu remained unavailable after REST menu_button open")

    def get_menu_screen(self) -> bytes:
        status, headers, body = self.request("GET", MENU_SCREEN_PATH)
        verify_binary_contract(status, headers, body)
        return body

    def try_get_menu_screen(self) -> Optional[bytes]:
        status, headers, body = self.request("GET", MENU_SCREEN_PATH)
        if status == 404:
            content_type = header_value(headers, "Content-Type")
            if "application/json" not in content_type:
                raise Failure(f"expected application/json, got {content_type!r}")
            require_error("menu_screen unavailable", body, MENU_SCREEN_UNAVAILABLE)
            return None
        verify_binary_contract(status, headers, body)
        return body

    def post_events(self, events: List[Dict[str, object]]) -> Dict[str, object]:
        status, headers, body = self.request("POST", INPUT_PATH, payload={"events": events})
        if status != 200:
            raise Failure(f"input event failed with HTTP {status}: {body[:160]!r}")
        content_type = header_value(headers, "Content-Type")
        if "application/json" not in content_type:
            raise Failure(f"expected application/json from input event, got {content_type!r}")
        return parse_json("input event", body)

    def tap_keyboard(self, inputs: List[str]) -> None:
        self.post_events([{"kind": "keyboard", "inputs": inputs, "transition": "tap"}])

    def release_all_input(self) -> None:
        self.post_events([{"kind": "release_all"}])

    def close_menu_from_anywhere(self) -> None:
        for _ in range(12):
            body = self.try_get_menu_screen()
            if body is None:
                return
            if "Save changes to Flash?" in menu_screen_text(body):
                self.tap_keyboard(["cursor_left_right"])
                time.sleep(MENU_TOGGLE_SETTLE_SECONDS)
                self.tap_keyboard(["return"])
                time.sleep(MENU_TOGGLE_SETTLE_SECONDS)
                continue
            self.tap_keyboard(["run_stop"])
            time.sleep(MENU_TOGGLE_SETTLE_SECONDS)

        if self.menu_screen_unavailable():
            return
        self.close_menu()


def header_value(headers: Dict[str, str], name: str) -> str:
    wanted = name.lower()
    for key, value in headers.items():
        if key.lower() == wanted:
            return value
    return ""


def parse_json(label: str, body: bytes) -> Dict[str, object]:
    try:
        data = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as exc:
        raise Failure(f"{label}: response body is not valid JSON: {body[:160]!r}") from exc
    if not isinstance(data, dict):
        raise Failure(f"{label}: expected JSON object, got {data!r}")
    return data


def require_error(label: str, body: bytes, message: str) -> None:
    data = parse_json(label, body)
    errors = data.get("errors")
    if not isinstance(errors, list) or message not in errors:
        raise Failure(f"{label}: expected errors to contain {message!r}, got {data!r}")


def verify_binary_contract(status: int, headers: Dict[str, str], body: bytes) -> None:
    if status != 200:
        raise Failure(f"expected HTTP 200, got HTTP {status}: {body[:160]!r}")

    content_type = header_value(headers, "Content-Type")
    if "application/octet-stream" not in content_type:
        raise Failure(f"expected application/octet-stream, got {content_type!r}")
    if len(body) != SCREEN_BYTES:
        raise Failure(f"expected {SCREEN_BYTES} response bytes, got {len(body)}")

    chars = body[:SCREEN_CELLS]
    attrs = body[SCREEN_CELLS:]
    if len(chars) != SCREEN_CELLS or len(attrs) != SCREEN_CELLS:
        raise Failure(f"response did not split into two {SCREEN_CELLS}-byte planes")
    if len(set(chars)) == 1 and len(set(attrs)) == 1:
        raise Failure("snapshot is trivially uniform in both planes")


def verify_menu_content(body: bytes) -> None:
    # Resilience over prescription: confirm the snapshot looks like a real
    # rendered menu (readable text in a small, deliberate palette) without
    # asserting any specific menu text, layout, or colours. This survives future
    # menu changes yet still rejects blank screens and random/garbage data.
    chars = body[:SCREEN_CELLS]
    colours = body[SCREEN_CELLS:]

    printable = sum(1 for ch in chars if 0x21 <= (ch & 0x7F) <= 0x7E)
    if printable < MENU_MIN_PRINTABLE_CELLS:
        raise Failure(
            f"snapshot has too little readable text to be a menu: {printable} "
            f"printable cells (need >= {MENU_MIN_PRINTABLE_CELLS})"
        )

    distinct_colours = len(set(colours))
    if not (1 < distinct_colours <= MENU_MAX_DISTINCT_COLOURS):
        raise Failure(
            f"snapshot colour plane is not menu-like: {distinct_colours} distinct "
            f"colours (expected 2..{MENU_MAX_DISTINCT_COLOURS})"
        )

    distinct_glyphs = len(set(chars))
    if distinct_glyphs > MENU_MAX_DISTINCT_GLYPHS:
        raise Failure(
            f"snapshot character plane is not menu-like: {distinct_glyphs} distinct "
            f"glyphs (expected <= {MENU_MAX_DISTINCT_GLYPHS})"
        )


def menu_screen_text(body: bytes) -> str:
    chars = body[:SCREEN_CELLS]
    rows = []
    for row in range(SCREEN_HEIGHT):
        row_chars = chars[row * SCREEN_WIDTH:(row + 1) * SCREEN_WIDTH]
        rows.append("".join(
            chr(ch & 0x7F) if 0x20 <= (ch & 0x7F) <= 0x7E else " "
            for ch in row_chars
        ))
    return "\n".join(rows)


def run_contract(session: RestSession) -> None:
    with check("GET menu_screen contract"):
        status, headers, body = session.request("GET", MENU_SCREEN_PATH)
        verify_binary_contract(status, headers, body)
    with check("menu_screen renders a real menu"):
        verify_menu_content(body)


def run_contract_with_open_menu(session: RestSession) -> None:
    if not session.menu_screen_unavailable():
        session.close_menu()
    session.open_menu()
    try:
        run_contract(session)
    finally:
        session.close_menu()


def run_unavailable(session: RestSession) -> None:
    with check("GET menu_screen unavailable"):
        if not session.menu_screen_unavailable():
            raise Failure("expected HTTP 404 unavailable state")


def run_auth(session: RestSession) -> None:
    if not session.password:
        print("menu_screen_test: SKIP auth (--password not supplied)", file=sys.stderr)
        return
    with check("GET menu_screen auth"):
        status, headers, body = session.request("GET", MENU_SCREEN_PATH, use_password=False)
        if status != 403:
            raise Failure(f"expected HTTP 403 without X-Password, got HTTP {status}")
        content_type = header_value(headers, "Content-Type")
        if "application/json" not in content_type:
            raise Failure(f"expected application/json, got {content_type!r}")
        require_error("menu_screen auth", body, FORBIDDEN)


def parse_duration(value: str) -> float:
    text = value.strip().lower()
    multiplier = 1.0
    if text.endswith("ms"):
        multiplier = 0.001
        text = text[:-2]
    elif text.endswith("s"):
        text = text[:-1]
    elif text.endswith("m"):
        multiplier = 60.0
        text = text[:-1]

    try:
        duration = float(text) * multiplier
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid duration: {value!r}") from exc

    if duration <= 0.0:
        raise argparse.ArgumentTypeError("duration must be greater than zero")
    return duration


def parse_soak_stages(value: str) -> List[float]:
    stages = [parse_duration(part) for part in value.split(",") if part.strip()]
    if not stages:
        raise argparse.ArgumentTypeError("at least one soak stage is required")
    return stages


def format_duration(seconds: float) -> str:
    if seconds >= 60.0 and seconds % 60.0 == 0.0:
        return f"{int(seconds / 60.0)}m"
    if seconds % 1.0 == 0.0:
        return f"{int(seconds)}s"
    return f"{seconds:.3f}s"


class MenuScreenInfo:
    def __init__(self, body: bytes) -> None:
        chars = body[:SCREEN_CELLS]
        colors = body[SCREEN_CELLS:]

        self.screen_id = hashlib.sha256(chars).digest()[:8]
        self.rows = [
            "".join(chr(ch & 0x7F) if 0x20 <= (ch & 0x7F) <= 0x7E else " "
                for ch in chars[row * SCREEN_WIDTH:(row + 1) * SCREEN_WIDTH]).strip()
            for row in range(SCREEN_HEIGHT)
        ]
        self.selected_row = self.find_selected_row(chars, colors)
        self.selected_text = self.rows[self.selected_row]
        self.text = "\n".join(self.rows)

    @staticmethod
    def find_selected_row(chars: bytes, colors: bytes) -> int:
        best_background_row = -1
        best_background_count = 0
        best_reverse_row = -1
        best_reverse_count = 0
        best_foreground_row = -1
        best_foreground_count = 0
        for row in range(2, SCREEN_HEIGHT - 1):
            background_counts: Dict[int, int] = {}
            foreground_counts: Dict[int, int] = {}
            reverse_count = 0
            row_chars = chars[row * SCREEN_WIDTH + 1:(row + 1) * SCREEN_WIDTH - 1]
            row_colors = colors[row * SCREEN_WIDTH + 1:(row + 1) * SCREEN_WIDTH - 1]
            for ch, color_code in zip(row_chars, row_colors):
                if ch & 0x80:
                    reverse_count += 1
                foreground = color_code & 0x0F
                background = (color_code >> 4) & 0x0F
                if background == 0:
                    if foreground != 0x0F:
                        foreground_counts[foreground] = foreground_counts.get(foreground, 0) + 1
                    continue
                background_counts[background] = background_counts.get(background, 0) + 1
            background_count = max(background_counts.values()) if background_counts else 0
            foreground_count = max(foreground_counts.values()) if foreground_counts else 0
            if background_count > best_background_count:
                best_background_count = background_count
                best_background_row = row
            if reverse_count > best_reverse_count:
                best_reverse_count = reverse_count
                best_reverse_row = row
            if foreground_count > best_foreground_count:
                best_foreground_count = foreground_count
                best_foreground_row = row

        if best_background_count >= SELECTED_ROW_MIN_MARKED_CELLS:
            return best_background_row
        if best_reverse_count >= SELECTED_ROW_MIN_MARKED_CELLS:
            return best_reverse_row
        if best_foreground_count >= SELECTED_ROW_MIN_MARKED_CELLS:
            return best_foreground_row
        raise Failure("could not locate selected menu row from colour codes")


class MenuSoakNavigator:
    UP = ["left_shift", "cursor_up_down"]
    DOWN = ["cursor_up_down"]
    F2 = ["left_shift", "f1"]

    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self.started = False
        self.mode = "rewind"
        self.visited = set()
        self.top_level_sections = 0
        self.boundary_hits = 0
        self.last_action = ""
        self.last_screen_id: Optional[bytes] = None
        self.last_selected_row: Optional[int] = None

    def _remember_action(self, action: str, info: MenuScreenInfo) -> None:
        self.last_action = action
        self.last_screen_id = info.screen_id
        self.last_selected_row = info.selected_row

    def _moved_since_last_action(self, info: MenuScreenInfo) -> bool:
        return (
            self.last_screen_id != info.screen_id or
            self.last_selected_row != info.selected_row
        )

    def _apply_last_action_outcome(self, info: MenuScreenInfo) -> None:
        if not self.last_action:
            return

        moved = self._moved_since_last_action(info)
        if self.last_action in ("up", "down"):
            if moved:
                self.boundary_hits = 0
            else:
                self.boundary_hits += 1
        elif self.last_action == "f2":
            self.mode = "rewind"
            self.boundary_hits = 0

        self.last_action = ""

    def next_key(self, body: bytes) -> Tuple[str, List[str]]:
        info = MenuScreenInfo(body)
        self._apply_last_action_outcome(info)

        if not self.started:
            self.started = True
            self._remember_action("f2", info)
            return "enter config menu with F2", self.F2

        if self.mode == "rewind":
            if self.boundary_hits >= 2:
                self.mode = "advance"
                self.boundary_hits = 0
            else:
                self._remember_action("up", info)
                return f"rewind selection upward from row {info.selected_row}", self.UP

        visit_key = (info.screen_id, info.selected_row, info.selected_text)
        self.visited.add(visit_key)

        if self.mode == "advance" and self.boundary_hits >= 2:
            self.boundary_hits = 0
            self.top_level_sections += 1
            if self.top_level_sections >= SOAK_TOP_LEVEL_SECTION_LIMIT:
                self.top_level_sections = 0
                self.visited.clear()
            self.mode = "rewind" if (self.top_level_sections % 2) == 0 else "advance"
            self._remember_action("f2", info)
            return "switch non-mutating menu view with F2", self.F2

        self._remember_action("down", info)
        return f"move selection downward from row {info.selected_row}", self.DOWN


def run_soak(session: RestSession, stages: List[float], navigation_interval: float) -> None:
    if not session.menu_screen_unavailable():
        session.close_menu()
    session.open_menu()
    navigator = MenuSoakNavigator()
    try:
        for stage in stages:
            label = f"GET menu_screen soak {format_duration(stage)}"
            with check(label):
                count = 0
                navigation_count = 0
                reopened_count = 0
                snapshot_hashes = set()
                next_navigation = time.monotonic()
                started = time.monotonic()
                deadline = started + stage
                while time.monotonic() < deadline:
                    try:
                        body = session.try_get_menu_screen()
                    except Exception as exc:
                        elapsed = time.monotonic() - started
                        raise Failure(
                            f"soak failed after {count} successful requests "
                            f"in {elapsed:.3f}s: {exc}"
                        ) from exc
                    if body is None:
                        reopened_count += 1
                        navigator.reset()
                        try:
                            session.open_menu()
                        except Exception as exc:
                            elapsed = time.monotonic() - started
                            raise Failure(
                                f"menu reopen failed after {count} successful snapshots "
                                f"in {elapsed:.3f}s: {exc}"
                            ) from exc
                        continue
                    count += 1
                    snapshot_hashes.add(hashlib.sha256(body).digest()[:8])

                    now = time.monotonic()
                    if now >= next_navigation:
                        name, inputs = navigator.next_key(body)
                        try:
                            session.tap_keyboard(inputs)
                        except Exception as exc:
                            elapsed = time.monotonic() - started
                            raise Failure(
                                f"navigation {name} failed after {count} snapshots "
                                f"in {elapsed:.3f}s: {exc}"
                            ) from exc
                        navigation_count += 1
                        next_navigation = time.monotonic() + navigation_interval

                elapsed = time.monotonic() - started
                rate = count / elapsed if elapsed > 0.0 else 0.0
                if len(snapshot_hashes) < 2:
                    raise Failure("soak did not observe changing menu snapshots during navigation")
                print(
                    f"{count} snapshots, {navigation_count} keys, "
                    f"{len(snapshot_hashes)} variants, {reopened_count} reopens, {rate:.1f}/s ",
                    end="",
                    flush=True,
                )
    finally:
        try:
            session.release_all_input()
        except Exception as exc:
            print(f"menu_screen_test: soak cleanup release_all failed: {exc}", file=sys.stderr)
        session.close_menu_from_anywhere()


def expand_tests(selected: Optional[List[str]]) -> List[str]:
    if not selected:
        selected = ["contract", "unavailable", "auth"]
    expanded: List[str] = []
    for name in selected:
        names = ["contract", "unavailable", "auth"] if name == "all" else [name]
        for item in names:
            if item not in expanded:
                expanded.append(item)
    return expanded


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate /v1/machine:menu_screen on real firmware.")
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
        "--test",
        action="append",
        choices=("all", "contract", "unavailable", "auth", "soak"),
    )
    parser.add_argument(
        "--open-menu",
        action="store_true",
        help="Deprecated compatibility option; the contract check opens the menu by default.",
    )
    parser.add_argument(
        "--soak-stages",
        type=parse_soak_stages,
        default=list(DEFAULT_SOAK_STAGES),
        metavar="DURATIONS",
        help="Comma-separated soak durations for --test soak, e.g. 10s,30s,2m,5m.",
    )
    parser.add_argument(
        "--soak-key-interval",
        type=parse_duration,
        default=SOAK_NAVIGATION_INTERVAL_SECONDS,
        metavar="DURATION",
        help="Minimum interval between REST keyboard navigation taps during soak.",
    )
    args = parser.parse_args()

    rest_host = args.rest_host or args.host
    session = RestSession(rest_host, args.password, args.timeout)
    tests = expand_tests(args.test)

    if "contract" in tests:
        run_contract_with_open_menu(session)
    if "unavailable" in tests:
        run_unavailable(session)
    if "auth" in tests:
        run_auth(session)
    if "soak" in tests:
        run_soak(session, args.soak_stages, args.soak_key_interval)

    print(f"menu_screen_test: OK ({CHECK_COUNT} checks)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        print(f"menu_screen_test: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
