#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import math
import os
import random
import sys
import threading
import time
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Callable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import u64_ftp  # noqa: E402
import u64_http  # noqa: E402
import u64_ident  # noqa: E402
import u64_modem  # noqa: E402
import u64_ping  # noqa: E402
import u64_dma  # noqa: E402
import u64_stream  # noqa: E402
import u64_telnet  # noqa: E402
from u64_connection_runtime import (  # noqa: E402
    ProbeCorrectness,
    ProbeExecutionContext,
    ProbeOutcome,
    ProbeSurface,
    RuntimeSettings,
)


HOST = os.getenv("HOST", "192.168.1.13")
HTTP_PATH = os.getenv("HTTP_PATH", "v1/version")
HTTP_PORT = int(os.getenv("HTTP_PORT", "80"))
TELNET_PORT = int(os.getenv("TELNET_PORT", "23"))
FTP_PORT = int(os.getenv("FTP_PORT", "21"))
FTP_USER = os.getenv("FTP_USER", "anonymous")
FTP_PASS = os.getenv("FTP_PASS", "")
NETWORK_PASSWORD = os.getenv("NETWORK_PASSWORD", "")
MODEM_PORT = int(os.getenv("MODEM_PORT", "3000"))
INTER_CALL_DELAY_MS = int(os.getenv("INTER_CALL_DELAY_MS", "0"))
LOG_EVERY_N_ITERATIONS = int(os.getenv("LOG_EVERY_N_ITERATIONS", "10"))
VERBOSE = os.getenv("VERBOSE", "").strip().lower() not in {"", "0", "false", "no"}
LEGACY_PROBE_ALIASES = {"raw64": "dma"}
DEFAULT_PROBES = ("ping", "ident", "dma", "telnet", "ftp", "http")
OPTIONAL_PROBES = ("modem",)
PROBE_CHOICES = DEFAULT_PROBES + OPTIONAL_PROBES
DEFAULT_SCHEDULE = "sequential"
DEFAULT_RUNNERS = 1
DEFAULT_PROFILE_HOST = "u64"
DEFAULT_PROFILE_DURATION_S = 120
DEFAULT_SOAK_DURATION_S = 12 * 60 * 60
PROFILE_SOAK = "soak"
PROFILE_STRESS = "stress"
SCHEDULE_SEQUENTIAL = "sequential"
SCHEDULE_CONCURRENT = "concurrent"
CONNECTION_STREAM_CHOICES = (u64_stream.StreamKind.AUDIO, u64_stream.StreamKind.VIDEO)
DEFAULT_CONNECTION_STREAMS = tuple(kind.value for kind in CONNECTION_STREAM_CHOICES)
PROBE_RANDOM_SEED_ENV = "VIVIPI_CONNECTION_TEST_RANDOM_SEED"

PROBE_SURFACE_CHOICES = {
    "ping": (ProbeSurface.SMOKE,),
    "ident": (ProbeSurface.SMOKE,),
    "dma": (ProbeSurface.SMOKE, ProbeSurface.READ, ProbeSurface.READWRITE),
    "telnet": (ProbeSurface.SMOKE, ProbeSurface.READ, ProbeSurface.READWRITE),
    "ftp": (ProbeSurface.SMOKE, ProbeSurface.READ, ProbeSurface.READWRITE),
    "http": (ProbeSurface.SMOKE, ProbeSurface.READ, ProbeSurface.READWRITE),
    "modem": (ProbeSurface.SMOKE,),
}
PROBE_SURFACE_ORDER = (ProbeSurface.SMOKE, ProbeSurface.READ, ProbeSurface.READWRITE)
# Degradation ladder used by _fallback_correctness to pick the "nearest supported
# lower" mode when a globally requested --mode is not available for a protocol.
# VANISH is deliberately excluded: it is a telnet-only reap lane, not a point on
# the correctness continuum. Including it here made `--mode vanish` degrade every
# other protocol (ftp -> INVALID, http -> INCOMPLETE) as a side effect. Keeping it
# out means a VANISH request falls back to the safe default for non-telnet probes.
PROBE_CORRECTNESS_ORDER = (
    ProbeCorrectness.COMPLETE,
    ProbeCorrectness.OPEN,
    ProbeCorrectness.INCOMPLETE,
    ProbeCorrectness.INVALID,
)
PROBE_CORRECTNESS_CHOICES = {
    "ping": (ProbeCorrectness.COMPLETE,),
    "ident": (ProbeCorrectness.COMPLETE,),
    "dma": (ProbeCorrectness.COMPLETE,),
    "telnet": (ProbeCorrectness.COMPLETE, ProbeCorrectness.OPEN, ProbeCorrectness.INCOMPLETE, ProbeCorrectness.VANISH),
    "ftp": (ProbeCorrectness.COMPLETE, ProbeCorrectness.OPEN, ProbeCorrectness.INCOMPLETE, ProbeCorrectness.INVALID),
    "http": (ProbeCorrectness.COMPLETE, ProbeCorrectness.INCOMPLETE),
    "modem": (ProbeCorrectness.COMPLETE,),
}
CORRECTNESS_DEGRADATION_HELP = (
    "Correctness degradation: "
    f"{ProbeCorrectness.COMPLETE.value} (finish and close cleanly), "
    f"{ProbeCorrectness.OPEN.value} (finish and skip orderly teardown), "
    f"{ProbeCorrectness.INCOMPLETE.value} (abort before completion), "
    f"{ProbeCorrectness.INVALID.value} (send malformed or unsupported input)."
)
HISTORICAL_CORRECTNESS_EVIDENCE = {
    "ftp": {
        ProbeCorrectness.OPEN.value: {
            "commit": "f0ef5db",
            "path": "scripts/u64_ftp.py",
            "summary": "Former driver-side incomplete FTP probing reused the full surface operations, then dropped the session without an orderly QUIT.",
        },
        ProbeCorrectness.INCOMPLETE.value: {
            "commit": "37314b1",
            "path": "src/vivipi/runtime/checks.py",
            "summary": "Historical Pico-side FTP probe verified only the greeting path, then sent QUIT without login, PWD, PASV, or NLST.",
        }
    },
    "telnet": {
        ProbeCorrectness.OPEN.value: {
            "commit": "f0ef5db",
            "path": "scripts/u64_telnet.py",
            "summary": "Former driver-side incomplete Telnet probing reused the full surface operations and accepted abrupt disconnects after the completed interaction.",
        },
        ProbeCorrectness.INCOMPLETE.value: {
            "commit": "37314b1",
            "path": "src/vivipi/runtime/checks.py",
            "summary": "Historical Pico-side Telnet runner performed a single initial read and classified login failure, banner-ready output, or a quiet connected session.",
        }
    },
}

