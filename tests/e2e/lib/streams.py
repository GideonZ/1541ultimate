"""Everything about the device's video and audio streams, in one place.

`vic_video.py`, `av_stream.py` and the recorder are all callers of this. The
duplication they had before had already diverged: two sets of group and port
constants, one socket setting `SO_REUSEADDR` and one setting nothing, one
filtering by source address and one not, and one assembling frames by
concatenating payloads in arrival order. Three implementations of one wire
format is the state `tests/lib/check_transport_usage.py` exists because the
HTTP client reached.

What this owns: the wire constants, socket creation, source-address filtering,
frame assembly, the audio concealment timeline, and the arming discipline. What
it does not own is what a caller does with a frame. A suite asserting that two
frames differ and a recorder composing a pane share the decode and share
nothing else.

There is no shared socket, and there is deliberately no attempt at one.
Multicast is delivered to every subscriber that joined, so a suite and a
recorder each having their own socket is correct and costs the device nothing.
What is shared is the code and the arming.

Two rules about the addressing, both of which look like accidents and are not:

- **Every target streams to the one group and port pair.** `streams:start` sets
  where the device sends, and three suites start the streams themselves at the
  fixed addresses, so a receiver that had asked for a different port would lose
  the stream the moment one of them ran and would not get it back. The vendor's
  documentation adds the second reason: multicast forwarding is decided by
  group address alone and ignores the UDP port, so two ports on one group are
  not separated by the network either. Devices are separated by the source
  address of each packet.
- **Two sockets share the port on purpose.** `SO_REUSEADDR` is what lets a
  suite and a recorder both receive the same multicast stream; on the BSD stack
  macOS uses, `SO_REUSEPORT` is what does it, and `socket.SO_REUSEPORT` is not
  defined on every platform Python builds for, so both are set and the second
  is behind a guard. This is a multicast property and it does not generalise: two
  sockets sharing a port for unicast split the datagrams between them, which is
  why exactly one process binds the syslog port.
"""

import os
import socket
import struct
import sys
from array import array
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Sequence, Set

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "lib"))

import targets as targets_lib  # noqa: E402
from report import Failure  # noqa: E402

import screens as screen_spool  # noqa: E402

# The wire formats, from the vendor's data-streams documentation and confirmed
# against what the device sends.
VIDEO_PACKET_BYTES = 780
VIDEO_HEADER_BYTES = 12
VIDEO_LINE_BYTES = 192
AUDIO_PACKET_BYTES = 770
AUDIO_HEADER_BYTES = 2

# Re-exported so a caller has one name for each.
VIDEO_GROUP = targets_lib.VIDEO_GROUP
VIDEO_PORT = targets_lib.VIDEO_PORT
AUDIO_GROUP = targets_lib.AUDIO_GROUP
AUDIO_PORT = targets_lib.AUDIO_PORT


