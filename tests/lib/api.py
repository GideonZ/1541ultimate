"""The Ultimate's REST API, as typed calls rather than paths and query strings.

Modelled on the device's OpenAPI description for firmware 3.15. Each group
below mirrors one section of it: `machine`, `drives`, `configs`, `files`,
`runners`, `streams`, plus `version` and `info`.

Two things this buys over passing paths around. A call names what it does and
returns something with fields, so a suite reads as what it asks the device to
do. And the parameter limits the API actually has (a drive is `a` or `b`, an
address is at most four hex digits, a D64 has 35 to 41 tracks, an input batch
holds at most 64 events) are checked here, where the failure names the argument,
instead of arriving as an opaque HTTP 400.

Transport, the `X-Password` rule and the retry policy stay in `rest.py`. Reach
for `UltimateApi.rest` when a check is about the HTTP contract itself: status
codes, headers, malformed bodies, and authentication are what several suites
assert on, and those want the raw response rather than a decoded object.
"""

from __future__ import annotations

import json
import os
import time
import urllib.parse
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

import pacing
from report import Failure
from rest import DEFAULT_TIMEOUT, RestClient, Response, multipart_body

DRIVE_SLOTS = ("a", "b")
MAX_ADDRESS = 0xFFFF
MAX_READ_LENGTH = 65536
MAX_INPUT_EVENTS = 64
# PUT /v1/machine:writemem takes at most this many bytes as a hex string;
# anything longer has to go through the POST upload form.
MAX_WRITEMEM_HEX_BYTES = 128
MAX_KEYBOARD_INPUTS = 8
D64_TRACKS = (35, 41)
DNP_TRACKS = (1, 255)
INPUT_TRANSITIONS = ("press", "release", "tap")

# The menu screen is a 40x25 matrix of character and colour planes.
SCREEN_COLS = 40
SCREEN_ROWS = 25
SCREEN_CELLS = SCREEN_COLS * SCREEN_ROWS

# "READY." in C64 screen codes, which is how the KERNAL signals it has finished
# booting. Screen RAM is at $0400 and readable over DMA whatever the menu is
# doing, so this works with the menu open or closed.
SCREEN_RAM = 0x0400
READY_SCREEN_CODES = bytes((0x12, 0x05, 0x01, 0x04, 0x19, 0x2E))
# How long the machine gets to reach the BASIC prompt, and how often to look.
# Measured on a U64 Elite: a reset reaches READY in about 34ms, so the budget
# is generous and is only ever paid in full by a machine that is not going to
# get there, such as one resetting into a cartridge.
READY_TIMEOUT_SECONDS = 6.0
READY_POLL_SECONDS = 0.01

# config_menu.cc asks this before it leaves a config page with unsaved changes.
SAVE_TO_FLASH_PROMPT = "Save changes to Flash?"
# How many keystrokes close_menu_from_anywhere sends before it gives up on
# keys and toggles the menu button, and where it switches from F8 to RUN/STOP.
MENU_CLOSE_ATTEMPTS = 12
MENU_CLOSE_RUN_STOP_FROM = 8


# ---------------------------------------------------------------------------
# Decoded responses
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Info:
    product: str
    firmware_version: str
    fpga_version: str
    hostname: str
    extra: Dict[str, object] = field(default_factory=dict)


@dataclass(frozen=True)
class DriveInfo:
    slot: str
    enabled: bool
    bus_id: Optional[int]
    type: str
    rom: str
    image_file: str
    image_path: str
    extra: Dict[str, object] = field(default_factory=dict)

    @property
    def mounted(self) -> bool:
        return bool(self.image_file)


@dataclass(frozen=True)
class FileInfo:
    path: str
    filename: str
    size: int
    extension: str
    extra: Dict[str, object] = field(default_factory=dict)


def _errors(payload: object, what: str) -> Dict[str, object]:
    """Unwrap the device's `{..., "errors": [...]}` envelope."""
    if not isinstance(payload, dict):
        raise Failure(f"{what}: expected a JSON object, got {type(payload).__name__}")
    reported = payload.get("errors") or []
    if reported:
        raise Failure(f"{what} failed: {'; '.join(str(e) for e in reported)}")
    return payload


