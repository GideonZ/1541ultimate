from __future__ import annotations

import http.client
import json
import re
import socket
import struct
import time
import urllib.parse
from contextlib import nullcontext
from typing import Any

from u64_connection_runtime import (
    ProbeCorrectness,
    ProbeExecutionContext,
    ProbeOutcome,
    ProbeSurface,
    RuntimeSettings,
    is_expected_incomplete_disconnect,
    run_surface_operation,
    select_operation_index,
    surface_detail,
)


HTTP_AUDIO_MIXER_CATEGORY_PATH = "/v1/configs/Audio%20Mixer"
HTTP_VOLUME_ULTISID_1_PATH = f"{HTTP_AUDIO_MIXER_CATEGORY_PATH}/Vol%20UltiSid%201"
AUDIO_MIXER_WRITE_ITEM = "Vol UltiSid 1"
AUDIO_MIXER_WRITE_TARGET_VALUES = ("0 dB", "+1 dB")
AUDIO_MIXER_SHARED_STATE_KEY = "u64.audio_mixer.vol_ultisid_1"
AUDIO_MIXER_TENTATIVE_STATE_KEY = "u64.audio_mixer.vol_ultisid_1.tentative"
# Keep probe write slots in the small unused page-3 ranges so readwrite probes
# avoid both visible screen RAM and the datasette buffer.
PROBE_WRITE_ADDRESSES = tuple(range(0x0334, 0x033C)) + tuple(range(0x03FC, 0x0400))
PROBE_WRITE_RUNNER_SLOT_COUNT = len(PROBE_WRITE_ADDRESSES)
STATE_VERIFY_RETRY_DELAYS_S = (0.05, 0.10, 0.20)


def request_headers(settings: RuntimeSettings) -> dict[str, str]:
    headers = {"Connection": "close"}
    if settings.network_password:
        headers["X-Password"] = settings.network_password
    return headers


def request_path(http_path: str) -> str:
    return f"/{http_path}"


def parse_response(payload: bytes) -> tuple[int, bytes]:
    header_end = payload.find(b"\r\n\r\n")
    if header_end < 0:
        raise RuntimeError("invalid HTTP response")
    header_block = payload[:header_end].decode("iso-8859-1", "replace")
    status_line = header_block.split("\r\n", 1)[0]
    parts = status_line.split()
    if len(parts) < 2 or not parts[1].isdigit():
        raise RuntimeError("invalid HTTP status")
    return int(parts[1]), payload[header_end + 4 :]


def request_bytes(settings: RuntimeSettings, method: str, path: str) -> tuple[int, bytes, dict[str, str]]:
    conn = http.client.HTTPConnection(settings.host, settings.http_port, timeout=3)
    try:
        conn.request(method, path, headers=request_headers(settings))
        response = conn.getresponse()
        body = response.read()
        headers = {key.lower(): value for key, value in response.getheaders()}
        return response.status, body, headers
    finally:
        conn.close()


def json_request(settings: RuntimeSettings, method: str, path: str) -> tuple[int, object, int]:
    status, body, _headers = request_bytes(settings, method, path)
    if not 200 <= status < 300:
        raise RuntimeError(f"expected HTTP 2xx, got {status}")
    if not body:
        raise RuntimeError("empty JSON body")
    try:
        payload = json.loads(body.decode("utf-8"))
    except Exception as error:
        raise RuntimeError(f"invalid JSON body: {error}") from error
    if payload in (None, "", [], {}):
        raise RuntimeError("empty JSON payload")
    return status, payload, len(body)


def safe_read(settings: RuntimeSettings, path: str) -> str:
    status, body, headers = request_bytes(settings, "GET", path)
    if path.startswith("/v1/files") and status == 404:
        return "skip=files_endpoint_unavailable"
    if not 200 <= status < 300:
        raise RuntimeError(f"expected HTTP 2xx, got {status}")
    if not body:
        raise RuntimeError("empty HTTP body")
    content_type = headers.get("content-type", "")
    if "json" in content_type.lower():
        try:
            payload = json.loads(body.decode("utf-8"))
        except Exception as error:
            raise RuntimeError(f"invalid JSON body: {error}") from error
        if payload in (None, "", [], {}):
            raise RuntimeError("empty JSON payload")
        return f"http_status={status} body_bytes={len(body)} json_type={type(payload).__name__}"
    return f"http_status={status} body_bytes={len(body)}"


