#!/usr/bin/env python3
"""E2E: the streams start and stop over either interface, and leave by the wired one.

For video, audio and debug to unicast and multicast receivers, every pairing
of REST/DMA and Ethernet/WiFi controls that starts and stops the stream.
Each started stream has to arrive from the wired address with the stream's
own source port and packet size, with nothing from
the WiFi address, and each stopped stream has to fall silent. A device that
answers on only one address runs the available pairings and skips the rest.

Also checks retargeting, default ports, DMA timers and configured destinations,
simultaneous streams, invalid requests, every debug mode, and menu start/stop
and cancellation. Pass --receiver-name to exercise DNS on the receiving host.

A unicast start needs the destination's hardware address on the wired port.
The wired address is found from a multicast start, which needs no ARP, and the
pairings started over WiFi run first. To exercise an uncached lookup, run after
a fresh device boot and address the suite to WiFi, without prior traffic to
the wired address. A successful start with a warm wired ARP entry does not by
itself prove that an uncached lookup uses the wired port.

    ./run-tests --suite stream-routing u64
"""

import argparse
import ipaddress
import socket
import struct
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
sys.path.insert(0, bootstrap.directory("soak", "network"))

import dma_probe  # noqa: E402
import machine as machine_lib  # noqa: E402
import streams  # noqa: E402
import targets  # noqa: E402
from api import UltimateApi  # noqa: E402
from stream_monitor import (DEBUG_PACKET_SIZE, STREAM_IDS, StreamKind,  # noqa: E402
                            StreamPacketTracker)
from ui_backend import MODE_OVERLAY, make_browser  # noqa: E402
from report import (Failure, check, check_skip, check_start, detail,  # noqa: E402
                    format_exception, suite_fail, suite_ok, suite_skip,
                    teardown_step)
from stream_wired_link_test import (collect, expect_refusal, local_address,  # noqa: E402
                                    senders_on_groups, standard_groups, start_video,
                                    stream_sender, wireless_address)

SUITE = "stream_routing_test"

# Source port and datagram size of each stream, from DataStreamer.
STREAM_SOURCE = {"video": (53248, streams.VIDEO_PACKET_BYTES),
                 "audio": (54272, streams.AUDIO_PACKET_BYTES),
                 "debug": (6510, DEBUG_PACKET_SIZE)}
# A quarter of the roughly 3400 video and 250 audio packets one second carries.
MIN_PACKETS = {"video": 800, "audio": 60, "debug": 1}

LISTEN_SECONDS = 1.0
# Packets already on the wire when the stop was answered.
DRAIN_SECONDS = 0.3


