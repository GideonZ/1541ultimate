#!/usr/bin/env python3
"""E2E: disabling and re-enabling the ident listener takes effect live."""

import argparse
import json
import socket
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402

import machine as machine_lib
import targets
from api import UltimateApi
from report import (Failure, check, check_count, format_exception,
                    note_assumed_fix, suite_fail, suite_ok, suite_skip)

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
    cli.add_device_arguments(parser, timeout=5.0, colour=False)
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    original = api.configs.current(STORE, ITEM)
    if original not in {"Enabled", "Disabled"}:
        suite_fail("ident_service_switch_test", "device did not report the original setting")
        return 1

    info = api.info()
    machine = machine_lib.identify(
        targets.device_of(args.host),
        lambda: (str(info.product), str(info.firmware_version)))

    # The whole suite is about the switch taking effect live, so a machine
    # whose firmware does not do that yet has nothing here to measure. The
    # table in tests/lib/machine.py names those machines and the wording.
    absent = machine.missing_fix(machine_lib.IDENT_SWITCHES_LIVE)
    if absent:
        suite_skip("ident_service_switch_test", absent)
        return 0
    # This gates the whole suite rather than one check, so it cannot call
    # Machine.skip_without_fix (one check per gate). Tags the first check
    # below instead, the same signal a single-check gate would leave, so
    # tools/stale_gates.py can tell a run under --assume-fix that this
    # suite ran clean from one where it was skipped outright.
    if machine.assumed_fix(machine_lib.IDENT_SWITCHES_LIVE):
        note_assumed_fix(machine_lib.IDENT_SWITCHES_LIVE, machine.kind)

    failure = ""
    try:
        with check("ident answers when enabled"):
            api.configs.set(STORE, ITEM, "Enabled")
            wait_enabled(args.host)
        # Both of these need the listener to act on the switch while it runs.
        # Firmware without that reads the switch once, when the listener
        # starts, so the first would fail and the second would then prove
        # nothing. Skipped together, with the fix named on each line: a list
        # rather than a generator, so both lines are printed.
        live_checks = [
            "ident stops answering when disabled",
            "ident answers again when re-enabled",
        ]
        if not any([machine.skip_without_fix(
                machine_lib.SERVICE_SWITCHES_APPLY_LIVE, label)
                    for label in live_checks]):
            with check(live_checks[0]):
                api.configs.set(STORE, ITEM, "Disabled")
                wait_disabled(args.host)
            with check(live_checks[1]):
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