def _hex_address(address: int) -> str:
    if not 0 <= address <= MAX_ADDRESS:
        raise Failure(f"address ${address:X} is outside $0000-$FFFF")
    return f"{address:04X}"


def _padded_label(value: object) -> bool:
    """An enum label the firmware padded so the menu can right align it.

    The inner space is part of the test: " 0 dB" is padded to the width of the
    "-6 dB" beside it, and a caller that merely stripped all whitespace would
    match neither.
    """
    return (isinstance(value, str) and value != value.strip()
            and " " in value.strip())


def _plain_label(value: object) -> bool:
    """An enum label carrying no padding but still containing a space."""
    return isinstance(value, str) and value == value.strip() and " " in value


# ---------------------------------------------------------------------------
# Endpoint groups
# ---------------------------------------------------------------------------


class MachineApi:
    """/v1/machine:* - the C64 itself, its memory and its on-device UI."""

    def __init__(self, rest: RestClient) -> None:
        self._rest = rest
        # The mutation count as of the last reset. A harness that has just
        # reset the device exports U64_DEVICE_RESET, which seeds this so the
        # first suite in a fresh process does not reset a machine nothing has
        # touched since. Any mutating call clears the assumption by advancing
        # the counter past it.
        self._reset_at: Optional[int] = (
            rest.mutations if os.environ.get("U64_DEVICE_RESET") == "1" else None)

    def _act(self, action: str) -> None:
        code, _, body = self._rest.request("PUT", f"/v1/machine:{action}")
        if code != 200:
            raise Failure(f"machine:{action} returned HTTP {code}: {body[:160]!r}")

    def wait_until_ready(self, timeout: float = READY_TIMEOUT_SECONDS) -> bool:
        """Poll screen RAM until the KERNAL has printed READY. No fixed sleep.

        Returns whether it appeared. False is not necessarily a fault: a
        machine resetting into a cartridge never prints it, so the caller
        decides what that means.

        Polling costs GETs, which do not count as mutations, so waiting here
        cannot make the next reset look necessary. See reset().
        """
        deadline = time.monotonic() + timeout
        while True:
            if READY_SCREEN_CODES in self.readmem(SCREEN_RAM, 400):
                return True
            if time.monotonic() >= deadline:
                return False
            time.sleep(READY_POLL_SECONDS)

    def reset(self, force: bool = False, wait: bool = True,
              timeout: float = READY_TIMEOUT_SECONDS) -> bool:
        """Reset the C64, unless nothing has happened that a reset would clear.

        Waits for the BASIC prompt by polling rather than sleeping for a fixed
        time: measured at about 34ms on a U64 Elite, against the 1 to 3 second
        sleeps this replaces. Pass wait=False for a machine that is not
        expected to reach the prompt, such as one resetting into a cartridge.

        Resetting twice in a row is common and always wasted: one suite ends
        with a reset and the next begins with one, and a suite's own setup
        often resets after a helper already did. Only a request to the device
        can put the machine into a state a reset would clear, so if the
        transport has sent nothing since the last reset, this one cannot change
        anything and is skipped.

        Only mutating requests count. Reading memory, the menu screen or a
        config value cannot move the machine, so a health check between two
        resets does not make the second one necessary.

        Across processes the runner exports U64_DEVICE_RESET after its own
        reset, which covers the common case of one suite ending with a reset
        and the next beginning with one. A caller that reached the device by
        another route entirely (FTP, Telnet, the DMA control port) should pass
        `force`, since those are invisible here.
        """
        if not force and self._reset_at == self._rest.mutations:
            return True
        # Blank the top of the screen first, so the READY left by the previous
        # boot cannot be mistaken for this one. Without it the wait returns
        # immediately and proves nothing.
        if wait:
            self.writemem(SCREEN_RAM, bytes([0x20]) * len(READY_SCREEN_CODES))
        self._act("reset")
        self._reset_at = self._rest.mutations
        if not wait:
            return False
        ready = self.wait_until_ready(timeout)
        # The blanking write and the reset both counted as mutations, so the
        # bookkeeping is restored to "reset, and untouched since".
        self._reset_at = self._rest.mutations
        return ready

    @property
    def was_just_reset(self) -> bool:
        """Whether this client reset the device and nothing has moved it since."""
        return self._reset_at is not None and self._reset_at == self._rest.mutations

    def reboot(self) -> None:
        self._act("reboot")

    def pause(self) -> None:
        self._act("pause")

    def resume(self) -> None:
        self._act("resume")

    def poweroff(self) -> None:
        self._act("poweroff")

    def menu_button(self) -> None:
        self._act("menu_button")

    def readmem(self, address: int, length: int = 256) -> bytes:
        if not 0 < length <= MAX_READ_LENGTH:
            raise Failure(f"length {length} is outside 1..{MAX_READ_LENGTH}")
        return self._rest.expect("GET", "/v1/machine:readmem",
                                 params={"address": _hex_address(address), "length": length})

    def writemem(self, address: int, data: bytes, idempotent: bool = False) -> None:
        """Write `data` at `address`.

        The API offers two forms and they are not interchangeable: PUT carries
        the bytes as a hex query string and takes at most
        MAX_WRITEMEM_HEX_BYTES, while POST uploads them as a file part. The
        right one is chosen from the length here, because an over-long PUT
        fails in two different ways and neither is useful to a caller.

        Measured on a U64 Elite, firmware 3.15, writing to $C800:

        - up to 128 bytes: HTTP 200.
        - 129 to about 900 bytes: HTTP 400 from route_machine.cc, promptly.
        - from about 950 bytes: no answer at all. The request grows past what
          the httpd will accumulate for one request, so it never reaches the
          handler and the caller waits out its whole timeout. 2000 bytes was
          refused earlier still, with the connection reset.

        The boundary between the last two is not a constant to rely on: it
        depends on the whole request rather than the data alone, which is why
        the routing here keys off MAX_WRITEMEM_HEX_BYTES and not off it.

        `idempotent` opts into the transport retry, for a write that applies
        the same state however many times it arrives. See rest.RestClient.
        """
        if not data:
            return
        if len(data) <= MAX_WRITEMEM_HEX_BYTES:
            code, _, body = self._rest.request(
                "PUT", "/v1/machine:writemem", idempotent=idempotent,
                params={"address": _hex_address(address), "data": data.hex()})
        else:
            payload, content_type = multipart_body("file", "data.bin", data)
            code, _, body = self._rest.request(
                "POST", "/v1/machine:writemem", idempotent=idempotent,
                params={"address": _hex_address(address)}, body=payload,
                headers={"Content-Type": content_type})
        if code != 200:
            raise Failure(f"writemem ${address:04X} ({len(data)} bytes) returned "
                          f"HTTP {code}: {body[:160]!r}")

    def menu_screen(self) -> Optional[bytes]:
        """The rendered menu screen, or None when no menu is open (HTTP 404)."""
        code, _, body = self._rest.request("GET", "/v1/machine:menu_screen")
        if code == 404:
            return None
        if code != 200:
            raise Failure(f"machine:menu_screen returned HTTP {code}: {body[:160]!r}")
        return body

    def menu_open(self) -> bool:
        return self.menu_screen() is not None

    def input_state(self) -> Dict[str, object]:
        return _errors(self._rest.json("/v1/machine:input"), "machine:input")

    def send_input(self, events: Sequence[Dict[str, object]]) -> None:
        if not 0 < len(events) <= MAX_INPUT_EVENTS:
            raise Failure(f"an input batch holds 1..{MAX_INPUT_EVENTS} events, got {len(events)}")
        for event in events:
            inputs = event.get("inputs") or []
            if event.get("kind") == "keyboard" and len(inputs) > MAX_KEYBOARD_INPUTS:
                raise Failure(f"a keyboard event holds at most {MAX_KEYBOARD_INPUTS} "
                              f"inputs, got {len(inputs)}")
            transition = event.get("transition")
            if transition is not None and transition not in INPUT_TRANSITIONS:
                raise Failure(f"unknown transition {transition!r}; "
                              f"expected one of {', '.join(INPUT_TRANSITIONS)}")
        code, _, body = self._rest.request("POST", "/v1/machine:input",
                                           payload={"events": list(events)})
        if code != 200:
            raise Failure(f"machine:input returned HTTP {code}: {body[:160]!r}")

    def press(self, *keys: str) -> None:
        """Tap one keyboard combination."""
        self.send_input([{"kind": "keyboard", "inputs": list(keys), "transition": "tap"}])

    def release_all(self) -> None:
        self.send_input([{"kind": "release_all"}])

    def menu_rows(self) -> List[str]:
        """The open menu as 25 rows of text, or an empty list when it is closed.

        Only the character plane is decoded, and only its printable range;
        everything else becomes a space, so a search for a label finds it
        wherever the firmware drew it.
        """
        body = self.menu_screen()
        if body is None:
            return []
        chars = "".join(chr(c & 0x7F) if 0x20 <= (c & 0x7F) <= 0x7E else " "
                        for c in body[:SCREEN_CELLS])
        return [chars[r * SCREEN_COLS:(r + 1) * SCREEN_COLS] for r in range(SCREEN_ROWS)]

    def close_menu_from_anywhere(self, confirm_key: Optional[str] = None) -> None:
        """Back out of the whole UI object stack until the menu is closed.

        Every step here is one that a separate copy of this got wrong, and the
        copies diverged in ways that changed what the device was left holding:

        - Injected input is released first, so a key still held down by an
          earlier check does not fight the keys sent below.
        - "Save changes to Flash?" is answered No, using the popup's own 'n'
          hotkey. The popup's first button is Yes and RETURN takes whichever
          button is active, so answering it blind writes whatever the suite
          had changed into the device's flash.
        - F8 (shift+F7) is the firmware's full UI exit. It destroys nested
          config and search objects instead of hiding them for the next suite
          to reopen, which RUN/STOP alone does not do.
        - RUN/STOP takes over for the last few attempts, for the editors F8
          does not reach. Never RETURN by default: it activates the entry
          under the cursor, and on the Assembly 64 entry that opens its form.
        - The menu button is the last resort rather than the first, because it
          is a toggle and does not leave a nested object.

        `confirm_key` is pressed on every other attempt when it is given. It is
        for a suite that leaves OK popups of its own behind, such as "N files
        placed on clipboard": UIPopup::poll answers only RETURN, SPACE and its
        own button hotkeys, so neither F8 nor RUN/STOP dismisses one and the
        loop would spend every attempt on a popup that is not going anywhere.

        Raises Failure if the menu is still open at the end.
        """
        self.release_all()
        for attempt in range(MENU_CLOSE_ATTEMPTS):
            rows = self.menu_rows()
            if not rows:
                return
            if any(SAVE_TO_FLASH_PROMPT in row for row in rows):
                self.press("n")
            elif confirm_key is not None and attempt % 2:
                self.press(confirm_key)
            elif attempt < MENU_CLOSE_RUN_STOP_FROM:
                self.press("left_shift", "f7")
            else:
                self.press("run_stop")
            time.sleep(pacing.MENU_TOGGLE_SETTLE_SECONDS)
        self.menu_button()
        time.sleep(pacing.MENU_TOGGLE_SETTLE_SECONDS)
        if self.menu_open():
            raise Failure("the menu could not be closed from where the last check left it")


