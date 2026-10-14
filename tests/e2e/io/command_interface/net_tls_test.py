#!/usr/bin/env python3
# E2E: TLS sockets on the UCI network target ($21-$25): RED on stock firmware, GREEN with TLS.

"""End-to-end check of the UCI network target's TLS commands ($21-$25).

The contract is doc/uci_tls.md; this suite checks it. Every
scenario is RED on firmware without the TLS commands, with the one signature
`21,UNKNOWN COMMAND` to its first TLS command, and GREEN on firmware with
them. The exceptions are marked `control` (they pass on both, and say the
test setup itself works) and `measurement` (they only record a number).

Tier 1 is the RED signature and the smoke test to run first:
  S0   startup sweep (CLOSE 0..15)              control
  S1   capability: `03 23 FF`, 16 bytes          RED 21 / GREEN v1 record
  S20  plain TCP round trip                      control
  S2   PIN echo to this machine, TLS 1.3, INFO for the handle
  S23  hardened TLS 1.2 fallback (EMS bit, one of the six suites)
  S5   a refused certificate: exactly `93,CERTIFICATE NOT TRUSTED: 0x00000008`,
       and `96` for a wrong pin
  S19e an ABORTed OPEN, then $25 (client deadlines; REST route only)
  S30  BUNDLE to real hosts (internet_hosts.json) with an HTTP GET
Tier 2 is the other scenarios this machine can drive (see --list).
Oracles come from the server side (net_tls_fixture.py logs), never from the device's claim.

The suite first SETS and reads back Command Interface, WiFi Enabled (one-way on the U64:
never written back), SNTP Enable and Turbo Control Off, resets the C64 and sweeps the route.
Every command has a client deadline (45 s for an open, 12 s otherwise), then ABORT and,
if $DF1C bit 2 stays set, a C64 reset: nothing waits forever.
Results: --results (default .net_tls_results/<UTC>-<expect>/): run.json and wire.jsonl.
--expect red exits 0 exactly when every TLS scenario is RED and every control passed.
"""

import argparse
import datetime
import hashlib
import json
import os
import socket
import ssl
import subprocess
import sys
import time
import traceback
from dataclasses import dataclass, field
from pathlib import Path
from collections.abc import Callable

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import targets  # noqa: E402
from api import UltimateApi  # noqa: E402
from report import (  # noqa: E402
    FAIL, OK, SKIP, Failure, check, check_fail, check_ok, check_skip, check_start, check_warn, detail,
    format_exception, section, suite_fail, suite_ok, warn)
from uci import Uci, interface_present  # noqa: E402
from uci_native import NativeUci  # noqa: E402

sys.path.insert(0, bootstrap.directory("e2e", "io", "command_interface"))
import net_tls_fixture as fx  # noqa: E402
from net_tls_proto import (  # noqa: E402
    BOUND_OPEN, CA_CHUNK, GROUPS, HANDLE_RANGE, INFO_CAPABILITY,
    INFO_CAPABILITY_BYTES, INFO_HANDLE_BYTES, NET_CMD_TLS_INFO, NET_CMD_TLS_RELEASE,
    OPEN_REPLY_BYTES, STATUS_CLOSE_UNOWNED, STATUS_HS_TIMEOUT, STATUS_INVALID_PARAMS,
    STATUS_NO_CUSTOM_CA, STATUS_NO_DATA_GONE, STATUS_NO_DATA_IDLE, STATUS_NO_NOTIFY, STATUS_OK,
    STATUS_OUT_OF_RANGE, STATUS_PEER_CLOSED, STATUS_PIN_MISMATCH, STATUS_TLS_IN_USE,
    STATUS_UNKNOWN_COMMAND, STATUS_UNKNOWN_ROOT, TLS12, TLS12_SUITES, TLS13, TLS13_SUITES,
    TLS_COMMANDS, TRUST_BUNDLE, TRUST_CUSTOM, TRUST_PIN, WRITE_MAX, Capability, HandleInfo,
    Link, LinkLost, NativeLink, OpenReply, ReadReply, RestLink, Result, close_command,
    info_command, open_tcp_command, open_tls_command, read_command, release_command,
    status_code, trust_ca_command, write_command)

SUITE = "net_tls_test"
HERE = Path(__file__).resolve().parent
DEFAULT_RESULTS = HERE / ".net_tls_results"
HOSTS_FILE = HERE / "internet_hosts.json"
HOSTS_ENV = "NET_TLS_INTERNET_HOSTS"

# What the runner sets, and whether it puts it back afterwards. WiFi Enabled is
# one-way on the U64 (network_esp32.cc:134-136: the disable branch is commented
# out), so writing it back would record a restore that did not happen.
SETTINGS = (
    ("C64 and Cartridge Settings", "Command Interface", "Enabled", True),
    ("WiFi settings", "WiFi Enabled", "Enabled", False),
    ("Network Settings", "SNTP Enable", "Enabled", True),
    ("U64 Specific Settings", "Turbo Control", "Off", True),
)
REQUIRED_SETTING = "Command Interface"
WIFI_SETTING = "WiFi Enabled"
# INFO $FF [4]: module, entropy, trusted time. How long the module and SNTP may
# take to come up after the settings above.
READY_BITS = 0x07
READY_WAIT_SECONDS = 90.0

ECHO_BYTES = 4096
PEER_STREAM_BYTES = 3000
BIG_RECORD_BYTES = 16384
READ_CHUNK = 892
READ_BUDGET_SECONDS = 20.0
SLOW_HANDSHAKE_SECONDS = 4.0
ABORT_AFTER_SECONDS = 1.5
HANDSHAKE_MS = 15.0
HTTP_READ_BUDGET_SECONDS = 15.0
RESAMPLE_AFTER_SECONDS = 30.0
MAC_SAMPLES = 3
# A name no resolver answers (RFC 6761), and an address nothing answers SYNs on
# (RFC 5737 TEST-NET-1, dropped rather than refused on most networks).
UNRESOLVABLE = "e2e-unresolvable.invalid"
# S32 stops here: with a 4 KB receive buffer the stall comes within tens of
# WRITEs, and each native WRITE is a body-carrying writemem.
S32_MAX_WRITES = 100
BLACKHOLE = "192.0.2.1"


# -- the record --------------------------------------------------------------

@dataclass
class Scenario:
    sid: str
    name: str
    tier: int
    kind: str            # "tls" (RED on stock), "control", "measurement"
    oracle: str
    fn: Callable
    needs: tuple = ()    # "fixture", "internet", "abort"
    default: bool = True


@dataclass
class Outcome:
    scenario: Scenario
    route: str
    verdict: str = "not run"          # PASS, FAIL, SKIP, N/A (not on this route), ERROR
    signature: str = ""               # GREEN, RED, FAIL, CONTROL-PASS, ...
    reason: str = ""
    first_tls_status: str | None = None
    elapsed: float = 0.0
    evidence: dict = field(default_factory=dict)

    def as_dict(self) -> dict:
        s = self.scenario
        return {"sid": s.sid, "name": s.name, "tier": s.tier, "kind": s.kind,
                "route": self.route, "oracle": s.oracle, "verdict": self.verdict,
                "signature": self.signature, "reason": self.reason,
                "first_tls_status": self.first_tls_status,
                "elapsed": round(self.elapsed, 2), "evidence": self.evidence}


