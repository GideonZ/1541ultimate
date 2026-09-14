#!/usr/bin/env python3
"""E2E: read D81 track 81 and survive an oversized D81 on real hardware."""

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

SUITE = "d81_track_bounds_test"
SOURCE = SCRIPT_DIR / "d81_track_reader.asm"

BYTES_PER_TRACK = 10240
BYTES_PER_SIDE = 5120
BYTES_PER_SECTOR = 512
TRACK_81 = 80
SECTOR_5 = 4
MAX_TRACKS = 84
OVERSIZED_TRACKS = MAX_TRACKS + 1
SENTINEL = bytes((value ^ 0xa5) for value in range(256)) * 2

RESULT_STATUS = 0xc000
RESULT_BYTES = 4
RESULT_DATA = 0xc100
STATUS_DONE = 1
READY_MARK = 0xa5
POLL_SECONDS = 0.05
PROGRAM_TIMEOUT_SECONDS = 10.0
LIVENESS_TIMEOUT_SECONDS = 10.0

CASES = {
    "missing-track-81": (80, False),
    "track-81": (81, True),
    "oversized": (OVERSIZED_TRACKS, True),
}


def fixture(tracks: int, with_track_81: bool) -> bytes:
    image = bytearray(tracks * BYTES_PER_TRACK)
    if with_track_81:
        for side in range(2):
            offset = (TRACK_81 * BYTES_PER_TRACK + side * BYTES_PER_SIDE
                      + SECTOR_5 * BYTES_PER_SECTOR)
            image[offset:offset + BYTES_PER_SECTOR] = SENTINEL
    return bytes(image)


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

        self.api.drives.set_mode(self.slot, "1581")
        self.api.drives.on(self.slot)
        with ftp_lib.session(self.args.host, self.args.password or None) as client:
            for name, (tracks, expected) in CASES.items():
                path = f"/Temp/d81-e2e-{name}.d81"
                ftp_lib.store(client, path, fixture(tracks, expected))
                self.paths[name] = path
        return assemble(SOURCE, {"DEVICE": self.original.bus_id})

    def await_program(self) -> bytes:
        deadline = time.monotonic() + PROGRAM_TIMEOUT_SECONDS
        while True:
            result = self.api.machine.readmem(RESULT_STATUS, RESULT_BYTES)
            if result[1] == READY_MARK and result[0] != 0:
                return result
            if time.monotonic() >= deadline:
                raise Failure(f"track reader did not finish: result={result.hex(' ')}")
            time.sleep(POLL_SECONDS)

    def run_case(self, name: str, prg: bytes) -> None:
        tracks, expected = CASES[name]
        path = self.paths[name]
        section(name)

        with check(f"mount {tracks}-track D81 on drive {self.slot}"):
            self.api.drives.mount(self.slot, path, type="d81", mode="readonly")
            mounted = self.api.drives.get(self.slot)
            if posixpath.basename(path) not in mounted.image_file:
                raise Failure(f"drive reports {mounted.image_file!r}, expected {path!r}")

        with check("device remains reachable after mount"):
            reason = self.api.unreachable_reason(LIVENESS_TIMEOUT_SECONDS)
            if reason:
                raise Failure(reason)

        with check("physical track 81 has the expected result"):
            self.api.machine.writemem(RESULT_STATUS, bytes(RESULT_BYTES), idempotent=True)
            self.api.machine.writemem(RESULT_DATA, bytes(BYTES_PER_SECTOR), idempotent=True)
            status, _, body = self.api.runners.upload("run_prg", prg)
            if status != 200:
                raise Failure(f"run_prg returned HTTP {status}: {body[:160]!r}")
            result = self.await_program()
            data = self.api.machine.readmem(RESULT_DATA, BYTES_PER_SECTOR)
            detail(f"tracks={tracks}, job=${result[2]:02x}, io=${result[3]:02x}")
            if result[0] != STATUS_DONE:
                raise Failure(f"reader failed: result={result.hex(' ')}")
            if expected and data != SENTINEL:
                raise Failure("track 81 sector 5 did not return its sentinel")
            if not expected and data == SENTINEL:
                raise Failure("an 80-track image unexpectedly exposed track 81")

    def cleanup(self) -> None:
        try:
            if self.slot:
                self.api.drives.remove(self.slot)
        except Exception:
            pass
        try:
            with ftp_lib.session(self.args.host, self.args.password or None) as client:
                for path in self.paths.values():
                    ftp_lib.delete_quietly(client, path)
        except Exception:
            pass
        if self.original is not None:
            try:
                self.api.drives.set_mode(self.slot, self.original.type)
                image = mounted_path(self.original)
                if image:
                    self.api.drives.mount(self.slot, image)
                (self.api.drives.on if self.original.enabled else self.api.drives.off)(self.slot)
            except Exception as exc:
                detail(f"could not restore drive {self.slot}: {exc}")
        try:
            self.api.machine.reset(force=True)
        except Exception:
            pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser, timeout=15.0, colour=False)
    parser.add_argument("--test", choices=("all", *CASES), default="all")
    args = parser.parse_args()

    runner = SuiteRunner(args)
    try:
        with check("prepare 1581 and generated D81 fixtures"):
            prg = runner.prepare()
        selected = CASES if args.test == "all" else (args.test,)
        for name in selected:
            runner.run_case(name, prg)
    except Exception as exc:
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        runner.cleanup()
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
