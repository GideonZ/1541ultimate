#!/usr/bin/env python3
# E2E: USB storage writes preserve every byte received by the FTP server.

"""Exercise the USB bulk-out tail transfer through the device FTP server.

The file lives on the first physical USB volume the FTP root exposes.  The
upload begins with 1024 bytes followed by two bytes, then continues in
1024-byte writes.  The two-byte write keeps subsequent filesystem writes at an
offset of two modulo 512, which forces the bulk-out tail geometry this check
covers.

Regression guard for GideonZ/1541ultimate#803 (FTP uploads to USB storage come
back with a handful of wrong bytes at the correct size). GideonZ/1541ultimate#821
carries a workaround for the same bulk-out bounce condition, targeted at
(buffer alignment + length) mod 4 != 0; this reproduction takes a different
path to the bug (a 512-byte-aligned two-sector `disk_write()` losing the top
byte of its last word), so it is not known whether #821 addresses it.

This is manual: it only reproduces on firmware where the defect is present
(observed on a C64 Ultimate at firmware 1.2.0; not observed at 3.15 on a
U2+L or a U64), so it does not belong in the default gate that runs against
whatever firmware is currently flashed.

Supported targets: devices with a writable physical USB volume.
"""

import argparse
import hashlib
import os
import re
import socket
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "lib"))

import ftp as ftp_lib
from report import Failure, check, check_count, detail, format_exception, section, suite_fail, suite_ok

SUITE = "bulk_out_integrity_test"
PAYLOAD_BYTES = 8 * 1024
CHUNK_BYTES = 1024
INITIAL_TAIL_BYTES = 2
SEND_DELAY_SECONDS = 0.020
MISMATCH_LIMIT = 4


def payload() -> bytes:
    """Words identify their own expected offset without a separate fixture."""
    return b"".join(struct.pack("<I", 0xA5000000 | index)
                    for index in range(PAYLOAD_BYTES // 4))


def fixture_name() -> str:
    return f"usb_bulk_out_{os.getpid()}_{time.monotonic_ns():x}.bin"


def first_usb_directory(client) -> str:
    """Return the first physical USB volume the device exposes over FTP."""
    entries = set()
    for entry in ftp_lib.names(client, "/"):
        name = entry.rsplit("/", 1)[-1]
        if re.fullmatch(r"USB\d+", name):
            entries.add(name)
    for name in ("USB0", "USB1", "USB2"):
        if name in entries:
            return f"/{name}"
    raise Failure(f"FTP root has no physical USB<n> volume: {sorted(entries)}")


def send_payload(client, name: str, data: bytes) -> None:
    """Send the known bulk-out trigger without letting ftplib coalesce writes."""
    try:
        connection = client.transfercmd(f"STOR {name}")
        try:
            connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            connection.sendall(data[:CHUNK_BYTES])
            time.sleep(SEND_DELAY_SECONDS)
            connection.sendall(data[CHUNK_BYTES:CHUNK_BYTES + INITIAL_TAIL_BYTES])
            time.sleep(SEND_DELAY_SECONDS)
            for offset in range(CHUNK_BYTES + INITIAL_TAIL_BYTES, len(data), CHUNK_BYTES):
                connection.sendall(data[offset:offset + CHUNK_BYTES])
                if offset + CHUNK_BYTES < len(data):
                    time.sleep(SEND_DELAY_SECONDS)
        finally:
            connection.close()
        client.voidresp()
    except Exception as exc:  # noqa: BLE001 - a socket or FTP fault must not be a bare traceback
        raise Failure(f"FTP STOR of {name} did not complete: {exc}") from exc


def mismatches(expected: bytes, actual: bytes) -> list[str]:
    """Describe word-level corruption at offsets meaningful to the USB transfer."""
    found = []
    words = (max(len(expected), len(actual)) + 3) // 4
    for index in range(words):
        offset = index * 4
        wanted = expected[offset:offset + 4]
        received = actual[offset:offset + 4]
        if wanted == received:
            continue
        expected_value = (f"0x{struct.unpack('<I', wanted)[0]:08x}"
                          if len(wanted) == 4 else wanted.hex())
        actual_value = (f"0x{struct.unpack('<I', received)[0]:08x}"
                        if len(received) == 4 else received.hex() or "<missing>")
        found.append(
            f"byte {offset}, word {index}: expected {expected_value}, got {actual_value}; "
            f"mod 512={offset % 512}, mod 1024={offset % 1024}")
        if len(found) == MISMATCH_LIMIT:
            break
    return found


def scenario_bulk_out_integrity(host: str, password: str, timeout: float) -> None:
    section("FTP round trip through physical USB storage")
    name = fixture_name()
    expected = payload()
    client = ftp_lib.connect(host, password, timeout)
    try:
        with check("the paced bulk-out upload returns byte-exact data"):
            volume = first_usb_directory(client)
            client.cwd(volume)
            detail(f"physical volume {volume}")
            send_payload(client, name, expected)
            actual = ftp_lib.retrieve(client, name)
            if actual != expected:
                detail(f"file {name}, expected {len(expected)} bytes, received {len(actual)} bytes")
                detail(f"sha256 expected {hashlib.sha256(expected).hexdigest()}, "
                       f"got {hashlib.sha256(actual).hexdigest()}")
                for mismatch in mismatches(expected, actual):
                    detail(mismatch)
                raise Failure("FTP round trip changed USB storage data")
    finally:
        # This is the sole fixture the suite creates.  Keep cleanup independent
        # of the comparison so a red run does not leave the USB volume dirty.
        ftp_lib.delete_quietly(client, name)
        ftp_lib.close(client)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Verify paced FTP writes round-trip exactly through physical USB storage.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "10.0")))
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    try:
        scenario_bulk_out_integrity(args.host, args.password, args.timeout)
    except Failure as exc:
        suite_fail(SUITE, str(exc))
        return 1
    except Exception as exc:  # noqa: BLE001 - a device/FTP fault must not be a bare traceback
        suite_fail(SUITE, format_exception(exc))
        return 1
    suite_ok(SUITE, f"{check_count()} checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