def stream_socket(group: str, port: int, timeout: Optional[float] = 2.0) -> socket.socket:
    """A socket receiving one stream, sharing its port with anything else.

    Joins `group` when it is a multicast address. A unicast address is a real
    mode the vendor's documentation lists and recommends where IGMP snooping is
    absent, and it is what a loopback stand-in for a device sends to, so the
    join is conditional rather than assumed.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        # macOS needs this to let two sockets share the port; Linux needs only
        # SO_REUSEADDR, and some platforms define neither.
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except OSError:
            pass
    sock.bind(("", port))
    if is_multicast(group):
        # INADDR_ANY takes the interface from the routing table, which is
        # correct on a host with one LAN interface and is what both the runner
        # host and a developer laptop have.
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                        struct.pack("4sL", socket.inet_aton(group),
                                    socket.INADDR_ANY))
    if timeout is not None:
        sock.settimeout(timeout)
    return sock


def is_multicast(address: str) -> bool:
    """Whether `address` is in 224.0.0.0/4."""
    try:
        first = int(address.split(".", 1)[0])
    except (ValueError, IndexError):
        return False
    return 224 <= first <= 239


def source_addresses(host) -> Set[str]:
    """Every address whose packets count as this device's.

    A second machine streaming into the same group is indistinguishable from
    this one without it: measured with two Ultimates sending at once, the
    result is twice the packet rate, two independent 16-bit sequence counters
    interleaved, and every packet in order with zero apparent loss from each
    sender's point of view. Nothing in the receive path looks wrong.
    """
    name = targets_lib.resolve(host).device if not isinstance(host, str) else host
    try:
        found = socket.getaddrinfo(name, 0, socket.AF_INET, socket.SOCK_DGRAM)
    except OSError:
        return set()
    return {entry[4][0] for entry in found}


class Arming:
    """Start a stream only when it is not already running, and stop only what
    this started.

    A suite may take the stream at any moment, and it wins: it gets the stream
    immediately, and this is the only party that ever waits. What this does
    instead is leave the streams as it found them, and write down every arm and
    stop so a reader can tell "the av suite took the stream here" from an
    unexplained gap.
    """

    def __init__(self, api, target=None) -> None:
        self.api = api
        self.target = targets_lib.resolve(target) if target is not None else None
        self.started: Set[str] = set()
        self.arms: Dict[str, int] = {}
        self.failures: Dict[str, str] = {}

    def address(self, stream: str) -> str:
        handle = self.target
        if stream == "audio":
            group = handle.audio_group if handle else AUDIO_GROUP
            port = handle.audio_port if handle else AUDIO_PORT
        else:
            group = handle.video_group if handle else VIDEO_GROUP
            port = handle.video_port if handle else VIDEO_PORT
        return f"{group}:{port}"

    def start(self, stream: str, already_arriving: bool = False) -> bool:
        """Ask the device to send `stream`, unless it already is.

        `already_arriving` is the caller saying it has seen packets from its
        own device at the standard address. That is the one thing about a
        stream that is not free to ask for twice, so a caller that finds it
        running issues no request at all and, by not having started it, leaves
        it running afterwards.
        """
        if already_arriving or stream in self.started:
            return False
        try:
            self.api.streams.start(stream, ip=self.address(stream))
        except Failure as exc:
            self.failures[stream] = str(exc)
            self.publish("start-failed", stream)
            return False
        self.started.add(stream)
        self.arms[stream] = self.arms.get(stream, 0) + 1
        self.publish("start", stream)
        return True

    def stop(self, stream: str) -> bool:
        """Stop `stream`, and only when this started it."""
        if stream not in self.started:
            return False
        self.started.discard(stream)
        try:
            self.api.streams.stop(stream)
        except Failure as exc:
            self.failures[stream] = str(exc)
            return False
        self.publish("stop", stream)
        return True

    def stop_all(self) -> None:
        # Audio first, so a failure to stop it still leaves the video stop to
        # run: a stream left running floods the LAN until somebody notices.
        for stream in ("audio", "video"):
            self.stop(stream)

    def publish(self, action: str, stream: str) -> None:
        """Record one arm or stop, so a reader can attribute a gap to a suite."""
        screen_spool.publish_event("stream", action=action, stream=stream,
                                   address=self.address(stream))

    def __enter__(self) -> "Arming":
        return self

    def __exit__(self, *_exc) -> None:
        self.stop_all()


def receive(sockets: Sequence[socket.socket], addresses: Set[str],
            timeout: float) -> Iterable:
    """Every packet from `addresses` that arrives within `timeout`.

    Yields `(socket, data)`. A packet from anywhere else is not this device's
    and is dropped by the caller, which counts it: on a multi-target run the
    other sender is another target of the same run, and stopping it would break
    that recording.
    """
    import select
    import time

    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return
        ready, _, _ = select.select(list(sockets), (), (), remaining)
        for sock in ready:
            try:
                data, sender = sock.recvfrom(2048)
            except OSError:
                continue
            yield sock, data, sender[0] in addresses


# -------------------------------------------------------------------------
# The video wire format, and assembling frames from it
# -------------------------------------------------------------------------


"""Reassemble VIC video frames from the raw UDP packet stream.