class DrivesApi:
    """/v1/drives - the emulated 1541/1571/1581 slots."""

    def __init__(self, rest: RestClient) -> None:
        self._rest = rest

    @staticmethod
    def _slot(slot: str) -> str:
        if slot not in DRIVE_SLOTS:
            raise Failure(f"unknown drive {slot!r}; expected one of {', '.join(DRIVE_SLOTS)}")
        return slot

    def list(self) -> Dict[str, DriveInfo]:
        payload = _errors(self._rest.json("/v1/drives"), "drives")
        found: Dict[str, DriveInfo] = {}
        for entry in payload.get("drives", []):
            if not isinstance(entry, dict):
                continue
            for slot, info in entry.items():
                if not isinstance(info, dict):
                    continue
                known = ("enabled", "bus_id", "type", "rom", "image_file", "image_path")
                found[slot] = DriveInfo(
                    slot=slot,
                    enabled=bool(info.get("enabled", False)),
                    bus_id=info.get("bus_id"),
                    type=str(info.get("type", "")),
                    rom=str(info.get("rom", "")),
                    image_file=str(info.get("image_file", "")),
                    image_path=str(info.get("image_path", "")),
                    extra={k: v for k, v in info.items() if k not in known})
        return found

    def get(self, slot: str) -> DriveInfo:
        found = self.list()
        if self._slot(slot) not in found:
            raise Failure(f"drive {slot!r} is missing from the drives listing")
        return found[slot]

    def _act(self, slot: str, action: str, params: Optional[Dict[str, object]] = None) -> None:
        path = f"/v1/drives/{self._slot(slot)}:{action}"
        code, _, body = self._rest.request("PUT", path, params=params)
        if code != 200:
            raise Failure(f"{path} returned HTTP {code}: {body[:160]!r}")

    def mount(self, slot: str, image: str, type: Optional[str] = None,
              mode: Optional[str] = None) -> None:
        params: Dict[str, object] = {"image": image}
        if type is not None:
            params["type"] = type
        if mode is not None:
            params["mode"] = mode
        self._act(slot, "mount", params)

    def reset(self, slot: str) -> None:
        self._act(slot, "reset")

    def remove(self, slot: str) -> None:
        self._act(slot, "remove")

    def on(self, slot: str) -> None:
        self._act(slot, "on")

    def off(self, slot: str) -> None:
        self._act(slot, "off")

    def unlink(self, slot: str) -> None:
        self._act(slot, "unlink")

    def load_rom(self, slot: str, file: str) -> None:
        self._act(slot, "load_rom", {"file": file})

    def set_mode(self, slot: str, mode: str) -> None:
        self._act(slot, "set_mode", {"mode": mode})


