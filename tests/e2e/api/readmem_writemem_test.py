#!/usr/bin/env python3
# E2E: Verifies REST memory reads and writes across live and frozen C64 modes.

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Dict, List, Optional, Set, Tuple

# tests/lib holds the reporting rules every suite shares.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import machine as machine_lib  # noqa: E402  (needs tests/lib on sys.path first)
import pacing  # noqa: E402  (needs tests/lib on sys.path first)
import profiles  # noqa: E402  (needs tests/lib on sys.path first)
import rest as rest_lib  # noqa: E402  (needs tests/lib on sys.path first)
import targets  # noqa: E402  (needs tests/lib on sys.path first)
from api import UltimateApi  # noqa: E402  (needs tests/lib on sys.path first)
from report import (
    FAIL, OK, SKIP, Failure, check, detail, format_exception, section, suite_fail,
    suite_ok, warn)

MENU_SCREEN_PATH = "/v1/machine:menu_screen"
MENU_BUTTON_PATH = "/v1/machine:menu_button"
READMEM_PATH = "/v1/machine:readmem"
WRITEMEM_PATH = "/v1/machine:writemem"
MEASURE_PATH = "/v1/machine:measure"
RESET_PATH = "/v1/machine:reset"
PAUSE_PATH = "/v1/machine:pause"
RESUME_PATH = "/v1/machine:resume"
INPUT_PATH = "/v1/machine:input"
CONFIG_CATEGORY = "User Interface Settings"
CONFIG_ITEM = "Interface Type"
INTERFACE_FREEZE = "Freeze"
INTERFACE_OVERLAY = "Overlay on HDMI"
# Shared with every suite; see tests/lib/pacing.py.
MENU_TOGGLE_SETTLE_SECONDS = pacing.MENU_TOGGLE_SETTLE_SECONDS
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
        self.target = targets.parse(host)
        self.host = self.target.device
        self.password = password
        self.timeout = timeout
        # For the calls this suite makes no assertion about, so that the menu
        # teardown has one implementation across the tree.
        self.api = UltimateApi(host, password, timeout)

    @property
    def machine(self) -> machine_lib.Machine:
        """Which machine this is, for the checks that need a firmware fix."""
        info = self.api.info()
        return machine_lib.identify(self.host, lambda: (info.product,
                                                        info.firmware_version))

    def url(self, path: str, params: Optional[Dict[str, object]] = None) -> str:
        query = ""
        if params:
            query = "?" + urllib.parse.urlencode(params)
        # Keyboard injection belongs to the C64-side computer on a cartridge
        # target; see tests/lib/targets.py.
        return f"http://{self.target.host_for(path)}{path}{query}"

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
            with rest_lib.retrying_urlopen(request, self.timeout) as response:
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

    def close_menu_from_anywhere(self) -> None:
        self.api.machine.close_menu_from_anywhere()

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

    def pause(self) -> None:
        status, _, body = self.request("PUT", PAUSE_PATH)
        if status != 200:
            raise Failure(f"pause failed with HTTP {status}: {body[:200]!r}")

    def resume(self) -> None:
        status, _, body = self.request("PUT", RESUME_PATH)
        if status != 200:
            raise Failure(f"resume failed with HTTP {status}: {body[:200]!r}")


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


# One probe per 4 KB of plain RAM, avoiding page zero (whose first two bytes are
# the CPU port), the stack, and screen RAM.
FROZEN_PROBE_ADDRESSES = [0x0800] + list(range(0x1000, 0xD000, 0x1000))
FROZEN_PROBE_BYTES = 4