def extract_first_byte(payload: object) -> int | None:
    if isinstance(payload, dict):
        if "data" in payload:
            return extract_first_byte(payload["data"])
        if "value" in payload:
            return extract_first_byte(payload["value"])
        return None
    if isinstance(payload, int):
        return payload & 0xFF
    if isinstance(payload, list):
        if not payload:
            return None
        return extract_first_byte(payload[0])
    if isinstance(payload, str):
        raw = payload.strip()
        if not raw:
            return None
        tokens = [token for token in re.split(r"[\s,]+", raw) if token]
        if not tokens:
            return None
        token = tokens[0]
        if token.lower().startswith("0x"):
            token = token[2:]
        for base in (16, 10):
            try:
                return int(token, base) & 0xFF
            except ValueError:
                continue
    return None


def generic_read(settings: RuntimeSettings, path: str) -> str:
    return safe_read(settings, path)


def normalize_audio_mixer_value(value: str) -> str:
    return " ".join(value.split())


def resolve_audio_mixer_value(values: tuple[str, ...], target: str) -> str:
    normalized_target = normalize_audio_mixer_value(target)
    for value in values:
        if normalize_audio_mixer_value(value) == normalized_target:
            return value
    raise RuntimeError(f"unsupported target value: {target}")


def audio_mixer_shared_lock(shared_state: Any | None):
    if shared_state is None or not hasattr(shared_state, "shared_resource_lock_for"):
        return nullcontext()
    return shared_state.shared_resource_lock_for(AUDIO_MIXER_SHARED_STATE_KEY)


def remember_audio_mixer_value(shared_state: Any | None, value: str) -> str:
    normalized = normalize_audio_mixer_value(value)
    if shared_state is not None and hasattr(shared_state, "set_shared_resource_value"):
        shared_state.set_shared_resource_value(AUDIO_MIXER_SHARED_STATE_KEY, normalized)
    return normalized


def stage_audio_mixer_value(shared_state: Any | None, value: str) -> str:
    normalized = normalize_audio_mixer_value(value)
    if shared_state is not None and hasattr(shared_state, "set_shared_resource_value"):
        shared_state.set_shared_resource_value(AUDIO_MIXER_TENTATIVE_STATE_KEY, normalized)
    return normalized


def clear_audio_mixer_tentative(shared_state: Any | None) -> None:
    if shared_state is None or not hasattr(shared_state, "set_shared_resource_value"):
        return
    shared_state.set_shared_resource_value(AUDIO_MIXER_TENTATIVE_STATE_KEY, None)


def confirm_audio_mixer_value(shared_state: Any | None, value: str) -> str:
    normalized = remember_audio_mixer_value(shared_state, value)
    clear_audio_mixer_tentative(shared_state)
    return normalized


def latest_audio_mixer_value(shared_state: Any | None, *, include_tentative: bool = False) -> str | None:
    if shared_state is None or not hasattr(shared_state, "get_shared_resource_value"):
        return None
    if include_tentative:
        tentative = shared_state.get_shared_resource_value(AUDIO_MIXER_TENTATIVE_STATE_KEY)
        if isinstance(tentative, str) and tentative.strip():
            return normalize_audio_mixer_value(tentative)
    value = shared_state.get_shared_resource_value(AUDIO_MIXER_SHARED_STATE_KEY)
    if not isinstance(value, str) or not value.strip():
        return None
    return normalize_audio_mixer_value(value)


def verify_audio_mixer_value(settings: RuntimeSettings, expected: str, *, shared_state: Any | None = None) -> str:
    normalized_target = normalize_audio_mixer_value(expected)
    observed = "unknown"
    attempts = len(STATE_VERIFY_RETRY_DELAYS_S) + 1
    for attempt in range(attempts):
        current, _values, _body_bytes = audio_mixer_item_state(settings)
        observed = remember_audio_mixer_value(shared_state, current)
        if observed == normalized_target:
            return confirm_audio_mixer_value(shared_state, observed)
        if attempt + 1 < attempts:
            time.sleep(STATE_VERIFY_RETRY_DELAYS_S[attempt])
    clear_audio_mixer_tentative(shared_state)
    raise RuntimeError(
        f"verification mismatch expected={normalized_target} got={observed} latest_known={latest_audio_mixer_value(shared_state) or 'unknown'}"
    )


def audio_mixer_item_state(settings: RuntimeSettings) -> tuple[str, tuple[str, ...], int]:
    _status, payload, body_bytes = json_request(settings, "GET", HTTP_VOLUME_ULTISID_1_PATH)
    category_payload = payload.get("Audio Mixer") if isinstance(payload, dict) else None
    if not isinstance(category_payload, dict):
        raise RuntimeError("missing Audio Mixer payload")
    item_payload = category_payload.get(AUDIO_MIXER_WRITE_ITEM)
    if not isinstance(item_payload, dict):
        raise RuntimeError("missing Audio Mixer write payload")
    current = item_payload.get("current")
    values = item_payload.get("values")
    if not isinstance(current, str) or not current.strip():
        raise RuntimeError("missing Audio Mixer write current value")
    if not isinstance(values, list) or not values:
        raise RuntimeError("missing Audio Mixer write values")
    normalized_values = tuple(str(value) for value in values if str(value).strip())
    if not normalized_values:
        raise RuntimeError("empty Audio Mixer write values")
    return current, normalized_values, body_bytes


