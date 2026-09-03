#!/usr/bin/env python3
# Device health: every listener the tests depend on, plus proof the C64 runs.

"""One bounded sweep of the device, reported as a single line.

A suite that fails tells you the check did not hold. It does not tell you
whether the device was still healthy enough to test on, and by the time
someone reads the log it has usually been reset several times over. The sweep
below answers that question at the moment it mattered: before each suite, and
again after one fails.

What it covers, and why each one is here:

- `ping`   the device is on the network at all. Distinguishes a wedged service
           from a device that has gone.
- `rest`   `/v1/version`. Nearly every suite drives the device through this.
- `ftp`    port 21 answers, and a listing comes back over a data connection.
           The banner alone is not enough: a device out of data connections
           still answers `220` and still takes commands, and only the PASV
           every transfer needs fails.
- `telnet` port 23 accepts a connection. Proven harmless to the UI: measured at
           about 45ms, and the menu state and the running C64 are unchanged
           afterwards, because nothing is sent and the socket is closed at once.
- `ident`  `/v1/info`. Names the product and firmware in the same line, which
           is what makes a log readable weeks later.
- `dma`    the control port, 64. It is a separate listener from the HTTP server
           and has wedged on its own.
- `heap`   free FreeRTOS heap. One GET, roughly 10ms on a sweep costing about
           150ms, and it gives every suite a before and an after by
           construction: suite N's before sample is suite N-1's after sample.
           It can never fail the sweep; see `_heap`.
- `raster` `$D012` moves, so the VIC is scanning. This is the one that says
           the machine is alive: a C64 stopped in Ultimax mode still serves
           REST perfectly well.
- `jiffy`  `$00A2` moves, so the KERNAL interrupt is running as well.

The last two are skipped rather than failed while the menu is open, because under
Freeze the menu has stopped the machine on purpose.

A static jiffy on its own is **not** a degraded device, and treating it as one
cost a healthy device a JTAG redeploy during a real run. The KERNAL stops
ticking `$00A2` for several ordinary reasons: for about 2.4s after a reset,
while a suite has the machine paused, and while a program with its own
interrupt handler is running. In every one of those the VIC is still scanning.
So the raster decides whether the machine is alive, and a static jiffy under a
moving raster is reported as an observation rather than a fault.
"""

from __future__ import annotations

import http.client
import ftplib
import socket
import struct
import subprocess
import sys
import time
from dataclasses import dataclass
from collections.abc import Sequence

import interactions
import targets
from api import UltimateApi
from report import Failure

# States a single check can end in. A skipped check never makes the device
# unhealthy: it means the answer could not be obtained, not that it was bad.
OK = "ok"
FAIL = "fail"
SKIP = "skip"

# The IDENTIFY command on the DMA control port. Same framing as the soak probe
# in tests/soak/network/dma_probe.py: a little-endian command word and payload
# length, answered by a length-prefixed title. Which port it is on is the
# handle's answer; see tests/lib/targets.py.
DMA_CMD_IDENTIFY = 0xFF0E

# The ninth check, named here because Check.render treats it differently.
HEAP = "heap"

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
# The jiffy gets its own, much shorter budget, because of what its answer is
# allowed to do. The raster decides whether the machine is alive and keeps the
# full budget above. A static jiffy under a moving raster is an observation
# that can never fail the sweep, so the only thing the long budget buys there
# is the wording of that observation: "static" instead of "moving".
#
# It cost 4s of every sweep that ran while the KERNAL was not ticking, which is
# the ordinary state straight after a suite resets or freezes the machine. In a
# smoke run of eight suites that was two sweeps and five seconds, spent
# re-deciding something already decided. The clock advances every 20ms and a
# read costs about 15ms, so 0.3s is around eight attempts at a two-attempt
# question.
# Measured on an Ultimate 64 at the BASIC prompt: a ticking jiffy is seen to
# move in 21 to 62ms, 15 times out of 15. 0.5s is eight times that worst case,
# so a clock that is running is still reported as running.
JIFFY_MOVEMENT_TIMEOUT_SECONDS = 0.5
MOVEMENT_PAUSE_SECONDS = 0.02
JIFFY_ADDRESS = 0x00A2
RASTER_ADDRESS = 0xD012


