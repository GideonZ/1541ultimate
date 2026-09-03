#!/usr/bin/env python3
# PERF: Verifies managed /Temp uploads and measures cleanup-mode performance.

"""Validate and measure managed Temp uploads with cleanup and subfolders toggled.

The benchmark uploads the same small attachment-backed payload via HTTP in two
timed stages against the same Ultimate 64: first with Temp auto cleanup and
Temp subfolders enabled, then with both disabled. Each stage starts from an
empty managed Temp area, records upload latency samples, and asserts 
that the resulting managed uploadcount matches the expected cleanup behavior.
"""

import argparse
from collections import deque
import ftplib
import json
import math
import os
import statistics
import sys
import time
from dataclasses import dataclass
from pathlib import Path

# tests/lib holds the reporting rules every suite shares.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

import ftp as ftp_lib
import rest as rest_lib
from api import UltimateApi
from report import detail, progress, progress_done, section, suite_fail, suite_ok, warn

SUITE = "temp_auto_cleanup_perf_test"
CONFIG_CATEGORY = "User Interface Settings"
CONFIG_ITEMS = (
    ("Temp%20Auto%20Cleanup", "Temp Auto Cleanup"),
    ("Temp%20Subfolders", "Temp Subfolders"),
)
DEFAULT_STAGE_MODE = "both"
FTP_TIMEOUT_SECONDS = 30
MANAGED_UPLOAD_PATHS = ("/Temp/cache/upload", "/Temp/upload", "/Temp")
MAX_DISABLED_TOTAL_UPLOADS = 1000
MEMORY_START_ADDRESS = 0x0400
PAYLOAD_SIZE = 1024
ROLLING_WINDOW_SECONDS = 1
VERIFY_POLL_INTERVAL_SECONDS = 0.5
VERIFY_TIMEOUT_SECONDS = 45.0
WRITEMEM_PATH = f"/v1/machine:writemem?address={MEMORY_START_ADDRESS:04X}"
WARMUP_PROGRESS_INTERVAL = 25


def require_toggle_value(flag, value):
    if value not in ("Enabled", "Disabled"):
        raise argparse.ArgumentTypeError(f"{flag} expects Enabled or Disabled")
    return value


def require_stage_mode(flag, value):
    if value not in ("enabled", "disabled", DEFAULT_STAGE_MODE):
        raise argparse.ArgumentTypeError(f"{flag} expects enabled, disabled, or {DEFAULT_STAGE_MODE}")
    return value


def percentile(values, percent):
    ordered = sorted(values)
    index = int(math.ceil((percent / 100.0) * len(ordered))) - 1
    return ordered[max(0, min(index, len(ordered) - 1))]


def format_ms(value):
    return f"{value:.3f} ms"


def format_rps(value):
    return f"{value:.2f} rps"


def format_percent(value):
    return f"{value:.2f}%"


def managed_upload_dirs(subfolder):
    if subfolder == "Enabled":
        return ("/Temp/cache/upload", "/Temp/upload")
    return ("/Temp", "/Temp/upload")


def assert_or_warn(assertions_enabled, condition, message):
    if condition:
        return
    if assertions_enabled:
        raise RuntimeError(message)
    warn(message)


@dataclass
class StageResult:
    name: str
    sample_count: int
    warmup_count: int
    total_uploads: int
    duration_seconds: float
    min_ms: float
    avg_ms: float
    p50_ms: float
    p90_ms: float
    p99_ms: float
    max_ms: float
    rps: float
    managed_file_count: int


