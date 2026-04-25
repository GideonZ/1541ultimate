#!/usr/bin/env python3
"""Measure managed Temp upload latency with auto cleanup enabled and disabled.

The benchmark uploads the same small attachment-backed payload via native HTTP in two
timed stages against the same Ultimate 64: first with Temp auto cleanup enabled,
then with it disabled. Each stage starts from an empty managed Temp area,
records upload latency samples, and asserts that the resulting managed upload
count matches the expected cleanup behavior.
"""

import argparse
import ftplib
import http.client
import json
import math
import statistics
import sys
import time
from dataclasses import dataclass


CONFIG_CATEGORY = "User Interface Settings"
CONFIG_ITEMS = (
    ("Temp%20Auto%20Cleanup", "Temp Auto Cleanup"),
    ("Temp%20Subfolders", "Temp Subfolders"),
)
FTP_DEFAULT_PASSWORD = "password"
FTP_USER = "user"
MANAGED_UPLOAD_PATHS = ("/Temp/cache/upload", "/Temp/upload")
MAX_DISABLED_TOTAL_UPLOADS = 100
MEASURE_PROGRESS_INTERVAL = 100
MEMORY_START_ADDRESS = 0x0400
PAYLOAD_SIZE = 1024
WRITEMEM_PATH = f"/v1/machine:writemem?address={MEMORY_START_ADDRESS:04X}"
WARMUP_PROGRESS_INTERVAL = 25


def require_toggle_value(flag, value):
    if value not in ("Enabled", "Disabled"):
        raise argparse.ArgumentTypeError(f"{flag} expects Enabled or Disabled")
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


def managed_upload_dir(subfolder):
    if subfolder == "Enabled":
        return "/Temp/cache/upload"
    return "/Temp/upload"


def assert_or_warn(assertions_enabled, condition, message):
    if condition:
        return
    if assertions_enabled:
        raise RuntimeError(message)
    print(f"WARNING: {message}")


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
        connection = http.client.HTTPConnection(self.host, timeout=10)
        try:
            connection.request(method, path, body=body, headers=self._headers(body, extra_headers))
            response = connection.getresponse()
            payload = response.read()
            return response.status, payload
        except (OSError, http.client.HTTPException):
            connection.close()
            if retry and (body is None):
                return self.request(method, path, body=body, retry=False, extra_headers=extra_headers)
            raise
        finally:
            connection.close()

    def require_ok(self, method, path, body=None, description=None, allow_warning=False, extra_headers=None):
        status, payload = self.request(method, path, body=body, extra_headers=extra_headers)
        if status == 200:
            return payload

        message = f"{description or path} failed with HTTP {status}"
        if payload:
            try:
                detail = json.loads(payload.decode("utf-8"))
                errors = detail.get("errors")
                if errors:
                    message += f": {errors}"
            except (ValueError, UnicodeDecodeError):
                message += f": {payload[:160]!r}"

        if allow_warning and not self.assertions_enabled:
            print(f"WARNING: {message}")
            return None

        raise RuntimeError(message)


