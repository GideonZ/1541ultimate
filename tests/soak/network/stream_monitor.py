from __future__ import annotations

import enum
import errno
import socket
import struct
import threading
import time
from dataclasses import dataclass
from typing import Callable, Sequence

import dma_probe


DEFAULT_CONTROL_PORT = 64
DEFAULT_PACKET_TIMEOUT_S = 1.0
DEFAULT_STARTUP_GRACE_S = 2.0
DEFAULT_RECEIVE_BUFFER_BYTES = 2 * 1024 * 1024
AUDIO_START_RETRY_DELAY_S = 0.25
STREAM_CONTROL_RETRY_DELAYS_S = (0.05, 0.1)
MAX_ERROR_LOGS = 20
ERROR_LOG_INTERVAL = 100
ARP_PRIMER_PAYLOAD = b"\x00\x00"
VIDEO_PACKET_SIZE = 780
AUDIO_PACKET_SIZE = 770
DEBUG_PACKET_SIZE = 1444
VIDEO_HEADER_SIZE = 12
AUDIO_HEADER_SIZE = 2
DEBUG_HEADER_SIZE = 4
VIDEO_WIDTH = 384
VIDEO_LINES_PER_PACKET = 4
VIDEO_BITS_PER_PIXEL = 4
VIDEO_ENCODING = 0
VIDEO_MAX_LINE = 512
AUDIO_STEREO_SAMPLES_PER_PACKET = 192
DEBUG_MAX_ENTRIES_PER_PACKET = 360


class StreamKind(enum.StrEnum):
    VIDEO = "video"
    AUDIO = "audio"
    DEBUG = "debug"


STREAM_KIND_ORDER = (StreamKind.VIDEO, StreamKind.AUDIO, StreamKind.DEBUG)
STREAM_IDS = {
    StreamKind.VIDEO: 0,
    StreamKind.AUDIO: 1,
    StreamKind.DEBUG: 2,
}
STREAM_PACKET_SIZES = {
    StreamKind.VIDEO: VIDEO_PACKET_SIZE,
    StreamKind.AUDIO: AUDIO_PACKET_SIZE,
    StreamKind.DEBUG: DEBUG_PACKET_SIZE,
}
LOGGER = Callable[[str, str, str], None]


@dataclass(frozen=True)
class StreamRuntimeSettings:
    host: str
    control_port: int = DEFAULT_CONTROL_PORT
    packet_timeout_s: float = DEFAULT_PACKET_TIMEOUT_S
    startup_grace_s: float = DEFAULT_STARTUP_GRACE_S
    receive_buffer_bytes: int = DEFAULT_RECEIVE_BUFFER_BYTES
    network_password: str = ""


@dataclass(frozen=True)
class StreamSnapshot:
    kind: StreamKind
    status: str
    packets_received: int
    lost_packets: int
    reordered_packets: int
    size_errors: int
    header_errors: int
    structure_errors: int
    timeout_errors: int
    first_packet_at: float | None
    last_packet_at: float | None
    last_sequence: int | None
    last_error: str


def parse_stream_selection(values: Sequence[str]) -> tuple[StreamKind, ...]:
    if not values:
        return STREAM_KIND_ORDER
    seen: set[StreamKind] = set()
    ordered: list[StreamKind] = []
    for raw_value in values:
        kind = StreamKind(raw_value)
        if kind in seen:
            continue
        seen.add(kind)
        ordered.append(kind)
    return tuple(ordered)


def stream_summary_parts(snapshots: Sequence[StreamSnapshot]) -> tuple[str, ...]:
    return tuple(
        (
            f"stream_{snapshot.kind.value}="
            f"{snapshot.status},packets:{snapshot.packets_received},lost:{snapshot.lost_packets},"
            f"reordered:{snapshot.reordered_packets},size_errs:{snapshot.size_errors},"
            f"header_errs:{snapshot.header_errors},structure_errs:{snapshot.structure_errors},"
            f"timeout_errs:{snapshot.timeout_errors}"
        )
        for snapshot in snapshots
    )


