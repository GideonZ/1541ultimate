#!/usr/bin/env python3
"""E2E: a G71 whose MFM-marked tracks are malformed must not take the drive
with it, and the GCR side of the same disk has to keep reading.

The fixtures are built here rather than shipped, because what is interesting
about them is exactly the two-byte length word in front of each track, and a
committed binary would hide it. Three images, one normal and two malformed in
the ways the bounds checks in `gcr_track_bounds.h` are about:

  mixed      a well-formed disk: GCR tracks, and one track carrying the MFM
             payload this firmware writes -- sector count, version byte, five
             bytes per sector, sector data from offset 162 on.
  shortmfm   the same disk, but the MFM-marked track declares one single byte.
             The metadata area alone is 162, so there is nothing to read; the
             mapping used to read it anyway.
  overlong   the same disk, but one track declares 0x4123. Read as fifteen
             bits that is 16675, above GCRIMAGE_MAXTRACKLEN, and the track has
             to be refused. Masked to fourteen it became 291 and was mounted.

Every case asserts the same two things: the device is still reachable, and
track 18 sector 0 still comes back through the drive's own U1 command. A
malformed track elsewhere on the disk may not cost the rest of it.
"""

import argparse
import posixpath
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402

import ftp as ftp_lib  # noqa: E402
from api import DriveInfo, UltimateApi  # noqa: E402
from assembler import assemble  # noqa: E402
from report import (  # noqa: E402
    Failure,
    check,
    detail,
    format_exception,
    section,
    suite_fail,
    suite_ok,
)

SUITE = "g71_mfm_bounds_test"
SOURCE = SCRIPT_DIR / "g71_block_reader.asm"

# ---------------------------------------------------------------- GCR encoding

# The 1541's 4-to-5 code. Its point is that no legal sequence has more than two
# zero bits in a row, which is what makes a run of zeros recognisable as an
# unformatted or weak area rather than as data.
GCR_NIBBLE = (
    0x0a, 0x0b, 0x12, 0x13, 0x0e, 0x0f, 0x16, 0x17,
    0x09, 0x19, 0x1a, 0x1b, 0x0d, 0x1d, 0x1e, 0x15,
)

SYNC = b"\xff" * 5
GAP = b"\x55"

# Sectors per track, and the nominal track length of each speed zone.
ZONES = ((31, 0, 17, 6250), (25, 1, 18, 6666), (18, 2, 19, 7142), (1, 3, 21, 7692))

DISK_ID = b"E2"


def zone_of(track: int) -> tuple:
    """Returns (speed zone, sectors, nominal length) for a 1541 track."""
    for first, zone, sectors, length in ZONES:
        if track >= first:
            return zone, sectors, length
    raise ValueError(track)


def gcr_encode(data: bytes) -> bytes:
    """Four bytes in, five out: eight nibbles of five bits each."""
    if len(data) % 4:
        raise ValueError("GCR encodes whole groups of four bytes")
    out = bytearray()
    for i in range(0, len(data), 4):
        bits = 0
        for byte in data[i:i + 4]:
            bits = (bits << 5) | GCR_NIBBLE[byte >> 4]
            bits = (bits << 5) | GCR_NIBBLE[byte & 0x0f]
        out += bits.to_bytes(5, "big")
    return bytes(out)


def sector_image(track: int, sector: int, payload: bytes) -> bytes:
    """One sector as it lies on the medium: header, gap, data, gap."""
    if len(payload) != 256:
        raise ValueError("a sector holds 256 bytes")
    id1, id2 = DISK_ID[0], DISK_ID[1]
    header = bytes((0x08, sector ^ track ^ id2 ^ id1, sector, track, id2, id1, 0x0f, 0x0f))
    checksum = 0
    for byte in payload:
        checksum ^= byte
    block = bytes((0x07,)) + payload + bytes((checksum, 0x00, 0x00))
    return (SYNC + gcr_encode(header) + GAP * 9
            + SYNC + gcr_encode(block) + GAP * 9)


def gcr_track(track: int, payload_of) -> bytes:
    """A whole GCR track, padded to its zone's nominal length."""
    _, sectors, length = zone_of(track)
    data = b"".join(sector_image(track, s, payload_of(track, s)) for s in range(sectors))
    if len(data) > length:
        raise ValueError(f"track {track} came out at {len(data)}, zone allows {length}")
    return data + GAP * (length - len(data))


# --------------------------------------------------------------- image layout

SIGNATURE = b"GCR-1571"
HALF_TRACKS = 168          # 84 per side, the 1571 in double-sided mode
FIRST_SIDE_1 = 84
MAX_TRACK_SIZE = 7928      # the usual value; the header is what actually binds
SLOT = MAX_TRACK_SIZE + 2  # length word plus the track it announces
TABLES = 12 + HALF_TRACKS * 4 * 2