class ConfigsApi:
    """/v1/configs - the device's settings tree."""

    def __init__(self, rest: RestClient) -> None:
        self._rest = rest

    def categories(self) -> Dict[str, object]:
        return _errors(self._rest.json("/v1/configs"), "configs")

    def category(self, category: str) -> Dict[str, object]:
        payload = _errors(self._rest.json(f"/v1/configs/{_quote(category)}"),
                          f"configs/{category}")
        value = payload.get(category)
        if not isinstance(value, dict):
            raise Failure(f"configs/{category}: category missing from the answer")
        return value

    def get(self, category: str, item: str) -> object:
        """One item's value, out of the whole category in one request.

        The two config endpoints answer in different shapes, which is worth
        knowing before choosing between them. The category listing maps each
        item straight to its current value, so this returns that value. The
        per-item endpoint, `item()` below, returns an object carrying
        `current`, `values` and `default`.
        """
        return self.category(category).get(item)

    def item(self, category: str, item: str) -> Dict[str, object]:
        """One item's full description: its current value, range and default."""
        path = f"/v1/configs/{_quote(category)}/{_quote(item)}"
        payload = _errors(self._rest.json(path), f"configs/{category}/{item}")
        entry = payload.get(category)
        if isinstance(entry, dict):
            entry = entry.get(item)
        if not isinstance(entry, dict):
            raise Failure(f"configs/{category}/{item}: item missing from the answer")
        return entry

    def find_padded_enum(self) -> Optional[Tuple[str, str]]:
        """A store and enum item this machine serves whose labels are padded.

        The three CFG suites all need one setting of the same shape: an enum
        whose labels are right aligned, so one of its values carries leading
        padding and another does not, and both have a space inside the label.
        That is what makes a hand-edited .cfg a real case rather than a
        contrived one, because nobody types "Vol Master= 0 dB" with the
        leading space.

        The volume ladders have that shape on every machine, but the store
        holding them does not have the same name everywhere. Measured with GET
        /v1/configs: an Ultimate 64 serves "Audio Mixer", while an Ultimate
        II+L serves "Audio Output Settings" and has no Audio Mixer category at
        all. Asking the machine which of its stores has such an item is what
        lets one suite run on either, and costs one request per category plus
        one for the first item that looks right.

        Returns None when no store serves one, which is a reason for a suite
        to skip rather than to fail.
        """
        names = self.categories().get("categories")
        if not isinstance(names, list):
            raise Failure(f"configs: no category list in the answer: {names!r}")
        for category in names:
            if not isinstance(category, str):
                continue
            for item, value in self.category(category).items():
                # The category listing carries every item's current value, so
                # the candidates can be spotted without a request each. An item
                # whose current value is padded is an enum with padded labels.
                if not _padded_label(value):
                    continue
                values = self.item(category, item).get("values")
                if not isinstance(values, list):
                    continue
                if (any(_padded_label(v) for v in values)
                        and any(_plain_label(v) for v in values)):
                    return category, item
        return None

    def current(self, category: str, item: str) -> str:
        """The item's current value, or "" when the device did not report one.

        Returning "" rather than raising for a missing or non-string value is
        what a caller restoring a setting needs: it has to tell "the device did
        not say" from a real value without deciding whether that is a failure.
        """
        value = self.item(category, item).get("current")
        return value if isinstance(value, str) else ""

    def set(self, category: str, item: str, value: object) -> None:
        path = f"/v1/configs/{_quote(category)}/{_quote(item)}"
        code, _, body = self._rest.request("PUT", path, params={"value": value})
        if code != 200:
            raise Failure(f"{path}={value!r} returned HTTP {code}: {body[:160]!r}")

    def _flash(self, action: str, category: Optional[str]) -> None:
        path = (f"/v1/configs:{action}" if category is None
                else f"/v1/configs/{_quote(category)}:{action}")
        code, _, body = self._rest.request("PUT", path)
        if code != 200:
            raise Failure(f"{path} returned HTTP {code}: {body[:160]!r}")

    def save_to_flash(self, category: Optional[str] = None) -> None:
        self._flash("save_to_flash", category)

    def load_from_flash(self, category: Optional[str] = None) -> None:
        self._flash("load_from_flash", category)

    def reset_to_default(self, category: Optional[str] = None) -> None:
        self._flash("reset_to_default", category)