@dataclass(frozen=True)
class Check:
    name: str
    state: str
    ms: float
    detail: str = ""
    # The figures a check measured rather than timed. Only the heap check has
    # any; the other eight are latencies.
    figures: dict[str, int] | None = None

    def render(self) -> str:
        if self.state == SKIP:
            return f"{self.name}=skip"
        if self.state == FAIL:
            return f"{self.name}=FAIL"
        if self.name == HEAP and self.figures and "free" in self.figures:
            # A latency for this one says nothing anybody wants; the figure is
            # the point. The special case is on the check's name, not on
            # whether a check carries a detail: `ident` and `dma` both carry
            # one, and rendering those would rewrite two existing checks'
            # output and lengthen the sweep line as a side effect.
            return f"{self.name}={self.figures['free']}B"
        return f"{self.name}={self.ms:.0f}ms"


@dataclass(frozen=True)
class Health:
    checks: tuple[Check, ...]

    @property
    def ok(self) -> bool:
        return not self.failed

    @property
    def failed(self) -> tuple[str, ...]:
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


def ping_command(host: str, platform: str = sys.platform) -> list[str]:
    """The `ping` argument list for one probe of `host` on this platform.

    `-W` carries a different unit on each family, and passing the wrong one is
    not cosmetic here. On Linux it is a timeout in seconds; on macOS and the
    BSDs it is a wait in milliseconds, so `-W 2` there waits two milliseconds
    and every ping fails before a device on a LAN could answer. `ping` is the
    first check in the sweep, a failed sweep makes Health.ok false, and
    Device.ensure_healthy answers an unhealthy device by running the
    operator's recovery command, which reboots or reflashes hardware. A
    developer driving a device from a laptop would have every device recovered
    before every suite.
    """
    # Linux takes seconds; everything else here is the BSD stack, which macOS
    # is the one that matters for.
    if platform.startswith("linux"):
        wait = str(PING_TIMEOUT_SECONDS)
    else:
        wait = str(int(PING_TIMEOUT_SECONDS * 1000))
    return ["ping", "-c", "1", "-W", wait, host]


def _ping(host: str) -> Check:
    started = time.perf_counter()
    try:
        completed = subprocess.run(
            ping_command(host),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            timeout=PING_TIMEOUT_SECONDS + 2, check=False)
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
    started = time.monotonic()
    with socket.create_connection((host, port), timeout=SOCKET_TIMEOUT_SECONDS) as sock:
        sock.settimeout(SOCKET_TIMEOUT_SECONDS)
        try:
            greeting = sock.recv(128)
        except TimeoutError:
            greeting = b""
        interactions.record("socket", f"banner {port}", host=host,
                            ms=round((time.monotonic() - started) * 1000.0, 1),
                            body=greeting)
        if expect and not greeting.startswith(expect):
            raise RuntimeError(f"expected {expect!r}, got {greeting[:32]!r}")
        return ""


def _ftp_listing(host: str, port: int, password: str, passive: bool) -> None:
    """One listing over a data connection, in the mode asked for."""
    client = ftplib.FTP(timeout=SOCKET_TIMEOUT_SECONDS)
    try:
        client.connect(host, port)
        client.login("ultimate", password or "ultimate")
        client.set_pasv(passive)
        client.nlst("/")
    finally:
        try:
            client.close()
        except Exception:
            pass


def _ftp(host: str, port: int, password: str = "") -> str:
    """Prove FTP can still carry a transfer, not just answer its banner.

    The suites transfer in passive mode, which is ftplib's default, so that is
    what the sweep proves. A device whose data path has stopped working still
    answers `220` and still takes commands, and only the transfer fails.
    Measured on a C64 Ultimate part-way through a standard run: every passive
    transfer was reset from then on while the banner check called the device
    healthy, so the runner kept feeding it suites and seven more failed for a
    reason that had nothing to do with them.

    When passive fails, active mode is tried as well, because which of the two
    failed says what is wrong: both failing is a device that cannot transfer at
    all, and passive alone failing is a device whose passive listener is gone
    while its files are still readable. A listing is the cheapest thing that
    opens a data connection either way.
    """
    started = time.monotonic()
    try:
        _ftp_listing(host, port, password, passive=True)
    except (*ftplib.all_errors, EOFError) as exc:
        # Every one of these has to leave the sweep as a failed check. An
        # ftplib error that is not one _timed catches escapes the sweep
        # instead, and the runner then dies part-way through a run rather than
        # reporting a degraded device: a listener that closes without its
        # banner raises EOFError, which killed the whole run.
        detail = f"{type(exc).__name__}: {exc}".strip(": ")
        try:
            _ftp_listing(host, port, password, passive=False)
        except (*ftplib.all_errors, EOFError):
            raise RuntimeError(f"no data connection at all, {detail}") from exc
        raise RuntimeError(
            f"passive transfers are refused and active ones work, {detail}"
        ) from exc
    interactions.record("ftp", "nlst /", host=host,
                        ms=round((time.monotonic() - started) * 1000.0, 1))
    return ""