PROBE_RUNNERS = {
    "ping": u64_ping.run_probe,
    "ident": u64_ident.run_probe,
    "dma": u64_dma.run_probe,
    "telnet": u64_telnet.run_probe,
    "ftp": u64_ftp.run_probe,
    "http": u64_http.run_probe,
    "modem": u64_modem.run_probe,
}


def default_probe_random_seed() -> int:
    raw_value = os.getenv(PROBE_RANDOM_SEED_ENV, "").strip()
    if raw_value:
        return int(raw_value, 0)
    return int.from_bytes(os.urandom(8), "big")


@dataclass(frozen=True)
class ExecutionConfig:
    profile: str | None
    probes: tuple[str, ...]
    schedule: str
    runners: int
    duration_s: int | None
    probe_correctness: dict[str, ProbeCorrectness]
    uses_extended_flags: bool
    overrides: tuple[str, ...]
    probe_surfaces: dict[str, ProbeSurface] = field(default_factory=lambda: {protocol: PROBE_SURFACE_CHOICES[protocol][0] for protocol in PROBE_CHOICES})
    streams: tuple[str, ...] = ()


@dataclass
class ExecutionState:
    settings: RuntimeSettings
    include_runner_context: bool
    runner_count: int = 1
    summary_protocols: tuple[str, ...] = DEFAULT_PROBES
    random_seed: int = field(default_factory=default_probe_random_seed)
    latency_samples: dict[str, list[float]] = field(default_factory=dict)
    sample_lock: threading.Lock = field(default_factory=threading.Lock)
    output_lock: threading.Lock = field(default_factory=threading.Lock)
    probe_selection_lock: threading.Lock = field(default_factory=threading.Lock)
    probe_operation_counts: dict[tuple[int, str, str], int] = field(default_factory=dict)
    failure_count: int = 0
    shared_resource_registry_lock: threading.Lock = field(default_factory=threading.Lock)
    shared_resource_locks: dict[str, threading.Lock] = field(default_factory=dict)
    shared_resource_values: dict[str, object] = field(default_factory=dict)
    stream_monitor: u64_stream.StreamMonitor | None = None

    def _derived_seed(self, *parts: object) -> int:
        digest = hashlib.blake2b(digest_size=16)
        digest.update(str(self.random_seed).encode("utf-8"))
        for part in parts:
            digest.update(b"\0")
            digest.update(str(part).encode("utf-8"))
        return int.from_bytes(digest.digest(), "big")

    def probe_iteration_sequence(self, probes: tuple[str, ...], runner_id: int, iteration: int) -> tuple[tuple[int, str], ...]:
        indexed_probes = list(enumerate(probes))
        if len(indexed_probes) < 2:
            return tuple(indexed_probes)
        order = list(range(len(indexed_probes)))
        random.Random(self._derived_seed("probe-sequence", iteration, len(indexed_probes), ",".join(probes))).shuffle(order)
        if self.runner_count > 1:
            offset = (runner_id - 1) % len(order)
            order = order[offset:] + order[:offset]
        return tuple(indexed_probes[position] for position in order)

    def shared_resource_lock_for(self, resource_key: str) -> threading.Lock:
        with self.shared_resource_registry_lock:
            existing = self.shared_resource_locks.get(resource_key)
            if existing is not None:
                return existing
            created = threading.Lock()
            self.shared_resource_locks[resource_key] = created
            return created

    def get_shared_resource_value(self, resource_key: str) -> object | None:
        with self.shared_resource_registry_lock:
            return self.shared_resource_values.get(resource_key)

    def set_shared_resource_value(self, resource_key: str, value: object) -> None:
        with self.shared_resource_registry_lock:
            self.shared_resource_values[resource_key] = value

    def record_latency(self, protocol: str, elapsed_ms: float) -> None:
        with self.sample_lock:
            self.latency_samples.setdefault(protocol, []).append(elapsed_ms)

    def percentile_ms(self, protocol: str, percentile: int) -> int:
        with self.sample_lock:
            samples = tuple(self.latency_samples.get(protocol, ()))
        if not samples:
            return 0
        ordered = sorted(samples)
        rank = max(1, math.ceil(percentile / 100.0 * len(ordered)))
        return int(round(ordered[rank - 1]))

    def next_probe_operation_index(self, protocol: str, runner_id: int, surface: ProbeSurface, pool_size: int) -> int:
        if pool_size < 1:
            raise ValueError("pool_size must be >= 1")
        key = (runner_id, protocol, surface.value)
        with self.probe_selection_lock:
            counter = self.probe_operation_counts.get(key, 0)
            self.probe_operation_counts[key] = counter + 1
        cycle = counter // pool_size
        position = counter % pool_size
        permutation = list(range(pool_size))
        random.Random(self._derived_seed("probe-operation", protocol, surface.value, pool_size, cycle)).shuffle(permutation)
        if self.runner_count > 1:
            position = (position + runner_id - 1) % pool_size
        return permutation[position]

    def emit_log(self, protocol: str, result: str, detail: str, *, iteration: int, runner_id: int) -> None:
        if result != "FAIL" and not self.settings.verbose and self.settings.log_every > 1 and iteration % self.settings.log_every != 0:
            return
        if self.include_runner_context:
            detail = f"runner={runner_id} iteration={iteration} {detail}"
        with self.output_lock:
            try:
                print(f'{ts()} protocol={protocol} result={result} detail="{detail.replace(chr(34), chr(39))}"', flush=True)
            except BrokenPipeError:
                raise SystemExit(0)

    def emit_probe_outcome(self, protocol: str, outcome: ProbeOutcome, *, iteration: int, runner_id: int) -> None:
        if outcome.result == "FAIL":
            with self.sample_lock:
                self.failure_count += 1
        self.record_latency(protocol, outcome.elapsed_ms)
        self.emit_log(protocol, outcome.result, f"{outcome.detail} latency_ms={int(round(outcome.elapsed_ms))}", iteration=iteration, runner_id=runner_id)

    def emit_iteration_summary(self, started_at: float, iteration: int, runner_id: int) -> None:
        if not self.settings.verbose and self.settings.log_every > 1 and iteration % self.settings.log_every != 0:
            return
        parts = [f"iteration={iteration}", f"runtime_s={int(time.time() - started_at)}", f"host={self.settings.host}"]
        if self.include_runner_context:
            parts.insert(0, f"runner={runner_id}")
        for protocol in self.summary_protocols:
            parts.append(f"{protocol}_median_ms={self.percentile_ms(protocol, 50)}")
            parts.append(f"{protocol}_p90_ms={self.percentile_ms(protocol, 90)}")
            parts.append(f"{protocol}_p99_ms={self.percentile_ms(protocol, 99)}")
        if self.stream_monitor is not None:
            parts.extend(u64_stream.stream_summary_parts(self.stream_monitor.snapshots()))
        with self.output_lock:
            try:
                print(f'{ts()} protocol=iteration result=INFO detail="{' '.join(parts)}"', flush=True)
            except BrokenPipeError:
                raise SystemExit(0)


