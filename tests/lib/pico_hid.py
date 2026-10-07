#!/usr/bin/env python3
"""A Raspberry Pi Pico 2 W as a USB keyboard and wheel mouse for the Ultimate.

The Pico runs the MicroPython fixture in `tests/soak/io/usb/pico/`. One USB
device offers a boot keyboard and a wheel mouse with a horizontal wheel, and a
narrow line protocol over Wi-Fi drives both. The fixture never runs code a
caller sends: it can only press keys from a fixed table and send mouse reports.

This module is the host side, shared by every suite that uses the fixture:

- `Pico`, the control client, with keyboard and mouse helpers
- `discover_pico`, which finds the fixture on the local network
- `setup_pico`, which provisions a Pico from this computer

Provision a Pico once, connected to this computer by USB:

    PICO_WIFI_SSID=... PICO_WIFI_PASSWORD=... tests/lib/pico_hid.py setup

Then connect it to a USB port of the Ultimate. `tests/lib/pico_hid.py status`
prints what the fixture reports. The full guide is
`tests/soak/doc/usb-keyboard-repeat-pico.md`.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path
from typing import Any

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
from report import Failure, detail  # noqa: E402

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
FIXTURE_DIR = Path(__file__).resolve().parents[1] / "soak" / "io" / "usb" / "pico"
FIXTURE_FILES = ("u64_hid_keyboard.py", "u64_hid_mouse.py", "main.py")
KEY_F13 = 183  # Linux input-event-codes.h
REL_X = 0      # Linux input-event-codes.h
EV_REL = 2

# Mouse buttons as the fixture's `mouse_buttons` bit mask, which is HID order.
MOUSE_BUTTONS = {"left": 0x01, "right": 0x02, "middle": 0x04}


# Replies can be longer than requests: a status carries every counter.
MAX_REPLY = 4096
# How long a command may take on top of the time its own arguments ask for.
REPLY_MARGIN_SECONDS = 8.0


def _command_seconds(command: str, arguments: dict[str, Any]) -> float:
    """The time a command spends on the fixture before it answers."""
    if command == "mouse_stream":
        return arguments.get("count", 1) * max(20, arguments.get("interval_ms", 0)) / 1000.0
    if command == "mouse_wheel":
        detents = abs(arguments.get("vertical", 0)) + abs(arguments.get("horizontal", 0))
        return detents * max(20, arguments.get("gap_ms", 150)) / 1000.0
    return (arguments.get("duration_ms", 0) + arguments.get("fault_duration_ms", 0)) / 1000.0


class Pico:
    def __init__(self, host: str):
        self.host, self.sock, self.request_id = host, None, 0
    def connect(self):
        self.close()
        self.sock = socket.create_connection((self.host, TCP_PORT), timeout=5)
    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None
    def call(self, command: str, **kwargs) -> dict[str, Any]:
        if not self.sock:
            self.connect()
        self.request_id += 1
        request = {"protocol_version": PROTOCOL_VERSION, "id": self.request_id, "command": command, **kwargs}
        wire = (json.dumps(request, separators=(",", ":")) + "\n").encode()
        if len(wire) > MAX_LINE:
            raise Failure("fixture request unexpectedly exceeds protocol limit")
        try:
            self.sock.settimeout(REPLY_MARGIN_SECONDS + _command_seconds(command, kwargs))
            self.sock.sendall(wire)
            data = b""
            while not data.endswith(b"\n"):
                if len(data) >= MAX_REPLY:
                    raise ValueError("fixture reply is too long")
                part = self.sock.recv(MAX_REPLY - len(data))
                if not part:
                    raise OSError("fixture closed its control connection")
                data += part
            answer = json.loads(data)
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            self.close()
            raise Failure(f"Pico control failure: {exc}") from exc
        if answer.get("id") != self.request_id or answer.get("protocol_version") != PROTOCOL_VERSION or not answer.get("ok"):
            raise Failure(f"Pico rejected {command}: {answer.get('error', answer)!r}")
        return answer["result"]

    def status(self) -> dict[str, Any]:
        return self.call("status")

    def tap(self, key: str, duration_ms: int = 30) -> dict[str, Any]:
        return self.call("tap", key=key, duration_ms=duration_ms)

    def release_all(self) -> dict[str, Any]:
        return self.call("release_all")

    def press(self, key: str) -> dict[str, Any]:
        """Hold a key until `release_all`; the fixture keeps the report fresh."""
        return self.call("press", key=key)

    def mouse_move(self, dx: int, dy: int) -> dict[str, Any]:
        """One mouse report moving by dx, dy HID counts, each -127..127."""
        return self.call("mouse_move", dx=dx, dy=dy)

    def mouse_report(self, buttons: int, dx: int = 0, dy: int = 0, wheel: int = 0, pan: int = 0) -> dict[str, Any]:
        """One raw report: the button mask in HID order, then each axis -127..127."""
        return self.call("mouse_report", buttons=buttons, dx=dx, dy=dy, wheel=wheel, pan=pan)

    def mouse_stream(self, dx: int = 0, dy: int = 0, count: int = 1, interval_ms: int = 0,
                     wheel: int = 0, pan: int = 0, buttons: int | None = None,
                     key: str | None = None) -> dict[str, Any]:
        """`count` identical reports, `interval_ms` apart, 0 meaning as fast as the host polls.

        `buttons` keeps a mask for the whole stream, and `key` holds a keyboard
        key for its duration. The status that comes back names how many
        reports went out and how long that took, in `last_stream`.
        """
        arguments: dict[str, Any] = {"dx": dx, "dy": dy, "wheel": wheel, "pan": pan,
                                     "count": count, "interval_ms": interval_ms}
        if buttons is not None:
            arguments["buttons"] = buttons
        if key is not None:
            arguments["key"] = key
        return self.call("mouse_stream", **arguments)

    def mouse_buttons(self, *pressed: str) -> dict[str, Any]:
        """One mouse report with exactly these buttons held."""
        mask = 0
        for name in pressed:
            mask |= MOUSE_BUTTONS[name]
        return self.call("mouse_buttons", buttons=mask)

    def mouse_wheel(self, vertical: int = 0, horizontal: int = 0, gap_ms: int = 150) -> dict[str, Any]:
        """One report per detent, vertical first; returns once all are sent.

        Positive vertical is away from the user, positive horizontal is to
        the right, as HID and Linux define AC Pan. Each detent is followed by
        `gap_ms`, 0 meaning as fast as the host polls, so the call takes at
        least (|vertical| + |horizontal|) * gap_ms.
        """
        return self.call("mouse_wheel", vertical=vertical, horizontal=horizontal, gap_ms=gap_ms)

    def require_mouse(self) -> dict[str, Any]:
        status = self.status()
        if "mouse" not in status.get("capabilities", []):
            raise Failure("the Pico runs a keyboard-only fixture; provision it again with "
                          "tests/lib/pico_hid.py setup")
        if not status.get("mouse_open"):
            raise Failure(f"the fixture's mouse interface is not open: {status}")
        return status


def discover_pico(timeout: float = 4.0, optional: bool = False) -> str | None:
    """The fixture's address, or None when `optional` and none answers.

    A bench without the fixture is a bench where the suites that drive real
    USB HID cannot run at all, which is a different thing from a fixture that
    is present and misbehaving. `optional` lets a caller tell the two apart and
    skip rather than report a failure nobody can act on. Finding more than one
    stays a failure either way: which of them to drive is not for this to guess.
    """
    request = json.dumps({"service": MAGIC, "protocol_version": PROTOCOL_VERSION}).encode()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(.25)
    found = set()
    deadline = time.monotonic() + timeout
    # Limited broadcast is not always bridged between wired Ethernet and Wi-Fi.
    # Also send the ordinary /24 directed broadcast for the common lab LAN.
    targets = [("255.255.255.255", DISCOVERY_PORT)]
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("8.8.8.8", 80))
        octets = probe.getsockname()[0].split(".")
        probe.close()
        if len(octets) == 4:
            targets.append((".".join([*octets[:3], "255"]), DISCOVERY_PORT))
    except OSError:
        pass
    try:
        while time.monotonic() < deadline:
            for target in targets:
                sock.sendto(request, target)
            try:
                while True:
                    body, address = sock.recvfrom(MAX_LINE)
                    reply = json.loads(body)
                    if reply.get("service") == MAGIC and reply.get("protocol_version") == PROTOCOL_VERSION:
                        found.add(reply.get("ip") or address[0])
            except TimeoutError:
                pass
    finally:
        sock.close()
    if not found:
        found = sweep_for_pico()
    if not found and optional:
        return None
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
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("8.8.8.8", 80))
        octets = probe.getsockname()[0].split(".")
        probe.close()
    except OSError:
        return set()
    if len(octets) != 4:
        return set()
    prefix = ".".join(octets[:3])
    detail("discovery: broadcast found nothing, sweeping %s.0/24 for the fixture control port" % prefix)

    def probe_address(last):
        address = "%s.%d" % (prefix, last)
        try:
            connection = socket.create_connection((address, port), timeout=1.5)
            connection.close()
        except OSError:
            return None
        try:
            status = Pico(address).call("status")
        except Failure:
            return None
        return address if status.get("service") == MAGIC else None

    with concurrent.futures.ThreadPoolExecutor(max_workers=64) as pool:
        return {result for result in pool.map(probe_address, range(1, 255)) if result}




def bootsel_disks():
    matches = []
    for path in glob.glob("/dev/disk/by-id/*"):
        name = os.path.basename(path).lower()
        if "rp2350" in name or ("rpi" in name and "pico" in name):
            if not name.endswith("-part1"):
                matches.append(os.path.realpath(path))
    return sorted(set(matches))


def serial_ports():
    return sorted(glob.glob("/dev/serial/by-id/*MicroPython*"))


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
            if not readable:
                continue
            data = os.read(fd, event.size * 16)
            for offset in range(0, len(data) - event.size + 1, event.size):
                _, _, event_type, code, value = event.unpack_from(data, offset)
                if event_type == 1 and code == KEY_F13:
                    pressed |= value == 1
                    released |= value == 0
        if not (pressed and released):
            raise Failure("Pico HID self-test did not produce both Linux KEY_F13 press and release")
    finally:
        os.close(fd)
        pico.call("release_all")


def xinput_hid_self_test(pico: Pico):
    if not shutil.which("xinput") or not os.environ.get("DISPLAY"):
        raise Failure("the Pico input device is not readable and no X11 xinput fallback is available; "
                      "add this user to the input group and log in again")
    listing = subprocess.run(["xinput", "list"], text=True, stdout=subprocess.PIPE, check=False).stdout
    identifiers = [line.split("id=", 1)[1].split()[0] for line in listing.splitlines()
                   if "MicroPython" in line and "slave  keyboard" in line]
    if len(identifiers) != 1:
        raise Failure(f"expected one MicroPython xinput keyboard, found {identifiers!r}")
    watcher = subprocess.Popen(["xinput", "test", identifiers[0]], text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    try:
        time.sleep(.5)
        pico.call("tap", key="f13", duration_ms=30)
        time.sleep(1.0)
    finally:
        watcher.terminate()
        output = watcher.communicate(timeout=5)[0] or ""
        pico.call("release_all")
    # X11 keycodes are the kernel's evdev codes plus 8.
    expected = str(KEY_F13 + 8)
    presses = sum(line.startswith("key press") and line.split()[-1] == expected for line in output.splitlines())
    releases = sum(line.startswith("key release") and line.split()[-1] == expected for line in output.splitlines())
    if (presses, releases) != (1, 1):
        raise Failure(f"expected exactly one F13 press and release through xinput, saw {presses} and {releases}")


def linux_mouse_self_test(pico: Pico):
    """Observe the fixture mouse move one count right and back on Linux.

    The net movement is zero, so the desktop pointer ends where it started.
    Buttons and wheels are not tried, because on a desktop they click or
    scroll whatever is under the pointer.
    """
    paths = glob.glob("/dev/input/by-id/*MicroPython*event-mouse")
    if len(paths) != 1:
        raise Failure(f"expected one Linux event device for the Pico mouse, found {paths!r}")
    try:
        fd = os.open(paths[0], os.O_RDONLY | os.O_NONBLOCK)
    except PermissionError:
        detail("setup: %s is not readable by this user; the mouse is checked on the Ultimate instead" % paths[0])
        return
    motions = []
    try:
        pico.mouse_move(1, 0)
        pico.mouse_move(-1, 0)
        deadline = time.monotonic() + 2
        event = struct.Struct("llHHi")
        while time.monotonic() < deadline and len(motions) < 2:
            readable, _, _ = select.select([fd], [], [], .1)
            if not readable:
                continue
            data = os.read(fd, event.size * 16)
            for offset in range(0, len(data) - event.size + 1, event.size):
                _, _, event_type, code, value = event.unpack_from(data, offset)
                if event_type == EV_REL and code == REL_X:
                    motions.append(value)
    finally:
        os.close(fd)
    if motions != [1, -1]:
        raise Failure(f"Pico mouse self-test expected X motions [1, -1] on Linux, saw {motions!r}")


def linux_validation(pico: Pico):
    """Validate the fixture against Linux, which is all that setup can prove.

    The Ultimate has not been attached yet, so the HID idle rate is whatever
    Linux negotiated. The keyboard soak additionally demands `idle_rate == 25`,
    which only the Ultimate 64 firmware sets, so that is not checked here.
    """
    status = pico.call("status")
    if status.get("protocol_version") != PROTOCOL_VERSION:
        raise Failure("Pico reported protocol version %r, expected %d" % (status.get("protocol_version"), PROTOCOL_VERSION))
    if not status.get("hid_open"):
        raise Failure("Pico booted but its HID interface is not open on Linux: %s" % status)
    linux_hid_self_test(pico)
    pico.require_mouse()
    linux_mouse_self_test(pico)
    detail("setup: Linux validation passed; idle_rate is %r as negotiated by Linux, not by the Ultimate"
           % status.get("idle_rate"))


def choose_serial_port(port: str | None) -> str | None:
    """The MicroPython CDC port to provision over, or None for a BOOTSEL disk."""
    if port:
        if not os.path.exists(port):
            raise Failure(f"serial port {port} does not exist")
        return port
    disks, ports = bootsel_disks(), serial_ports()
    if len(disks) == 1:
        return None
    if len(ports) == 1:
        # The board already runs MicroPython and still exposes its CDC serial
        # port, so it can be re-provisioned without erasing the firmware.
        return ports[0]
    raise Failure("found %r BOOTSEL disks and %r MicroPython serial ports; expected one of either. "
                  "Pass --port with the Pico's /dev/serial/by-id path when several MicroPython boards "
                  "are connected. Otherwise unplug the Pico 2 W, hold down BOOTSEL, reconnect its USB "
                  "cable while continuing to hold BOOTSEL, then release BOOTSEL after about one second."
                  % (disks, ports))


def flash_micropython(cache: Path) -> str:
    """Copy the pinned MicroPython UF2 to the one BOOTSEL disk; return its CDC port."""
    disks = bootsel_disks()
    uf2 = cache / ("RPI_PICO2_W-v" + UF2_VERSION + ".uf2")
    if not uf2.exists():
        detail("setup: downloading pinned official MicroPython " + UF2_VERSION + " to " + str(uf2))
        urllib.request.urlretrieve(UF2_URL, uf2)
    mount = None
    for candidate in ("/media/" + os.environ.get("USER", "") + "/RP2350", "/run/media/" + os.environ.get("USER", "") + "/RP2350"):
        if os.path.ismount(candidate):
            mount = candidate
    if not mount:
        output = subprocess.check_output(["udisksctl", "mount", "-b", disks[0] + "1"], text=True)
        mount = output.split(" at ", 1)[1].strip().rstrip(".")
    detail("setup: copying UF2 to " + mount)
    before = set(serial_ports())
    shutil.copy2(uf2, mount)
    os.sync()
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        new = sorted(set(serial_ports()) - before)
        if len(new) == 1:
            return new[0]
        time.sleep(.5)
    raise Failure("MicroPython CDC did not enumerate within 45s")


def setup_pico(ssid_override: str | None = None, pico_host: str | None = None,
               port: str | None = None, keep_wifi_config: bool = False):
    ssid = ssid_override or os.environ.get("PICO_WIFI_SSID")
    password = os.environ.get("PICO_WIFI_PASSWORD")
    if not keep_wifi_config and (not ssid or not password):
        raise Failure("setup requires PICO_WIFI_SSID and PICO_WIFI_PASSWORD (password is never printed), "
                      "or --keep-wifi-config for a Pico that is already provisioned")
    if not shutil.which("mpremote"):
        raise Failure("mpremote is required; run: pip install -r tests/requirements.txt")
    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "1541ultimate" / "pico-usb-keyboard-soak"
    cache.mkdir(parents=True, exist_ok=True)
    port = choose_serial_port(port)
    if port is None:
        if keep_wifi_config:
            raise Failure("--keep-wifi-config needs a provisioned Pico, but a blank BOOTSEL disk was found")
        port = flash_micropython(cache)
    else:
        detail("setup: re-provisioning the MicroPython board on " + port)
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
    # One mpremote session with `resume`, so the board is not soft reset
    # between copies. A soft reset runs boot.py, and on a board provisioned
    # before, boot.py re-enumerates USB and drops the serial port mid-copy.
    # boot.py is still copied last, so a session that fails part way leaves a
    # board that boots without HID and keeps its serial port.
    session = ["mpremote", "connect", port, "resume"]
    for directory in (":/lib", ":/lib/usb", ":/lib/usb/device"):
        # mkdir fails when the directory already exists, which is the normal
        # state on a board that has been provisioned before.
        subprocess.run([*session, "fs", "mkdir", directory], text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    copies = [(hid_cache / target, ":/lib/" + target) for _, target in HID_FILES]
    copies += [(FIXTURE_DIR / name, ":" + name) for name in FIXTURE_FILES]
    # delete=False because mpremote needs the path after the handle closes;
    # the finally below unlinks it.
    config = tempfile.NamedTemporaryFile("w", delete=False)  # noqa: SIM115
    try:
        if not keep_wifi_config:
            config.write("WIFI_SSID = %r\nWIFI_PASSWORD = %r\n" % (ssid, password))
            copies.append((Path(config.name), ":config.py"))
        config.close()
        copies.append((FIXTURE_DIR / "boot.py", ":boot.py"))
        command = list(session)
        for source, destination in copies:
            if len(command) > len(session):
                command.append("+")
            command += ["fs", "cp", str(source), destination]
        detail("setup: copying " + ", ".join(destination for _, destination in copies))
        subprocess.run(command, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    finally:
        os.unlink(config.name)
    # A machine reset ends the management session and starts the deployed
    # application with HID configured before enumeration. The serial port
    # disappears and reappears, so this command's own exit status is not
    # meaningful.
    command = [*session, "exec", "import machine; machine.reset()"]
    detail("setup: " + " ".join(command))
    subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    # Wi-Fi association after a reset takes up to 30 s on this board.
    time.sleep(8)
    host = pico_host or discover_pico(30)
    pico = Pico(host)
    linux_validation(pico)
    status = pico.call("status")
    print("Pico 2 W configured successfully.\n\nDevice: %s\nWi-Fi: connected\nIP: %s\n\n"
          "Now unplug the Pico from this computer and connect it to a USB port of the Ultimate."
          % (status["device_id"], host))
    return host


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    setup = commands.add_parser("setup", help="provision a Pico connected to this computer")
    setup.add_argument("--wifi-ssid", help="default: $PICO_WIFI_SSID; the password is only read from $PICO_WIFI_PASSWORD")
    setup.add_argument("--keep-wifi-config", action="store_true",
                       help="re-provision a Pico without replacing its Wi-Fi settings")
    setup.add_argument("--port", help="the Pico's /dev/serial/by-id path, when several MicroPython boards are connected")
    for command in (setup, commands.add_parser("status", help="print what the fixture reports")):
        command.add_argument("--pico-host", help="fixture IP address; required on networks that do not "
                             "forward broadcast between the wired test host and the Wi-Fi client")
    args = parser.parse_args()
    if args.command == "setup":
        setup_pico(args.wifi_ssid, args.pico_host, args.port, args.keep_wifi_config)
    else:
        print(json.dumps(Pico(args.pico_host or discover_pico()).status(), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        print("pico_hid: " + str(exc), file=sys.stderr)
        raise SystemExit(1)