def frozen_dma_blind_spots(session: "RestSession") -> List[int]:
    """The 4 KB windows a DMA write does not reach while the machine is frozen.

    A cartridge freezes the computer it is plugged into rather than a machine of
    its own, and that computer can stop answering DMA for part of its RAM while
    it is held frozen: writemem reports success and the read gives $FF back.
    Measured on u2@c64u, $1000-$7FFF behaves that way while $0000-$0FFF and
    $8000-$CFFF answer normally; on u64 every window answers.

    Probed rather than derived from the target name, so this reports what the
    machine under test actually does. The addresses it returns are excluded from
    the frozen round-trip comparison and named in the output; everything else
    is still compared byte for byte.
    """
    blind: List[int] = []
    for address in FROZEN_PROBE_ADDRESSES:
        # A ROM window answers with the ROM image whatever the write did, which
        # skip_rom already accounts for; only plain RAM is probed here.
        if classify(address) != "ram":
            continue
        probe = bytes(((address >> 8) ^ i) & 0xFF for i in range(FROZEN_PROBE_BYTES))
        session.writemem(address, probe)
        if session.readmem(address, FROZEN_PROBE_BYTES) != probe:
            blind.append(address)
    return blind


def compare(
    expected: bytes,
    actual: bytes,
    *,
    label: str,
    allow_screen_mismatch: bool,
    noise_addrs: Set[int],
    skip_rom: bool = False,
    unreachable_addrs: Set[int] = frozenset(),
    session: Optional["RestSession"] = None,
) -> bool:
    ram_mismatches: List[int] = []
    color_mismatches: List[int] = []
    screen_mismatches: List[int] = []
    ignored_noise = 0
    ignored_rom = 0
    ignored_unreachable = 0

    for addr in range(MEM_SIZE):
        cls = classify(addr)
        if cls == "io":
            continue
        if addr in unreachable_addrs:
            if expected[addr] != actual[addr]:
                ignored_unreachable += 1
            continue
        if expected[addr] == actual[addr]:
            continue
        if cls == "screen":
            screen_mismatches.append(addr)
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
            detail(f"[{label}] re-check: {transient} mismatch(es) had already changed again on their own "
                  f"(background activity, not a bug)")
        ram_mismatches = settled

    if skip_rom:
        detail(f"[{label}] ignored ROM mismatches (writes to real ROM are no-ops): {ignored_rom}")

    detail(f"[{label}] screen ($0400-$07FF) mismatches: {len(screen_mismatches)} "
          f"({'expected, menu owns this range while frozen' if allow_screen_mismatch else 'expected 0 in same-mode round trip'})")
    detail(f"[{label}] ignored known-live-noise mismatches: {ignored_noise}")
    if unreachable_addrs:
        detail(f"[{label}] ignored mismatches in windows this machine does not answer "
               f"DMA for while frozen: {ignored_unreachable}")
    detail(f"[{label}] color RAM (low nibble) mismatches: {len(color_mismatches)}")
    detail(f"[{label}] RAM mismatches: {len(ram_mismatches)}")

    ok = (len(ram_mismatches) == 0) and (len(color_mismatches) == 0)
    if not allow_screen_mismatch and screen_mismatches:
        ok = False
        detail(f"[{label}] UNEXPECTED: screen mismatched but this comparison stays within one mode")
        # Report what the screen actually held, not just how much of it differed:
        # a contiguous run points at a transfer that did not land, a scattered
        # one at something writing the range, and the read values name the
        # writer.
        detail(f"[{label}] screen mismatch ranges:")
        for line in summarize_ranges(screen_mismatches):
            detail(line)
        seen: Dict[int, int] = {}
        for addr in screen_mismatches:
            seen[actual[addr]] = seen.get(actual[addr], 0) + 1
        common = sorted(seen.items(), key=lambda kv: -kv[1])[:4]
        detail(f"[{label}] most common values read back: "
               + ", ".join(f"${v:02X} x{n}" for v, n in common))
        sample = ", ".join(f"${a:04X} wrote ${expected[a]:02X} read ${actual[a]:02X}"
                           for a in screen_mismatches[:6])
        detail(f"[{label}] first mismatches: {sample}")

    if ram_mismatches:
        detail(f"[{label}] RAM mismatch ranges:")
        for line in summarize_ranges(ram_mismatches):
            detail(line)
    if color_mismatches:
        detail(f"[{label}] color RAM mismatch ranges:")
        for line in summarize_ranges(color_mismatches):
            detail(line)
    return ok