def ts() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def log(protocol: str, result: str, detail: str) -> None:
    try:
        print(f'{ts()} protocol={protocol} result={result} detail="{detail.replace(chr(34), chr(39))}"', flush=True)
    except BrokenPipeError:
        raise SystemExit(0)


def parse_probes(value: str) -> tuple[str, ...]:
    raw_value = value.strip()
    if not raw_value:
        raise argparse.ArgumentTypeError("--probes must be a non-empty comma-separated list")
    probes = tuple(LEGACY_PROBE_ALIASES.get(part.strip(), part.strip()) for part in raw_value.split(","))
    if any(not probe for probe in probes):
        raise argparse.ArgumentTypeError("--probes must not contain empty entries")
    invalid = [probe for probe in probes if probe not in PROBE_CHOICES]
    if invalid:
        raise argparse.ArgumentTypeError(f"unknown probe name(s): {', '.join(sorted(set(invalid)))}")
    if len(set(probes)) != len(probes):
        raise argparse.ArgumentTypeError("--probes must not contain duplicates")
    return probes


def parse_runners(value: str) -> int:
    try:
        runners = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("--runners must be an integer >= 1") from error
    if runners < 1:
        raise argparse.ArgumentTypeError("--runners must be >= 1")
    return runners


def parse_duration_s(value: str) -> int:
    try:
        duration_s = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("--duration-s must be an integer >= 1") from error
    if duration_s < 1:
        raise argparse.ArgumentTypeError("--duration-s must be >= 1")
    return duration_s


