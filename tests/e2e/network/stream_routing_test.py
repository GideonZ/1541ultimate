#!/usr/bin/env python3
"""E2E: the streams start and stop over either interface, and leave by the wired one.

For video and audio to a unicast port on this host, and for video to its
multicast group, every pairing of the interface that starts the stream with the
interface that stops it. Each started stream has to arrive from the wired
address with the stream's own source port and packet size, with nothing from
the WiFi address, and each stopped stream has to fall silent. A device that
answers on only one address runs the one pairing it has and skips the rest.

A unicast start needs the destination's hardware address on the wired port.
The pairings started over WiFi run first, and the wired address is found from
a multicast start, which needs no ARP, so the first unicast check meets a wired
port that has not resolved this host since it booted or since this host last
addressed it. A lookup routed through WiFi fails that check with 404.

    ./run-tests --suite stream-routing u64
"""

import argparse
import socket
import sys
import time
from collections import Counter
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
sys.path.insert(0, bootstrap.directory("e2e", "network"))

import machine as machine_lib  # noqa: E402
import streams  # noqa: E402
import targets  # noqa: E402
from api import UltimateApi  # noqa: E402
from report import (Failure, check, check_skip, check_start, detail,  # noqa: E402
                    format_exception, suite_fail, suite_ok, suite_skip,
                    teardown_step)
from stream_wired_link_test import (collect, expect_refusal, ident_addresses,  # noqa: E402
                                    local_address, senders_on_groups, start_video,
                                    stream_sender)

SUITE = "stream_routing_test"

# Source port and datagram size of each stream, from DataStreamer.
STREAM_SOURCE = {"video": (53248, streams.VIDEO_PACKET_BYTES),
                 "audio": (54272, streams.AUDIO_PACKET_BYTES)}
# A quarter of the roughly 3400 video and 250 audio packets one second carries.
MIN_PACKETS = {"video": 800, "audio": 60}

LISTEN_SECONDS = 1.0
# Packets already on the wire when the stop was answered.
DRAIN_SECONDS = 0.3


def receiver(group: str | None) -> tuple[socket.socket, int]:
    """A socket on a free unicast port, or on the video group's port."""
    if group is None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("", 0))
        sock.settimeout(0.1)
        return sock, sock.getsockname()[1]
    return streams.stream_socket(group, streams.VIDEO_PORT, timeout=0.1), streams.VIDEO_PORT


def expect_stream(seen: Counter, stream: str, wired: str, wireless: set[str]) -> str:
    """Raise unless `stream` arrived from the wired address and none of it
    from the wireless one. Another device on the group is ignored."""
    port, size = STREAM_SOURCE[stream]
    ours = sum(n for (sender, sport, length), n in seen.items()
               if sender == wired and sport == port and length == size)
    stray = {key: n for key, n in seen.items() if key[0] in wireless}
    if stray:
        raise Failure(f"packets from the wireless address: {stray}")
    if ours < MIN_PACKETS[stream]:
        raise Failure(f"{ours} {stream} packets from {wired}:{port} in "
                      f"{LISTEN_SECONDS:.1f}s, expected at least {MIN_PACKETS[stream]}; "
                      f"saw {dict(seen)}")
    return f"{ours} packets from {wired}:{port}"


def expect_silence(sock: socket.socket, wired: str) -> None:
    time.sleep(DRAIN_SECONDS)
    sock.setblocking(False)
    try:
        while True:
            sock.recvfrom(2048)
    except OSError:
        pass
    finally:
        sock.settimeout(0.1)
    late = sum(n for key, n in collect(sock, LISTEN_SECONDS).items() if key[0] == wired)
    if late:
        raise Failure(f"{late} packets from {wired} after the stop")


def routing(apis: dict[str, UltimateApi], wired: str, wireless: set[str],
            local: str) -> None:
    # WiFi first, so the first unicast start meets a wired port that has not
    # resolved this host.
    names = sorted(apis, reverse=True)
    pairs = [(a, b) for a in names for b in names]
    for stream, group in (("video", None), ("audio", None),
                          ("video", streams.VIDEO_GROUP)):
        where = "multicast" if group else "unicast"
        for via_start, via_stop in pairs:
            sock, port = receiver(group)
            try:
                with check(f"{stream} {where}: start over {via_start}, stop over {via_stop}"):
                    code, _, body = apis[via_start].rest.request(
                        "PUT", f"/v1/streams/{stream}:start",
                        params={"ip": f"{group or local}:{port}"})
                    if code != 200:
                        raise Failure(f"start returned HTTP {code}: "
                                      f"{' '.join(body.decode('utf-8', 'replace').split())}")
                    detail(expect_stream(collect(sock, LISTEN_SECONDS), stream,
                                         wired, wireless))
                    apis[via_stop].streams.stop(stream)
                    expect_silence(sock, wired)
            finally:
                sock.close()
                teardown_step(f"stop {stream}", lambda: apis[via_stop].streams.stop(stream))
        if len(names) == 1:
            check_start(f"{stream} {where}: start and stop over different interfaces")
            check_skip("the device answers on one address only")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    cli.add_device_arguments(parser)
    args = parser.parse_args()
    password = args.password or None

    host = targets.device_of(args.host)
    api = UltimateApi(args.host, password, args.timeout)
    info = api.info()
    machine = machine_lib.identify(host, lambda: (str(info.product),
                                                  str(info.firmware_version)))
    if not machine.has_data_streams:
        suite_skip(SUITE, f"{machine.described} has no data streams")
        return 0

    failure = ""
    running: set[str] = set()
    try:
        before = senders_on_groups()
        code, body, seen = start_video(api)
        if code != 200:
            expect_refusal(code, body)
            suite_skip(SUITE, "the device has no wired address, which the streams need")
            return 0
        api.streams.stop("video")
        wired = stream_sender(seen, before["video"])
        running = {name for name, senders in before.items() if wired in senders}
        local = local_address(socket.gethostbyname(host))
        found = ident_addresses(str(info.extra.get("unique_id", "")), local)
        wireless = (found | {socket.gethostbyname(host)}) - {wired}
        detail(f"wired {wired}, wireless {sorted(wireless) or 'none'}, this host {local}")
        apis = {"Ethernet": UltimateApi(wired, password, args.timeout)}
        if wireless:
            apis["WiFi"] = UltimateApi(min(wireless), password, args.timeout)
        routing(apis, wired, wireless, local)
    except Failure as exc:
        failure = str(exc)
    except Exception as exc:  # noqa: BLE001
        failure = format_exception(exc)
    finally:
        # Leave the standard streams as they were found, for a recorder.
        for name in sorted(running):
            teardown_step(f"restart {name}", lambda name=name: api.streams.start(
                name, ip=streams.Arming(api, args.host).address(name)))

    if failure:
        suite_fail(SUITE, failure)
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