def _dma_identify(host: str, port: int) -> str:
    started = time.monotonic()
    with socket.create_connection((host, port), timeout=SOCKET_TIMEOUT_SECONDS) as sock:
        sock.settimeout(SOCKET_TIMEOUT_SECONDS)
        sock.sendall(struct.pack("<HH", DMA_CMD_IDENTIFY, 0))
        length = sock.recv(1)
        if not length or not length[0]:
            raise RuntimeError("no identify title")
        title = sock.recv(length[0])
        if not title:
            raise RuntimeError("empty identify title")
    interactions.record("socket", f"dma identify {port}", host=host,
                        ms=round((time.monotonic() - started) * 1000.0, 1),
                        body=title)
    return title.decode("utf-8", "replace").strip()


def _heap(api: UltimateApi) -> Check:
    """Free heap, which can never make a device unhealthy.

    `Health.ok` is `not self.failed`, and a degraded sweep is what fires the
    operator's recovery command in `Device.ensure_healthy`, which reboots or
    reflashes hardware. A number that moves for a dozen ordinary reasons must
    not be able to do that, so this reports OK with the figure or SKIP with the
    reason and never FAIL. The precedent is the jiffy check below, already
    downgraded from FAIL to SKIP when the raster says the machine is alive.

    Not an assertion either way: a sample taken at a suite boundary has no
    settle time, and a transient borrowing reads as a step down. Leak
    assertions stay in tests/soak/.
    """
    started = time.perf_counter()
    try:
        figures = api.machine.heap()
    except (Failure, OSError, TimeoutError, ValueError, TypeError,
            RuntimeError, http.client.HTTPException) as exc:
        # Wider than the other eight checks because this one may never fail a
        # sweep: an http.client.HTTPException that is not an OSError escapes
        # rest.py's own handler, and a malformed body reaches int() as a
        # TypeError. Either would otherwise leave the sweep with an exception
        # rather than with a verdict.
        return Check(HEAP, SKIP, (time.perf_counter() - started) * 1000.0,
                     str(exc))
    ms = (time.perf_counter() - started) * 1000.0
    if figures is None:
        return Check(HEAP, SKIP, ms, "this firmware has no machine:heap")
    return Check(HEAP, OK, ms, "", figures)


def _ident(api: UltimateApi) -> Check:
    """The product and firmware version, from `/v1/info`.

    Which machine this is decides which checks a suite may run, so a sweep that
    could not read it has found something a run needs to know before it starts
    driving anything. See tests/lib/machine.py for what the answer is used for.

    Nothing here reports how much of the device's log went missing. The
    firmware counts refused datagrams and buffer overflows internally, but a
    run acts on neither, and the report would only print them: syslog is best
    effort by design and a lost line costs a run nothing it was testing. The
    cost of leaving them alone is stated in OBS-7.15: a device that says
    nothing is not distinguishable from a device whose datagrams were refused,
    and the report shows the silence without a cause.
    """
    started = time.perf_counter()
    try:
        info = api.info()
    except (Failure, OSError, TimeoutError, ValueError, RuntimeError) as exc:
        return Check("ident", FAIL, (time.perf_counter() - started) * 1000.0,
                     str(exc))
    ms = (time.perf_counter() - started) * 1000.0
    return Check("ident", OK, ms, f"{info.product} {info.firmware_version}")


