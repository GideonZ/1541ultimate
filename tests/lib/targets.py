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

Nothing here knows any host names. A target is whatever was typed, and the
physical resources a target owns are its own host names, which is what lets
the runner decide that two targets cannot run at the same time without a list
of devices to consult.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Tuple

SEPARATOR = "@"

# The one REST path that belongs to the C64-side computer. Everything else -
# identity, the menu screen, the menu button, memory, the machine reset,
# configuration - is the device under test's.
INPUT_PATH = "/v1/machine:input"

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

    @property
    def split(self) -> bool:
        """Whether the C64-side computer is a second physical machine."""
        return self.device != self.computer

    @property
    def input_host(self) -> str:
        """Where C64 keyboard injection goes for this target."""
        return self.computer

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
        """
        return self.input_host if path.split("?")[0] == INPUT_PATH else self.device

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
    computer = computer or device
    for part in (device, computer):
        if not _HOST_RE.match(part):
            raise TargetError(f"{raw!r} is not a valid host name: {part!r}")
    if separator and device == computer:
        raise TargetError(
            f"{raw!r} names {device!r} as both the cartridge and the computer; "
            f"write {device} for a device that is its own computer")
    return Target(token=raw, device=device, computer=computer)


def host_for(token: str, path: str) -> str:
    """Which machine of `token` serves `path`. See Target.host_for."""
    return parse(token).host_for(path)


def device_of(token: str) -> str:
    """The bare host name in a target token, for a caller that needs one.

    Anything opening a socket - ping, FTP, Telnet, the DMA control port - wants
    a host rather than a target, and passing the token straight through would
    try to resolve "u2@c64u" as a name.
    """
    return parse(token).device