class Ctx:
    """One route's view of the device, the fixture and the open scenario."""

    def __init__(self, link: Link, args, pki, bind_ip: str | None,
                 reset_c64: Callable[[], Link] | None, wire: list[dict]) -> None:
        self.wire = wire
        self.link = link
        link.log = wire
        self.args = args
        self.pki = pki
        self.bind_ip = bind_ip
        self._reset_c64 = reset_c64
        self.servers: list[fx.TlsServer] = []
        self.outcome: Outcome | None = None

    # -- commands ------------------------------------------------------------

    def cmd(self, command: bytes, bound: float | None = None) -> Result:
        result = self.link.command(command, bound)
        o = self.outcome
        if o is not None and len(command) >= 2 and command[1] in TLS_COMMANDS:
            if o.first_tls_status is None:
                o.first_tls_status = result.status.decode("latin-1")
            if result.status == STATUS_UNKNOWN_COMMAND:
                o.evidence.setdefault("unknown_command", []).append(command[:3].hex(" "))
        if result.aborted:
            raise Failure(f"{command[:6].hex(' ')} gave no reply within its client deadline "
                          f"and was ABORTed after {result.elapsed:.1f}s")
        return result

    def expect(self, result: Result, status: bytes, what: str) -> Result:
        if result.status != status:
            raise Failure(f"{what}: expected {status.decode()!r}, got {result.describe()}")
        return result

    def info_ff(self) -> Capability:
        r = self.cmd(info_command(INFO_CAPABILITY))
        self.expect(r, STATUS_OK, "TLS_INFO $FF")
        if len(r.data) != INFO_CAPABILITY_BYTES:
            raise Failure(f"TLS_INFO $FF answered {len(r.data)} bytes, not 16")
        return Capability(r.data)

    def info(self, handle: int) -> HandleInfo:
        r = self.cmd(info_command(handle))
        self.expect(r, STATUS_OK, f"TLS_INFO {handle}")
        if len(r.data) != INFO_HANDLE_BYTES:
            raise Failure(f"TLS_INFO {handle} answered {len(r.data)} bytes, not 48")
        return HandleInfo(r.data)

    def open_tls(self, port: int, host: str, trust: int = TRUST_PIN, flags: int = 0,
                 name: str | None = None, pin: bytes | None = None) -> Result:
        return self.cmd(open_tls_command(port, host, trust, flags, name, pin))

    def expect_open(self, port: int, host: str, trust: int = TRUST_PIN, flags: int = 0,
                    name: str | None = None, pin: bytes | None = None) -> OpenReply:
        r = self.open_tls(port, host, trust, flags, name, pin)
        self.expect(r, STATUS_OK, "OPEN_TLS")
        if len(r.data) != OPEN_REPLY_BYTES:
            raise Failure(f"OPEN_TLS answered {len(r.data)} bytes, not 8: {r.data.hex(' ')}")
        reply = OpenReply(r.data)
        self.note("open", reply.as_dict())
        return reply

    def read(self, handle: int, maxlen: int = READ_CHUNK) -> ReadReply:
        return ReadReply(self.cmd(read_command(handle, maxlen)))

    def read_until(self, handle: int, count: int, maxlens=(READ_CHUNK,),
                   budget: float = READ_BUDGET_SECONDS) -> tuple[bytes, ReadReply | None]:
        """READ until `count` bytes or the stream ends; (bytes, the ending reply or None).

        `budget` is IDLE time: the clock restarts on every READ that brings at
        least one byte. On the REST route every byte costs one REST call, so a
        total-time budget would fail a healthy but slow transfer for the wrong
        reason. A READ answering 00,OK with no bytes is not a valid READ answer
        and fails at once, so the loop is always bounded.
        """
        got = bytearray()
        deadline = time.monotonic() + budget
        index = 0
        while len(got) < count:
            reply = self.read(handle, maxlens[index % len(maxlens)])
            index += 1
            if reply.result.ok:
                if reply.header != len(reply.payload):
                    raise Failure(f"READ header {reply.header} but {len(reply.payload)} bytes")
                if not reply.payload:
                    raise Failure("READ answered 00,OK with header $0000: the protocol has no "
                                  "such answer (data is 1..maxlen; nothing yet is $FFFF + 02)")
                got += reply.payload
                deadline = time.monotonic() + budget
                continue
            if reply.header == 0xFFFF and reply.status == STATUS_NO_DATA_IDLE:
                if time.monotonic() > deadline:
                    raise Failure(f"only {len(got)} of {count} bytes: no data for {budget:.0f}s")
                time.sleep(0.05)
                continue
            return bytes(got), reply
        return bytes(got), None

    def write(self, handle: int, data: bytes) -> None:
        r = self.cmd(write_command(handle, data))
        self.expect(r, STATUS_OK, f"WRITE of {len(data)} bytes")
        if r.data[:2] != len(data).to_bytes(2, "little"):
            raise Failure(f"WRITE header {r.data[:2].hex(' ')}, expected {len(data)}")

    def close(self, handle: int) -> Result:
        return self.cmd(close_command(handle))

    def release(self) -> int:
        r = self.cmd(release_command())
        self.expect(r, STATUS_OK, "TLS_RELEASE")
        if len(r.data) != 1:
            raise Failure(f"TLS_RELEASE answered {len(r.data)} bytes, not 1")
        return r.data[0]

    def entries(self, wanted: int, what: str) -> Capability:
        cap = self.info_ff()
        if cap.entries != wanted:
            raise Failure(f"{what}: INFO $FF [2] = {cap.entries}, expected {wanted}")
        return cap

    def upload_ca(self, der: bytes) -> Result:
        last = None
        for offset in range(0, len(der), CA_CHUNK):
            last = self.cmd(trust_ca_command(offset, len(der), der[offset:offset + CA_CHUNK]))
            if not last.ok:
                return last
        return last

    def sweep(self) -> list[int]:
        """The startup CLOSE sweep of 0..15; the numbers that were held."""
        held = []
        for handle in HANDLE_RANGE:
            r = self.link.command(close_command(handle))
            if r.status == STATUS_OK:
                held.append(handle)
            elif r.status != STATUS_CLOSE_UNOWNED:
                raise Failure(f"CLOSE {handle} in the sweep answered {r.describe()}")
        return held

    def reset_c64(self) -> None:
        if self._reset_c64 is None:
            raise Failure("this route cannot reset the C64")
        self.link = self._reset_c64()
        self.link.log = self.wire

    # -- fixture -------------------------------------------------------------

    def server(self, chain: str | None, behaviour: fx.Behaviour) -> fx.TlsServer:
        if self.pki is None or self.bind_ip is None:
            raise Skipped("no local TLS fixture on this run (see the fixture check)")
        server = fx.TlsServer(self.bind_ip, self.pki.chains[chain] if chain else None, behaviour)
        self.servers.append(server)
        return server

    def note(self, key: str, value) -> None:
        if self.outcome is not None:
            self.outcome.evidence.setdefault(key, [])
            self.outcome.evidence[key].append(value)

    def close_servers(self) -> None:
        for server in self.servers:
            if self.outcome is not None:
                self.outcome.evidence.setdefault("server", []).append(
                    {"port": server.port, "connections": [c.as_dict() for c in server.logs]})
            server.close()
        self.servers.clear()


class Skipped(Failure):
    """The scenario could not run here. Counts AGAINST every verdict."""


class NotOnRoute(Skipped):
    """By design the scenario does not apply to this route (S19e/S19e2b native, S32 REST).

    The only skip a verdict may ignore; the run still needs TLS evidence elsewhere.
    """


def tls13_server() -> fx.Behaviour:
    return fx.Behaviour()


def tls12_server(**kw) -> fx.Behaviour:
    return fx.Behaviour(min_version=ssl.TLSVersion.TLSv1_2, max_version=ssl.TLSVersion.TLSv1_2,
                        ciphers="ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:"
                                "ECDHE-ECDSA-CHACHA20-POLY1305", **kw)


def one_connection(server: fx.TlsServer, timeout: float = 15.0) -> fx.ConnLog:
    logs = server.wait_connections(1, timeout)
    if not logs:
        raise Failure("the server saw no connection")
    return logs[0]


def pin_of(ctx: Ctx, chain: str = "ecdsa") -> bytes:
    return ctx.pki.chains[chain].spki_sha256


def echo_round_trip(ctx: Ctx, handle: int, total: int, seed: int) -> bytes:
    data = fx.payload(seed, total)
    with check(f"{total} B echoed back byte exact, written in {WRITE_MAX} B WRITEs"):
        got = bytearray()
        for offset in range(0, total, WRITE_MAX):
            ctx.write(handle, data[offset:offset + WRITE_MAX])
            part, end = ctx.read_until(handle, min(offset + WRITE_MAX, total) - len(got))
            got += part
            if end is not None:
                raise Failure(f"the stream ended early: {end.result.describe()}")
        if bytes(got) != data:
            raise Failure(f"echo differs (sha256 {hashlib.sha256(got).hexdigest()[:16]} vs "
                          f"{hashlib.sha256(data).hexdigest()[:16]})")
    return data


def check_reply_bits(reply: OpenReply, version: int) -> None:
    with check(f"OPEN reply: version 0x{version:04X}, a policy suite, a policy group, EMS bit"):
        if reply.version != version:
            raise Failure(f"version 0x{reply.version:04X}")
        suites = TLS13_SUITES if version == TLS13 else TLS12_SUITES
        if reply.suite not in suites:
            raise Failure(f"suite 0x{reply.suite:04X} is not one of the policy suites")
        if reply.group not in GROUPS:
            raise Failure(f"group 0x{reply.group:04X} (0 is a FAIL in a GREEN run)")
        ems = 1 if version == TLS12 else 0
        if reply.bits != ems:
            raise Failure(f"byte 7 = 0x{reply.bits:02X}, expected 0x{ems:02X} (bit0 EMS)")


# -- Tier 1 ------------------------------------------------------------------

def s0_startup_sweep(ctx: Ctx) -> None:
    if ctx.link.can_abort_in_flight:
        with check("startup: $DF1D reads $C9/$49, $0C written, bit 2 back to 0 within 45 s"):
            seconds = ctx.link.startup_abort()
            detail(f"bit 2 cleared {seconds:.2f}s after the $0C")
    else:
        check_start("startup: $0C and the wait for bit 2")
        check_skip("the 6502 agent starts from run_prg's reset, and writes no $0C itself")
    with check("startup: CLOSE 0..15 each answers 00,OK or 12,ERROR ON CLOSE: 9"):
        held = ctx.sweep()
        ctx.note("swept", held)
        detail(f"leftover handles closed: {held or 'none'}")


def s1_capability(ctx: Ctx) -> None:
    with check("`03 23 FF` answers 00,OK with the 16-byte v1 record"):
        cap = ctx.info_ff()
        ctx.note("info_ff", cap.as_dict())
        detail(json.dumps(cap.as_dict()))
        problems = []
        if cap.version != 1:
            problems.append(f"[0] interface {cap.version}, not 1")
        if cap.max_sessions != 2:
            problems.append(f"[1] max sessions {cap.max_sessions}, not 2")
        if cap.trust_modes != 0x07:
            problems.append(f"[3] trust modes 0x{cap.trust_modes:02X}, not 07")
        if cap.versions != 0x03:
            problems.append(f"[5] versions 0x{cap.versions:02X}, not 03")
        if cap.raw[15] != 0:
            problems.append(f"[15] = {cap.raw[15]}, not 0")
        if problems:
            raise Failure("; ".join(problems))
    with check("the startup sweep left no entries: [2] = 0 and [14] = 0"):
        if cap.entries or cap.unclaimed:
            raise Failure(f"[2] = {cap.entries}, [14] = {cap.unclaimed}")
    with check("ready bits match the configured state: module, entropy, time (0x07)"):
        if cap.ready & READY_BITS != READY_BITS:
            raise Failure(f"[4] = 0x{cap.ready:02X}: Wi-Fi and SNTP are set on, so b0-b2 are owed")
        if cap.ready & 0x08:
            raise Failure("[4] b3 (custom CA LOADED) is set after the startup reset")
    with check("module version set and [9..11] = FF FF FF while b0 is set"):
        major, minor = cap.module_version
        if (major, minor) == (0, 0):
            raise Failure("[6..8] = 0 with b0 set")
        if cap.store_date != b"\xff\xff\xff":
            raise Failure(f"[9..11] = {cap.store_date.hex(' ')}")
        detail(f"module {major}.{minor}, block_max {cap.block_max_ms} ms")
        if ctx.args.module_minor is not None and minor != ctx.args.module_minor:
            raise Failure(f"module minor {minor}, expected {ctx.args.module_minor} (--module-minor)")
    with check("`03 23 FF 00` (4 bytes) answers 81"):
        ctx.expect(ctx.cmd(bytes([3, NET_CMD_TLS_INFO, 0xFF, 0])), STATUS_INVALID_PARAMS, "03 23 FF 00")
    with check("`03 25` answers 1 byte, 0, 00,OK; `03 25 00` answers 81"):
        if ctx.release() != 0:
            raise Failure("$25 closed something with no OPEN before it")
        ctx.expect(ctx.cmd(bytes([3, NET_CMD_TLS_RELEASE, 0])), STATUS_INVALID_PARAMS, "03 25 00")
    with check("INFO on a handle that is not TLS answers 81 with no data"):
        r = ctx.cmd(info_command(0x7F))
        ctx.expect(r, STATUS_INVALID_PARAMS, "03 23 7F")


