#!/usr/bin/env python3
# Self-test: the UCI TLS suite against a simulated device; needs no hardware.

"""Check net_tls_test.py without a device: it must go GREEN, RED, and red for the right reason.

Seven parts, none of which touches any Ultimate:

1. The client deadlines on the REST route, against fake registers: a late
   reply is ABORTed, and a bit 2 that never clears is a LinkLost, never a hang.
2. The scenarios against net_tls_mock.MockDevice and the real local fixture:
   the tls flavour GREEN (except S22, a mock limit), stock RED.
3. Mutants of the mock: the scenario guarding each broken rule must go red.
4. main() over real HTTP (tests/lib/device_double.py) for Tier 1 without S30:
   --expect green on tls and red on stock exit 0, and swapped, 1.
5. No verdict without evidence, and setup failures are errors.
6. Setup refusals change no setting; READs and routes are bounded.
7. The fixture outlasts a slow reader (S13 over REST).

Usage: net_tls_selftest.py [--quick]   (--quick skips the tls flavour's 15 s
handshake-timeout scenario and the 16 KB record)
"""

import argparse
import json
import os
import sys
import tempfile
import threading
import time
from pathlib import Path
from types import SimpleNamespace

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
from report import Failure, check, check_skip, check_start, detail, section, suite_fail, suite_ok, suite_skip  # noqa: E402
from device_double import DeviceDouble  # noqa: E402
from uci import MAX_BLOCK_BYTES, Uci  # noqa: E402

sys.path.insert(0, bootstrap.directory("e2e", "io", "command_interface"))
import net_tls_fixture as fx  # noqa: E402
import net_tls_proto as proto  # noqa: E402
import net_tls_test as suite  # noqa: E402
from net_tls_mock import MockDevice  # noqa: E402

SUITE = "net_tls_selftest"
STANDINS = (("H1", "h1.standin.test"), ("H2", "h2.standin.test"))


# -- part 1: fake registers --------------------------------------------------

class FakeRegisters:
    """$DF1B-$DF1F as a MachineApi: readmem / writemem, one command at a time."""

    def __init__(self, busy: float, reply: bytes = b"\x05", text: bytes = b"00,OK",
                 stuck_abort: bool = False) -> None:
        self.busy, self.reply, self.text, self.stuck = busy, reply, text, stuck_abort
        self.state = "idle"
        self.done_at = 0.0
        self.abort = False
        self.resp = bytearray()
        self.stat = bytearray()

    def _tick(self) -> None:
        if self.state == "busy" and time.monotonic() >= self.done_at:
            if self.abort:
                # The ABORT is serviced right after the command returns: the
                # reply is dropped and the state goes back to Idle.
                self.state = "idle"
                self.abort = self.stuck
            else:
                self.state = "reply"
                self.resp, self.stat = bytearray(self.reply), bytearray(self.text)

    def _status(self) -> int:
        self._tick()
        value = {"idle": 0x00, "busy": 0x11, "reply": 0x20}[self.state]
        if self.abort:
            value |= 0x04
        if self.state == "reply":
            value |= (0x80 if self.resp else 0) | (0x40 if self.stat else 0)
        return value

    def _read(self, address: int) -> int:
        if address == 0xDF1C:
            return self._status()
        if address == 0xDF1D:
            return 0xC9
        if address == 0xDF1E:
            return self.resp.pop(0) if self.resp else 0
        if address == 0xDF1F:
            return self.stat.pop(0) if self.stat else 0
        return 0x02

    def readmem(self, address: int, length: int = 1) -> bytes:
        return bytes(self._read(address + i) for i in range(length))

    def writemem(self, address: int, data: bytes, idempotent: bool = False) -> None:
        if address != 0xDF1C:
            return
        for bits in data:
            self._tick()
            if bits & 0x01:
                self.state, self.done_at = "busy", time.monotonic() + self.busy
            if bits & 0x02 and self.state == "reply":
                self.state = "idle"
            if bits & 0x04:
                # On an idle interface the ABORT is serviced at once.
                self.abort = self.state == "busy" or self.stuck


