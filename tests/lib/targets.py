"""What a run is aimed at, in one grammar and one place.

    host                 the device under test is also its own C64-side
                         computer, which is every Ultimate 64 and C64 Ultimate
    cartridge@computer   the left side is the device under test; the right
                         side is the real computer it is plugged into, which
                         supplies the C64 keyboard, video and companion
                         facilities

The split form exists because a cartridge is not a whole machine. An Ultimate
II+ serves its own menu, memory and configuration over REST, but it has no
keyboard matrix of its own: `POST /v1/machine:input` answers HTTP 501 there,
and the keys have to be injected into the computer it is plugged into, from
where they reach the cartridge over the expansion port. Everything else -
device identity, product detection, the menu screen, the machine reset, the
memory the monitor shows - stays with the cartridge.

The same token is accepted wherever a device is named, so a command line reads
the same from the runner down to a suite started by hand:

    ./run-tests u2@c64u
    python3 tests/e2e/monitor/monitor_test.py -H u2@c64u

A bench where a cartridge is permanently in one computer can say so once,
instead of every command line spelling it out:

    export U64_COMPUTERS=u2@c64u
    ./run-tests u2                 the same two machines as u2@c64u

Without that, `u2` on its own means a u2 that is its own computer, which no
cartridge is. See COMPUTERS_ENV.

Nothing here knows any host names. A target is whatever was typed, plus what
the environment says about this bench, and the physical resources a target
owns are its own host names, which is what lets the runner decide that two
targets cannot run at the same time without a list of devices to consult.
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from typing import Tuple

SEPARATOR = "@"

# Which port a device serves each of its surfaces on. A target answers this as
# well as which of its two machines serves what, so one object says where a
# device is rather than each library carrying a constant of its own.
#
# The defaults are the real device's, so a target parsed from a token needs
# nothing set and a caller pointing a handle somewhere else sets a field.
REST_PORT = 80
FTP_PORT = 21
TELNET_PORT = 23
DMA_PORT = 64
# Where the device sends its video and audio when they are started. Every
# target streams to the one group and port pair, and a receiver separates
# devices by the source address of each packet: `streams:start` sets where the
# device sends, three suites start the streams themselves at these addresses,
# and multicast forwarding is decided by group address alone with the UDP port
# not considered, so per-target ports separate nothing and lose the stream the
# moment one of those suites runs.
VIDEO_GROUP = "239.0.1.64"
VIDEO_PORT = 11000
AUDIO_GROUP = "239.0.1.65"
AUDIO_PORT = 11001

# Each port's environment override, so a caller can address a device serving
# somewhere else without every suite growing a flag. U64_TELNET_PORT already
# exists and every suite that drives Telnet honours it; the other three are
# spelled the same way. Read per parse rather than at import, so a process
# that sets one before resolving a target is obeyed.
#
# The REST port is the one that has to be movable: a loopback stand-in for a
# device cannot bind port 80 without root.
REST_PORT_ENV = "U64_REST_PORT"
FTP_PORT_ENV = "U64_FTP_PORT"
TELNET_PORT_ENV = "U64_TELNET_PORT"
DMA_PORT_ENV = "U64_DMA_PORT"
# The stream addresses move for the same reason: a receiver cannot rely on
# multicast working in a container, and the vendor's documentation lists
# unicast as supported and recommends it where IGMP snooping is absent, so
# pointing a stream at a loopback address is a real mode rather than a
# test-only path.
VIDEO_GROUP_ENV = "U64_VIDEO_GROUP"
VIDEO_PORT_ENV = "U64_VIDEO_PORT"
AUDIO_GROUP_ENV = "U64_AUDIO_GROUP"
AUDIO_PORT_ENV = "U64_AUDIO_PORT"

# The REST paths that belong to the C64-side computer rather than to the
# device under test. Everything else - identity, the menu screen, the menu
# button, memory, the machine reset, configuration - is the device's.
#
# The keyboard, because a cartridge has no keyboard matrix of its own and
# answers HTTP 501 here.
INPUT_PATH = "/v1/machine:input"
# The streams, because a cartridge has no VIC and no streaming hardware:
# route_streams.cc and data_streamer.cc are absent from every U2 makefile, so
# `streams:start` sent to a cartridge fails and the picture that shows what
# the cartridge is doing is the computer's anyway.
STREAM_PREFIX = "/v1/streams/"

# Which computer a cartridge is plugged into, for the bench it is plugged into
# it on. A cartridge named on its own is otherwise taken to be its own
# computer, which no cartridge is: it answers machine:input with HTTP 501, so
# every suite that presses a key fails, and the scheduler does not know it is
# sharing a machine with the computer's own target.
#
# The value is a comma-separated list in the same grammar a target uses, so
# `U64_COMPUTERS=u2@c64u` says that `u2` means the u2 in the c64u. It is an
# environment variable rather than a table here because it describes one
# bench's cabling, which is not a property of the tree, and it is read per
# parse for the same reason the ports are.
COMPUTERS_ENV = "U64_COMPUTERS"

# Conservative, and deliberately not a host-name authority: it rejects the
# things a mistyped target actually looks like (an empty half, a stray space,
# a second separator) and lets a name or an address through.
_HOST_RE = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9._-]*[A-Za-z0-9])?$")


class TargetError(ValueError):
    """A target that cannot be parsed. The message is shown to the user."""


@dataclass(frozen=True)
class Target:
    """One device under test, and the computer that drives its C64 side."""

    token: str
    device: str
    computer: str
    rest_port: int = REST_PORT
    ftp_port: int = FTP_PORT
    telnet_port: int = TELNET_PORT
    dma_port: int = DMA_PORT
    video_group: str = VIDEO_GROUP
    video_port: int = VIDEO_PORT
    audio_group: str = AUDIO_GROUP
    audio_port: int = AUDIO_PORT

    @property
    def split(self) -> bool:
        """Whether the C64-side computer is a second physical machine."""
        return self.device != self.computer

    @property
    def input_host(self) -> str:
        """Where C64 keyboard injection goes for this target."""
        return self.computer

    @property
    def video_host(self) -> str:
        """Which machine's video this target's picture comes from.

        A U2 has no `streams` route and no streaming hardware: `route_streams`
        and `data_streamer` are absent from every U2 makefile, so
        `streams:start` sent to a cartridge fails. The computer is the machine
        with the VIC, and what it renders is what the cartridge under test is
        doing, so it is also the right picture.
        """
        return self.computer

    @property
    def log_hosts(self) -> Tuple[str, ...]:
        """Whose logs belong to this target, the device under test first.

        Both machines of a cartridge target log: the firmware being tested is
        the cartridge's, and the computer it is plugged into runs its own. Both
        are kept and the order says which is which.
        """
        if self.split:
            return (self.device, self.computer)
        return (self.device,)

    @property
    def resources(self) -> Tuple[str, ...]:
        """The physical machines this target occupies while it runs.

        Two targets may not run at the same time when these overlap: a
        cartridge target owns both halves, so `u2@c64u` cannot run beside
        either `c64u` or `u2@u64`, while `u64` and `u2@c64u` share nothing.
        """
        if self.split:
            return (self.device, self.computer)
        return (self.device,)

    def conflicts_with(self, other: "Target") -> bool:
        return bool(set(self.resources) & set(other.resources))

    def host_for(self, path: str) -> str:
        """Which of this target's machines serves `path`.

        For a suite that builds its own URLs. tests/lib/rest.py,
        tests/e2e/lib/ui_backend.py and tests/e2e/lib/ui_state.py apply the
        same rule to the requests they make.

        Two paths belong to the computer: the keyboard and the streams. See
        INPUT_PATH and STREAM_PREFIX.
        """
        bare = path.split("?")[0]
        if bare == INPUT_PATH or bare.startswith(STREAM_PREFIX):
            return self.computer
        return self.device

    @property
    def slug(self) -> str:
        """A file-name-safe form of the token, for per-target output."""
        return self.token.replace(SEPARATOR, "-at-")

    def __str__(self) -> str:
        return self.token


def parse(token: str) -> Target:
    """Resolve one target token. Raises TargetError on a malformed one."""
    raw = (token or "").strip()
    if not raw:
        raise TargetError("a target cannot be empty")
    if raw.count(SEPARATOR) > 1:
        raise TargetError(
            f"{raw!r} names more than one computer; the form is "
            f"cartridge{SEPARATOR}computer")

    device, separator, computer = raw.partition(SEPARATOR)
    if separator and not computer:
        raise TargetError(
            f"{raw!r} does not name the computer; the form is "
            f"cartridge{SEPARATOR}computer")
    if not device:
        raise TargetError(
            f"{raw!r} does not name the cartridge; the form is "
            f"cartridge{SEPARATOR}computer")
    computer = computer or _declared_computer(device) or device
    for part in (device, computer):
        if not _HOST_RE.match(part):
            raise TargetError(f"{raw!r} is not a valid host name: {part!r}")
    if separator and device == computer:
        raise TargetError(
            f"{raw!r} names {device!r} as both the cartridge and the computer; "
            f"write {device} for a device that is its own computer")
    return Target(token=raw, device=device, computer=computer,
                  rest_port=_port(REST_PORT_ENV, REST_PORT),
                  ftp_port=_port(FTP_PORT_ENV, FTP_PORT),
                  telnet_port=_port(TELNET_PORT_ENV, TELNET_PORT),
                  dma_port=_port(DMA_PORT_ENV, DMA_PORT),
                  video_group=_address(VIDEO_GROUP_ENV, VIDEO_GROUP),
                  video_port=_port(VIDEO_PORT_ENV, VIDEO_PORT),
                  audio_group=_address(AUDIO_GROUP_ENV, AUDIO_GROUP),
                  audio_port=_port(AUDIO_PORT_ENV, AUDIO_PORT))


def _declared_computer(device: str) -> str:
    """The computer `U64_COMPUTERS` says `device` is plugged into, or "".

    A malformed entry is ignored rather than fatal, and an entry naming the
    device as its own computer is ignored too, because both would only stop a
    run that the operator meant to point somewhere real.
    """
    for entry in (os.environ.get(COMPUTERS_ENV) or "").split(","):
        cartridge, separator, computer = entry.strip().partition(SEPARATOR)
        if not separator or cartridge != device or cartridge == computer:
            continue
        if _HOST_RE.match(computer):
            return computer
    return ""


def _address(variable: str, default: str) -> str:
    """One stream address, honouring its environment override."""
    raw = (os.environ.get(variable) or "").strip()
    return raw if _HOST_RE.match(raw) else default


def _port(variable: str, default: int) -> int:
    """One port, honouring its environment override.

    A malformed value is ignored rather than fatal: a target that cannot be
    resolved stops a run, and a variable somebody exported by mistake is not a
    reason to stop one.
    """
    raw = (os.environ.get(variable) or "").strip()
    if raw.isdigit() and 0 < int(raw) <= 65535:
        return int(raw)
    return default


def resolve(host: "str | Target") -> Target:
    """A target from a handle or from a token, for a library that takes either.

    Every library here takes whatever the runner gave it, which is a token, and
    a caller pointing one at another address passes the handle instead.
    """
    return host if isinstance(host, Target) else parse(host)


def host_for(token: "str | Target", path: str) -> str:
    """Which machine of `token` serves `path`. See Target.host_for."""
    return resolve(token).host_for(path)


def device_of(token: "str | Target") -> str:
    """The bare host name in a target token, for a caller that needs one.

    Anything opening a socket - ping, FTP, Telnet, the DMA control port - wants
    a host rather than a target, and passing the token straight through would
    try to resolve "u2@c64u" as a name.
    """
    return resolve(token).device
