from __future__ import annotations

import socket
import struct
import time
from typing import Callable

from connection_runtime import (
    ProbeExecutionContext,
    ProbeOutcome,
    ProbeSurface,
    RuntimeSettings,
    run_surface_operation,
    select_operation_index,
    surface_detail,
)


CONTROL_PORT = 64
SOCKET_CMD_IDENTIFY = 0xFF0E
SOCKET_CMD_AUTHENTICATE = 0xFF1F
SOCKET_CMD_READFLASH = 0xFF75
SOCKET_CMD_DEBUG_REG = 0xFF76
DMA_TIMEOUT_S = 1.0
FLASH_METADATA_PAGE_SIZE = 0
FLASH_METADATA_PAGE_COUNT = 1


def command_frame(command: int, payload: bytes = b"") -> bytes:
    return struct.pack("<HH", command, len(payload)) + payload


def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise RuntimeError(f"short dma read expected={size} got={len(chunks)}")
        chunks.extend(chunk)
    return bytes(chunks)


def authenticate_socket(sock: socket.socket, password: str) -> None:
    if not password:
        return
    sock.sendall(command_frame(SOCKET_CMD_AUTHENTICATE, password.encode("utf-8")))
    if recv_exact(sock, 1) != b"\x01":
        raise RuntimeError("dma authentication failed")


def open_socket(settings: RuntimeSettings) -> socket.socket:
    sock = socket.create_connection((settings.host, CONTROL_PORT), timeout=2)
    sock.settimeout(DMA_TIMEOUT_S)
    authenticate_socket(sock, settings.network_password)
    return sock


def identify(settings: RuntimeSettings) -> str:
    sock = open_socket(settings)
    try:
        return identify_from_socket(sock)
    finally:
        sock.close()


def read_debug_register(settings: RuntimeSettings) -> str:
    sock = open_socket(settings)
    try:
        return read_debug_register_from_socket(sock)
    finally:
        sock.close()


def identify_from_socket(sock: socket.socket) -> str:
    sock.sendall(command_frame(SOCKET_CMD_IDENTIFY))
    title_length = recv_exact(sock, 1)[0]
    if title_length < 1:
        raise RuntimeError("empty identify title")
    title = recv_exact(sock, title_length).decode("utf-8", "replace").strip()
    if not title:
        raise RuntimeError("empty identify title")
    return f"title={title}"


def read_debug_register_value_from_socket(sock: socket.socket) -> int:
    sock.sendall(command_frame(SOCKET_CMD_DEBUG_REG))
    return recv_exact(sock, 1)[0]


def read_debug_register_from_socket(sock: socket.socket) -> str:
    value = read_debug_register_value_from_socket(sock)
    return f"debug_reg=0x{value:02X}"


def write_debug_register_value_from_socket(sock: socket.socket, value: int) -> int:
    sock.sendall(command_frame(SOCKET_CMD_DEBUG_REG, bytes((value & 0xFF,))))
    return recv_exact(sock, 1)[0]


def read_flash_metadata_from_socket(sock: socket.socket, selector: int) -> int:
    sock.sendall(command_frame(SOCKET_CMD_READFLASH, bytes((selector & 0xFF,))))
    return int.from_bytes(recv_exact(sock, 4), "little")


def read_flash_page_size(settings: RuntimeSettings) -> str:
    sock = open_socket(settings)
    try:
        page_size = read_flash_metadata_from_socket(sock, FLASH_METADATA_PAGE_SIZE)
    finally:
        sock.close()
    if page_size < 1:
        raise RuntimeError("invalid flash page size")
    return f"flash_page_size={page_size}"


def read_flash_page_count(settings: RuntimeSettings) -> str:
    sock = open_socket(settings)
    try:
        page_count = read_flash_metadata_from_socket(sock, FLASH_METADATA_PAGE_COUNT)
    finally:
        sock.close()
    if page_count < 1:
        raise RuntimeError("invalid flash page count")
    return f"flash_pages={page_count}"


def write_restore_debug_register(settings: RuntimeSettings) -> str:
    sock = open_socket(settings)
    try:
        original = read_debug_register_value_from_socket(sock)
        candidate = original ^ 0x01
        echoed_original = write_debug_register_value_from_socket(sock, candidate)
        if echoed_original != original:
            raise RuntimeError(
                f"debug register write precondition mismatch expected=0x{original:02X} got=0x{echoed_original:02X}"
            )
        current = read_debug_register_value_from_socket(sock)
        if current != candidate:
            raise RuntimeError(f"debug register write verify mismatch expected=0x{candidate:02X} got=0x{current:02X}")
        echoed_candidate = write_debug_register_value_from_socket(sock, original)
        if echoed_candidate != candidate:
            raise RuntimeError(
                f"debug register restore precondition mismatch expected=0x{candidate:02X} got=0x{echoed_candidate:02X}"
            )
        restored = read_debug_register_value_from_socket(sock)
        if restored != original:
            raise RuntimeError(f"debug register restore verify mismatch expected=0x{original:02X} got=0x{restored:02X}")
    finally:
        sock.close()
    return f"debug_reg_restored=0x{restored:02X} temporary=0x{candidate:02X}"


def surface_operations(surface: ProbeSurface) -> tuple[tuple[str, Callable[[RuntimeSettings], str]], ...]:
    if surface == ProbeSurface.SMOKE:
        return (("dma_identify", identify),)
    if surface == ProbeSurface.READWRITE:
        return (
            ("dma_identify", identify),
            ("dma_debug_register", read_debug_register),
            ("dma_flash_page_size", read_flash_page_size),
            ("dma_flash_page_count", read_flash_page_count),
            ("dma_debug_register_write_restore", write_restore_debug_register),
        )
    return (
        ("dma_identify", identify),
        ("dma_debug_register", read_debug_register),
        ("dma_flash_page_size", read_flash_page_size),
        ("dma_flash_page_count", read_flash_page_count),
    )


def run_probe(settings: RuntimeSettings, correctness, *, context: ProbeExecutionContext | None = None) -> ProbeOutcome:
    del correctness
    if context is not None:
        operations = surface_operations(context.surface)
        index = select_operation_index(context, len(operations))
        op_name, operation = operations[index]
        started_at = time.perf_counter_ns()
        try:
            detail = run_surface_operation("dma", operation, settings)
            elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
            return ProbeOutcome("OK", surface_detail(context.surface, op_name, detail), elapsed_ms)
        except Exception as error:
            elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
            return ProbeOutcome("FAIL", surface_detail(context.surface, op_name, str(error)), elapsed_ms)

    started_at = time.perf_counter_ns()
    try:
        sock = open_socket(settings)
        try:
            identify_detail = identify_from_socket(sock)
            debug_detail = read_debug_register_from_socket(sock)
        finally:
            sock.close()
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("OK", f"{identify_detail} {debug_detail}", elapsed_ms)
    except Exception as error:
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("FAIL", f"dma failed: {error}", elapsed_ms)
