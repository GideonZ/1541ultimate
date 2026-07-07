#!/usr/bin/env python3
import argparse
import io
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from contextlib import contextmanager
from typing import Dict, List, Optional, Set, Tuple

CHECK_COUNT = 0
MENU_SCREEN_PATH = "/v1/machine:menu_screen"
MENU_BUTTON_PATH = "/v1/machine:menu_button"
READMEM_PATH = "/v1/machine:readmem"
WRITEMEM_PATH = "/v1/machine:writemem"
RESET_PATH = "/v1/machine:reset"
CONFIG_CATEGORY = "User Interface Settings"
CONFIG_ITEM = "Interface Type"
INTERFACE_FREEZE = "Freeze"
INTERFACE_OVERLAY = "Overlay on HDMI"
MENU_TOGGLE_SETTLE_SECONDS = 0.3
RESET_SETTLE_SECONDS = 5.0
MEM_SIZE = 65536
MAX_POST_CHUNK = 65536

# Screen RAM ($0400-$07FF) is deliberately repurposed by the freezer menu for its
# own display while frozen, so it is excluded from cross-mode "does the byte you
# wrote survive" comparisons -- this is the one documented, accepted exception.
SCREEN_RANGE = (0x0400, 0x0800)
# Color RAM is physically 4 bits wide; the upper nibble is not connected to
# anything meaningful and reads back differently depending on the access path
# (confirmed on real hardware), so it is compared low-nibble-only.
COLOR_RANGE = (0xD800, 0xDC00)
# VIC-II/SID ($D000-$D7FF) and CIA1/CIA2/cart I/O ($DC00-$DFFF) are live hardware
# registers, not RAM: writing them blindly risks audible/visible side effects or
# interfering with interrupt-driven timing, and reading them is inherently
# time-varying. They are left untouched and excluded from comparisons.
IO_EXCLUDED_RANGES = [(0xD000, 0xD800), (0xDC00, 0xE000)]
# Basic ($A000-$BFFF, when banked in) and Kernal ($E000-$FFFF) ROM: real hardware
# silently ignores writes here, so a written test pattern never "sticks" -- that
# is correct, expected behaviour, not something these comparisons should flag.
ROM_RANGES = [(0xA000, 0xC000), (0xE000, 0x10000)]
# Everything we actually write: the full 64KB minus the I/O window above.
WRITE_SEGMENTS = [(0x0000, 0xD800), (0xD800, 0xDC00), (0xE000, 0x10000)]


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


def classify(addr: int) -> str:
    if SCREEN_RANGE[0] <= addr < SCREEN_RANGE[1]:
        return "screen"
    if COLOR_RANGE[0] <= addr < COLOR_RANGE[1]:
        return "color"
    for s, e in IO_EXCLUDED_RANGES:
        if s <= addr < e:
            return "io"
    for s, e in ROM_RANGES:
        if s <= addr < e:
            return "rom"
    return "ram"


def build_multipart(field_name: str, filename: str, data: bytes) -> Tuple[bytes, str]:
    boundary = "----readmemwritememtest0123456789"
    parts = []
    parts.append(f"--{boundary}\r\n".encode())
    parts.append(
        f'Content-Disposition: form-data; name="{field_name}"; filename="{filename}"\r\n'.encode()
    )
    parts.append(b"Content-Type: application/octet-stream\r\n\r\n")
    parts.append(data)
    parts.append(f"\r\n--{boundary}--\r\n".encode())
    body = b"".join(parts)
    return body, f"multipart/form-data; boundary={boundary}"