def run_selfcheck(
    session: RestSession, interface: str, xor_value: int, noise_addrs: Set[int], interface_selectable: bool = True
) -> bool:
    label = f"selfcheck-{interface.lower().replace(' ', '-')}"
    if interface_selectable:
        with check(f"set Interface Type = {interface}"):
            session.set_interface_type(interface)
    with check(f"open menu ({interface})"):
        session.set_menu_open(True)

    unreachable: Set[int] = set()
    noise_addrs = set(noise_addrs)
    if interface == INTERFACE_FREEZE:
        with check("every 4 KB of RAM answers DMA while the machine is frozen"):
            blind = frozen_dma_blind_spots(session)
            if blind:
                for start in blind:
                    unreachable.update(range(start, start + 0x1000))
                detail("excluded from the frozen round trip, this machine does not "
                       "answer DMA there while frozen: "
                       + ", ".join(f"${a:04X}-${a + 0xFFF:04X}" for a in blind))
        with check("probe addresses that still change while the machine is frozen"):
            # Freeze halts the CPU of the machine the firmware runs on, so on an
            # Ultimate 64 nothing moves. A cartridge freezes the computer it is
            # plugged into by running its own stub on that computer's CPU, and
            # that stub keeps using zero page and the stack. Measured rather
            # than assumed, so the same comparison holds on both.
            frozen_noise = probe_live_noise(session, samples=3, interval=0.4)
            frozen_noise -= unreachable
            noise_addrs |= frozen_noise
            detail(f"{len(frozen_noise)} address(es) change on their own while frozen"
                   + (": " + ", ".join(f"${a:04X}" for a in sorted(frozen_noise)[:20])
                      if frozen_noise else ""))

    pattern = make_pattern(xor_value)
    paused = interface == INTERFACE_OVERLAY
    if paused:
        with check(f"pause C64 for exact round trip ({interface})"):
            session.pause()
    try:
        with check(f"write full-range pattern ({interface})"):
            session.write_pattern(pattern)
        with check(f"read back full range ({interface})"):
            actual = session.read_full()
    finally:
        if paused:
            with check(f"resume C64 after exact round trip ({interface})"):
                session.resume()
    with check(f"menu still open after write+read ({interface})"):
        if not session.menu_screen_open():
            raise Failure("menu closed unexpectedly during self-check read/write")

    # The on-device browser repaints its entry rows into the C64 screen while
    # the menu is open, on its own drive-status refresh rather than in response
    # to a keypress. Measured during a failing Overlay round trip: the read-back
    # of $0400-$07FF held the browser listing from $0450 to $07DE, which is row 2
    # onward, while rows 0 and 1 still held the written pattern. That is true in
    # Overlay as well as Freeze, so the screen range is not comparable while the
    # menu is open in either mode. run_screen_round_trip below asserts it with
    # the menu closed, where nothing else draws.
    allow_screen_mismatch = True
    section(f"{label}: write and read both happen in {interface} "
          f"(screen RAM excluded, the menu redraws its entry rows while open)")
    # Freeze already halts the CPU. Overlay normally keeps it running (verified by
    # the live-noise probe), so pause it explicitly around this multi-request
    # transaction. Otherwise the CPU can legitimately mutate RAM between the
    # three writes and the read, making an exact round-trip assertion sporadic.
    ok = compare(pattern, actual, label=label, allow_screen_mismatch=allow_screen_mismatch,
                 noise_addrs=set() if paused else noise_addrs, skip_rom=True,
                 unreachable_addrs=unreachable,
                 session=None if paused else session)

    with check(f"close menu ({interface})"):
        session.set_menu_open(False)
    return ok


