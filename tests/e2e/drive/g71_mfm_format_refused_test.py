#!/usr/bin/env python3
"""E2E: an MFM format the image has no room for must not be recorded as done.

A 1571 formats an MFM track with WRITE TRACK. When the image cannot hold what
the drive wrote, `MfmDisk::UpdateTrack()` refuses the track, and the firmware
used to carry on regardless: it wrote the sectors at the file position the
previous command had left behind, and marked the track as formatted in the
image (#921).

The image is a G71 with GCR tracks 1 to 35, cylinder 35 as a 4000-byte track,
cylinder 36 as a full one and cylinder 37 as 100 bytes. Nine sectors of 512
bytes need 4608, so the 1571's burst format can put them on cylinder 36 but
not on cylinder 35, and cylinder 37 is shorter than the 162 bytes an MFM
track's metadata alone takes, so the image reserves no space for it at all.

  control   cylinder 36 is formatted with fill byte $E5: the drive reports OK,
            and the image file holds the nine sectors behind a header that
            says so. This is the format that has to keep working, and it shows
            that the image file reflects what the drive did.
  refused   cylinder 35 is formatted with fill byte $AA right after: its header
            in the image file has to stay as it was. Master marks it as an MFM
            track with nine sectors whose data was never stored.
  no room   cylinder 37 is formatted the same way. With no space reserved at
            all, it is refused like cylinder 35. The drive has to answer, the
            device has to stay reachable, and the track has to stay as it was.
            Master marks it as MFM and writes the refused fill byte into it.
  stale     master also writes the refused sectors at the file position the
            previous command left behind. That lands in the drive's copy of the
            image, not in the file, so a second image shows it through the
            drive: an MFM track on cylinder 0 with sector 1 filled with $11,
            and cylinder 35 as above. A program reads sector 1, formats
            cylinder 35 and reads sector 1 again; the second read has to find
            $11 too. Master finds the refused format's fill byte there.

Both formats are partial, one cylinder each, sent from the C64 over the slow
serial bus; only the command travels, so no burst transfer is needed. The
sector reads need none either: a burst READ that leaves out the data transfer
puts the sector into the drive's buffer, and M-R fetches it over the slow bus
(mfm_stale_write_probe.asm).
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
import g71_mfm_bounds_test as g71  # noqa: E402
from api import UltimateApi  # noqa: E402
from assembler import assemble  # noqa: E402
from report import (  # noqa: E402
    Failure,
    check,
    detail,
    format_exception,
    suite_fail,
    suite_ok,
)

SUITE = "g71_mfm_format_refused_test"
SOURCE = SCRIPT_DIR / "mfm_format_command.asm"
PROBE_SOURCE = SCRIPT_DIR / "mfm_stale_write_probe.asm"
IMAGE_PATH = "/Temp/g71-mfm-format-refused.g71"
STALE_IMAGE_PATH = "/Temp/g71-mfm-stale-write.g71"

# Cylinders count from 0, half-track indices in a G71 from 0 for track 1.
SHORT_CYLINDER = 35
FULL_CYLINDER = 36
TINY_CYLINDER = 37
SHORT_INDEX = SHORT_CYLINDER * 2
FULL_INDEX = FULL_CYLINDER * 2
TINY_INDEX = TINY_CYLINDER * 2
SHORT_TRACK_BYTES = 4000
# Shorter than the 162 bytes an MFM track's metadata alone takes, so the image
# reserves no space for it at all, and a format can be refused on arrival.
TINY_TRACK_BYTES = 100
UNFORMATTED = b"\x55"

CONTROL_FILL = 0xE5
REFUSED_FILL = 0xAA
SECTORS = 9
SECTOR_BYTES = 512

RESULT_STATUS = 0xc000
RESULT_BYTES = 4
RESULT_DATA = 0xc100

# The probe's results: status, ready mark, KERNAL I/O status, and the drive's
# answers to the inquire, the first read, the format and the second read as
# BCD bytes; then four bytes of sector 1 before and after the format, and the
# drive's answer to the format as text.
PROBE_RESULT_BYTES = 7
PROBE_SECTOR_BEFORE = 0xc100
PROBE_SECTOR_AFTER = 0xc104
PROBE_MESSAGE = 0xc110
PROBE_SECTOR_FILL = 0x11
MESSAGE_BYTES = 64
STATUS_DONE = 0x01
READY_MARK = 0xa5
PROGRAM_TIMEOUT_SECONDS = 90.0
POLL_SECONDS = 0.5
LIVENESS_TIMEOUT_SECONDS = 20.0


def image() -> bytes:
    tracks = {(track - 1) * 2: g71.gcr_track(track, g71.sector_payload)
              for track in range(1, 36)}
    tracks = {index: (len(data), data) for index, data in tracks.items()}
    tracks[SHORT_INDEX] = (SHORT_TRACK_BYTES, UNFORMATTED * SHORT_TRACK_BYTES)
    tracks[FULL_INDEX] = (g71.MAX_TRACK_SIZE, UNFORMATTED * g71.MAX_TRACK_SIZE)
    tracks[TINY_INDEX] = (TINY_TRACK_BYTES, UNFORMATTED * TINY_TRACK_BYTES)
    return g71.g71(tracks)


def stale_image() -> bytes:
    """Cylinder 0 as an MFM track whose sector 1 holds $11, cylinder 35 as above.

    Cylinder 0 has to be MFM, as the burst INQUIRE that puts the drive into MFM
    mode reads an ID there.
    """
    header = bytearray(g71.MFM_HEADER_SIZE)
    header[0] = 2
    header[2:7] = bytes((0, 0, 1, g71.MFM_SIZE_CODE, 0))
    header[7:12] = bytes((0, 0, 2, g71.MFM_SIZE_CODE, 0))
    data = (bytes((PROBE_SECTOR_FILL,)) * g71.MFM_SECTOR_BYTES
            + b"\x22" * g71.MFM_SECTOR_BYTES)
    track = bytes(header) + data
    track += UNFORMATTED * (g71.MAX_TRACK_SIZE - len(track))
    return g71.g71({0: (g71.MFM_MARKER | g71.MAX_TRACK_SIZE, track),
                    SHORT_INDEX: (SHORT_TRACK_BYTES, UNFORMATTED * SHORT_TRACK_BYTES)})


def track_at(data: bytes, index: int) -> tuple[int, bytes]:
    """The length word and the bytes behind it, of one half-track in a G71."""
    table = 12 + index * 4
    offset = int.from_bytes(data[table:table + 4], "little")
    word = int.from_bytes(data[offset:offset + 2], "little")
    return word, data[offset + 2:offset + 2 + (word & 0x7fff)]


class SuiteRunner:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.api = UltimateApi(args.host, args.password or None, args.timeout)
        self.slot = ""
        self.bus_id = 0
        self.drive_before = None
        self.original = image()

    def prepare(self) -> None:
        drives = self.api.drives.list()
        self.slot = "b" if "b" in drives else "a"
        if self.slot not in drives:
            raise Failure("the device exposes neither drive a nor drive b")
        if drives[self.slot].bus_id is None:
            raise Failure(f"drive {self.slot} has no IEC bus ID")
        self.bus_id = drives[self.slot].bus_id
        self.drive_before = drives[self.slot]
        self.api.drives.set_mode(self.slot, "1571")
        self.api.drives.on(self.slot)
        with ftp_lib.session(self.args.host, self.args.password or None) as client:
            ftp_lib.store(client, IMAGE_PATH, self.original)
        self.api.drives.mount(self.slot, IMAGE_PATH, type="g71", mode="readwrite")
        mounted = self.api.drives.get(self.slot)
        if posixpath.basename(IMAGE_PATH) not in mounted.image_file:
            raise Failure(f"drive reports {mounted.image_file!r}, expected {IMAGE_PATH!r}")

    def format(self, cylinder: int, fill: int) -> str:
        """Format one cylinder on side 0 and return the drive's answer."""
        prg = assemble(SOURCE, {"DEVICE": self.bus_id, "FIRST": cylinder,
                                "LAST": cylinder, "FILL": fill})
        self.api.machine.writemem(RESULT_STATUS, bytes(RESULT_BYTES), idempotent=True)
        self.api.machine.writemem(RESULT_DATA, bytes(MESSAGE_BYTES), idempotent=True)
        status, _, body = self.api.runners.upload("run_prg", prg)
        if status != 200:
            raise Failure(f"run_prg returned HTTP {status}: {body[:160]!r}")
        deadline = time.monotonic() + PROGRAM_TIMEOUT_SECONDS
        while True:
            result = self.api.machine.readmem(RESULT_STATUS, RESULT_BYTES)
            if result[1] == READY_MARK and result[0] != 0:
                break
            if time.monotonic() >= deadline:
                raise Failure(f"the format did not finish: result={result.hex(' ')}")
            time.sleep(POLL_SECONDS)
        if result[0] != STATUS_DONE:
            raise Failure(f"the format program failed on the bus: result={result.hex(' ')}")
        message = self.api.machine.readmem(RESULT_DATA, MESSAGE_BYTES).split(b"\r")[0]
        answer = message.decode("ascii", "replace")
        detail(f"cylinder {cylinder}: {answer}")
        return answer

    def probe(self) -> tuple[bytes, bytes, bytes, str]:
        """Read sector 1 of cylinder 0, format cylinder 35, read sector 1 again."""
        prg = assemble(PROBE_SOURCE, {"DEVICE": self.bus_id, "CYLINDER": SHORT_CYLINDER,
                                      "FILL": REFUSED_FILL})
        self.api.machine.writemem(RESULT_STATUS, bytes(PROBE_RESULT_BYTES), idempotent=True)
        self.api.machine.writemem(RESULT_DATA, bytes(MESSAGE_BYTES), idempotent=True)
        status, _, body = self.api.runners.upload("run_prg", prg)
        if status != 200:
            raise Failure(f"run_prg returned HTTP {status}: {body[:160]!r}")
        deadline = time.monotonic() + PROGRAM_TIMEOUT_SECONDS
        while True:
            result = self.api.machine.readmem(RESULT_STATUS, PROBE_RESULT_BYTES)
            if result[1] == READY_MARK and result[0] != 0:
                break
            if time.monotonic() >= deadline:
                raise Failure(f"the probe did not finish: result={result.hex(' ')}")
            time.sleep(POLL_SECONDS)
        if result[0] != STATUS_DONE:
            raise Failure(f"the probe failed on the bus: result={result.hex(' ')}")
        before = self.api.machine.readmem(PROBE_SECTOR_BEFORE, 4)
        after = self.api.machine.readmem(PROBE_SECTOR_AFTER, 4)
        message = self.api.machine.readmem(PROBE_MESSAGE, MESSAGE_BYTES).split(b"\r")[0]
        answer = message.decode("ascii", "replace")
        detail(f"cylinder {SHORT_CYLINDER}: {answer}")
        return result, before, after, answer

    def switch_to_stale_image(self) -> None:
        self.api.drives.unlink(self.slot)
        self.api.drives.remove(self.slot)
        with ftp_lib.session(self.args.host, self.args.password or None) as client:
            ftp_lib.store(client, STALE_IMAGE_PATH, stale_image())
        self.api.drives.mount(self.slot, STALE_IMAGE_PATH, type="g71", mode="readwrite")

    def image_now(self) -> bytes:
        with ftp_lib.session(self.args.host, self.args.password or None) as client:
            return ftp_lib.retrieve(client, IMAGE_PATH)

    def run(self) -> None:
        failures = []
        with check("the drive takes the image"):
            self.prepare()

        try:
            with check(f"[control] cylinder {FULL_CYLINDER} formats, and the image holds it"):
                answer = self.format(FULL_CYLINDER, CONTROL_FILL)
                if not answer.startswith("00,"):
                    raise Failure(f"the drive answered {answer!r}")
                word, data = track_at(self.image_now(), FULL_INDEX)
                if not word & g71.MFM_MARKER or data[0] != SECTORS:
                    raise Failure(f"the header says {word:#06x} with {data[0]} sectors")
                stored = data[g71.MFM_HEADER_SIZE:g71.MFM_HEADER_SIZE + SECTORS * SECTOR_BYTES]
                if stored != bytes((CONTROL_FILL,)) * (SECTORS * SECTOR_BYTES):
                    raise Failure("the sectors in the image are not the fill byte")
        except Failure as exc:
            failures.append(exc)

        try:
            with check(f"[wrong on master] cylinder {SHORT_CYLINDER}, too small for the "
                       f"format, stays as it was"):
                self.format(SHORT_CYLINDER, REFUSED_FILL)
                now = track_at(self.image_now(), SHORT_INDEX)
                before = track_at(self.original, SHORT_INDEX)
                if now != before:
                    word, data = now
                    raise Failure(f"its header now says {word:#06x}, {data[0]} sectors, "
                                  f"for a format the image could not hold")
        except Failure as exc:
            failures.append(exc)

        try:
            with check(f"[wrong on master] cylinder {TINY_CYLINDER}, with no room at all, "
                       f"is refused and the drive answers"):
                self.format(TINY_CYLINDER, REFUSED_FILL)
                reason = self.api.unreachable_reason(LIVENESS_TIMEOUT_SECONDS)
                if reason:
                    raise Failure(reason)
                word, data = track_at(self.image_now(), TINY_INDEX)
                if (word, data) != track_at(self.original, TINY_INDEX):
                    raise Failure(f"its track in the image changed: the length word is "
                                  f"{word:#06x}, the first bytes {data[:8].hex(' ')}")
        except Failure as exc:
            failures.append(exc)

        try:
            with check("[wrong on master] a refused format leaves the track the drive "
                       "read last as it was"):
                self.switch_to_stale_image()
                result, before, after, _ = self.probe()
                inquire, read1, _, read2 = result[3:7]
                if inquire != 0 or read1 != 0 or before != bytes((PROBE_SECTOR_FILL,)) * 4:
                    raise Failure(f"the first read of sector 1 did not work: inquire "
                                  f"{inquire:02x}, read {read1:02x}, data {before.hex(' ')}")
                if read2 != 0 or after != before:
                    raise Failure(f"sector 1 of cylinder 0 now reads {after.hex(' ')} "
                                  f"(answer {read2:02x}): the refused format was written "
                                  f"over it")
        except Failure as exc:
            failures.append(exc)

        if failures:
            raise failures[0]

    def cleanup(self) -> None:
        # The formats left the disk modified, and a modified disk is not removed
        # until it is saved. Unlinking it first gives up the changes, which
        # belong to this suite's own image.
        try:
            if self.slot:
                self.api.drives.unlink(self.slot)
                self.api.drives.remove(self.slot)
        except Exception:
            pass
        # The drive goes back to the model and power state it had.
        try:
            if self.drive_before is not None:
                self.api.drives.set_mode(self.slot, self.drive_before.type)
                if not self.drive_before.enabled:
                    self.api.drives.off(self.slot)
        except Exception:
            pass
        try:
            with ftp_lib.session(self.args.host, self.args.password or None) as client:
                ftp_lib.delete_quietly(client, IMAGE_PATH)
                ftp_lib.delete_quietly(client, STALE_IMAGE_PATH)
        except Exception:
            pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser, timeout=15.0, colour=False)
    args = parser.parse_args()
    runner = SuiteRunner(args)
    try:
        runner.run()
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