def s20_plain_tcp(ctx: Ctx) -> None:
    """Control: OPEN_TCP / WRITE / READ / CLOSE to this machine, on any firmware."""
    if ctx.bind_ip is None:
        raise Skipped("this machine is not reachable from the device")
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind((ctx.bind_ip, 0))
    listener.listen(1)
    listener.settimeout(10)
    try:
        with check("OPEN_TCP to this machine answers a handle"):
            r = ctx.cmd(open_tcp_command(ctx.bind_ip, listener.getsockname()[1]))
            ctx.expect(r, STATUS_OK, "OPEN_TCP")
            handle = r.data[0]
            conn, _ = listener.accept()
        try:
            with check("a 100-byte WRITE arrives, and 100 bytes sent back are READ"):
                data = fx.payload(20, 100)
                ctx.write(handle, data)
                conn.settimeout(5)
                got = b""
                while len(got) < 100:
                    got += conn.recv(100)
                if got != data:
                    raise Failure("the bytes that arrived differ")
                conn.sendall(data)
                back, end = ctx.read_until(handle, 100)
                if back != data:
                    raise Failure(f"READ gave {len(back)} bytes, not the 100 sent ({end})")
            with check("CLOSE answers 00,OK"):
                ctx.expect(ctx.close(handle), STATUS_OK, "CLOSE")
        finally:
            conn.close()
    finally:
        listener.close()


def s2_pin_echo(ctx: Ctx) -> None:
    server = ctx.server("ecdsa", tls13_server())
    with check("OPEN_TLS PIN to this machine's IP (no name) answers 8 bytes, 00,OK"):
        reply = ctx.expect_open(server.port, ctx.bind_ip, TRUST_PIN, pin=pin_of(ctx))
    check_reply_bits(reply, TLS13)
    with check("INFO $FF: [2] = 1, [14] = 1 (the new entry is unclaimed until the next command)"):
        cap = ctx.info_ff()
        if (cap.entries, cap.unclaimed) != (1, 1):
            raise Failure(f"[2] = {cap.entries}, [14] = {cap.unclaimed}")
    with check("INFO for the handle: 48 bytes, the same version/suite/group, trust 2, SPKI = pin"):
        info = ctx.info(reply.handle)
        ctx.note("info", info.as_dict())
        if (info.version, info.suite, info.group) != (reply.version, reply.suite, reply.group):
            raise Failure(f"INFO {info.as_dict()} differs from the OPEN reply {reply.as_dict()}")
        if info.trust != TRUST_PIN or info.spki_sha256 != pin_of(ctx):
            raise Failure(f"trust {info.trust}, SPKI {info.spki_sha256.hex()[:16]}")
        if info.group == 0:
            raise Failure("INFO group 0")
    sent = echo_round_trip(ctx, reply.handle, ECHO_BYTES, seed=2)
    with check("CLOSE answers 00,OK; INFO $FF [2] = 0 afterwards"):
        ctx.expect(ctx.close(reply.handle), STATUS_OK, "CLOSE")
        ctx.entries(0, "after CLOSE")
    with check("server: TLS 1.3, no SNI (an IPv4 host, no name), sha256 of what it got, close_notify"):
        log = one_connection(server)
        ctx.note("conn", log.as_dict())
        if log.version != "TLSv1.3":
            raise Failure(f"server negotiated {log.version}")
        if log.sni is not None:
            raise Failure(f"SNI {log.sni!r} was sent for an IP literal")
        if hashlib.sha256(log.received).digest() != hashlib.sha256(sent).digest():
            raise Failure(f"server received {len(log.received)} bytes that differ from the payload")
        if log.peer_end != "close_notify":
            raise Failure(f"the client ended with {log.peer_end}, not close_notify")


def s23_tls12(ctx: Ctx) -> None:
    server = ctx.server("ecdsa", tls12_server())
    with check("OPEN_TLS PIN, default flags, to a TLS 1.2-only server answers 00,OK"):
        reply = ctx.expect_open(server.port, ctx.bind_ip, TRUST_PIN, pin=pin_of(ctx))
    check_reply_bits(reply, TLS12)
    sent = echo_round_trip(ctx, reply.handle, WRITE_MAX, seed=23)
    with check("CLOSE 00,OK; server log: TLSv1.2, a hardened suite, the bytes, close_notify"):
        ctx.expect(ctx.close(reply.handle), STATUS_OK, "CLOSE")
        log = one_connection(server)
        ctx.note("conn", log.as_dict())
        if log.version != "TLSv1.2" or not (log.cipher or "").startswith("ECDHE-"):
            raise Failure(f"server: {log.version} {log.cipher}")
        if bytes(log.received) != sent or log.peer_end != "close_notify":
            raise Failure(f"server got {len(log.received)} B, ended by {log.peer_end}")


def s5_refused_cert(ctx: Ctx) -> None:
    server = ctx.server("ecdsa", tls13_server())
    with check("BUNDLE to a local-PKI server answers exactly "
               f"{STATUS_UNKNOWN_ROOT.decode()!r} with no data"):
        r = ctx.open_tls(server.port, ctx.bind_ip, TRUST_BUNDLE, name=fx.FIXTURE_NAME)
        ctx.expect(r, STATUS_UNKNOWN_ROOT, "OPEN_TLS BUNDLE")
        if r.data:
            raise Failure(f"a refused OPEN answered {len(r.data)} data bytes")
    with check("PIN with a key that is not the server's answers 96 with no data"):
        r = ctx.open_tls(server.port, ctx.bind_ip, TRUST_PIN, pin=hashlib.sha256(b"x").digest())
        ctx.expect(r, STATUS_PIN_MISMATCH, "OPEN_TLS PIN")
    with check("nothing leaked: INFO $FF [2] = 0; the server received 0 application bytes"):
        ctx.entries(0, "after two refusals")
        logs = server.wait_connections(2)
        for log in logs:
            ctx.note("conn", log.as_dict())
        if any(log.received for log in logs):
            raise Failure("the server received application data on a refused connection")
        if ctx.args.strict_sni and logs and logs[0].sni != fx.FIXTURE_NAME:
            raise Failure(f"BUNDLE sent SNI {logs[0].sni!r}, expected {fx.FIXTURE_NAME!r}")


def s19e_abort_release(ctx: Ctx) -> None:
    if not ctx.link.can_abort_in_flight:
        raise NotOnRoute(f"the {ctx.link.name} route cannot ABORT a command in flight (REST only)")
    with check("TLS_INFO $FF answers before the OPEN (and claims no session)"):
        ctx.entries(0, "before the OPEN")
    server = ctx.server("ecdsa", fx.Behaviour(delay_handshake=SLOW_HANDSHAKE_SECONDS))
    command = open_tls_command(server.port, ctx.bind_ip, TRUST_PIN, pin=pin_of(ctx))
    with check(f"ABORT {ABORT_AFTER_SECONDS}s into an OPEN_TLS: bit 2 clears only when the "
               "command ends (ABORT never shortens it), within the client deadline"):
        aborted = ctx.link.abort_in_flight(command, ABORT_AFTER_SECONDS)
        ctx.note("abort", {"busy_at_abort": aborted.busy_at_abort,
                           "bit2_cleared_after": round(aborted.bit2_cleared_after, 2)})
        if not aborted.busy_at_abort:
            raise Failure("the OPEN had already answered when the ABORT was written, so this "
                          "run did not test an ABORT in PROCESSING")
        if aborted.bit2_cleared_after > BOUND_OPEN + 12:
            raise Failure(f"bit 2 cleared {aborted.bit2_cleared_after:.1f}s after the PUSH")
        # The server holds its answer SLOW_HANDSHAKE_SECONDS, so an OPEN that
        # ran to its end cannot clear bit 2 sooner.
        if aborted.bit2_cleared_after < SLOW_HANDSHAKE_SECONDS - 0.5:
            raise Failure(f"bit 2 cleared {aborted.bit2_cleared_after:.1f}s after the PUSH, before "
                          f"the server's {SLOW_HANDSHAKE_SECONDS:.0f}s delay: the ABORT shortened it")
    with check("`03 23 FF` (allowed in between): [2] = 1 and [14] = 1, the session is unclaimed"):
        cap = ctx.info_ff()
        if (cap.entries, cap.unclaimed) != (1, 1):
            raise Failure(f"[2] = {cap.entries}, [14] = {cap.unclaimed}")
    with check("`03 25` as the next command closes it: 1 byte, 01, 00,OK"):
        if (closed := ctx.release()) != 1:
            raise Failure(f"$25 closed {closed}")
    with check("a second `03 25` answers 0 (idempotent); INFO $FF [2] = 0, [14] = 0"):
        if (closed := ctx.release()) != 0:
            raise Failure(f"$25 closed {closed} the second time")
        cap = ctx.info_ff()
        if cap.entries or cap.unclaimed:
            raise Failure(f"[2] = {cap.entries}, [14] = {cap.unclaimed}")
    with check("the server saw the handshake complete, then close_notify + FIN from the device"):
        log = one_connection(server, timeout=20)
        ctx.note("conn", log.as_dict())
        if log.version != "TLSv1.3" or log.peer_end != "close_notify":
            raise Failure(f"server: {log.version}, ended by {log.peer_end}")


# -- S30: real hosts -----------------------------------------------------------

@dataclass
class Host:
    host: str
    hid: str = ""
    ip: str | None = None      # pinned, for the self-test's stand-ins
    port: int = 443
    record: dict = field(default_factory=dict)


def load_hosts() -> list[Host]:
    override = os.environ.get(HOSTS_ENV)
    if override:
        warn(f"{HOSTS_ENV} replaces the built-in hosts for this run: {override}")
        return [Host(h.strip(), f"X{i + 1}") for i, h in enumerate(override.split(",")) if h.strip()]
    entries = json.loads(HOSTS_FILE.read_text())["hosts"]
    return [Host(e["host"], e["id"], record=e) for e in entries]


def mac_context(cafile: str | None) -> tuple[ssl.SSLContext, str]:
    ctx = ssl.create_default_context(cafile=cafile)
    if cafile:
        return ctx, f"cafile {cafile}"
    if ctx.cert_store_stats().get("x509_ca", 0) == 0:
        try:
            import certifi  # noqa: PLC0415 - only when the default store is empty
            return ssl.create_default_context(cafile=certifi.where()), "certifi"
        except ImportError:
            pass
    return ctx, "the system default store"


