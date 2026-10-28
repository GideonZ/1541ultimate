#!/usr/bin/env python3
# PERF: REST latency per network interface address and per route, comparable across firmware builds.

"""Time a spread of REST routes on every address of a device, and compare two runs.

A device with both Ethernet and WiFi up answers on two addresses, and a change to
how the firmware picks the interface for a reply can move one without the other.
This measures the same routes on each address given and keeps every sample, so a
run made on one firmware build can be compared with a run made on another.

Each request opens a new TCP connection, so the handshake and every reply segment
take the route the device chooses. Requests rotate through the routes and the
addresses in every round, which spreads drift in the network over all of them.

The routes cover a short reply (`/v1/version`), JSON of different sizes, a DMA read
of 64 bytes, 16 KB and 60 KB, a write, and a file lookup.

    # Measure, then flash the other firmware, measure again with another label.
    tests/perf/rest_path_latency_perf_test.py -H 192.168.1.13 --address 192.168.1.70 \\
        --label stock --json stock.json
    tests/perf/rest_path_latency_perf_test.py -H 192.168.1.13 --address 192.168.1.70 \\
        --label patched --json patched.json
    tests/perf/rest_path_latency_perf_test.py --compare stock.json patched.json

The comparison prints, per address and route, both medians, the change, and a 95%
bootstrap interval for the change in the median. A cell is marked SLOWER or FASTER
only when that interval excludes zero and the change is at least `--min-change`
percent, so run-to-run noise on a wireless path is not reported as a difference.
Measure each firmware more than once, and measure an address on its own as well as
together with the others: traffic on one address can keep the radio awake for the
other, which changes the wireless figure without any change in the firmware.
"""
import argparse
import http.client
import json
import random
import statistics
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import cli  # noqa: E402
from report import (  # noqa: E402
    Failure, apply_colour, check_ok, check_start, detail, section, suite_fail, suite_ok)

SUITE = "rest_path_latency_perf_test"
ROUNDS = 100
BOOTSTRAP_RESAMPLES = 2000

ROUTES = (
    ("version", "GET", "/v1/version"),
    ("info", "GET", "/v1/info"),
    ("configs-all", "GET", "/v1/configs"),
    ("configs-category", "GET", "/v1/configs/C64%20and%20Cartridge%20Settings"),
    ("drives", "GET", "/v1/drives"),
    ("readmem-64B", "GET", "/v1/machine:readmem?address=0400&length=64"),
    ("readmem-16KB", "GET", "/v1/machine:readmem?address=0800&length=16384"),
    ("readmem-60KB", "GET", "/v1/machine:readmem?address=0400&length=61440"),
    # $0334 is the cassette buffer, which nothing on the device reads while idle.
    ("writemem-8B", "PUT", "/v1/machine:writemem?address=0334&data=0102030405060708"),
    ("file-info", "GET", "/v1/files/Temp:info"),
)


def request_ms(address: str, method: str, path: str, password: str, timeout: float) -> float:
    headers = {"Connection": "close"}
    if password:
        headers["X-Password"] = password
    started = time.perf_counter()
    connection = http.client.HTTPConnection(address, 80, timeout=timeout)
    try:
        connection.request(method, path, headers=headers)
        response = connection.getresponse()
        response.read()
        elapsed = (time.perf_counter() - started) * 1000.0
        if response.status != 200:
            raise Failure(f"{method} {path} on {address} answered {response.status}")
        return elapsed
    except OSError as exc:
        raise Failure(f"{method} {path} on {address} failed: {exc}") from exc
    finally:
        connection.close()


def percentile(sorted_values: list[float], fraction: float) -> float:
    return sorted_values[min(len(sorted_values) - 1, int(len(sorted_values) * fraction))]


def measure(args) -> int:
    addresses = [args.host, *args.address]
    samples: dict[str, list[float]] = {}
    section(f"Warm up, {len(addresses)} address(es), {len(ROUTES)} routes")
    for address in addresses:
        for _, method, path in ROUTES:
            request_ms(address, method, path, args.password, args.timeout)
    section(f"{args.rounds} rounds")
    check_start("measure")
    for _ in range(args.rounds):
        for name, method, path in ROUTES:
            for address in addresses:
                samples.setdefault(f"{address} {name}", []).append(
                    request_ms(address, method, path, args.password, args.timeout))
    check_ok(f"{args.rounds * len(ROUTES) * len(addresses)} requests")
    for key, values in samples.items():
        ordered = sorted(values)
        detail(f"{key:<32} p50 {statistics.median(ordered):7.1f}ms  p90 {percentile(ordered, 0.90):7.1f}  "
               f"p99 {percentile(ordered, 0.99):7.1f}  max {ordered[-1]:7.1f}")
    if args.json:
        Path(args.json).write_text(json.dumps(
            {"label": args.label, "rounds": args.rounds, "samples": samples}, indent=1))
        detail(f"samples written to {args.json}")
    return 0


def median_change_interval(base: list[float], new: list[float], rng: random.Random):
    """(change of the median in percent, 95% bootstrap interval of that change)."""
    reference = statistics.median(base)
    changes = []
    for _ in range(BOOTSTRAP_RESAMPLES):
        resampled_base = statistics.median(rng.choices(base, k=len(base)))
        resampled_new = statistics.median(rng.choices(new, k=len(new)))
        changes.append((resampled_new / resampled_base - 1.0) * 100.0)
    changes.sort()
    return ((statistics.median(new) / reference - 1.0) * 100.0,
            changes[int(0.025 * len(changes))], changes[int(0.975 * len(changes)) - 1])


def compare(args) -> int:
    base_file, new_file = (json.loads(Path(p).read_text()) for p in args.compare)
    rng = random.Random(1541)
    slower = faster = 0
    section(f"{base_file['label']} -> {new_file['label']}")
    for key, base in base_file["samples"].items():
        new = new_file["samples"].get(key)
        if new is None:
            continue
        change, low, high = median_change_interval(base, new, rng)
        verdict = ""
        if low > 0 and change >= args.min_change:
            verdict, slower = "SLOWER", slower + 1
        elif high < 0 and -change >= args.min_change:
            verdict, faster = "FASTER", faster + 1
        detail(f"{key:<32} {statistics.median(base):7.1f} -> {statistics.median(new):7.1f}ms  "
               f"{change:+6.1f}% [{low:+6.1f}, {high:+6.1f}]  {verdict}")
    detail(f"{faster} faster, {slower} slower")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    cli.add_device_arguments(parser, timeout=20.0)
    parser.add_argument("--address", action="append", default=[],
                        help="another address to measure alongside, for instance the WiFi one")
    parser.add_argument("-n", "--rounds", type=int, default=ROUNDS)
    parser.add_argument("--label", default="run", help="name stored with the samples")
    parser.add_argument("--json", help="write every sample to this file")
    parser.add_argument("--compare", nargs=2, metavar=("BASE", "NEW"),
                        help="compare two --json files instead of measuring")
    parser.add_argument("--min-change", type=float, default=5.0,
                        help="smallest change in percent that --compare reports")
    args = parser.parse_args()
    apply_colour(args.color)
    try:
        status = compare(args) if args.compare else measure(args)
    except Failure as exc:
        suite_fail(SUITE, str(exc))
        return 1
    if not status:
        suite_ok(SUITE)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