class FilesApi:
    """/v1/files/<path>:* - file information and disk-image creation."""

    def __init__(self, rest: RestClient) -> None:
        self._rest = rest

    def info(self, path: str) -> Optional[FileInfo]:
        """File information, or None when the path does not exist (HTTP 404)."""
        code, _, body = self._rest.request("GET", f"/v1/files/{_quote_path(path)}:info")
        if code == 404:
            return None
        if code != 200:
            raise Failure(f"files/{path}:info returned HTTP {code}: {body[:160]!r}")
        payload = _errors(_json(body, f"files/{path}:info"), f"files/{path}:info")
        entry = payload.get("files")
        if not isinstance(entry, dict):
            raise Failure(f"files/{path}:info: no file object in the answer")
        known = ("path", "filename", "size", "extension")
        return FileInfo(
            path=str(entry.get("path", "")),
            filename=str(entry.get("filename", "")),
            size=int(entry.get("size", 0) or 0),
            extension=str(entry.get("extension", "")),
            extra={k: v for k, v in entry.items() if k not in known})

    def exists(self, path: str) -> bool:
        return self.info(path) is not None

    def _create(self, kind: str, path: str, params: Dict[str, object]) -> None:
        target = f"/v1/files/{_quote_path(path)}:create_{kind}"
        code, _, body = self._rest.request("PUT", target, params=params)
        if code != 200:
            raise Failure(f"{target} returned HTTP {code}: {body[:160]!r}")

    def create_d64(self, path: str, tracks: Optional[int] = None,
                   diskname: Optional[str] = None) -> None:
        params: Dict[str, object] = {}
        if tracks is not None:
            _in_range("tracks", tracks, D64_TRACKS)
            params["tracks"] = tracks
        if diskname is not None:
            params["diskname"] = diskname
        self._create("d64", path, params)

    def create_d71(self, path: str, diskname: Optional[str] = None) -> None:
        self._create("d71", path, {"diskname": diskname} if diskname else {})

    def create_d81(self, path: str, diskname: Optional[str] = None) -> None:
        self._create("d81", path, {"diskname": diskname} if diskname else {})

    def create_dnp(self, path: str, tracks: int, diskname: Optional[str] = None) -> None:
        _in_range("tracks", tracks, DNP_TRACKS)
        params: Dict[str, object] = {"tracks": tracks}
        if diskname is not None:
            params["diskname"] = diskname
        self._create("dnp", path, params)