def part1() -> bool:
    section("part 1: client deadlines over fake registers")
    ok = True
    saved = proto.POST_ABORT_MIN
    proto.POST_ABORT_MIN = 1.5
    try:
        cases = [
            ("a reply inside its bound is collected", FakeRegisters(0.2), 2.0, "reply"),
            ("a reply past its bound is ABORTed; bit 2 clears when the command ends",
             FakeRegisters(1.0), 0.4, "aborted"),
            ("bit 2 that never clears is LinkLost at the deadline, not a hang",
             FakeRegisters(5.0, stuck_abort=True), 0.4, "lost"),
        ]
        for label, regs, bound, wanted in cases:
            try:
                with check(label):
                    link = proto.RestLink(Uci(regs, 30.0))
                    started = time.monotonic()
                    try:
                        result = link.command(proto.info_command(0xFF), bound)
                        got = "aborted" if result.aborted else "reply"
                        if got == "reply" and (result.data, result.status) != (b"\x05", b"00,OK"):
                            raise Failure(f"collected {result.describe()}")
                    except proto.LinkLost:
                        got = "lost"
                    took = time.monotonic() - started
                    detail(f"{got} after {took:.2f}s")
                    if got != wanted:
                        raise Failure(f"{got}, expected {wanted}")
                    if took > bound + proto.POST_ABORT_MIN + 1.0:
                        raise Failure(f"took {took:.2f}s, past the client deadline")
            except Failure:
                ok = False
        try:
            with check("abort_in_flight: ABORT lands in PROCESSING; bit 2 clears at the command's end"):
                regs = FakeRegisters(1.0)
                outcome = proto.RestLink(Uci(regs, 30.0)).abort_in_flight(
                    proto.info_command(0xFF), 0.3, bound=5.0)
                if not outcome.busy_at_abort or not 0.9 <= outcome.bit2_cleared_after <= 2.0:
                    raise Failure(str(outcome))
        except Failure:
            ok = False
    finally:
        proto.POST_ABORT_MIN = saved
    return ok


# -- parts 2 and 3: the mock -----------------------------------------------

class World:
    """The fixture PKI, a stand-in public PKI and its HTTP servers."""

    def __init__(self) -> None:
        self.pki = fx.Pki()
        self.public = fx.Pki()
        self.public_root = str(self.public.root_pem_file())
        self.servers = []
        self.hosts = []
        self.resolver = {}
        for hid, name in STANDINS:
            chain = self.public.issue(hid, name)
            server = fx.TlsServer("127.0.0.1", chain, fx.Behaviour(then="http"))
            self.servers.append(server)
            self.hosts.append(suite.Host(name, hid, ip="127.0.0.1", port=server.port))
            self.resolver[name] = "127.0.0.1"

    def device(self, flavour: str) -> MockDevice:
        return MockDevice(flavour, bundle_cafile=self.public_root, resolver=self.resolver,
                          system_bundle=False)

    def args(self) -> SimpleNamespace:
        return SimpleNamespace(module_minor=8, mac_cafile=self.public_root, strict_sni=True,
                               hosts=self.hosts)

    def run(self, flavour: str, sids: list[str]) -> dict[str, suite.Outcome]:
        device = self.device(flavour)

        def reset():
            device.reset()
            return device
        scenarios = [s for s in suite.SCENARIOS if s.sid in sids]
        outcomes, _ = suite.run_routes([(f"mock-{flavour}", lambda: device, reset)], scenarios,
                                       self.args(), self.pki, "127.0.0.1",
                                       {"peer", "fixture", "crypto", "internet"})
        device.reset()
        return {o.scenario.sid: o for o in outcomes}

    def close(self) -> None:
        for server in self.servers:
            server.close()
        self.pki.close()
        self.public.close()


# -- part 4: main() over HTTP ---------------------------------------------------

