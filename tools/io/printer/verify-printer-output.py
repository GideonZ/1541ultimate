#!/usr/bin/env python3
"""Standalone structural verification for virtual-printer PNG/ASCII output.

Downloads printer output files from an Ultimate 64/64e over FTP and checks
that they are well-formed (PNG chunk/CRC structure, or non-blank text) without
needing to run a print job first. Useful for re-checking output left behind by
printer-e2e.py, or output produced interactively (e.g. via the on-device menu).

Usage:
    ./verify-printer-output.py -H u64 --output-base /Usb0/printer/e2e-abc --pages 2
    ./verify-printer-output.py -H u64 --path /Temp/mypage-001.png
"""

import argparse
import ftplib
import io
import struct
import sys
import zlib

FTP_USER_DEFAULT = "user"
FTP_PASSWORD_DEFAULT = "password"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class Failure(RuntimeError):
    pass


class FtpInspector:
    def __init__(self, host, user, password, timeout=15):
        self.host = host
        self.user = user
        self.password = password
        self.timeout = timeout

    def download(self, path):
        ftp = ftplib.FTP()
        ftp.connect(self.host, 21, timeout=self.timeout)
        try:
            ftp.login(self.user, self.password)
            buf = io.BytesIO()
            ftp.retrbinary(f"RETR {path}", buf.write)
            return buf.getvalue()
        finally:
            try:
                ftp.quit()
            except (OSError, EOFError, ftplib.Error):
                ftp.close()


def decode_png_dimensions(data):
    if data[:8] != PNG_SIGNATURE or data[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", data[16:24])


def png_is_well_formed(data):
    if data[:8] != PNG_SIGNATURE:
        return False, "missing PNG signature"
    offset = 8
    saw_ihdr = False
    saw_iend = False
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        chunk_type = data[offset + 4:offset + 8]
        chunk_data = data[offset + 8:offset + 8 + length]
        if offset + 12 + length > len(data):
            return False, f"truncated chunk {chunk_type!r}"
        crc_stored = struct.unpack(">I", data[offset + 8 + length:offset + 12 + length])[0]
        crc_calc = zlib.crc32(chunk_type + chunk_data) & 0xFFFFFFFF
        if crc_calc != crc_stored:
            return False, f"bad CRC in chunk {chunk_type!r}"
        if chunk_type == b"IHDR":
            saw_ihdr = True
        if chunk_type == b"IEND":
            saw_iend = True
            break
        offset += 12 + length
    if not saw_ihdr:
        return False, "no IHDR chunk"
    if not saw_iend:
        return False, "no IEND chunk (truncated file)"
    return True, "ok"


def verify_one(inspector, path, is_ascii):
    try:
        data = inspector.download(path)
    except ftplib.Error as exc:
        print(f"  {path}: FAIL (download error: {exc})")
        return False

    if len(data) == 0:
        print(f"  {path}: FAIL (empty file)")
        return False

    if is_ascii:
        text = data.decode("ascii", errors="replace")
        non_blank_lines = [line for line in text.splitlines() if line.strip()]
        if not non_blank_lines:
            print(f"  {path}: FAIL ({len(data)} bytes, but no non-blank text)")
            return False
        print(f"  {path}: PASS ({len(data)} bytes, {len(non_blank_lines)} non-blank lines)")
        return True

    ok, reason = png_is_well_formed(data)
    if not ok:
        print(f"  {path}: FAIL ({len(data)} bytes, {reason})")
        return False
    dims = decode_png_dimensions(data)
    print(f"  {path}: PASS ({len(data)} bytes, {dims[0]}x{dims[1]}px)")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default="u64")
    parser.add_argument("--ftp-user", default=FTP_USER_DEFAULT)
    parser.add_argument("--ftp-password", default=FTP_PASSWORD_DEFAULT)
    parser.add_argument("--output-base", help="Printer output file base, e.g. /Usb0/printer/e2e-abc")
    parser.add_argument("--pages", type=int, default=1, help="Expected page count for --output-base")
    parser.add_argument("--ascii", action="store_true", help="Treat --output-base as ASCII (<base>.txt)")
    parser.add_argument("--path", action="append", default=[], help="Explicit file path to verify (repeatable)")
    args = parser.parse_args()

    if not args.output_base and not args.path:
        parser.error("specify --output-base or one or more --path")

    inspector = FtpInspector(args.host, args.ftp_user, args.ftp_password)

    paths = list(args.path)
    if args.output_base:
        if args.ascii:
            paths.append(f"{args.output_base}.txt")
        else:
            for page in range(1, args.pages + 1):
                paths.append(f"{args.output_base}-{page:03d}.png")

    print(f"Verifying {len(paths)} file(s) on {args.host}")
    results = [verify_one(inspector, path, args.ascii or path.endswith(".txt")) for path in paths]

    passed = sum(1 for r in results if r)
    print(f"\n{passed}/{len(results)} passed")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
