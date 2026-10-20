"""Multicast VIC video capture and frame assertions for hardware E2E tests.

The wire format, the socket options and the frame assembly are `streams.py`'s,
which the recorder shares. What stays here is what a suite does with a frame:
a palette-indexed image and the assertions that go with it.
"""

from collections import Counter
import sys

from PIL import Image
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
from report import Failure

import streams
import targets

# A PAL frame is 272 lines and every packet carries four of them.
MAX_PACKETS_PER_FRAME = -(-272 // streams.LINES_PER_PACKET)

# The public names a suite already imports, answered by the one place that
# knows them.
MULTICAST_GROUP = streams.VIDEO_GROUP
VIDEO_PORT = streams.VIDEO_PORT


class VicStreamCapture:
    """Capture one complete palette-indexed VIC frame from the multicast stream.

    `host` is a target token or a resolved handle. Given one, the capture joins
    that target's own group and port and ignores datagrams from anything else:
    two targets sharing a bench stream to the same address unless the
    `U64_VIDEO_*` variables separate them, and an unfiltered capture would
    assemble one target's frame out of both machines' packets.
    """

    def __init__(self, host=None, port: int | None = None) -> None:
        handle = targets.resolve(host) if host is not None else None
        group = handle.video_group if handle else MULTICAST_GROUP
        if port is None:
            port = handle.video_port if handle else VIDEO_PORT
        # Packets from anything but this target's stream source. An empty
        # capture with none of these is no stream at all, with some a wrong
        # address; the same distinction av_stream.py keeps.
        self.foreign_packets = 0
        self.source_addresses = streams.source_addresses(handle) if handle else None
        # The socket sets SO_REUSEADDR and, where the platform has it,
        # SO_REUSEPORT. Multicast delivers to every subscriber that joined, so
        # that is what lets a suite and a recorder both receive this stream.
        self.sock = streams.stream_socket(group, port, timeout=2.0)

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def capture_image(self) -> Image.Image:
        """One complete frame, as a palette-indexed image.

        Assembled by header offset rather than by concatenating payloads in
        arrival order, so a lost or reordered packet leaves an incomplete
        frame rather than shifting the rest of the picture upward.
        """
        assembler = streams.FrameAssembler()
        # Two frames' worth of packets at the largest geometry, which is the
        # bound on how many packets one complete frame can cost when the read
        # starts partway through a frame or the network drops a few. Only this
        # target's packets are counted against it: another machine streaming to
        # the same group would otherwise spend the budget without contributing
        # a line.
        budget = MAX_PACKETS_PER_FRAME * 2
        while budget:
            try:
                data, sender = self.sock.recvfrom(2048)
            except OSError as exc:
                raise Failure(f"the VIC stream stopped: {exc}") from exc
            if self.source_addresses is not None and sender[0] not in self.source_addresses:
                self.foreign_packets += 1
                continue
            budget -= 1
            frame = assembler.push(data)
            if frame is None:
                continue
            image = Image.frombytes("P", (frame.width, frame.height),
                                    streams.unpack(frame.packed))
            return image
        raise Failure("Did not receive a complete VIC frame.")


def frame_colors(image: Image.Image) -> Counter:
    """Palette-index histogram for a captured VIC frame."""
    return Counter(image.getdata())


def assert_not_black(image: Image.Image, label: str = "VIC frame") -> None:
    """Require at least one non-black pixel in a captured frame."""
    colors = frame_colors(image)
    if len(colors) == 1 and 0 in colors:
        raise Failure(f"{label} is completely black ({image.width}x{image.height})")


def assert_frames_differ(images: list[Image.Image], label: str = "VIC frames") -> None:
    """Require a sequence of captured frames to contain a visible change."""
    if len(images) < 2:
        raise ValueError("assert_frames_differ requires at least two frames")
    first = images[0].tobytes()
    if all(image.tobytes() == first for image in images[1:]):
        raise Failure(f"{label} did not change")