Each datagram on the wire is 12 bytes of header followed by 768 bytes of
4-bit-per-pixel VIC colour indices, packed two pixels to a byte, low nibble
first. A frame is many packets; packets can be lost, duplicated or delivered
out of order by the network, and the 16-bit sequence and frame counters both
wrap. This module turns that packet stream into complete, correctly placed
`Frame` objects and keeps loss/reorder counters that account for every
packet pushed into it.
"""

# Wire layout, all little-endian (see module docstring). "<HHHHBBH" reads:
# seq(2) frame(2) line+last-flag(2) width(2) lines_per_packet(1) bits_per_pixel(1) encoding(2)
# which is exactly the 12 header bytes; struct.calcsize confirms this at import time below.
_HEADER_FORMAT = "<HHHHBBH"
HEADER_SIZE = struct.calcsize(_HEADER_FORMAT)

# Fixed by the hardware encoder, not negotiated, so a packet advertising any
# other value is not a packet from this stream and gets dropped as malformed.
PIXELS_PER_LINE = 384
LINES_PER_PACKET = 4
BITS_PER_PIXEL = 4
ENCODING = 0

# 4 bits per pixel packs 2 pixels per byte, so a line of 384 pixels is 192
# packed bytes. 4 lines per packet times 192 bytes per line is 768, the
# payload size; 12 header bytes plus that payload is the 780-byte datagram.
BYTES_PER_LINE = PIXELS_PER_LINE // 2
PAYLOAD_SIZE = LINES_PER_PACKET * BYTES_PER_LINE
PACKET_SIZE = HEADER_SIZE + PAYLOAD_SIZE

# The two frame heights the hardware actually produces: PAL is 272 lines,
# NTSC is 240. The last packet's declared height is clamped to this range
# (see _clamp_height) so a corrupt "line" field cannot grow the frame buffer
# past what real hardware ever sends.
HEIGHT_PAL = 272
HEIGHT_NTSC = 240

# Bit 15 of the header's line field marks the last packet of a frame; the
# line number itself is the low 15 bits.
_LAST_PACKET_FLAG = 0x8000
_LINE_MASK = 0x7FFF

# A 16-bit counter that wraps to 0 after 65535. Used to sign-extend the
# difference between two wrapping counters (see _wrapped_gap).
_COUNTER_MODULUS = 0x10000
_COUNTER_HALF = 0x8000


@dataclass(frozen=True)
class Frame:
    """One assembled VIC video frame.

    `packed` holds the 4-bit colour indices two to a byte exactly as they
    arrived on the wire, `height * BYTES_PER_LINE` of them; `unpack` expands
    it to one colour index per byte. `complete` is what the assembler only
    ever produces, and is a field so a caller building a frame by hand can say
    otherwise.
    """

    number: int
    width: int
    height: int
    packed: bytes
    complete: bool


# Built once: the tables are the same on every call and rebuilding two
# 256-byte tables per frame at the output rate is work for nothing.
_LOW_NIBBLE_TABLE = bytes(byte & 0x0F for byte in range(256))
_HIGH_NIBBLE_TABLE = bytes(byte >> 4 for byte in range(256))


def unpack(packed: bytes) -> bytes:
    """Expand packed 4-bit colour indices to one index per byte, low nibble first.

    A PAL frame is 384*272 = 104448 pixels, delivered 50 times a second; an
    interpreted per-pixel Python loop over that many pixels every 20ms is not
    affordable on a Raspberry Pi. `bytes.translate` runs its lookup at C
    speed over the whole buffer in one call, so two translate passes (one
    selecting the low nibble of each packed byte, one the high nibble) plus
    two strided slice assignments do the same work as 104448 individual
    pixel writes in three whole-buffer passes instead of one interpreted loop
    per pixel.
    """
    low_nibbles = packed.translate(_LOW_NIBBLE_TABLE)
    high_nibbles = packed.translate(_HIGH_NIBBLE_TABLE)

    out = bytearray(len(packed) * 2)
    out[0::2] = low_nibbles   # even pixels: low nibble of each packed byte, arrives first on the wire
    out[1::2] = high_nibbles  # odd pixels: high nibble of each packed byte
    return bytes(out)


def _clamp_height(candidate: int) -> int:
    """Bound a last-packet-derived height to the hardware's two real heights.

    `candidate` is `line + LINES_PER_PACKET` from the last packet. For real
    PAL/NTSC traffic this is exactly 272 or 240; clamping it into
    [HEIGHT_NTSC, HEIGHT_PAL] stops a corrupt line number from growing the
    frame buffer past the largest size the hardware ever sends, or shrinking
    it below the smallest.
    """
    return min(max(candidate, HEIGHT_NTSC), HEIGHT_PAL)


def _wrapped_gap(current: int, previous: int) -> int:
    """Signed distance from `previous` to `current` on a wrapping 16-bit counter.

    A raw `current - previous` reports a ~65535 loss every time either the
    packet sequence number or the frame number wraps past 65535 back to 0,
    because the wrap looks like a huge backward jump. Sign-extending the
    difference through the counter's half-range instead treats a wrap as the
    small forward step it actually is: with current=0, previous=65535 this
    returns +1, not -65535. A positive result is forward progress (the gap,
    in counter units); zero or negative means `current` arrived out of order
    behind `previous`.
    """
    return ((current - previous + _COUNTER_HALF) % _COUNTER_MODULUS) - _COUNTER_HALF


class _FrameBuilder:
    """Accumulates the packets of a single, still-in-progress frame."""

    def __init__(self, frame_number: int) -> None:
        self.frame_number = frame_number
        # Sized to the largest frame the hardware ever sends (PAL, 272
        # lines) so any valid line offset can be written without a resize;
        # frames shorter than that (NTSC) simply leave the tail unused and
        # it gets sliced off in `_finish`.
        self.buffer = bytearray(HEIGHT_PAL * BYTES_PER_LINE)
        self.received_lines: Set[int] = set()
        self.height: Optional[int] = None  # known only once the last packet arrives
        self.last_seen = False

    def add_packet(self, line: int, is_last: bool, payload: bytes) -> None:
        offset = line * BYTES_PER_LINE
        self.buffer[offset:offset + len(payload)] = payload
        self.received_lines.add(line)
        if is_last:
            self.height = _clamp_height(line + LINES_PER_PACKET)
            self.last_seen = True

    def is_complete(self) -> bool:
        if not self.last_seen or self.height is None:
            return False
        needed = range(0, self.height, LINES_PER_PACKET)
        return all(line in self.received_lines for line in needed)

    def finish(self) -> Frame:
        assert self.height is not None
        end = self.height * BYTES_PER_LINE
        return Frame(
            number=self.frame_number,
            width=PIXELS_PER_LINE,
            height=self.height,
            packed=bytes(self.buffer[:end]),
            complete=True,
        )


class FrameAssembler:
    """Reassembles VIC video frames from a stream of UDP datagrams.

    Feed each datagram to `push` in the order it is received (including out
    of order and duplicated ones; the caller need not pre-sort). It returns
    the `Frame` a packet completes, or None while a frame is still in
    progress or the packet was dropped. `counts` reports totals for loss
    accounting.
    """

    def __init__(self) -> None:
        self._builder: Optional[_FrameBuilder] = None
        self._last_seq: Optional[int] = None
        self._last_completed_frame: Optional[int] = None
        self._counts: Dict[str, int] = {
            "packets": 0,
            "packets_dropped": 0,
            "packets_malformed": 0,
            "frames_completed": 0,
            "frames_lost": 0,
            "frames_reordered": 0,
        }

    def push(self, data: bytes) -> Optional[Frame]:
        """Take one datagram. Return the frame it completed, or None."""
        self._counts["packets"] += 1

        if len(data) != PACKET_SIZE:
            # Cannot even trust the header is present at the expected
            # offsets, so no field is inspected; not this stream.
            self._counts["packets_malformed"] += 1
            return None

        seq, frame_number, line_and_flag, width, lines_per_packet, bits_per_pixel, encoding = (
            struct.unpack(_HEADER_FORMAT, data[:HEADER_SIZE])
        )
        line = line_and_flag & _LINE_MASK
        is_last = bool(line_and_flag & _LAST_PACKET_FLAG)

        if (
            width != PIXELS_PER_LINE
            or lines_per_packet != LINES_PER_PACKET
            or bits_per_pixel != BITS_PER_PIXEL
            or encoding != ENCODING
            # A line whose packet would run past the largest real frame
            # (272 lines) cannot come from this hardware; without this
            # guard a corrupt line field would write past the fixed-size
            # frame buffer in _FrameBuilder.
            or line + LINES_PER_PACKET > HEIGHT_PAL
        ):
            self._counts["packets_malformed"] += 1
            return None

        self._account_packet_sequence(seq)

        if self._builder is None or self._builder.frame_number != frame_number:
            # Either the first packet seen, or a packet for a different
            # frame than the one in progress. The frame in progress (if any)
            # never got its last packet and all its lines while it was
            # current, so it is abandoned rather than force-completed: a
            # short frame would misreport what the hardware actually sent.
            self._builder = _FrameBuilder(frame_number)

        payload = data[HEADER_SIZE:]
        self._builder.add_packet(line, is_last, payload)

        if not self._builder.is_complete():
            return None

        frame = self._builder.finish()
        self._builder = None
        self._account_completed_frame(frame.number)
        return frame

    def counts(self) -> Dict[str, int]:
        """Packet/frame accounting since construction; see the class docstring."""
        return dict(self._counts)

    def _account_packet_sequence(self, seq: int) -> None:
        if self._last_seq is not None:
            gap = _wrapped_gap(seq, self._last_seq)
            if gap > 1:
                # gap - 1 packets between the last one counted and this one
                # never arrived at all.
                self._counts["packets_dropped"] += gap - 1
            if gap <= 0:
                # This packet is behind the newest one seen so far (delayed
                # or duplicated); it does not represent forward progress, so
                # it must not move the baseline backward or a later,
                # genuinely-forward packet would recompute an inflated gap.
                return
        self._last_seq = seq

    def _account_completed_frame(self, frame_number: int) -> None:
        self._counts["frames_completed"] += 1
        if self._last_completed_frame is None:
            self._last_completed_frame = frame_number
            return
        gap = _wrapped_gap(frame_number, self._last_completed_frame)
        if gap <= 0:
            # A frame that finished assembling after a later frame number
            # already completed. Its number is behind the completed
            # baseline, so it is reordering, not loss, and the baseline must
            # not move backward: doing so would make the next frame that
            # completes in forward order recompute the gap the earlier,
            # already-counted frame already accounted for.
            self._counts["frames_reordered"] += 1
            return
        if gap > 1:
            self._counts["frames_lost"] += gap - 1
        self._last_completed_frame = frame_number


# -------------------------------------------------------------------------
# The audio wire format, and the timeline that conceals its gaps
# -------------------------------------------------------------------------


"""Concealment timeline for the device's UDP audio stream.

