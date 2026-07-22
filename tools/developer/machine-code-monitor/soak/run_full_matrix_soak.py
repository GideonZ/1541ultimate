#!/usr/bin/env python3
"""Fully automated Machine Code Monitor soak runner.

Local-only orchestration for the U64 debug lifecycle acceptance matrix.  This
script deliberately reuses the existing production-API Overlay/Freeze and
standard Telnet lifecycle helpers; it owns only matrix orchestration, progress
logging, liveness classification, evidence capture, and JTAG recovery.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import threading
import time
import traceback
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Optional


SOAK_DIR = Path(__file__).resolve().parent
MCM_DIR = SOAK_DIR.parent
REPO_ROOT = SOAK_DIR.parents[3]
sys.path.insert(0, str(MCM_DIR))

from mcm_rest import Failure as RestFailure  # noqa: E402
from mcm_rest import Rest  # noqa: E402
import overlay_lifecycle_clean as overlay_lifecycle  # noqa: E402
import monitor_debug_test as telnet_debug  # noqa: E402
import monitor_test as monitor_telnet  # noqa: E402


VIEW_TO_INTERFACE = {
    "overlay": "Overlay on HDMI",
    "freeze": "Freeze",
}

BANKINGS = ("ram", "ram-under-rom", "rom")
VIEWS = ("overlay", "freeze", "telnet")
SCENARIO_TYPES = ("clean", "concurrency")
RECOVER_POLICIES = ("hard-wedge", "unavailable", "any", "never")
DEVICE_DEGRADATION_KINDS = (
    "hard_wedge",
    "persistent_api_unavailability",
    "degraded_rest_endpoint",
    "persistent_screen_or_monitor_unavailability",
)


class SoakFailure(RuntimeError):
    def __init__(self, message: str, exc: Optional[BaseException] = None) -> None:
        super().__init__(message)
        self.exc = exc


class EndpointDegraded(RuntimeError):
    pass


@dataclass(frozen=True)
class Scenario:
    kind: str
    view: str
    banking: str

    @property
    def label(self) -> str:
        return f"{self.kind} {self.view}/{self.banking}"

    @property
    def slug(self) -> str:
        return f"{self.kind}-{self.view}-{self.banking}".replace("_", "-")


@dataclass
class ScenarioState:
    scenario: Scenario
    attempts: int = 0
    recoveries: int = 0
    passed: bool = False
    failed: bool = False
    failure_iteration: Optional[int] = None
    last_successful_operation: str = ""
    failure_last_successful_operation: str = ""
    failing_operation: str = ""
    failure_kind: str = ""
    failure_detail: str = ""
    failure_dir: Optional[Path] = None
    failure_dirs: list[Path] = field(default_factory=list)
    recovery_outcomes: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)


@dataclass
class RunContext:
    scenario: Optional[Scenario] = None
    iteration: int = 0
    operation: str = ""
    last_successful_operation: str = ""


class EventLogger:
    def __init__(self, log_root: Path, run_id: str) -> None:
        self.run_id = run_id
        self.log_dir = log_root / run_id
        self.failures_dir = self.log_dir / "failures"
        self.recoveries_dir = self.log_dir / "recoveries"
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.failures_dir.mkdir(parents=True, exist_ok=True)
        self.recoveries_dir.mkdir(parents=True, exist_ok=True)
        self.started = time.monotonic()
        self._lock = threading.Lock()
        self._run_log = (self.log_dir / "run.log").open("a", encoding="utf-8")
        self._events = (self.log_dir / "events.jsonl").open("a", encoding="utf-8")

    def close(self) -> None:
        self._run_log.close()
        self._events.close()

    def now(self) -> str:
        return datetime.now(timezone.utc).astimezone().isoformat(timespec="milliseconds")

    def elapsed(self) -> float:
        return time.monotonic() - self.started

    def log(
        self,
        phase: str,
        scenario: Optional[Scenario],
        iteration: int,
        operation: str,
        expected: str,
        actual: str,
        duration_ms: int,
        status: str,
        *,
        stdout: Optional[str] = None,
        extra: Optional[dict[str, Any]] = None,
    ) -> None:
        event: dict[str, Any] = {
            "timestamp": self.now(),
            "elapsed_seconds": round(self.elapsed(), 3),
            "run_id": self.run_id,
            "phase": phase,
            "view": scenario.view if scenario else "",
            "banking": scenario.banking if scenario else "",
            "scenario_type": scenario.kind if scenario else "",
            "iteration": iteration,
            "operation": operation,
            "expected_result": expected,
            "actual_result": actual,
            "duration_milliseconds": duration_ms,
            "status": status,
        }
        if extra:
            event.update(extra)
        line = self.format_line(event)
        with self._lock:
            self._events.write(json.dumps(event, sort_keys=True) + "\n")
            self._events.flush()
            self._run_log.write(line + "\n")
            self._run_log.flush()
            if stdout is not None:
                print(stdout, flush=True)
            else:
                print(line, flush=True)

    def format_line(self, event: dict[str, Any]) -> str:
        elapsed = time.strftime("%H:%M:%S", time.gmtime(event["elapsed_seconds"]))
        prefix = f"[{elapsed}]"
        scenario = ""
        if event["scenario_type"]:
            scenario = (
                f" {event['scenario_type']} {event['view']}/{event['banking']}"
                f" iter {event['iteration']}"
            )
        status = event["status"].upper()
        actual = event["actual_result"]
        duration = event["duration_milliseconds"]
        return f"{prefix}{scenario} {event['operation']}: {status} {actual} {duration}ms"


class OperationRunner:
    def __init__(self, logger: EventLogger, ctx: RunContext, state: ScenarioState) -> None:
        self.logger = logger
        self.ctx = ctx
        self.state = state

    def run(
        self,
        phase: str,
        name: str,
        expected: str,
        func: Callable[[], Any],
        *,
        actual_ok: str = "OK",
    ) -> Any:
        self.ctx.operation = name
        start = time.monotonic()
        done = threading.Event()
        box: dict[str, Any] = {}

        def invoke() -> None:
            try:
                box["result"] = func()
            except BaseException as exc:  # noqa: BLE001
                box["exc"] = exc
                box["traceback"] = traceback.format_exc()
            finally:
                done.set()

        worker = threading.Thread(target=invoke, name=f"soak-operation-{name}", daemon=True)
        worker.start()
        while not done.wait(10.0):
            duration = int((time.monotonic() - start) * 1000)
            self.logger.log(
                "operation",
                self.ctx.scenario,
                self.ctx.iteration,
                f"{name} progress",
                expected,
                f"still running after {duration // 1000}s; last successful operation: "
                f"{self.ctx.last_successful_operation or '<none>'}",
                duration,
                "progress",
            )

        if "exc" in box:
            exc = box["exc"]
            duration = int((time.monotonic() - start) * 1000)
            actual = f"{type(exc).__name__}: {format_exception(exc)}"
            self.logger.log(
                phase,
                self.ctx.scenario,
                self.ctx.iteration,
                name,
                expected,
                actual,
                duration,
                "fail",
                extra={"traceback": box.get("traceback", "")},
            )
            self.state.failing_operation = name
            raise SoakFailure(f"{name} failed: {actual}", exc) from exc
        result = box.get("result")
        duration = int((time.monotonic() - start) * 1000)
        self.logger.log(
            phase,
            self.ctx.scenario,
            self.ctx.iteration,
            name,
            expected,
            actual_ok,
            duration,
            "pass",
        )
        self.ctx.last_successful_operation = name
        self.state.last_successful_operation = name
        return result


class ConcurrencyLoad:
    def __init__(self, host: str, logger: EventLogger, scenario: Scenario) -> None:
        self.host = host
        self.logger = logger
        self.scenario = scenario
        self.stop_event = threading.Event()
        self.thread: Optional[threading.Thread] = None

    def start(self) -> None:
        self.thread = threading.Thread(target=self._run, name="overlay-concurrency", daemon=True)
        self.thread.start()
        self.logger.log(
            "scenario",
            self.scenario,
            0,
            "concurrency-worker-start",
            "worker loops reset/menu_button/telnet remote menu",
            "started",
            0,
            "pass",
        )

    def stop(self) -> None:
        self.stop_event.set()
        if self.thread is not None:
            self.thread.join(timeout=5.0)
        self.logger.log(
            "scenario",
            self.scenario,
            0,
            "concurrency-worker-stop",
            "worker stopped",
            "stopped",
            0,
            "pass",
        )

    def _run(self) -> None:
        while not self.stop_event.is_set():
            self._put("/v1/machine:reset")
            self._sleep(0.35)
            self._put("/v1/machine:menu_button")
            self._sleep(0.2)
            self._telnet_remote_menu_cycle()
            self._sleep(0.4)

    def _sleep(self, seconds: float) -> None:
        self.stop_event.wait(seconds)

    def _put(self, path: str) -> None:
        url = f"http://{self.host}{path}"
        request = urllib.request.Request(url, data=b"", method="PUT")
        try:
            with urllib.request.urlopen(request, timeout=2.0):
                pass
        except Exception as exc:  # noqa: BLE001 - load failures are evidence, not fatal here
            self.logger.log(
                "operation",
                self.scenario,
                0,
                f"concurrency {path}",
                "background load operation",
                f"{type(exc).__name__}: {format_exception(exc)}",
                0,
                "fail",
            )

    def _telnet_remote_menu_cycle(self) -> None:
        try:
            with socket.create_connection((self.host, 23), timeout=2.0) as sock:
                sock.settimeout(2.0)
                sock.sendall(b"\x0f")
                time.sleep(0.1)
                sock.sendall(b"\x0f")
        except Exception as exc:  # noqa: BLE001
            self.logger.log(
                "operation",
                self.scenario,
                0,
                "concurrency telnet remote-menu",
                "background telnet connect/open/close/disconnect",
                f"{type(exc).__name__}: {format_exception(exc)}",
                0,
                "fail",
            )


def format_exception(exc: BaseException) -> str:
    if isinstance(exc, urllib.error.URLError) and getattr(exc, "reason", None) is not None:
        return f"{exc} ({exc.reason})"
    return str(exc)


def degraded_endpoint_text(text: str) -> bool:
    lowered = text.lower()
    endpoint_markers = (
        "menu_screen",
        "/v1/machine:menu_screen",
        "/v1/machine:input",
        "/v1/machine:reset",
        "/v1/machine:readmem",
        "/v1/machine:writemem",
        "/v1/machine:menu_button",
    )
    error_markers = (
        "http error",
        "404",
        "500",
        "timeout",
        "timed out",
        "connection refused",
        "no route to host",
        "transport error",
        "not found",
    )
    return any(marker in lowered for marker in endpoint_markers) and any(
        marker in lowered for marker in error_markers
    )


def request_json(host: str, path: str, timeout: float = 5.0) -> dict[str, Any]:
    with urllib.request.urlopen(f"http://{host}{path}", timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def run_capture(cmd: list[str], timeout: float = 10.0) -> dict[str, Any]:
    start = time.monotonic()
    try:
        result = subprocess.run(
            cmd,
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=timeout,
            check=False,
        )
        return {
            "cmd": cmd,
            "returncode": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
            "duration_ms": int((time.monotonic() - start) * 1000),
        }
    except BaseException as exc:
        return {
            "cmd": cmd,
            "error": f"{type(exc).__name__}: {format_exception(exc)}",
            "duration_ms": int((time.monotonic() - start) * 1000),
        }


def write_run_manifest(logger: EventLogger, args: argparse.Namespace, scenarios: list[Scenario]) -> None:
    manifest = {
        "run_id": logger.run_id,
        "created_at": logger.now(),
        "repo_root": str(REPO_ROOT),
        "cwd": str(Path.cwd()),
        "argv": sys.argv,
        "host": args.host,
        "port": args.port,
        "iterations": args.iterations,
        "recover_jtag": args.recover_jtag,
        "recover_on": args.recover_on,
        "max_recoveries": args.max_recoveries,
        "jtag_deploy": args.jtag_deploy,
        "syslog_file": args.syslog_file,
        "selected_scenarios": [s.label for s in scenarios],
        "logs": {
            "run_log": str(logger.log_dir / "run.log"),
            "events_jsonl": str(logger.log_dir / "events.jsonl"),
            "summary": str(logger.log_dir / "summary.md"),
            "failures": str(logger.failures_dir),
            "recoveries": str(logger.recoveries_dir),
        },
        "git": {
            "branch": run_capture(["git", "branch", "--show-current"]),
            "status_short": run_capture(["git", "status", "--short"]),
            "head": run_capture(["git", "rev-parse", "HEAD"]),
            "diff_stat": run_capture(["git", "diff", "--stat"]),
        },
        "processes": {
            "leftover_probes": run_capture(
                [
                    "pgrep",
                    "-af",
                    "overlay_lifecycle_clean|monitor_debug_test|run_full_matrix_soak|syslog_listen|nios2-download",
                ]
            )
        },
        "device_preflight": {},
    }
    try:
        rest = Rest(args.host, args.timeout)
        manifest["device_preflight"] = probe_once(args.host, rest)
    except BaseException as exc:
        manifest["device_preflight"] = {
            "error": f"{type(exc).__name__}: {format_exception(exc)}",
            "traceback": traceback.format_exc(),
        }
    (logger.log_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    logger.log(
        "setup",
        None,
        0,
        "write manifest",
        "manifest.json contains command, git state, scenario list, and preflight probes",
        str(logger.log_dir / "manifest.json"),
        0,
        "pass",
    )


def tcp_probe(host: str, port: int, timeout: float = 4.0) -> None:
    with socket.create_connection((host, port), timeout=timeout):
        pass


def ping_probe(host: str) -> None:
    subprocess.run(
        ["ping", "-c1", "-W2", host],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        timeout=4.0,
    )


def probe_once(host: str, rest: Rest) -> dict[str, dict[str, Any]]:
    probes: dict[str, dict[str, Any]] = {}

    def record(name: str, func: Callable[[], Any]) -> None:
        start = time.monotonic()
        try:
            result = func()
            probes[name] = {
                "status": "pass",
                "actual": "OK" if result is None else str(result),
                "duration_ms": int((time.monotonic() - start) * 1000),
            }
        except BaseException as exc:
            probes[name] = {
                "status": "fail",
                "actual": f"{type(exc).__name__}: {format_exception(exc)}",
                "duration_ms": int((time.monotonic() - start) * 1000),
            }

    record("ping", lambda: ping_probe(host))
    record("rest_version", lambda: request_json(host, "/v1/version"))
    record("readmem_0400", lambda: rest.read_mem(0x0400, 1).hex().upper())
    record("telnet_23", lambda: tcp_probe(host, 23))
    record("ftp_21", lambda: tcp_probe(host, 21))
    return probes


def read_jiffy(rest: Rest) -> int:
    raw = rest.read_mem(0x00A0, 3)
    return (raw[0] << 16) | (raw[1] << 8) | raw[2]


def assert_jiffy_advances(
    rest: Rest, label: str, samples: int = 4, gap: float = 0.2
) -> str:
    """Confirm the C64 jiffy clock ($00A0..$00A2) advances, i.e. the KERNAL
    IRQ is live on the running CPU.  A jiffy frozen across the whole window
    after a no-breakpoint continue is the H8 regression signature (BRK-pushed
    I flag left set on the resume RTI, leaving IRQs disabled).  Only valid
    where the CPU is expected to be RUNNING; during active Debug the CPU is
    halted and a frozen jiffy is correct, so never call this then."""
    samples = max(2, samples)
    values: list[int] = []
    for _ in range(samples):
        values.append(read_jiffy(rest))
        time.sleep(gap)
    advanced = (values[-1] - values[0]) % (1 << 24)
    if advanced == 0:
        raise SoakFailure(
            f"{label}: jiffy frozen at ${values[0]:06X} across {samples} "
            f"samples over ~{gap * (samples - 1):.1f}s (KERNAL IRQ not "
            "running; H8 regression?)"
        )
    return f"jiffy ${values[0]:06X}->${values[-1]:06X} (+{advanced})"


def run_liveness(
    runner: OperationRunner,
    host: str,
    rest: Rest,
    stage: str,
    active_ui: Optional[Callable[[], str]] = None,
    expect_running: bool = False,
) -> dict[str, dict[str, Any]]:
    results: dict[str, dict[str, Any]] = {}
    failures: list[str] = []

    def one(name: str, expected: str, func: Callable[[], Any]) -> None:
        def wrapped() -> str:
            result = func()
            return "OK" if result is None else str(result)

        start = time.monotonic()
        try:
            actual = wrapped()
        except BaseException as exc:
            duration = int((time.monotonic() - start) * 1000)
            text = f"{type(exc).__name__}: {format_exception(exc)}"
            results[name] = {"status": "fail", "actual": text, "duration_ms": duration}
            failures.append(f"{name}: {text}")
            runner.logger.log(
                "liveness",
                runner.ctx.scenario,
                runner.ctx.iteration,
                f"{stage} {name}",
                expected,
                text,
                duration,
                "fail",
                extra={"traceback": traceback.format_exc()},
            )
            return
        duration = int((time.monotonic() - start) * 1000)
        results[name] = {"status": "pass", "actual": actual, "duration_ms": duration}
        runner.logger.log(
            "liveness",
            runner.ctx.scenario,
            runner.ctx.iteration,
            f"{stage} {name}",
            expected,
            actual,
            duration,
            "pass",
        )

    one("ping -c1 -W2", "host replies to ICMP", lambda: ping_probe(host))
    one("REST /v1/version", "REST version returns JSON", lambda: request_json(host, "/v1/version"))
    one(
        "REST readmem $0400",
        "REST readmem returns one byte",
        lambda: rest.read_mem(0x0400, 1).hex().upper(),
    )
    one("Telnet TCP 23", "TCP 23 accepts connection", lambda: tcp_probe(host, 23))
    one("FTP TCP 21", "TCP 21 accepts connection", lambda: tcp_probe(host, 21))
    if expect_running:
        one(
            "jiffy advances",
            "jiffy clock $00A0 advances (KERNAL IRQ alive on running CPU)",
            lambda: assert_jiffy_advances(rest, f"{stage} jiffy"),
        )
    if active_ui is not None:
        one("active UI path", "UI accepts input and reflects monitor state", active_ui)
    if failures:
        runner.state.failing_operation = f"{stage} liveness"
        raise SoakFailure("; ".join(failures))
    return results


def classify_failure(
    logger: EventLogger,
    scenario: Scenario,
    iteration: int,
    host: str,
    rest: Rest,
    probe_window: float,
    failing_operation: str,
    failure_text: str,
) -> tuple[str, dict[str, Any]]:
    if degraded_endpoint_text(f"{failing_operation}\n{failure_text}"):
        samples = [probe_once(host, rest)]
        logger.log(
            "failure",
            scenario,
            iteration,
            "failure-classification-probe",
            "classify transient/API-unavailable/hard-wedge",
            "degraded production REST endpoint detected",
            max(item["duration_ms"] for item in samples[-1].values()),
            "fail",
        )
        return "degraded_rest_endpoint", {"samples": samples, "failure_text": failure_text}
    deadline = time.monotonic() + probe_window
    samples: list[dict[str, dict[str, Any]]] = []
    while True:
        sample = probe_once(host, rest)
        samples.append(sample)
        base_ok = all(
            sample[name]["status"] == "pass"
            for name in ("ping", "rest_version", "readmem_0400", "telnet_23", "ftp_21")
        )
        logger.log(
            "failure",
            scenario,
            iteration,
            "failure-classification-probe",
            "classify transient/API-unavailable/hard-wedge",
            json.dumps({name: item["status"] for name, item in sample.items()}, sort_keys=True),
            max(item["duration_ms"] for item in sample.values()),
            "pass" if base_ok else "fail",
        )
        if base_ok:
            combined = f"{failing_operation}\n{failure_text}"
            if any(token in combined for token in ("active UI", "screen", "monitor")):
                return "persistent_screen_or_monitor_unavailability", {"samples": samples}
            return "transient_transport_recovered", {"samples": samples}
        if time.monotonic() >= deadline:
            break
        time.sleep(2.0)

    final = samples[-1]
    network_names = ("ping", "rest_version", "telnet_23", "ftp_21")
    if all(final[name]["status"] == "fail" for name in network_names):
        return "hard_wedge", {"samples": samples}
    if final["rest_version"]["status"] == "fail" or final["readmem_0400"]["status"] == "fail":
        return "persistent_api_unavailability", {"samples": samples}
    return "persistent_screen_or_monitor_unavailability", {"samples": samples}


def should_recover(classification: str, args: argparse.Namespace) -> bool:
    if not args.recover_jtag or args.recover_on == "never":
        return False
    if args.recover_on == "any":
        return True
    if args.recover_on == "unavailable":
        return classification in (
            "hard_wedge",
            "persistent_api_unavailability",
            "degraded_rest_endpoint",
        )
    return classification == "hard_wedge"


def overlay_screen_text_or_degraded(rest: Rest, label: str, timeout: float = 6.0) -> str:
    deadline = time.monotonic() + timeout
    last_error = ""
    while time.monotonic() < deadline:
        try:
            text = rest.screen_text()
            if text.strip():
                return text
        except Exception as exc:  # noqa: BLE001
            last_error = f"{type(exc).__name__}: {format_exception(exc)}"
            if degraded_endpoint_text(last_error):
                time.sleep(0.3)
                continue
        time.sleep(0.2)
    raise EndpointDegraded(f"{label}: menu_screen unavailable or blank: {last_error}")


def overlay_monitor_is_open(rest: Rest) -> bool:
    try:
        lines = rest.screen_lines()
    except Exception as exc:  # noqa: BLE001
        if degraded_endpoint_text(f"{type(exc).__name__}: {format_exception(exc)}"):
            raise EndpointDegraded(f"menu_screen unavailable while checking monitor: {exc}") from exc
        return False
    return any(line.lstrip().startswith("MONITOR ") and "$" in line for line in lines)


def overlay_status_line_from_lines(lines: list[str]) -> str:
    for line in lines:
        if "CPU" in line and "VIC" in line:
            return line
        if "$A:" in line and "VIC" in line:
            return line
    return ""


def overlay_normal_monitor_is_open(rest: Rest) -> bool:
    try:
        lines = rest.screen_lines()
    except Exception as exc:  # noqa: BLE001
        if degraded_endpoint_text(f"{type(exc).__name__}: {format_exception(exc)}"):
            raise EndpointDegraded(f"menu_screen unavailable while checking monitor: {exc}") from exc
        return False
    has_header = any(line.lstrip().startswith("MONITOR ") and "$" in line for line in lines)
    return has_header and bool(overlay_status_line_from_lines(lines))


def wait_overlay_normal_monitor(rest: Rest, label: str, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        try:
            lines = rest.screen_lines()
            last = "\n".join(lines)
            has_header = any(line.lstrip().startswith("MONITOR ") and "$" in line for line in lines)
            if has_header and overlay_status_line_from_lines(lines):
                return "monitor visible"
        except EndpointDegraded:
            raise
        except Exception as exc:  # noqa: BLE001
            last = f"<screen capture failed: {type(exc).__name__}: {format_exception(exc)}>"
        time.sleep(0.25)
    raise RestFailure(f"{label}: normal monitor status line not visible\n--- last screen ---\n{last}")


def open_overlay_monitor_strict(rest: Rest, label: str) -> str:
    last = ""
    last_endpoint_error = ""
    force_menu_button = False

    for attempt in range(1, 5):
        try:
            if overlay_normal_monitor_is_open(rest):
                return "monitor visible"
        except EndpointDegraded as exc:
            last_endpoint_error = format_exception(exc)

        try:
            text = rest.screen_text()
            last = text
        except Exception as exc:  # noqa: BLE001
            text = ""
            last_endpoint_error = f"{type(exc).__name__}: {format_exception(exc)}"

        if "MONITOR " in text and "$" in text:
            try:
                rest.tap(["run_stop"])
                return wait_overlay_normal_monitor(
                    rest, f"{label}: leave stale Debug state", 2.5
                )
            except Exception as exc:  # noqa: BLE001
                last = f"{last}\n<run_stop did not restore normal monitor: {exc}>"

        if force_menu_button or not text.strip():
            try:
                rest.menu_button()
            except Exception as exc:  # noqa: BLE001
                last_endpoint_error = f"{type(exc).__name__}: {format_exception(exc)}"
            time.sleep(2.5)
            force_menu_button = False

        try:
            if overlay_normal_monitor_is_open(rest):
                return "monitor visible"
            text = rest.screen_text()
            last = text
        except Exception as exc:  # noqa: BLE001
            text = ""
            last_endpoint_error = f"{type(exc).__name__}: {format_exception(exc)}"

        if "MONITOR " in text and "$" in text:
            try:
                rest.tap(["run_stop"])
                return wait_overlay_normal_monitor(
                    rest, f"{label}: leave stale Debug state after open", 2.5
                )
            except Exception as exc:  # noqa: BLE001
                last = f"{last}\n<run_stop did not restore normal monitor: {exc}>"

        try:
            rest.tap(["ctrl", "o"])
        except Exception as exc:  # noqa: BLE001
            last_endpoint_error = f"{type(exc).__name__}: {format_exception(exc)}"
        try:
            return wait_overlay_normal_monitor(rest, f"{label}: ctrl+O monitor entry", 4.0)
        except EndpointDegraded as exc:
            last_endpoint_error = format_exception(exc)
        except Exception as exc:  # noqa: BLE001
            last = f"{last}\n<ctrl+O attempt {attempt} did not enter normal monitor: {exc}>"

        if attempt % 2 == 0:
            force_menu_button = True

    if degraded_endpoint_text(last_endpoint_error):
        raise EndpointDegraded(f"{label}: menu_screen/input endpoint degraded: {last_endpoint_error}")
    raise RestFailure(
        f"{label}: UI did not reach normal Machine Code Monitor after bounded attempts\n"
        f"--- last screen ---\n{last}"
    )


def overlay_active_ui(runner: OperationRunner, rest: Rest, stage: str, label: str) -> None:
    runner.run(
        "liveness",
        f"{stage} active UI path",
        "UI accepts input and screen reflects Machine Code Monitor state",
        lambda: open_overlay_monitor_strict(rest, label),
        actual_ok="monitor visible",
    )


def set_interface_type_robust(rest: Rest, value: str, label: str) -> None:
    last_exc: Optional[BaseException] = None
    for attempt in range(1, 6):
        try:
            overlay_lifecycle.set_interface_type(rest, value)
            return
        except (OSError, TimeoutError, urllib.error.URLError, RestFailure) as exc:
            last_exc = exc
            time.sleep(min(1.5 * attempt, 5.0))
    assert last_exc is not None
    raise last_exc


def telnet_active_ui(session: monitor_telnet.MonitorSession) -> str:
    telnet_debug._reopen_monitor(session)  # noqa: SLF001
    snap = session.capture()
    status = snap.line(snap.find_status_line()).strip()
    if not status:
        raise monitor_telnet.Failure("empty telnet status line")
    return status


def close_telnet_session(session: Optional[monitor_telnet.MonitorSession]) -> None:
    if session is None:
        return
    try:
        session.close()
    except Exception:
        pass


def open_telnet_session(args: argparse.Namespace) -> monitor_telnet.MonitorSession:
    return monitor_telnet.MonitorSession(args.host, args.port, args.password, args.timeout)


def reset_with_telnet_closed(
    host: str,
    telnet_holder: dict[str, Optional[monitor_telnet.MonitorSession]],
) -> None:
    close_telnet_session(telnet_holder.get("session"))
    telnet_holder["session"] = None
    telnet_debug._reset_c64_core(host)  # noqa: SLF001


def run_overlay_iteration(
    runner: OperationRunner,
    rest: Rest,
    scenario: Scenario,
    host: str,
    iterations: int,
) -> None:
    label = f"{scenario.label} iter {runner.ctx.iteration}/{iterations}"
    runner.run(
        "iteration",
        "reset to READY",
        "C64 reaches READY after REST reset",
        lambda: overlay_lifecycle.reset_to_basic(rest, label),
    )
    run_liveness(
        runner,
        host,
        rest,
        "after reset",
        expect_running=True,
    )
    runner.run(
        "operation",
        "load debug fixture",
        "fixture writes and round-trips through REST; live C64 screen may briefly show @ fill",
        lambda: overlay_lifecycle.load_fixture(rest),
    )
    overlay_active_ui(
        runner, rest, "after fixture load", f"{label}: active UI after fixture load"
    )
    runner.run(
        "operation",
        "debug breakpoint continue sequence",
        "step/set breakpoint/continue/remove/continue succeeds",
        lambda: overlay_first_session_from_open_monitor(rest, scenario.banking, label),
    )
    runner.run(
        "liveness",
        "jiffy advances after no-breakpoint continue",
        "KERNAL IRQ stays live after the monitor resumes the CPU without a "
        "breakpoint (H8 resume-SR contract)",
        lambda: assert_jiffy_advances(rest, f"{label}: post-continue jiffy"),
    )
    runner.run(
        "iteration",
        "reset to READY for re-entry",
        "C64 reaches READY after post-continue reset",
        lambda: overlay_lifecycle.reset_to_basic(rest, f"{label}: re-entry reset"),
    )
    run_liveness(
        runner,
        host,
        rest,
        "after re-entry reset",
        expect_running=True,
    )
    overlay_active_ui(
        runner, rest, "after re-entry reset", f"{label}: active UI after re-entry reset"
    )
    runner.run(
        "operation",
        "re-enter Debug and first step",
        "monitor re-enters Debug and first step/trace reaches expected PC",
        lambda: overlay_reenter_and_step_without_reset(rest, scenario.banking, label),
    )
    run_liveness(
        runner,
        host,
        rest,
        "after re-entry step",
    )
    runner.run(
        "operation",
        "clear REST input state",
        "REST-held keyboard and joystick state is released",
        lambda: (rest.release_all(), time.sleep(0.3)),
    )


def require_overlay_monitor_visible(rest: Rest, label: str) -> str:
    if not overlay_monitor_is_open(rest):
        raise RestFailure(f"{label}: monitor not visible")
    return "monitor visible"


def overlay_first_session_from_open_monitor(rest: Rest, scenario: str, label: str) -> None:
    if scenario == "ram":
        overlay_lifecycle.enter_debug_at(rest, 0xC300, 7, "RAM", label)
        rest.send_text("d")
        overlay_lifecycle.wait_pc(rest, 0xC302, f"{label}: step to C302")
        overlay_lifecycle.ensure_breakpoint_at(rest, 0xC302, 7, "RAM", f"{label}: set bp C302")
        rest.send_text("g")
        overlay_lifecycle.wait_pc(rest, 0xC302, f"{label}: run to bp C302")
        overlay_lifecycle.clear_breakpoint_at(rest, 0xC302, 7, f"{label}: remove bp C302")
        rest.send_text("g")
        overlay_lifecycle.assert_region_changes(
            rest, 0x0600, 0x01E8, f"{label}: RAM loop after continue", minimum_cells=2
        )
    elif scenario == "ram-under-rom":
        overlay_lifecycle.enter_debug_at(rest, 0xC000, 7, "RAM", label)
        for pc in (0xC001, 0xC003, 0xC005, 0xC007, 0xC009, 0xC00B, 0xE000):
            rest.send_text("d")
            overlay_lifecycle.wait_pc(rest, pc, f"{label}: step to {pc:04X}")
        overlay_lifecycle.ensure_breakpoint_at(rest, 0xE000, 5, "RAM", f"{label}: set bp E000")
        rest.send_text("g")
        overlay_lifecycle.wait_pc(rest, 0xE000, f"{label}: run to bp E000")
        overlay_lifecycle.clear_breakpoint_at(rest, 0xE000, 5, f"{label}: remove bp E000")
        rest.send_text("g")
        overlay_lifecycle.assert_region_changes(
            rest, 0x0400, 0x0200, f"{label}: under-ROM loop after continue", minimum_cells=2
        )
    elif scenario == "rom":
        overlay_lifecycle.enter_debug_at(rest, 0xE002, 7, "KRN", label)
        overlay_lifecycle.ensure_breakpoint_at(rest, 0xBC0F, 7, "BAS", f"{label}: set bp BC0F")
        rest.send_text("g")
        overlay_lifecycle.wait_pc(rest, 0xBC0F, f"{label}: run to bp BC0F")
        overlay_lifecycle.clear_breakpoint_at(rest, 0xBC0F, 7, f"{label}: remove bp BC0F")
        rest.send_text("g")
        time.sleep(1.0)
    else:
        raise RestFailure(f"unknown scenario {scenario!r}")


def overlay_reenter_and_step_without_reset(rest: Rest, banking: str, label: str) -> None:
    if banking == "ram":
        overlay_lifecycle.enter_debug_at(rest, 0xC300, 7, "RAM", label + " reenter")
        rest.send_text("d")
        overlay_lifecycle.wait_pc(rest, 0xC302, f"{label}: re-entered step to C302")
    elif banking == "ram-under-rom":
        overlay_lifecycle.enter_debug_at(rest, 0xC000, 7, "RAM", label + " reenter")
        rest.send_text("d")
        overlay_lifecycle.wait_pc(rest, 0xC001, f"{label}: re-entered step to C001")
    elif banking == "rom":
        overlay_lifecycle.enter_debug_at(rest, 0xE002, 7, "KRN", label + " reenter")
        rest.send_text("t")
        overlay_lifecycle.wait_pc(rest, 0xBC0F, f"{label}: re-entered trace to BC0F")
    else:
        raise RestFailure(f"unknown banking {banking!r}")


def run_telnet_iteration(
    runner: OperationRunner,
    host: str,
    rest: Rest,
    scenario: Scenario,
    iterations: int,
    args: argparse.Namespace,
    telnet_holder: dict[str, Optional[monitor_telnet.MonitorSession]],
) -> None:
    label = f"{scenario.label} iter {runner.ctx.iteration}/{iterations}"
    close_telnet_session(telnet_holder.get("session"))
    telnet_holder["session"] = None
    runner.run(
        "iteration",
        "reset to READY",
        "C64 reaches READY after REST reset before any Telnet monitor owns the UI",
        lambda: telnet_debug._reset_c64_core(host),  # noqa: SLF001
    )
    run_liveness(
        runner,
        host,
        rest,
        "after reset",
        expect_running=True,
    )
    runner.run(
        "liveness",
        "open Telnet monitor",
        "Telnet monitor connects and enters Machine Code Monitor after reset",
        lambda: telnet_holder.__setitem__("session", open_telnet_session(args)),
    )
    session = telnet_holder["session"]
    if session is None:
        raise SoakFailure("missing Telnet session after open")
    run_liveness(
        runner,
        host,
        rest,
        "after Telnet monitor open",
        lambda: telnet_active_ui(session),
    )
    runner.run(
        "operation",
        "clear stale breakpoints",
        "all breakpoint slots clear",
        lambda: telnet_debug._clear_all_breakpoints(session, f"{label}: setup clear breakpoints"),  # noqa: SLF001
    )
    if scenario.banking in ("ram", "ram-under-rom"):
        runner.run(
            "operation",
            "load debug fixture",
            "fixture writes through REST",
            lambda: telnet_debug._load_repeat_redebug_fixtures(host),  # noqa: SLF001
        )
    runner.run(
        "operation",
        "debug breakpoint continue sequence",
        "step/set breakpoint/continue/remove/continue succeeds",
        lambda: telnet_first_session(host, session, scenario.banking, label),
    )
    runner.run(
        "iteration",
        "reset to READY for re-entry",
        "C64 reaches READY after post-continue reset with Telnet monitor closed",
        lambda: reset_with_telnet_closed(host, telnet_holder),
    )
    run_liveness(
        runner,
        host,
        rest,
        "after re-entry reset",
        expect_running=True,
    )
    runner.run(
        "liveness",
        "reopen Telnet monitor",
        "Telnet monitor reconnects after reset and reflects Machine Code Monitor state",
        lambda: telnet_holder.__setitem__("session", open_telnet_session(args)),
    )
    session = telnet_holder["session"]
    if session is None:
        raise SoakFailure("missing Telnet session after reopen")
    run_liveness(
        runner,
        host,
        rest,
        "after Telnet monitor reopen",
        lambda: telnet_active_ui(session),
    )
    runner.run(
        "operation",
        "re-enter Debug and first step",
        "monitor re-enters Debug and first step/trace reaches expected PC",
        lambda: telnet_reenter_and_step(session, scenario.banking, label),
    )
    run_liveness(
        runner,
        host,
        rest,
        "after re-entry step",
        lambda: telnet_active_ui(session),
    )
    runner.run(
        "operation",
        "leave Debug state",
        "Telnet monitor is not left in Debug context",
        lambda: telnet_debug._ensure_no_debug(session),  # noqa: SLF001
    )
    close_telnet_session(telnet_holder.get("session"))
    telnet_holder["session"] = None

def telnet_first_session(
    host: str,
    session: monitor_telnet.MonitorSession,
    banking: str,
    label: str,
) -> None:
    if banking == "ram":
        telnet_debug._telnet_run_to_breakpoint_then_continue(  # noqa: SLF001
            host,
            session,
            label,
            0xC300,
            0xC302,
            7,
            "RAM",
            (0xC302,),
            progress_addr=0x0600,
            progress_len=0x01E8,
        )
    elif banking == "ram-under-rom":
        telnet_debug._telnet_run_to_breakpoint_then_continue(  # noqa: SLF001
            host,
            session,
            label,
            0xC000,
            0xE000,
            5,
            "RAM",
            (0xC001, 0xC003, 0xC005, 0xC007, 0xC009, 0xC00B, 0xE000),
        )
    elif banking == "rom":
        telnet_debug._telnet_run_to_breakpoint_then_continue(  # noqa: SLF001
            host,
            session,
            label,
            0xE002,
            0xBC0F,
            7,
            "BAS",
            (),
            start_source="KRN",
            start_bank=7,
        )
    else:
        raise monitor_telnet.Failure(f"unknown banking {banking!r}")


def telnet_reenter_and_step(
    session: monitor_telnet.MonitorSession,
    banking: str,
    label: str,
) -> None:
    if banking == "ram":
        telnet_debug._enter_lifecycle_debug_at(  # noqa: SLF001
            session, 0xC300, 7, "RAM", f"{label}: re-enter Debug"
        )
        telnet_debug._step_and_assert_pc(  # noqa: SLF001
            session, "D", 0xC302, f"{label}: first re-entered step"
        )
    elif banking == "ram-under-rom":
        telnet_debug._enter_lifecycle_debug_at(  # noqa: SLF001
            session, 0xC000, 7, "RAM", f"{label}: re-enter Debug"
        )
        telnet_debug._step_and_assert_pc(  # noqa: SLF001
            session, "D", 0xC001, f"{label}: first re-entered step"
        )
    elif banking == "rom":
        telnet_debug._enter_lifecycle_debug_at(  # noqa: SLF001
            session, 0xE002, 7, "KRN", f"{label}: re-enter Debug"
        )
        telnet_debug._step_and_assert_pc(  # noqa: SLF001
            session, "T", 0xBC0F, f"{label}: first re-entered trace"
        )
    else:
        raise monitor_telnet.Failure(f"unknown banking {banking!r}")


def capture_failure_evidence(
    logger: EventLogger,
    state: ScenarioState,
    ctx: RunContext,
    host: str,
    rest: Rest,
    exc: BaseException,
    classification: str,
    classification_data: dict[str, Any],
    syslog_file: Path,
    telnet_session: Optional[monitor_telnet.MonitorSession],
) -> Path:
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    scenario = state.scenario
    failure_dir = logger.failures_dir / f"{timestamp}-{scenario.slug}-iter{ctx.iteration}"
    failure_dir.mkdir(parents=True, exist_ok=True)

    context = {
        "run_id": logger.run_id,
        "scenario": scenario.label,
        "iteration": ctx.iteration,
        "failing_operation": state.failing_operation or ctx.operation,
        "last_successful_operation": state.last_successful_operation,
        "failure_last_successful_operation": state.failure_last_successful_operation,
        "failure_kind": classification,
        "failure_detail": state.failure_detail,
        "exception": f"{type(exc).__name__}: {format_exception(exc)}",
    }
    (failure_dir / "context.json").write_text(
        json.dumps(context, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (failure_dir / "traceback.txt").write_text(
        "".join(traceback.format_exception(type(exc), exc, exc.__traceback__)),
        encoding="utf-8",
    )
    (failure_dir / "probes.json").write_text(
        json.dumps(classification_data, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    copy_recent_lines(logger.log_dir / "events.jsonl", failure_dir / "recent-events.jsonl", 120)
    copy_recent_lines(logger.log_dir / "run.log", failure_dir / "run-log-tail.txt", 160)
    (failure_dir / "endpoint-probes.json").write_text(
        json.dumps(endpoint_probe_snapshot(host, rest), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (failure_dir / "memory-snapshots.json").write_text(
        json.dumps(memory_snapshot(rest), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    try:
        (failure_dir / "ui-screen.txt").write_text(rest.screen_text(), encoding="utf-8")
    except Exception as screen_exc:  # noqa: BLE001
        (failure_dir / "ui-screen.txt").write_text(
            f"<capture failed: {type(screen_exc).__name__}: {format_exception(screen_exc)}>\n",
            encoding="utf-8",
        )

    if telnet_session is not None:
        try:
            (failure_dir / "telnet-screen.txt").write_text(
                telnet_session.capture().text(), encoding="utf-8"
            )
        except Exception as screen_exc:  # noqa: BLE001
            (failure_dir / "telnet-screen.txt").write_text(
                f"<capture failed: {type(screen_exc).__name__}: {format_exception(screen_exc)}>\n",
                encoding="utf-8",
            )

    if syslog_file.exists():
        tail = tail_lines(syslog_file, 200)
        (failure_dir / "syslog-tail.txt").write_text(tail, encoding="utf-8")
        clients = "\n".join(line for line in tail.splitlines() if "connection from" in line)
        (failure_dir / "syslog-clients.txt").write_text(clients + "\n", encoding="utf-8")
    else:
        (failure_dir / "syslog-tail.txt").write_text(
            f"<syslog file not found: {syslog_file}>\n", encoding="utf-8"
        )

    logger.log(
        "failure",
        scenario,
        ctx.iteration,
        "capture evidence",
        "write failure evidence bundle",
        str(failure_dir),
        0,
        "pass",
    )
    return failure_dir


def copy_recent_lines(src: Path, dst: Path, count: int) -> None:
    if src.exists():
        dst.write_text(tail_lines(src, count), encoding="utf-8")
    else:
        dst.write_text(f"<missing: {src}>\n", encoding="utf-8")


def endpoint_probe_snapshot(host: str, rest: Rest) -> dict[str, Any]:
    snapshot: dict[str, Any] = {
        "base_liveness": probe_once(host, rest),
        "menu_screen": {},
        "version": {},
    }

    def capture(name: str, func: Callable[[], Any]) -> None:
        start = time.monotonic()
        try:
            value = func()
            snapshot[name] = {
                "status": "pass",
                "actual": value,
                "duration_ms": int((time.monotonic() - start) * 1000),
            }
        except BaseException as exc:
            snapshot[name] = {
                "status": "fail",
                "actual": f"{type(exc).__name__}: {format_exception(exc)}",
                "duration_ms": int((time.monotonic() - start) * 1000),
            }

    capture("version", lambda: request_json(host, "/v1/version"))

    def menu_screen_summary() -> dict[str, Any]:
        raw = rest.menu_screen_raw()
        chars = raw[:1000]
        lines = []
        for y in range(25):
            row = []
            for x in range(40):
                c = chars[y * 40 + x] & 0x7F
                row.append(chr(c) if 0x20 <= c <= 0x7E else " ")
            lines.append("".join(row).rstrip())
        return {"length": len(raw), "text": "\n".join(lines)}

    capture("menu_screen", menu_screen_summary)
    return snapshot


def memory_snapshot(rest: Rest) -> dict[str, Any]:
    regions = [
        ("screen_0400", 0x0400, 80),
        ("jiffy_00a2", 0x00A2, 3),
        ("cpu_port_0001", 0x0001, 1),
        ("debug_scratch_0314", 0x0314, 16),
        ("cassette_buffer_033c", 0x033C, 64),
        ("fixture_c300", 0xC300, 16),
    ]
    data: dict[str, Any] = {}
    for name, addr, length in regions:
        try:
            raw = rest.read_mem(addr, length)
            data[name] = {
                "address": f"{addr:04X}",
                "length": length,
                "hex": raw.hex().upper(),
            }
        except BaseException as exc:
            data[name] = {
                "address": f"{addr:04X}",
                "length": length,
                "error": f"{type(exc).__name__}: {format_exception(exc)}",
            }
    return data


def tail_lines(path: Path, count: int) -> str:
    try:
        result = subprocess.run(
            ["tail", "-n", str(count), str(path)],
            check=True,
            text=True,
            capture_output=True,
        )
        return result.stdout
    except Exception as exc:  # noqa: BLE001
        return f"<tail failed: {type(exc).__name__}: {format_exception(exc)}>\n"


def capture_preflight_evidence(
    logger: EventLogger,
    host: str,
    syslog_file: Path,
    probes: dict[str, dict[str, Any]],
) -> Path:
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    evidence_dir = logger.failures_dir / f"{timestamp}-preflight-device-unreachable"
    evidence_dir.mkdir(parents=True, exist_ok=True)
    (evidence_dir / "context.json").write_text(
        json.dumps(
            {
                "run_id": logger.run_id,
                "host": host,
                "phase": "preflight",
                "failure_kind": "hard_wedge",
                "operation": "device preflight",
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    (evidence_dir / "probes.json").write_text(
        json.dumps({"samples": [probes]}, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    if syslog_file.exists():
        tail = tail_lines(syslog_file, 200)
        (evidence_dir / "syslog-tail.txt").write_text(tail, encoding="utf-8")
        clients = "\n".join(line for line in tail.splitlines() if "connection from" in line)
        (evidence_dir / "syslog-clients.txt").write_text(clients + "\n", encoding="utf-8")
    else:
        (evidence_dir / "syslog-tail.txt").write_text(
            f"<syslog file not found: {syslog_file}>\n", encoding="utf-8"
        )
    logger.log(
        "failure",
        None,
        0,
        "preflight evidence",
        "capture probes and syslog before unattended recovery",
        str(evidence_dir),
        0,
        "pass",
    )
    return evidence_dir


def ensure_syslog_sink(logger: EventLogger, syslog_file: Path) -> Optional[subprocess.Popen[bytes]]:
    pattern = "syslog_listen"
    probe = subprocess.run(
        ["pgrep", "-af", pattern],
        text=True,
        capture_output=True,
        check=False,
    )
    current = "\n".join(
        line for line in probe.stdout.splitlines() if "pgrep -af" not in line
    ).strip()
    if current:
        logger.log("setup", None, 0, "syslog sink", "syslog listener is running", current, 0, "pass")
        return None
    script = Path("/tmp/syslog_listen.py")
    if not script.exists():
        logger.log(
            "setup",
            None,
            0,
            "syslog sink",
            "syslog listener is running or startable",
            f"missing {script}; continuing without starting listener",
            0,
            "fail",
        )
        return None
    syslog_file.parent.mkdir(parents=True, exist_ok=True)
    proc = subprocess.Popen(
        ["python3", str(script), str(syslog_file)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.5)
    logger.log(
        "setup",
        None,
        0,
        "syslog sink",
        "syslog listener is running",
        f"started pid {proc.pid}",
        0,
        "pass",
    )
    return proc


def wait_for_rest(host: str, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    last_exc: Optional[BaseException] = None
    while time.monotonic() < deadline:
        try:
            request_json(host, "/v1/version", timeout=5.0)
            return
        except BaseException as exc:
            last_exc = exc
            time.sleep(2.0)
    if last_exc is not None:
        raise last_exc
    raise TimeoutError(f"timed out waiting for REST on {host}")


def recover_jtag(
    logger: EventLogger,
    scenario: Scenario,
    iteration: int,
    args: argparse.Namespace,
    failure_dir: Optional[Path] = None,
    ) -> bool:
    start = time.monotonic()
    recovery_dir = logger.recoveries_dir / (
        f"{datetime.now().strftime('%Y%m%d-%H%M%S')}-{scenario.slug}-iter{iteration}"
    )
    recovery_dir.mkdir(parents=True, exist_ok=True)
    deploy_log = recovery_dir / "jtag-deploy.log"
    context = {
        "run_id": logger.run_id,
        "scenario": scenario.label,
        "iteration": iteration,
        "jtag_deploy": args.jtag_deploy,
        "started_at": logger.now(),
        "trigger_failure_dir": str(failure_dir) if failure_dir else "",
    }
    (recovery_dir / "context.json").write_text(
        json.dumps(context, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    logger.log(
        "recovery",
        scenario,
        iteration,
        "JTAG redeploy",
        "deploy firmware over JTAG",
        f"starting; log {deploy_log}",
        0,
        "pass",
        stdout=f"[{time.strftime('%H:%M:%S', time.gmtime(logger.elapsed()))}] recovery: starting JTAG redeploy",
    )
    try:
        with deploy_log.open("wb") as handle:
            result = subprocess.run(
                [args.jtag_deploy],
                cwd=REPO_ROOT,
                stdout=handle,
                stderr=subprocess.STDOUT,
                check=False,
            )
        if result.returncode != 0:
            raise SoakFailure(f"JTAG deploy failed with exit code {result.returncode}")
        wait_for_rest(args.host, timeout=90.0)
        rest = Rest(args.host, args.timeout)
        overlay_lifecycle.reset_to_basic(rest, "post-recovery reset")
        if scenario.view in VIEW_TO_INTERFACE:
            overlay_lifecycle.set_interface_type(rest, VIEW_TO_INTERFACE[scenario.view])
        else:
            monitor_telnet.wait_for_monitor_ready(
                args.host, args.port, args.password, args.timeout
            )
        if args.syslog_file:
            ensure_syslog_sink(logger, Path(args.syslog_file))
        post_rest = Rest(args.host, args.timeout)
        (recovery_dir / "post-recovery-probes.json").write_text(
            json.dumps(endpoint_probe_snapshot(args.host, post_rest), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        duration = int((time.monotonic() - start) * 1000)
        logger.log(
            "recovery",
            scenario,
            iteration,
            "JTAG redeploy",
            "device reachable and reset to READY after deployment",
            f"recovered; log {deploy_log}",
            duration,
            "pass",
        )
        return True
    except BaseException as exc:
        duration = int((time.monotonic() - start) * 1000)
        logger.log(
            "recovery",
            scenario,
            iteration,
            "JTAG redeploy",
            "device reachable and reset to READY after deployment",
            f"{type(exc).__name__}: {format_exception(exc)}; log {deploy_log}",
            duration,
            "fail",
            extra={"traceback": traceback.format_exc()},
        )
        return False


def preflight_recover_if_unreachable(
    logger: EventLogger,
    args: argparse.Namespace,
) -> None:
    rest = Rest(args.host, args.timeout)
    probes = probe_once(args.host, rest)
    logger.log(
        "setup",
        None,
        0,
        "device preflight",
        "ping/REST/readmem/Telnet/FTP are probed before scenarios",
        json.dumps({name: item["status"] for name, item in probes.items()}, sort_keys=True),
        max(item["duration_ms"] for item in probes.values()),
        "pass" if all(item["status"] == "pass" for item in probes.values()) else "fail",
    )
    network_names = ("ping", "rest_version", "telnet_23", "ftp_21")
    all_ok = all(item["status"] == "pass" for item in probes.values())
    if all_ok:
        return
    hard_wedge = all(probes[name]["status"] == "fail" for name in network_names)
    capture_preflight_evidence(logger, args.host, Path(args.syslog_file), probes)
    preflight_classification = "hard_wedge" if hard_wedge else "persistent_api_unavailability"
    if not should_recover(preflight_classification, args):
        logger.log(
            "recovery",
            None,
            0,
            "preflight JTAG redeploy",
            "degraded device must be recovered before unattended run",
            "disabled by CLI",
            0,
            "fail",
        )
        return
    scenario = Scenario("setup", "device", "preflight")
    recovered = recover_jtag(logger, scenario, 0, args)
    if not recovered:
        logger.log(
            "recovery",
            scenario,
            0,
            "preflight JTAG redeploy",
            "device reachable after recovery",
            "failed",
            0,
            "fail",
        )


def scenario_list(args: argparse.Namespace) -> list[Scenario]:
    views = normalize_selected(args.view, args.views, VIEWS)
    bankings = normalize_selected(args.banking, args.bankings, BANKINGS)
    selected: list[Scenario] = []
    if args.clean:
        for view in views:
            for banking in bankings:
                selected.append(Scenario("clean", view, banking))
    if args.concurrency:
        for banking in bankings:
            if "overlay" in views:
                selected.append(Scenario("concurrency", "overlay", banking))
    if args.scenario:
        wanted = set()
        for item in args.scenario:
            parts = item.split(":")
            if len(parts) == 2:
                wanted.add(("clean", parts[0], parts[1]))
                wanted.add(("concurrency", parts[0], parts[1]))
            elif len(parts) == 3:
                wanted.add((parts[0], parts[1], parts[2]))
            else:
                raise SystemExit(f"invalid --scenario {item!r}; use view:banking or type:view:banking")
        selected = [s for s in selected if (s.kind, s.view, s.banking) in wanted]
    return selected


def parse_csv(value: str, valid: tuple[str, ...]) -> list[str]:
    valid_set = set(valid)
    items = [item.strip() for item in value.split(",") if item.strip()]
    unknown = [item for item in items if item not in valid_set]
    if unknown:
        raise SystemExit(f"unknown value(s): {', '.join(unknown)}; choose from {', '.join(valid)}")
    return items


def normalize_selected(
    repeat_values: Optional[list[str]],
    csv_value: Optional[str],
    valid: tuple[str, ...],
) -> list[str]:
    items: list[str] = []
    if csv_value:
        items.extend(parse_csv(csv_value, valid))
    if repeat_values:
        for value in repeat_values:
            items.extend(parse_csv(value, valid))
    if not items:
        items = list(valid)
    seen: set[str] = set()
    result: list[str] = []
    for item in items:
        if item in seen:
            continue
        seen.add(item)
        result.append(item)
    return result


def run_scenario(args: argparse.Namespace, logger: EventLogger, scenario: Scenario) -> ScenarioState:
    state = ScenarioState(scenario)
    ctx = RunContext(scenario=scenario)
    rest = Rest(args.host, args.timeout)
    telnet_holder: dict[str, Optional[monitor_telnet.MonitorSession]] = {"session": None}
    concurrency: Optional[ConcurrencyLoad] = None

    while not state.passed and not state.failed:
        state.attempts += 1
        ctx.iteration = 0
        ctx.operation = ""
        ctx.last_successful_operation = ""
        state.last_successful_operation = ""
        logger.log(
            "scenario",
            scenario,
            0,
            f"start attempt {state.attempts}",
            f"{args.iterations} consecutive clean iterations",
            "starting",
            0,
            "pass",
        )
        try:
            if scenario.view in VIEW_TO_INTERFACE:
                OperationRunner(logger, ctx, state).run(
                    "setup",
                    "set UI mode",
                    f"Interface Type becomes {VIEW_TO_INTERFACE[scenario.view]}",
                    lambda: set_interface_type_robust(
                        rest, VIEW_TO_INTERFACE[scenario.view], f"{scenario.label}: set UI mode"
                    ),
                )
            if scenario.kind == "concurrency":
                concurrency = ConcurrencyLoad(args.host, logger, scenario)
                concurrency.start()

            for iteration in range(1, args.iterations + 1):
                ctx.iteration = iteration
                ctx.operation = ""
                runner = OperationRunner(logger, ctx, state)
                if scenario.view in ("overlay", "freeze"):
                    run_overlay_iteration(runner, rest, scenario, args.host, args.iterations)
                else:
                    run_telnet_iteration(
                        runner, args.host, rest, scenario, args.iterations, args, telnet_holder
                    )
                logger.log(
                    "iteration",
                    scenario,
                    iteration,
                    "iteration complete",
                    "iteration finishes without stale breakpoint/trap state",
                    "complete",
                    0,
                    "pass",
                    stdout=(
                        f"[{time.strftime('%H:%M:%S', time.gmtime(logger.elapsed()))}] "
                        f"{scenario.label} iter {iteration}/{args.iterations} COMPLETE"
                    ),
                )
            state.passed = True
            logger.log(
                "scenario",
                scenario,
                args.iterations,
                "scenario complete",
                f"{args.iterations} consecutive clean iterations",
                "passed",
                0,
                "pass",
            )
        except Exception as raw_exc:
            if isinstance(raw_exc, SoakFailure):
                exc = raw_exc
            else:
                if not state.failing_operation:
                    state.failing_operation = ctx.operation or "scenario setup"
                exc = SoakFailure(
                    f"{state.failing_operation} failed unexpectedly: "
                    f"{type(raw_exc).__name__}: {format_exception(raw_exc)}",
                    raw_exc,
                )
            state.failure_iteration = ctx.iteration
            if not state.failing_operation:
                state.failing_operation = ctx.operation
            state.failure_detail = str(exc)
            state.failure_last_successful_operation = state.last_successful_operation
            if not state.failure_last_successful_operation:
                state.failure_last_successful_operation = ctx.last_successful_operation
            classification, classification_data = classify_failure(
                logger,
                scenario,
                ctx.iteration,
                args.host,
                rest,
                args.probe_window,
                state.failing_operation or ctx.operation,
                state.failure_detail,
            )
            state.failure_kind = classification
            state.failure_dir = capture_failure_evidence(
                logger,
                state,
                ctx,
                args.host,
                rest,
                exc.exc or exc,
                classification,
                classification_data,
                Path(args.syslog_file),
                telnet_holder.get("session"),
            )
            state.failure_dirs.append(state.failure_dir)
            if (
                should_recover(classification, args)
                and state.recoveries < args.max_recoveries
            ):
                state.recoveries += 1
                recovered = recover_jtag(logger, scenario, ctx.iteration, args, state.failure_dir)
                outcome = f"recovery {state.recoveries}: {'ok' if recovered else 'failed'}"
                state.recovery_outcomes.append(outcome)
                if recovered:
                    logger.log(
                        "recovery",
                        scenario,
                        ctx.iteration,
                        "restart scenario",
                        "scenario restarts from iteration 1 after recovery",
                        "restart",
                        0,
                        "pass",
                    )
                    close_telnet_session(telnet_holder.get("session"))
                    telnet_holder["session"] = None
                    continue
            elif should_recover(classification, args):
                logger.log(
                    "recovery",
                    scenario,
                    ctx.iteration,
                    "JTAG redeploy skipped",
                    "recoverable failure should redeploy unless recovery budget is exhausted",
                    f"recovery budget exhausted ({state.recoveries}/{args.max_recoveries})",
                    0,
                    "fail",
                )
            state.failed = True
        finally:
            if concurrency is not None:
                concurrency.stop()
                concurrency = None
            close_telnet_session(telnet_holder.get("session"))
            telnet_holder["session"] = None
    return state


def write_summary(logger: EventLogger, states: list[ScenarioState], args: argparse.Namespace) -> int:
    summary = logger.log_dir / "summary.md"

    def evidence_for(state: ScenarioState) -> str:
        paths = state.failure_dirs or ([state.failure_dir] if state.failure_dir else [])
        return "<br>".join(str(path) for path in paths if path)

    lines = [
        "# Machine Code Monitor Soak Summary",
        "",
        f"- Run id: `{logger.run_id}`",
        f"- Host: `{args.host}`",
        f"- Iterations required: `{args.iterations}`",
        f"- Log directory: `{logger.log_dir}`",
        "",
        "| Scenario | Status | Attempts | Recoveries | Failure iteration | Last successful operation | Failing operation | Failure kind | Evidence |",
        "|---|---:|---:|---:|---:|---|---|---|---|",
    ]
    for state in states:
        status = "SHOWSTOPPER" if state.recoveries else ("PASS" if state.passed else "FAIL")
        evidence = evidence_for(state)
        lines.append(
            "| "
            + " | ".join(
                [
                    state.scenario.label,
                    status,
                    str(state.attempts),
                    str(state.recoveries),
                    str(state.failure_iteration or ""),
                    state.failure_last_successful_operation or state.last_successful_operation,
                    state.failing_operation,
                    state.failure_kind,
                    evidence,
                ]
            )
            + " |"
        )
    failed = [state for state in states if not state.passed]
    showstoppers = [state for state in states if state.recoveries]
    recovered = [state for state in states if state.recoveries]
    selected_scenarios = getattr(args, "selected_scenarios", [])
    completed_labels = {state.scenario.label for state in states}
    not_run = [scenario.label for scenario in selected_scenarios if scenario.label not in completed_labels]
    lines.extend(["", "## Result", ""])
    if not failed and not showstoppers:
        lines.append("Full run passed: every selected scenario reached the required consecutive iterations.")
    else:
        lines.append("Full run failed or hit a show-stopping recovery.")
    if failed:
        lines.append("")
        lines.append("Failed scenarios:")
        for state in failed:
            lines.append(
                f"- {state.scenario.label}: iteration {state.failure_iteration}, "
                f"last successful operation "
                f"`{state.failure_last_successful_operation or state.last_successful_operation}`, "
                f"failing operation `{state.failing_operation}`, "
                f"liveness/failure kind `{state.failure_kind}`, evidence `{evidence_for(state)}`"
            )
    if showstoppers:
        lines.append("")
        lines.append("Show-stopping JTAG redeploys:")
        for state in showstoppers:
            lines.append(
                f"- {state.scenario.label}: {state.recoveries} redeploy(s). "
                f"First failing operation `{state.failing_operation}`, "
                f"kind `{state.failure_kind}`, evidence `{evidence_for(state)}`"
            )
    if recovered:
        lines.append("")
        lines.append("Scenarios requiring JTAG recovery:")
        for state in recovered:
            lines.append(
                f"- {state.scenario.label}: iteration {state.failure_iteration}, "
                f"last successful operation "
                f"`{state.failure_last_successful_operation or state.last_successful_operation}`, "
                f"failing operation `{state.failing_operation}`, "
                f"liveness/failure kind `{state.failure_kind}`, "
                f"recovery `{', '.join(state.recovery_outcomes)}`, "
                f"evidence `{evidence_for(state)}`"
            )
    if not_run:
        lines.append("")
        lines.append("Scenarios not run because the runner halted after an unrecovered device degradation:")
        for label in not_run:
            lines.append(f"- {label}")
    summary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    logger.log(
        "summary",
        None,
        0,
        "write summary",
        "summary.md contains scenario table and failures",
        str(summary),
        0,
        "pass" if not failed else "fail",
    )
    return 0 if not failed and not showstoppers else 1


def write_fatal_summary(
    logger: EventLogger,
    states: list[ScenarioState],
    args: argparse.Namespace,
    exc: BaseException,
) -> int:
    fatal_dir = logger.failures_dir / f"{datetime.now().strftime('%Y%m%d-%H%M%S')}-fatal-run-error"
    fatal_dir.mkdir(parents=True, exist_ok=True)
    (fatal_dir / "context.json").write_text(
        json.dumps(
            {
                "run_id": logger.run_id,
                "phase": "fatal",
                "exception": f"{type(exc).__name__}: {format_exception(exc)}",
                "completed_scenarios": [state.scenario.label for state in states],
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    (fatal_dir / "traceback.txt").write_text(
        "".join(traceback.format_exception(type(exc), exc, exc.__traceback__)),
        encoding="utf-8",
    )
    logger.log(
        "failure",
        None,
        0,
        "fatal run error",
        "unexpected top-level errors are captured before exit",
        str(fatal_dir),
        0,
        "fail",
    )
    fatal_state = ScenarioState(Scenario("fatal", "runner", "top-level"))
    fatal_state.attempts = 1
    fatal_state.failed = True
    fatal_state.failure_iteration = 0
    fatal_state.failing_operation = "top-level runner"
    fatal_state.failure_kind = "fatal_runner_error"
    fatal_state.failure_dir = fatal_dir
    fatal_state.failure_dirs.append(fatal_dir)
    fatal_state.notes.append(f"{type(exc).__name__}: {format_exception(exc)}")
    states.append(fatal_state)
    write_summary(logger, states, args)
    return 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("-H", "--host", default="u64")
    parser.add_argument("-p", "--port", type=int, default=23)
    parser.add_argument("--password", default=os.environ.get("U64_MONITOR_PASSWORD"))
    parser.add_argument("--timeout", type=float, default=12.0)
    parser.add_argument("-n", "--iterations", type=int, default=25)
    parser.add_argument(
        "--view",
        action="append",
        choices=VIEWS,
        help="View to run. Repeat to select multiple views.",
    )
    parser.add_argument(
        "--banking",
        action="append",
        choices=BANKINGS,
        help="Banking target to run. Repeat to select multiple targets.",
    )
    parser.add_argument(
        "--scenario",
        action="append",
        help="Focused scenario: view:banking or type:view:banking. Repeatable.",
    )
    parser.add_argument("--clean", dest="clean", action="store_true")
    parser.add_argument("--no-clean", dest="clean", action="store_false")
    parser.add_argument("--concurrency", dest="concurrency", action="store_true")
    parser.add_argument("--no-concurrency", dest="concurrency", action="store_false")
    parser.set_defaults(clean=True, concurrency=True)
    parser.add_argument(
        "--views",
        default=None,
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--bankings",
        default=None,
        help=argparse.SUPPRESS,
    )
    parser.add_argument("--include-clean", dest="clean", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument(
        "--include-concurrency",
        dest="concurrency",
        action="store_true",
        help=argparse.SUPPRESS,
    )
    parser.add_argument("--recover-jtag", dest="recover_jtag", action="store_true")
    parser.add_argument("--no-recover-jtag", dest="recover_jtag", action="store_false")
    parser.set_defaults(recover_jtag=True)
    parser.add_argument(
        "--recover-on",
        choices=RECOVER_POLICIES,
        default="any",
        help="When to redeploy with JTAG and restart the failed scenario.",
    )
    parser.add_argument("--max-recoveries", type=int, default=3)
    parser.add_argument("--jtag-deploy", default=str(REPO_ROOT / "tooling" / "build_and_deploy_u64.sh"))
    parser.add_argument("--syslog-file", default="/tmp/u64_syslog.log")
    parser.add_argument(
        "-o",
        "--log-root",
        "--output",
        type=Path,
        default=Path("doc/research/machine-monitor/debug/stabilization/6-final/soak-runs"),
    )
    parser.add_argument("--probe-window", type=float, default=15.0)
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument(
        "--continue-after-unrecovered-degradation",
        action="store_true",
        help="Diagnostic override: keep running later scenarios after a degraded device state remains unrecovered.",
    )
    args = parser.parse_args()
    if args.iterations < 1:
        parser.error("--iterations must be at least 1")
    if args.max_recoveries < 0:
        parser.error("--max-recoveries must be non-negative")
    return args


def main() -> int:
    args = parse_args()
    run_id = datetime.now().strftime("soak-%Y%m%d-%H%M%S")
    logger = EventLogger(args.log_root, run_id)
    syslog_proc: Optional[subprocess.Popen[bytes]] = None
    states: list[ScenarioState] = []
    try:
        logger.log("setup", None, 0, "run directory", "log directory exists", str(logger.log_dir), 0, "pass")
        syslog_proc = ensure_syslog_sink(logger, Path(args.syslog_file))
        scenarios = scenario_list(args)
        if not scenarios:
            raise SystemExit("no scenarios selected")
        args.selected_scenarios = scenarios
        write_run_manifest(logger, args, scenarios)
        preflight_recover_if_unreachable(logger, args)
        logger.log(
            "setup",
            None,
            0,
            "selected scenarios",
            "at least one scenario selected",
            ", ".join(s.label for s in scenarios),
            0,
            "pass",
        )
        for scenario in scenarios:
            state = run_scenario(args, logger, scenario)
            states.append(state)
            if args.fail_fast and not state.passed:
                break
            if (
                not state.passed
                and state.failure_kind in DEVICE_DEGRADATION_KINDS
                and not args.continue_after_unrecovered_degradation
            ):
                logger.log(
                    "summary",
                    scenario,
                    state.failure_iteration or 0,
                    "halt remaining scenarios",
                    "runner must not keep interacting with an unrecovered degraded device",
                    state.failure_kind,
                    0,
                    "fail",
                )
                break
        return write_summary(logger, states, args)
    except Exception as exc:
        return write_fatal_summary(logger, states, args, exc)
    finally:
        if syslog_proc is not None and syslog_proc.poll() is None:
            syslog_proc.terminate()
        logger.close()


if __name__ == "__main__":
    raise SystemExit(main())