class RunnersApi:
    """/v1/runners:* - hand a file to the machine and start it."""

    def __init__(self, rest: RestClient) -> None:
        self._rest = rest

    def _run(self, action: str, params: Dict[str, object]) -> None:
        path = f"/v1/runners:{action}"
        code, _, body = self._rest.request("PUT", path, params=params)
        if code != 200:
            raise Failure(f"{path} returned HTTP {code}: {body[:160]!r}")

    def load_prg(self, file: str) -> None:
        self._run("load_prg", {"file": file})

    def run_prg(self, file: str) -> None:
        self._run("run_prg", {"file": file})

    def run_crt(self, file: str) -> None:
        self._run("run_crt", {"file": file})

    def sidplay(self, file: str, songnr: Optional[int] = None) -> None:
        params: Dict[str, object] = {"file": file}
        if songnr is not None:
            params["songnr"] = songnr
        self._run("sidplay", params)

    def modplay(self, file: str) -> None:
        self._run("modplay", {"file": file})

    def upload(self, action: str, payload: bytes,
               params: Optional[Dict[str, object]] = None) -> Response:
        """POST a file body to a runner, e.g. `upload("run_prg", prg_bytes)`."""
        return self._rest.request(
            "POST", f"/v1/runners:{action}", params=params, body=payload,
            headers={"Content-Type": "application/octet-stream"})