class UciDouble(DeviceDouble):
    """The repository's REST double, with a command interface run by a MockDevice."""

    def __init__(self, device: MockDevice) -> None:
        super().__init__()
        self.mock = device
        self.configs.update({
            "C64 and Cartridge Settings": {"Command Interface": "Disabled"},
            "WiFi settings": {"WiFi Enabled": "Enabled"},
            "Network Settings": {"SNTP Enable": "Disabled"},
            "U64 Specific Settings": {"Turbo Control": "Manual"},
        })
        self.cmd = bytearray()
        self.state = "idle"
        self.abort = False
        self.blocks: list[tuple[bytes, bytes]] = []
        self.resp = bytearray()
        self.stat = bytearray()
        self.uci_lock = threading.Lock()

    def route(self, method, path):
        if path == "/v1/machine:readmem" and method == "GET":
            return self._uci_read
        if path == "/v1/machine:writemem" and method == "PUT":
            return self._uci_write
        if path == "/v1/machine:reset" and method == "PUT":
            return self._reset
        return super().route(method, path)

    def _reset(self, params):
        self.mock.reset()
        return self._ok_json(params)

    def _run(self, command: bytes) -> None:
        data, text = self.mock._dispatch(command)
        with self.uci_lock:
            if self.abort:
                # The ABORT is serviced once the command returns: reply dropped.
                self.state, self.abort, self.blocks = "idle", False, []
                return
            chunks = [data[i:i + MAX_BLOCK_BYTES] for i in range(0, len(data), MAX_BLOCK_BYTES)]
            chunks = chunks or [b""]
            self.blocks = [(chunk, text if i == len(chunks) - 1 else b"")
                           for i, chunk in enumerate(chunks)]
            self._next_block()

    def _next_block(self) -> None:
        chunk, text = self.blocks.pop(0)
        self.resp, self.stat = bytearray(chunk), bytearray(text)
        self.state = "more" if self.blocks else "last"

    def _status(self) -> int:
        value = {"idle": 0x00, "busy": 0x11, "last": 0x20, "more": 0x30}[self.state]
        if self.abort:
            value |= 0x04
        if self.state in ("last", "more"):
            value |= (0x80 if self.resp else 0) | (0x40 if self.stat else 0)
        return value

    def _byte(self, address: int) -> int:
        if address == 0xDF1C:
            return self._status()
        if address == 0xDF1D:
            return 0xC9
        if address == 0xDF1E:
            return self.resp.pop(0) if self.resp else 0
        if address == 0xDF1F:
            return self.stat.pop(0) if self.stat else 0
        return 0x02

    def _uci_read(self, params):
        address = int(params.get("address", "0"), 16)
        if not 0xDF1B <= address <= 0xDF1F:
            return super()._readmem(params)
        length = int(params.get("length", "1"))
        with self.uci_lock:
            body = bytes(self._byte(address + i) for i in range(length))
        return 200, body, "application/octet-stream"

    def _uci_write(self, params):
        address = int(params.get("address", "0"), 16)
        data = bytes.fromhex(params.get("data", ""))
        with self.uci_lock:
            for value in data:
                if address == 0xDF1D:
                    self.cmd.append(value)
                elif address == 0xDF1C:
                    self._control(value)
        return self._ok_json(params)

    def _control(self, bits: int) -> None:
        if bits & 0x01 and self.state == "idle":
            command, self.cmd = bytes(self.cmd), bytearray()
            self.state = "busy"
            threading.Thread(target=self._run, args=(command,), daemon=True).start()
        if bits & 0x02:
            if self.state == "more":
                self._next_block()
            elif self.state == "last":
                self.state = "idle"
        if bits & 0x04:
            if self.state == "busy":
                self.abort = True
            else:
                self.state, self.blocks, self.cmd = "idle", [], bytearray()


