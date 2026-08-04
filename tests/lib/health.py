#!/usr/bin/env python3
# Device health: every listener the tests depend on, plus proof the C64 runs.

"""One bounded sweep of the device, reported as a single line.

A suite that fails tells you the check did not hold. It does not tell you
whether the device was still fit to be tested, and by the time someone reads
the log the device has usually been reset several times over. The sweep below
answers that question at the moment it mattered: before each suite, and again
after one fails.

What it covers, and why each one is here:

- `ping`   the device is on the network at all. Distinguishes a wedged service
           from a device that has gone.
- `rest`   `/v1/version`. Nearly every suite drives the device through this.
- `ftp`    port 21 answers with its `220` banner. The file suites need it.
- `telnet` port 23 accepts a connection. Proven harmless to the UI: measured at
           about 45ms, and the menu state and the running C64 are unchanged
           afterwards, because nothing is sent and the socket is closed at once.
- `ident`  `/v1/info`. Names the product and firmware in the same line, which
           is what makes a log readable weeks later.
- `dma`    the control port, 64. It is a separate listener from the HTTP server
           and has wedged on its own.
- `jiffy`  `$00A2` moves, so the KERNAL interrupt is running.
- `raster` `$D012` moves, so the VIC is scanning.

The last two are what separate "the services answer" from "the machine under
them is alive": a C64 stopped in Ultimax mode still serves REST perfectly well.
They are skipped rather than failed when the menu is open, because the menu
legitimately stops the machine under Freeze, and reported as failed only when
the machine should be running and is not.
"""

from __future__ import annotations

import socket
import struct
import subprocess
import time
from dataclasses import dataclass
from typing import List, Optional, Sequence, Tuple

from api import UltimateApi
from report import Failure

# States a single check can end in. A skipped check never makes the device
# unhealthy: it means the answer could not be obtained, not that it was bad.
OK = "ok"
FAIL = "fail"
SKIP = "skip"

FTP_PORT = 21
TELNET_PORT = 23
# The DMA control port, and its IDENTIFY command. Same framing as the soak
# probe in tests/soak/network/dma_probe.py: a little-endian command word and
# payload length, answered by a length-prefixed title.
DMA_PORT = 64
DMA_CMD_IDENTIFY = 0xFF0E

# Every check is bounded well below a second, because this runs before each of
# seventeen suites and a slow sweep would be paid seventeen times.
SOCKET_TIMEOUT_SECONDS = 2.0
REST_TIMEOUT_SECONDS = 5.0
PING_TIMEOUT_SECONDS = 2
# The jiffy clock advances every 20ms at 50Hz; the raster line moves every
# 63us. Both are read until they change, so a healthy device costs two round
# trips and only a stopped one pays the budget.
#
# The budget has to outlast a C64 cold start. Measured live: a sweep that ran
# straight after a suite had reset the machine read $00A2 static at $00 while
# the raster was moving, because the KERNAL had not started ticking yet, and
# the device was recovered over JTAG for it. The machine reaches the BASIC
# prompt in about 2.4s, so the budget is comfortably past that.
MOVEMENT_TIMEOUT_SECONDS = 4.0
MOVEMENT_PAUSE_SECONDS = 0.02
JIFFY_ADDRESS = 0x00A2
RASTER_ADDRESS = 0xD012


@dataclass(frozen=True)
class Check:
    name: str
    state: str
    ms: float
    detail: str = ""

    def render(self) -> str:
        if self.state == SKIP:
            return f"{self.name}=skip"
        if self.state == FAIL:
            return f"{self.name}=FAIL"
        return f"{self.name}={self.ms:.0f}ms"


@dataclass(frozen=True)
class Health:
    checks: Tuple[Check, ...]

    @property
    def ok(self) -> bool:
        return not self.failed

    @property
    def failed(self) -> Tuple[str, ...]:
        return tuple(c.name for c in self.checks if c.state == FAIL)

    def detail_for(self, name: str) -> str:
        for check in self.checks:
            if check.name == name:
                return check.detail
        return ""

    def one_line(self) -> str:
        """The whole sweep on one line, latencies included.

        Ordered as the checks ran, so the line reads outside-in: is it on the
        network, do its services answer, is the machine under them alive.
        """
        parts = " ".join(check.render() for check in self.checks)
        verdict = "OK" if self.ok else "DEGRADED (" + ", ".join(self.failed) + ")"
        return f"health: {parts} -> {verdict}"


def _timed(name: str, action) -> Check:
    """Run `action`, timing it, and turn whatever it raises into a failure."""
    started = time.perf_counter()
    try:
        detail = action() or ""
    except (Failure, OSError, TimeoutError, ValueError, RuntimeError) as exc:
        return Check(name, FAIL, (time.perf_counter() - started) * 1000.0, str(exc))
    return Check(name, OK, (time.perf_counter() - started) * 1000.0, detail)