class StreamsApi:
    """/v1/streams/<stream>:start|stop - the device's video and audio streams."""

    def __init__(self, rest: RestClient) -> None:
        self._rest = rest

    def start(self, stream: str, **params: object) -> None:
        path = f"/v1/streams/{_quote(stream)}:start"
        code, _, body = self._rest.request("PUT", path, params=params or None)
        if code != 200:
            raise Failure(f"{path} returned HTTP {code}: {body[:160]!r}")

    def stop(self, stream: str) -> None:
        path = f"/v1/streams/{_quote(stream)}:stop"
        code, _, body = self._rest.request("PUT", path)
        if code != 200:
            raise Failure(f"{path} returned HTTP {code}: {body[:160]!r}")


class UltimateApi:
    """One device's REST API.

    `rest` is the underlying transport, for checks about the HTTP contract
    itself rather than about what the device did.
    """

    def __init__(self, host, password: Optional[str] = None,
                 timeout: float = DEFAULT_TIMEOUT) -> None:
        # `host` is a target token or a resolved handle; see tests/lib/targets.py.
        self.rest = RestClient(host, password, timeout)
        self.machine = MachineApi(self.rest)
        self.drives = DrivesApi(self.rest)
        self.configs = ConfigsApi(self.rest)
        self.files = FilesApi(self.rest)
        self.runners = RunnersApi(self.rest)
        self.streams = StreamsApi(self.rest)

    @property
    def host(self) -> str:
        return self.rest.host

    def version(self) -> str:
        return str(_errors(self.rest.json("/v1/version"), "version").get("version", ""))

    def info(self) -> Info:
        payload = _errors(self.rest.json("/v1/info"), "info")
        known = ("product", "firmware_version", "fpga_version", "hostname", "errors")
        return Info(
            product=str(payload.get("product", "")),
            firmware_version=str(payload.get("firmware_version", "")),
            fpga_version=str(payload.get("fpga_version", "")),
            hostname=str(payload.get("hostname", "")),
            extra={k: v for k, v in payload.items() if k not in known})

    def reachable(self) -> bool:
        try:
            return bool(self.version())
        except Failure:
            return False


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _json(body: bytes, what: str) -> object:
    try:
        return json.loads(body.decode("utf-8", "replace"))
    except ValueError as exc:
        raise Failure(f"{what}: unparsable JSON: {exc}") from exc


def _quote(value: str) -> str:
    return urllib.parse.quote(value, safe="")


def _quote_path(value: str) -> str:
    # A file path keeps its separators; only the segments are escaped.
    return urllib.parse.quote(value.lstrip("/"), safe="/")


def _in_range(name: str, value: int, bounds: Tuple[int, int]) -> None:
    low, high = bounds
    if not low <= value <= high:
        raise Failure(f"{name} {value} is outside {low}..{high}")
