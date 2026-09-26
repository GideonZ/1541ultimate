#!/usr/bin/env python3
"""Exercise target 6 with the repository's native 6502 agent (Linux/WSL).

This replaces the running C64 program and uses its RAM. It does not install
firmware. URLs must be disposable, credential-free GET test endpoints.
"""

import argparse
import json
import os
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "tests/lib"), str(ROOT / "tests/e2e/lib")]

from api import MachineApi, RunnersApi
from rest import RestClient
from uci_native import NativeUci


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--url", action="append", default=[])
    parser.add_argument("--raw", action="store_true")
    expected = parser.add_mutually_exclusive_group()
    expected.add_argument("--expect-hex", help="Exact raw body or encoded object-query bytes")
    expected.add_argument("--expect-file", type=Path, help="File containing exact expected bytes")
    parser.add_argument("--expect-blocks", help="Exact comma-separated exchange block lengths")
    parser.add_argument("--expect-unavailable", action="store_true",
                        help="Require TLS failure with no returned body")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    expected_bytes = (args.expect_file.read_bytes() if args.expect_file is not None else
                      bytes.fromhex(args.expect_hex) if args.expect_hex is not None else None)
    rest = RestClient(args.host, password=os.environ.get("ULTIMATE_PASSWORD"), timeout=10)
    uci = NativeUci(MachineApi(rest), RunnersApi(rest), busy_timeout=40, wait_wraps=32,
                    first_status_only=True)
    records = []

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
        print(json.dumps(record), flush=True)
        return result

    try:
        print(rest.expect("GET", "/v1/info").decode(), flush=True)
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
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({"time": time.time(), "records": records}, indent=2) + "\n")


if __name__ == "__main__":
    main()