class ManagedTempInspector:
    def __init__(self, host, password):
        self.host = host
        self.password = password or FTP_DEFAULT_PASSWORD

    def _open(self):
        ftp = ftplib.FTP(self.host, timeout=10)
        ftp.login(FTP_USER, self.password)
        return ftp

    def _close(self, ftp):
        try:
            ftp.quit()
        except (OSError, EOFError, ftplib.Error):
            ftp.close()

    def _list_current_directory(self, ftp):
        try:
            names = ftp.nlst()
        except ftplib.error_perm as exc:
            if str(exc).startswith("550"):
                return []
            raise
        return sorted(name for name in names if name not in (".", ".."))

    def list_files(self, directory):
        ftp = self._open()
        try:
            ftp.cwd(directory)
            return self._list_current_directory(ftp)
        except ftplib.error_perm as exc:
            if str(exc).startswith("550"):
                return []
            raise RuntimeError(f"FTP list failed for {directory}: {exc}") from exc
        finally:
            self._close(ftp)

    def purge_directory(self, directory):
        ftp = self._open()
        try:
            ftp.cwd(directory)
            names = self._list_current_directory(ftp)
            for name in names:
                ftp.delete(name)
            return len(names)
        except ftplib.error_perm as exc:
            if str(exc).startswith("550"):
                return 0
            raise RuntimeError(f"FTP purge failed for {directory}: {exc}") from exc
        finally:
            self._close(ftp)

    def purge_all(self):
        removed = 0
        for directory in MANAGED_UPLOAD_PATHS:
            removed += self.purge_directory(directory)
        return removed


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Benchmark attachment-backed 1 KiB screen-memory writes on a real Ultimate 64 with "
            "Temp auto cleanup enabled first and disabled second."
        ),
        epilog=(
            "Before each stage the managed Temp upload area is purged and verified "
            "empty. After each stage the script asserts that uploads created Temp "
            "files and that the resulting managed file count matches the active "
            "cleanup mode. Each request writes 1 KiB starting at $0400 via POST "
            "/v1/machine:writemem with an application/octet-stream body. The "
            "original Temp settings are restored before exit."
        ),
    )
    parser.add_argument("-H", "--host", default="u64", help="IP or hostname of the U64")
    parser.add_argument("-p", "--password", default="", help="U64 REST password")
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
        default=15.0,
        help="Measured seconds per stage",
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
        default="Enabled",
        help="Temp Subfolders setting used for both stages",
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

    print("\nRestoring initial Temp settings")
    for encoded_key, _ in CONFIG_ITEMS:
        value = snapshot[encoded_key]
        client.require_ok(
            "PUT",
            f"/v1/configs/User%20Interface%20Settings/{encoded_key}?value={value}",
            description=f"restore {encoded_key}",
            allow_warning=not client.assertions_enabled,
        )
        print(f"  {encoded_key}: {value}")


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
        message = f"\r{prefix}: {current}/{total}"
    else:
        message = f"\r{prefix}: {current} samples in {elapsed:.2f}s"
    sys.stdout.write(message)
    sys.stdout.flush()


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


def wait_for_expected_file_count(inspector, directory, predicate, description, assertions_enabled):
    deadline = time.monotonic() + 8.0
    last_seen = len(inspector.list_files(directory))

    while time.monotonic() < deadline:
        if predicate(last_seen):
            return last_seen
        time.sleep(0.25)
        last_seen = len(inspector.list_files(directory))

    assert_or_warn(assertions_enabled, False, f"{description} (observed {last_seen})")
    return last_seen


def prepare_stage(inspector, upload_dir, stage_name, assertions_enabled):
    removed = inspector.purge_all()
    if removed:
        print(f"  Purged {removed} existing managed temp files")

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

    pre_count = len(inspector.list_files(upload_dir))
    assert_or_warn(
        assertions_enabled,
        pre_count == 0,
        f"{stage_name}: expected 0 managed temp files before stage, found {pre_count}",
    )