def parse_session_slots(value: str) -> int:
    try:
        slots = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("--session-slots must be an integer >= 1") from error
    if slots < 1:
        raise argparse.ArgumentTypeError("--session-slots must be >= 1")
    return slots


def parse_reap_timeout_s(value: str) -> float:
    try:
        reap_timeout_s = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("--reap-timeout-s must be a number > 0") from error
    if reap_timeout_s <= 0:
        raise argparse.ArgumentTypeError("--reap-timeout-s must be > 0")
    return reap_timeout_s


def _fallback_surface(protocol: str, requested: ProbeSurface) -> ProbeSurface:
    available = PROBE_SURFACE_CHOICES[protocol]
    if requested in available:
        return requested
    requested_index = PROBE_SURFACE_ORDER.index(requested)
    for candidate in reversed(PROBE_SURFACE_ORDER[: requested_index + 1]):
        if candidate in available:
            return candidate
    return available[0]


def _fallback_correctness(protocol: str, requested: ProbeCorrectness) -> ProbeCorrectness:
    available = PROBE_CORRECTNESS_CHOICES[protocol]
    if requested in available:
        return requested
    if requested not in PROBE_CORRECTNESS_ORDER:
        # A protocol-specific mode (e.g. the telnet-only VANISH reap lane) was
        # requested globally for a protocol that does not support it. Fall back to
        # the safe default rather than silently degrading this protocol.
        return available[0]
    requested_index = PROBE_CORRECTNESS_ORDER.index(requested)
    for candidate in reversed(PROBE_CORRECTNESS_ORDER[: requested_index + 1]):
        if candidate in available:
            return candidate
    return available[0]