DOS_TRACK = 18
SENTINEL_SECTOR = 1
SENTINEL = bytes((value ^ 0xa5) for value in range(256))

# Physical track 40 on side 0. Outside what the DOS ever touches, so a broken
# one cannot be confused with a broken directory.
MFM_HALF_TRACK = (40 - 1) * 2
OVERLONG_HALF_TRACK = (39 - 1) * 2

# The payload this firmware writes for an MFM track: a sector count, a version
# byte of zero, and five bytes for each of up to 32 sectors -- track, side,
# sector, size code, error byte. Sector data starts behind that fixed area,
# whatever the sector count is (c1541.cc, mfm_update_callback).
MFM_MAX_SECTORS = 32
MFM_HEADER_SIZE = 2 + MFM_MAX_SECTORS * 5
MFM_SECTORS = 10
MFM_SIZE_CODE = 2          # 1 << (7 + 2) = 512 bytes
MFM_SECTOR_BYTES = 1 << (7 + MFM_SIZE_CODE)

MFM_MARKER = 0x8000


def bam() -> bytes:
    """Track 18 sector 0. Only the fields the DOS reads to learn the disk."""
    block = bytearray(256)
    block[0:2] = bytes((DOS_TRACK, 1))
    block[2] = 0x41                       # 'A', DOS version
    for track in range(1, 36):
        _, sectors, _ = zone_of(track)
        entry = 4 + (track - 1) * 4
        block[entry] = 0 if track == DOS_TRACK else sectors
        block[entry + 1:entry + 4] = b"\xff\xff\x1f"
    block[0x90:0xa2] = b"\xa0" * 18
    block[0x90:0x99] = b"MFM BOUNDS"[:9]
    block[0xa2:0xa4] = DISK_ID
    block[0xa4] = 0xa0
    block[0xa5:0xa7] = b"2A"
    block[0xa7:0xab] = b"\xa0" * 4
    return bytes(block)


def sector_payload(track: int, sector: int) -> bytes:
    if track == DOS_TRACK and sector == 0:
        return bam()
    if track == DOS_TRACK and sector == SENTINEL_SECTOR:
        return SENTINEL
    return bytes((track, sector)) + bytes(254)


def mfm_track(sectors: int = MFM_SECTORS) -> bytes:
    """A well-formed MFM payload, laid out as the firmware lays it out."""
    header = bytearray(MFM_HEADER_SIZE)
    header[0] = sectors
    header[1] = 0                          # version
    for index in range(sectors):
        entry = 2 + index * 5
        header[entry:entry + 5] = bytes((40, 0, index + 1, MFM_SIZE_CODE, 0))
    data = bytearray()
    for index in range(sectors):
        data += bytes((index + 1,)) * MFM_SECTOR_BYTES
    return bytes(header) + bytes(data)


def g71(tracks: dict) -> bytes:
    """`tracks` maps a half-track index to (declared word, track bytes).

    Tracks that fit the header's maximum go into fixed slots behind the
    tables, the way a G64 is normally laid out. One that does not is appended
    behind them, which the format allows outright: "The location of the actual
    track or speed zone data is not important."
    """
    image = bytearray(TABLES)
    image[0:8] = SIGNATURE
    image[8] = 0                           # G64 version
    image[9] = HALF_TRACKS
    image[10:12] = MAX_TRACK_SIZE.to_bytes(2, "little")

    offsets = {}
    inline = sorted(index for index, (_, data) in tracks.items()
                    if len(data) <= MAX_TRACK_SIZE)
    for slot, index in enumerate(inline):
        offsets[index] = TABLES + slot * SLOT
    image += bytearray(len(inline) * SLOT)

    for index in sorted(tracks):
        if index not in offsets:
            offsets[index] = len(image)
            image += bytearray(2 + len(tracks[index][1]))

    for index, (declared, data) in tracks.items():
        at = offsets[index]
        image[at:at + 2] = declared.to_bytes(2, "little")
        image[at + 2:at + 2 + len(data)] = data

    speeds = 12 + HALF_TRACKS * 4
    for index in tracks:
        at = 12 + index * 4
        image[at:at + 4] = offsets[index].to_bytes(4, "little")
        track = index // 2 + 1
        zone = zone_of(track)[0] if index % 2 == 0 and track <= 35 else 0
        at = speeds + index * 4
        image[at:at + 4] = zone.to_bytes(4, "little")
    return bytes(image)


def fixtures() -> dict:
    """The three images, each built from the same GCR side 0."""
    gcr = {(track - 1) * 2: gcr_track(track, sector_payload) for track in range(1, 36)}
    common = {index: (len(data), data) for index, data in gcr.items()}

    payload = mfm_track()
    well_formed = dict(common)
    well_formed[MFM_HALF_TRACK] = (MFM_MARKER | len(payload), payload)

    short = dict(common)
    short[MFM_HALF_TRACK] = (MFM_MARKER | 1, b"\x00")

    overlong = dict(common)
    overlong[OVERLONG_HALF_TRACK] = (0x4123, bytes(0x4123 & 0x7fff))

    return {"mixed": well_formed, "shortmfm": short, "overlong": overlong}


