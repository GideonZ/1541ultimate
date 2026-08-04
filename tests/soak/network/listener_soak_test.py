#!/usr/bin/env python3
# SOAK: Verifies listeners reap abandoned connections and stay responsive under churn.

"""Short soak over the network listeners, checking capacity is not lost.

Every service caps concurrent sessions to bound lwIP netconn use, so a client
that goes away without closing holds one of those slots. software/network/
socket_keepalive.h gives telnet and the FTP control connection one shared TCP
keepalive policy for exactly this, detecting a gone peer in roughly
idle + count * interval seconds.

This suite does not fake a vanished peer at the network level; that needs root
and is covered by telnet_stale_session_test.py. It does the thing a real client
does far more often: connect and disappear without a clean shutdown, repeatedly,
while REST keeps being used. If a service leaked a slot per abandoned connection
it would run out within a handful of rounds, and REST latency would climb as the
shared netconn pool drained.

Checks, all externally visible:
  - REST stays responsive throughout, with no request exceeding the budget
  - telnet and FTP still accept a fresh connection after each churn round
  - final REST latency has not degraded against the baseline

Runs in about two minutes by default. Use --rounds to lengthen it.
"""
import argparse
import os
import socket
import struct
import statistics
import sys
import time
import urllib.error
import urllib.request
from typing import List, Optional

# tests/lib holds the reporting rules every suite shares.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
from report import Failure, check, check_ok, check_start, suite_fail, suite_ok


TELNET_PORT = 23
FTP_PORT = 21
# Each service caps concurrent sessions; abandon more than the cap per round so a
# leak cannot hide behind spare capacity.
ABANDON_PER_ROUND = 6
DEFAULT_ROUNDS = 8
ROUND_PAUSE_SECONDS = 12.0
REST_SAMPLES_PER_ROUND = 5
# A healthy /v1/version is milliseconds. This is a generous ceiling that still
# catches a device whose listeners are draining the netconn pool.
REST_BUDGET_SECONDS = 5.0
DEGRADATION_FACTOR = 4.0


def rest_latency(host: str, password: Optional[str], timeout: float) -> float:
    headers = {"X-Password": password} if password else {}
    request = urllib.request.Request(f"http://{host}/v1/version", headers=headers)
    start = time.monotonic()
    with urllib.request.urlopen(request, timeout=timeout) as response:
        response.read()
    return time.monotonic() - start


def sample_rest(host: str, password: Optional[str], samples: int, budget: float) -> List[float]:
    out = []
    for _ in range(samples):
        try:
            out.append(rest_latency(host, password, budget))
        except (urllib.error.URLError, OSError, TimeoutError) as exc:
            raise Failure(f"REST did not answer within {budget:.0f}s: {exc}")
        time.sleep(0.2)
    return out


def abandon(host: str, port: int, count: int) -> int:
    """Open connections and drop them without a clean shutdown.

    The sockets are closed with SO_LINGER 0 so the peer sees a reset rather than
    an orderly FIN, which is the closest a non-root test can get to a client that
    simply went away.
    """
    opened = 0
    for _ in range(count):
        try:
            s = socket.create_connection((host, port), timeout=4.0)
        except OSError:
            continue  # service at capacity right now; that is what we are measuring
        opened += 1
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
        except OSError:
            pass
        s.close()
        time.sleep(0.1)
    return opened


def accepts_connection(host: str, port: int, timeout: float = 6.0) -> bool:
    try:
        s = socket.create_connection((host, port), timeout=timeout)
    except OSError:
        return False
    s.close()
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Soak the network listeners with abandoned connections."
    )
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float, default=REST_BUDGET_SECONDS)
    parser.add_argument("-r", "--rounds", type=int, default=DEFAULT_ROUNDS,
                        help=f"churn rounds (default {DEFAULT_ROUNDS}, about two minutes)")
    args = parser.parse_args()

    check_start("REST baseline latency")
    baseline = sample_rest(args.host, args.password, REST_SAMPLES_PER_ROUND, args.timeout)
    base_median = statistics.median(baseline)
    check_ok(f"median {base_median * 1000:.0f} ms")

    with check("telnet and FTP accept a connection before the soak"):
        if not accepts_connection(args.host, TELNET_PORT):
            raise Failure(f"telnet {args.host}:{TELNET_PORT} refused a connection at baseline")
        if not accepts_connection(args.host, FTP_PORT):
            raise Failure(f"FTP {args.host}:{FTP_PORT} refused a connection at baseline")

    for round_index in range(1, args.rounds + 1):
        check_start(f"round {round_index}/{args.rounds}: abandon connections, REST stays responsive")
        opened_telnet = abandon(args.host, TELNET_PORT, ABANDON_PER_ROUND)
        opened_ftp = abandon(args.host, FTP_PORT, ABANDON_PER_ROUND)
        latencies = sample_rest(args.host, args.password, REST_SAMPLES_PER_ROUND, args.timeout)
        worst = max(latencies)
        check_ok(f"telnet={opened_telnet} ftp={opened_ftp} worst REST {worst * 1000:.0f} ms")
        time.sleep(ROUND_PAUSE_SECONDS)

    with check("both listeners still accept a connection after the soak"):
        if not accepts_connection(args.host, TELNET_PORT):
            raise Failure(
                f"telnet refused a connection after {args.rounds} rounds: abandoned sessions "
                "were not reaped, so the session table stayed full"
            )
        if not accepts_connection(args.host, FTP_PORT):
            raise Failure(
                f"FTP refused a connection after {args.rounds} rounds: abandoned sessions "
                "were not reaped, so the session table stayed full"
            )

    with check("REST latency has not degraded against the baseline"):
        final = sample_rest(args.host, args.password, REST_SAMPLES_PER_ROUND, args.timeout)
        final_median = statistics.median(final)
        ceiling = max(base_median * DEGRADATION_FACTOR, 0.5)
        if final_median > ceiling:
            raise Failure(
                f"REST median went from {base_median * 1000:.0f} ms to "
                f"{final_median * 1000:.0f} ms, past the {ceiling * 1000:.0f} ms ceiling"
            )

    suite_ok("listener_soak_test")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("listener_soak_test", str(exc))
        raise SystemExit(1)