def mac_view(host: Host, address: str, cafile: str | None) -> dict:
    """One verified handshake from this machine to address:port with SNI = host."""
    ctx, store = mac_context(cafile)
    with socket.create_connection((address, host.port), timeout=10) as raw:
        with ctx.wrap_socket(raw, server_hostname=host.host) as tls:
            der = tls.getpeercert(binary_form=True)
            chain = []
            getter = getattr(tls, "get_verified_chain", None)
            if getter is not None:
                for cert in getter():
                    chain.append(cert if isinstance(cert, bytes) else cert.public_bytes(1))
            return {"version": tls.version(), "spki": fx.spki_sha256(der).hex(),
                    "store": store, "chain": [describe_cert(c) for c in chain]}


def describe_cert(der: bytes) -> dict:
    cert = fx.x509.load_der_x509_certificate(der)
    return {"subject": cert.subject.rfc4514_string(), "issuer": cert.issuer.rfc4514_string(),
            "sha256": hashlib.sha256(der).hexdigest()}


def s30_real_hosts(ctx: Ctx) -> None:
    hosts = ctx.args.hosts if ctx.args.hosts is not None else load_hosts()
    failures = []
    for host in hosts:
        line = f"{host.hid} {host.host}"
        try:
            verdict = one_real_host(ctx, host)
            detail(f"PASS {line}: {verdict}")
            ctx.note("hosts", {"host": host.host, "verdict": "PASS", "detail": verdict})
        except Skipped as exc:
            failures.append(f"{line}: {exc}")
            ctx.note("hosts", {"host": host.host, "verdict": "UNREACHABLE", "detail": str(exc)})
        except Failure as exc:
            failures.append(f"{line}: {exc}")
            ctx.note("hosts", {"host": host.host, "verdict": "FAIL", "detail": str(exc)})
    with check("S30: INFO $FF [2] = 0 after the hosts"):
        ctx.entries(0, "after S30")
    if failures:
        raise Failure(" | ".join(failures))


def one_real_host(ctx: Ctx, host: Host) -> str:
    with check(f"{host.host}: step 1, this machine resolves it and verifies a TLS handshake"):
        try:
            address = host.ip or socket.getaddrinfo(host.host, host.port, socket.AF_INET)[0][4][0]
        except OSError as exc:
            raise Skipped(f"FAIL internet unreachable: host: dns {host.host}: {exc}") from exc
        try:
            first = mac_view(host, address, ctx.args.mac_cafile)
        except ssl.SSLError as exc:
            raise Skipped(f"FAIL internet path failed TLS verification (this machine): "
                          f"{host.host} at {address}: {exc}; possible causes: a captive portal, TLS "
                          f"interception, or a broken or misconfigured host; step 1 cannot "
                          f"tell which") from exc
        except OSError as exc:
            raise Skipped(f"FAIL internet unreachable: host: connect {host.host}: {exc}") from exc
        detail(f"{address}, {first['version']}, leaf SPKI {first['spki'][:16]}, via {first['store']}")
    with check(f"{host.host}: step 2, the device's OPEN_TCP to the name and to {address}"):
        for target in (host.host, address):
            r = ctx.cmd(open_tcp_command(target, host.port))
            if not r.ok:
                raise Skipped(f"FAIL internet unreachable: device: OPEN_TCP {target} "
                              f"{r.status.decode('latin-1')}")
            ctx.expect(ctx.close(r.data[0]), STATUS_OK, "CLOSE")
    with check(f"{host.host}: step 3, OPEN_TLS BUNDLE to {address} with name {host.host}"):
        r = ctx.open_tls(host.port, address, TRUST_BUNDLE, name=host.host)
        if not r.ok:
            chain = "; ".join(f"{c['subject']} <- {c['issuer']}" for c in first["chain"])
            raise Failure(f"FAIL {host.host}: {r.status.decode('latin-1')} (this machine's chain: "
                          f"{chain or 'not available'})")
        reply = OpenReply(r.data)
        handle = reply.handle
        info = ctx.info(handle)
    gone = False
    try:
        with check(f"{host.host}: step 4, version/group policy, "
                   f"leaf SPKI among this machine's view"):
            samples = {first["spki"]}
            for _ in range(MAC_SAMPLES - 1):
                samples.add(mac_view(host, address, ctx.args.mac_cafile)["spki"])
            if info.version == TLS12 and (info.suite not in TLS12_SUITES or not info.bits & 1):
                raise Failure(f"1.2 without the hardened rules: {info.as_dict()}")
            if info.version not in (TLS13, TLS12):
                raise Failure(f"INFO version 0x{info.version:04X}")
            if info.group not in GROUPS:
                raise Failure(f"INFO group 0x{info.group:04X}")
            if info.spki_sha256.hex() not in samples:
                time.sleep(RESAMPLE_AFTER_SECONDS if ctx.args.hosts is None else 0)
                samples.add(mac_view(host, address, ctx.args.mac_cafile)["spki"])
                if info.spki_sha256.hex() not in samples:
                    raise Failure(f"leaf key differs from this machine's view (same IP, same "
                                  f"offer): device {info.spki_sha256.hex()[:16]}, this machine "
                                  f"{sorted(s[:16] for s in samples)}")
        with check(f"{host.host}: step 5, an HTTP GET over the session starts with HTTP/1."):
            request = (f"GET / HTTP/1.1\r\nHost: {host.host}\r\nUser-Agent: u64-e2e\r\n"
                       f"Connection: close\r\n\r\n").encode("ascii")
            ctx.write(handle, request)
            got = bytearray()
            deadline = time.monotonic() + HTTP_READ_BUDGET_SECONDS
            while b"\r\n" not in got:
                reply_r = ctx.read(handle, 512)
                if reply_r.result.ok:
                    got += reply_r.payload
                    continue
                if reply_r.gone:
                    gone = True
                    break
                if reply_r.status != STATUS_NO_DATA_IDLE:
                    raise Failure(f"READ answered {reply_r.result.describe()}")
                if time.monotonic() > deadline:
                    raise Failure("no status line within 15 s")
                time.sleep(0.05)
            status_line = bytes(got).split(b"\r\n")[0]
            if not status_line.startswith(b"HTTP/1."):
                raise Failure(f"the response starts {bytes(got[:40])!r}")
            detail(f"{status_line.decode('latin-1')}, TLS 0x{info.version:04X}, "
                   f"group {GROUPS[info.group]}, handshake {info.handshake_ms} ms")
    finally:
        if not gone:
            ctx.close(handle)
    return (f"0x{info.version:04X} suite 0x{info.suite:04X} group {GROUPS.get(info.group)} "
            f"handshake {info.handshake_ms} ms, {status_line.decode('latin-1')}")


# -- Tier 2 ------------------------------------------------------------------

def s6_peer_close(ctx: Ctx, then: str = "close_notify") -> None:
    data = fx.payload(6, PEER_STREAM_BYTES)
    server = ctx.server("ecdsa", fx.Behaviour(send=data, then=then))
    end_status = STATUS_PEER_CLOSED if then == "close_notify" else STATUS_NO_NOTIFY
    with check(f"all {PEER_STREAM_BYTES} B delivered BEFORE $0000 + {end_status.decode()!r}"):
        reply = ctx.expect_open(server.port, ctx.bind_ip, TRUST_PIN, pin=pin_of(ctx))
        got, end = ctx.read_until(reply.handle, PEER_STREAM_BYTES + 1)
        if got != data:
            raise Failure(f"{len(got)} of {PEER_STREAM_BYTES} bytes, or different bytes")
        if end is None or end.header != 0 or end.status != end_status:
            raise Failure(f"the stream ended with {end.result.describe() if end else 'nothing'}")
    with check("the handle is GONE: a CLOSE answers 12,ERROR ON CLOSE: 9; INFO [2] = 0"):
        ctx.expect(ctx.close(reply.handle), STATUS_CLOSE_UNOWNED, "CLOSE after the end")
        ctx.entries(0, "after the end")


def s7_truncation(ctx: Ctx) -> None:
    s6_peer_close(ctx, then="fin")


def s7c_rst_handshake(ctx: Ctx) -> None:
    server = ctx.server(None, fx.Behaviour(rst_after_client_hello=True))
    with check("an RST after the ClientHello: `13,TLS HANDSHAKE FAILED: PEER RESET: 104`"):
        r = ctx.open_tls(server.port, ctx.bind_ip, TRUST_PIN, pin=pin_of(ctx))
        ctx.expect(r, b"13,TLS HANDSHAKE FAILED: PEER RESET: 104", "OPEN_TLS")
        ctx.entries(0, "after the reset")


def s10_handshake_timeout(ctx: Ctx) -> None:
    server = ctx.server(None, fx.Behaviour(stall=True))
    with check("a server that never answers: 15 within HANDSHAKE_MS + 2 s; nothing leaked"):
        r = ctx.open_tls(server.port, ctx.bind_ip, TRUST_PIN, pin=pin_of(ctx))
        ctx.expect(r, STATUS_HS_TIMEOUT, "OPEN_TLS")
        if r.elapsed > HANDSHAKE_MS + 2 + 1:
            raise Failure(f"15 after {r.elapsed:.1f}s")
        ctx.entries(0, "after the timeout")


def s11_cap(ctx: Ctx) -> None:
    server = ctx.server("ecdsa", tls13_server())
    handles = []
    try:
        with check("two OPENs succeed, the third answers 91; after one CLOSE an OPEN works"):
            handles.append(ctx.expect_open(server.port, ctx.bind_ip, pin=pin_of(ctx)).handle)
            handles.append(ctx.expect_open(server.port, ctx.bind_ip, pin=pin_of(ctx)).handle)
            ctx.expect(ctx.open_tls(server.port, ctx.bind_ip, pin=pin_of(ctx)),
                       STATUS_TLS_IN_USE, "third OPEN")
            ctx.expect(ctx.close(handles.pop()), STATUS_OK, "CLOSE")
            handles.append(ctx.expect_open(server.port, ctx.bind_ip, pin=pin_of(ctx)).handle)
    finally:
        for handle in handles:
            ctx.close(handle)
    ctx.entries(0, "after the cap")


def s12_open_close(ctx: Ctx) -> None:
    server = ctx.server("ecdsa", tls13_server())
    with check("20 x (OPEN PIN, CLOSE): every one 00,OK, INFO [2] = 0 after"):
        for _ in range(20):
            handle = ctx.expect_open(server.port, ctx.bind_ip, pin=pin_of(ctx)).handle
            ctx.expect(ctx.close(handle), STATUS_OK, "CLOSE")
        cap = ctx.entries(0, "after 20 cycles")
        detail(f"module block_max {cap.block_max_ms} ms (heap is not exported over UCI)")