def profile_overrides_help() -> str:
    return (
        "Profile precedence: if --profile is supplied, explicit --probes, --schedule, --runners, --surface, --mode, --*-surface, and --*-mode values override the profile.\n\n"
        f"{CORRECTNESS_DEGRADATION_HELP}\n\n"
        "Examples:\n"
        "  ./u64_connection_test.py\n"
        "  ./u64_connection_test.py --profile soak\n"
        "  ./u64_connection_test.py --profile stress\n"
        "  ./u64_connection_test.py --profile soak --duration-s 300\n"
        f"  ./u64_connection_test.py --surface {ProbeSurface.READWRITE.value}\n"
        f"  ./u64_connection_test.py --mode {ProbeCorrectness.OPEN.value}\n"
        f"  ./u64_connection_test.py --mode {ProbeCorrectness.INCOMPLETE.value}\n"
        "  ./u64_connection_test.py --probes ping,ident,dma,telnet,ftp,http\n"
        "  ./u64_connection_test.py --schedule concurrent --runners 3\n"
        f"  ./u64_connection_test.py --http-surface {ProbeSurface.READ.value}\n"
        f"  ./u64_connection_test.py --ftp-surface {ProbeSurface.READWRITE.value} --telnet-surface {ProbeSurface.READ.value}\n"
        f"  ./u64_connection_test.py --telnet-mode {ProbeCorrectness.INCOMPLETE.value}\n"
        f"  ./u64_connection_test.py --schedule concurrent --runners 2 --ftp-mode {ProbeCorrectness.INVALID.value} --telnet-mode {ProbeCorrectness.INCOMPLETE.value}\n"
        "  ./u64_connection_test.py --profile stress --runners 4\n"
        "  ./u64_connection_test.py --profile soak --probes ping,http"
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Repeated U64 connectivity checks. Default: 12h soak with concurrent readwrite probes and audio+video streams. "
            "ident targets UDP port 64 JSON discovery; dma targets the DMA-capable TCP port 64 command endpoint."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=profile_overrides_help(),
    )
    parser.add_argument("-H", "--host", default=HOST, help="Target host or IP")
    parser.add_argument("-d", "--delay-ms", type=int, default=INTER_CALL_DELAY_MS, help="Delay between checks in milliseconds")
    parser.add_argument("-n", "--log-every", type=int, default=LOG_EVERY_N_ITERATIONS, help="Log every Nth successful iteration")
    parser.add_argument("-u", "--ftp-user", default=FTP_USER, help="FTP username")
    parser.add_argument("-P", "--ftp-pass", default=FTP_PASS, help="Legacy alias for the shared device network password.")
    parser.add_argument("--network-password", default=NETWORK_PASSWORD, help="Shared device network password used for HTTP, Telnet, FTP, and dma.")
    parser.add_argument("--http-path", default=HTTP_PATH, help="HTTP path")
    parser.add_argument("--http-port", type=int, default=HTTP_PORT, help="HTTP port")
    parser.add_argument("--ftp-port", type=int, default=FTP_PORT, help="FTP port")
    parser.add_argument("--telnet-port", type=int, default=TELNET_PORT, help="Telnet port")
    parser.add_argument("--modem-port", type=int, default=MODEM_PORT, help="Optional modem listener port")
    parser.add_argument("-v", "--verbose", action="store_true", default=VERBOSE, help="Log every successful check")
    parser.add_argument(
        "--profile",
        choices=(PROFILE_SOAK, PROFILE_STRESS),
        default=None,
        help="Preset profile. Explicit --probes, --schedule, --runners, --surface, --mode, --*-surface, and --*-mode flags override the profile.",
    )
    parser.add_argument(
        "--probes",
        type=parse_probes,
        default=None,
        help=(
            "Ordered non-empty comma-separated probe list using ping,ident,dma,telnet,ftp,http,modem; "
            "ident is UDP/64 and dma is TCP/64. When set without --stream, profile-default streams are disabled."
        ),
    )
    parser.add_argument("--schedule", choices=(SCHEDULE_SEQUENTIAL, SCHEDULE_CONCURRENT), default=None, help="Per-runner scheduling mode.")
    parser.add_argument("--runners", type=parse_runners, default=None, help="Logical runner count >= 1.")
    parser.add_argument("--duration-s", type=parse_duration_s, default=None, help="Optional total run duration in seconds. Soak defaults to 43200 (12h); stress defaults to 120.")
    parser.add_argument("--surface", choices=[value.value for value in ProbeSurface], default=None, help="Apply the same surface to all probes, falling back per protocol to the nearest supported lower surface.")
    parser.add_argument("--mode", choices=[value.value for value in ProbeCorrectness], default=None, help=f"Apply the same correctness mode to all probes, falling back per protocol to the nearest supported lower mode. {CORRECTNESS_DEGRADATION_HELP}")
    parser.add_argument("--http-surface", choices=[value.value for value in ProbeSurface], default=None, help="HTTP probe surface.")
    parser.add_argument("--ftp-surface", choices=[value.value for value in ProbeSurface], default=None, help="FTP probe surface.")
    parser.add_argument("--telnet-surface", choices=[value.value for value in ProbeSurface], default=None, help="Telnet probe surface.")
    parser.add_argument("--dma-surface", dest="dma_surface", choices=[value.value for value in ProbeSurface], default=None, help="DMA TCP/64 probe surface.")
    parser.add_argument("--raw64-surface", dest="dma_surface", choices=[value.value for value in ProbeSurface], help=argparse.SUPPRESS)
    parser.add_argument("--ping-mode", choices=[value.value for value in ProbeCorrectness], default=None, help="Ping probe correctness.")
    parser.add_argument("--http-mode", choices=[value.value for value in ProbeCorrectness], default=None, help="HTTP probe correctness.")
    parser.add_argument("--ftp-mode", choices=[value.value for value in ProbeCorrectness], default=None, help="FTP probe correctness.")
    parser.add_argument("--telnet-mode", choices=[value.value for value in ProbeCorrectness], default=None, help="Telnet probe correctness.")
    parser.add_argument("--iface", default=os.environ.get("C64_LAN_IFACE"),
                        help="LAN interface to add the throwaway victim IP on for --telnet-mode vanish.")
    parser.add_argument("--victim-ip", default=os.environ.get("C64_VICTIM_IP"),
                        help="Unused LAN IP to source the vanishing half-open connections from (--telnet-mode vanish).")
    parser.add_argument("--session-slots", type=parse_session_slots, default=4,
                        help="Telnet session slots to fill for the half-open lane (= firmware TELNET_MAX_SESSIONS).")
    parser.add_argument("--reap-timeout-s", type=parse_reap_timeout_s, default=75.0,
                        help="Seconds to wait for keepalive reaping before the half-open lane declares RED.")
    parser.add_argument(
        "--stream",
        nargs="*",
        choices=[kind.value for kind in CONNECTION_STREAM_CHOICES],
        default=None,
        help="Verify audio, video, or both UDP streams. Omit values to select both audio and video.",
    )
    return parser


def sleep_ms(value: int) -> None:
    if value > 0:
        time.sleep(value / 1000.0)


def build_runtime_settings(args: argparse.Namespace) -> RuntimeSettings:
    shared_network_password = args.network_password or args.ftp_pass
    return RuntimeSettings(
        host=args.host,
        http_path=args.http_path,
        http_port=args.http_port,
        telnet_port=args.telnet_port,
        ftp_port=args.ftp_port,
        ftp_user=args.ftp_user,
        ftp_pass=shared_network_password,
        delay_ms=args.delay_ms,
        log_every=args.log_every,
        verbose=args.verbose,
        network_password=shared_network_password,
        modem_port=args.modem_port,
        lan_iface=args.iface or "",
        victim_ip=args.victim_ip or "",
        session_slots=args.session_slots,
        reap_timeout_s=args.reap_timeout_s,
    )


def default_execution_config() -> ExecutionConfig:
    return profile_execution_config(PROFILE_SOAK)


def _profile_default_probe_surfaces() -> dict[str, ProbeSurface]:
    return {protocol: PROBE_SURFACE_CHOICES[protocol][-1] for protocol in PROBE_CHOICES}