def part4(world: "World") -> bool:
    section("part 4: main() over HTTP, against a REST double with a mocked network target")
    ok = True
    tier1 = ["S1", "S20", "S2", "S23", "S5", "S19e"]
    for flavour, expect, code in (("tls", "green", 0), ("stock", "red", 0),
                                  ("tls", "red", 1), ("stock", "green", 1)):
        double = UciDouble(world.device(flavour))
        saved = {k: os.environ.get(k) for k in double.environment()}
        os.environ.update(double.environment())
        try:
            with tempfile.TemporaryDirectory() as results:
                argv = ["-H", "127.0.0.1", "-t", "10", "--route", "rest", "--expect", expect,
                        "--results", results, "--module-minor", "8"]
                for sid in tier1:
                    argv += ["--test", sid]
                exit_code = suite.main(argv)
                run = json.loads((Path(results) / "run.json").read_text())
            try:
                with check(f"{flavour} with --expect {expect} exits {code}; run.json and settings"):
                    if exit_code != code:
                        raise Failure(f"exit {exit_code}")
                    settings = run.get("settings", {})
                    wanted = {"Command Interface": "Enabled", "WiFi Enabled": "Enabled",
                              "SNTP Enable": "Enabled", "Turbo Control": "Off"}
                    if settings != wanted:
                        raise Failure(f"settings read back {settings}")
                    restored = double.configs["C64 and Cartridge Settings"]["Command Interface"]
                    if restored != "Disabled":
                        raise Failure(f"Command Interface left {restored!r}, not restored")
                    if double.configs["WiFi settings"]["WiFi Enabled"] != "Enabled":
                        raise Failure("WiFi Enabled was written back (it is one-way)")
                    sigs = {o["sid"]: o["signature"] for o in run["scenarios"]}
                    detail(str(sigs))
                    if len(run["scenarios"]) != len(tier1) + 1:
                        raise Failure(f"{len(run['scenarios'])} scenario records")
                    resets = double.calls(path="/v1/machine:reset")
                    if not resets:
                        raise Failure("the C64 was never reset")
            except Failure:
                ok = False
        finally:
            for key, value in saved.items():
                if value is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = value
            double.close()
    return ok


def run_main(world: "World", flavour: str, argv: list[str], fixture: bool = True,
             native_fails: bool = False, wifi: str = "Enabled") -> tuple[int, dict, UciDouble]:
    """suite.main() against a double; (exit code, run.json, the double)."""
    double = UciDouble(world.device(flavour))
    double.configs["WiFi settings"]["WiFi Enabled"] = wifi
    saved_env = {k: os.environ.get(k) for k in double.environment()}
    os.environ.update(double.environment())
    saved_address, saved_start = fx.local_address_towards, suite.NativeUci.start

    def no_route(*_args, **_kw):
        raise OSError("self-test: no route to the device")

    def no_agent(_self):
        raise Failure("self-test: the agent did not start (64tass Exec format error)")
    if not fixture:
        fx.local_address_towards = no_route
    if native_fails:
        suite.NativeUci.start = no_agent
    try:
        with tempfile.TemporaryDirectory() as results:
            try:
                code = suite.main(["-H", "127.0.0.1", "-t", "10", "--results", results, *argv])
            except SystemExit as exc:   # argparse refusing an option
                code = exc.code
            path = Path(results) / "run.json"
            run = json.loads(path.read_text()) if path.exists() else {"scenarios": []}
    finally:
        fx.local_address_towards, suite.NativeUci.start = saved_address, saved_start
        for key, value in saved_env.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        double.close()
    return code, run, double


class SlowReads(proto.Link):
    """READs that each take 50 ms and alternate data with `02,NO DATA: 11`."""
    name = "slow"

    def __init__(self) -> None:
        super().__init__()
        self.turn = 0

    def _command(self, command: bytes, bound: float) -> proto.Result:
        time.sleep(0.05)
        self.turn += 1
        if self.turn % 2:
            return proto.Result(command, (100).to_bytes(2, "little") + bytes(100), b"00,OK", 0.05)
        return proto.Result(command, b"\xff\xff", proto.STATUS_NO_DATA_IDLE, 0.05)