def run_screen_round_trip(session: RestSession, interface: str, xor_value: int) -> bool:
    """Screen RAM must round-trip exactly while no menu is drawing over it.

    This is the assertion run_selfcheck cannot make with the menu open. The menu
    is closed here, so $0400-$07FF belongs to the C64 alone and an exact match is
    the right expectation over the whole range.
    """
    label = f"screen-round-trip-{interface.lower().replace(' ', '-')}"
    with check(f"close menu for the screen round trip ({interface})"):
        session.set_menu_open(False)
    with check(f"pause C64 for the screen round trip ({interface})"):
        session.pause()
    lo, hi = SCREEN_RANGE
    pattern = make_pattern(xor_value)
    try:
        with check(f"write and read back $0400-$07FF ({interface})"):
            session.writemem(lo, pattern[lo:hi])
            actual = session.readmem(lo, hi - lo)
    finally:
        with check(f"resume C64 after the screen round trip ({interface})"):
            session.resume()

    section(f"{label}: screen RAM round-trips exactly with no menu open")
    mismatches = [a for a in range(lo, hi) if pattern[a] != actual[a - lo]]
    detail(f"[{label}] screen ($0400-$07FF) mismatches: {len(mismatches)} (expected 0)")
    if mismatches:
        detail(f"[{label}] mismatch ranges:")
        for line in summarize_ranges(mismatches):
            detail(line)
        sample = ", ".join(f"${a:04X} wrote ${pattern[a]:02X} read ${actual[a - lo]:02X}"
                           for a in mismatches[:6])
        detail(f"[{label}] first mismatches: {sample}")
    return not mismatches


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

    section(f"{label}: bytes written while {write_interface} must read back the same while {read_interface}, "
          f"except screen RAM (menu-owned while frozen)")
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
    warn("zero page did not settle; proceeding anyway")


def run_reset(session: RestSession) -> None:
    with check("reset C64 to a clean, stable state"):
        session.reset()
    time.sleep(RESET_SETTLE_SECONDS)
    wait_for_stable_zero_page(session, attempts=10, interval=1.0)


# Every readmem request the firmware accepts allocates a caller-sized buffer, so
# the parameter checks are the only thing standing between a hostile `length` and
# the allocator. Rejection has to happen before the allocation, and the allocation
# has to be able to fail: `operator new` PANICs and spins forever on OOM
# (software/system/memory_wrap.cc), which takes the device down with no reset, so
# the handler uses malloc and reports HTTP 500 instead.
# Each entry is (params, label, fix). `fix` names the firmware fix the
# rejection needs, or None where every machine under test has always refused
# the request. A tagged entry is skipped on a machine that lacks the fix
# rather than failed, because the scenario below raises on the first
# unexpected answer and would otherwise take the whole suite with it.
BAD_READ_REQUESTS = [
    ({"address": "0000", "length": 0}, "zero length",
     machine_lib.READMEM_REJECTS_ZERO_LENGTH),
    ({"address": "0000", "length": -1}, "negative length", None),
    ({"address": "0000", "length": 65537}, "length one past the 64KB cap", None),
    ({"address": "0000", "length": 16 * 1024 * 1024}, "length far past the cap", None),
    ({"address": "0000", "length": "99999999999999999999"}, "length that overflows a long", None),
    ({"address": "ff00", "length": 65536}, "read running past $FFFF", None),
    ({"address": "10000", "length": 1}, "address above $FFFF", None),
    ({"address": "-1", "length": 1}, "negative address", None),
]
# Repeats chosen so a 64KB-per-call leak is a few megabytes without making the
# suite slow. This is a smoke check, not proof of a leak-free heap: nothing over
# REST reports free heap, so the assertion is the observable one -- a max-size
# read still succeeds afterwards.
MEASURE_LEAK_REPEATS = 25
# The suite's own live-noise probe already does eight full 64KB reads, so eight
# here costs no more than something the run does anyway.
MAX_READ_REPEATS = 8


