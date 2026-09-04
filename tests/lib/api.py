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
import re
import time
import urllib.parse
from dataclasses import dataclass, field
from collections.abc import Sequence

import pacing
import targets
from report import Failure
import rest
from rest import DEFAULT_TIMEOUT, RestClient, Response, multipart_body

DRIVE_SLOTS = ("a", "b")
MAX_ADDRESS = 0xFFFF
MAX_READ_LENGTH = 65536
# One request may carry 64 events, which is the API's own limit
# (INPUT_API_MAX_EVENTS, software/api/input_api.h:10). A batch of exactly 64
# used to lose its last key with the on-device menu up: the ring those keys
# are pushed into keeps one slot empty to tell full from empty, so a 64-slot
# ring held 63 keys. The ring is now USB_INJECTED_BUFFER_SIZE = 65
# (keyboard_usb.h:21) and a full batch arrives complete. See
# tests/e2e/doc/key-injection-rate.md.
MAX_INPUT_EVENTS = 64
# The device caps the request body as well as the event count:
# INPUT_JSON_BODY_MAX_SIZE is 4096 bytes (software/api/route_input.cc:32) and a
# longer body is refused with HTTP 400 "JSON body is too large." The two limits
# are not interchangeable, because an event is not a fixed size: a tap of one
# short key name is about 55 bytes and a tap of "inst_del" is 62, so 64
# inst_del taps come to 4110 bytes and are refused although they are within the
# event limit. A batch therefore has to be measured, which is what
# input_batches below does.
MAX_INPUT_BODY_BYTES = 4096
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
    extra: dict[str, object] = field(default_factory=dict)


@dataclass(frozen=True)
class DriveInfo:
    slot: str
    enabled: bool
    bus_id: int | None
    type: str
    rom: str
    image_file: str
    image_path: str
    extra: dict[str, object] = field(default_factory=dict)

    @property
    def mounted(self) -> bool:
        return bool(self.image_file)


@dataclass(frozen=True)
class FileInfo:
    path: str
    filename: str
    size: int
    extension: str
    extra: dict[str, object] = field(default_factory=dict)


def _errors(payload: object, what: str) -> dict[str, object]:
    """Unwrap the device's `{..., "errors": [...]}` envelope."""
    if not isinstance(payload, dict):
        raise Failure(f"{what}: expected a JSON object, got {type(payload).__name__}")
    reported = payload.get("errors") or []
    if reported:
        raise Failure(f"{what} failed: {'; '.join(str(e) for e in reported)}")
    return payload


def input_body_bytes(events: Sequence[dict[str, object]]) -> int:
    """How large the request body for these events is, as the device sees it.

    The same serialisation the transport uses, so the number this returns is
    the number the device measures against INPUT_JSON_BODY_MAX_SIZE.
    """
    return len(json.dumps({"events": list(events)}).encode("utf-8"))


