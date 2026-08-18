#!/usr/bin/env python3
"""Hardware soak for #797 using a Pico 2 W as a deliberately faulty USB keyboard.

The fixture has one boot-keyboard HID interface and a narrow Wi-Fi protocol.
It never executes caller-supplied code and can only inject the small key set
needed to navigate the Ultimate menu.

The Pico must be the only USB keyboard attached to the Ultimate 64.  The
firmware passes the negotiated idle period on to the repeat only when exactly
one attached keyboard accepted it, because Keyboard_USB sees a single merged
report stream and the periodic reports of a second keyboard would keep a stale
key of the first looking fresh (software/io/usb/usb_hid_selection.h).  With a
second keyboard present the repeat bound is disabled and the fault cases below
report far more than one menu movement.
"""
from __future__ import annotations

import argparse
import ftplib
import glob
import json
import os
import shutil
import socket
import select
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tests" / "lib"))
from api import MachineApi  # noqa: E402
from report import Failure, check, detail, format_exception, suite_fail, suite_ok  # noqa: E402
from rest import RestClient, header_value  # noqa: E402

PROTOCOL_VERSION = 1
MAGIC = "u64-usb-keyboard-soak"
DISCOVERY_PORT = 49196
TCP_PORT = 49197
MAX_LINE = 512
UF2_VERSION = "1.28.0"
UF2_URL = "https://micropython.org/resources/firmware/RPI_PICO2_W-20260406-v1.28.0.uf2"
# Exact micropython-lib commit examined with this fixture.  HIDInterface handles
# SET_IDLE/GET_IDLE; the fixture's own scheduler implements the required resend.
HID_LIB_REVISION = "ee4bb8ff139e24c42b739935fbd8ec7c4d061e02"
HID_FILES = (
    # usb-device 0.2.1 and usb-device-hid 0.2.0, both at this exact revision.
    ("usb-device/usb/device/__init__.py", "usb/device/__init__.py"),
    ("usb-device/usb/device/core.py", "usb/device/core.py"),
    ("usb-device-hid/usb/device/hid.py", "usb/device/hid.py"),
)
DEFAULT_DURATION = {"stress": 120.0, "soak": 12 * 60 * 60.0}
KEY_F13 = 183  # Linux input-event-codes.h
# The root browser holds six entries, so a held key reaches its end within a
# fraction of a second and further presses stop moving the selection.  The suite
# builds its own list on the RAM disk instead, deep enough that a held key never
# reaches the end and independent of what is on the attached storage.
DEEP_LIST_DIRECTORY = "usbsoak"
DEEP_LIST_ENTRIES = 60
SCREEN_CELLS = 1000
SELECTED_MIN = 12


def parse_duration(value: str) -> float:
    value = value.strip().lower(); multiplier = 1.0
    if value.endswith("ms"): multiplier, value = .001, value[:-2]
    elif value.endswith("s"): value = value[:-1]
    elif value.endswith("m"): multiplier, value = 60.0, value[:-1]
    elif value.endswith("h"): multiplier, value = 3600.0, value[:-1]
    try: result = float(value) * multiplier
    except ValueError as exc: raise argparse.ArgumentTypeError("duration must be e.g. 30s or 2m") from exc
    if result <= 0: raise argparse.ArgumentTypeError("duration must be positive")
    return result


def marker(body: bytes) -> Tuple[int, str]:
    if len(body) != 2000: raise Failure(f"menu screen has {len(body)} bytes, expected 2000")
    chars, colours = body[:SCREEN_CELLS], body[SCREEN_CELLS:]
    candidates = []
    for row in range(2, 24):
        text = "".join(chr(c & 0x7f) if 0x20 <= (c & 0x7f) <= 0x7e else " "
                       for c in chars[row * 40:(row + 1) * 40]).strip()
        row_chars, row_colours = chars[row * 40 + 1:(row + 1) * 40 - 1], colours[row * 40 + 1:(row + 1) * 40 - 1]
        backgrounds, foregrounds, reverse = {}, {}, 0
        for char, colour in zip(row_chars, row_colours):
            reverse += bool(char & 0x80); background, foreground = colour >> 4, colour & 15
            if background: backgrounds[background] = backgrounds.get(background, 0) + 1
            elif foreground != 15: foregrounds[foreground] = foregrounds.get(foreground, 0) + 1
        candidates.append((max(backgrounds.values(), default=0), max(foregrounds.values(), default=0), reverse, row, text))
    background = max(candidates, key=lambda x: x[0])
    reverse = max(candidates, key=lambda x: x[2])
    foreground = max(candidates, key=lambda x: x[1])
    chosen = background if background[0] >= SELECTED_MIN else reverse if reverse[2] >= SELECTED_MIN else foreground
    if max(chosen[:3]) < SELECTED_MIN: raise Failure("could not locate selected menu row")
    return chosen[3], chosen[4]