def profile_execution_config(profile: str) -> ExecutionConfig:
    if profile == PROFILE_SOAK:
        return ExecutionConfig(
            profile=PROFILE_SOAK,
            probes=DEFAULT_PROBES,
            schedule=SCHEDULE_CONCURRENT,
            runners=1,
            duration_s=DEFAULT_SOAK_DURATION_S,
            probe_correctness={protocol: PROBE_CORRECTNESS_CHOICES[protocol][0] for protocol in PROBE_CHOICES},
            uses_extended_flags=True,
            overrides=(),
            probe_surfaces=_profile_default_probe_surfaces(),
            streams=DEFAULT_CONNECTION_STREAMS,
        )
    if profile == PROFILE_STRESS:
        return ExecutionConfig(
            profile=PROFILE_STRESS,
            probes=("dma", "ftp", "telnet", "http", "ftp", "telnet", "ping", "ident"),
            schedule=SCHEDULE_CONCURRENT,
            runners=5,
            duration_s=DEFAULT_PROFILE_DURATION_S,
            probe_correctness={
                "ping": ProbeCorrectness.COMPLETE,
                "ident": ProbeCorrectness.COMPLETE,
                "dma": ProbeCorrectness.COMPLETE,
                "telnet": ProbeCorrectness.INCOMPLETE,
                "ftp": ProbeCorrectness.INCOMPLETE,
                "http": ProbeCorrectness.COMPLETE,
                "modem": ProbeCorrectness.COMPLETE,
            },
            uses_extended_flags=True,
            overrides=(),
            probe_surfaces=_profile_default_probe_surfaces(),
            streams=(),
        )
    raise ValueError(f"unsupported profile: {profile}")


def parse_connection_stream_selection(values: tuple[str, ...] | list[str]) -> tuple[str, ...]:
    if not values:
        return DEFAULT_CONNECTION_STREAMS
    seen: set[str] = set()
    ordered: list[str] = []
    for raw_value in values:
        if raw_value in seen:
            continue
        seen.add(raw_value)
        ordered.append(raw_value)
    return tuple(ordered)


def resolve_execution_config(args: argparse.Namespace) -> ExecutionConfig:
    resolved = default_execution_config() if args.profile is None else profile_execution_config(args.profile)
    probe_correctness = dict(resolved.probe_correctness)
    probe_surfaces = dict(resolved.probe_surfaces)
    streams = tuple(resolved.streams)
    overrides: list[str] = []

    if args.probes is not None:
        resolved = replace(resolved, probes=args.probes)
        if args.stream is None:
            streams = ()
        overrides.append("probes")
    if args.schedule is not None:
        resolved = replace(resolved, schedule=args.schedule)
        overrides.append("schedule")
    if args.runners is not None:
        resolved = replace(resolved, runners=args.runners)
        overrides.append("runners")
    if args.duration_s is not None:
        resolved = replace(resolved, duration_s=args.duration_s)
        overrides.append("duration-s")
    if args.surface is not None:
        requested_surface = ProbeSurface(args.surface)
        for protocol in PROBE_CHOICES:
            probe_surfaces[protocol] = _fallback_surface(protocol, requested_surface)
        overrides.append("surface")
    if args.mode is not None:
        requested_mode = ProbeCorrectness(args.mode)
        for protocol in PROBE_CHOICES:
            probe_correctness[protocol] = _fallback_correctness(protocol, requested_mode)
        overrides.append("mode")

    for protocol, raw_value in {"http": args.http_surface, "ftp": args.ftp_surface, "telnet": args.telnet_surface, "dma": args.dma_surface}.items():
        if raw_value is None:
            continue
        probe_surfaces[protocol] = _fallback_surface(protocol, ProbeSurface(raw_value))
        overrides.append(f"{protocol}-surface")

    for protocol, raw_value in {"ping": args.ping_mode, "http": args.http_mode, "ftp": args.ftp_mode, "telnet": args.telnet_mode}.items():
        if raw_value is None:
            continue
        probe_correctness[protocol] = _fallback_correctness(protocol, ProbeCorrectness(raw_value))
        overrides.append(f"{protocol}-mode")

    if args.stream is not None:
        streams = parse_connection_stream_selection(args.stream)
        overrides.append("stream")

    return ExecutionConfig(
        profile=resolved.profile,
        probes=resolved.probes,
        schedule=resolved.schedule,
        runners=resolved.runners,
        duration_s=resolved.duration_s,
        probe_correctness=probe_correctness,
        uses_extended_flags=True,
        overrides=tuple(overrides),
        probe_surfaces=probe_surfaces,
        streams=streams,
    )