def input_batches(events: Sequence[dict[str, object]]) -> list[list[dict[str, object]]]:
    """Split events into batches `machine:input` accepts, one request each.

    Both device limits apply and neither implies the other: at most
    MAX_INPUT_EVENTS events, and at most MAX_INPUT_BODY_BYTES of body. Counting
    alone sent 64 backspaces as one request and was answered HTTP 400 "JSON
    body is too large", because those events are 62 bytes each; measuring alone
    would let a batch of short events past the event limit.

    A single event larger than the body limit cannot be split further and is
    returned as its own batch, so the device reports it rather than this
    silently dropping it.
    """
    batches: list[list[dict[str, object]]] = []
    current: list[dict[str, object]] = []
    for event in events:
        candidate = [*current, event]
        if current and (len(candidate) > MAX_INPUT_EVENTS
                        or input_body_bytes(candidate) > MAX_INPUT_BODY_BYTES):
            batches.append(current)
            current = [event]
        else:
            current = candidate
    if current:
        batches.append(current)
    return batches


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
        self._reset_at: tuple[int, int] | None = (
            self._counts()
            if os.environ.get("U64_DEVICE_RESET") == "1" else None)

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

    def _counts(self) -> tuple[int, int]:
        """What has moved the machine, seen from this client and the process.

        Two counters, because either can be the one that saw it. This client's
        own is what a caller injecting a transport gets, and it is what the
        runner tests drive. The process-wide one in rest.py catches the case
        the first cannot: a suite that mutates through one object and resets
        through another, where a per-client count would be zero and a reset
        that was needed would be skipped as a no-op.

        A reset is a no-op only when neither has moved.
        """
        return (getattr(self._rest, "mutations", 0), rest.mutation_count())

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
        if not force and self._reset_at == self._counts():
            return True
        # Blank the top of the screen first, so the READY left by the previous
        # boot cannot be mistaken for this one. Without it the wait returns
        # immediately and proves nothing.
        if wait:
            self.writemem(SCREEN_RAM, bytes([0x20]) * len(READY_SCREEN_CODES))
        self._act("reset")
        self._reset_at = self._counts()
        if not wait:
            return False
        ready = self.wait_until_ready(timeout)
        # The blanking write and the reset both counted as mutations, so the
        # bookkeeping is restored to "reset, and untouched since".
        self._reset_at = self._counts()
        return ready

    @property
    def was_just_reset(self) -> bool:
        """Whether this client reset the device and nothing has moved it since."""
        return self._reset_at is not None and self._reset_at == self._counts()

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

    def measure(self) -> bytes | None:
        """A bus timing capture, or None where the FPGA cannot measure (501)."""
        code, _, body = self._rest.request("GET", "/v1/machine:measure",
                                           idempotent=True)
        if code == 501:
            return None
        if code != 200:
            raise Failure(f"machine:measure returned HTTP {code}: {body[:160]!r}")
        return body

    def debugreg(self) -> str | None:
        """The debug register as two hex digits, or None where it does not exist.

        Both debugreg routes sit inside `#if U64`, so on other products they
        are not in the route table and the dispatcher answers 404. That is a
        product difference, not a failure, so it is None rather than a raise.
        """
        code, _, body = self._rest.request("GET", "/v1/machine:debugreg",
                                           idempotent=True)
        if code == 404:
            return None
        if code != 200:
            raise Failure(f"machine:debugreg returned HTTP {code}: {body[:160]!r}")
        payload = _errors(_json(body, "machine:debugreg"), "machine:debugreg")
        return str(payload.get("value", ""))

    def set_debugreg(self, value: str) -> str | None:
        """Set the debug register, returning what it reads back, or None where
        the route does not exist."""
        code, _, body = self._rest.request("PUT", "/v1/machine:debugreg",
                                           params={"value": value})
        if code == 404:
            return None
        if code != 200:
            raise Failure(f"machine:debugreg returned HTTP {code}: {body[:160]!r}")
        payload = _errors(_json(body, "machine:debugreg"), "machine:debugreg")
        return str(payload.get("value", ""))

    def heap(self) -> dict[str, int] | None:
        """Free, low-water and total FreeRTOS heap, or None on firmware without it.

        `free` is the figure to diff. `min_ever_free` is the low-water mark
        since boot and never recovers, so it says whether a run came close to
        running out but cannot tell a leak from a transient peak. `total` is
        configTOTAL_HEAP_SIZE and is constant.

        None means the endpoint answered 404, which is firmware predating it.
        A transport failure raises, because a caller deciding whether to skip
        needs those two apart: one is a device that cannot answer this, and the
        other is a device that is not answering.
        """
        code, _, body = self._rest.request("GET", "/v1/machine:heap")
        if code == 404:
            return None
        if code != 200:
            raise Failure(f"machine:heap returned HTTP {code}: {body[:160]!r}")
        payload = _errors(_json(body, "machine:heap"), "machine:heap")
        return {name: int(payload.get(name, 0))
                for name in ("free", "min_ever_free", "total")}

    def heap_free(self) -> int:
        """The free-heap figure the leak suites diff.

        Five soak suites each defined this one-liner over `heap()`. It raises
        rather than returning None on firmware without the endpoint, because a
        caller measuring a slope has nothing to measure without it; a caller
        deciding whether to skip asks `heap()` and reads the None.
        """
        reading = self.heap()
        if reading is None:
            raise Failure("machine:heap is not served by this firmware, so a "
                          "heap slope cannot be measured")
        return reading["free"]

    def menu_screen(self, timeout: float | None = None,
                    retries: int | None = None) -> bytes | None:
        """The rendered menu screen, or None when no menu is open (HTTP 404).

        `timeout` and `retries` are for a caller that has to bound how long
        the call can take; see rest.RestClient.request.
        """
        code, _, body = self._rest.request("GET", "/v1/machine:menu_screen",
                                           timeout=timeout, retries=retries)
        if code == 404:
            return None
        if code != 200:
            raise Failure(f"machine:menu_screen returned HTTP {code}: {body[:160]!r}")
        return body

    def menu_open(self) -> bool:
        return self.menu_screen() is not None

    def input_state(self) -> dict[str, object]:
        return _errors(self._rest.json("/v1/machine:input"), "machine:input")

    def send_input(self, events: Sequence[dict[str, object]]) -> None:
        if not 0 < len(events) <= MAX_INPUT_EVENTS:
            raise Failure(f"an input batch holds 1..{MAX_INPUT_EVENTS} events, got {len(events)}")
        body_bytes = input_body_bytes(events)
        if body_bytes > MAX_INPUT_BODY_BYTES:
            raise Failure(f"an input batch body holds at most {MAX_INPUT_BODY_BYTES} "
                          f"bytes, got {body_bytes} for {len(events)} events; "
                          f"split it with api.input_batches")
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

    def menu_rows(self) -> list[str]:
        """The open menu as 25 rows of text, or an empty list when it is closed.

        Only the character plane is decoded, and only its printable range;
        everything else becomes a space, so a search for a label finds it
        wherever the firmware drew it.
        """
        body = self.menu_screen()
        return [] if body is None else self.rows_of(body)

    @staticmethod
    def rows_of(body: bytes) -> list[str]:
        """The same decode, for a caller that already holds the payload.

        Separate so that reading the screen and reading its text is one
        request rather than two: the device serves about four concurrent HTTP
        connections, and a caller that wants both was paying twice.
        """
        chars = "".join(chr(c & 0x7F) if 0x20 <= (c & 0x7F) <= 0x7E else " "
                        for c in body[:SCREEN_CELLS])
        return [chars[r * SCREEN_COLS:(r + 1) * SCREEN_COLS] for r in range(SCREEN_ROWS)]

    def close_menu_from_anywhere(self, confirm_key: str | None = None) -> None:
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

    def list(self) -> dict[str, DriveInfo]:
        payload = _errors(self._rest.json("/v1/drives"), "drives")
        found: dict[str, DriveInfo] = {}
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

    def _act(self, slot: str, action: str, params: dict[str, object] | None = None) -> None:
        path = f"/v1/drives/{self._slot(slot)}:{action}"
        code, _, body = self._rest.request("PUT", path, params=params)
        if code != 200:
            raise Failure(f"{path} returned HTTP {code}: {body[:160]!r}")

    def mount(self, slot: str, image: str, type: str | None = None,
              mode: str | None = None) -> None:
        params: dict[str, object] = {"image": image}
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

    def categories(self) -> dict[str, object]:
        return _errors(self._rest.json("/v1/configs"), "configs")

    def category_names(self) -> list[str]:
        """Just the category names.

        `categories()` hands back the whole answer, whose keys are "categories"
        and "errors" rather than the names themselves, so iterating it gives
        neither and the mistake only shows when a request is built from it.
        """
        listed = self.categories().get("categories")
        if not isinstance(listed, list):
            raise Failure("configs: no category list in the answer")
        return [str(name) for name in listed]

    def category(self, category: str) -> dict[str, object]:
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

    def item(self, category: str, item: str) -> dict[str, object]:
        """One item's full description: its current value, range and default."""
        path = f"/v1/configs/{_quote(category)}/{_quote(item)}"
        payload = _errors(self._rest.json(path), f"configs/{category}/{item}")
        entry = payload.get(category)
        if isinstance(entry, dict):
            entry = entry.get(item)
        if not isinstance(entry, dict):
            raise Failure(f"configs/{category}/{item}: item missing from the answer")
        return entry

    def find_padded_enum(self) -> tuple[str, str] | None:
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
        for category in self.category_names():
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
        # idempotent: the call names both the item and the value it is to hold,
        # so a resend after a transport failure writes the same value again.
        # Without it a config write is not retried once the request has left
        # the client, which the suites that restore a captured setting rely on.
        path = f"/v1/configs/{_quote(category)}/{_quote(item)}"
        code, _, body = self._rest.request("PUT", path, params={"value": value},
                                           idempotent=True)
        if code != 200:
            raise Failure(f"{path}={value!r} returned HTTP {code}: {body[:160]!r}")

    def set_by_path(self, category: str, item: str, value: object) -> None:
        """Set an item with the value as the third path element.

        The route takes either `?value=` with a two-element path or a
        three-element path with no query argument (route_configs.cc). Both
        reach the same code; a suite that only ever used one would not notice
        the other breaking.
        """
        path = (f"/v1/configs/{_quote(category)}/{_quote(item)}"
                f"/{_quote(str(value))}")
        code, _, body = self._rest.request("PUT", path, idempotent=True)
        if code != 200:
            raise Failure(f"{path} returned HTTP {code}: {body[:160]!r}")

    def apply(self, settings: dict[str, dict[str, object]]) -> None:
        """Set several items at once, as the JSON body form of the route."""
        body = json.dumps(settings).encode("utf-8")
        code, _, answer = self._rest.request(
            "POST", "/v1/configs", body=body,
            headers={"Content-Type": "application/json"})
        if code != 200:
            raise Failure(f"POST /v1/configs returned HTTP {code}: {answer[:160]!r}")

    def _flash(self, action: str, category: str | None) -> None:
        path = (f"/v1/configs:{action}" if category is None
                else f"/v1/configs/{_quote(category)}:{action}")
        code, _, body = self._rest.request("PUT", path)
        if code != 200:
            raise Failure(f"{path} returned HTTP {code}: {body[:160]!r}")

    def save_to_flash(self, category: str | None = None) -> None:
        self._flash("save_to_flash", category)

    def load_from_flash(self, category: str | None = None) -> None:
        self._flash("load_from_flash", category)

    def reset_to_default(self, category: str | None = None) -> None:
        self._flash("reset_to_default", category)