class Pico:
    def __init__(self, host: str): self.host, self.sock, self.request_id = host, None, 0
    def connect(self):
        self.close(); self.sock = socket.create_connection((self.host, TCP_PORT), timeout=5)
        self.sock.settimeout(8)
    def close(self):
        if self.sock:
            try: self.sock.close()
            except OSError: pass
        self.sock = None
    def call(self, command: str, **kwargs) -> Dict[str, Any]:
        if not self.sock: self.connect()
        self.request_id += 1; request = {"protocol_version": PROTOCOL_VERSION, "id": self.request_id, "command": command, **kwargs}
        wire = (json.dumps(request, separators=(",", ":")) + "\n").encode()
        if len(wire) > MAX_LINE: raise Failure("fixture request unexpectedly exceeds protocol limit")
        try:
            self.sock.sendall(wire); data = b""
            while not data.endswith(b"\n"):
                part = self.sock.recv(MAX_LINE - len(data))
                if not part: raise OSError("fixture closed its control connection")
                data += part
            answer = json.loads(data)
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            self.close(); raise Failure(f"Pico control failure: {exc}") from exc
        if answer.get("id") != self.request_id or answer.get("protocol_version") != PROTOCOL_VERSION or not answer.get("ok"):
            raise Failure(f"Pico rejected {command}: {answer.get('error', answer)!r}")
        return answer["result"]


def discover_pico(timeout: float = 4.0) -> str:
    request = json.dumps({"service": MAGIC, "protocol_version": PROTOCOL_VERSION}).encode()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1); sock.settimeout(.25)
    found = set(); deadline = time.monotonic() + timeout
    # Limited broadcast is not always bridged between wired Ethernet and Wi-Fi.
    # Also send the ordinary /24 directed broadcast for the common lab LAN.
    targets = [("255.255.255.255", DISCOVERY_PORT)]
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); probe.connect(("8.8.8.8", 80))
        octets = probe.getsockname()[0].split("."); probe.close()
        if len(octets) == 4: targets.append((".".join(octets[:3] + ["255"]), DISCOVERY_PORT))
    except OSError: pass
    try:
        while time.monotonic() < deadline:
            for target in targets: sock.sendto(request, target)
            try:
                while True:
                    body, address = sock.recvfrom(MAX_LINE); reply = json.loads(body)
                    if reply.get("service") == MAGIC and reply.get("protocol_version") == PROTOCOL_VERSION: found.add(reply.get("ip") or address[0])
            except socket.timeout: pass
    finally: sock.close()
    if not found: found = sweep_for_pico()
    if len(found) != 1:
        raise Failure("expected exactly one Pico fixture, found %r. Pass --pico-host with the "
                      "fixture's IP address if this network does not forward broadcast between "
                      "the wired test host and the Wi-Fi client." % sorted(found))
    return found.pop()