class U64Client:
    def __init__(self, host, password, assertions_enabled):
        self.host = host
        self.password = password
        self.assertions_enabled = assertions_enabled
        # For the calls this benchmark makes no measurement of, so that the
        # menu teardown has one implementation across the tree.
        self.api = UltimateApi(host, password)

    def close(self):
        return

    def _headers(self, body, extra_headers=None):
        headers = {"Connection": "close"}
        if self.password:
            headers["X-Password"] = self.password
        if body is not None:
            headers["Content-Length"] = str(len(body))
        if extra_headers:
            headers.update(extra_headers)
        return headers

    def request(self, method, path, body=None, retry=True, extra_headers=None):
        # Transport and retry policy come from tests/lib/rest.py; see
        # rest.may_retry. A request without a payload carries its arguments in
        # the query string, so applying it twice is the same as once.
        status, _headers, payload = rest_lib.retrying_http_request(
            self.host, method, path,
            body=body,
            headers=self._headers(body, extra_headers),
            timeout=10,
            idempotent=retry and body is None,
        )
        return status, payload

    def require_ok(self, method, path, body=None, description=None, allow_warning=False, extra_headers=None):
        status, payload = self.request(method, path, body=body, extra_headers=extra_headers)
        if status == 200:
            return payload

        message = f"{description or path} failed with HTTP {status}"
        if payload:
            try:
                document = json.loads(payload.decode("utf-8"))
                errors = document.get("errors")
                if errors:
                    message += f": {errors}"
            except (ValueError, UnicodeDecodeError):
                message += f": {payload[:160]!r}"

        if allow_warning and not self.assertions_enabled:
            warn(message)
            return None

        raise RuntimeError(message)

    def close_menu_from_anywhere(self):
        self.api.machine.close_menu_from_anywhere()


class ManagedTempInspector:
    """The managed Temp upload area, over FTP."""

    def __init__(self, host, password):
        self.host = host
        self.password = password

    def list_files(self, directory):
        with ftp_lib.session(self.host, self.password, FTP_TIMEOUT_SECONDS) as ftp:
            return ftp_lib.file_names(ftp, directory)

    def purge_directory(self, directory):
        with ftp_lib.session(self.host, self.password, FTP_TIMEOUT_SECONDS) as ftp:
            return ftp_lib.purge_directory(ftp, directory)

    def purge_all(self):
        with ftp_lib.session(self.host, self.password, FTP_TIMEOUT_SECONDS) as ftp:
            return sum(ftp_lib.purge_directory(ftp, directory)
                       for directory in MANAGED_UPLOAD_PATHS)


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Benchmark attachment-backed 1 KiB screen-memory writes on a real Ultimate 64 with "
            "Temp auto cleanup and Temp subfolders enabled first, then both disabled after a 5-second wait."
        ),
        epilog=(
            "Before each stage the managed Temp upload area is purged and verified "
            "empty. After each stage the script asserts that uploads created Temp "
            "files and that the resulting managed file count matches the active "
            "cleanup mode. Each request writes 1 KiB starting at $0400 via POST "
            "/v1/machine:writemem with an application/octet-stream body. The "
            "original Temp settings are restored before exit unless --no-config-change is used."
        ),
    )
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"),
                        help="IP or hostname of the U64 (default: $U64_HOST or u64)")
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""),
                        help="U64 REST password (default: $U64_PASS, empty)")
    parser.add_argument(
        "-n",
        "--no-assertions",
        action="store_true",
        help="Continue when config writes or Temp file assertions fail",
    )
    parser.add_argument(
        "--seed-count",
        type=int,
        default=1,
        help="Untimed warmup uploads before each measured stage",
    )
    parser.add_argument(
        "--test-count",
        type=int,
        default=8,
        help="Minimum measured uploads per stage",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=20.0,
        help="Measured seconds per stage",
    )
    parser.add_argument(
        "--stage",
        type=lambda value: require_stage_mode("--stage", value),
        default=DEFAULT_STAGE_MODE,
        help="Select enabled, disabled, or both stages (default: both)",
    )
    parser.add_argument(
        "--no-config-change",
        action="store_true",
        help="Do not change or restore Temp config settings; requires a single selected stage",
    )
    parser.add_argument(
        "--max-disabled-total-uploads",
        type=int,
        default=MAX_DISABLED_TOTAL_UPLOADS,
        help=(
            "Maximum warmup plus measured uploads to keep in Temp during the disabled-cleanup stage "
            f"(default: {MAX_DISABLED_TOTAL_UPLOADS})"
        ),
    )
    parser.add_argument(
        "--subfolder",
        type=lambda value: require_toggle_value("--subfolder", value),
        default=None,
        help="Override Temp Subfolders for both stages; default follows each stage setting",
    )
    parser.add_argument("-l", "--limit", type=int, default=10, help=argparse.SUPPRESS)
    parser.add_argument(
        "--cleanup",
        type=lambda value: require_toggle_value("--cleanup", value),
        default="Enabled",
        help=argparse.SUPPRESS,
    )

    args = parser.parse_args()
    if args.seed_count < 0:
        parser.error("--seed-count must be >= 0")
    if args.test_count <= 0:
        parser.error("--test-count must be > 0")
    if args.duration <= 0:
        parser.error("--duration must be > 0")
    if args.limit <= 0:
        parser.error("--limit must be > 0")
    if args.max_disabled_total_uploads <= 0:
        parser.error("--max-disabled-total-uploads must be > 0")
    if args.seed_count >= args.max_disabled_total_uploads:
        parser.error("--seed-count must be less than --max-disabled-total-uploads")
    if args.no_config_change and args.stage == DEFAULT_STAGE_MODE:
        parser.error("--no-config-change requires --stage enabled or --stage disabled")
    return args