def _default_logger(protocol: str, result: str, detail: str) -> None:
    sanitized = detail.replace('"', "'")
    print(f'{_ts()} protocol={protocol} result={result} detail="{sanitized}"', flush=True)


def _ts() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def _resolve_local_ip(host: str, port: int) -> str:
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect((host, port))
        return str(probe.getsockname()[0])
    finally:
        probe.close()


def _resolve_peer_ips(host: str, port: int) -> tuple[str, ...]:
    try:
        infos = socket.getaddrinfo(host, port, type=socket.SOCK_DGRAM)
    except socket.gaierror:
        return (host,)
    resolved = []
    for family, _socktype, _proto, _canonname, sockaddr in infos:
        if family != socket.AF_INET:
            continue
        resolved.append(str(sockaddr[0]))
    if not resolved:
        return (host,)
    return tuple(dict.fromkeys(resolved))


def _build_enable_command(kind: StreamKind, destination: str) -> bytes:
    payload = struct.pack("<H", 0) + destination.encode("ascii")
    return struct.pack("<HH", 0xFF20 + STREAM_IDS[kind], len(payload)) + payload


def _build_disable_command(kind: StreamKind) -> bytes:
    return struct.pack("<HH", 0xFF30 + STREAM_IDS[kind], 0)


def _is_retryable_stream_control_error(error: Exception) -> bool:
    if isinstance(error, (ConnectionResetError, BrokenPipeError, TimeoutError)):
        return True
    if isinstance(error, OSError):
        return error.errno in {errno.ECONNRESET, errno.EPIPE, errno.ECONNABORTED, errno.ETIMEDOUT}
    return False


def _send_command(settings: StreamRuntimeSettings, payload: bytes) -> None:
    last_error: Exception | None = None
    for delay_s in (*STREAM_CONTROL_RETRY_DELAYS_S, None):
        command_sock = socket.create_connection((settings.host, settings.control_port), timeout=2)
        try:
            dma_probe.authenticate_socket(command_sock, settings.network_password)
            command_sock.sendall(payload)
            return
        except Exception as error:
            last_error = error
            if delay_s is None or not _is_retryable_stream_control_error(error):
                raise
        finally:
            command_sock.close()
        time.sleep(delay_s)
    if last_error is not None:
        raise last_error


def _sequence_gap(expected: int, actual: int) -> tuple[int, bool]:
    forward = (actual - expected) & 0xFFFF
    if forward == 0:
        return 0, True
    if 0 < forward < 0x8000:
        return forward, True
    return 0, False