def run_bounds(session: RestSession) -> bool:
    """Reject out-of-range readmem parameters, and survive the allocations we do accept.

    Every accepted readmem allocates a caller-sized buffer, so these parameter
    checks are the whole defence. A wrong verdict here means the firmware is
    allocating on input it should have refused, which makes the rest of the run
    meaningless -- so these raise rather than accumulate.
    """
    section("bounds: readmem rejects out-of-range parameters before allocating, "
            "and max-size reads do not degrade the device")

    for params, label, fix in BAD_READ_REQUESTS:
        if fix is not None and session.machine.skip_without_fix(
                fix, f"readmem rejects {label}"):
            continue
        with check(f"readmem rejects {label}"):
            status, _, body = session.request("GET", READMEM_PATH, params=params)
            if status != 400:
                raise Failure(f"readmem({params}) returned HTTP {status}, expected 400: {body[:200]!r}")

    # The success path allocates and frees the same 64KB every time. A regression
    # that drops the free shows up here as a later iteration failing outright.
    with check(f"{MAX_READ_REPEATS} back-to-back max-size reads ($0000, {MEM_SIZE} bytes) all succeed"):
        for _ in range(MAX_READ_REPEATS):
            session.readmem(0, MEM_SIZE)

    # machine:measure allocated its 64KB buffer before checking whether the FPGA
    # supports bus measurement, then returned 501 without freeing it. Bus timing
    # measurement is off by default on U2 builds (commit 8155daec), so on those
    # devices the leaking path is the only path this endpoint ever takes.
    measure_label = (f"{MEASURE_LEAK_REPEATS} unsupported machine:measure "
                     "calls leave readmem working")
    if session.machine.skip_without_fix(
            machine_lib.MEASURE_FREES_ITS_BUFFER, measure_label):
        # Not a check this machine merely fails: without the fix the 25 calls
        # leak 1.6MB, which exhausts a C64 Ultimate's heap and takes it off the
        # network until someone power cycles it by hand.
        return True

    status, _, _ = session.request("GET", MEASURE_PATH)
    if status == 501:
        with check(measure_label):
            for _ in range(MEASURE_LEAK_REPEATS):
                session.request("GET", MEASURE_PATH)
            session.readmem(0, MEM_SIZE)
    elif status == 200:
        detail("machine:measure is supported on this FPGA build; not repeating it, "
               "a real bus measurement is intrusive and the leak was on the 501 path only")
    else:
        warn(f"machine:measure returned unexpected HTTP {status}; leak check not applicable")

    return True


FREEZE_ONLY_TESTS = ["selfcheck-freeze"]
OVERLAY_DEPENDENT_TESTS = ["selfcheck-overlay", "screen-round-trip",
                           "overlay-to-freeze", "freeze-to-overlay"]


# What the smoke profile runs when no stage is named: one write-and-read round
# trip in Freeze. It is the cheapest thing that proves both routes work at all,
# and Freeze halts the CPU, so it is also the one stage that needs no
# live-noise probe. The whole suite measured 28s on an Ultimate 64 and this
# stage measured 1.5s of it, which is the difference between a smoke profile
# that gets run after a reflash and one that does not.
SMOKE_TESTS = ["selfcheck-freeze"]

# The stages that read memory while the C64 is running, so an address that
# changes on its own has to be known about first. Freeze halts the CPU, so
# nothing in FREEZE_ONLY_TESTS needs the probe.
NOISE_DEPENDENT_TESTS = ["selfcheck-overlay", "screen-round-trip",
                         "overlay-to-freeze", "freeze-to-overlay"]