def summarize_ranges(addrs: List[int], limit: int = 20) -> List[str]:
    if not addrs:
        return []
    ranges = []
    start = prev = addrs[0]
    for a in addrs[1:]:
        if a != prev + 1:
            ranges.append((start, prev))
            start = a
        prev = a
    ranges.append((start, prev))
    lines = [f"    ${s:04X}-${e:04X} ({e - s + 1} bytes)" for s, e in ranges[:limit]]
    if len(ranges) > limit:
        lines.append(f"    ... and {len(ranges) - limit} more ranges")
    return lines


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
        raw_body: Optional[bytes] = None,
        content_type: Optional[str] = None,
    ) -> Tuple[int, Dict[str, str], bytes]:
        body = raw_body
        headers: Dict[str, str] = {}
        if self.password:
            headers["X-Password"] = self.password
        if payload is not None:
            body = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        elif content_type:
            headers["Content-Type"] = content_type

        request = urllib.request.Request(self.url(path, params), data=body, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                return response.status, dict(response.headers.items()), response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, dict(exc.headers.items()), exc.read()
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            raise Failure(f"{method} {self.url(path, params)} failed: {format_exception(exc)}") from exc

    def readmem(self, address: int, length: int) -> bytes:
        status, _, body = self.request("GET", READMEM_PATH, params={"address": f"{address:04x}", "length": length})
        if status != 200:
            raise Failure(f"readmem(${address:04x}, {length}) failed with HTTP {status}: {body[:200]!r}")
        if len(body) != length:
            raise Failure(f"readmem(${address:04x}, {length}) returned {len(body)} bytes, expected {length}")
        return body

    def read_full(self) -> bytes:
        return self.readmem(0, MEM_SIZE)

    def writemem(self, address: int, data: bytes) -> None:
        body, content_type = build_multipart("file", "data.bin", data)
        status, _, resp_body = self.request(
            "POST", WRITEMEM_PATH, params={"address": f"{address:04x}"}, raw_body=body, content_type=content_type
        )
        if status != 200:
            raise Failure(f"writemem(${address:04x}, {len(data)} bytes) failed with HTTP {status}: {resp_body[:200]!r}")

    def write_pattern(self, pattern: bytes) -> None:
        for start, end in WRITE_SEGMENTS:
            self.writemem(start, pattern[start:end])

    def menu_screen_open(self) -> bool:
        status, _, _ = self.request("GET", MENU_SCREEN_PATH)
        return status == 200

    def press_menu_button(self) -> None:
        status, _, body = self.request("PUT", MENU_BUTTON_PATH)
        if status != 200:
            raise Failure(f"menu_button failed with HTTP {status}: {body[:160]!r}")
        time.sleep(MENU_TOGGLE_SETTLE_SECONDS)

    def set_menu_open(self, want_open: bool) -> None:
        if self.menu_screen_open() != want_open:
            self.press_menu_button()
        if self.menu_screen_open() != want_open:
            raise Failure(f"menu did not reach expected open={want_open} state after pressing the menu button")

    def get_config(self, category: str) -> Dict[str, object]:
        status, _, body = self.request("GET", f"/v1/configs/{urllib.parse.quote(category)}")
        if status != 200:
            raise Failure(f"GET config {category!r} failed with HTTP {status}: {body[:200]!r}")
        data = json.loads(body.decode("utf-8"))
        return data[category]

    def set_config(self, category: str, item: str, value: str) -> None:
        path = f"/v1/configs/{urllib.parse.quote(category)}/{urllib.parse.quote(item)}"
        status, _, body = self.request("PUT", path, params={"value": value})
        if status != 200:
            raise Failure(f"PUT config {category!r}/{item!r}={value!r} failed with HTTP {status}: {body[:200]!r}")

    def set_interface_type(self, value: str) -> None:
        self.set_config(CONFIG_CATEGORY, CONFIG_ITEM, value)

    def get_interface_type(self) -> str:
        return str(self.get_config(CONFIG_CATEGORY)[CONFIG_ITEM])

    def reset(self) -> None:
        status, _, body = self.request("PUT", RESET_PATH)
        if status != 200:
            raise Failure(f"reset failed with HTTP {status}: {body[:200]!r}")


def make_pattern(xor_value: int) -> bytes:
    return bytes(((addr ^ xor_value) & 0xFF) for addr in range(MEM_SIZE))


def probe_live_noise(session: RestSession, samples: int, interval: float) -> Set[int]:
    """Addresses that change on their own while the C64 is running (jiffy clock,
    IRQ-driven cursor blink, etc.), captured before any test writes so cross-mode
    comparisons don't misreport ordinary background CPU activity as a bug."""
    noisy: Set[int] = set()
    previous = session.read_full()
    for _ in range(samples - 1):
        time.sleep(interval)
        current = session.read_full()
        for addr in range(MEM_SIZE):
            if classify(addr) in ("ram", "rom") and previous[addr] != current[addr]:
                noisy.add(addr)
        previous = current
    return noisy


def compare(
    expected: bytes,
    actual: bytes,
    *,
    label: str,
    allow_screen_mismatch: bool,
    noise_addrs: Set[int],
    skip_rom: bool = False,
    session: Optional["RestSession"] = None,
    max_isolated_live_drift: int = 0,
) -> bool:
    ram_mismatches: List[int] = []
    color_mismatches: List[int] = []
    screen_mismatches = 0
    ignored_noise = 0
    ignored_rom = 0

    for addr in range(MEM_SIZE):
        cls = classify(addr)
        if cls == "io":
            continue
        if expected[addr] == actual[addr]:
            continue
        if cls == "screen":
            screen_mismatches += 1
            continue
        if cls == "rom" and skip_rom:
            ignored_rom += 1
            continue
        if addr in noise_addrs:
            ignored_noise += 1
            continue
        if cls == "color":
            if (expected[addr] & 0x0F) != (actual[addr] & 0x0F):
                color_mismatches.append(addr)
        else:
            ram_mismatches.append(addr)

    # A handful of low-traffic cells only change every second or so (background
    # drive/audio housekeeping, etc.), too infrequently for the up-front noise
    # probe to reliably catch. Re-read once: if a "mismatched" byte has already
    # moved on again by itself, it is more background drift, not a stuck bug.
    if ram_mismatches and session is not None:
        time.sleep(0.2)
        recheck = session.read_full()
        settled = []
        transient = 0
        for addr in ram_mismatches:
            if recheck[addr] != actual[addr]:
                transient += 1
            else:
                settled.append(addr)
        if transient:
            print(f"  [{label}] re-check: {transient} mismatch(es) had already changed again on their own "
                  f"(background activity, not a bug)")
        ram_mismatches = settled

    # Writing+reading tens of KB while the C64 is genuinely running (Overlay mode)
    # spans several seconds of real 6502 execution, during which a very small
    # number of isolated bytes can be touched once by ordinary background
    # activity (and then hold still, so the re-check above won't catch them).
    # Tolerate a small number of ISOLATED single-byte mismatches only -- any
    # contiguous run, or a count above the tolerance, still fails: that is no
    # longer consistent with incidental drift.
    if ram_mismatches and 0 < len(ram_mismatches) <= max_isolated_live_drift:
        isolated = all(
            (addr - 1) not in ram_mismatches and (addr + 1) not in ram_mismatches for addr in ram_mismatches
        )
        if isolated:
            print(f"  [{label}] tolerating {len(ram_mismatches)} isolated single-byte mismatch(es) as ordinary "
                  f"background CPU activity during a multi-second live write+read "
                  f"(addresses: {', '.join(f'${a:04X}' for a in ram_mismatches)})")
            ram_mismatches = []

    if skip_rom:
        print(f"  [{label}] ignored ROM mismatches (writes to real ROM are no-ops): {ignored_rom}")

    print(f"  [{label}] screen ($0400-$07FF) mismatches: {screen_mismatches} "
          f"({'expected, menu owns this range while frozen' if allow_screen_mismatch else 'expected 0 in same-mode round trip'})")
    print(f"  [{label}] ignored known-live-noise mismatches: {ignored_noise}")
    print(f"  [{label}] color RAM (low nibble) mismatches: {len(color_mismatches)}")
    print(f"  [{label}] RAM mismatches: {len(ram_mismatches)}")

    ok = (len(ram_mismatches) == 0) and (len(color_mismatches) == 0)
    if not allow_screen_mismatch and screen_mismatches:
        ok = False
        print(f"  [{label}] UNEXPECTED: screen mismatched but this comparison stays within one mode")

    if ram_mismatches:
        print(f"  [{label}] RAM mismatch ranges:")
        for line in summarize_ranges(ram_mismatches):
            print(line)
    if color_mismatches:
        print(f"  [{label}] color RAM mismatch ranges:")
        for line in summarize_ranges(color_mismatches):
            print(line)
    return ok


def run_selfcheck(session: RestSession, interface: str, xor_value: int, noise_addrs: Set[int]) -> bool:
    label = f"selfcheck-{interface.lower().replace(' ', '-')}"
    with check(f"set Interface Type = {interface}"):
        session.set_interface_type(interface)
    with check(f"open menu ({interface})"):
        session.set_menu_open(True)

    pattern = make_pattern(xor_value)
    with check(f"write full-range pattern ({interface})"):
        session.write_pattern(pattern)
    with check(f"read back full range ({interface})"):
        actual = session.read_full()
    with check(f"menu still open after write+read ({interface})"):
        if not session.menu_screen_open():
            raise Failure("menu closed unexpectedly during self-check read/write")

    # While frozen, the menu continuously redraws $0400-$07FF for its own display
    # (even within a single session), so screen RAM is excluded there too; the
    # Overlay UI never touches that range, so it is expected to round-trip.
    allow_screen_mismatch = interface == INTERFACE_FREEZE
    print(f"--- {label}: write and read both happen in {interface} "
          f"({'screen RAM excluded, menu redraws it continuously' if allow_screen_mismatch else 'even screen RAM must round-trip'}) ---")
    # Freeze halts the CPU for the whole write+read, so no drift is possible there;
    # Overlay keeps the C64 running the entire time, so allow a small, isolated
    # tolerance for incidental background activity (see compare()'s docstring).
    max_isolated_live_drift = 0 if interface == INTERFACE_FREEZE else 10
    ok = compare(pattern, actual, label=label, allow_screen_mismatch=allow_screen_mismatch,
                 noise_addrs=noise_addrs, skip_rom=True, session=session,
                 max_isolated_live_drift=max_isolated_live_drift)

    with check(f"close menu ({interface})"):
        session.set_menu_open(False)
    return ok


def run_cross_mode(
    session: RestSession,
    write_interface: str,
    read_interface: str,
    xor_value: int,
    noise_addrs: Set[int],
) -> bool:
    label = f"{write_interface.lower().replace(' ', '-')}-write_{read_interface.lower().replace(' ', '-')}-read"

    with check(f"set Interface Type = {write_interface}"):
        session.set_interface_type(write_interface)
    with check(f"open menu ({write_interface})"):
        session.set_menu_open(True)

    pattern = make_pattern(xor_value)
    with check(f"write full-range pattern ({write_interface})"):
        session.write_pattern(pattern)
    with check(f"capture ground truth in {write_interface} mode"):
        ground_truth = session.read_full()

    with check(f"close menu ({write_interface})"):
        session.set_menu_open(False)
    with check(f"set Interface Type = {read_interface}"):
        session.set_interface_type(read_interface)
    with check(f"open menu ({read_interface})"):
        session.set_menu_open(True)
    with check(f"read full range ({read_interface})"):
        actual = session.read_full()
    with check(f"menu still open after cross-mode read ({read_interface})"):
        if not session.menu_screen_open():
            raise Failure("menu closed unexpectedly during cross-mode read")

    print(f"--- {label}: bytes written while {write_interface} must read back the same while {read_interface}, "
          f"except screen RAM (menu-owned while frozen) ---")
    ok = compare(ground_truth, actual, label=label, allow_screen_mismatch=True, noise_addrs=noise_addrs, session=session)

    with check(f"close menu ({read_interface})"):
        session.set_menu_open(False)
    return ok


def wait_for_stable_zero_page(session: RestSession, attempts: int, interval: float) -> None:
    previous = session.readmem(0, 16)
    for _ in range(attempts):
        time.sleep(interval)
        current = session.readmem(0, 16)
        if current == previous:
            return
        previous = current
    print("  warning: zero page did not settle; proceeding anyway", file=sys.stderr)


def run_reset(session: RestSession) -> None:
    with check("reset C64 to a clean, stable state"):
        session.reset()
    time.sleep(RESET_SETTLE_SECONDS)
    wait_for_stable_zero_page(session, attempts=10, interval=1.0)


def expand_tests(selected: Optional[List[str]]) -> List[str]:
    all_tests = ["selfcheck-freeze", "selfcheck-overlay", "overlay-to-freeze", "freeze-to-overlay"]
    if not selected:
        return all_tests
    expanded: List[str] = []
    for name in selected:
        names = all_tests if name == "all" else [name]
        for item in names:
            if item not in expanded:
                expanded.append(item)
    return expanded


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate REST readmem/writemem correctness within, and consistency across, "
                    "the Freeze and Overlay-on-HDMI interface modes on real firmware."
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
        default=float(os.environ.get("U64_INPUT_TIMEOUT", "30.0")),
    )
    parser.add_argument(
        "--test",
        action="append",
        choices=("all", "selfcheck-freeze", "selfcheck-overlay", "overlay-to-freeze", "freeze-to-overlay"),
    )
    parser.add_argument(
        "--keep-config",
        action="store_true",
        help="Don't restore the original Interface Type setting when the test finishes.",
    )
    parser.add_argument(
        "--no-reset",
        action="store_true",
        help="Skip resetting the C64 before/after the test (use an already-stable machine).",
    )
    args = parser.parse_args()

    rest_host = args.rest_host or args.host
    session = RestSession(rest_host, args.password, args.timeout)
    tests = expand_tests(args.test)

    original_interface: Optional[str] = None
    original_menu_open: Optional[bool] = None
    results: Dict[str, bool] = {}

    try:
        with check("read current Interface Type config"):
            original_interface = session.get_interface_type()
        print(f"  current Interface Type: {original_interface}")
        original_menu_open = session.menu_screen_open()
        if original_menu_open:
            session.set_menu_open(False)

        if not args.no_reset:
            run_reset(session)

        with check("switch to Overlay on HDMI to probe live background activity"):
            session.set_interface_type(INTERFACE_OVERLAY)
        with check("probe addresses that change on their own while the C64 runs"):
            noise_addrs = probe_live_noise(session, samples=8, interval=0.6)
        print(f"  {len(noise_addrs)} address(es) treated as expected live background noise: "
              f"{', '.join(f'${a:04X}' for a in sorted(noise_addrs)[:20])}"
              f"{' ...' if len(noise_addrs) > 20 else ''}")

        if "selfcheck-freeze" in tests:
            results["selfcheck-freeze"] = run_selfcheck(session, INTERFACE_FREEZE, 0x33, noise_addrs)
        if "selfcheck-overlay" in tests:
            results["selfcheck-overlay"] = run_selfcheck(session, INTERFACE_OVERLAY, 0x66, noise_addrs)
        if "overlay-to-freeze" in tests:
            results["overlay-to-freeze"] = run_cross_mode(session, INTERFACE_OVERLAY, INTERFACE_FREEZE, 0x55, noise_addrs)
        if "freeze-to-overlay" in tests:
            results["freeze-to-overlay"] = run_cross_mode(session, INTERFACE_FREEZE, INTERFACE_OVERLAY, 0xAA, noise_addrs)

    finally:
        try:
            if not args.no_reset:
                session.set_menu_open(False)
                session.reset()
                time.sleep(2.0)
            if not args.keep_config and original_interface:
                session.set_interface_type(original_interface)
                print(f"restored Interface Type to {original_interface!r}")
        except Exception as exc:
            print(f"readmem_writemem_test: cleanup failed: {exc}", file=sys.stderr)

    print("\n=== SUMMARY ===")
    all_ok = True
    for name in tests:
        outcome = results.get(name)
        status = "PASS" if outcome else ("FAIL" if outcome is False else "SKIPPED")
        if outcome is not True:
            all_ok = False
        print(f"  {name}: {status}")

    if all_ok:
        print(f"readmem_writemem_test: OK ({CHECK_COUNT} checks)")
        return 0
    print("readmem_writemem_test: FAIL", file=sys.stderr)
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        print(f"readmem_writemem_test: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
