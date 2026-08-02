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

from dataclasses import dataclass, field
from typing import Dict, Optional, Sequence, Tuple

from report import Failure
from rest import DEFAULT_TIMEOUT, RestClient, Response

DRIVE_SLOTS = ("a", "b")
MAX_ADDRESS = 0xFFFF
MAX_READ_LENGTH = 65536
MAX_INPUT_EVENTS = 64
MAX_KEYBOARD_INPUTS = 8
D64_TRACKS = (35, 41)
DNP_TRACKS = (1, 255)
INPUT_TRANSITIONS = ("press", "release", "tap")


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


# ---------------------------------------------------------------------------
# Endpoint groups
# ---------------------------------------------------------------------------


class MachineApi:
    """/v1/machine:* - the C64 itself, its memory and its on-device UI."""

    def __init__(self, rest: RestClient) -> None:
        self._rest = rest

    def _act(self, action: str) -> None:
        code, _, body = self._rest.request("PUT", f"/v1/machine:{action}")
        if code != 200:
            raise Failure(f"machine:{action} returned HTTP {code}: {body[:160]!r}")

    def reset(self) -> None:
        self._act("reset")

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

        `idempotent` opts into the transport retry, for a write that applies
        the same state however many times it arrives. See rest.RestClient.
        """
        code, _, body = self._rest.request(
            "PUT", "/v1/machine:writemem", idempotent=idempotent,
            params={"address": _hex_address(address), "data": data.hex()})
        if code != 200:
            raise Failure(f"writemem ${address:04X} returned HTTP {code}: {body[:160]!r}")

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
        return self.category(category).get(item)

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

    def __init__(self, host: str, password: Optional[str] = None,
                 timeout: float = DEFAULT_TIMEOUT) -> None:
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
    import json
    try:
        return json.loads(body.decode("utf-8", "replace"))
    except ValueError as exc:
        raise Failure(f"{what}: unparsable JSON: {exc}") from exc


def _quote(value: str) -> str:
    import urllib.parse
    return urllib.parse.quote(value, safe="")


def _quote_path(value: str) -> str:
    # A file path keeps its separators; only the segments are escaped.
    import urllib.parse
    return urllib.parse.quote(value.lstrip("/"), safe="/")


def _in_range(name: str, value: int, bounds: Tuple[int, int]) -> None:
    low, high = bounds
    if not low <= value <= high:
        raise Failure(f"{name} {value} is outside {low}..{high}")