def part5(world: "World") -> bool:
    section("part 5: no verdict without evidence; setup failures are errors")
    ok = True
    t1 = ["--test", "tier1", "--module-minor", "8", "--route", "rest"]
    cases = [
        ("tls, --expect green, the fixture cannot start -> exit 1",
         "tls", ["--expect", "green", *t1], {"fixture": False}, 1),
        ("stock, --expect red, the fixture cannot start -> exit 1",
         "stock", ["--expect", "red", *t1], {"fixture": False}, 1),
        ("mutant bundle-accepts-all, --expect green, no fixture -> exit 1",
         "bundle-accepts-all", ["--expect", "green", *t1], {"fixture": False}, 1),
        ("stock, --expect green, only S32 on REST (route n/a) -> exit 1",
         "stock", ["--expect", "green", "--test", "S32", "--route", "rest"], {}, 1),
        ("stock, --expect red, only the S33 measurement -> exit 1",
         "stock", ["--expect", "red", "--test", "S33", "--route", "rest"], {}, 1),
        ("WiFi Disabled and no --set-wifi: setup refuses, WiFi untouched -> exit 1",
         "tls", ["--expect", "green", "--test", "S1", "--route", "rest", "--module-minor", "8"],
         {"wifi": "Disabled"}, 1),
        ("WiFi Disabled with --set-wifi: set and kept -> exit 0",
         "tls", ["--expect", "green", "--test", "S1", "--route", "rest", "--module-minor", "8",
                "--set-wifi"], {"wifi": "Disabled"}, 0),
    ]
    for label, flavour, argv, options, wanted in cases:
        try:
            with check(label):
                code, run, double = run_main(world, flavour, argv, **options)
                detail(str([(o["sid"], o["verdict"], o["signature"]) for o in run["scenarios"]]))
                if code != wanted:
                    raise Failure(f"exit {code}")
                wifi = double.configs["WiFi settings"]["WiFi Enabled"]
                if options.get("wifi") == "Disabled" and "--set-wifi" not in argv and wifi != "Disabled":
                    raise Failure(f"WiFi Enabled was written ({wifi!r}) without --set-wifi")
                if "--set-wifi" in argv and wifi != "Enabled":
                    raise Failure(f"--set-wifi left WiFi {wifi!r}")
        except Failure:
            ok = False
    try:
        with check("the native agent fails to start: REST records kept, native ERROR, exit 1"):
            code, run, _ = run_main(world, "tls", ["--expect", "green", "--test", "S1",
                                                  "--module-minor", "8"], native_fails=True)
            records = [(o["route"], o["sid"], o["verdict"]) for o in run["scenarios"]]
            detail(str(records))
            rest = [r for r in records if r[0] == "rest"]
            native = [r for r in records if r[0] == "native"]
            if code != 1 or not rest or any(v != "PASS" for _, _, v in rest):
                raise Failure(f"exit {code}, REST records {rest}")
            if not native or any(v != "ERROR" for _, _, v in native):
                raise Failure(f"native records {native}")
    except Failure:
        ok = False
    try:
        with check("read_until's budget is idle time: 2000 B over 50 ms READs, budget 0.5 s"):
            ctx = suite.Ctx(SlowReads(), SimpleNamespace(), None, None, None, [])
            got, end = ctx.read_until(0, 2000, budget=0.5)
            if len(got) != 2000 or end is not None:
                raise Failure(f"{len(got)} bytes, end {end}")
    except Failure:
        ok = False
    return ok


class EmptyOk(proto.Link):
    """READs that answer 00,OK with header $0000: not a 1.6 answer."""
    name = "empty-ok"

    def __init__(self) -> None:
        super().__init__()
        self.calls = 0

    def _command(self, command: bytes, bound: float) -> proto.Result:
        self.calls += 1
        if self.calls > 5000:
            raise Failure("read_until was still reading after 5000 empty 00,OK READs: unbounded")
        return proto.Result(command, b"\x00\x00", b"00,OK", 0.0)