class FilesApi:
    """/v1/files/<path>:* - file information and disk-image creation."""

    def __init__(self, rest: RestClient) -> None:
        self._rest = rest

    def info(self, path: str) -> FileInfo | None:
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

    def _create(self, kind: str, path: str, params: dict[str, object]) -> None:
        target = f"/v1/files/{_quote_path(path)}:create_{kind}"
        code, _, body = self._rest.request("PUT", target, params=params)
        if code != 200:
            raise Failure(f"{target} returned HTTP {code}: {body[:160]!r}")

    def create_d64(self, path: str, tracks: int | None = None,
                   diskname: str | None = None) -> None:
        params: dict[str, object] = {}
        if tracks is not None:
            _in_range("tracks", tracks, D64_TRACKS)
            params["tracks"] = tracks
        if diskname is not None:
            params["diskname"] = diskname
        self._create("d64", path, params)

    def create_d71(self, path: str, diskname: str | None = None) -> None:
        self._create("d71", path, {"diskname": diskname} if diskname else {})

    def create_d81(self, path: str, diskname: str | None = None) -> None:
        self._create("d81", path, {"diskname": diskname} if diskname else {})

    def create_dnp(self, path: str, tracks: int, diskname: str | None = None) -> None:
        _in_range("tracks", tracks, DNP_TRACKS)
        params: dict[str, object] = {"tracks": tracks}
        if diskname is not None:
            params["diskname"] = diskname
        self._create("dnp", path, params)


