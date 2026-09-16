from __future__ import annotations

import json
import os
import socket
import time
from collections.abc import Callable

from connection_runtime import (
    ProbeExecutionContext,
    ProbeOutcome,
    ProbeSurface,
    RuntimeSettings,
    run_selected_surface_operation,
)


IDENT_PORT = 64
IDENT_TIMEOUT_S = 1.0
IDENT_RETRY_COUNT = 3


def ident_nonce() -> str:
    return f"vivipi-{os.getpid()}-{time.monotonic_ns()}"


def _parse_ident_payload(payload: bytes, nonce: str) -> dict[str, str]:
    try:
        response = json.loads(payload.decode("utf-8"))
    except Exception as error:
        raise RuntimeError(f"invalid ident JSON: {error}") from error
    if not isinstance(response, dict):
        raise RuntimeError("invalid ident payload")
    for key in ("product", "firmware_version", "hostname", "your_string"):
        value = response.get(key)
        if not isinstance(value, str) or not value.strip():
            raise RuntimeError(f"missing ident field: {key}")
    if response["your_string"] != nonce:
        raise RuntimeError("ident echo mismatch")
    return response


def identify_json(settings: RuntimeSettings) -> str:
    nonce = ident_nonce()
    response: dict[str, str] | None = None
    last_error: RuntimeError | None = None
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        for _attempt in range(IDENT_RETRY_COUNT):
            sock.sendto(f"json{nonce}".encode(), (settings.host, IDENT_PORT))
            deadline = time.monotonic() + IDENT_TIMEOUT_S
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                sock.settimeout(remaining)
                try:
                    candidate_payload, _address = sock.recvfrom(4096)
                except TimeoutError:
                    break
                try:
                    response = _parse_ident_payload(candidate_payload, nonce)
                except RuntimeError as error:
                    last_error = error
                    continue
                break
            if response is not None:
                break
    finally:
        sock.close()
    if response is None:
        if last_error is not None:
            raise last_error
        raise RuntimeError("ident request timed out")
    return f"product={response['product']} hostname={response['hostname']}"


def surface_operations(surface: ProbeSurface) -> tuple[tuple[str, Callable[[RuntimeSettings], str]], ...]:
    del surface
    return (("ident_json", identify_json),)


def run_probe(settings: RuntimeSettings, correctness, *, context: ProbeExecutionContext | None = None) -> ProbeOutcome:
    del correctness
    if context is not None:
        return run_selected_surface_operation(
            "ident", context, settings, surface_operations(context.surface))

    started_at = time.perf_counter_ns()
    try:
        detail = identify_json(settings)
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("OK", detail, elapsed_ms)
    except Exception as error:
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("FAIL", f"ident failed: {error}", elapsed_ms)