CASES = ("mixed", "shortmfm", "overlong")

RESULT_STATUS = 0xc000
RESULT_BYTES = 4
RESULT_DATA = 0xc100
STATUS_DONE = 0x01
READY_MARK = 0xa5
PROGRAM_TIMEOUT_SECONDS = 60.0
POLL_SECONDS = 0.5
LIVENESS_TIMEOUT_SECONDS = 20.0


def mounted_path(drive: DriveInfo) -> str:
    if drive.image_file.startswith("/"):
        return drive.image_file
    return posixpath.join(drive.image_path, drive.image_file) if drive.image_file else ""


class SuiteRunner:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.api = UltimateApi(args.host, args.password or None, args.timeout)
        self.slot = ""
        self.original: DriveInfo | None = None
        self.paths: dict[str, str] = {}

    def prepare(self) -> bytes:
        drives = self.api.drives.list()
        self.slot = "b" if "b" in drives else "a"
        if self.slot not in drives:
            raise Failure("the device exposes neither drive a nor drive b")
        self.original = drives[self.slot]
        if self.original.bus_id is None:
            raise Failure(f"drive {self.slot} has no IEC bus ID")

        self.api.drives.set_mode(self.slot, "1571")
        self.api.drives.on(self.slot)
        images = fixtures()
        with ftp_lib.session(self.args.host, self.args.password or None) as client:
            for name in CASES:
                path = f"/Temp/g71-mfm-{name}.g71"
                ftp_lib.store(client, path, g71(images[name]))
                self.paths[name] = path
        return assemble(SOURCE, {"DEVICE": self.original.bus_id,
                                 "TRACK": DOS_TRACK, "SECTOR": SENTINEL_SECTOR})

    def await_program(self) -> bytes:
        deadline = time.monotonic() + PROGRAM_TIMEOUT_SECONDS
        while True:
            result = self.api.machine.readmem(RESULT_STATUS, RESULT_BYTES)
            if result[1] == READY_MARK and result[0] != 0:
                return result
            if time.monotonic() >= deadline:
                raise Failure(f"block reader did not finish: result={result.hex(' ')}")
            time.sleep(POLL_SECONDS)

    def run_case(self, name: str, prg: bytes) -> None:
        path = self.paths[name]
        section(name)

        with check(f"mount the {name} G71 on drive {self.slot}"):
            self.api.drives.mount(self.slot, path, type="g71", mode="readonly")
            mounted = self.api.drives.get(self.slot)
            if posixpath.basename(path) not in mounted.image_file:
                raise Failure(f"drive reports {mounted.image_file!r}, expected {path!r}")

        with check("device remains reachable after mount"):
            reason = self.api.unreachable_reason(LIVENESS_TIMEOUT_SECONDS)
            if reason:
                raise Failure(reason)

        with check(f"track {DOS_TRACK} sector {SENTINEL_SECTOR} still reads"):
            self.api.machine.writemem(RESULT_STATUS, bytes(RESULT_BYTES), idempotent=True)
            self.api.machine.writemem(RESULT_DATA, bytes(256), idempotent=True)
            status, _, body = self.api.runners.upload("run_prg", prg)
            if status != 200:
                raise Failure(f"run_prg returned HTTP {status}: {body[:160]!r}")
            result = self.await_program()
            data = self.api.machine.readmem(RESULT_DATA, 256)
            detail(f"dos=${result[2]:02x}, io=${result[3]:02x}")
            if result[0] != STATUS_DONE:
                raise Failure(f"reader failed: result={result.hex(' ')}")
            if data != SENTINEL:
                raise Failure(f"sentinel came back as {data[:8].hex(' ')}...")

    def cleanup(self) -> None:
        try:
            if self.slot:
                self.api.drives.remove(self.slot)
        except Exception:
            pass
        try:
            with ftp_lib.session(self.args.host, self.args.password or None) as client:
                for path in self.paths.values():
                    try:
                        ftp_lib.delete(client, path)
                    except Exception:
                        pass
        except Exception:
            pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser, timeout=15.0, colour=False)
    args = parser.parse_args()
    runner = SuiteRunner(args)
    try:
        prg = runner.prepare()
        for name in CASES:
            runner.run_case(name, prg)
    except Failure as failure:
        suite_fail(SUITE, str(failure))
        return 1
    except Exception as exc:  # noqa: BLE001 - the harness reports, it does not raise
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        runner.cleanup()
    return suite_ok(SUITE)


if __name__ == "__main__":
    sys.exit(main())