class RunnersApi:
    """/v1/runners:* - hand a file to the machine and start it."""

    def __init__(self, rest: RestClient) -> None:
        self._rest = rest

    def _run(self, action: str, params: dict[str, object]) -> None:
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

    def sidplay(self, file: str, songnr: int | None = None) -> None:
        params: dict[str, object] = {"file": file}
        if songnr is not None:
            params["songnr"] = songnr
        self._run("sidplay", params)

    def modplay(self, file: str) -> None:
        self._run("modplay", {"file": file})

    def upload(self, action: str, payload: bytes,
               params: dict[str, object] | None = None,
               timeout: float | None = None) -> Response:
        """POST a file body to a runner, e.g. `upload("run_prg", prg_bytes)`.

        `timeout` is worth setting for a large body. A megabytes-sized REU image
        is streamed into memory as it arrives, but the device answers only once
        all of it is written, so the client's ordinary per-request timeout can
        expire on a request that is progressing normally.
        """
        return self._rest.request(
            "POST", f"/v1/runners:{action}", params=params, body=payload,
            headers={"Content-Type": "application/octet-stream"}, timeout=timeout)


class StreamsApi:
    """/v1/streams/<stream>:start|stop - the device's video and audio streams."""

    def __init__(self, rest: RestClient) -> None:
        self._rest = rest

    def start(self, stream: str, timeout: float | None = None,
              retries: int | None = None, **params: object) -> None:
        path = f"/v1/streams/{_quote(stream)}:start"
        code, _, body = self._rest.request("PUT", path, params=params or None,
                                           timeout=timeout, retries=retries)
        if code != 200:
            raise Failure(f"{path} returned HTTP {code}: {body[:160]!r}")

    def stop(self, stream: str, timeout: float | None = None,
             retries: int | None = None) -> None:
        path = f"/v1/streams/{_quote(stream)}:stop"
        code, _, body = self._rest.request("PUT", path, timeout=timeout,
                                           retries=retries)
        if code != 200:
            raise Failure(f"{path} returned HTTP {code}: {body[:160]!r}")


