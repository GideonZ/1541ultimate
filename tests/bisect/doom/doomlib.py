"""Shared helpers for the Doom C64U hardware bisection harness.

Only REST routes that exist from firmware 3.14 onward are used, so the same
code drives every build in the bisection range. In particular `machine:input`
is not required: it was added during the 3.15 cycle.
"""
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

import numpy as np

# The suites' shared HTTP policy, so this harness retries the way they do
# rather than growing its own client. See tests/lib/check_transport_usage.py.
_TESTS = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(_TESTS, "lib"))
sys.path.insert(0, os.path.join(_TESTS, "e2e", "lib"))
from rest import retrying_urlopen, url_for                          # noqa: E402
import streams                                                      # noqa: E402

# VICE-matched C64 palette, indexed by the nibble the video stream carries.
PALETTE = np.array([
    (0x00, 0x00, 0x00), (0xEF, 0xEF, 0xEF), (0x8D, 0x2F, 0x34), (0x6A, 0xD4, 0xCD),
    (0x98, 0x35, 0xA4), (0x4C, 0xB4, 0x42), (0x2C, 0x29, 0xB1), (0xEF, 0xEF, 0x5D),
    (0x98, 0x4E, 0x20), (0x5B, 0x38, 0x00), (0xD1, 0x67, 0x6D), (0x4A, 0x4A, 0x4A),
    (0x7B, 0x7B, 0x7B), (0x9F, 0xEF, 0x93), (0x6D, 0x6A, 0xEF), (0xB2, 0xB2, 0xB2),
], np.uint8)

# Engine state, from the Doom C64U sources (src/defs.asm).
FRAMECNT = 0x0F40      # 2 bytes, incremented once per rendered frame
MAPOK    = 0x0F47      # 1 = the REU image was found and verified at boot
MAPERR   = 0x0F48      # why not, when MAPOK is 0
CAMERA   = 0x0050      # camX (2B), camY (2B), camZ (2B), camA (1B), camSec (1B)

# launcher.prg polls $DC00/$DC01 for SPACE before chaining into the game. This
# is the `bne` back-edge of that loop; overwriting it with two NOPs falls
# through into chainToGame and needs nothing but machine:writemem.
SPACE_BNE = 0x0945
SPACE_BNE_OPCODE = b"\xd0\xf4"


def req(host, path, method="GET", data=None, ctype=None, timeout=90):
    """One REST call. Returns (status, body); status 0 means the call failed."""
    r = urllib.request.Request(url_for(host, path), data=data, method=method)
    if ctype:
        r.add_header("Content-Type", ctype)
    try:
        with retrying_urlopen(r, timeout, idempotent=(method == "GET")) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()
    except Exception as e:                      # noqa: BLE001 - transport errors
        return 0, str(e).encode()


def readmem(host, addr, length):
    """The bytes at `addr`, or None when the device did not answer with them.

    A caller that treats an error body as memory reports a transient REST
    failure as a defect in the bitstream under test, which is the one verdict a
    bisection must never produce.
    """
    status, body = req(host, f"/v1/machine:readmem?address={addr:04X}&length={length}")
    if status != 200 or len(body) != length:
        return None
    return body


def writemem(host, addr, hexdata):
    return req(host, f"/v1/machine:writemem?address={addr:04X}&data={hexdata}",
               "PUT", timeout=15)[0] == 200


def framecnt(host):
    """The engine's frame counter, or None if it could not be read."""
    b = readmem(host, FRAMECNT, 2)
    return (b[0] | (b[1] << 8)) if b else None


def camera(host):
    """Player camera. A picture that changed while this did not is corruption;
    a picture that changed after it moved is the player having moved, which a
    twitchy joystick on port 2 can cause with nobody touching it."""
    return readmem(host, CAMERA, 8)


def inject_input(host):
    """Real key and joystick input. Returns False on firmware without the API."""
    body = json.dumps({"events": [
        {"kind": "keyboard", "inputs": ["w"], "transition": "tap"},
        {"kind": "joystick", "port": 2, "inputs": ["left"], "transition": "tap"},
    ]}).encode()
    return req(host, "/v1/machine:input", "POST", body,
               "application/json", timeout=15)[0] == 200


