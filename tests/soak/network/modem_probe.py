from __future__ import annotations

import socket
import time
from collections.abc import Callable

from connection_runtime import (
    ProbeExecutionContext,
    ProbeOutcome,
    ProbeSurface,
    RuntimeSettings,
    run_surface_operation,
    select_operation_index,
    surface_detail,
)


MODEM_CONNECT_TIMEOUT_S = 2.0
MODEM_READ_TIMEOUT_S = 1.0


def read_banner(settings: RuntimeSettings) -> str:
    sock = socket.create_connection((settings.host, settings.modem_port), timeout=MODEM_CONNECT_TIMEOUT_S)
    try:
        sock.settimeout(MODEM_READ_TIMEOUT_S)
        chunks = bytearray()
        while True:
            try:
                chunk = sock.recv(4096)
            except TimeoutError:
                break
            if not chunk:
                break
            chunks.extend(chunk)
        text = chunks.decode("utf-8", "ignore").strip()
        if not text:
            return f"status=connected port={settings.modem_port}"
        lowered = text.lower()
        if "busy" in lowered:
            status = "busy"
        elif "offline" in lowered or "not running" in lowered:
            status = "offline"
        else:
            status = "connected"
        return f"status={status} port={settings.modem_port} banner_bytes={len(chunks)}"
    finally:
        sock.close()


def surface_operations(surface: ProbeSurface) -> tuple[tuple[str, Callable[[RuntimeSettings], str]], ...]:
    del surface
    return (("modem_banner", read_banner),)


def run_probe(settings: RuntimeSettings, correctness, *, context: ProbeExecutionContext | None = None) -> ProbeOutcome:
    del correctness
    if context is not None:
        operations = surface_operations(context.surface)
        index = select_operation_index(context, len(operations))
        op_name, operation = operations[index]
        started_at = time.perf_counter_ns()
        try:
            detail = run_surface_operation("modem", operation, settings)
            elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
            return ProbeOutcome("OK", surface_detail(context.surface, op_name, detail), elapsed_ms)
        except Exception as error:
            elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
            return ProbeOutcome("FAIL", surface_detail(context.surface, op_name, str(error)), elapsed_ms)

    started_at = time.perf_counter_ns()
    try:
        detail = read_banner(settings)
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("OK", detail, elapsed_ms)
    except Exception as error:
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("FAIL", f"modem failed: {error}", elapsed_ms)