def set_temp_settings(client, cleanup, subfolder, allow_warning=False):
    for key, value in (
        ("Temp%20Auto%20Cleanup", cleanup),
        ("Temp%20Subfolders", subfolder),
    ):
        client.require_ok(
            "PUT",
            f"/v1/configs/User%20Interface%20Settings/{key}?value={value}",
            description=f"configure {key}",
            allow_warning=allow_warning,
        )


def capture_initial_settings(client):
    snapshot = {}
    for encoded_key, plain_key in CONFIG_ITEMS:
        payload = client.require_ok(
            "GET",
            f"/v1/configs/User%20Interface%20Settings/{encoded_key}",
            description=f"read {encoded_key}",
        )
        document = json.loads(payload.decode("utf-8"))
        current = document[CONFIG_CATEGORY][plain_key]["current"]
        require_toggle_value(f"captured {plain_key}", current)
        snapshot[encoded_key] = current
    return snapshot


def restore_initial_settings(client, snapshot):
    if not snapshot:
        return

    section("restoring initial Temp settings")
    for encoded_key, _ in CONFIG_ITEMS:
        value = snapshot[encoded_key]
        client.require_ok(
            "PUT",
            f"/v1/configs/User%20Interface%20Settings/{encoded_key}?value={value}",
            description=f"restore {encoded_key}",
            allow_warning=not client.assertions_enabled,
        )
        detail(f"{encoded_key}: {value}")


def build_frame_payload(frame_number):
    payload = bytearray(b" " * PAYLOAD_SIZE)
    payload[:4] = f"{frame_number:04d}"[-4:].encode("ascii")
    return bytes(payload)


def write_screen_payload(client, payload):
    client.require_ok(
        "POST",
        WRITEMEM_PATH,
        body=payload,
        description=WRITEMEM_PATH,
        extra_headers={"Content-Type": "application/octet-stream"},
    )


def clear_screen(client):
    write_screen_payload(client, b" " * PAYLOAD_SIZE)


def post_screen_memory_write(client, frame_counter):
    frame_counter[0] += 1
    payload = build_frame_payload(frame_counter[0])
    started = time.perf_counter_ns()
    write_screen_payload(client, payload)
    return (time.perf_counter_ns() - started) / 1_000_000.0


def emit_progress(prefix, current, total=None, elapsed=None):
    if total is not None:
        progress(f"{prefix}: {current}/{total}")
    else:
        progress(f"{prefix}: {current} samples in {elapsed:.2f}s")


def emit_rolling_window_stats(total_uploads, elapsed, window_latencies_ms):
    if not window_latencies_ms:
        return

    window_count = len(window_latencies_ms)
    rps = window_count / ROLLING_WINDOW_SECONDS
    print(
        f"files={total_uploads} t={elapsed:.1f}s "
        f"5s p50={format_ms(percentile(window_latencies_ms, 50))} "
        f"p90={format_ms(percentile(window_latencies_ms, 90))} "
        f"p99={format_ms(percentile(window_latencies_ms, 99))} "
        f"rps={format_rps(rps)}"
    )