def sweep_for_pico(port: int = TCP_PORT) -> set:
    """Find the fixture by connecting to every address on the test host's /24.

    Access points commonly drop broadcast between a wired host and a Wi-Fi
    client, which makes UDP discovery return nothing.  A direct TCP connection
    to the fixture's control port is not affected by that.
    """
    import concurrent.futures
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); probe.connect(("8.8.8.8", 80))
        octets = probe.getsockname()[0].split("."); probe.close()
    except OSError:
        return set()
    if len(octets) != 4: return set()
    prefix = ".".join(octets[:3])
    detail("discovery: broadcast found nothing, sweeping %s.0/24 for the fixture control port" % prefix)

    def probe_address(last):
        address = "%s.%d" % (prefix, last)
        try:
            connection = socket.create_connection((address, port), timeout=1.5); connection.close()
        except OSError:
            return None
        try:
            status = Pico(address).call("status")
        except Failure:
            return None
        return address if status.get("service") == MAGIC else None

    with concurrent.futures.ThreadPoolExecutor(max_workers=64) as pool:
        return {result for result in pool.map(probe_address, range(1, 255)) if result}


class U64:
    def __init__(self, host: str, password: Optional[str], timeout: float):
        self.host = host
        self.rest = RestClient(host, password, timeout); self.machine = MachineApi(self.rest)
    def rows(self) -> list:
        status, headers, body = self.rest.request("GET", "/v1/machine:menu_screen")
        if status != 200: raise Failure(f"menu_screen failed with HTTP {status}")
        chars = body[:SCREEN_CELLS]
        return [line for line in
                ("".join(chr(c & 0x7f) if 0x20 <= (c & 0x7f) <= 0x7e else " "
                         for c in chars[row * 40:(row + 1) * 40]).strip()
                 for row in range(2, 24)) if line]
    def key(self, *names: str, settle: float = .35):
        """Press a menu key over REST.

        Menu keys injected through `machine:input` reach the same on-screen menu
        as the USB keyboard, so this drives the suite's own setup navigation.
        The Telnet remote menu is a separate 60-column user interface instance
        and does not move the on-screen selection.
        """
        self.machine.press(*names); time.sleep(settle)
    def screen(self) -> Tuple[int, str]:
        status, headers, body = self.rest.request("GET", "/v1/machine:menu_screen")
        if status != 200 or "application/octet-stream" not in header_value(headers, "Content-Type"):
            raise Failure(f"menu_screen failed with HTTP {status}: {body[:120]!r}")
        return marker(body)
    def stable_screen(self, attempts: int = 4) -> Tuple[int, str]:
        """Read the menu until two consecutive reads agree.

        A read that lands in the middle of a redraw can report the previous
        selection, which over a long run would fail a check that the firmware
        passed.  Callers use this only where the selection is expected to have
        settled, so a disagreement means a redraw, not a live repeat.
        """
        previous = self.screen()
        for _ in range(attempts):
            current = self.screen()
            if current == previous: return current
            previous = current
        return previous
    def open_menu(self):
        status, _, _ = self.rest.request("GET", "/v1/machine:menu_screen")
        if status == 200:
            self.screen()
            return
        status, _, body = self.rest.request("PUT", "/v1/machine:menu_button")
        if status != 200: raise Failure(f"menu button failed: {body[:120]!r}")
        time.sleep(.3); self.screen()
    def close_menu(self): self.machine.close_menu_from_anywhere()


def build_deep_list(host: str) -> int:
    """Create the RAM-disk directory the suite navigates, and return its size.

    The RAM disk is volatile, so nothing is written to flash or to attached
    storage.  Files that already exist are left alone, which makes a repeated
    run cheap.
    """
    ftp = ftplib.FTP(); ftp.connect(host, 21, timeout=15); ftp.login()
    try:
        try: ftp.mkd("/Temp/" + DEEP_LIST_DIRECTORY)
        except ftplib.error_perm: pass
        ftp.cwd("/Temp/" + DEEP_LIST_DIRECTORY)
        existing = set(ftp.nlst())
        for index in range(DEEP_LIST_ENTRIES):
            name = "row%02d.txt" % index
            if name not in existing:
                ftp.storbinary("STOR " + name, __import__("io").BytesIO(b"soak\n"))
        return len(ftp.nlst())
    finally:
        try: ftp.quit()
        except Exception: ftp.close()


