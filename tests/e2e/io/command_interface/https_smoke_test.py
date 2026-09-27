#!/usr/bin/env python3
"""Exercise target 6 with the repository's native 6502 agent (Linux/WSL).

This replaces the running C64 program and uses its RAM. It does not install
firmware. URLs must be disposable, credential-free GET test endpoints.
"""

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import report  # noqa: E402
from https_native import evidence_path, run_suite  # noqa: E402

from api import MachineApi, RunnersApi
from rest import RestClient
from uci_native import NativeUci


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser)
    parser.add_argument("--url", action="append", required=True)
    parser.add_argument("--raw", action="store_true")
    expected = parser.add_mutually_exclusive_group()
    expected.add_argument("--expect-hex", help="Exact raw body or encoded object-query bytes")
    expected.add_argument("--expect-file", type=Path, help="File containing exact expected bytes")
    parser.add_argument("--expect-blocks", help="Exact comma-separated exchange block lengths")
    parser.add_argument("--expect-unavailable", action="store_true",
                        help="Require TLS failure with no returned body")
    parser.add_argument("--output", type=Path, default=evidence_path('https_smoke_test'))
    args = parser.parse_args()
    report.apply_colour(args.color)
    if args.output.exists():
        parser.error("Use a new evidence file")
    if "@" in args.host:
        parser.error("Native UCI requires a single device")
    expected_bytes = (args.expect_file.read_bytes() if args.expect_file is not None else
                      bytes.fromhex(args.expect_hex) if args.expect_hex is not None else None)
    rest = RestClient(args.host, password=args.password, timeout=args.timeout)
    machine = MachineApi(rest)
    uci = NativeUci(machine, RunnersApi(rest), busy_timeout=40, wait_wraps=32,
                    first_status_only=True)
    records = []
    started = False

    def command(label, payload, multiple=False):
        try:
            result = uci.transact(payload, single_part=not multiple)
        except Exception as exc:
            records.append({"label": label, "command": payload.hex(), "error": str(exc)})
            raise
        record = {"label": label, "command": payload.hex(), "data": result.data.hex(),
                      "status": result.status_text.decode("ascii", errors="backslashreplace"),
                      "blocks": [len(b.data) for b in result.blocks], "elapsed": result.elapsed}
        records.append(record)
        report.detail(json.dumps(record))
        return result

    try:
        report.detail(rest.expect("GET", "/v1/info").decode())
        started = True
        uci.start()
        identified = command("identify", bytes([6, 1]))
        assert identified.data.rstrip(b"\0") == b"ULTIMATE HTTP TARGET V1.0", identified.data
        for url in args.url:
            command("free all", bytes([6, 0x10]))
            created = command("create " + url, bytes([6, 0x11, 1]) + url.encode("ascii") + b"\0")
            assert created.status_text == b"000 OK" and len(created.data) == 1
            added = command("add user agent", bytes([6, 0x13, created.data[0]]) +
                            b"User-Agent: Ultimate-HTTPS-hardware-test/1.0\0")
            assert added.status_text == b"000 OK"
            result = command("exchange " + url,
                             bytes([6, 0x32 if args.raw else 0x31, created.data[0], 0xff]), True)
            if args.expect_unavailable:
                assert result.status_text == b"503 SERVICE UNAVAILABLE", result.status_text
                assert result.data == b"", "failed exchange returned data"
            elif args.raw:
                assert result.status_text.startswith(b"HTTP/1.1 200 "), result.status_text
                assert 0 < len(result.status_text) <= 255, "invalid hardware status length"
                assert all(len(block.data) <= 895 for block in result.blocks), "unreadable hardware block"
                if args.expect_blocks is not None:
                    assert [len(b.data) for b in result.blocks] == [int(n) for n in args.expect_blocks.split(',')]
                if expected_bytes is not None:
                    assert result.data == expected_bytes, "raw response mismatch"
            elif result.status_text.startswith(b"200 ") and len(result.data) == 2:
                queried = command("query body " + url, bytes([6, 0x2a, result.data[1], 0]), True)
                assert queried.status_text == b"000 OK", queried.status_text
                if expected_bytes is not None:
                    assert queried.data == expected_bytes, "object response mismatch"
            else:
                raise AssertionError(f"exchange failed: {result.status_text!r}")
            command("free all", bytes([6, 0x10]))
    except Exception as exc:
        records.append({"error": str(exc)})
        raise
    finally:
        try:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps({"time": time.time(), "records": records}, indent=2) + "\n")
        finally:
            if started:
                machine.reset(force=True, wait=False)


if __name__ == "__main__":
    run_suite("https-smoke", main)
