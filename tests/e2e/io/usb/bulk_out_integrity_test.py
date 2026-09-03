#!/usr/bin/env python3
# E2E: what an FTP upload stores on USB media is what the client sent.

"""Check that an upload survives a client that writes in segment-sized pieces.

GideonZ/1541ultimate#803: a file uploaded by FTP onto USB storage comes back
with individual 32-bit words altered, at exactly the right size and with no
error reported by either side. Only the top byte of an affected word changes,
and the affected words sit at the last word of every 1024-byte pair of sectors.

What decides whether it happens is the shape of the writes the *client* makes,
not the speed of the link:

- 1024 bytes, then 2 bytes, then 1024 bytes at a time leaves the file offset at
  2 modulo 512 for every write that follows. The FTP server then hands
  `f_write` a source pointer part-way into its own buffer, `disk_write` passes
  it to the USB bulk-out transfer unchecked, and the transfer loses the top
  byte of its last word. The alignment guard that would have caught it is live
  in `UsbBase::bulk_in` and commented out in `bulk_out`.
- 700 bytes at a time never leaves a whole sector over after topping up
  `fp->buf`, so that pointer is never formed.

GideonZ/1541ultimate#821 carries a workaround for a bulk-out bounce condition
described as `(buffer alignment + length) mod 4 != 0`. That is not the same
geometry as the 512-byte-aligned two-sector `disk_write` reproduced here, so it
is not known whether #821 covers this case; anyone applying it can run this
suite to find out.

Both shapes are pure client behaviour, so this needs no traffic shaping and no
impaired link. It does need real USB media in the machine, and it writes one
64 KiB file there, which is why it is manual.

Measured, on the write shapes above:

- C64 Ultimate, firmware 1.2.0, fpga 122, core 1.4D: the paced shape corrupts
  59 words of 64 KiB on every run, the 700-byte control shape is clean, and the
  same paced shape is clean on the RAM disk.
- Ultimate II+L, firmware 3.15, fpga 123: clean.
- Ultimate 64 Elite, firmware 3.15, fpga 123, core 1.4F: clean.

The payload is self-describing: the 32-bit word at offset `o` holds
`(TAG << 24) | (o >> 2)`, so a word that comes back wrong reports the offset it
was meant to carry, and a word that arrived from elsewhere in the file names
where it came from.

    ./run-tests --suite usb-bulk-out-integrity --manual c64u
"""

import argparse
import ftplib
import os
import socket
import struct
import sys
import time
from pathlib import Path

# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402

import ftp as ftp_lib
from report import (
    Failure, check, check_count, check_skip, check_start, detail,
    format_exception, section, suite_fail, suite_ok)

SUITE = "bulk_out_integrity_test"

# One name per shape, and every upload deletes first. Measured, not assumed:
# overwriting a file of the same size reuses the clusters it already owns, and
# the defect does not appear. A newly created file reproduces it every time, so
# a suite that let one check overwrite another's file would report a false pass.
NAME = "usb_bulk_out_{shape}.bin"
SIZE = 64 * 1024
TAG = 0xA5

# The paced shape: one sector-sized write, then two bytes, then sector-sized
# writes to the end. The two-byte write is what puts every later write at 2
# modulo 512, which is the offset the defect needs.
HEAD = 1024
TAIL = 2
BODY = 1024
# The control shape, which tops up the server's buffer without ever leaving a
# whole sector over.
CONTROL = 700
# How long the client waits between writes. This is what lets the device drain
# its socket, so that one `recv` sees one write rather than a coalesced pair;
# 20 ms is comfortably longer than a LAN round trip. Three transfers of 64 KiB
# at this pacing is a handful of seconds on a LAN, so the checks are
# legitimately slower than the 10 s the runner paints SLOW -- the pacing is the
# experiment, not overhead to trim.
DELAY = 0.020

SECTOR = 512
PAIR = 1024