def read_audio_mixer_item(settings: RuntimeSettings, *, shared_state: Any | None = None) -> str:
    with audio_mixer_shared_lock(shared_state):
        current, values, body_bytes = audio_mixer_item_state(settings)
        normalized_current = confirm_audio_mixer_value(shared_state, current)
        return f"body_bytes={body_bytes} current={normalized_current} options={len(values)}"


def write_audio_mixer_item(settings: RuntimeSettings, target: str, *, shared_state: Any | None = None) -> str:
    with audio_mixer_shared_lock(shared_state):
        current, values, _body_bytes = audio_mixer_item_state(settings)
        normalized_current = remember_audio_mixer_value(shared_state, current)
        resolved_target = resolve_audio_mixer_value(values, target)
        normalized_target = normalize_audio_mixer_value(resolved_target)
        if normalized_current != normalized_target:
            encoded_target = urllib.parse.quote(resolved_target, safe="")
            status, _body, _headers = request_bytes(settings, "PUT", f"{HTTP_VOLUME_ULTISID_1_PATH}?value={encoded_target}")
            if not 200 <= status < 300:
                raise RuntimeError(f"expected HTTP 2xx, got {status}")
            stage_audio_mixer_value(shared_state, normalized_target)
        normalized_updated = verify_audio_mixer_value(settings, normalized_target, shared_state=shared_state)
        return f"from={normalized_current} to={normalized_updated}"


def memory_read(settings: RuntimeSettings, address: str, length: int) -> str:
    status, body, _headers = request_bytes(settings, "GET", f"/v1/machine:readmem?address={address}&length={length}")
    if not 200 <= status < 300:
        raise RuntimeError(f"expected HTTP 2xx, got {status}")
    if not body:
        raise RuntimeError("empty memory read body")
    expected_length = max(1, length)
    if len(body) < expected_length:
        raise RuntimeError(f"short memory read: expected at least {expected_length} bytes, got {len(body)}")
    return f"http_status={status} body_bytes={len(body)} byte=0x{body[0]:02X}"


def memory_write_verify(settings: RuntimeSettings, address: str, data_hex: str) -> str:
    write_status, _body, _headers = request_bytes(settings, "PUT", f"/v1/machine:writemem?address={address}&data={data_hex}")
    if not 200 <= write_status < 300:
        raise RuntimeError(f"expected HTTP 2xx, got {write_status}")
    read_status, read_body, _headers = request_bytes(settings, "GET", f"/v1/machine:readmem?address={address}&length=1")
    if not 200 <= read_status < 300:
        raise RuntimeError(f"expected HTTP 2xx, got {read_status}")
    if len(read_body) < 1:
        raise RuntimeError("empty write verification body")
    value = read_body[0]
    expected = int(data_hex, 16)
    if value != expected:
        raise RuntimeError(f"verification mismatch expected=0x{expected:02X} got=0x{value:02X}")
    return f"http_status={write_status} verified=0x{value:02X}"


def _runner_probe_write_address(runner_id: int) -> str:
    slot = (runner_id - 1) % PROBE_WRITE_RUNNER_SLOT_COUNT
    return f"0x{PROBE_WRITE_ADDRESSES[slot]:04X}"


def _has_multiple_runners(context: ProbeExecutionContext | None) -> bool:
    if context is None or context.state is None:
        return False
    return getattr(context.state, "runner_count", 1) > 1


def surface_operations(
    surface: ProbeSurface,
    *,
    runner_id: int = 1,
    concurrent_multi_runner: bool = False,
    shared_state: Any | None = None,
) -> tuple[tuple[str, callable], ...]:
    read_operations = (
        ("get_version", lambda settings: generic_read(settings, "/v1/version")),
        ("get_info", lambda settings: generic_read(settings, "/v1/info")),
        ("get_configs", lambda settings: generic_read(settings, "/v1/configs")),
        ("get_config_audio_mixer", lambda settings: generic_read(settings, HTTP_AUDIO_MIXER_CATEGORY_PATH)),
        ("get_vol_ultisid_1", lambda settings: read_audio_mixer_item(settings, shared_state=shared_state)),
        ("get_drives", lambda settings: generic_read(settings, "/v1/drives")),
        ("get_files_temp", lambda settings: generic_read(settings, "/v1/files?path=/Temp")),
        ("mem_read_zero_page", lambda settings: memory_read(settings, "0x0000", 16)),
        ("mem_read_screen_ram", lambda settings: memory_read(settings, "0x0400", 16)),
        ("mem_read_io_area", lambda settings: memory_read(settings, "0xD000", 16)),
        ("mem_read_debug_register", lambda settings: memory_read(settings, "0xD7FF", 1)),
    )
    if surface == ProbeSurface.SMOKE:
        return (("get_version_smoke", lambda settings: generic_read(settings, "/v1/version")),)
    if surface == ProbeSurface.READ:
        return read_operations
    probe_write_address = _runner_probe_write_address(runner_id)
    operations = read_operations + (
        ("mem_write_probe_a5", lambda settings: memory_write_verify(settings, probe_write_address, "A5")),
        ("mem_write_probe_5a", lambda settings: memory_write_verify(settings, probe_write_address, "5A")),
        ("set_vol_ultisid_1_0_db", lambda settings: write_audio_mixer_item(settings, "0 dB", shared_state=shared_state)),
        ("set_vol_ultisid_1_plus_1_db", lambda settings: write_audio_mixer_item(settings, "+1 dB", shared_state=shared_state)),
    )
    return operations