def remove_deep_list(host: str):
    ftp = ftplib.FTP(); ftp.connect(host, 21, timeout=15); ftp.login()
    try:
        ftp.cwd("/Temp/" + DEEP_LIST_DIRECTORY)
        for name in ftp.nlst():
            try: ftp.delete(name)
            except ftplib.error_perm: pass
        ftp.cwd("/Temp")
        try: ftp.rmd(DEEP_LIST_DIRECTORY)
        except ftplib.error_perm: pass
    finally:
        try: ftp.quit()
        except Exception: ftp.close()


def enter_deep_list(u64: "U64"):
    """Navigate the on-screen menu from wherever it is into the deep list.

    Return on a directory opens a context menu whose first item is `Enter`, so
    each level costs two Return presses.
    """
    # Reopening the menu restores whichever directory it was left in, so back
    # out until the root browser is on screen rather than pressing a fixed
    # number of times.
    for attempt in range(12):
        if not u64.machine.menu_open():
            u64.machine.menu_button(); time.sleep(.8)
        try: rows = u64.rows()
        except Failure: rows = []
        if rows and rows[0].startswith("Temp") and any(line.startswith("Flash") for line in rows):
            break
        # Cursor left leaves a directory.  run_stop closes the whole menu.
        u64.key("left_shift", "cursor_left_right", settle=.35)
    else:
        raise Failure("could not return to the root browser of the Ultimate menu")
    # The root browser is six entries deep, so ten Up presses reach the top from
    # any position without needing to know where the selection started.
    for _ in range(10): u64.key("left_shift", "cursor_up_down", settle=.12)
    row, text = u64.screen()
    if not text.startswith("Temp"):
        raise Failure(f"expected the RAM disk at the top of the root browser, found {text!r}")
    u64.key("return", settle=.5); u64.key("return", settle=.9)
    row, text = u64.screen()
    if not text.startswith(DEEP_LIST_DIRECTORY):
        raise Failure(f"expected {DEEP_LIST_DIRECTORY!r} inside the RAM disk, found {text!r}")
    u64.key("return", settle=.5); u64.key("return", settle=.9)
    row, text = u64.screen()
    if not text.startswith("row"):
        raise Failure(f"expected the generated list, found {text!r}")


def require_fixture(status: Dict[str, Any]):
    if status.get("protocol_version") != PROTOCOL_VERSION or not status.get("hid_open"):
        raise Failure(f"Pico HID not enumerated/open: {status}")
    if status.get("idle_rate") != 25:
        raise Failure(f"Pico idle_rate is {status.get('idle_rate')!r}, expected 25 (U64 SET_IDLE/GET_IDLE path is absent)")
    if status.get("currently_held_keys"): raise Failure(f"Pico has keys held: {status}")


def navigate(pico: Pico, u64: U64, command: str, key: str, **kwargs) -> Tuple[int, str]:
    pico.call(command, key=key, **kwargs); time.sleep(.18); return u64.stable_screen()


def preflight(pico: Pico, u64: U64):
    with check("Pico HID is enumerated and U64 negotiated SET_IDLE(25)"):
        require_fixture(pico.call("status"))
    with check("open the Ultimate menu on a list deep enough to hold a repeat"):
        u64.open_menu()
        entries = build_deep_list(u64.host)
        if entries < DEEP_LIST_ENTRIES:
            raise Failure(f"the RAM disk list holds {entries} entries, expected {DEEP_LIST_ENTRIES}")
        enter_deep_list(u64); anchor = u64.screen()
    with check("normal Down tap moves once and Up returns"):
        down = navigate(pico, u64, "tap", "down", duration_ms=30)
        if down == anchor: raise Failure(f"normal Down did not move: {anchor}")
        if navigate(pico, u64, "tap", "up", duration_ms=30) != anchor: raise Failure("Up did not return to the anchor")
    with check("genuine held Down repeats and stops on release"):
        start = u64.screen(); pico.call("hold", key="down", duration_ms=760); time.sleep(.1); after = u64.screen()
        if after == start or after == down: raise Failure(f"held key did not make more than one movement: {start} -> {after}")
        stable = u64.screen(); time.sleep(.4)
        if u64.screen() != stable: raise Failure("selection continued moving after hold release")
    with check("menu still responds to a single tap after a held key"):
        # A held Down can reach the end of the menu, where further Down taps do
        # not move the selection.  Hold Up for the same period to come back, then
        # prove the menu is still moving one row at a time before the fault cases
        # rely on that.
        pico.call("hold", key="up", duration_ms=760); time.sleep(.3)
        recovered = u64.screen()
        moved = navigate(pico, u64, "tap", "down", duration_ms=30)
        if moved == recovered: raise Failure(f"menu stopped responding to Down after the held key: {recovered}")
        if navigate(pico, u64, "tap", "up", duration_ms=30) != recovered:
            raise Failure("Up did not return to the recovered position")
    for fault in ("drop_release_once", "silence_after_press"):
        with check(f"{fault} produces exactly one menu movement"):
            anchor = u64.screen(); expected = navigate(pico, u64, "tap", "down", duration_ms=30)
            navigate(pico, u64, "tap", "up", duration_ms=30)
            actual = navigate(pico, u64, fault, "down", duration_ms=30, fault_duration_ms=750)
            if actual != expected: raise Failure(f"{fault}: anchor={anchor}, expected={expected}, actual={actual}. "
                                                 "Check that the Pico is the only USB keyboard attached to the "
                                                 "Ultimate 64: the firmware disables the idle-rate repeat bound "
                                                 "while a second keyboard is present.")
            require_fixture(pico.call("status"))