def _ping(host: str) -> Check:
    started = time.perf_counter()
    try:
        completed = subprocess.run(
            ["ping", "-c", "1", "-W", str(PING_TIMEOUT_SECONDS), host],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            timeout=PING_TIMEOUT_SECONDS + 2)
    except FileNotFoundError:
        # No ping binary. Not knowing is not the same as a bad answer.
        return Check("ping", SKIP, 0.0, "no ping command")
    except subprocess.TimeoutExpired:
        return Check("ping", FAIL, (time.perf_counter() - started) * 1000.0, "timed out")
    ms = (time.perf_counter() - started) * 1000.0
    if completed.returncode != 0:
        return Check("ping", FAIL, ms, f"ping exited {completed.returncode}")
    return Check("ping", OK, ms)


def _banner(host: str, port: int, expect: bytes = b"") -> str:
    """Connect, read whatever the listener volunteers, and close at once."""
    with socket.create_connection((host, port), timeout=SOCKET_TIMEOUT_SECONDS) as sock:
        sock.settimeout(SOCKET_TIMEOUT_SECONDS)
        try:
            greeting = sock.recv(128)
        except (socket.timeout, TimeoutError):
            greeting = b""
        if expect and not greeting.startswith(expect):
            raise RuntimeError(f"expected {expect!r}, got {greeting[:32]!r}")
        return ""


def _dma_identify(host: str) -> str:
    with socket.create_connection((host, DMA_PORT), timeout=SOCKET_TIMEOUT_SECONDS) as sock:
        sock.settimeout(SOCKET_TIMEOUT_SECONDS)
        sock.sendall(struct.pack("<HH", DMA_CMD_IDENTIFY, 0))
        length = sock.recv(1)
        if not length or not length[0]:
            raise RuntimeError("no identify title")
        title = sock.recv(length[0])
        if not title:
            raise RuntimeError("empty identify title")
    return title.decode("utf-8", "replace").strip()


def _moves(api: UltimateApi, address: int, means: str) -> str:
    """Read `address` until the value changes, or say it never did.

    Returns as soon as it moves, so this costs two round trips on a device
    that is running and the whole budget only on one that is not.
    """
    first = api.machine.readmem(address, 1)[0]
    deadline = time.monotonic() + MOVEMENT_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        if api.machine.readmem(address, 1)[0] != first:
            return ""
        time.sleep(MOVEMENT_PAUSE_SECONDS)
    raise RuntimeError(f"${address:04X} stayed at ${first:02X} for "
                       f"{MOVEMENT_TIMEOUT_SECONDS:g}s: {means}")


def probe(host: str, password: str = "", api: Optional[UltimateApi] = None,
          include: Sequence[str] = ()) -> Health:
    """Sweep the device once. `include` limits it to the named checks.

    Never raises: a sweep that cannot reach the device is the answer, not an
    error, and its caller is usually deciding whether to recover the device.
    """
    api = api or UltimateApi(host, password, REST_TIMEOUT_SECONDS)
    wanted = set(include)

    def skip(name: str) -> bool:
        return bool(wanted) and name not in wanted

    checks: List[Check] = []
    if not skip("ping"):
        checks.append(_ping(host))
    if not skip("rest"):
        checks.append(_timed("rest", lambda: api.version() and ""))
    if not skip("ftp"):
        checks.append(_timed("ftp", lambda: _banner(host, FTP_PORT, b"220")))
    if not skip("telnet"):
        checks.append(_timed("telnet", lambda: _banner(host, TELNET_PORT)))
    if not skip("ident"):
        def identify() -> str:
            info = api.info()
            return f"{info.product} {info.firmware_version}"
        checks.append(_timed("ident", identify))
    if not skip("dma"):
        checks.append(_timed("dma", lambda: _dma_identify(host)))

    # The machine checks need the menu shut: under Freeze the open menu has
    # stopped the C64 on purpose, and calling that a degraded device would send
    # the runner off to recover hardware that is doing exactly what was asked.
    machine_checks = (
        ("jiffy", JIFFY_ADDRESS,
         "the KERNAL interrupt is not running, so the C64 is held in reset, "
         "stopped, or running with interrupts masked"),
        ("raster", RASTER_ADDRESS,
         "the VIC is not scanning, so the C64 is not running at all"),
    )
    if any(not skip(name) for name, _, _ in machine_checks):
        menu_open = None
        try:
            menu_open = api.machine.menu_open()
        except Failure:
            menu_open = None
        for name, address, means in machine_checks:
            if skip(name):
                continue
            if menu_open:
                checks.append(Check(name, SKIP, 0.0, "the menu is open"))
            elif menu_open is None:
                checks.append(Check(name, SKIP, 0.0, "menu state unknown"))
            else:
                checks.append(_timed(
                    name, lambda a=address, m=means: _moves(api, a, m)))
    return Health(tuple(checks))


def main() -> int:
    import argparse
    import os
    import sys

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from report import detail, suite_fail, suite_ok  # noqa: PLC0415

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-c", "--check", action="append", default=[],
                        help="Run only this check. Repeatable.")
    args = parser.parse_args()

    health = probe(args.host, args.password, include=args.check)
    detail(health.one_line())
    for check in health.checks:
        if check.detail:
            detail(f"  {check.name}: {check.detail}")
    if health.ok:
        suite_ok("health", f"{len(health.checks)} checks")
        return 0
    suite_fail("health", "degraded: " + ", ".join(health.failed))
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
