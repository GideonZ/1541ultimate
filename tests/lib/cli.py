#!/usr/bin/env python3
# The command-line arguments every suite that drives a device shares.

"""One definition of `-H`, `-p`, `-t` and of a duration.

`-H` was registered in 51 files and `-p` in 41, each with its own default and
its own help text, and `tests/lib/report.py`'s `add_colour_argument` was the
only argparse helper the tree shared. The spellings had already drifted: `-p`
defaulted to `""` in nineteen files and to `None` in fourteen, so a suite ported
between them changed what it handed `RestClient`.

`parse_duration` had five copies with three behaviours. One accepted `ms`, `s`,
`m` and `h` and rejected anything else with an `ArgumentTypeError`; one accepted
no `h`; and `ftp_client_test.py`'s had no error handling at all, so
`--duration 5x` ended the run with a bare `ValueError` traceback out of
`float()`. The first is the one kept here.
"""

import argparse
import os

DEFAULT_HOST_ENV = "U64_HOST"
DEFAULT_PASSWORD_ENV = "U64_PASS"
DEFAULT_TIMEOUT_ENV = "U64_TIMEOUT"

# The fallback when neither the caller nor the environment names one. The bench
# convention is that U64_HOST is set; this only keeps `--help` readable.
FALLBACK_HOST = "u64"
FALLBACK_TIMEOUT_SECONDS = 10.0

# What the environment says now. Read per call rather than at import, because
# the self-tests set these variables around a case.


def host_default(fallback: str = FALLBACK_HOST) -> str:
    return os.environ.get(DEFAULT_HOST_ENV, fallback)


def password_default(fallback: str | None = "") -> str | None:
    """`None` and `""` both mean "no password"; the fallback decides which.

    Fourteen suites defaulted to None and nineteen to the empty string. Both
    reach RestClient as no password, so the difference is only what a suite's
    own code sees, and each keeps what it had.
    """
    return os.environ.get(DEFAULT_PASSWORD_ENV, fallback)


def timeout_default(fallback: float = FALLBACK_TIMEOUT_SECONDS) -> float:
    try:
        return float(os.environ.get(DEFAULT_TIMEOUT_ENV, fallback))
    except (TypeError, ValueError):
        return fallback


def parse_duration(value: str) -> float:
    """Seconds from `30`, `500ms`, `45s`, `5m` or `1.5h`.

    Written for argparse's `type=`, so a bad value is reported as a usage error
    naming the argument rather than as a traceback from inside `float()`.
    """
    text = value.strip().lower()
    multiplier = 1.0
    if text.endswith("ms"):
        multiplier, text = 0.001, text[:-2]
    elif text.endswith("s"):
        text = text[:-1]
    elif text.endswith("m"):
        multiplier, text = 60.0, text[:-1]
    elif text.endswith("h"):
        multiplier, text = 3600.0, text[:-1]
    try:
        seconds = float(text) * multiplier
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            f"invalid duration {value!r}; use 30, 500ms, 45s, 5m or 1.5h") from exc
    if seconds <= 0:
        raise argparse.ArgumentTypeError(
            f"duration {value!r} must be greater than zero")
    return seconds


def add_device_arguments(parser: argparse.ArgumentParser, *,
                         host: str | None = None,
                         password: str | None = "",
                         timeout: float | None = FALLBACK_TIMEOUT_SECONDS,
                         colour: bool = True) -> None:
    """Register `-H`, `-p`, `-t` and `--color` on `parser`.

    Each keyword is the fallback used when the matching environment variable is
    not set, so a suite that legitimately wants a longer timeout or a different
    default host passes one and keeps everything else shared. Passing `None` for
    `password` keeps the `None` default that fourteen suites had; the rest used
    the empty string, and both reach `RestClient` as "no password".

    `colour` is on by default because every suite here reports through
    `report.py`, and off for a parser that already registered it itself.
    `timeout=None` registers no `-t` at all, for a suite that does not take a
    per-call budget: `printer_test.py` already had a `--timeout-seconds` for
    its page budget, and adding `-t/--timeout` beside it made
    `--timeout 240` set a value nothing reads while leaving the page budget at
    its default, because argparse prefers an exact match to a prefix.
    """
    parser.add_argument(
        "-H", "--host", default=host_default(host or FALLBACK_HOST),
        help=f"Device, or a cartridge@computer target "
             f"(default: ${DEFAULT_HOST_ENV}, else {host or FALLBACK_HOST})")
    parser.add_argument(
        "-p", "--password", default=password_default(password),
        help=f"REST and FTP password (default: ${DEFAULT_PASSWORD_ENV}, else none)")
    if timeout is not None:
        parser.add_argument(
            "-t", "--timeout", type=float, default=timeout_default(timeout),
            help=f"Seconds to wait for one device call "
                 f"(default: ${DEFAULT_TIMEOUT_ENV}, else {timeout})")
    if colour:
        import report

        report.add_colour_argument(parser)