def receiver(group: str | None, port: int = 0) -> tuple[socket.socket, int]:
    """A socket on a free port, joined to `group` when one is given."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if port:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", port))
    if group is not None:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                        socket.inet_aton(group) + socket.inet_aton("0.0.0.0"))
    sock.settimeout(0.1)
    return sock, sock.getsockname()[1]


def control(api: UltimateApi, protocol: str, stream: str, action: str,
            destination: str | None = None, ticks: int = 0) -> None:
    """Issue one control command; a DMA identify reply fences its execution."""
    if protocol == "REST":
        if action == "start":
            api.streams.start(stream, ip=destination)
        else:
            api.streams.stop(stream)
        return
    start = action == "start"
    payload = (struct.pack("<H", ticks) + (destination or "").encode("ascii")
               if start and (destination is not None or ticks) else b"")
    command = (0xFF20 if start else 0xFF30) + STREAM_IDS[stream]
    with socket.create_connection((api.host, api.target.dma_port), timeout=api.rest.timeout) as sock:
        dma_probe.authenticate_socket(sock, api.rest.password)
        sock.sendall(dma_probe.command_frame(command, payload))
        dma_probe.identify_from_socket(sock)


def expect_stream(seen: Counter, stream: str, wired: str, wireless: str | None,
                  seconds: float = LISTEN_SECONDS) -> str:
    """Raise unless `stream` arrived from the wired address, and nothing from
    the wireless one."""
    port, size = STREAM_SOURCE[stream]
    ours = sum(n for (sender, sport, length), n in seen.items()
               if sender == wired and sport == port and length == size)
    stray = {key: n for key, n in seen.items() if key[0] == wireless}
    if stray:
        raise Failure(f"packets from the wireless address: {stray}")
    minimum = max(1, int(MIN_PACKETS[stream] * seconds / LISTEN_SECONDS))
    if ours < minimum:
        raise Failure(f"{ours} {stream} packets from {wired}:{port} in "
                      f"{seconds:.1f}s, expected at least {minimum}; "
                      f"saw {dict(seen)}")
    return f"{ours} packets from {wired}:{port}"


def expect_silence(sock: socket.socket, wired: str) -> None:
    # Bound the drain even when a broken stop leaves the stream running.
    collect(sock, DRAIN_SECONDS)
    late = sum(n for key, n in collect(sock, LISTEN_SECONDS).items() if key[0] == wired)
    if late:
        raise Failure(f"{late} packets from {wired} after the stop")


def expect_debug_data(sock: socket.socket, wired: str, wireless: str | None) -> str:
    tracker = StreamPacketTracker(StreamKind.DEBUG, logger=lambda *_: None)
    deadline = time.monotonic() + LISTEN_SECONDS
    while time.monotonic() < deadline:
        try:
            data, sender = sock.recvfrom(2048)
        except TimeoutError:
            continue
        if sender[0] == wireless:
            raise Failure("debug data arrived from WiFi")
        if sender == (wired, STREAM_SOURCE["debug"][0]):
            tracker.note_packet(data)
    result = tracker.snapshot()
    if not result.packets_received or result.size_errors or result.header_errors or result.structure_errors:
        raise Failure(f"invalid debug trace: {result}")
    return f"{result.packets_received} trace packets with valid headers and bus-record lengths"


def routing(apis: dict[str, UltimateApi], wired: str, wireless: str | None,
            local: str, group: str) -> None:
    # WiFi first to avoid warming the wired ARP entry through a REST connection.
    names = [name for name in ("WiFi", "Ethernet") if name in apis]
    controllers = [(name, protocol) for name in names for protocol in ("REST", "DMA")]
    pairs = [(a, b) for a in controllers for b in controllers]
    cases = [(stream, joined) for stream in STREAM_SOURCE for joined in (None, group)]
    for stream, joined in cases:
        where = "multicast" if joined else "unicast"
        for (via_start, start_protocol), (via_stop, stop_protocol) in pairs:
            sock, port = receiver(joined)
            try:
                with check(f"{stream} {where}: {start_protocol}/{via_start} start, "
                           f"{stop_protocol}/{via_stop} stop"):
                    control(apis[via_start], start_protocol, stream, "start",
                            f"{joined or local}:{port}")
                    detail(expect_stream(collect(sock, LISTEN_SECONDS), stream,
                                         wired, wireless))
                    control(apis[via_stop], stop_protocol, stream, "stop")
                    expect_silence(sock, wired)
            finally:
                sock.close()
                teardown_step(f"stop {stream}", lambda: apis[via_stop].streams.stop(stream))
        if len(names) == 1:
            check_start(f"{stream} {where}: start and stop over different interfaces")
            check_skip("the device answers on one address only")


def lifecycle(api: UltimateApi, wired: str, wireless: str | None,
              local: str, group: str, receiver_name: str | None) -> None:
    for stream in STREAM_SOURCE:
        for protocol in ("REST", "DMA"):
            first, first_port = receiver(None)
            second, second_port = receiver(group)
            try:
                with check(f"{stream}: {protocol} repeated starts/stops and retargeting"):
                    control(api, protocol, stream, "stop")
                    control(api, protocol, stream, "stop")
                    for _ in range(2):
                        control(api, protocol, stream, "start", f"{local}:{first_port}")
                    detail(expect_stream(collect(first, LISTEN_SECONDS), stream, wired, wireless))
                    control(api, protocol, stream, "start", f"{group}:{second_port}")
                    expect_silence(first, wired)
                    detail(expect_stream(collect(second, LISTEN_SECONDS), stream, wired, wireless))
                    control(api, protocol, stream, "stop")
                    control(api, protocol, stream, "stop")
                    expect_silence(second, wired)
            finally:
                first.close()
                second.close()
                teardown_step(f"stop {stream}", lambda: api.streams.stop(stream))

        # The default port is independent of the control transport.
        sock, _ = receiver(None, 11000 + STREAM_IDS[stream])
        try:
            with check(f"{stream}: default UDP port and case-insensitive REST name"):
                api.streams.start(stream.upper(), ip=local)
                detail(expect_stream(collect(sock, LISTEN_SECONDS), stream, wired, wireless))
                api.streams.stop(stream.upper())
                expect_silence(sock, wired)
        finally:
            sock.close()
            teardown_step(f"stop {stream}", lambda: api.streams.stop(stream))

        sock, port = receiver(None)
        item = {"video": "Stream VIC to", "audio": "Stream Audio to",
                "debug": "Stream Debug to"}[stream]
        original = api.configs.current("Data Streams", item)
        try:
            with check(f"{stream}: DMA configured destination and timed stop"):
                api.configs.set("Data Streams", item, f"{local}:{port}")
                control(api, "DMA", stream, "start")
                detail(expect_stream(collect(sock, LISTEN_SECONDS), stream, wired, wireless))
                control(api, "DMA", stream, "stop")
                expect_silence(sock, wired)
                # FreeRTOSConfig.h defines 200 ticks/second: two seconds.
                control(api, "DMA", stream, "start", ticks=400)
                detail(expect_stream(collect(sock, LISTEN_SECONDS), stream, wired, wireless))
                time.sleep(1.2)
                expect_silence(sock, wired)
        finally:
            sock.close()
            teardown_step(f"stop {stream}", lambda: api.streams.stop(stream))
            teardown_step(f"restore {item}", lambda: api.configs.set("Data Streams", item, original))

        sock, port = receiver(None)
        try:
            with check(f"{stream}: broadcast destination"):
                # Broadcast floods the AP too; fence this bounded burst over Ethernet.
                wired_api = UltimateApi(wired, api.rest.password, api.rest.timeout)
                control(wired_api, "DMA", stream, "start", f"255.255.255.255:{port}", ticks=40)
                detail(expect_stream(collect(sock, LISTEN_SECONDS), stream, wired, wireless, 0.1))
                expect_silence(sock, wired)
            with check(f"{stream}: DNS receiver name"):
                if receiver_name is None:
                    check_skip("provide --receiver-name for a name the device's DNS resolves to this host")
                else:
                    api.streams.start(stream, ip=f"{receiver_name}:{port}")
                    detail(expect_stream(collect(sock, LISTEN_SECONDS), stream, wired, wireless))
                    api.streams.stop(stream)
                    expect_silence(sock, wired)
        finally:
            sock.close()
            teardown_step(f"stop {stream}", lambda: api.streams.stop(stream))


def interactions(api: UltimateApi, wired: str, wireless: str | None, local: str) -> None:
    sockets = {name: receiver(None) for name in STREAM_SOURCE}

    def start(stream: str, protocol: str = "REST") -> None:
        control(api, protocol, stream, "start", f"{local}:{sockets[stream][1]}")

    def arrives(stream: str) -> None:
        detail(expect_stream(collect(sockets[stream][0], LISTEN_SECONDS), stream, wired, wireless))

    try:
        with check("audio and video coexist; stopping either leaves the other running"):
            start("audio")
            start("video")
            arrives("audio")
            arrives("video")
            api.streams.stop("video")
            expect_silence(sockets["video"][0], wired)
            arrives("audio")
            start("video")
            api.streams.stop("audio")
            expect_silence(sockets["audio"][0], wired)
            arrives("video")
            api.streams.stop("video")
        with check("REST video start stops debug and leaves audio running"):
            start("audio")
            start("debug")
            arrives("debug")
            start("video")
            expect_silence(sockets["debug"][0], wired)
            arrives("video")
            arrives("audio")
        with check("DMA debug/video starts replace each other and leave audio running"):
            start("debug", "DMA")
            expect_silence(sockets["video"][0], wired)
            arrives("debug")
            arrives("audio")
            start("video", "DMA")
            expect_silence(sockets["debug"][0], wired)
            arrives("video")
            arrives("audio")
    finally:
        for name, (sock, _) in sockets.items():
            sock.close()
            teardown_step(f"stop {name}", lambda name=name: api.streams.stop(name))


def refusals(api: UltimateApi) -> None:
    for action in ("start", "stop"):
        with check(f"REST refuses unknown stream on {action}"):
            code, _, body = api.rest.request("PUT", f"/v1/streams/not-a-stream:{action}",
                                             params={"ip": "127.0.0.1"} if action == "start" else None)
            if code != 404 or b"Unrecognized stream name" not in body:
                raise Failure(f"HTTP {code}: {body!r}")
    for stream in STREAM_SOURCE:
        for params, expected in ((None, 400), ({"ip": "127.0.0.1:0"}, 400),
                                 ({"ip": "no-stream-receiver.invalid"}, 404)):
            with check(f"REST {stream}: refuses {params or 'missing ip'}"):
                code, _, body = api.rest.request("PUT", f"/v1/streams/{stream}:start",
                                                 params=params, timeout=20)
                if code != expected:
                    raise Failure(f"HTTP {code}, expected {expected}: {body!r}")


def debug_modes(api: UltimateApi, wired: str, wireless: str | None, local: str) -> None:
    item = api.configs.item("Data Streams", "Debug Stream Mode")
    original = str(item["current"])
    sock, port = receiver(None)
    try:
        for mode in item["values"]:
            with check(f"debug packets and stop in {mode} mode"):
                api.configs.set("Data Streams", "Debug Stream Mode", mode)
                api.streams.start("debug", ip=f"{local}:{port}")
                detail(expect_debug_data(sock, wired, wireless))
                api.streams.stop("debug")
                expect_silence(sock, wired)
    finally:
        sock.close()
        teardown_step("stop debug", lambda: api.streams.stop("debug"))
        teardown_step("restore debug mode",
                      lambda: api.configs.set("Data Streams", "Debug Stream Mode", original))


def menu_controls(api: UltimateApi, wired: str, wireless: str | None, local: str) -> None:
    # Accept the configured address unchanged: the string box persists it.
    for stream, label, item in (("video", "VIC Stream", "Stream VIC to"),
                                ("audio", "Audio Stream", "Stream Audio to"),
                                ("debug", "Debug Stream", "Stream Debug to")):
        destination = api.configs.current("Data Streams", item)
        host, colon, port_text = destination.partition(":")
        address = socket.gethostbyname(host)
        joined = address if ipaddress.ip_address(address).is_multicast else None
        if joined is None and address not in (local, "255.255.255.255"):
            check_start(f"{stream}: menu controls")
            check_skip("the configured receiver is another host; leave the saved destination intact")
            continue
        port = int(port_text) if colon else 11000 + STREAM_IDS[stream]
        sock, _ = receiver(joined, port)

        def action(start: bool, cancel: bool = False) -> None:
            browser = make_browser(MODE_OVERLAY, api.host, api.rest.password, api.rest.timeout)

            try:
                browser.backend.ensure_ready()
                categories = browser.open_task_menu()
                before = browser.rows()
                browser.choose_overlay_item(categories, "Developer" if stream == "debug" else "Streams")
                choices = browser.wait_for_overlay(before)
                matches = [choice for choice in choices if choice.endswith(label)]
                if len(matches) != 1:
                    raise Failure(f"expected one {label!r} entry, got {choices}")
                # These actions sit at the menu edges; paging skips disabled rows.
                edge = 0 if stream == "video" else -1
                if choices[edge] != matches[0]:
                    raise Failure(f"{label!r} is no longer at the expected menu edge: {choices}")
                browser.press(browser.backend.machine.page_up_key if edge == 0
                              else browser.backend.machine.page_down_key)
                browser.press("ENTER")
                if start:
                    if "Send to" not in browser.screen():
                        raise Failure(f"no destination dialog: {browser.screen()}")
                    browser.press("ESC" if cancel else "ENTER")
            finally:
                browser.close()

        try:
            api.streams.stop(stream)
            with check(f"{stream}: cancelling the menu destination starts nothing"):
                action(True, cancel=True)
                expect_silence(sock, wired)
            with check(f"{stream}: menu start and stop deliver packets, then silence"):
                action(True)
                detail(expect_stream(collect(sock, LISTEN_SECONDS), stream, wired, wireless))
                action(False)
                expect_silence(sock, wired)
        finally:
            sock.close()
            teardown_step(f"stop {stream}", lambda: api.streams.stop(stream))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    cli.add_device_arguments(parser)
    parser.add_argument("--receiver-name", help="DNS name that resolves to this test host on the device")
    args = parser.parse_args()
    password = args.password or None

    target = targets.resolve(args.host)
    api = UltimateApi(args.host, password, args.timeout)
    info = api.info()
    machine = machine_lib.identify(target.device, lambda: (str(info.product),
                                                           str(info.firmware_version)))
    if not machine.has_data_streams:
        suite_skip(SUITE, f"{machine.described} has no data streams")
        return 0

    failure = ""
    running: set[str] = set()
    cleanup_api = api
    try:
        before = senders_on_groups(target)
        code, body, seen = start_video(api, target.video_group)
        if code != 200:
            expect_refusal(code, body)
            suite_skip(SUITE, "the device has no wired address, which the streams need")
            return 0
        try:
            wired = stream_sender(seen)
        except Failure:
            api.streams.stop("video")
            raise
        running = {name for name, senders in before.items() if wired in senders}
        api.streams.stop("video")
        local = local_address(socket.gethostbyname(target.device))
        wireless = wireless_address(api, info, wired, local)
        detail(f"wired {wired}, wireless {wireless or 'none'}, this host {local}")
        apis = {"Ethernet": UltimateApi(wired, password, args.timeout)}
        cleanup_api = apis["Ethernet"]
        if wireless:
            apis["WiFi"] = UltimateApi(wireless, password, args.timeout)
        routing(apis, wired, wireless, local, target.video_group)
        lifecycle(api, wired, wireless, local, target.video_group, args.receiver_name)
        interactions(api, wired, wireless, local)
        refusals(api)
        debug_modes(api, wired, wireless, local)
        menu_controls(api, wired, wireless, local)
    except Failure as exc:
        failure = str(exc)
    except Exception as exc:  # noqa: BLE001
        failure = format_exception(exc)
    finally:
        for name in STREAM_SOURCE:
            teardown_step(f"stop {name}", lambda name=name: cleanup_api.streams.stop(name))
        # Leave the standard streams as they were found, for a recorder.
        for name in sorted(running):
            group_port = standard_groups(target)[name]
            teardown_step(f"restart {name}", lambda name=name, group_port=group_port:
                          cleanup_api.streams.start(name, ip=f"{group_port[0]}:{group_port[1]}"))

    if failure:
        suite_fail(SUITE, failure)
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