class UltimateApi:
    """One device's REST API.

    `rest` is the underlying transport, for checks about the HTTP contract
    itself rather than about what the device did.
    """

    def __init__(self, host, password: str | None = None,
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

    @property
    def target(self):
        """The resolved handle this was built from, for a caller that needs
        more of a target than its device's host name: which machine has the
        VIC, where the streams are, which ports this device serves."""
        return self.rest.target

    def version(self) -> str:
        return str(_errors(self.rest.json("/v1/version"), "version").get("version", ""))

    def help(self, command: str) -> bytes:
        """The help page for one command. Answers HTML, not JSON."""
        code, _, body = self.rest.request("GET", "/v1/help",
                                          params={"command": command},
                                          idempotent=True)
        if code != 200:
            raise Failure(f"/v1/help returned HTTP {code}: {body[:160]!r}")
        return body

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

    def unreachable_reason(self, budget: float = 10.0,
                           poll: float = 1.0) -> str | None:
        """None when the device answers within `budget`, else why it did not.

        `reachable()` answers yes or no, which is all a gate needs. A suite
        asserting that the device survived the call it just made has to put the
        reason in the failure, or every lockup reads as the same blank timeout.
        This is that probe, in one place, so suites that check liveness after
        each step do not each grow their own poll loop.

        Catches Failure only, deliberately: rest.py raises it when no answer
        arrived, and a blanket except here would report a coding mistake in the
        probe as a dead device.
        """
        deadline = time.monotonic() + budget
        last = "no answer"
        while True:
            try:
                if self.version():
                    return None
                last = "version answered without a version string"
            except Failure as exc:
                last = str(exc)
            if time.monotonic() >= deadline:
                return last
            time.sleep(poll)


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


def _in_range(name: str, value: int, bounds: tuple[int, int]) -> None:
    low, high = bounds
    if not low <= value <= high:
        raise Failure(f"{name} {value} is outside {low}..{high}")


# What a cartridge target needs of the computer it is plugged into.
#
# The computer decides which of its resources the cartridge in its port owns
# (software/io/c64/c64.cc, CFG_C64_CART_PREF and C64::setCartPref). On "Auto"
# it hands the bus over only when it detects an external cartridge, and a
# Commodore 64 Ultimate with an Ultimate II+ in its port does not always detect
# one: the cartridge can then put bytes into the C64's memory while nothing it
# starts ever runs, which reads as the launch failing rather than as the bus
# never having been handed over. "External" forces the handover.
#
# The item lives in a store the U64-class builds compile in, which is what the
# computer of a cartridge target always is, so a store or item that is not
# there is worth reporting rather than passing over.
CARTRIDGE_STORE = "C64 and Cartridge Settings"
CARTRIDGE_PREFERENCE_ITEM = "Cartridge Preference"
CARTRIDGE_PREFERENCE_EXTERNAL = "External"


class CartridgePreferenceUnavailable(Failure):
    """The computer could not be asked for the setting. The message says why.

    Its own type because the two outcomes need different verdicts. A computer
    that serves the setting and refuses the value has to stop the run, since
    every launch after it fails for a reason that names neither. A computer
    that cannot be asked at all - a loopback stand-in with no config store, a
    machine that is not answering - is a reason to say so and carry on, because
    the suites that need it report their own failures.
    """


def ensure_cartridge_preference(target, password: str | None = None,
                                timeout: float = DEFAULT_TIMEOUT) -> str | None:
    """Make the computer of a cartridge target prefer the cartridge in its port.

    Answers what it did, for a caller that reports it:

      None            nothing to do - the target is its own computer, or the
                      computer already prefers the external cartridge
      a description   the value it changed, and from what

    Raises `CartridgePreferenceUnavailable` when the computer cannot be asked,
    and `Failure` when it serves the setting and will not take it.

    The change is not saved to flash. It takes effect through the item's own
    change hook, and leaving flash alone keeps a test run from deciding what a
    machine boots with.
    """
    handle = targets.resolve(target)
    if not handle.split:
        return None
    computer = UltimateApi(handle.computer, password, timeout)
    try:
        current = computer.configs.current(CARTRIDGE_STORE, CARTRIDGE_PREFERENCE_ITEM)
    except Failure as exc:
        # The URL is taken out of the reason rather than passed on. This
        # message reaches the run's records and from there the generated
        # report, where a link to a machine is an external reference the
        # document must not carry; the host is already named beside it.
        reason = re.sub(r"https?://\S+", "its config API", str(exc))
        raise CartridgePreferenceUnavailable(
            f"{handle.computer} did not answer for "
            f"'{CARTRIDGE_PREFERENCE_ITEM}': {reason}") from exc
    if current == CARTRIDGE_PREFERENCE_EXTERNAL:
        return None
    computer.configs.set(CARTRIDGE_STORE, CARTRIDGE_PREFERENCE_ITEM,
                         CARTRIDGE_PREFERENCE_EXTERNAL)
    now = computer.configs.current(CARTRIDGE_STORE, CARTRIDGE_PREFERENCE_ITEM)
    if now != CARTRIDGE_PREFERENCE_EXTERNAL:
        raise Failure(
            f"{handle.computer} kept '{CARTRIDGE_PREFERENCE_ITEM}' at {now!r} "
            f"after it was set to {CARTRIDGE_PREFERENCE_EXTERNAL!r}; the "
            f"cartridge in its port will not own the bus")
    return (f"{handle.computer}: {CARTRIDGE_PREFERENCE_ITEM} "
            f"{current!r} -> {CARTRIDGE_PREFERENCE_EXTERNAL!r}")


# The computer of a cartridge target has drives of its own, and they answer on
# the same IEC bus and the same bus IDs as the cartridge's. Measured on an
# Ultimate II+L in a C64 Ultimate with both machines' Drive A enabled on bus 8:
# every action that goes through the bus failed - Run, Load, Mount & Run, Real
# Run, and the printer suite's PRG, which timed out waiting for output - while
# every action that goes through DMA passed. Two devices answering as drive 8
# is not a defect in either of them.
DRIVE_STORES = ("Drive A Settings", "Drive B Settings")
DRIVE_ENABLE_ITEM = "Drive"
DRIVE_DISABLED = "Disabled"


# Every machine in this tree carries "Fast Reset" in the C64 store: the item is
# outside the `#if U64` guard in software/io/c64/c64.cc. It defaults to
# Disabled, which makes each reset run the KERNAL's full RAM test. A run resets
# the machine before most suites, so that test is paid for over and over.
FAST_RESET_ITEM = "Fast Reset"
FAST_RESET_ENABLED = "Enabled"


def ensure_fast_reset(target, password: str | None = None,
                      timeout: float = DEFAULT_TIMEOUT) -> str | None:
    """Make every machine this target occupies skip the RAM test on reset.

    Answers what it changed, or None when there was nothing to change. A
    machine that does not serve the item is passed over rather than reported,
    for the same reason a computer without drives has nothing to silence.

    Both halves of a cartridge target are set, because either half can reset
    the C64 and the setting belongs to whichever one does.

    The caller captures the settings before calling this, so what the run found
    is what it puts back, and that capture is the only thing this relies on.

    Do not read "a REST write does not reach flash" into that. The write marks
    the store flash-stale, and `ConfigBrowser::on_exit` in
    software/userinterface/config_menu.cc writes every stale store to flash
    when the config browser is left, unless 'Auto Save Config' is No. Several
    suites in a run open and leave that browser, and the C64 Ultimate on this
    bench has the setting on Yes, so a value this helper changes can be in
    flash by the time the run ends. A run killed between here and the restore
    can therefore leave the machine with Fast Reset enabled for good.
    """
    handle = targets.resolve(target)
    changed = []
    for host in handle.resources:
        machine = UltimateApi(host, password, timeout)
        try:
            current = machine.configs.current(CARTRIDGE_STORE, FAST_RESET_ITEM)
        except Failure:
            continue
        if current == FAST_RESET_ENABLED:
            continue
        machine.configs.set(CARTRIDGE_STORE, FAST_RESET_ITEM, FAST_RESET_ENABLED)
        now = machine.configs.current(CARTRIDGE_STORE, FAST_RESET_ITEM)
        if now != FAST_RESET_ENABLED:
            raise Failure(
                f"{host} kept {CARTRIDGE_STORE}/{FAST_RESET_ITEM} at {now!r} "
                f"after it was set to {FAST_RESET_ENABLED!r}")
        changed.append(f"{host}: {current!r} -> {FAST_RESET_ENABLED!r}")
    return ", ".join(changed) if changed else None


def ensure_host_drives_off(target, password: str | None = None,
                           timeout: float = DEFAULT_TIMEOUT) -> str | None:
    """Silence the computer's own drives, so the cartridge owns the IEC bus.

    Answers what it did, or None when there was nothing to do: the target is
    its own computer, or the computer's drives are already off.

    Like the cartridge preference, the change is not saved to flash. A store
    the computer does not serve is passed over rather than reported, because a
    computer without drives is a computer with nothing to silence.
    """
    handle = targets.resolve(target)
    if not handle.split:
        return None
    computer = UltimateApi(handle.computer, password, timeout)
    silenced = []
    for store in DRIVE_STORES:
        try:
            current = computer.configs.current(store, DRIVE_ENABLE_ITEM)
        except Failure:
            continue
        if current == DRIVE_DISABLED:
            continue
        computer.configs.set(store, DRIVE_ENABLE_ITEM, DRIVE_DISABLED)
        now = computer.configs.current(store, DRIVE_ENABLE_ITEM)
        if now != DRIVE_DISABLED:
            raise Failure(
                f"{handle.computer} kept {store}/{DRIVE_ENABLE_ITEM} at "
                f"{now!r} after it was set to {DRIVE_DISABLED!r}; it will "
                f"answer on the IEC bus alongside the cartridge")
        silenced.append(store)
    if not silenced:
        return None
    return f"{handle.computer}: {', '.join(silenced)} -> {DRIVE_DISABLED!r}"