def count_managed_files(inspector, directories):
    total = 0
    counts = []
    seen = set()

    for directory in directories:
        if directory in seen:
            continue
        seen.add(directory)
        file_count = len(inspector.list_files(directory))
        total += file_count
        counts.append((directory, file_count))

    return total, counts


def format_managed_counts(counts):
    return ", ".join(f"{directory}={count}" for directory, count in counts)


def summarize_stage(name, warmup_count, duration_seconds, latencies_ms, managed_file_count):
    if not latencies_ms:
        raise RuntimeError(f"No latency samples collected for {name}")

    sample_count = len(latencies_ms)
    return StageResult(
        name=name,
        sample_count=sample_count,
        warmup_count=warmup_count,
        total_uploads=warmup_count + sample_count,
        duration_seconds=duration_seconds,
        min_ms=min(latencies_ms),
        avg_ms=statistics.fmean(latencies_ms),
        p50_ms=percentile(latencies_ms, 50),
        p90_ms=percentile(latencies_ms, 90),
        p99_ms=percentile(latencies_ms, 99),
        max_ms=max(latencies_ms),
        rps=sample_count / duration_seconds,
        managed_file_count=managed_file_count,
    )


def wait_for_expected_file_count(inspector, directories, predicate, description, assertions_enabled):
    deadline = time.monotonic() + VERIFY_TIMEOUT_SECONDS
    last_seen, last_counts = count_managed_files(inspector, directories)

    while time.monotonic() < deadline:
        if predicate(last_seen):
            return last_seen
        time.sleep(VERIFY_POLL_INTERVAL_SECONDS)
        try:
            last_seen, last_counts = count_managed_files(inspector, directories)
        except (OSError, EOFError, ftplib.Error) as exc:
            if time.monotonic() >= deadline:
                message = f"{description} (last FTP error: {exc})"
                assert_or_warn(assertions_enabled, False, message)
                return last_seen

    assert_or_warn(
        assertions_enabled,
        False,
        f"{description} (observed {last_seen}; {format_managed_counts(last_counts)})",
    )
    return last_seen


def prepare_stage(inspector, upload_dirs, stage_name, assertions_enabled):
    removed = inspector.purge_all()
    if removed:
        detail(f"purged {removed} existing managed temp files")

    leftovers = []
    for directory in MANAGED_UPLOAD_PATHS:
        names = inspector.list_files(directory)
        if names:
            leftovers.append(f"{directory}: {', '.join(names)}")
    assert_or_warn(
        assertions_enabled,
        not leftovers,
        f"{stage_name}: expected empty managed Temp area before stage, found {'; '.join(leftovers)}",
    )

    pre_count, pre_counts = count_managed_files(inspector, upload_dirs)
    assert_or_warn(
        assertions_enabled,
        pre_count == 0,
        f"{stage_name}: expected 0 managed temp files before stage, found {pre_count} ({format_managed_counts(pre_counts)})",
    )


def verify_stage_results(inspector, upload_dirs, cleanup, limit, result, assertions_enabled):
    if cleanup == "Enabled":
        managed_count = wait_for_expected_file_count(
            inspector,
            upload_dirs,
            lambda count: 0 < count <= limit,
            f"{result.name}: expected managed temp uploads to exist but stay at or below {limit}",
            assertions_enabled,
        )
    else:
        expected = result.total_uploads
        managed_count = wait_for_expected_file_count(
            inspector,
            upload_dirs,
            lambda count: count == expected,
            f"{result.name}: expected all {expected} uploaded temp files to remain",
            assertions_enabled,
        )

    result.managed_file_count = managed_count
    assert_or_warn(
        assertions_enabled,
        managed_count > 0,
        f"{result.name}: no managed temp files were created during the stage",
    )