class StreamPacketTracker:
    def __init__(
        self,
        kind: StreamKind,
        *,
        packet_timeout_s: float = DEFAULT_PACKET_TIMEOUT_S,
        startup_grace_s: float = DEFAULT_STARTUP_GRACE_S,
        logger: LOGGER | None = None,
    ) -> None:
        self.kind = kind
        self.packet_timeout_s = packet_timeout_s
        self.startup_grace_s = startup_grace_s
        self.logger = logger or _default_logger
        self.started_at = time.monotonic()
        self.packets_received = 0
        self.lost_packets = 0
        self.reordered_packets = 0
        self.size_errors = 0
        self.header_errors = 0
        self.structure_errors = 0
        self.timeout_errors = 0
        self.first_packet_at: float | None = None
        self.last_packet_at: float | None = None
        self.last_sequence: int | None = None
        self.expected_sequence: int | None = None
        self.last_error = ""
        self._lock = threading.Lock()
        self._error_logs = 0
        self._startup_timeout_raised = False
        self._idle_timeout_raised = False
        self._video_last_frame: int | None = None
        self._video_last_line: int | None = None
        self._video_last_terminal = False

    def note_packet(self, payload: bytes, now: float | None = None) -> None:
        received_at = time.monotonic() if now is None else now
        with self._lock:
            self.packets_received += 1
            if self.first_packet_at is None:
                self.first_packet_at = received_at
            self.last_packet_at = received_at
            self._startup_timeout_raised = False
            self._idle_timeout_raised = False
            if self.kind == StreamKind.VIDEO:
                self._note_video_packet(payload)
            elif self.kind == StreamKind.AUDIO:
                self._note_audio_packet(payload)
            else:
                self._note_debug_packet(payload)

    def note_idle(self, now: float | None = None) -> None:
        idle_at = time.monotonic() if now is None else now
        with self._lock:
            if self.packets_received == 0:
                if idle_at - self.started_at >= self.startup_grace_s and not self._startup_timeout_raised:
                    self.timeout_errors += 1
                    self._startup_timeout_raised = True
                    self._record_error_locked("timeout", "no packets received before startup grace expired")
                return
            assert self.last_packet_at is not None
            if idle_at - self.last_packet_at >= self.packet_timeout_s and not self._idle_timeout_raised:
                self.timeout_errors += 1
                self._idle_timeout_raised = True
                self._record_error_locked("timeout", f"packet stall detected idle_s={idle_at - self.last_packet_at:.3f}")

    def snapshot(self, now: float | None = None) -> StreamSnapshot:
        current = time.monotonic() if now is None else now
        with self._lock:
            if self.packets_received == 0 and current - self.started_at < self.startup_grace_s:
                status = "STARTING"
            elif self._has_errors_locked():
                status = "FAIL"
            else:
                status = "OK"
            return StreamSnapshot(
                kind=self.kind,
                status=status,
                packets_received=self.packets_received,
                lost_packets=self.lost_packets,
                reordered_packets=self.reordered_packets,
                size_errors=self.size_errors,
                header_errors=self.header_errors,
                structure_errors=self.structure_errors,
                timeout_errors=self.timeout_errors,
                first_packet_at=self.first_packet_at,
                last_packet_at=self.last_packet_at,
                last_sequence=self.last_sequence,
                last_error=self.last_error,
            )

    def _has_errors_locked(self) -> bool:
        return any(
            (
                self.lost_packets,
                self.reordered_packets,
                self.size_errors,
                self.header_errors,
                self.structure_errors,
                self.timeout_errors,
            )
        )

    def _record_error_locked(self, category: str, detail: str) -> None:
        self.last_error = detail
        self._error_logs += 1
        if self._error_logs <= MAX_ERROR_LOGS or self._error_logs % ERROR_LOG_INTERVAL == 0:
            self.logger("stream", "FAIL", f"kind={self.kind.value} category={category} packets={self.packets_received} detail={detail}")

    def _note_sequence_locked(self, sequence: int) -> None:
        if self.expected_sequence is None:
            self.last_sequence = sequence
            self.expected_sequence = (sequence + 1) & 0xFFFF
            return
        expected_sequence = self.expected_sequence
        missing, is_forward = _sequence_gap(self.expected_sequence, sequence)
        if missing == 0 and is_forward:
            self.last_sequence = sequence
            self.expected_sequence = (sequence + 1) & 0xFFFF
            return
        if is_forward:
            self.lost_packets += missing
            self.last_sequence = sequence
            self.expected_sequence = (sequence + 1) & 0xFFFF
            self._record_error_locked("sequence", f"expected_seq={expected_sequence} got_seq={sequence} lost_packets={missing}")
            return
        self.reordered_packets += 1
        self._record_error_locked("sequence", f"expected_seq={expected_sequence} got_seq={sequence} reordered_or_duplicate=1")

    def _note_video_packet(self, payload: bytes) -> None:
        if len(payload) >= 2:
            self._note_sequence_locked(struct.unpack_from("<H", payload, 0)[0])
        if len(payload) != VIDEO_PACKET_SIZE:
            self.size_errors += 1
            self._record_error_locked("size", f"expected_bytes={VIDEO_PACKET_SIZE} got_bytes={len(payload)}")
            return
        frame, line_field, width = struct.unpack_from("<HHH", payload, 2)
        lines_per_packet, bits_per_pixel = struct.unpack_from("<BB", payload, 8)
        encoding = struct.unpack_from("<H", payload, 10)[0]
        if (
            width != VIDEO_WIDTH
            or lines_per_packet != VIDEO_LINES_PER_PACKET
            or bits_per_pixel != VIDEO_BITS_PER_PIXEL
            or encoding != VIDEO_ENCODING
        ):
            self.header_errors += 1
            self._record_error_locked(
                "header",
                f"width={width} lines_per_packet={lines_per_packet} bits_per_pixel={bits_per_pixel} encoding={encoding}",
            )
        line_number = line_field & 0x7FFF
        is_terminal_packet = bool(line_field & 0x8000)
        if line_number % VIDEO_LINES_PER_PACKET != 0 or line_number >= VIDEO_MAX_LINE:
            self.structure_errors += 1
            self._record_error_locked("structure", f"frame={frame} line={line_number} terminal={int(is_terminal_packet)}")
        elif self._video_last_frame is not None:
            if frame == self._video_last_frame:
                expected_line = self._video_last_line + VIDEO_LINES_PER_PACKET if self._video_last_line is not None else None
                if self._video_last_terminal:
                    self.structure_errors += 1
                    self._record_error_locked("structure", f"frame={frame} repeated_after_terminal=1 line={line_number}")
                elif expected_line is not None and line_number != expected_line:
                    self.structure_errors += 1
                    self._record_error_locked("structure", f"frame={frame} expected_line={expected_line} got_line={line_number}")
            elif frame == ((self._video_last_frame + 1) & 0xFFFF) and not self._video_last_terminal:
                self.structure_errors += 1
                self._record_error_locked(
                    "structure",
                    f"frame_advanced_without_terminal previous_frame={self._video_last_frame} got_frame={frame}",
                )
        self._video_last_frame = frame
        self._video_last_line = line_number
        self._video_last_terminal = is_terminal_packet

    def _note_audio_packet(self, payload: bytes) -> None:
        if len(payload) >= AUDIO_HEADER_SIZE:
            self._note_sequence_locked(struct.unpack_from("<H", payload, 0)[0])
        if len(payload) != AUDIO_PACKET_SIZE:
            self.size_errors += 1
            self._record_error_locked("size", f"expected_bytes={AUDIO_PACKET_SIZE} got_bytes={len(payload)}")
            return
        sample_bytes = len(payload) - AUDIO_HEADER_SIZE
        if sample_bytes != AUDIO_STEREO_SAMPLES_PER_PACKET * 4:
            self.structure_errors += 1
            self._record_error_locked(
                "structure",
                f"expected_sample_bytes={AUDIO_STEREO_SAMPLES_PER_PACKET * 4} got_sample_bytes={sample_bytes}",
            )

    def _note_debug_packet(self, payload: bytes) -> None:
        if len(payload) < DEBUG_HEADER_SIZE:
            self.size_errors += 1
            self._record_error_locked("size", f"expected_min_bytes={DEBUG_HEADER_SIZE} got_bytes={len(payload)}")
            return
        sequence, reserved = struct.unpack_from("<HH", payload, 0)
        self._note_sequence_locked(sequence)
        if len(payload) > DEBUG_PACKET_SIZE:
            self.size_errors += 1
            self._record_error_locked("size", f"expected_max_bytes={DEBUG_PACKET_SIZE} got_bytes={len(payload)}")
            return
        if reserved != 0:
            self.header_errors += 1
            self._record_error_locked("header", f"reserved={reserved}")
        payload_bytes = len(payload) - DEBUG_HEADER_SIZE
        entry_count, remainder = divmod(payload_bytes, 4)
        if remainder != 0 or entry_count < 1 or entry_count > DEBUG_MAX_ENTRIES_PER_PACKET:
            self.structure_errors += 1
            self._record_error_locked(
                "structure",
                f"entry_count={entry_count} payload_bytes={payload_bytes} max_entries={DEBUG_MAX_ENTRIES_PER_PACKET}",
            )