def s13_tickets_big_record(ctx: Ctx) -> None:
    data = fx.payload(13, BIG_RECORD_BYTES)
    server = ctx.server("ecdsa", fx.Behaviour(num_tickets=10, send=data, then="echo"))
    reply = ctx.expect_open(server.port, ctx.bind_ip, pin=pin_of(ctx))
    try:
        with check("10 tickets, then one 16 KB record read with maxlen 1, 100, 893, 894, 1472"):
            got, end = ctx.read_until(reply.handle, BIG_RECORD_BYTES,
                                      maxlens=(1, 100, 893, 894, 1472), budget=120)
            if got != data:
                raise Failure(f"{len(got)} bytes, or different bytes ({end})")
        echo_round_trip(ctx, reply.handle, 100, seed=131)
    finally:
        ctx.close(reply.handle)


def s3_custom_ca(ctx: Ctx) -> None:
    with check("clear the CA (`03 22 00 00 00 00`) answers 00,OK, b3 clear"):
        ctx.expect(ctx.cmd(trust_ca_command(0, 0, b"")), STATUS_OK, "clear")
        if ctx.info_ff().ready & 0x08:
            raise Failure("b3 still set after the clear")
    with check("CUSTOM with nothing loaded answers 97,NO CUSTOM CA"):
        ctx.expect(ctx.open_tls(1, ctx.bind_ip, TRUST_CUSTOM, name=fx.FIXTURE_NAME),
                   STATUS_NO_CUSTOM_CA, "OPEN CUSTOM")
    with check(f"the test root ({len(ctx.pki.root_der)} B DER) uploads in chunks: 00,OK, b3 set"):
        ctx.expect(ctx.upload_ca(ctx.pki.root_der), STATUS_OK, "final chunk")
        if not ctx.info_ff().ready & 0x08:
            raise Failure("b3 clear after the upload")
    for chain in ("ecdsa", "rsa"):
        server = ctx.server(chain, tls13_server())
        with check(f"CUSTOM {chain}: OPEN 00,OK; INFO SPKI = the server leaf's; echo"):
            reply = ctx.expect_open(server.port, ctx.bind_ip, TRUST_CUSTOM, name=fx.FIXTURE_NAME)
            info = ctx.info(reply.handle)
            if info.spki_sha256 != pin_of(ctx, chain):
                raise Failure("INFO SPKI is not the leaf this machine served")
            echo_round_trip(ctx, reply.handle, 500, seed=3)
            ctx.expect(ctx.close(reply.handle), STATUS_OK, "CLOSE")
    for chain, prefix in (("wrong", b"94,"), ("expired", b"95,"), ("future", b"95,"),
                          ("unknown", b"93,")):
        server = ctx.server(chain, tls13_server())
        with check(f"CUSTOM must-fail {chain}: {prefix.decode()} with no data, nothing leaked"):
            r = ctx.open_tls(server.port, ctx.bind_ip, TRUST_CUSTOM, name=fx.FIXTURE_NAME)
            if not r.status.startswith(prefix) or r.data:
                raise Failure(r.describe())
    ctx.entries(0, "after the must-fails")
    ctx.cmd(trust_ca_command(0, 0, b""))


def s14_reset_cleanup(ctx: Ctx) -> None:
    server = ctx.server("ecdsa", fx.Behaviour(then="hold"))
    with check("two sessions and a LOADED CA, then a C64 reset"):
        ctx.expect_open(server.port, ctx.bind_ip, pin=pin_of(ctx))
        ctx.expect_open(server.port, ctx.bind_ip, pin=pin_of(ctx))
        ctx.expect(ctx.upload_ca(ctx.pki.root_der), STATUS_OK, "upload")
        ctx.reset_c64()
    with check("the server sees both TCP connections end; INFO [2] = 0, b3 clear"):
        logs = server.wait_connections(2, timeout=10)
        ends = [log.peer_end for log in logs]
        if len(logs) != 2 or "open" in ends:
            raise Failure(f"connection ends seen by the server: {ends}")
        cap = ctx.info_ff()
        if cap.entries or cap.ready & 0x08:
            raise Failure(f"[2] = {cap.entries}, [4] = 0x{cap.ready:02X}")
    with check("OPEN CUSTOM after the reset answers 97,NO CUSTOM CA"):
        ctx.expect(ctx.open_tls(server.port, ctx.bind_ip, TRUST_CUSTOM, name=fx.FIXTURE_NAME),
                   STATUS_NO_CUSTOM_CA, "OPEN CUSTOM")


def s19_params(ctx: Ctx) -> None:
    server = ctx.server("ecdsa", tls13_server())
    port, ip, pin = server.port, ctx.bind_ip, pin_of(ctx)
    head = bytes([3, 0x21, port & 0xFF, port >> 8])
    cases = [
        ("trust 3", head + bytes([3, 0]) + ip.encode(), STATUS_OUT_OF_RANGE),
        ("flags $02", head + bytes([2, 2]) + pin + ip.encode(), STATUS_OUT_OF_RANGE),
        ("flags $80", head + bytes([2, 0x80]) + pin + ip.encode(), STATUS_OUT_OF_RANGE),
        ("all-zero pin", head + bytes([2, 0]) + bytes(32) + ip.encode(), STATUS_OUT_OF_RANGE),
        ("empty host", head + bytes([2, 0]) + pin, STATUS_INVALID_PARAMS),
        ("host 00 00", head + bytes([2, 0]) + pin + ip.encode() + b"\0\0", STATUS_INVALID_PARAMS),
        ("254-byte host", head + bytes([2, 0]) + pin + b"a" * 254, STATUS_INVALID_PARAMS),
        ("short pin", head + bytes([2, 0]) + pin[:20], STATUS_INVALID_PARAMS),
        ("BUNDLE, IPv4 host, no name", head + bytes([0, 0]) + ip.encode(), STATUS_INVALID_PARAMS),
        ("BUNDLE, explicit IPv4 name", head + bytes([0, 0]) + b"x.test\x00" + ip.encode(),
         STATUS_INVALID_PARAMS),
        ("BUNDLE, host 127.1", head + bytes([0, 0]) + b"127.1", STATUS_INVALID_PARAMS),
        ("INFO, 2 bytes", bytes([3, 0x23]), STATUS_INVALID_PARAMS),
        ("INFO, 4 bytes", bytes([3, 0x23, 0xFF, 0x00]), STATUS_INVALID_PARAMS),
    ]
    for label, command, expected in cases:
        with check(f"{label} -> {expected.decode()}"):
            r = ctx.cmd(command)
            ctx.expect(r, expected, label)
            if r.data:
                raise Failure(f"{len(r.data)} data bytes with a refusal")
    with check("flags $01 (REQUIRE_TLS13) is accepted, and `host 00` is a host with no name"):
        r = ctx.cmd(head + bytes([2, 1]) + pin + ip.encode() + b"\0")
        ctx.expect(r, STATUS_OK, "flags $01")
        handle = r.data[0]
    try:
        with check("READ maxlen 0 and 1473 on a TLS handle -> 82"):
            ctx.expect(ctx.cmd(read_command(handle, 0)), STATUS_OUT_OF_RANGE, "maxlen 0")
            ctx.expect(ctx.cmd(read_command(handle, 1473)), STATUS_OUT_OF_RANGE, "maxlen 1473")
    finally:
        ctx.close(handle)
    with check("a closed port -> 11, an unresolvable host -> 84"):
        probe = socket.socket()
        probe.bind((ip, 0))
        closed_port = probe.getsockname()[1]
        probe.close()
        r = ctx.open_tls(closed_port, ip, TRUST_PIN, pin=pin)
        if status_code(r.status) != 11:
            raise Failure(f"closed port: {r.describe()}")
        r = ctx.open_tls(443, UNRESOLVABLE, TRUST_PIN, pin=pin)
        if status_code(r.status) != 84:
            raise Failure(f"unresolvable: {r.describe()}")
    ctx.entries(0, "after the parameter cases")


def s19e2_release_spares_claimed(ctx: Ctx) -> None:
    server = ctx.server("ecdsa", tls13_server())
    a = ctx.expect_open(server.port, ctx.bind_ip, pin=pin_of(ctx)).handle
    try:
        with check("READ on A (claims it), OPEN B (unclaimed), `03 25` -> 1"):
            ctx.read(a, 16)
            b = ctx.expect_open(server.port, ctx.bind_ip, pin=pin_of(ctx)).handle
            if (closed := ctx.release()) != 1:
                raise Failure(f"$25 closed {closed}")
        with check("A still echoes; a READ on B's number -> $FFFF + 02,NO DATA: 9 (S19e2c)"):
            echo_round_trip(ctx, a, 64, seed=192)
            r = ctx.read(b, 16)
            if r.header != 0xFFFF or r.status != STATUS_NO_DATA_GONE:
                raise Failure(f"READ {b}: {r.result.describe()}")
    finally:
        ctx.close(a)
    ctx.entries(0, "after S19e2")


def s19e2b_info_does_not_claim(ctx: Ctx) -> None:
    if not ctx.link.can_abort_in_flight:
        raise NotOnRoute(f"the {ctx.link.name} route cannot ABORT a command in flight (REST only)")
    ctx.entries(0, "before the OPENs")
    fast = ctx.server("ecdsa", tls13_server())
    slow = ctx.server("ecdsa", fx.Behaviour(delay_handshake=SLOW_HANDSHAKE_SECONDS))
    a = ctx.expect_open(fast.port, ctx.bind_ip, pin=pin_of(ctx)).handle
    try:
        with check("OPEN A, accept it, unused; ABORTed OPEN B; `03 23 FF` [14] = 1; $25 -> 1"):
            ctx.link.abort_in_flight(open_tls_command(slow.port, ctx.bind_ip, TRUST_PIN,
                                                      pin=pin_of(ctx)), ABORT_AFTER_SECONDS)
            cap = ctx.info_ff()
            if (cap.entries, cap.unclaimed) != (2, 1):
                raise Failure(f"[2] = {cap.entries}, [14] = {cap.unclaimed}")
            if (closed := ctx.release()) != 1:
                raise Failure(f"$25 closed {closed}")
        with check("A still echoes"):
            echo_round_trip(ctx, a, 64, seed=193)
    finally:
        ctx.close(a)