def validate_execution_config(config: ExecutionConfig) -> None:
    if ProbeCorrectness.VANISH in config.probe_correctness.values():
        # The vanish reap lane fills and monopolises the whole telnet session
        # table for one fill/vanish/recover cycle and mutates a shared victim IP
        # alias. Any concurrency (multiple runner threads, or the per-probe
        # threads spawned by the concurrent schedule) races on that table and the
        # alias, producing misleading EEXIST/"could not saturate" failures. Require
        # a single runner and sequential scheduling so it runs alone.
        if config.runners != 1 or config.schedule != SCHEDULE_SEQUENTIAL:
            raise ValueError(
                "telnet vanish mode monopolises the whole telnet session table and "
                "cannot run with concurrency; use --runners 1 --schedule sequential"
            )
    if config.schedule != SCHEDULE_CONCURRENT:
        return
    if config.runners <= u64_http.PROBE_WRITE_RUNNER_SLOT_COUNT:
        return
    if config.probe_surfaces.get("http") != ProbeSurface.READWRITE:
        return
    raise ValueError(
        "concurrent HTTP readwrite probing supports at most "
        f"{u64_http.PROBE_WRITE_RUNNER_SLOT_COUNT} runners; got {config.runners}"
    )


def log_resolved_startup(settings: RuntimeSettings, config: ExecutionConfig) -> None:
    profile_name = config.profile or "custom"
    parts = [
        f"host={settings.host}",
        f"http={settings.http_port}/{settings.http_path}",
        f"telnet={settings.telnet_port}",
        f"ftp={settings.ftp_port}",
        f"user={settings.ftp_user}",
        f"sample_every={settings.log_every}",
        f"verbose={int(settings.verbose)}",
        f"call_gap_ms={settings.delay_ms}",
        f"profile={profile_name}",
        f"probes={','.join(config.probes)}",
        f"schedule={config.schedule}",
        f"runners={config.runners}",
        f"duration_s={config.duration_s if config.duration_s is not None else 'infinite'}",
        "surfaces=" + ",".join(f"{protocol}:{config.probe_surfaces[protocol].value}" for protocol in config.probes),
        "correctness=" + ",".join(f"{protocol}:{config.probe_correctness[protocol].value}" for protocol in config.probes),
        f"streams={','.join(config.streams) if config.streams else 'off'}",
        f"network_password_set={int(bool(settings.network_password))}",
        f"modem_port={settings.modem_port}",
    ]
    if config.profile is not None and config.overrides:
        parts.append(f"overrides={','.join(config.overrides)}")
    try:
        print(f'{ts()} protocol=config result=INFO detail="{' '.join(parts)}"', flush=True)
    except BrokenPipeError:
        raise SystemExit(0)


def execute_probe(
    protocol: str,
    settings: RuntimeSettings,
    config: ExecutionConfig,
    probe_runners: dict[str, Callable] | None = None,
    *,
    state: ExecutionState | None = None,
    runner_id: int = 1,
    iteration: int = 1,
) -> ProbeOutcome:
    runners = PROBE_RUNNERS if probe_runners is None else probe_runners
    context = ProbeExecutionContext(
        protocol=protocol,
        runner_id=runner_id,
        iteration=iteration,
        surface=config.probe_surfaces.get(protocol, ProbeSurface.SMOKE),
        state=state,
    )
    return runners[protocol](settings, config.probe_correctness[protocol], context=context)


def execute_probe_safely(
    protocol: str,
    settings: RuntimeSettings,
    config: ExecutionConfig,
    probe_runners: dict[str, Callable] | None = None,
    *,
    state: ExecutionState | None = None,
    runner_id: int = 1,
    iteration: int = 1,
) -> ProbeOutcome:
    started_at = time.perf_counter_ns()
    try:
        return execute_probe(protocol, settings, config, probe_runners, state=state, runner_id=runner_id, iteration=iteration)
    except Exception as error:
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("FAIL", f"{protocol} failed: {error}", elapsed_ms)


def ordered_probe_reports(
    results: tuple[tuple[int, str, ProbeOutcome], ...],
) -> tuple[tuple[str, ProbeOutcome], ...]:
    protocol_rank = {protocol: index for index, protocol in enumerate(PROBE_CHOICES)}
    ordered = sorted(
        results,
        key=lambda item: (protocol_rank.get(item[1], len(PROBE_CHOICES)), item[0]),
    )
    return tuple((protocol, outcome) for _index, protocol, outcome in ordered)


def run_runner_iteration(
    runner_id: int,
    iteration: int,
    config: ExecutionConfig,
    settings: RuntimeSettings,
    state: ExecutionState,
    *,
    sleep_fn=sleep_ms,
    probe_runners: dict[str, Callable] | None = None,
) -> tuple[tuple[str, ProbeOutcome], ...]:
    sequence = state.probe_iteration_sequence(config.probes, runner_id, iteration)
    if config.schedule == SCHEDULE_SEQUENTIAL:
        results: list[tuple[int, str, ProbeOutcome]] = []
        for sequence_index, (original_index, protocol) in enumerate(sequence):
            outcome = execute_probe_safely(protocol, settings, config, probe_runners, state=state, runner_id=runner_id, iteration=iteration)
            results.append((original_index, protocol, outcome))
            if sequence_index < len(sequence) - 1:
                sleep_fn(settings.delay_ms)
        reported_results = ordered_probe_reports(tuple(results))
        for protocol, outcome in reported_results:
            state.emit_probe_outcome(protocol, outcome, iteration=iteration, runner_id=runner_id)
        return reported_results

    ordered_results: list[tuple[int, str, ProbeOutcome] | None] = [None] * len(config.probes)
    threads: list[threading.Thread] = []

    def worker(index: int, protocol: str) -> None:
        ordered_results[index] = (
            index,
            protocol,
            execute_probe_safely(protocol, settings, config, probe_runners, state=state, runner_id=runner_id, iteration=iteration),
        )

    for index, protocol in sequence:
        thread = threading.Thread(target=worker, args=(index, protocol), daemon=False)
        thread.start()
        threads.append(thread)
    for thread in threads:
        thread.join()
    results = tuple(ordered_results[index] for index, _protocol in sequence if ordered_results[index] is not None)
    reported_results = ordered_probe_reports(results)
    for protocol, outcome in reported_results:
        state.emit_probe_outcome(protocol, outcome, iteration=iteration, runner_id=runner_id)
    return reported_results