class _StreamReceiver(threading.Thread):
    def __init__(self, sock: socket.socket, tracker: StreamPacketTracker, stop_event: threading.Event, allowed_sources: set[str]) -> None:
        super().__init__(daemon=True)
        self.sock = sock
        self.tracker = tracker
        self.stop_event = stop_event
        self.allowed_sources = allowed_sources

    def run(self) -> None:
        while not self.stop_event.is_set():
            try:
                payload, address = self.sock.recvfrom(STREAM_PACKET_SIZES[self.tracker.kind] + 64)
            except socket.timeout:
                self.tracker.note_idle()
                continue
            except OSError:
                if self.stop_event.is_set():
                    return
                self.tracker.note_idle()
                continue
            if self.allowed_sources and address[0] not in self.allowed_sources:
                continue
            # DataStreamer sends this probe while resolving the destination's
            # MAC address, before enabling the actual stream.
            if payload == ARP_PRIMER_PAYLOAD:
                continue
            self.tracker.note_packet(payload)


class StreamMonitor:
    def __init__(self, settings: StreamRuntimeSettings, streams: Sequence[StreamKind], *, logger: LOGGER | None = None) -> None:
        self.settings = settings
        self.streams = tuple(streams)
        self.logger = logger or _default_logger
        self.local_ip = _resolve_local_ip(settings.host, settings.control_port)
        self.peer_ips = _resolve_peer_ips(settings.host, settings.control_port)
        self._stop_event = threading.Event()
        self._sockets: dict[StreamKind, socket.socket] = {}
        self._threads: dict[StreamKind, _StreamReceiver] = {}
        self._trackers: dict[StreamKind, StreamPacketTracker] = {}

    def _destination_for(self, kind: StreamKind) -> str:
        return f"{self.local_ip}:{self._sockets[kind].getsockname()[1]}"

    def _enable_stream(self, kind: StreamKind, *, retry: bool = False) -> None:
        destination = self._destination_for(kind)
        _send_command(self.settings, _build_enable_command(kind, destination))
        action = "re-enabled" if retry else "enabled"
        detail = f"kind={kind.value} {action} destination={destination}"
        if retry:
            detail += " reason=startup_silence"
        self.logger("stream", "INFO", detail)

    def _retry_audio_start_if_silent(self) -> None:
        if StreamKind.AUDIO not in self._trackers:
            return
        time.sleep(AUDIO_START_RETRY_DELAY_S)
        if self._trackers[StreamKind.AUDIO].snapshot().packets_received != 0:
            return
        self._enable_stream(StreamKind.AUDIO, retry=True)

    def start(self) -> None:
        for kind in self.streams:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, self.settings.receive_buffer_bytes)
            sock.settimeout(min(self.settings.packet_timeout_s / 2.0, 0.25))
            sock.bind((self.local_ip, 0))
            tracker = StreamPacketTracker(
                kind,
                packet_timeout_s=self.settings.packet_timeout_s,
                startup_grace_s=self.settings.startup_grace_s,
                logger=self.logger,
            )
            receiver = _StreamReceiver(sock, tracker, self._stop_event, set(self.peer_ips))
            self._sockets[kind] = sock
            self._trackers[kind] = tracker
            self._threads[kind] = receiver
            self.logger("stream", "INFO", f"kind={kind.value} listening={self.local_ip}:{sock.getsockname()[1]} expected_sources={','.join(self.peer_ips)}")
            receiver.start()
        try:
            for kind in self.streams:
                self._enable_stream(kind)
            self._retry_audio_start_if_silent()
        except Exception:
            self.stop()
            raise

    def stop(self) -> None:
        self._stop_event.set()
        for kind in self.streams:
            try:
                _send_command(self.settings, _build_disable_command(kind))
                self.logger("stream", "INFO", f"kind={kind.value} disabled")
            except Exception:
                continue
        for sock in self._sockets.values():
            try:
                sock.close()
            except OSError:
                pass
        for thread in self._threads.values():
            thread.join(timeout=1)

    def snapshots(self) -> tuple[StreamSnapshot, ...]:
        return tuple(self._trackers[kind].snapshot() for kind in self.streams)
