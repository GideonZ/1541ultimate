#!/usr/bin/env python3
# E2E: what an FTP upload stores on USB media is what the client sent.

"""Check that an upload survives a client that writes in segment-sized pieces.

GideonZ/1541ultimate#803: a file uploaded by FTP onto USB storage comes back
with individual 32-bit words altered, at exactly the right size and with no
error reported by either side. Two bytes in every 1024 are replaced along the
affected stretch, always the last word of the first sector of a 1024-byte pair.

What decides whether it happens is the size of the writes the *client* makes,
not the speed of the link:

- 1350 bytes at a time leaves `recv(socket, buffer, 1024)` returning 1024 and
  then the 326-byte remainder, so the file pointer goes out of sector alignment
  and `f_write` hands `disk_write` a pointer part-way into the FTP buffer. That
  pointer reaches the USB DMA unchecked, because the alignment guard live in
  `UsbBase::bulk_in` is commented out in `bulk_out`.
- 700 bytes at a time never leaves a whole sector over after topping up
  `fp->buf`, so that pointer is never formed.

Both shapes are pure client behaviour, so this needs no traffic shaping and no
impaired link. It does need real USB media in the machine, and it writes one
64 KiB file there, which is why it is manual.

The payload is self-describing: the 32-bit word at offset `o` holds
`(TAG << 24) | (o >> 2)`, so a word that comes back wrong reports the offset it
was meant to carry, and a word that arrived from elsewhere in the file names
where it came from.

    ./run-tests --suite ftp-usb-integrity --manual c64u
"""

import argparse
import ftplib
import os
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))

import ftp as ftp_lib
from report import (
    Failure, check, check_count, check_skip, check_start, detail,
    format_exception, section, suite_fail, suite_ok)

SUITE = "ftp_usb_integrity_test"

# One name per shape, and every upload deletes first. Measured, not assumed:
# overwriting a file of the same size reuses the clusters it already owns, and
# the defect does not appear. A newly created file reproduces it every time, so
# a suite that let one check overwrite another's file would report a false pass.
NAME = "ftp_usb_integrity_{chunk}.bin"
SIZE = 64 * 1024
TAG = 0xA5

# The size of one write, and how long the client waits before the next. The
# delay is what lets the device drain its socket, so that `recv` sees exactly
# one write's worth rather than a full buffer; 20 ms is comfortably longer than
# a LAN round trip. Three transfers of 64 KiB at this pacing is a handful of
# seconds on a LAN, so the checks are legitimately slower than the 10 s the
# runner paints SLOW -- the pacing is the experiment, not overhead to trim.
REGRESSION_CHUNK = 1350
CONTROL_CHUNK = 700
DELAY = 0.020

SECTOR = 512
PAIR = 1024