def part6(world: "World") -> bool:
    section("part 6: setup refusals and bounded reads")
    ok = True
    try:
        with check("WiFi off, no --set-wifi: refused before ANY setting is written"):
            code, run, double = run_main(world, "tls", ["--expect", "green", "--test", "S1", "--route",
                                                       "rest", "--module-minor", "8"],
                                         wifi="Disabled")
            ci = double.configs["C64 and Cartridge Settings"]["Command Interface"]
            sntp = double.configs["Network Settings"]["SNTP Enable"]
            if code != 1 or ci != "Disabled" or sntp != "Disabled":
                raise Failure(f"exit {code}, Command Interface {ci!r}, SNTP {sntp!r}")
            if not run.get("settings_before"):
                raise Failure("run.json does not record the settings as found")
            if double.calls(path="/v1/machine:reset"):
                raise Failure("the C64 was reset although nothing was run")
    except Failure:
        ok = False
    try:
        with check("--route native with S19e selected: S19e ran on no route -> exit 1"):
            outcomes = [suite.Outcome(suite.BY_NAME["S0"], "native", "PASS"),
                        suite.Outcome(suite.BY_NAME["S1"], "native", "PASS"),
                        suite.Outcome(suite.BY_NAME["S19e"], "native", "N/A")]
            for o in outcomes:
                suite.judge(o)
            met, problems = suite.expectation_met(outcomes, "green")
            detail(str(problems))
            if met:
                raise Failure("met")
    except Failure:
        ok = False
    try:
        with check("READ answering 00,OK with no data: read_until fails, bounded"):
            link = EmptyOk()
            ctx = suite.Ctx(link, SimpleNamespace(), None, None, None, [])
            try:
                ctx.read_until(0, 100, budget=0.5)
                raise Failure("read_until returned")
            except Failure as exc:
                if "unbounded" in str(exc):
                    raise
                detail(f"failed as it should: {exc}")
    except Failure:
        ok = False
    try:
        from c64_test_harness.backends.device_lock import DeviceLock  # noqa: PLC0415
    except ImportError:
        DeviceLock = None
    # Without the harness every run with --require-lock is refused for want of a DeviceLock, so this check
    # would pass without looking at any lock.
    if DeviceLock is None:
        check_start("--require-lock with no session lock: refused, nothing changed")
        check_skip("c64_test_harness is not installed, so there is no session lock to be missing")
    else:
        try:
            with check("--require-lock with no session lock: refused, nothing changed"):
                code, run, double = run_main(world, "tls", ["--expect", "green", "--test", "S1", "--route",
                                                           "rest", "--module-minor", "8",
                                                           "--require-lock"])
                ci = double.configs["C64 and Cartridge Settings"]["Command Interface"]
                if code != 1 or ci != "Disabled":
                    raise Failure(f"exit {code}, Command Interface {ci!r}")
        except Failure:
            ok = False
    if DeviceLock is None:
        check_start("--require-lock inside this process's own lock (NET_TLS_LOCK_PID) runs -> exit 0")
        check_skip("c64_test_harness is not installed, so there is no DeviceLock to take")
        return ok
    try:
        with check("--require-lock inside this process's own lock (NET_TLS_LOCK_PID) runs -> exit 0"):
            lock = DeviceLock("127.0.0.1")
            if not lock.acquire(timeout=10):
                raise Failure("could not take the 127.0.0.1 test lock")
            os.environ["NET_TLS_LOCK_PID"] = str(os.getpid())
            try:
                code, run, _ = run_main(world, "tls", ["--expect", "green", "--test", "S1", "--route",
                                                      "rest", "--module-minor", "8",
                                                      "--require-lock"])
            finally:
                os.environ.pop("NET_TLS_LOCK_PID", None)
                lock.release()
            if code != 0:
                raise Failure(f"exit {code}: {run.get('setup_error')}")
    except Failure:
        ok = False
    return ok


SLOW_READER_SECONDS = 65.0