def s22_groups(ctx: Ctx) -> None:
    for curve, group in ((None, 0x001D), ("prime256v1", 0x0017)):
        server = ctx.server("ecdsa", fx.Behaviour(ecdh_curve=curve))
        label = "a default 1.3 server" if curve is None else "a P-256-only server (HRR)"
        with check(f"{label}: OPEN reply version 0x0304, group 0x{group:04X}"):
            reply = ctx.expect_open(server.port, ctx.bind_ip, pin=pin_of(ctx))
            ctx.close(reply.handle)
            if (reply.version, reply.group) != (TLS13, group):
                raise Failure(json.dumps(reply.as_dict()))


def s24_require13(ctx: Ctx) -> None:
    server = ctx.server("ecdsa", tls12_server())
    with check("REQUIRE_TLS13 to a 1.2-only server: 14 with 70, or 18 with 0x0303 (recorded)"):
        r = ctx.open_tls(server.port, ctx.bind_ip, TRUST_PIN, flags=1, pin=pin_of(ctx))
        if r.status not in (b"14,TLS ALERT RECEIVED: 70", b"18,TLS VERSION REFUSED: 0x0303"):
            raise Failure(r.describe())
        ctx.note("variant", r.status.decode())
        ctx.entries(0, "after the refusal")


def s18_block_max(ctx: Ctx) -> None:
    server = ctx.server("ecdsa", tls13_server())
    with check("after a handshake, INFO module_block_max_ms < 50 "
               "(it is the longest the module's dispatcher blocked since boot)"):
        handle = ctx.expect_open(server.port, ctx.bind_ip, pin=pin_of(ctx)).handle
        ctx.close(handle)
        cap = ctx.info_ff()
        ctx.note("block_max_ms", cap.block_max_ms)
        if cap.block_max_ms >= 50:
            raise Failure(f"block_max {cap.block_max_ms} ms")


def s32_write_stall(ctx: Ctx) -> None:
    """S32: a WRITE stall, and the CLOSE time measured on the stalled socket."""
    if ctx.link.name == "rest":
        raise NotOnRoute("hundreds of 892-B WRITEs cost a REST call per byte; run it native")
    server = ctx.server("ecdsa", fx.Behaviour(then="stall", rcvbuf=4096))
    reply = ctx.expect_open(server.port, ctx.bind_ip, pin=pin_of(ctx))
    chunk = fx.payload(32, WRITE_MAX)
    with check("WRITE until TCP is full: FFFF + 12 or 17, sticky; never a partial count"):
        for count in range(1, S32_MAX_WRITES + 1):
            r = ctx.cmd(write_command(reply.handle, chunk))
            if r.ok:
                if r.data[:2] != WRITE_MAX.to_bytes(2, "little"):
                    raise Failure(f"WRITE {count}: partial count {r.data[:2].hex(' ')}")
                continue
            ctx.note("stall", {"writes_ok": count - 1, "status": r.status.decode("latin-1"),
                               "elapsed": round(r.elapsed, 2)})
            if r.data[:2] != b"\xff\xff" or status_code(r.status) not in (12, 17):
                raise Failure(f"WRITE {count}: {r.describe()}")
            again = ctx.cmd(write_command(reply.handle, b"x"))
            if again.status != r.status:
                raise Failure(f"not sticky: then {again.describe()}")
            break
        else:
            raise Failure(f"{S32_MAX_WRITES} WRITEs never stalled")
    with check("CLOSE on the stalled TLS socket answers 00,OK within 12 s (measured)"):
        r = ctx.close(reply.handle)
        ctx.note("close_elapsed", round(r.elapsed, 3))
        ctx.expect(r, STATUS_OK, "CLOSE")
        detail(f"CLOSE took {r.elapsed:.2f}s (expected: about 1 s at most, then RST)")


def s34_big_curves(ctx: Ctx) -> None:
    """Measurement: P-384 / P-521 under 1.3 and 1.2, CUSTOM (the TLS-task stack case)."""
    ctx.expect(ctx.cmd(trust_ca_command(0, 0, b"")), STATUS_OK, "clear")
    ctx.expect(ctx.upload_ca(ctx.pki.root_der), STATUS_OK, "upload")
    for curve, group in (("secp384r1", 0x0018), ("secp521r1", 0x0019)):
        for label, behaviour in (("1.3", fx.Behaviour(ecdh_curve=curve)),
                                 ("1.2", tls12_server(ecdh_curve=curve))):
            server = ctx.server("ecdsa", behaviour)
            with check(f"CUSTOM, TLS {label}, {curve}: OPEN 00,OK, group 0x{group:04X}"):
                reply = ctx.expect_open(server.port, ctx.bind_ip, TRUST_CUSTOM,
                                        name=fx.FIXTURE_NAME)
                info = ctx.info(reply.handle)
                ctx.close(reply.handle)
                cap = ctx.info_ff()
                ctx.note("timing", {"curve": curve, "tls": label, "handshake_ms": info.handshake_ms,
                                    "block_max_ms": cap.block_max_ms})
                detail(f"handshake {info.handshake_ms} ms, block_max {cap.block_max_ms} ms")
                if reply.group != group:
                    raise Failure(f"group 0x{reply.group:04X}")
    ctx.cmd(trust_ca_command(0, 0, b""))


def s33_open_tcp_duration(ctx: Ctx) -> None:
    """Measurement: how long a plain OPEN_TCP takes to give up."""
    for label, host in (("an address that drops SYNs", BLACKHOLE),
                        ("an unresolvable name", UNRESOLVABLE)):
        check_start(f"OPEN_TCP to {label}: time to 11 / 84")
        r = ctx.link.command(open_tcp_command(host, 80))
        ctx.note("open_tcp", {"target": label, "status": r.status.decode("latin-1"),
                              "elapsed": round(r.elapsed, 2), "aborted": r.aborted})
        if r.ok:
            ctx.close(r.data[0])
        check_ok(f"{r.status.decode('latin-1') or 'ABORTed'} after {r.elapsed:.1f}s")


SCENARIOS = [
    Scenario("S0", "startup-sweep", 1, "control", "every CLOSE 0..15 answers 00 or 12,…: 9",
             s0_startup_sweep),
    Scenario("S1", "capability", 1, "tls", "16-byte v1 record, ready 0x07, length checks",
             s1_capability),
    Scenario("S20", "plain-tcp", 1, "control", "OPEN_TCP/WRITE/READ/CLOSE round trip",
             s20_plain_tcp, ("peer",)),
    Scenario("S2", "pin-echo", 1, "tls", "PIN, TLS 1.3, 4096 B echo, INFO SPKI = pin",
             s2_pin_echo, ("fixture",)),
    Scenario("S23", "tls12-fallback", 1, "tls", "0x0303, a hardened suite, EMS bit, echo",
             s23_tls12, ("fixture",)),
    Scenario("S5", "refused-cert", 1, "tls", "exactly 93 0x00000008; 96 for a wrong pin",
             s5_refused_cert, ("fixture",)),
    Scenario("S19e", "abort-then-release", 1, "tls", "ABORTed OPEN: [14] = 1, $25 -> 1, then 0",
             s19e_abort_release, ("fixture",)),
    Scenario("S30", "bundle-real-hosts", 1, "tls", "BUNDLE to H1-H3, SPKI as seen here, HTTP/1.",
             s30_real_hosts, ("internet", "crypto")),
    Scenario("S6", "peer-close-notify", 2, "tls", "3000 B then $0000 + 01; CLOSE -> 12,…: 9",
             s6_peer_close, ("fixture",)),
    Scenario("S7", "peer-fin-truncation", 2, "tls", "3000 B then $0000 + 05", s7_truncation,
             ("fixture",)),
    Scenario("S7c", "rst-mid-handshake", 2, "tls", "13 PEER RESET: 104", s7c_rst_handshake,
             ("fixture",)),
    Scenario("S10", "handshake-timeout", 2, "tls", "15 within 17 s", s10_handshake_timeout,
             ("fixture",)),
    Scenario("S11", "session-cap", 2, "tls", "2 OK, 3rd 91, CLOSE one, OPEN OK", s11_cap,
             ("fixture",)),
    Scenario("S12", "open-close-x20", 2, "tls", "20 cycles, INFO [2] = 0", s12_open_close,
             ("fixture",)),
    Scenario("S13", "tickets-big-record", 2, "tls", "10 tickets; 16 KB record byte exact",
             s13_tickets_big_record, ("fixture",)),
    Scenario("S3", "custom-ca", 2, "tls", "CUSTOM echo ECDSA+RSA, SPKI; 94/95/95/93; 97",
             s3_custom_ca, ("fixture",)),
    Scenario("S19", "params", 2, "tls", "81/82 table, flags $01 OK, maxlen 82, 11, 84",
             s19_params, ("fixture",)),
    Scenario("S19e2", "release-spares-claimed", 2, "tls", "$25 -> 1; A echoes; B -> 02: 9",
             s19e2_release_spares_claimed, ("fixture",)),
    Scenario("S19e2b", "info-ff-does-not-claim", 2, "tls", "[14] = 1 after 03 23 FF; $25 -> 1",
             s19e2b_info_does_not_claim, ("fixture",)),
    Scenario("S22", "group-negotiation", 2, "tls", "0x001D default; 0x0017 via HRR",
             s22_groups, ("fixture",)),
    Scenario("S24", "require-tls13-vs-1.2", 2, "tls", "14 with 70, or 18 with 0x0303",
             s24_require13, ("fixture",)),
    Scenario("S18", "dispatcher-not-blocked", 2, "tls", "block_max_ms < 50 after a handshake",
             s18_block_max, ("fixture",)),
    Scenario("S14", "reset-cleanup", 2, "tls", "reset closes both, b3 clear, then 97",
             s14_reset_cleanup, ("fixture",)),
    Scenario("S32", "write-stall-close-cap", 2, "tls", "FFFF + 12/17 sticky; CLOSE time",
             s32_write_stall, ("fixture",), default=False),
    Scenario("S34", "p384-p521-custom", 2, "measurement", "handshake ms for P-384/P-521, 1.3/1.2",
             s34_big_curves, ("fixture",), default=False),
    Scenario("S33", "open-tcp-duration", 2, "measurement", "seconds to 11 / 84",
             s33_open_tcp_duration, (), default=False),
]
BY_NAME = {s.name: s for s in SCENARIOS} | {s.sid: s for s in SCENARIOS}