def payload() -> bytes:
    return struct.pack(f"<{SIZE // 4}I", *((TAG << 24) | w for w in range(SIZE // 4)))


def paced_store(client: ftplib.FTP, path: str, data: bytes, chunk: int,
                transfer_timeout: float) -> float:
    """STOR `data` in `chunk`-sized writes, pausing between them.

    Deliberately not ftp.store(): storbinary decides its own write size, and the
    write size is the whole point of this suite.

    The session timeout governs command replies and is measured in seconds; a
    paced upload of this size is a minute's work on a link that stalls, and the
    closing reply only arrives once the device has written everything. Both
    sockets are therefore given `transfer_timeout` for the duration and put back
    afterwards, so a slow link fails the check for the reason it deserves rather
    than for a timeout.
    """
    control_timeout = client.sock.gettimeout() if client.sock else None
    try:
        sock = client.transfercmd(f"STOR {path}")
    except ftplib.all_errors as exc:
        raise Failure(f"FTP store of {path} failed to start: {exc}") from exc
    started = time.monotonic()
    try:
        sock.settimeout(transfer_timeout)
        if client.sock:
            client.sock.settimeout(transfer_timeout)
        for offset in range(0, len(data), chunk):
            sock.sendall(data[offset:offset + chunk])
            time.sleep(DELAY)
        sock.close()
        try:
            client.voidresp()
        except ftplib.all_errors as exc:
            raise Failure(f"FTP store of {path} was refused: {exc}") from exc
    finally:
        try:
            sock.close()
        except OSError:
            pass
        if client.sock:
            client.sock.settimeout(control_timeout)
    return time.monotonic() - started


def differing_words(sent: bytes, back: bytes):
    """Every 32-bit word that changed, as (offset, expected, received)."""
    return [(o, struct.unpack("<I", sent[o:o + 4])[0], struct.unpack("<I", back[o:o + 4])[0])
            for o in range(0, len(sent), 4) if sent[o:o + 4] != back[o:o + 4]]


def describe(bad) -> str:
    """The signature of #803, so a failure says whether it is the known one."""
    at_pair_end = sum(1 for o, _, _ in bad if o % PAIR == SECTOR - 4)
    halves = sum(1 for _, exp, got in bad if (exp & 0xFFFF) == (got & 0xFFFF))
    return (f"{len(bad)} word(s) differ; {at_pair_end} at offset {SECTOR - 4} of a "
            f"{PAIR}-byte pair, {halves} with the lower halfword intact")


def path_for(directory: str, chunk: int) -> str:
    return f"{directory.rstrip('/')}/{NAME.format(chunk=chunk)}"


def round_trip(client: ftplib.FTP, directory: str, data: bytes, chunk: int,
               transfer_timeout: float) -> None:
    """Upload in `chunk`-sized writes, read back, and compare. Raises on a diff."""
    path = path_for(directory, chunk)
    # Fresh allocation on every attempt -- see the note beside NAME.
    ftp_lib.delete_quietly(client, path)
    elapsed = paced_store(client, path, data, chunk, transfer_timeout)
    control_timeout = client.sock.gettimeout() if client.sock else None
    try:
        if client.sock:
            client.sock.settimeout(transfer_timeout)
        back = ftp_lib.retrieve(client, path)
    finally:
        if client.sock:
            client.sock.settimeout(control_timeout)
    rate = len(data) / elapsed / 1024 if elapsed else 0.0
    detail(f"{chunk} B per write, {elapsed:.1f}s ({rate:.1f} KiB/s)")

    if len(back) != len(data):
        raise Failure(f"stored {len(data)} bytes, read back {len(back)}")
    bad = differing_words(data, back)
    if not bad:
        return
    detail(describe(bad))
    for offset, expected, got in bad[:8]:
        detail(f"0x{offset:06X} expected {expected:08X} got {got:08X}")
    if len(bad) > 8:
        detail(f"... and {len(bad) - 8} more")
    raise Failure(
        f"the file read back differs from the file sent in {len(bad)} word(s), "
        f"at the right size and with no error reported")


def usable(client: ftplib.FTP, directory: str) -> bool:
    try:
        client.cwd(directory)
    except ftplib.all_errors:
        return False
    return True


def scenario_upload_integrity(host: str, password: str, timeout: float,
                              usb_dir: str, ram_dir: str,
                              transfer_timeout: float) -> None:
    section("an upload to USB storage is stored as it was sent")
    data = payload()
    with ftp_lib.session(host, password, timeout) as client:
        if not usable(client, usb_dir):
            check_start(f"{usb_dir} is served")
            check_skip(f"no {usb_dir}; this suite needs USB media in the machine")
            return
        try:
            # The control first: a client that never leaves a whole sector over
            # exercises the same file, the same medium and the same link, so a
            # failure here is not #803 and says the suite itself is unsound.
            with check(f"a client writing {CONTROL_CHUNK} B at a time stores the file intact"):
                round_trip(client, usb_dir, data, CONTROL_CHUNK, transfer_timeout)

            # The discriminator before the regression, deliberately: the check
            # below is the one that fails today, and `check` re-raises, so an
            # answer collected afterwards would never reach the report. RAM disk
            # against USB is the layer split -- same daemon, same buffer, no USB
            # write path. It is not a perfect control, since /Temp is FAT12/16
            # and USB media is usually FAT32.
            if usable(client, ram_dir):
                with check(f"the same client shape stores the file intact on {ram_dir}"):
                    round_trip(client, ram_dir, data, REGRESSION_CHUNK, transfer_timeout)
                ftp_lib.delete_quietly(client, path_for(ram_dir, REGRESSION_CHUNK))
            else:
                check_start(f"the same client shape stores the file intact on {ram_dir}")
                check_skip(f"no {ram_dir} to compare against")

            with check(f"a client writing {REGRESSION_CHUNK} B at a time stores the file intact"):
                round_trip(client, usb_dir, data, REGRESSION_CHUNK, transfer_timeout)
        finally:
            for directory in (usb_dir, ram_dir):
                for chunk in (CONTROL_CHUNK, REGRESSION_CHUNK):
                    ftp_lib.delete_quietly(client, path_for(directory, chunk))


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Verify FTP uploads to USB storage are stored unaltered.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "10.0")))
    parser.add_argument("--usb-dir", default=os.environ.get("U64_USB_DIR", "/USB0"),
                        help="USB medium to write to (default: /USB0).")
    parser.add_argument("--ram-dir", default="/Temp",
                        help="RAM disk to compare against (default: /Temp).")
    parser.add_argument("--transfer-timeout", type=float,
                        default=float(os.environ.get("U64_FTP_TRANSFER_TIMEOUT", "120.0")),
                        help="How long one 64 KiB transfer may take (default: 120). "
                             "Separate from --timeout, which governs command replies.")
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    try:
        scenario_upload_integrity(args.host, args.password, args.timeout,
                                  args.usb_dir, args.ram_dir,
                                  args.transfer_timeout)
    except Failure as exc:
        suite_fail(SUITE, str(exc))
        return 1
    except Exception as exc:  # noqa: BLE001  (a lost device must not print a traceback alone)
        suite_fail(SUITE, format_exception(exc))
        return 1
    suite_ok(SUITE, f"{check_count()} checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