def run_runner_loop(
    runner_id: int,
    config: ExecutionConfig,
    settings: RuntimeSettings,
    state: ExecutionState,
    stop_event: threading.Event,
    *,
    sleep_fn=sleep_ms,
    probe_runners: dict[str, Callable] | None = None,
    max_iterations: int | None = None,
    deadline_s: float | None = None,
) -> int:
    started_at = time.time()
    iteration = 0
    while not stop_event.is_set():
        if deadline_s is not None and time.monotonic() >= deadline_s:
            stop_event.set()
            return iteration
        iteration += 1
        run_runner_iteration(runner_id, iteration, config, settings, state, sleep_fn=sleep_fn, probe_runners=probe_runners)
        state.emit_iteration_summary(started_at, iteration, runner_id)
        if max_iterations is not None and iteration >= max_iterations:
            return iteration
        if deadline_s is not None and time.monotonic() >= deadline_s:
            stop_event.set()
            return iteration
        sleep_fn(settings.delay_ms)
    return iteration


def run_extended(config: ExecutionConfig, settings: RuntimeSettings) -> int:
    should_prime_ftp_temp_dir = "ftp" in config.probes and config.probe_surfaces.get("ftp", ProbeSurface.SMOKE) in (
        ProbeSurface.READ,
        ProbeSurface.READWRITE,
    )
    state = ExecutionState(settings=settings, include_runner_context=True, runner_count=config.runners, summary_protocols=tuple(dict.fromkeys(config.probes)))
    stop_event = threading.Event()
    deadline_s = None if config.duration_s is None else time.monotonic() + config.duration_s

    def emit_stream_log(protocol: str, result: str, detail: str) -> None:
        del protocol
        with state.output_lock:
            try:
                print(f'{ts()} protocol=stream result={result} detail="{detail.replace(chr(34), chr(39))}"', flush=True)
            except BrokenPipeError:
                raise SystemExit(0)

    stream_monitor = None
    if config.streams:
        stream_monitor = u64_stream.StreamMonitor(
            u64_stream.StreamRuntimeSettings(host=settings.host, network_password=settings.network_password),
            tuple(u64_stream.StreamKind(stream) for stream in config.streams),
            logger=emit_stream_log,
        )
        state.stream_monitor = stream_monitor

    result = 0
    try:
        if stream_monitor is not None:
            stream_monitor.start()
        if should_prime_ftp_temp_dir:
            u64_ftp.try_prime_temp_dir(settings, log_fn=lambda detail: log("ftp", "INFO", detail))
        if config.runners == 1:
            run_runner_loop(1, config, settings, state, stop_event, deadline_s=deadline_s)
        else:
            threads: list[threading.Thread] = []
            for runner_id in range(1, config.runners + 1):
                thread = threading.Thread(
                    target=run_runner_loop,
                    args=(runner_id, config, settings, state, stop_event),
                    kwargs={"deadline_s": deadline_s},
                    daemon=True,
                )
                thread.start()
                threads.append(thread)
            while any(thread.is_alive() for thread in threads):
                for thread in threads:
                    thread.join(timeout=0.2)
    finally:
        final_stream_snapshots: tuple[u64_stream.StreamSnapshot, ...] = ()
        if stream_monitor is not None:
            final_stream_snapshots = stream_monitor.snapshots()
            stream_monitor.stop()
        if final_stream_snapshots and any(snapshot.status == "FAIL" or snapshot.packets_received == 0 for snapshot in final_stream_snapshots):
            result = 1
        if state.failure_count:
            result = 1
    return result


def has_explicit_host(argv: list[str]) -> bool:
    return any(argument in {"-H", "--host"} or argument.startswith("--host=") for argument in argv)


def apply_profile_runtime_defaults(settings: RuntimeSettings, config: ExecutionConfig, argv: list[str]) -> RuntimeSettings:
    if config.profile is None or has_explicit_host(argv):
        return settings
    return replace(settings, host=DEFAULT_PROFILE_HOST)


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    settings = build_runtime_settings(args)
    try:
        config = resolve_execution_config(args)
        validate_execution_config(config)
    except ValueError as error:
        parser.error(str(error))
    settings = apply_profile_runtime_defaults(settings, config, argv)
    log_resolved_startup(settings, config)
    return run_extended(config, settings)


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        raise SystemExit(0)