# -- the run -----------------------------------------------------------------

def judge(outcome: Outcome) -> None:
    """The signature: GREEN, RED (21 to a TLS command), or a plain FAIL."""
    s = outcome.scenario
    unknown = outcome.evidence.get("unknown_command")
    if outcome.verdict == "PASS":
        outcome.signature = {"tls": "GREEN", "control": "CONTROL-PASS",
                             "measurement": "MEASURED"}[s.kind]
    elif outcome.verdict in ("SKIP", "N/A", "ERROR"):
        outcome.signature = outcome.verdict
    elif unknown and outcome.first_tls_status == STATUS_UNKNOWN_COMMAND.decode():
        outcome.signature = "RED"
    else:
        outcome.signature = "FAIL"


def run_scenario(ctx: Ctx, scenario: Scenario, route: str) -> Outcome:
    outcome = Outcome(scenario, route)
    ctx.outcome = outcome
    section(f"{scenario.sid} {scenario.name} [tier {scenario.tier}, {scenario.kind}, {route}]")
    started = time.monotonic()
    try:
        scenario.fn(ctx)
        outcome.verdict = "PASS"
    except NotOnRoute as exc:
        outcome.verdict, outcome.reason = "N/A", str(exc)
        check_start(f"{scenario.sid} applies to this route")
        check_skip(str(exc))
    except Skipped as exc:
        outcome.verdict, outcome.reason = "SKIP", str(exc)
        check_start(f"{scenario.sid} can run here")
        check_skip(str(exc))
    except LinkLost:
        ctx.outcome = None
        ctx.close_servers()
        raise
    except Failure as exc:
        outcome.verdict, outcome.reason = "FAIL", format_exception(exc)
    except Exception as exc:  # a bug in the suite must not pass for the firmware's
        outcome.verdict = "FAIL"
        outcome.reason = f"suite error: {type(exc).__name__}: {exc}"
        detail(traceback.format_exc())
    finally:
        outcome.elapsed = time.monotonic() - started
        if ctx.outcome is not None:
            ctx.close_servers()
        judge(outcome)
        detail(f"{scenario.sid}: {outcome.verdict} ({outcome.signature})"
               + (f": {outcome.reason}" if outcome.reason else ""))
        ctx.outcome = None
    if outcome.verdict == "FAIL":
        # Leave nothing behind for the next scenario (the startup sweep).
        try:
            ctx.sweep()
        except Failure as exc:
            warn(f"the sweep after {scenario.sid} failed: {exc}")
    return outcome


def expectation_met(outcomes: list[Outcome], expect: str) -> tuple[bool, list[str]]:
    """Whether the run shows what --expect asks, from evidence only.

    A SKIP or ERROR of any selected scenario is a problem; only N/A (by design
    not on this route) is set aside. green: every control passes, every TLS
    scenario is GREEN, no measurement failed. red: every control passes and
    every TLS scenario is RED. Either way each route must have produced at least
    one TLS outcome of the expected signature, so zero evidence never passes.
    """
    problems = []
    evidence: dict[str, int] = {}
    routes = []
    for o in outcomes:
        label = f"{o.scenario.sid} {o.scenario.name} [{o.route}]"
        if o.route not in routes:
            routes.append(o.route)
            evidence[o.route] = 0
        if o.verdict == "N/A":
            continue
        if o.verdict in ("SKIP", "ERROR"):
            problems.append(f"{label}: {o.verdict}, so it says nothing: {o.reason}")
            continue
        kind = o.scenario.kind
        if kind == "control":
            if o.verdict != "PASS":
                problems.append(f"{label}: the control failed, so the setup is not sound: {o.reason}")
        elif kind == "measurement":
            if expect == "green" and o.verdict != "PASS":
                problems.append(f"{label}: the measurement failed: {o.reason}")
        elif expect == "green":
            if o.signature == "GREEN":
                evidence[o.route] += 1
            else:
                problems.append(f"{label}: {o.signature}: {o.reason}")
        elif o.signature == "RED":
            evidence[o.route] += 1
        else:
            problems.append(f"{label}: {o.signature} where RED (21 to its first TLS command) "
                            f"was expected: {o.reason or 'it passed'}")
    if not outcomes:
        problems.append("no scenario ran")
    # A scenario selected for this run but N/A on every route of it ran nowhere
    # (e.g. --route native with S19e): never evidence.
    for sid in dict.fromkeys(o.scenario.sid for o in outcomes):
        if all(o.verdict == "N/A" for o in outcomes if o.scenario.sid == sid):
            problems.append(f"{sid}: selected, but N/A on every route of this run, so it ran nowhere")
    for route in routes:
        if not evidence[route]:
            problems.append(f"route {route}: no TLS scenario showed {expect.upper()}, so this run "
                            f"is no evidence either way")
    return not problems, problems


def ancestor_pids() -> set[int]:
    pids, pid = set(), os.getpid()
    for _ in range(16):
        try:
            parent = int(subprocess.run(["ps", "-o", "ppid=", "-p", str(pid)],
                                        capture_output=True, text=True, timeout=5,
                                        check=False).stdout.strip() or 0)
        except (OSError, ValueError, subprocess.SubprocessError):
            break
        if parent <= 1:
            break
        pids.add(parent)
        pid = parent
    return pids


LOCK_PID_ENV = "NET_TLS_LOCK_PID"


def lock_check(host: str, require: bool = False) -> dict:
    """The DeviceLock of the optional c64-test-harness package (https://github.com/JC-000/c64-test-harness):
    refuse if another live process holds this device.

    With `require` (or when a locking wrapper exported NET_TLS_LOCK_PID),
    the lock must be HELD, by that very process, and that process must be this
    one or an ancestor of it. A free lock is then a refusal: the user has left
    the locked shell, or never entered it.
    """
    try:
        from c64_test_harness.backends.device_lock import DeviceLock  # noqa: PLC0415
    except ImportError as exc:
        # The DeviceLock comes from the optional c64-test-harness package, which this tree does not
        # require; without it there is nothing to check, unless the run was told to require a lock.
        if require or os.environ.get(LOCK_PID_ENV):
            raise Failure(f"--require-lock, but the harness DeviceLock is not importable ({exc}); "
                          f"refusing to run against {host}") from exc
        print(f"warning: c64_test_harness (https://github.com/JC-000/c64-test-harness) is not installed: "
              f"not checking whether another run holds {host} ({exc})", file=sys.stderr)
        return {"lock": "not checked (c64_test_harness not installed)"}
    marker = os.environ.get(LOCK_PID_ENV, "")
    require = require or bool(marker)
    if require:
        if not marker.isdigit():
            raise Failure(f"--require-lock, but {LOCK_PID_ENV} is not set: this shell is not the "
                          f"shell of a locking wrapper. Nothing was run or changed")
        if int(marker) == os.getpid() and DeviceLock.held_by_this_process(host):
            return {"lock": f"held by this process ({marker})"}
        holder = DeviceLock.foreign_holder(host)
        if (not holder or holder.get("pid") != int(marker)
                or int(marker) not in ancestor_pids()):
            raise Failure(f"--require-lock: the session lock on {host} is not held by our "
                          f"locking wrapper (pid {marker}); holder {holder or 'none'}. Nothing "
                          f"was run or changed")
        return {"lock": f"held by our locking wrapper, pid {marker}"}
    holder = DeviceLock.foreign_holder(host)
    if not holder:
        return {"lock": "free"}
    if holder.get("pid") in ancestor_pids():
        return {"lock": f"held by our ancestor pid {holder.get('pid')} (a locking wrapper)"}
    raise Failure(f"{host} is held by pid {holder.get('pid')} ({holder}); another run is using "
                  f"the device. Run under its lock, or wait")


def apply_settings(device, set_wifi: bool, original: dict, header: dict) -> dict:
    """Read every setting first; refuse before writing anything; then set and read back.

    `original` is the caller's, filled as each setting is written, so whatever
    happens after a write the caller can restore it.
    """
    before: dict[str, str] = {}
    header["settings_before"] = before
    with check("read the settings this suite depends on, before changing any"):
        for category, item, _value, _restore in SETTINGS:
            try:
                before[item] = str(device.configs.current(category, item))
            except Failure:
                if item == REQUIRED_SETTING:
                    raise
                before[item] = "absent"
        detail(json.dumps(before))
    wifi = before.get(WIFI_SETTING, "absent")
    if wifi != "absent" and wifi.strip() != "Enabled" and not set_wifi:
        # One-way on the U64: switching it on cannot be undone by this suite, so
        # only an explicit --set-wifi may do it.
        raise SetupRefused(f"{WIFI_SETTING} is {wifi!r}; this suite needs it 'Enabled' and does "
                           f"not switch it on without --set-wifi (one-way on the U64). Nothing "
                           f"was changed")
    applied = {}
    for category, item, value, restore in SETTINGS:
        with check(f"set {category} / {item} = {value}"):
            current = before[item]
            if current == "absent":
                check_warn("not on this device; recorded as absent")
                applied[item] = "absent"
                continue
            if current.strip() != value:
                if restore:
                    original[(category, item)] = current
                device.configs.set(category, item, value)
            applied[item] = str(device.configs.current(category, item))
            if applied[item].strip() != value:
                raise Failure(f"read back {applied[item]!r}")
            detail(f"{current!r} -> {applied[item]!r}" + ("" if restore else " (one-way: kept)"))
    return applied


class SetupRefused(Failure):
    """Setup refused before changing anything on the device."""


def wait_ready(link: Link) -> dict:
    """With TLS, wait for INFO [4] b0-b2; on stock, the 21 says there is nothing to wait for."""
    deadline = time.monotonic() + READY_WAIT_SECONDS
    while True:
        r = link.command(info_command(INFO_CAPABILITY))
        if r.status == STATUS_UNKNOWN_COMMAND:
            return {"tls_info": "21,UNKNOWN COMMAND"}
        if r.ok and len(r.data) == INFO_CAPABILITY_BYTES:
            cap = Capability(r.data)
            if cap.ready & READY_BITS == READY_BITS or time.monotonic() > deadline:
                return cap.as_dict()
        elif time.monotonic() > deadline:
            return {"tls_info": r.status.decode("latin-1")}
        time.sleep(2)