def part7(world: "World") -> bool:
    """A device run's S13-over-REST failure, modelled: a client that reads after more than 60 s.

    On the REST route the device needed about 250 s to read a 16 KB record before
    it wrote (one REST call per byte). A fixture whose per-connection idle
    timeout is shorter than the slowest route gives up and closes, and the
    device's WRITE meets a reset: a fixture failure reported against the
    firmware.
    """
    section("part 7: the fixture outlasts a slow reader (S13 over REST)")
    ok = True
    try:
        with check(f"a client that waits {SLOW_READER_SECONDS:.0f}s before reading 16 KB and "
                   f"writing 100 B still gets its echo"):
            data = fx.payload(13, 16384)
            server = fx.TlsServer("127.0.0.1", world.pki.chains["ecdsa"],
                                  fx.Behaviour(send=data, then="echo"))
            device = world.device("tls")
            ctx = suite.Ctx(device, SimpleNamespace(), world.pki, "127.0.0.1", None, [])
            try:
                reply = ctx.expect_open(server.port, "127.0.0.1",
                                        pin=world.pki.chains["ecdsa"].spki_sha256)
                time.sleep(SLOW_READER_SECONDS)
                got, end = ctx.read_until(reply.handle, len(data))
                if got != data:
                    raise Failure(f"{len(got)} of {len(data)} bytes ({end})")
                suite.echo_round_trip(ctx, reply.handle, 100, seed=7)
                ctx.close(reply.handle)
            finally:
                server.close()
                device.reset()
            detail(str([c.as_dict() for c in server.logs]))
    except Failure:
        ok = False
    return ok


def judge_flavour(label: str, outcomes: dict, wanted: dict[str, str]) -> bool:
    """`wanted` maps a sid to the signature it must show."""
    ok = True
    for sid, signature in wanted.items():
        o = outcomes.get(sid)
        try:
            with check(f"{label}: {sid} shows {signature}"):
                if o is None:
                    raise Failure("not run")
                if o.signature != signature:
                    raise Failure(f"{o.signature}: {o.reason[:200]}")
        except Failure:
            ok = False
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--quick", action="store_true")
    args = parser.parse_args()
    if not fx.HAVE_CRYPTOGRAPHY:
        suite_skip(SUITE, "the `cryptography` package is not installed")
        return 0
    ok = part1()
    world = World()
    try:
        everything = [s.sid for s in suite.SCENARIOS if s.default]
        if args.quick:
            everything = [sid for sid in everything if sid not in ("S10", "S13")]
        section("part 2: the tls flavour goes GREEN")
        tls = world.run("tls", everything)
        wanted = {sid: ("CONTROL-PASS" if suite.BY_NAME[sid].kind == "control" else "GREEN")
                  for sid in everything}
        wanted["S22"] = "FAIL"   # the mock cannot report the negotiated group
        ok &= judge_flavour("tls", tls, wanted)
        section("part 2: the stock flavour goes RED")
        stock = world.run("stock", everything)
        wanted = {sid: ("CONTROL-PASS" if suite.BY_NAME[sid].kind == "control" else "RED")
                  for sid in everything}
        ok &= judge_flavour("stock", stock, wanted)
        try:
            with check("--expect red is met by the stock run, and --expect green is not"):
                red, problems = suite.expectation_met(list(stock.values()), "red")
                green, _ = suite.expectation_met(list(stock.values()), "green")
                if not red or green:
                    raise Failure(f"red met {red}, green met {green}: {problems[:3]}")
        except Failure:
            ok = False
        section("part 3: each mutant turns its guard red")
        for flavour, sids in (("bundle-accepts-all", ["S0", "S5"]),
                              ("release-noop", ["S0", "S19e", "S19e2"]),
                              ("group-zero", ["S0", "S2"]),
                              ("info-ff-claims", ["S0", "S19e", "S19e2b"])):
            outcomes = world.run(flavour, sids)
            ok &= judge_flavour(flavour, outcomes, {sid: "FAIL" for sid in sids[1:]})
        outcomes = world.run("abort-shortens", ["S0", "S19e"])
        ok &= judge_flavour("abort-shortens", outcomes, {"S19e": "FAIL"})
        if not args.quick:
            ok &= part4(world)
        ok &= part5(world)
        ok &= part6(world)
        if not args.quick:
            ok &= part7(world)
    finally:
        world.close()
    if ok:
        suite_ok(SUITE, "GREEN with TLS, RED on stock, every mutant caught")
        return 0
    suite_fail(SUITE, "see the checks above")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