def verify_stage_results(inspector, upload_dir, cleanup, limit, result, assertions_enabled):
    if cleanup == "Enabled":
        managed_count = wait_for_expected_file_count(
            inspector,
            upload_dir,
            lambda count: 0 < count <= limit,
            f"{result.name}: expected managed temp uploads to exist but stay at or below {limit}",
            assertions_enabled,
        )
    else:
        expected = result.total_uploads
        managed_count = wait_for_expected_file_count(
            inspector,
            upload_dir,
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
):
    set_temp_settings(client, cleanup, subfolder, allow_warning=not client.assertions_enabled)
    upload_dir = managed_upload_dir(subfolder)
    clear_screen(client)
    prepare_stage(inspector, upload_dir, name, client.assertions_enabled)

    print(f"\n{name}: cleanup {cleanup}, subfolders {subfolder}")
    print(f"  Upload target: {upload_dir}")
    print(f"  Memory write: ${MEMORY_START_ADDRESS:04X}-${MEMORY_START_ADDRESS + PAYLOAD_SIZE - 1:04X}")
    if max_total_uploads is not None:
        print(f"  Max total uploads this stage: {max_total_uploads}")

    for warmup_index in range(warmup_count):
        post_screen_memory_write(client, frame_counter)
        if ((warmup_index + 1) % WARMUP_PROGRESS_INTERVAL) == 0 or (warmup_index + 1) == warmup_count:
            emit_progress("Warmup", warmup_index + 1, total=warmup_count)
    if warmup_count:
        sys.stdout.write("\n")

    latencies_ms = []
    stage_started = time.perf_counter()
    deadline = stage_started + duration_seconds

    while True:
        latencies_ms.append(post_screen_memory_write(client, frame_counter))
        now = time.perf_counter()
        if (len(latencies_ms) % MEASURE_PROGRESS_INTERVAL) == 0:
            emit_progress("Measured", len(latencies_ms), elapsed=now - stage_started)
        if max_total_uploads is not None and (warmup_count + len(latencies_ms)) >= max_total_uploads:
            break
        if (now >= deadline) and (len(latencies_ms) >= minimum_samples):
            break

    actual_duration = time.perf_counter() - stage_started
    emit_progress("Measured", len(latencies_ms), elapsed=actual_duration)
    sys.stdout.write("\n")

    result = summarize_stage(name, warmup_count, actual_duration, latencies_ms, 0)
    verify_stage_results(inspector, upload_dir, cleanup, limit, result, client.assertions_enabled)
    return result


def print_stage_summary(result):
    print(f"\n{result.name} summary")
    print(f"  Warmup uploads: {result.warmup_count}")
    print(f"  Measured uploads: {result.sample_count}")
    print(f"  Total uploads: {result.total_uploads}")
    print(f"  Managed files after stage: {result.managed_file_count}")
    print(f"  Duration: {result.duration_seconds:.3f} s")
    print(f"  Throughput: {format_rps(result.rps)}")
    print(f"  P50: {format_ms(result.p50_ms)}")
    print(f"  P90: {format_ms(result.p90_ms)}")
    print(f"  P99: {format_ms(result.p99_ms)}")
    print(f"  Avg: {format_ms(result.avg_ms)}")
    print(f"  Min: {format_ms(result.min_ms)}")
    print(f"  Max: {format_ms(result.max_ms)}")


def print_comparison(enabled, disabled):
    def percent_delta(enabled_value, disabled_value):
        if disabled_value == 0:
            return None
        return ((enabled_value - disabled_value) / disabled_value) * 100.0

    p50_delta = enabled.p50_ms - disabled.p50_ms
    p90_delta = enabled.p90_ms - disabled.p90_ms
    p99_delta = enabled.p99_ms - disabled.p99_ms
    rps_delta = enabled.rps - disabled.rps

    print("\nDelta (enabled - disabled)")
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
    print(f"Write Range: ${MEMORY_START_ADDRESS:04X}-${MEMORY_START_ADDRESS + PAYLOAD_SIZE - 1:04X}")
    print(f"Binary Payload Size: {PAYLOAD_SIZE} bytes")

    try:
        initial_settings = capture_initial_settings(client)
        print("Initial Temp settings")
        for encoded_key, _ in CONFIG_ITEMS:
            print(f"  {encoded_key}: {initial_settings[encoded_key]}")

        enabled = run_stage(
            client,
            inspector,
            frame_counter,
            "Enabled",
            "Enabled",
            args.subfolder,
            args.seed_count,
            args.test_count,
            args.duration,
            args.limit,
        )
        disabled = run_stage(
            client,
            inspector,
            frame_counter,
            "Disabled",
            "Disabled",
            args.subfolder,
            args.seed_count,
            args.test_count,
            args.duration,
            args.limit,
            args.max_disabled_total_uploads,
        )
        print_stage_summary(enabled)
        print_stage_summary(disabled)
        print_comparison(enabled, disabled)
        return 0
    except KeyboardInterrupt:
        print("\nStopped.")
        return 130
    except Exception as exc:
        print(f"ERROR: {exc}")
        return 1
    finally:
        try:
            restore_initial_settings(client, initial_settings)
        finally:
            client.close()


if __name__ == "__main__":
    sys.exit(main())