def git_head() -> str:
    try:
        return subprocess.run(["git", "-C", str(HERE), "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True, timeout=5,
                              check=False).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="TLS sockets on the UCI network target: "
                                                 "RED on stock firmware, GREEN with TLS.")
    cli.add_device_arguments(parser, password=None, timeout=30.0, colour=False)
    parser.add_argument("--test", action="append",
                        help="A scenario id or name (repeatable), 'all', or 'tier1'. Default: "
                             "every default scenario.")
    parser.add_argument("--route", action="append", choices=["all", "rest", "native"],
                        help="How to reach $DF1C-$DF1F. Default: both.")
    parser.add_argument("--expect", choices=["green", "red"], default="green",
                        help="green: every TLS scenario must pass. red: every TLS scenario "
                             "must show the stock signature (21) and every control pass.")
    parser.add_argument("--results", type=Path, help="Where run.json and wire.jsonl go.")
    parser.add_argument("--label", default="", help="Free text recorded in run.json.")
    parser.add_argument("--module-minor", type=int,
                        help="S1 also requires INFO [7..8] to equal this (e.g. 8).")
    parser.add_argument("--mac-cafile", help="S30 step 1/4: verify with this CA file instead "
                                             "of this machine's default store.")
    parser.add_argument("--strict-sni", action="store_true",
                        help="S5 also requires the BUNDLE ClientHello to carry the SNI.")
    parser.add_argument("--require-lock", action="store_true",
                        help="Refuse unless the device's DeviceLock is held by the process in "
                             "NET_TLS_LOCK_PID, this process or an ancestor of it.")
    parser.add_argument("--set-wifi", action="store_true",
                        help="Switch 'WiFi Enabled' on if it is off. One-way on the U64 (a "
                             "reboot is the only way back), so without it the suite refuses.")
    parser.add_argument("--keep-config", action="store_true",
                        help="Don't restore the settings this suite changed.")
    parser.add_argument("--list", action="store_true", help="List the scenarios and exit.")
    args = parser.parse_args(argv)
    args.hosts = None
    return args


def select(args) -> list[Scenario]:
    """The scenarios asked for, always led by S0: every route starts with the startup sweep."""
    wanted = args.test or []
    if not wanted:
        return [s for s in SCENARIOS if s.default]
    if "all" in wanted:
        return list(SCENARIOS)
    chosen = [SCENARIOS[0]]
    for s in SCENARIOS:
        if s is SCENARIOS[0]:
            continue
        if ("tier1" in wanted and s.tier == 1) or s.sid in wanted or s.name in wanted:
            chosen.append(s)
    unknown = [w for w in wanted if w not in BY_NAME and w not in ("all", "tier1")]
    if unknown:
        raise SystemExit(f"unknown scenario(s): {unknown}; see --list")
    return chosen


def write_results(directory: Path, header: dict, outcomes: list[Outcome], wire: list[dict]) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    body = dict(header, scenarios=[o.as_dict() for o in outcomes])
    (directory / "run.json").write_text(json.dumps(body, indent=2, default=repr))
    with open(directory / "wire.jsonl", "w", encoding="utf-8") as handle:
        for entry in wire:
            handle.write(json.dumps(entry) + "\n")


def run_routes(links: list[tuple[str, Callable[[], Link], Callable[[], Link] | None]],
               scenarios: list[Scenario], args, pki, bind_ip,
               available: set[str]) -> tuple[list[Outcome], list[dict]]:
    """Every selected scenario on every route; (outcomes, wire log).

    `available` names what this run has: "peer" (the device can reach this
    machine), "fixture" (and the local PKI), "crypto", "internet".
    """
    outcomes: list[Outcome] = []
    wire: list[dict] = []
    for route, make_link, reset in links:
        section(f"route: {route}")
        try:
            link = make_link()
        except Failure as exc:
            # The other routes' records stay; this one is an ERROR.
            warn(f"the {route} route could not start: {exc}")
            for scenario in scenarios:
                failed = Outcome(scenario, route, "ERROR", reason=f"the {route} route could not "
                                                                  f"start: {exc}")
                judge(failed)
                outcomes.append(failed)
            continue
        ctx = Ctx(link, args, pki, bind_ip, reset, wire)
        pending = list(scenarios)
        try:
            while pending:
                scenario = pending.pop(0)
                missing = [need for need in scenario.needs if need not in available]
                if missing:
                    # The run was meant to have it (the fixture could not start,
                    # say): an ERROR, never a quiet SKIP.
                    outcome = Outcome(scenario, route, "ERROR",
                                      reason=f"this run has no {', '.join(missing)}")
                    judge(outcome)
                else:
                    outcome = run_scenario(ctx, scenario, route)
                outcomes.append(outcome)
        except LinkLost as exc:
            outcomes.append(ctx_lost := Outcome(scenario, route, "FAIL", reason=str(exc)))
            judge(ctx_lost)
            for rest in pending:
                lost = Outcome(rest, route, "ERROR", reason=f"the {route} route was lost")
                judge(lost)
                outcomes.append(lost)
            warn(f"the {route} route is lost: {exc}; resetting the C64")
            if reset is not None:
                try:
                    reset()
                except Failure as again:
                    warn(f"the reset failed too: {again}")
    return outcomes, wire


def summarise(outcomes: list[Outcome], expect: str) -> bool:
    section("summary")
    for o in outcomes:
        state = {"PASS": OK, "FAIL": FAIL, "ERROR": FAIL}.get(o.verdict, SKIP)
        detail(f"{o.scenario.sid:7} {o.scenario.name:24} tier {o.scenario.tier} "
               f"{o.scenario.kind:11} {o.route:7} {state:4} {o.signature}"
               + (f"  ({o.reason[:110]})" if o.reason and o.verdict != "PASS" else ""))
    met, problems = expectation_met(outcomes, expect)
    for problem in problems:
        detail(f"not {expect.upper()}: {problem}")
    return met


def main(argv=None) -> int:
    args = parse_args(argv)
    if args.list:
        for s in SCENARIOS:
            print(f"{s.sid:7} {s.name:24} tier {s.tier}  {s.kind:11} "
                  f"{'' if s.default else '(opt-in) '}{s.oracle}")
        return 0
    scenarios = select(args)
    routes = ["rest", "native"] if not args.route or "all" in args.route else args.route
    stamp = datetime.datetime.now(datetime.UTC).strftime("%Y%m%dT%H%M%SZ")
    results = args.results or DEFAULT_RESULTS / f"{stamp}-{args.expect}"
    target = targets.parse(args.host)
    device = UltimateApi(target.device, args.password, args.timeout)
    computer = UltimateApi(target.computer, args.password, args.timeout)
    header = {"suite": SUITE, "suite_commit": git_head(), "started": stamp, "expect": args.expect,
              "label": args.label, "host": args.host, "routes": routes,
              "scenarios_selected": [s.sid for s in scenarios]}
    # Before anything touches the device: a run holding the lock is mid-run,
    # and even the reset below would land in the middle of its work.
    try:
        with check("no other run holds this device (harness DeviceLock)"):
            header["lock"] = lock_check(target.device, args.require_lock)
            detail(header["lock"]["lock"])
    except Failure as exc:
        suite_fail(SUITE, f"nothing was run or changed: {format_exception(exc)}")
        return 1
    original: dict = {}
    outcomes: list[Outcome] = []
    wire: list[dict] = []
    pki = None
    rest_uci = Uci(computer.machine, BOUND_OPEN)
    ok = False
    setup_error = None
    touched = False     # whether anything on the device was changed
    try:
        with check("read /v1/info: product, firmware, core, fpga, git hash"):
            info = device.info()
            header["device"] = {"product": info.product, "firmware_version": info.firmware_version,
                                "fpga_version": info.fpga_version, **info.extra}
            detail(json.dumps(header["device"]))
        header["settings"] = apply_settings(device, args.set_wifi, original, header)
        touched = True
        with check("reset the C64 so nothing another program left is still driving the UCI"):
            computer.machine.reset(force=True)
            rest_uci.release()
        if not interface_present(rest_uci):
            raise Failure("$DF1B-$DF1F all read $FF: no command interface on this machine")
        with check("wait for INFO $FF ready bits (module, entropy, time), up to 90 s with TLS"):
            header["ready"] = wait_ready(RestLink(rest_uci))
            detail(json.dumps(header["ready"]))
        available = {"internet"}
        bind_ip = None
        check_start("this machine can serve the local TLS fixture to the device")
        try:
            bind_ip = fx.local_address_towards(target.device)
            available.add("peer")
            pki = fx.Pki()
            available |= {"fixture", "crypto"}
            check_ok(bind_ip)
        except (RuntimeError, OSError) as exc:
            # Every scenario that needs it then records ERROR.
            check_fail(f"{exc}; the scenarios that need it will be ERROR")
        header["fixture"] = {"bind_ip": bind_ip, "cryptography": fx.HAVE_CRYPTOGRAPHY}

        def reset_rest() -> Link:
            computer.machine.reset(force=True)
            rest_uci.release()
            return RestLink(rest_uci)

        def make_native() -> Link:
            computer.machine.reset(force=True)
            agent = NativeUci(computer.machine, computer.runners, BOUND_OPEN)
            agent.start()
            return NativeLink(agent)

        links = []
        for route in routes:
            if route == "rest":
                links.append(("rest", lambda: RestLink(rest_uci), reset_rest))
            elif target.split:
                warn("the native route cannot reach a cartridge's interface; skipped")
            else:
                links.append(("native", make_native, make_native))
        outcomes, wire = run_routes(links, scenarios, args, pki, bind_ip, available)
        ok = summarise(outcomes, args.expect)
    except Failure as exc:
        setup_error = format_exception(exc)
        ok = False
    finally:
        if pki is not None:
            pki.close()
        if touched or original:
            try:
                rest_uci.release()
                computer.machine.reset(force=True)
            except Failure as exc:
                warn(f"final reset: {exc}")
        if not args.keep_config:
            for (category, item), value in original.items():
                with check(f"restore {category} / {item} to {value!r}"):
                    device.configs.set(category, item, value)
        header["finished"] = datetime.datetime.now(datetime.UTC).strftime("%Y%m%dT%H%M%SZ")
        header["expectation_met"] = ok
        header["setup_error"] = setup_error
        write_results(results, header, outcomes, wire)
        print(f"results: {results}")
    if ok:
        suite_ok(SUITE, f"{args.expect.upper()} as expected over {len(outcomes)} scenario runs")
        return 0
    suite_fail(SUITE, setup_error or f"not {args.expect.upper()}; see the summary and "
                                     f"{results}/run.json")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