# A syntactically valid request *prefix* that is never terminated (no blank
# line), so the server accepts the connection and starts reading, then observes
# the abort on a subsequent read instead of a clean request.
HTTP_INCOMPLETE_PARTIAL_REQUEST = b"GET /v1/version HTTP/1.1\r\nHost: u64\r\n"


def run_probe_incomplete(settings: RuntimeSettings) -> ProbeOutcome:
    """Hostile HTTP probe: open a connection, send a partial (never-terminated)
    request, then abort with a TCP RST (SO_LINGER 0).

    This drives the server's recv() on an already-accepted connection to return
    an error (ECONNRESET, i.e. < 0) rather than a clean EOF, exercising its
    read-error teardown path under connection churn. A correct server frees the
    client slot on this path; one that does not will exhaust its connection
    table after a handful of aborts and stop accepting new connections.
    """
    started_at = time.perf_counter_ns()
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.settimeout(3)
        sock.connect((settings.host, settings.http_port))
        try:
            sock.sendall(HTTP_INCOMPLETE_PARTIAL_REQUEST)
        except OSError:
            pass  # peer may have already torn down; the abort below still applies
        # SO_LINGER {on=1, linger=0}: close() emits a TCP RST instead of a FIN,
        # so the peer's recv() returns an error rather than a clean EOF.
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("OK", "incomplete sent=partial close=RST", elapsed_ms)
    except Exception as error:
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        # A peer reset seen mid-abort (ECONNRESET / broken pipe) just means the
        # server tore the connection down before our own close; from our side the
        # abort still happened, so report OK. Anything else - notably a refused or
        # timed-out connect() - means we could not open the connection at all;
        # report FAIL, which is the signal we want (the listener has stopped
        # accepting, i.e. the very wedge this probe provokes).
        if is_expected_incomplete_disconnect(error):
            return ProbeOutcome("OK", "incomplete expected_disconnect", elapsed_ms)
        return ProbeOutcome("FAIL", f"http incomplete failed: {error}", elapsed_ms)
    finally:
        try:
            sock.close()  # with SO_LINGER {1,0} set above, this emits the RST
        except OSError:
            pass


def run_probe(settings: RuntimeSettings, correctness, *, context: ProbeExecutionContext | None = None) -> ProbeOutcome:
    if correctness == ProbeCorrectness.INCOMPLETE:
        # Surface-independent: a partial-request + RST abort that churns the
        # server's connection-error path regardless of read/readwrite surface.
        return run_probe_incomplete(settings)
    if context is not None:
        operations = surface_operations(
            context.surface,
            runner_id=context.runner_id,
            concurrent_multi_runner=_has_multiple_runners(context),
            shared_state=context.state,
        )
        index = select_operation_index(context, len(operations))
        op_name, operation = operations[index]
        started_at = time.perf_counter_ns()
        try:
            detail = run_surface_operation("http", operation, settings)
            elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
            return ProbeOutcome("OK", surface_detail(context.surface, op_name, detail), elapsed_ms)
        except Exception as error:
            elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
            return ProbeOutcome("FAIL", surface_detail(context.surface, op_name, str(error)), elapsed_ms)

    del correctness
    conn = http.client.HTTPConnection(settings.host, settings.http_port, timeout=8)
    started_at = time.perf_counter_ns()
    try:
        conn.request("GET", request_path(settings.http_path), headers=request_headers(settings))
        response = conn.getresponse()
        body = response.read()
        if not 200 <= response.status < 300:
            raise RuntimeError(f"expected HTTP 2xx, got {response.status}")
        if not body:
            raise RuntimeError("empty HTTP body")
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("OK", f"HTTP {response.status} body_bytes={len(body)}", elapsed_ms)
    except Exception as error:
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("FAIL", f"http failed: {error}", elapsed_ms)
    finally:
        conn.close()