def payload() -> bytes:
    return struct.pack(f"<{SIZE // 4}I", *((TAG << 24) | w for w in range(SIZE // 4)))


def control_writes(data: bytes) -> list[bytes]:
    """Equal writes that never leave a whole sector over."""
    return [data[o:o + CONTROL] for o in range(0, len(data), CONTROL)]


def paced_writes(data: bytes) -> list[bytes]:
    """The shape that puts every write after the first two at 2 modulo 512."""
    writes = [data[:HEAD], data[HEAD:HEAD + TAIL]]
    writes += [data[o:o + BODY] for o in range(HEAD + TAIL, len(data), BODY)]
    return writes


SHAPES = {
    "control": (f"{CONTROL} B at a time", control_writes),
    "paced": (f"{HEAD} B, {TAIL} B, then {BODY} B at a time", paced_writes),
}


def paced_store(client: ftplib.FTP, path: str, writes: list[bytes],
                transfer_timeout: float) -> float:
    """STOR one write at a time, pausing between them.

    Deliberately not ftp.store(): storbinary decides its own write size, and the
    write size is the whole point of this suite. TCP_NODELAY is set for the same
    reason -- with Nagle in the way, two of the client's writes can leave as one
    segment, the device's `recv` sees them joined, and the file offset the
    defect needs is never formed. Measured on a C64 Ultimate at firmware 1.2.0:
    the same paced shape corrupts 2 words of 64 KiB without TCP_NODELAY and 59
    with it.

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
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        if client.sock:
            client.sock.settimeout(transfer_timeout)
        for write in writes:
            sock.sendall(write)
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
    """The signature of #803, so a failure says whether it is the known one.

    Two counts, both measured on the affected firmware: every wrong word sits at
    the last word of a 1024-byte pair of sectors, and only its top byte is
    wrong, so the low three bytes still carry the offset the payload encodes.
    A failure that matches neither is some other fault on the same path.
    """
    at_pair_end = sum(1 for o, _, _ in bad if o % PAIR == PAIR - 4)
    top_byte_only = sum(1 for _, exp, got in bad if (exp & 0xFFFFFF) == (got & 0xFFFFFF))
    return (f"{len(bad)} word(s) differ; {at_pair_end} at offset {PAIR - 4} of a "
            f"{PAIR}-byte pair, {top_byte_only} with only the top byte wrong")


def path_for(directory: str, shape: str) -> str:
    return f"{directory.rstrip('/')}/{NAME.format(shape=shape)}"


def round_trip(client: ftplib.FTP, directory: str, data: bytes, shape: str,
               transfer_timeout: float) -> None:
    """Upload in `shape`'s writes, read back, and compare. Raises on a diff."""
    path = path_for(directory, shape)
    # Fresh allocation on every attempt -- see the note beside NAME.
    ftp_lib.delete_quietly(client, path)
    writes = SHAPES[shape][1](data)
    elapsed = paced_store(client, path, writes, transfer_timeout)
    control_timeout = client.sock.gettimeout() if client.sock else None
    try:
        if client.sock:
            client.sock.settimeout(transfer_timeout)
        back = ftp_lib.retrieve(client, path)
    finally:
        if client.sock:
            client.sock.settimeout(control_timeout)
    rate = len(data) / elapsed / 1024 if elapsed else 0.0
    detail(f"{len(writes)} writes, {elapsed:.1f}s ({rate:.1f} KiB/s)")

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


def first_usb_dir(client: ftplib.FTP) -> str | None:
    """The USB volume to write to, or None when the machine has no medium in.

    Which port the medium is in is not this suite's business; see
    ftp_lib.usb_volumes.
    """
    volumes = ftp_lib.usb_volumes(client)
    return volumes[0] if volumes else None


def usable(client: ftplib.FTP, directory: str) -> bool:
    try:
        client.cwd(directory)
    except ftplib.all_errors:
        return False
    return True


def scenario_upload_integrity(host: str, password: str, timeout: float,
                              usb_dir: str | None, ram_dir: str,
                              transfer_timeout: float) -> None:
    section("an upload to USB storage is stored as it was sent")
    data = payload()
    with ftp_lib.session(host, password, timeout) as client:
        # Without --usb-dir, write to whichever USB volume the device serves.
        # A fixed /USB0 is not a property of the device: the volume is named
        # after the port its medium is in, so a C64 Ultimate with the stick in
        # its third port serves /USB2, this suite found no /USB0, skipped, and
        # reported OK on a machine that reproduces #803 on every run.
        chosen = usb_dir or first_usb_dir(client)
        if chosen is None or not usable(client, chosen):
            check_start(f"{chosen or 'a USB volume'} is served")
            check_skip(f"no {chosen or 'USB volume'}; this suite needs USB media "
                       "in the machine")
            return
        usb_dir = chosen
        detail(f"USB volume {usb_dir}")
        try:
            # The control first: a client that never leaves a whole sector over
            # exercises the same file, the same medium and the same link, so a
            # failure here is not #803 and says the suite itself is unsound.
            with check(f"a client writing {SHAPES['control'][0]} stores the file intact"):
                round_trip(client, usb_dir, data, "control", transfer_timeout)

            # The discriminator before the regression, deliberately: the check
            # below is the one that fails today, and `check` re-raises, so an
            # answer collected afterwards would never reach the report. RAM disk
            # against USB is the layer split -- same daemon, same buffer, no USB
            # write path. It is not a perfect control, since /Temp is FAT12/16
            # and USB media is usually FAT32.
            if usable(client, ram_dir):
                with check(f"the same client shape stores the file intact on {ram_dir}"):
                    round_trip(client, ram_dir, data, "paced", transfer_timeout)
                ftp_lib.delete_quietly(client, path_for(ram_dir, "paced"))
            else:
                check_start(f"the same client shape stores the file intact on {ram_dir}")
                check_skip(f"no {ram_dir} to compare against")

            with check(f"a client writing {SHAPES['paced'][0]} stores the file intact"):
                round_trip(client, usb_dir, data, "paced", transfer_timeout)
        finally:
            for directory in (usb_dir, ram_dir):
                for shape in SHAPES:
                    ftp_lib.delete_quietly(client, path_for(directory, shape))


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Verify FTP uploads to USB storage are stored unaltered.")
    cli.add_device_arguments(parser, colour=False)
    parser.add_argument("--usb-dir", default=os.environ.get("U64_USB_DIR") or None,
                        help="USB medium to write to (default: the first USB "
                             "volume the device serves).")
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
    raise SystemExit(main())