def run_stage(
    client,
    inspector,
    frame_counter,
    name,
    cleanup,
    subfolder,
    warmup_count,
    minimum_samples,
    duration_seconds,
    limit,
    max_total_uploads=None,
    apply_config=True,
):
    if apply_config:
        set_temp_settings(client, cleanup, subfolder, allow_warning=not client.assertions_enabled)
    upload_dirs = managed_upload_dirs(subfolder)
    upload_dir = upload_dirs[0]
    clear_screen(client)
    prepare_stage(inspector, upload_dirs, name, client.assertions_enabled)

    section(f"{name}: cleanup {cleanup}, subfolders {subfolder}")
    detail(f"upload target: {upload_dir}")
    detail(f"memory write: ${MEMORY_START_ADDRESS:04X}-${MEMORY_START_ADDRESS + PAYLOAD_SIZE - 1:04X}")
    if max_total_uploads is not None:
        detail(f"max total uploads this stage: {max_total_uploads}")

    for warmup_index in range(warmup_count):
        post_screen_memory_write(client, frame_counter)
        if ((warmup_index + 1) % WARMUP_PROGRESS_INTERVAL) == 0 or (warmup_index + 1) == warmup_count:
            emit_progress("Warmup", warmup_index + 1, total=warmup_count)
    if warmup_count:
        progress_done()

    latencies_ms = []
    stage_started = time.perf_counter()
    deadline = stage_started + duration_seconds
    rolling_window = deque()
    next_window_report = stage_started + ROLLING_WINDOW_SECONDS

    while True:
        latency_ms = post_screen_memory_write(client, frame_counter)
        now = time.perf_counter()
        latencies_ms.append(latency_ms)
        rolling_window.append((now, latency_ms))

        cutoff = now - ROLLING_WINDOW_SECONDS
        while rolling_window and rolling_window[0][0] < cutoff:
            rolling_window.popleft()

        if now >= next_window_report:
            emit_rolling_window_stats(
                warmup_count + len(latencies_ms),
                now - stage_started,
                [sample_latency for _, sample_latency in rolling_window],
            )
            next_window_report += ROLLING_WINDOW_SECONDS
        if max_total_uploads is not None and (warmup_count + len(latencies_ms)) >= max_total_uploads:
            break
        if (now >= deadline) and (len(latencies_ms) >= minimum_samples):
            break

    actual_duration = time.perf_counter() - stage_started

    result = summarize_stage(name, warmup_count, actual_duration, latencies_ms, 0)
    verify_stage_results(inspector, upload_dirs, cleanup, limit, result, client.assertions_enabled)
    return result


def print_stage_summary(result):
    section(f"{result.name} summary")
    detail(f"warmup uploads: {result.warmup_count}")
    detail(f"measured uploads: {result.sample_count}")
    detail(f"total uploads: {result.total_uploads}")
    detail(f"managed files after stage: {result.managed_file_count}")
    detail(f"duration: {result.duration_seconds:.3f} s")
    detail(f"throughput: {format_rps(result.rps)}")
    detail(f"p50: {format_ms(result.p50_ms)}")
    detail(f"p90: {format_ms(result.p90_ms)}")
    detail(f"p99: {format_ms(result.p99_ms)}")
    detail(f"avg: {format_ms(result.avg_ms)}")
    detail(f"min: {format_ms(result.min_ms)}")
    detail(f"max: {format_ms(result.max_ms)}")


def print_comparison(enabled, disabled):
    def percent_delta(enabled_value, disabled_value):
        if disabled_value == 0:
            return None
        return ((enabled_value - disabled_value) / disabled_value) * 100.0

    p50_delta = enabled.p50_ms - disabled.p50_ms
    p90_delta = enabled.p90_ms - disabled.p90_ms
    p99_delta = enabled.p99_ms - disabled.p99_ms
    rps_delta = enabled.rps - disabled.rps

    section("delta (enabled - disabled)")
    print(
        f"  P50: {format_ms(p50_delta)} "
        f"({format_percent(percent_delta(enabled.p50_ms, disabled.p50_ms)) if percent_delta(enabled.p50_ms, disabled.p50_ms) is not None else 'n/a'})"
    )
    print(
        f"  P90: {format_ms(p90_delta)} "
        f"({format_percent(percent_delta(enabled.p90_ms, disabled.p90_ms)) if percent_delta(enabled.p90_ms, disabled.p90_ms) is not None else 'n/a'})"
    )
    print(
        f"  P99: {format_ms(p99_delta)} "
        f"({format_percent(percent_delta(enabled.p99_ms, disabled.p99_ms)) if percent_delta(enabled.p99_ms, disabled.p99_ms) is not None else 'n/a'})"
    )
    print(
        f"  Throughput: {format_rps(rps_delta)} "
        f"({format_percent(percent_delta(enabled.rps, disabled.rps)) if percent_delta(enabled.rps, disabled.rps) is not None else 'n/a'})"
    )