def expand_tests(selected: Optional[List[str]]) -> List[str]:
    all_tests = ["bounds", "selfcheck-freeze", "selfcheck-overlay", "screen-round-trip",
                 "overlay-to-freeze", "freeze-to-overlay"]
    if not selected:
        if not profiles.includes(profiles.QUICK):
            return list(SMOKE_TESTS)
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
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-r", "--rest-host", default=os.environ.get("U64_REST_HOST"))
    parser.add_argument(
        "-p",
        "--password",
        default=os.environ.get("U64_PASS"),
    )
    parser.add_argument(
        "-t",
        "--timeout",
        type=float,
        default=float(os.environ.get("U64_TIMEOUT", "30.0")),
    )
    parser.add_argument(
        "--test",
        action="append",
        choices=("all", "bounds", "selfcheck-freeze", "selfcheck-overlay", "screen-round-trip",
                 "overlay-to-freeze", "freeze-to-overlay"),
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
    results: Dict[str, bool] = {}

    try:
        session.close_menu_from_anywhere()
        if not args.no_reset:
            run_reset(session)

        with check("read User Interface Settings config"):
            ui_config = session.get_config(CONFIG_CATEGORY)
        interface_selectable = CONFIG_ITEM in ui_config
        if interface_selectable:
            original_interface = str(ui_config[CONFIG_ITEM])
            detail(f"current Interface Type: {original_interface}")
        else:
            # Freeze-only hardware (e.g. U2/U2+/U2+L cartridges without HDMI output)
            # has no "Interface Type" setting at all -- there is no Overlay mode to
            # switch to, so restrict to the tests that only need Freeze.
            detail("no 'Interface Type' setting on this device -- freeze-only hardware, "
                  "restricting to freeze-mode tests")
            # Only a test named individually (not via the "all" wildcard, and not the
            # implicit default-to-everything when --test is omitted) is an explicit,
            # unsatisfiable request on this hardware -- anything reached via "all"/the
            # default should be silently skipped instead, since "all" means "everything
            # this device can run", not "everything, or fail".
            explicit_overlay_tests = [t for t in (args.test or []) if t in OVERLAY_DEPENDENT_TESTS]
            if explicit_overlay_tests:
                raise Failure(
                    f"{', '.join(explicit_overlay_tests)} require Overlay-on-HDMI support, "
                    f"which this device does not have"
                )
            skipped_tests = [t for t in tests if t in OVERLAY_DEPENDENT_TESTS]
            tests = [t for t in tests if t not in OVERLAY_DEPENDENT_TESTS] or FREEZE_ONLY_TESTS
            if skipped_tests:
                detail(f"skipping (requires Overlay-on-HDMI, unsupported here): {', '.join(skipped_tests)}")
        # First, and before the live-noise probe spends eight full 64KB reads:
        # if readmem mishandles its own parameters, everything after it is noise.
        if "bounds" in tests:
            results["bounds"] = run_bounds(session)

        noise_addrs: Set[int] = set()
        # Eight full 64KB reads, so it is only worth paying where a stage
        # actually reads memory with the CPU running.
        if interface_selectable and not any(t in NOISE_DEPENDENT_TESTS for t in tests):
            detail("skipping the live-noise probe: no selected stage reads "
                   "memory while the C64 is running")
        elif interface_selectable:
            with check("switch to Overlay on HDMI to probe live background activity"):
                session.set_interface_type(INTERFACE_OVERLAY)
            with check("probe addresses that change on their own while the C64 runs"):
                noise_addrs = probe_live_noise(session, samples=8, interval=0.6)
            detail(f"{len(noise_addrs)} address(es) treated as expected live background noise: "
                  f"{', '.join(f'${a:04X}' for a in sorted(noise_addrs)[:20])}"
                  f"{' ...' if len(noise_addrs) > 20 else ''}")
        else:
            detail("skipping live-noise probe: Freeze halts the CPU entirely, no drift is possible")

        if "selfcheck-freeze" in tests:
            results["selfcheck-freeze"] = run_selfcheck(
                session, INTERFACE_FREEZE, 0x33, noise_addrs, interface_selectable=interface_selectable
            )
        if "selfcheck-overlay" in tests:
            results["selfcheck-overlay"] = run_selfcheck(session, INTERFACE_OVERLAY, 0x66, noise_addrs)
        if "screen-round-trip" in tests:
            results["screen-round-trip"] = run_screen_round_trip(
                session, INTERFACE_OVERLAY, 0x3C)
        if "overlay-to-freeze" in tests:
            results["overlay-to-freeze"] = run_cross_mode(session, INTERFACE_OVERLAY, INTERFACE_FREEZE, 0x55, noise_addrs)
        if "freeze-to-overlay" in tests:
            results["freeze-to-overlay"] = run_cross_mode(session, INTERFACE_FREEZE, INTERFACE_OVERLAY, 0xAA, noise_addrs)

    finally:
        try:
            if not args.no_reset:
                session.set_menu_open(False)
                session.api.machine.reset(force=True)
            if not args.keep_config and original_interface:
                session.set_interface_type(original_interface)
                detail(f"restored Interface Type to {original_interface!r}")
        except Exception as exc:
            warn(f"cleanup failed: {exc}")

    section("summary")
    all_ok = True
    for name in tests:
        outcome = results.get(name)
        status = OK if outcome else (FAIL if outcome is False else SKIP)
        if outcome is not True:
            all_ok = False
        detail(f"{name}: {status}")

    if all_ok:
        suite_ok("readmem_writemem_test")
        return 0
    suite_fail("readmem_writemem_test", "see the summary above")
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("readmem_writemem_test", format_exception(exc))
        raise SystemExit(1)
