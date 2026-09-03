#!/usr/bin/env python3
"""E2E: disabling and re-enabling the ident listener takes effect live."""

import argparse
import json
import os
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))

from api import UltimateApi
from report import Failure, check, check_count, format_exception, suite_fail, suite_ok

STORE = "Network Settings"
ITEM = "Ultimate Ident Service"
PORT = 64
PROBE_TIMEOUT = 0.25
STATE_TIMEOUT = 3.0
QUIET_SECONDS = 1.0


def identify(host: str) -> bool:
    nonce = f"switch-{time.monotonic_ns()}"
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(PROBE_TIMEOUT)
    try:
        sock.sendto(f"json{nonce}".encode("ascii"), (host, PORT))
        payload, _ = sock.recvfrom(4096)
        response = json.loads(payload.decode("utf-8"))
        return response.get("your_string") == nonce
    except (OSError, ValueError):
        return False
    finally:
        sock.close()


def wait_enabled(host: str) -> None:
    deadline = time.monotonic() + STATE_TIMEOUT
    while time.monotonic() < deadline:
        if identify(host):
            return
    raise Failure(f"ident did not answer within {STATE_TIMEOUT:.1f}s")


def wait_disabled(host: str) -> None:
    deadline = time.monotonic() + STATE_TIMEOUT
    quiet_since = None
    while time.monotonic() < deadline:
        if identify(host):
            quiet_since = None
        elif quiet_since is None:
            quiet_since = time.monotonic()
        elif time.monotonic() - quiet_since >= QUIET_SECONDS:
            return
    raise Failure(f"ident kept answering for {STATE_TIMEOUT:.1f}s after it was disabled")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "5.0")))
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    original = api.configs.current(STORE, ITEM)
    if original not in {"Enabled", "Disabled"}:
        suite_fail("ident_service_switch_test", "device did not report the original setting")
        return 1

    failure = ""
    try:
        with check("ident answers when enabled"):
            api.configs.set(STORE, ITEM, "Enabled")
            wait_enabled(args.host)
        with check("ident stops answering when disabled"):
            api.configs.set(STORE, ITEM, "Disabled")
            wait_disabled(args.host)
        with check("ident answers again when re-enabled"):
            api.configs.set(STORE, ITEM, "Enabled")
            wait_enabled(args.host)
    except Failure as exc:
        failure = str(exc)
    except Exception as exc:  # noqa: BLE001
        failure = format_exception(exc)

    try:
        api.configs.set(STORE, ITEM, original)
    except Exception as exc:  # noqa: BLE001
        restore_failure = f"could not restore {ITEM}: {format_exception(exc)}"
        failure = f"{failure}; {restore_failure}" if failure else restore_failure

    if failure:
        suite_fail("ident_service_switch_test", failure)
        return 1
    suite_ok("ident_service_switch_test", f"{check_count()} checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