def stage_specs(args):
    disabled_subfolder = args.subfolder if args.subfolder is not None else "Disabled"
    enabled_subfolder = args.subfolder if args.subfolder is not None else "Enabled"

    specs = {
        "enabled": {
            "name": "Enabled",
            "cleanup": "Enabled",
            "subfolder": enabled_subfolder,
            "max_total_uploads": None,
        },
        "disabled": {
            "name": "Disabled",
            "cleanup": "Disabled",
            "subfolder": disabled_subfolder,
            "max_total_uploads": args.max_disabled_total_uploads,
        },
    }

    if args.stage == DEFAULT_STAGE_MODE:
        return [specs["enabled"], specs["disabled"]]
    return [specs[args.stage]]


def main():
    args = parse_args()
    client = U64Client(args.host, args.password, not args.no_assertions)
    inspector = ManagedTempInspector(args.host, args.password)
    frame_counter = [0]
    initial_settings = None

    print("Ultimate 64 Temp Auto Cleanup Performance Probe")
    print(f"Target Host: {args.host}")
    print(f"Stage Duration: {args.duration:.3f} s")
    print(f"Minimum Samples Per Stage: {args.test_count}")
    print(f"Selected Stage Mode: {args.stage}")
    print(f"Change Config Settings: {'no' if args.no_config_change else 'yes'}")
    print(f"Write Range: ${MEMORY_START_ADDRESS:04X}-${MEMORY_START_ADDRESS + PAYLOAD_SIZE - 1:04X}")
    print(f"Binary Payload Size: {PAYLOAD_SIZE} bytes")

    try:
        client.close_menu_from_anywhere()
        # force: this suite reaches the device over FTP as well, which the
        # REST transport cannot see, so it cannot tell whether a reset is a
        # no-op.
        client.api.machine.reset(force=True, wait=False)
        time.sleep(1.0)

        if args.no_config_change:
            print("Skipping initial Temp settings capture because config changes are disabled")
        else:
            initial_settings = capture_initial_settings(client)
            print("Initial Temp settings")
            for encoded_key, _ in CONFIG_ITEMS:
                print(f"  {encoded_key}: {initial_settings[encoded_key]}")

        results = []
        for spec in stage_specs(args):
            results.append(
                run_stage(
                    client,
                    inspector,
                    frame_counter,
                    spec["name"],
                    spec["cleanup"],
                    spec["subfolder"],
                    args.seed_count,
                    args.test_count,
                    args.duration,
                    args.limit,
                    spec["max_total_uploads"],
                    apply_config=not args.no_config_change,
                )
            )

        for result in results:
            print_stage_summary(result)
        if len(results) == 2:
            print_comparison(results[0], results[1])
        suite_ok(SUITE, f"{len(results)} stage(s) measured")
        return 0
    except KeyboardInterrupt:
        suite_fail(SUITE, "interrupted")
        return 130
    except Exception as exc:
        suite_fail(SUITE, str(exc))
        return 1
    finally:
        try:
            # The measured stages upload hundreds of small files and the
            # purge only runs before each stage, so without this the last
            # stage's uploads stay in the managed Temp area and every later
            # run of any suite reads a directory listing that much longer.
            section("clearing the managed Temp upload area")
            try:
                detail(f"removed {inspector.purge_all()} managed temp files")
            except Exception as exc:
                warn(f"could not clear the managed Temp area: {exc}")
            if args.no_config_change:
                section("leaving Temp settings unchanged")
            else:
                restore_initial_settings(client, initial_settings)
        finally:
            client.close()


if __name__ == "__main__":
    sys.exit(main())