The stream carries no timestamp. Each datagram is 770 bytes: a 2-byte
little-endian sequence number followed by 768 bytes of PCM (192 stereo
frames, 16-bit signed little-endian, left then right per frame). The
sequence number is the only thing that says where a packet belongs in
time, so it is the clock this module reconstructs against.

The sample rate is derived from the video clock, not a fixed 48 kHz, and
differs between PAL and NTSC (see RATE_PAL_HZ / RATE_NTSC_HZ below). That
matters for callers computing durations from a frame count; it does not
change how this module orders packets, since ordering runs purely off the
16-bit sequence counter.

AudioTimeline takes packets in arrival order (which need not be sequence
order) and produces the PCM that should be appended to a continuous
recording, concealing loss with a decaying fill instead of leaving a gap
or a hole of zeros.
"""

# Wire format
#
# These are fixed by the device's UDP audio protocol, not tunable.

HEADER_BYTES = 2  # one u16le sequence number per datagram
CHANNELS = 2  # stereo
BYTES_PER_SAMPLE = 2  # s16le
FRAME_BYTES = CHANNELS * BYTES_PER_SAMPLE  # 4 bytes per interleaved L/R frame
FRAMES_PER_PACKET = 192  # fixed by the device; ~4ms of audio per datagram
PAYLOAD_BYTES = FRAMES_PER_PACKET * FRAME_BYTES  # 768
PACKET_BYTES = HEADER_BYTES + PAYLOAD_BYTES  # 770, the only valid datagram size

# The sequence number is a 16-bit counter, so it wraps at 65536 and every
# delta between packets must be taken modulo this to mean anything.
SEQ_MODULUS = 1 << 16

# Sample rates
#
# Both rates are derived from the respective video/CPU clock, not from an
# audio clock, which is why they are not round numbers and not 48000. They
# come from the device's own measured/reported timing for the audio-out
# feature and are treated as fixed constants of the hardware, not something
# this module can compute from first principles.

RATE_PAL_HZ = 47982.8869047619  # PAL clock derivation
RATE_NTSC_HZ = 47940.3408482143  # NTSC clock derivation, a different clock than PAL


def rate_for(pal: bool) -> float:
    """Return the stream's sample rate in Hz for the given video timing.

    Pass True for a PAL device, False for NTSC. The two differ because the
    audio sample clock is derived from the video clock, and PAL and NTSC
    run at different video clocks.
    """
    return RATE_PAL_HZ if pal else RATE_NTSC_HZ


# Concealment tuning
#
# LATE_REORDER_WINDOW_PACKETS: how far behind the last written sequence
# number a packet may be before it stops looking like ordinary UDP
# reordering/duplication and starts looking like the counter itself
# resetting (device restart). Small UDP jitter reorders packets by at most
# a handful of positions in practice; 16 packets (~64ms) is generously
# above that while still being nowhere near the scale of a restart, where
# the delta is typically tens of thousands.
LATE_REORDER_WINDOW_PACKETS = 16

# FILL_CAP_PACKETS: the largest forward gap this module will conceal by
# synthesising fill, rather than treating as a re-anchor. This module
# writes to a file, not a live socket, so the cost of concealing too long
# is a few seconds of faded-out audio sitting in the recording; the cost of
# re-anchoring too eagerly is a discontinuity (the file loses sync with
# whatever else is being recorded alongside it, e.g. video). A file can
# absorb the former far better than the latter, so the cap is set generously:
# 2500 packets is ~10.0s of audio (2500 * 192 frames / 47982.887 Hz), which
# covers a long stall (USB hiccup, buffer overrun recovery) without ever
# synthesising minutes of silence for what is actually a device restart. A
# live mirror would want a cap orders of magnitude smaller, since concealing
# ten seconds of audio live is worse than the discontinuity a re-anchor
# would cause; this module targets the file-writer case.
FILL_CAP_PACKETS = 2500

# RAMP_IN_FRAMES: length, in frames, of the short linear ramp at the very
# end of a fill that carries the signal from (near) zero into the first
# real sample of the packet that ends the gap. Kept well under one packet
# (192 frames) so that even a single-packet gap, the smallest gap this
# module ever conceals, still has most of its length doing the "hold and
# fade" and only a small tail doing the "ramp into the next real packet".
RAMP_IN_FRAMES = 32

_SAMPLE_MIN = -32768
_SAMPLE_MAX = 32767


def _clamp16(value: float) -> int:
    """Clamp a rounded sample value into the s16 range.

    Interpolation between two in-range samples cannot overflow s16 in
    theory, but this guards against float rounding landing one unit past
    an endpoint.
    """
    v = round(value)
    if v < _SAMPLE_MIN:
        return _SAMPLE_MIN
    if v > _SAMPLE_MAX:
        return _SAMPLE_MAX
    return v


def _fade_channel(last_sample: int, next_sample: int, n: int) -> List[int]:
    """Build n concealment samples for one channel bridging a gap.

    The shape is: hold-and-fade the last real sample toward zero for most
    of the gap (this is REQUIREMENT: real SID output rides on a DC offset,
    so silence is not zero for this signal; fading toward zero rather than
    stepping to it avoids a click at the start of the gap), then a short
    linear ramp (RAMP_IN_FRAMES) from near-zero into next_sample so the
    packet that ends the gap is not itself a step away from the fill.

    If the gap is too short to fit both phases, the whole thing is one
    ramp directly from last_sample to next_sample; this only happens for
    gaps shorter than RAMP_IN_FRAMES, which no forward-gap concealment in
    push() ever produces (the shortest concealed gap is one full packet,
    192 frames), but silence() can ask for an arbitrarily small n.
    """
    if n <= 0:
        return []
    if n <= RAMP_IN_FRAMES:
        return [
            _clamp16(last_sample + (next_sample - last_sample) * (i + 1) / (n + 1))
            for i in range(n)
        ]
    fade_len = n - RAMP_IN_FRAMES
    # i=0 reproduces last_sample exactly, so the first fill sample is a
    # step-free continuation of the last real sample. It decays toward
    # (not necessarily to) zero by the end of the fade phase.
    fade = [_clamp16(last_sample * (fade_len - i) / fade_len) for i in range(fade_len)]
    # j=RAMP_IN_FRAMES-1 reproduces next_sample exactly, so the last fill
    # sample is a step-free lead-in to the real packet that follows.
    ramp = [_clamp16(next_sample * (j + 1) / RAMP_IN_FRAMES) for j in range(RAMP_IN_FRAMES)]
    return fade + ramp


def _fade_to_silence(last_sample: int, n: int) -> List[int]:
    """Build n concealment samples for one channel fading toward zero.

    Used by silence(), which has no known future sample to ramp into
    (nothing has arrived yet), so it only ever does the hold-and-fade
    half of _fade_channel's shape.
    """
    if n <= 0:
        return []
    return [_clamp16(last_sample * (n - i) / n) for i in range(n)]


def _interleave(left: List[int], right: List[int]) -> bytes:
    """Pack per-channel int16 sample lists into s16le interleaved stereo bytes."""
    n = len(left)
    frames = [0] * (2 * n)
    frames[0::2] = left
    frames[1::2] = right
    arr = array("h", frames)
    if sys.byteorder != "little":
        arr.byteswap()
    return arr.tobytes()


def _signed_delta(seq: int, last_seq: int) -> int:
    """Sequence delta, sign-extended from the 16-bit counter.

    The counter wraps at SEQ_MODULUS, so a naive (seq - last_seq) is
    meaningless once either side has wrapped. Taking the difference modulo
    2**16 and then sign-extending the top bit gives the delta a device
    would have actually produced: +1 for the next packet including across
    a wrap (65535 -> 0), and a small negative number for reordering rather
    than a near-65536 "forward" jump for what is really one packet behind.
    """
    diff = (seq - last_seq) & 0xFFFF
    if diff >= 0x8000:
        diff -= 0x10000
    return diff


@dataclass(frozen=True)
class Written:
    """Result of pushing one packet: what now belongs in the file.

    pcm is s16le interleaved stereo, ready to append to the output file
    as-is; it may be empty (a discarded late/duplicate packet, or a
    malformed one). concealed_packets counts how many packets' worth of
    pcm were synthesised rather than received.
    """

    pcm: bytes
    concealed_packets: int


class AudioTimeline:
    """Reassembles a lossy, out-of-order UDP audio stream into continuous PCM.

    The sequence number in each packet is treated as the sole clock. Loss
    is concealed with a decaying fill rather than left as a hole or
    zero-filled, late/duplicate packets are dropped without disturbing the
    timeline, and gaps or backward jumps too large to be ordinary loss
    cause the timeline to re-anchor on the new packet rather than try to
    bridge them. See module docstring and the constants above for the
    reasoning behind each threshold.
    """

    def __init__(self) -> None:
        self._has_anchor = False
        self._last_seq = 0
        self._has_samples = False
        self._last_l = 0
        self._last_r = 0
        self._counts: Dict[str, int] = {
            "packets": 0,
            "packets_written": 0,
            "packets_lost": 0,
            "packets_concealed": 0,
            "late_dropped": 0,
            "duplicates": 0,
            "resyncs": 0,
            "malformed": 0,
        }

    def _update_last_samples(self, payload: bytes) -> None:
        last_l, last_r = struct.unpack_from("<2h", payload, len(payload) - FRAME_BYTES)
        self._last_l = last_l
        self._last_r = last_r
        self._has_samples = True

    def _anchor(self, seq: int, payload: bytes) -> None:
        self._last_seq = seq
        self._has_anchor = True
        self._update_last_samples(payload)

    def _make_fill(self, gap_packets: int, next_payload: bytes) -> bytes:
        n = gap_packets * FRAMES_PER_PACKET
        first_l, first_r = struct.unpack_from("<2h", next_payload, 0)
        last_l = self._last_l if self._has_samples else 0
        last_r = self._last_r if self._has_samples else 0
        left = _fade_channel(last_l, first_l, n)
        right = _fade_channel(last_r, first_r, n)
        return _interleave(left, right)

    def push(self, data: bytes) -> Written:
        """Take one datagram and return the PCM that now belongs in the file.

        See the module docstring and the per-outcome constants above for
        how a packet is classified (written / late-dropped / duplicate /
        concealed-and-written / re-anchored) and why.
        """
        self._counts["packets"] += 1

        if len(data) != PACKET_BYTES:
            self._counts["malformed"] += 1
            return Written(pcm=b"", concealed_packets=0)

        seq = struct.unpack_from("<H", data, 0)[0]
        payload = data[HEADER_BYTES:]

        if not self._has_anchor:
            # Nothing to compare against yet: this packet simply becomes
            # the start of the timeline. Not a resync, since there is no
            # prior state being discarded.
            self._anchor(seq, payload)
            self._counts["packets_written"] += 1
            return Written(pcm=payload, concealed_packets=0)

        delta = _signed_delta(seq, self._last_seq)

        if delta == 1:
            self._last_seq = seq
            self._update_last_samples(payload)
            self._counts["packets_written"] += 1
            return Written(pcm=payload, concealed_packets=0)

        if delta <= 0:
            magnitude = -delta
            if magnitude <= LATE_REORDER_WINDOW_PACKETS:
                # Ordinary UDP reordering or a resend: drop it, and
                # critically do not move the write index. Advancing here
                # would permanently shift audio against video by this
                # packet's ~4ms, once per occurrence.
                if delta == 0:
                    self._counts["duplicates"] += 1
                else:
                    self._counts["late_dropped"] += 1
                return Written(pcm=b"", concealed_packets=0)
            # Far behind the last written packet: not reordering at this
            # scale, this is the counter itself having reset (device
            # restart). Re-anchor rather than treat it as loss.
            self._counts["resyncs"] += 1
            self._anchor(seq, payload)
            self._counts["packets_written"] += 1
            return Written(pcm=payload, concealed_packets=0)

        # delta > 1: a forward gap.
        if delta <= FILL_CAP_PACKETS:
            gap_packets = delta - 1
            fill = self._make_fill(gap_packets, payload)
            self._last_seq = seq
            self._update_last_samples(payload)
            self._counts["packets_written"] += 1
            self._counts["packets_lost"] += gap_packets
            self._counts["packets_concealed"] += gap_packets
            return Written(pcm=fill + payload, concealed_packets=gap_packets)

        # Gap too large to be worth concealing (see FILL_CAP_PACKETS):
        # most likely a device restart or a very long stall either way,
        # re-anchor on the new packet instead of manufacturing a multi-
        # second fill for a gap we cannot characterise.
        self._counts["resyncs"] += 1
        self._anchor(seq, payload)
        self._counts["packets_written"] += 1
        return Written(pcm=payload, concealed_packets=0)

    def silence(self, packets: int) -> bytes:
        """Concealment for a caller-observed gap with no packet at all.

        For a live caller that knows time has passed (e.g. a receive
        timeout) without waiting for a packet whose sequence number would
        let push() measure the gap itself. Fades the last known sample
        toward zero across `packets` packets' worth of frames; there is no
        ramp-in tail, since unlike push() there is no next real sample yet
        to ramp into. Does not touch sequence-number bookkeeping (that
        only advances on an actually received packet), only the last-
        sample state used to keep any later fill click-free.
        """
        if packets <= 0:
            return b""
        n = packets * FRAMES_PER_PACKET
        last_l = self._last_l if self._has_samples else 0
        last_r = self._last_r if self._has_samples else 0
        left = _fade_to_silence(last_l, n)
        right = _fade_to_silence(last_r, n)
        if left:
            self._last_l = left[-1]
            self._last_r = right[-1]
            self._has_samples = True
        self._counts["packets_lost"] += packets
        self._counts["packets_concealed"] += packets
        return _interleave(left, right)

    def counts(self) -> Dict[str, int]:
        """Return a snapshot of the running per-outcome packet counters.

        Keys: packets, packets_written, packets_lost, packets_concealed,
        late_dropped, duplicates, resyncs, malformed. Every packet handed
        to push() is accounted for in exactly one of packets_written,
        late_dropped, duplicates, malformed, or as part of a resync's
        packets_written; packets_lost/packets_concealed track synthesised
        packets on top of that (not disjoint from packets_written, since a
        push() that closes a gap is one packets_written increment sitting
        alongside a possibly nonzero packets_concealed increment).
        """
        return dict(self._counts)