def run_soak(pico: Pico, u64: U64, duration: float, seed: int):
    import random
    randomizer = random.Random(seed); deadline = time.monotonic() + duration; iteration = 0
    while time.monotonic() < deadline:
        iteration += 1
        # Each iteration leaves the selection one row further in `key`'s
        # direction, because the fault case is expected to move exactly once.
        # Alternate the direction every iteration so the selection stays in the
        # middle of the menu instead of drifting to an end and clamping.  The
        # fault alternates on a different period, so all four combinations run.
        key = "down" if iteration % 2 else "up"; inverse = "up" if key == "down" else "down"
        fault = "drop_release_once" if (iteration // 2) % 2 else "silence_after_press"
        tap = randomizer.choice((12, 18, 25, 30, 35, 45, 60))
        fault_duration = randomizer.choice((400, 500, 600, 750, 900, 1050, 1200))
        with check(f"iteration {iteration}: normal {key}, {fault}"):
            require_fixture(pico.call("status")); anchor = u64.stable_screen()
            expected = navigate(pico, u64, "tap", key, duration_ms=tap)
            if expected == anchor:
                # The selection is at the end of the menu in this direction.
                # Step once the other way and repeat, so a menu boundary is not
                # reported as a firmware failure.
                anchor = navigate(pico, u64, "tap", inverse, duration_ms=tap)
                expected = navigate(pico, u64, "tap", key, duration_ms=tap)
                if expected == anchor: raise Failure(f"iteration {iteration}: normal {key} tap did not move from {anchor} in either position")
            if navigate(pico, u64, "tap", inverse, duration_ms=tap) != anchor: raise Failure(f"iteration {iteration}: inverse tap did not return")
            actual = navigate(pico, u64, fault, key, duration_ms=tap, fault_duration_ms=fault_duration)
            if actual != expected:
                status = pico.call("status")
                raise Failure(f"iteration={iteration} fault={fault} key={key} tap={tap}ms fault_duration={fault_duration}ms anchor={anchor} expected={expected} actual={actual} Pico={status}")
            require_fixture(pico.call("status"))
        if iteration % 5 == 0:
            # A genuine hold is the positive control: it proves the firmware has
            # not answered the fault cases by disabling auto-repeat altogether.
            with check(f"iteration {iteration}: genuine held {key} still repeats"):
                start = u64.stable_screen()
                hold = randomizer.choice((500, 700, 900))
                pico.call("hold", key=key, duration_ms=hold); time.sleep(.25)
                after = u64.stable_screen()
                if after == start:
                    raise Failure(f"iteration {iteration}: a genuine {hold}ms hold of {key} did not move from {start}")
                settled = u64.stable_screen(); time.sleep(.5)
                if u64.stable_screen() != settled:
                    raise Failure(f"iteration {iteration}: the selection kept moving after the hold was released")
                # Come back the same way so the next iterations stay in the
                # middle of the list rather than sitting against its end.
                pico.call("hold", key=inverse, duration_ms=hold); time.sleep(.35)
                require_fixture(pico.call("status"))


def bootsel_disks():
    matches = []
    for path in glob.glob("/dev/disk/by-id/*"):
        name = os.path.basename(path).lower()
        if "rp2350" in name or ("rpi" in name and "pico" in name):
            if not name.endswith("-part1"): matches.append(os.path.realpath(path))
    return sorted(set(matches))


def serial_ports():
    return sorted(glob.glob("/dev/serial/by-id/*"))


def linux_hid_self_test(pico: Pico):
    """Observe a benign F13 press and release arriving from the Pico keyboard.

    Reading `/dev/input/event*` directly requires membership of the `input`
    group.  Where that is not available, an X11 session can observe the same
    key through `xinput`, which needs no extra privilege.  F13 is used because
    it has no default binding in the shell, although some desktops open a
    settings panel for it.
    """
    paths = glob.glob("/dev/input/by-id/*MicroPython*event-kbd")
    if len(paths) != 1:
        raise Failure(f"expected one Linux event device for the Pico keyboard, found {paths!r}")
    try:
        fd = os.open(paths[0], os.O_RDONLY | os.O_NONBLOCK)
    except PermissionError:
        detail("setup: %s is not readable by this user, using xinput instead" % paths[0])
        return xinput_hid_self_test(pico)
    except OSError as exc:
        raise Failure(f"cannot read {paths[0]} for HID self-test: {exc}") from exc
    pressed = released = False
    try:
        pico.call("tap", key="f13", duration_ms=30)
        deadline = time.monotonic() + 2
        event = struct.Struct("llHHI")
        while time.monotonic() < deadline:
            readable, _, _ = select.select([fd], [], [], .1)
            if not readable: continue
            data = os.read(fd, event.size * 16)
            for offset in range(0, len(data) - event.size + 1, event.size):
                _, _, event_type, code, value = event.unpack_from(data, offset)
                if event_type == 1 and code == KEY_F13:
                    pressed |= value == 1; released |= value == 0
        if not (pressed and released):
            raise Failure("Pico HID self-test did not produce both Linux KEY_F13 press and release")
    finally:
        os.close(fd); pico.call("release_all")


def xinput_hid_self_test(pico: Pico):
    if not shutil.which("xinput") or not os.environ.get("DISPLAY"):
        raise Failure("the Pico input device is not readable and no X11 xinput fallback is available; "
                      "add this user to the input group and log in again")
    listing = subprocess.run(["xinput", "list"], text=True, stdout=subprocess.PIPE).stdout
    identifiers = [line.split("id=", 1)[1].split()[0] for line in listing.splitlines()
                   if "MicroPython" in line and "slave  keyboard" in line]
    if len(identifiers) != 1:
        raise Failure(f"expected one MicroPython xinput keyboard, found {identifiers!r}")
    watcher = subprocess.Popen(["xinput", "test", identifiers[0]], text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    try:
        time.sleep(.5); pico.call("tap", key="f13", duration_ms=30); time.sleep(1.0)
    finally:
        watcher.terminate(); output = watcher.communicate(timeout=5)[0] or ""; pico.call("release_all")
    # X11 keycodes are the kernel's evdev codes plus 8.
    expected = str(KEY_F13 + 8)
    presses = sum(line.startswith("key press") and line.split()[-1] == expected for line in output.splitlines())
    releases = sum(line.startswith("key release") and line.split()[-1] == expected for line in output.splitlines())
    if (presses, releases) != (1, 1):
        raise Failure(f"expected exactly one F13 press and release through xinput, saw {presses} and {releases}")


def run(command, **kwargs):
    detail("setup: " + " ".join(command)); return subprocess.run(command, check=True, text=True, **kwargs)


def linux_validation(pico: Pico):
    """Validate the fixture against Linux, which is all that setup can prove.

    The U64 has not been attached yet, so the HID idle rate is whatever Linux
    negotiated.  `require_fixture` additionally demands `idle_rate == 25`, which
    only the Ultimate 64 firmware sets, so it is not used here.
    """
    status = pico.call("status")
    if status.get("protocol_version") != PROTOCOL_VERSION:
        raise Failure("Pico reported protocol version %r, expected %d" % (status.get("protocol_version"), PROTOCOL_VERSION))
    if not status.get("hid_open"):
        raise Failure("Pico booted but its HID interface is not open on Linux: %s" % status)
    linux_hid_self_test(pico)
    detail("setup: Linux validation passed; idle_rate is %r as negotiated by Linux, not by the U64"
           % status.get("idle_rate"))


def setup_pico(ssid_override: Optional[str], pico_host: Optional[str] = None):
    ssid = ssid_override or os.environ.get("PICO_WIFI_SSID"); password = os.environ.get("PICO_WIFI_PASSWORD")
    if not ssid or not password: raise Failure("--setup-pico requires PICO_WIFI_SSID and PICO_WIFI_PASSWORD (password is never printed)")
    if not shutil.which("mpremote"): raise Failure("mpremote is required; run: pip install -r tests/requirements.txt")
    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "1541ultimate" / "pico-usb-keyboard-soak"; cache.mkdir(parents=True, exist_ok=True)
    disks, ports = bootsel_disks(), serial_ports()
    if len(disks) == 1:
        uf2 = cache / ("RPI_PICO2_W-v" + UF2_VERSION + ".uf2")
        if not uf2.exists():
            detail("setup: downloading pinned official MicroPython " + UF2_VERSION + " to " + str(uf2))
            urllib.request.urlretrieve(UF2_URL, uf2)
        mount = None
        for candidate in ("/media/" + os.environ.get("USER", "") + "/RP2350", "/run/media/" + os.environ.get("USER", "") + "/RP2350"):
            if os.path.ismount(candidate): mount = candidate
        if not mount:
            output = subprocess.check_output(["udisksctl", "mount", "-b", disks[0] + "1"], text=True); mount = output.split(" at ", 1)[1].strip().rstrip(".")
        detail("setup: copying UF2 to " + mount); shutil.copy2(uf2, mount); os.sync()
        deadline = time.monotonic() + 45; port = None
        while time.monotonic() < deadline:
            ports = serial_ports()
            if len(ports) == 1: port = ports[0]; break
            time.sleep(.5)
        if not port: raise Failure("MicroPython CDC did not enumerate within 45s")
    elif len(ports) == 1:
        # The board already runs MicroPython and still exposes its CDC serial
        # port, so it can be re-provisioned without erasing the firmware.
        port = ports[0]
        detail("setup: no BOOTSEL disk; re-provisioning the MicroPython board on " + port)
    else:
        raise Failure("found %r BOOTSEL disks and %r MicroPython serial ports; expected one of either. "
                      "Unplug the Pico 2 W. Hold down BOOTSEL, reconnect its USB cable while continuing "
                      "to hold BOOTSEL, then release BOOTSEL after about one second." % (disks, ports))
    # mip accepts an index/package pair, not a raw manifest URL.  Copy the
    # package files through mpremote instead, after caching the exact upstream
    # revision.  This avoids silently taking whichever package version happens
    # to be current when setup is run.
    hid_cache = cache / ("micropython-lib-" + HID_LIB_REVISION)
    for upstream, target in HID_FILES:
        local = hid_cache / target
        if not local.exists():
            local.parent.mkdir(parents=True, exist_ok=True)
            url = ("https://raw.githubusercontent.com/micropython/micropython-lib/" +
                   HID_LIB_REVISION + "/micropython/usb/" + upstream)
            detail("setup: downloading pinned usb-device-hid source " + upstream)
            urllib.request.urlretrieve(url, local)
    for directory in (":/lib", ":/lib/usb", ":/lib/usb/device"):
        # mkdir fails when the directory already exists, which is the normal
        # state on a board that has been provisioned before.
        mkdir = ["mpremote", "connect", port, "fs", "mkdir", directory]
        detail("setup: " + " ".join(mkdir))
        subprocess.run(mkdir, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for _, target in HID_FILES:
        run(["mpremote", "connect", port, "cp", str(hid_cache / target), ":/lib/" + target])
    config = tempfile.NamedTemporaryFile("w", delete=False)
    try:
        config.write("WIFI_SSID = %r\nWIFI_PASSWORD = %r\n" % (ssid, password)); config.close()
        pico = Path(__file__).with_name("pico")
        # boot.py is copied last and nothing is run afterwards.  Every mpremote
        # command soft resets the board, which executes boot.py.  Once boot.py
        # exists, that reset configures the HID interface during early boot and
        # removes the CDC serial port this deployment path depends on.
        for source, destination in ((pico / "u64_hid_keyboard.py", ":u64_hid_keyboard.py"),
                                    (pico / "main.py", ":main.py"),
                                    (Path(config.name), ":config.py"),
                                    (pico / "boot.py", ":boot.py")):
            run(["mpremote", "connect", port, "cp", str(source), destination])
    finally:
        os.unlink(config.name)
    # `mpremote reset` deliberately leaves a raw REPL, which does not run
    # main.py.  A machine reset ends the management session and starts the
    # deployed application.  This is the first reset after boot.py exists, so
    # it is also the first boot that configures HID before enumeration.  The
    # serial port disappears and reappears, so this command's own exit status
    # is not meaningful.
    command = ["mpremote", "connect", port, "exec", "import machine; machine.reset()"]
    detail("setup: " + " ".join(command))
    subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    # Wi-Fi association after a reset takes up to 30 s on this board.
    time.sleep(8); host = pico_host or discover_pico(30); pico = Pico(host)
    linux_validation(pico)
    status = pico.call("status")
    print("Pico 2 W configured successfully.\n\nDevice: %s\nWi-Fi: connected\nIP: %s\n\nNow unplug the Pico from this computer and connect it to a rear USB port of the Ultimate 64.\n\nThen run:\n\n./run-tests -H u64 --soak -s usb-keyboard-repeat" % (status["device_id"], host))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64")); parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS")); parser.add_argument("-t", "--timeout", type=float, default=float(os.environ.get("U64_TIMEOUT", "10")))
    parser.add_argument("--pico-host", help="fixture IP address; required on networks that do not "
                        "forward broadcast between the wired test host and the Wi-Fi client")
    parser.add_argument("--profile", choices=DEFAULT_DURATION, default="stress"); parser.add_argument("--duration", type=parse_duration); parser.add_argument("--seed", type=int, default=797)
    parser.add_argument("--setup-pico", action="store_true"); parser.add_argument("--wifi-ssid")
    args = parser.parse_args()
    if args.setup_pico: setup_pico(args.wifi_ssid, args.pico_host); return 0
    host = args.pico_host or discover_pico(); pico = Pico(host); u64 = U64(args.host, args.password, args.timeout)
    duration = args.duration or DEFAULT_DURATION[args.profile]
    detail(f"U64 hostname={args.host} Pico={host} profile={args.profile} duration={duration}s seed={args.seed}")
    try:
        info = u64.rest.json("/v1/version").get("version", "")
        detail("U64 version=" + str(info))
    except Exception as exc: raise Failure("U64 is not reachable: " + str(exc)) from exc
    try:
        preflight(pico, u64); run_soak(pico, u64, duration, args.seed)
    finally:
        try:
            pico.call("release_all")
            if pico.call("status").get("currently_held_keys"):
                raise Failure("fixture still reports held keys after release_all")
            for _ in range(3): u64.key("left_shift", "cursor_left_right", settle=.25)
            u64.close_menu()
        except Exception as cleanup: detail("cleanup failure: " + str(cleanup))
        try: remove_deep_list(u64.host)
        except Exception as cleanup: detail("cleanup failure removing the RAM disk list: " + str(cleanup))
        pico.close()
    suite_ok("usb_keyboard_repeat_soak_test", f"duration={duration}s")
    return 0


if __name__ == "__main__":
    try: raise SystemExit(main())
    except Failure as exc: suite_fail("usb_keyboard_repeat_soak_test", format_exception(exc)); raise SystemExit(1)