def _moves(api: UltimateApi, address: int, means: str,
           budget: float = MOVEMENT_TIMEOUT_SECONDS) -> str:
    """Read `address` until the value changes, or say it never did.

    Returns as soon as it moves, so this costs two round trips on a device
    that is running and the whole budget only on one that is not. `budget` is
    per check rather than shared: see JIFFY_MOVEMENT_TIMEOUT_SECONDS.
    """
    first = api.machine.readmem(address, 1)[0]
    deadline = time.monotonic() + budget
    while time.monotonic() < deadline:
        if api.machine.readmem(address, 1)[0] != first:
            return ""
        time.sleep(MOVEMENT_PAUSE_SECONDS)
    raise RuntimeError(f"${address:04X} stayed at ${first:02X} for "
                       f"{budget:g}s: {means}")


def probe(host, password: str = "", api: UltimateApi | None = None,
          include: Sequence[str] = ()) -> Health:
    """Sweep the device once. `include` limits it to the named checks.

    `host` is a target token or a resolved handle; see tests/lib/targets.py.

    Never raises: a sweep that cannot reach the device is the answer, not an
    error, and its caller is usually deciding whether to recover the device.
    """
    api = api or UltimateApi(host, password, REST_TIMEOUT_SECONDS)
    # The listener checks open sockets, so they need a name rather than a
    # target: on "u2@c64u" every listener under test is the cartridge's. The
    # handle also says which ports they are on, so a device serving them
    # somewhere else is swept the same way.
    target = targets.resolve(host)
    host = target.device
    wanted = set(include)

    def skip(name: str) -> bool:
        return bool(wanted) and name not in wanted

    checks: list[Check] = []
    if not skip("ping"):
        checks.append(_ping(host))
    if not skip("rest"):
        checks.append(_timed("rest", lambda: api.version() and ""))
    if not skip("ftp"):
        checks.append(_timed("ftp", lambda: _ftp(host, target.ftp_port, password)))
    if not skip("telnet"):
        checks.append(_timed("telnet", lambda: _banner(host, target.telnet_port)))
    if not skip("ident"):
        checks.append(_ident(api))
    if not skip("dma"):
        checks.append(_timed("dma", lambda: _dma_identify(host, target.dma_port)))
    if not skip(HEAP):
        checks.append(_heap(api))

    # The machine checks need the menu shut: under Freeze the open menu has
    # stopped the C64 on purpose, and calling that a degraded device would send
    # the runner off to recover hardware that is doing exactly what was asked.
    # The raster comes first because it is the one that decides: a jiffy
    # failure is only meaningful once the raster has said the machine is dead.
    machine_checks = (
        ("raster", RASTER_ADDRESS,
         "the VIC is not scanning, so the C64 is not running at all",
         MOVEMENT_TIMEOUT_SECONDS),
        ("jiffy", JIFFY_ADDRESS,
         "the KERNAL interrupt is not ticking",
         JIFFY_MOVEMENT_TIMEOUT_SECONDS),
    )
    if any(not skip(name) for name, _, _, _ in machine_checks):
        menu_open = None
        try:
            menu_open = api.machine.menu_open()
        except Failure:
            menu_open = None
        for name, address, means, budget in machine_checks:
            if skip(name):
                continue
            if menu_open:
                checks.append(Check(name, SKIP, 0.0, "the menu is open"))
            elif menu_open is None:
                checks.append(Check(name, SKIP, 0.0, "menu state unknown"))
            else:
                checks.append(_timed(
                    name, lambda a=address, m=means, b=budget: _moves(api, a, m, b)))

        # A static jiffy under a moving raster is an ordinary state, not a
        # fault: the machine is running, its KERNAL interrupt is not ticking.
        # Downgrading rather than dropping the check keeps the reason visible
        # in the line without it making the device unhealthy.
        states = {c.name: c.state for c in checks}
        if states.get("raster") == OK and states.get("jiffy") == FAIL:
            checks = [
                Check(c.name, SKIP, c.ms,
                      "the VIC is scanning, so the machine is alive; the KERNAL "
                      "interrupt is not ticking (a recent reset, a paused "
                      "machine, or a program with its own interrupt)")
                if c.name == "jiffy" else c
                for c in checks
            ]
    return Health(tuple(checks))


def main() -> int:
    import argparse
    import os
    import sys

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import cli  # noqa: PLC0415
    from report import detail, suite_fail, suite_ok  # noqa: PLC0415

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    cli.add_device_arguments(parser, colour=False)
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