class VideoStream:
    """The VIC video stream, held open so frames can be taken back to back.

    Frame assembly, the socket options and the wire format are
    tests/e2e/lib/streams.py's, and using anything else here is a mistake that
    has already been made once: assembling a frame by concatenating payloads in
    arrival order turns a reordered packet into a scrambled picture, and
    scrambled differently every time. That reads as random corruption and is
    indistinguishable from the defect this harness looks for.

    streams.FrameAssembler places each packet by its header offset and only
    returns complete frames, and packets from any other device on the shared
    multicast group are discarded: on a bench with more than one Ultimate they
    otherwise land in the middle of a frame.
    """

    def __init__(self, host, timeout=10.0):
        self.host = host
        self.addresses = streams.source_addresses(host)
        status, body = req(host, f"/v1/streams/video:start?ip={streams.VIDEO_GROUP}",
                           "PUT", timeout=15)
        if status != 200:
            raise RuntimeError(f"the device refused to start its video stream: "
                               f"HTTP {status} {body[:120]!r}")
        time.sleep(0.5)
        try:
            self.sock = streams.stream_socket(streams.VIDEO_GROUP, streams.VIDEO_PORT,
                                              timeout=timeout)
        except Exception:
            req(host, "/v1/streams/video:stop", "PUT", timeout=15)
            raise
        self.assembler = streams.FrameAssembler()
        self.timeout = timeout

    def close(self):
        try:
            self.sock.close()
        finally:
            req(self.host, "/v1/streams/video:stop", "PUT", timeout=15)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def frame(self, expect_lines=None):
        """The next complete frame, as an array of one colour index per pixel.

        `expect_lines`, when given, skips frames of another height rather than
        returning one the caller cannot compare against its reference.
        """
        for _sock, data, mine in streams.receive([self.sock], self.addresses,
                                                 self.timeout):
            if not mine:
                continue
            # push() returns a frame only once every packet of it has arrived,
            # so a frame that lost one is discarded inside the assembler and is
            # never compared. counts() is the loss accounting.
            frame = self.assembler.push(data)
            if frame is None:
                continue
            if expect_lines is not None and frame.height != expect_lines:
                continue
            pixels = np.frombuffer(streams.unpack(frame.packed), np.uint8)
            return pixels.reshape(frame.height, frame.width)
        raise RuntimeError(f"no complete VIC frame within {self.timeout}s")


def save_png(indices, path):
    from PIL import Image
    Image.fromarray(PALETTE[indices]).save(path)


def load_reu(host, remote_path, attempts=20, delay=3.0):
    """Load a .reu file straight into REU memory.

    runners:modplay is the only REST route that does this; it is the same
    operation the file browser's "Load into REU" performs. It then starts the
    MOD player, whose sampler channels keep reading REU in the FPGA, so the
    caller must silence them before running the game.

    The retries cover USB re-enumeration after a JTAG FPGA reconfigure, during
    which the path does not exist yet.
    """
    path = "/v1/runners:modplay?file=" + urllib.parse.quote(remote_path)
    status, body, attempt = 0, b"", 0
    for attempt in range(1, attempts + 1):
        status, body = req(host, path, "PUT", timeout=240)
        if status == 200:
            return attempt
        time.sleep(delay)
    raise RuntimeError(f"REU image never loaded: HTTP {status} {body[:120]!r}")


SAMPLER_BASE = 0xDF20
SAMPLER_END = 0xE000            # exclusive: $DF20-$DFFF is 224 bytes


def silence_sampler(host):
    """Gate off every Ultimate Audio channel at $DF20-$DFFF, so the MOD player
    started by load_reu() stops competing with the engine for REU bandwidth.

    The last chunk is clamped: Doom runs with RAM under the KERNAL, so writing a
    full 128 bytes from $DFA0 would zero $E000-$E01F of engine memory and the
    damage would be blamed on the bitstream under test.
    """
    for base in range(SAMPLER_BASE, SAMPLER_END, 128):
        writemem(host, base, "00" * min(128, SAMPLER_END - base))


def run_prg(host, prg_path):
    with open(prg_path, "rb") as fh:
        prg = fh.read()
    status, body = req(host, "/v1/runners:run_prg", "POST", prg,
                       "application/octet-stream", timeout=60)
    if status != 200:
        raise RuntimeError(f"run_prg failed: HTTP {status} {body[:120]!r}")


def wait_for_title(host, deadline=45.0):
    """Block until launcher.prg is loaded and sitting in its SPACE loop."""
    end = time.monotonic() + deadline
    while time.monotonic() < end:
        if readmem(host, SPACE_BNE, 2) == SPACE_BNE_OPCODE:
            return True
        time.sleep(0.5)
    return False


def skip_title(host, deadline=45.0):
    """Leave the launcher's SPACE wait without a keyboard.

    The loop is confirmed present before it is overwritten. Writing blind would
    put two NOPs over whatever the launcher had reached instead, and the run
    would then be reported broken by the harness that broke it.
    """
    if not wait_for_title(host, deadline):
        return False, None, None
    before = readmem(host, SPACE_BNE, 2)
    writemem(host, SPACE_BNE, "EAEA")
    return True, before, readmem(host, SPACE_BNE, 2)
