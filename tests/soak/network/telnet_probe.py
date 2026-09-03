from __future__ import annotations

import atexit
import os
import re
import select
import socket
import subprocess
import threading
import time
from dataclasses import dataclass
from typing import Any, Protocol
from collections.abc import Callable

import http_probe
from connection_runtime import (
    ProbeCorrectness,
    ProbeExecutionContext,
    ProbeOutcome,
    ProbeSurface,
    RuntimeSettings,
    has_multiple_runners,
    run_incomplete_surface_operation,
    run_surface_operation,
    select_operation_index,
    surface_detail,
)


TELNET_IDLE_TIMEOUT_S = 0.12
TELNET_POST_DATA_IDLE_TIMEOUT_S = 0.02
TELNET_COMMAND_RESPONSE_TIMEOUT_S = 0.12
TELNET_MAX_EMPTY_READS = 1
TELNET_LOGIN_INITIAL_TIMEOUT_S = 1.0
TELNET_LOGIN_QUIET_TIMEOUT_S = 0.2
TELNET_LOGIN_MAX_EMPTY_READS = 4
TELNET_AUTH_ENTER = b"\r\n"
IAC = 255
DONT = 254
DO = 253
WONT = 252
WILL = 251
SB = 250
SE = 240
TELNET_KEY_F2 = b"\x1b[12~"
TELNET_KEY_DOWN = b"\x1b[B"
TELNET_KEY_LEFT = b"\x1b[D"
TELNET_KEY_RIGHT = b"\x1b[C"
TELNET_KEY_UP = b"\x1b[A"
TELNET_KEY_INCREASE = b"+"
TELNET_KEY_DECREASE = b"-"
TELNET_KEY_ESC = b"\x1b"
TELNET_KEY_ENTER = b"\r"
TELNET_FAILURE_MARKERS = (b"incorrect", b"failed", b"denied", b"invalid")
TELNET_SAVE_FLASH_MARKERS = ("save changes to flash", "yes", "no")
TELNET_PASSWORD_PROMPT = "password:"
AUDIO_MIXER_WRITE_VALUE_PATTERN = re.compile(r"Vol UltiSid 1\s+(OFF|[+-]?\d+ dB|\d+ dB)")


class TelnetSocket(Protocol):
    def sendall(self, data: bytes) -> None: ...

    def recv(self, bufsize: int) -> bytes: ...

    def close(self) -> None: ...


class PrefetchedTelnetSocket:
    def __init__(self, sock: TelnetSocket, prefetched: bytes):
        self._sock = sock
        self._prefetched = prefetched

    def sendall(self, data: bytes) -> None:
        self._sock.sendall(data)

    def recv(self, bufsize: int) -> bytes:
        if self._prefetched:
            chunk = self._prefetched[:bufsize]
            self._prefetched = self._prefetched[bufsize:]
            return chunk
        return self._sock.recv(bufsize)

    def close(self) -> None:
        self._sock.close()

    def __getattr__(self, name: str):
        return getattr(self._sock, name)


@dataclass
class TelnetRunnerSession:
    sock: TelnetSocket
    view_state: str = "unknown"
    last_text: str = ""
    menu_focus: str = "unknown"


_TELNET_SESSION_LOCK = threading.Lock()
_TELNET_RUNNER_SESSIONS: dict[int, TelnetRunnerSession] = {}
_TELNET_CLEANUP_REGISTERED = False


def register_cleanup() -> None:
    global _TELNET_CLEANUP_REGISTERED
    with _TELNET_SESSION_LOCK:
        if not _TELNET_CLEANUP_REGISTERED:
            atexit.register(cleanup_sessions)
            _TELNET_CLEANUP_REGISTERED = True


def close_socket(sock: TelnetSocket | None) -> None:
    if sock is None:
        return
    try:
        sock.close()
    except OSError:
        pass


def cleanup_sessions() -> None:
    with _TELNET_SESSION_LOCK:
        sessions = tuple(_TELNET_RUNNER_SESSIONS.values())
        _TELNET_RUNNER_SESSIONS.clear()
    for session in sessions:
        close_socket(session.sock)


def drop_session(runner_id: int) -> None:
    with _TELNET_SESSION_LOCK:
        session = _TELNET_RUNNER_SESSIONS.pop(runner_id, None)
    if session is not None:
        close_socket(session.sock)


def peek_session(runner_id: int) -> TelnetRunnerSession | None:
    with _TELNET_SESSION_LOCK:
        return _TELNET_RUNNER_SESSIONS.get(runner_id)


def get_session(settings: RuntimeSettings, runner_id: int) -> TelnetRunnerSession:
    with _TELNET_SESSION_LOCK:
        existing = _TELNET_RUNNER_SESSIONS.get(runner_id)
    if existing is not None:
        return existing
    sock = connect(settings)
    session = TelnetRunnerSession(sock=sock)
    register_cleanup()
    with _TELNET_SESSION_LOCK:
        existing = _TELNET_RUNNER_SESSIONS.get(runner_id)
        if existing is not None:
            close_socket(sock)
            return existing
        _TELNET_RUNNER_SESSIONS[runner_id] = session
    return session


def looks_like_output(value: str) -> bool:
    stripped = value.strip()
    if not stripped:
        return False
    lowered = stripped.lower()
    if any(marker.decode("utf-8") in lowered for marker in TELNET_FAILURE_MARKERS):
        return False
    return any(character.isalnum() for character in stripped) or stripped[-1:] in ">#$%"


def contains_any(value: bytes, markers: tuple[bytes, ...]) -> bool:
    lowered = value.lower()
    return any(marker in lowered for marker in markers)


def normalize_text(text: str) -> str:
    return " ".join(text.split()).strip().lower()


def classify_view_state(text: str) -> tuple[str, str]:
    lowered = text.lower()
    if "vol ultisid 1" in lowered:
        return "audio_mixer", "audio_mixer"
    if "video configuration" in lowered and "audio mixer" in lowered and "speaker settings" not in lowered:
        return "audio_video_menu", "audio_mixer"
    if "audio mixer" in lowered and "speaker settings" in lowered:
        if "video configuration" in lowered:
            return "menu", "video_configuration"
        return "menu", "audio_mixer"
    return "unknown", "unknown"


def strip_vt_text(value: bytes) -> str:
    text = value.decode("utf-8", "ignore")
    cleaned: list[str] = []
    index = 0
    alt_charset = False
    while index < len(text):
        char = text[index]
        if char == "\x1b":
            if index + 1 >= len(text):
                break
            next_char = text[index + 1]
            if next_char == "[":
                index += 2
                while index < len(text) and not ("@" <= text[index] <= "~"):
                    index += 1
                index += 1
                continue
            if next_char == "(":
                if index + 2 < len(text):
                    alt_charset = text[index + 2] == "0"
                    index += 3
                    continue
                break
            if next_char == "c":
                index += 2
                continue
            index += 2
            continue
        if ord(char) < 32:
            cleaned.append(" ")
            index += 1
            continue
        if alt_charset and char in {"l", "k", "m", "j", "x", "q", "t", "u", "v", "w", "n"}:
            cleaned.append(" ")
            index += 1
            continue
        cleaned.append(char)
        index += 1
    return " ".join("".join(cleaned).split())


def connect(settings: RuntimeSettings) -> TelnetSocket:
    sock = socket.create_connection((settings.host, settings.telnet_port), timeout=2)
    sock.settimeout(TELNET_IDLE_TIMEOUT_S)
    prefetched = authenticate_if_needed(sock, settings)
    if prefetched:
        return PrefetchedTelnetSocket(sock, prefetched)
    return sock


def _looks_like_password_echo(text: str) -> bool:
    stripped = text.strip()
    return bool(stripped) and set(stripped) <= {"*"}


def _merge_post_login_text(existing: str, chunk: str) -> str:
    chunk = chunk.strip()
    if not chunk:
        return existing
    if _looks_like_password_echo(chunk):
        return existing
    if not existing or _looks_like_password_echo(existing):
        return chunk
    return f"{existing} {chunk}".strip()


def authenticate_if_needed(sock: TelnetSocket, settings: RuntimeSettings) -> bytes:
    if not settings.network_password:
        return b""
    prompt = read_until_idle(sock, max_empty_reads=2)
    if TELNET_PASSWORD_PROMPT not in prompt.lower():
        sock.sendall(TELNET_KEY_ENTER)
        prompt = read_until_idle(sock, max_empty_reads=2)
        if TELNET_PASSWORD_PROMPT not in prompt.lower():
            return b""
    sock.sendall(settings.network_password.encode("utf-8") + TELNET_AUTH_ENTER)
    response = ""
    for _ in range(2):
        chunk = read_until_idle(
            sock,
            max_empty_reads=TELNET_LOGIN_MAX_EMPTY_READS,
            initial_timeout_s=TELNET_LOGIN_INITIAL_TIMEOUT_S,
            quiet_timeout_s=TELNET_LOGIN_QUIET_TIMEOUT_S,
        )
        response = _merge_post_login_text(response, chunk)
        lowered = response.lower()
        if any(marker.decode("utf-8") in lowered for marker in TELNET_FAILURE_MARKERS) or TELNET_PASSWORD_PROMPT in lowered:
            snippet = response[:120].replace("\r", " ").replace("\n", " ").strip()
            raise RuntimeError(f"login failed for {settings.host}: password prompt persisted or failure marker seen; last response: {snippet!r}")
        if response and not _looks_like_password_echo(response):
            return response.encode("utf-8")
    return b""


def _wait_for_readable(sock: TelnetSocket, timeout_s: float) -> bool:
    fileno = getattr(sock, "fileno", None)
    if not callable(fileno):
        return False
    try:
        ready, _write_ready, _error_ready = select.select([sock], [], [], timeout_s)
    except (OSError, TypeError, ValueError):
        return False
    return bool(ready)


def collect_visible(handle: TelnetSocket, chunk: bytes) -> bytes:
    visible = bytearray()
    index = 0
    while index < len(chunk):
        byte = chunk[index]
        if byte == IAC:
            if index + 1 >= len(chunk):
                break
            command = chunk[index + 1]
            if command == IAC:
                visible.append(IAC)
                index += 2
                continue
            if command in (DO, DONT, WILL, WONT):
                if index + 2 >= len(chunk):
                    break
                option = chunk[index + 2]
                reply = bytes([IAC, WONT if command in (DO, DONT) else DONT, option])
                handle.sendall(reply)
                index += 3
                continue
            if command == SB:
                index += 2
                while index + 1 < len(chunk):
                    if chunk[index] == IAC and chunk[index + 1] == SE:
                        index += 2
                        break
                    index += 1
                continue
            index += 2
            continue
        visible.append(byte)
        index += 1
    return bytes(visible)


def read_until_idle(
    sock: TelnetSocket,
    *,
    max_empty_reads: int | None = None,
    initial_timeout_s: float | None = None,
    quiet_timeout_s: float | None = None,
) -> str:
    if max_empty_reads is None:
        max_empty_reads = TELNET_MAX_EMPTY_READS
    if initial_timeout_s is None:
        initial_timeout_s = TELNET_IDLE_TIMEOUT_S
    if quiet_timeout_s is None:
        quiet_timeout_s = TELNET_POST_DATA_IDLE_TIMEOUT_S
    use_select = callable(getattr(sock, "fileno", None))
    visible = bytearray()
    empty_reads = 0
    saw_data = False
    while empty_reads < max_empty_reads:
        timeout_s = quiet_timeout_s if saw_data else initial_timeout_s
        if use_select:
            if not _wait_for_readable(sock, timeout_s):
                empty_reads += 1
                continue
        else:
            # Fallback for test doubles and sockets without a selectable file descriptor.
            settimeout = getattr(sock, "settimeout", None)
            if callable(settimeout):
                settimeout(timeout_s)
        try:
            chunk = sock.recv(4096)
        except TimeoutError:
            empty_reads += 1
            continue
        if not chunk:
            break
        saw_data = True
        empty_reads = 0
        visible.extend(collect_visible(sock, chunk))
    text = strip_vt_text(bytes(visible))
    if text and any(marker.decode("utf-8") in text.lower() for marker in TELNET_FAILURE_MARKERS):
        raise RuntimeError("telnet failure marker present")
    return text


def require_text(text: str, *markers: str) -> str:
    if not text:
        raise RuntimeError("empty telnet text")
    lowered = text.lower()
    missing = [marker for marker in markers if marker.lower() not in lowered]
    if missing:
        raise RuntimeError(f"missing telnet text: {', '.join(missing)}")
    return text


def open_menu(sock) -> str:
    read_until_idle(sock)
    last_text = ""
    for _ in range(3):
        sock.sendall(TELNET_KEY_F2)
        text = read_until_idle(sock, max_empty_reads=2, initial_timeout_s=TELNET_IDLE_TIMEOUT_S)
        last_text = text or last_text
        lowered = text.lower()
        if "audio mixer" in lowered and "speaker settings" in lowered:
            return f"visible_bytes={len(text.encode())}"
        for _repaint in range(2):
            sock.sendall(TELNET_KEY_ENTER)
            text = read_until_idle(sock, max_empty_reads=2, initial_timeout_s=TELNET_IDLE_TIMEOUT_S)
            last_text = text or last_text
            lowered = text.lower()
            if "audio mixer" in lowered and "speaker settings" in lowered:
                return f"visible_bytes={len(text.encode())}"
    text = require_text(last_text, "Audio Mixer", "Speaker Settings")
    return f"visible_bytes={len(text.encode())}"


def banner(sock) -> str:
    initial_text = read_until_idle(sock)
    if initial_text:
        return f"banner_bytes={len(require_text(initial_text).encode())}"
    sock.sendall(b"\r\n")
    text = require_text(read_until_idle(sock))
    return f"banner_bytes={len(text.encode())}"


def smoke_connect(sock) -> str:
    text = read_until_idle(sock, max_empty_reads=1)
    if not text:
        return "connected"
    return f"visible_bytes={len(text.encode())}"


def session_capture(session: TelnetRunnerSession, text: str, view_state: str | None = None) -> str:
    if text:
        session.last_text = text
        inferred_view, inferred_focus = classify_view_state(text)
        if view_state is None and inferred_view != "unknown":
            session.view_state = inferred_view
            session.menu_focus = inferred_focus
    if view_state is not None:
        session.view_state = view_state
    return text


def session_read(
    session: TelnetRunnerSession,
    *,
    max_empty_reads: int = 1,
    view_state: str | None = None,
    initial_timeout_s: float | None = None,
    quiet_timeout_s: float | None = None,
) -> str:
    text = read_until_idle(
        session.sock,
        max_empty_reads=max_empty_reads,
        initial_timeout_s=initial_timeout_s,
        quiet_timeout_s=quiet_timeout_s,
    )
    return session_capture(session, text, view_state=view_state)


def session_send(
    session: TelnetRunnerSession,
    payload: bytes,
    *,
    view_state: str | None = None,
    initial_timeout_s: float = TELNET_COMMAND_RESPONSE_TIMEOUT_S,
) -> str:
    session.sock.sendall(payload)
    return session_read(session, max_empty_reads=1, view_state=view_state, initial_timeout_s=initial_timeout_s)


def session_has_menu(session: TelnetRunnerSession) -> bool:
    lowered = session.last_text.lower()
    return "audio mixer" in lowered and "speaker settings" in lowered


def session_has_audio_video_menu(session: TelnetRunnerSession) -> bool:
    lowered = session.last_text.lower()
    return "video configuration" in lowered and "audio mixer" in lowered and "speaker settings" not in lowered


def session_has_audio_mixer(session: TelnetRunnerSession) -> bool:
    return "vol ultisid 1" in session.last_text.lower()


def session_smoke_connect(session: TelnetRunnerSession) -> str:
    text = session_read(session, max_empty_reads=1, view_state=session.view_state)
    if not text:
        if session.last_text:
            return f"visible_bytes={len(session.last_text.encode())}"
        session.view_state = "home"
        return "connected"
    return f"visible_bytes={len(text.encode())}"


def session_open_menu(session: TelnetRunnerSession) -> str:
    if session.view_state == "menu" and session_has_menu(session):
        return f"visible_bytes={len(session.last_text.encode())}"
    if session.view_state == "audio_video_menu" and session_has_audio_video_menu(session):
        return f"visible_bytes={len(session.last_text.encode())}"
    if session.view_state == "audio_mixer" and session_has_audio_mixer(session):
        text = session_send(session, TELNET_KEY_LEFT)
        if text:
            lowered = text.lower()
            if "audio mixer" in lowered and "speaker settings" in lowered:
                session.last_text = text
                session.view_state = "menu"
                session.menu_focus = "audio_mixer"
                return f"visible_bytes={len(text.encode())}"
        session.view_state = "unknown"
        session.menu_focus = "unknown"
    session_read(session, max_empty_reads=1, view_state=session.view_state)
    last_text = session.last_text
    for attempt in range(3):
        if attempt > 0:
            reset_to_home(session.sock)
            session.view_state = "unknown"
            session.menu_focus = "unknown"
            session.last_text = ""
            session_read(session, max_empty_reads=1, view_state=session.view_state)
            last_text = session.last_text or last_text
        elif not last_text:
            wake_text = session_send(session, TELNET_KEY_ENTER, initial_timeout_s=TELNET_IDLE_TIMEOUT_S)
            last_text = wake_text or last_text
        text = session_send(session, TELNET_KEY_F2, initial_timeout_s=TELNET_IDLE_TIMEOUT_S)
        last_text = text or last_text
        if text and (session_has_menu(session) or session_has_audio_video_menu(session)):
            if session_has_menu(session):
                session.view_state = "menu"
                session.menu_focus = "video_configuration"
            else:
                session.view_state = "audio_video_menu"
                session.menu_focus = "audio_mixer"
            return f"visible_bytes={len(text.encode())}"
        if not text:
            for _repaint in range(2):
                text = session_send(session, TELNET_KEY_ENTER, initial_timeout_s=TELNET_IDLE_TIMEOUT_S)
                last_text = text or last_text
                if text and (session_has_menu(session) or session_has_audio_video_menu(session)):
                    break
        if text and not (session_has_menu(session) or session_has_audio_video_menu(session)):
            tail = session_read(session, max_empty_reads=2, view_state=session.view_state)
            if tail:
                text = f"{text} {tail}" if text else tail
                session_capture(session, text)
                last_text = text
        if text and (session_has_menu(session) or session_has_audio_video_menu(session)):
            if session_has_menu(session):
                session.view_state = "menu"
                session.menu_focus = "video_configuration"
            else:
                session.view_state = "audio_video_menu"
                session.menu_focus = "audio_mixer"
            return f"visible_bytes={len(text.encode())}"
    text = require_text(last_text, "Audio Mixer", "Speaker Settings")
    session.last_text = text
    session.view_state = "menu"
    session.menu_focus = "video_configuration"
    return f"visible_bytes={len(text.encode())}"


def session_open_audio_mixer(session: TelnetRunnerSession) -> str:
    if session.view_state == "audio_mixer" and session_has_audio_mixer(session):
        return session.last_text
    last_error: RuntimeError | None = None
    for attempt in range(3):
        if attempt > 0 or session.view_state != "unknown":
            reset_to_home(session.sock)
            session.view_state = "unknown"
            session.menu_focus = "unknown"
            session.last_text = ""
        if not session.last_text:
            session_read(session, max_empty_reads=1, view_state=session.view_state)
        text = session.last_text
        try:
            for payload in (TELNET_KEY_F2, TELNET_KEY_DOWN, TELNET_KEY_ENTER):
                text = session_send(session, payload)
                if "vol ultisid 1" in text.lower():
                    break
            if "vol ultisid 1" not in text.lower():
                tail = session_read(session, max_empty_reads=1, view_state=session.view_state)
                if tail:
                    text = f"{text} {tail}" if text else tail
                    session_capture(session, text)
            text = require_text(text, "Vol UltiSid 1")
            session.last_text = text
            session.view_state = "audio_mixer"
            session.menu_focus = "audio_mixer"
            return text
        except RuntimeError as error:
            last_error = error
        session.view_state = "unknown"
        session.menu_focus = "unknown"
        session.last_text = ""
    if last_error is not None:
        raise last_error
    raise RuntimeError("unable to open Audio Mixer")


def session_refresh_audio_mixer(session: TelnetRunnerSession) -> str:
    if session.view_state == "audio_mixer" and session_has_audio_mixer(session):
        session_open_menu(session)
    return session_open_audio_mixer(session)


def session_extract_audio_mixer_value(session: TelnetRunnerSession, text: str) -> tuple[str, str]:
    try:
        return text, extract_audio_mixer_write_value(text)
    except RuntimeError:
        tail = session_read(session, max_empty_reads=2, view_state=session.view_state)
        combined = text + tail if tail else text
        try:
            session.last_text = combined
            return combined, extract_audio_mixer_write_value(combined)
        except RuntimeError:
            session.view_state = "unknown"
            session.menu_focus = "unknown"
            reopened = session_open_audio_mixer(session)
            return reopened, extract_audio_mixer_write_value(reopened)


def session_read_audio_mixer_item(session: TelnetRunnerSession, *, shared_state: Any | None = None) -> str:
    with http_probe.audio_mixer_shared_lock(shared_state):
        text = session_refresh_audio_mixer(session)
        _text, current = session_extract_audio_mixer_value(session, text)
        normalized_current = http_probe.remember_audio_mixer_value(shared_state, current)
        return f"current={normalized_current}"


def audio_mixer_write_right_steps(settings: RuntimeSettings, current: str, target: str) -> int:
    _current_value, values, _body_bytes = http_probe.audio_mixer_item_state(settings)
    normalized_values = tuple(http_probe.normalize_audio_mixer_value(value) for value in values)
    normalized_current = http_probe.normalize_audio_mixer_value(current)
    normalized_target = http_probe.normalize_audio_mixer_value(target)
    if normalized_current not in normalized_values:
        raise RuntimeError(f"unsupported Audio Mixer write current value: {current}")
    if normalized_target not in normalized_values:
        raise RuntimeError(f"unsupported Audio Mixer write target value: {target}")
    current_index = normalized_values.index(normalized_current)
    target_index = normalized_values.index(normalized_target)
    return (target_index - current_index) % len(normalized_values)


def audio_mixer_adjustment_sequence(settings: RuntimeSettings, current: str, target: str) -> tuple[bytes, int]:
    _current_value, values, _body_bytes = http_probe.audio_mixer_item_state(settings)
    normalized_values = tuple(http_probe.normalize_audio_mixer_value(value) for value in values)
    normalized_current = http_probe.normalize_audio_mixer_value(current)
    normalized_target = http_probe.normalize_audio_mixer_value(target)
    if normalized_current not in normalized_values:
        raise RuntimeError(f"unsupported Audio Mixer write current value: {current}")
    if normalized_target not in normalized_values:
        raise RuntimeError(f"unsupported Audio Mixer write target value: {target}")
    current_index = normalized_values.index(normalized_current)
    target_index = normalized_values.index(normalized_target)
    if target_index < current_index:
        return TELNET_KEY_DECREASE, current_index - target_index
    return TELNET_KEY_INCREASE, target_index - current_index


def is_save_flash_dialog(text: str) -> bool:
    lowered = text.lower()
    return all(marker in lowered for marker in TELNET_SAVE_FLASH_MARKERS)


def is_post_audio_mixer_state(text: str) -> bool:
    lowered = text.lower()
    return bool(lowered.strip()) and "vol ultisid 1" not in lowered and not is_save_flash_dialog(text)


def session_save_changes_to_flash(session: TelnetRunnerSession) -> str:
    first_left = session_send(session, TELNET_KEY_LEFT, view_state="unknown")
    if is_save_flash_dialog(first_left):
        confirmed = session_send(session, TELNET_KEY_ENTER, view_state="unknown")
        return confirmed or first_left
    if is_post_audio_mixer_state(first_left):
        return first_left

    second_left = session_send(session, TELNET_KEY_LEFT, view_state="unknown")
    if is_save_flash_dialog(second_left):
        confirmed = session_send(session, TELNET_KEY_ENTER, view_state="unknown")
        return confirmed or second_left
    if is_post_audio_mixer_state(second_left):
        return second_left

    dialog_text = second_left if is_save_flash_dialog(second_left) else first_left
    tail = session_read(session, max_empty_reads=2, view_state="unknown")
    if tail:
        dialog_text = f"{dialog_text} {tail}".strip() if dialog_text else tail
    if is_save_flash_dialog(dialog_text):
        confirmed = session_send(session, TELNET_KEY_ENTER, view_state="unknown")
        return confirmed or dialog_text
    if is_post_audio_mixer_state(dialog_text):
        return dialog_text
    raise RuntimeError("missing telnet text: Save changes to Flash")


def authoritative_audio_mixer_value(settings: RuntimeSettings, *, shared_state: Any | None = None) -> str:
    current, _values, _body_bytes = http_probe.audio_mixer_item_state(settings)
    return http_probe.remember_audio_mixer_value(shared_state, current)


def session_write_audio_mixer_item(settings: RuntimeSettings, session: TelnetRunnerSession, target: str, *, shared_state: Any | None = None) -> str:
    with http_probe.audio_mixer_shared_lock(shared_state):
        text = session_refresh_audio_mixer(session)
        text, current = session_extract_audio_mixer_value(session, text)
        # Audio Mixer opens on Vol Master; Vol UltiSid 1 is the next row.
        text = session_send(session, TELNET_KEY_DOWN, view_state="audio_mixer")
        normalized_current = http_probe.remember_audio_mixer_value(shared_state, current)
        normalized_target = http_probe.normalize_audio_mixer_value(target)
        adjustment_key, steps = audio_mixer_adjustment_sequence(settings, current, target)
        if normalized_current != normalized_target:
            for _ in range(steps):
                text = session_send(session, adjustment_key, view_state="audio_mixer")
            text = session_save_changes_to_flash(session) or text
            http_probe.stage_audio_mixer_value(shared_state, normalized_target)
        normalized_authoritative = http_probe.verify_audio_mixer_value(settings, normalized_target, shared_state=shared_state)
        session.last_text = text
        session.view_state = "unknown"
        session.menu_focus = "unknown"
        adjustment = "increase" if adjustment_key == TELNET_KEY_INCREASE else "decrease"
        return f"from={normalized_current} to={normalized_authoritative} adjustment={adjustment} steps={steps}"


def abort_after_sequence(settings: RuntimeSettings, *payloads: bytes, read_initial: bool = True) -> str:
    sock = connect(settings)
    try:
        if read_initial:
            read_until_idle(sock, max_empty_reads=1)
        for payload in payloads:
            sock.sendall(payload)
        if not payloads:
            return "phase=connect_abort"
        return f"steps={len(payloads)} bytes={sum(len(payload) for payload in payloads)}"
    finally:
        close_socket(sock)


def initial_read_classify(settings: RuntimeSettings) -> str:
    sock = connect(settings)
    try:
        try:
            initial_raw = sock.recv(4096)
        except TimeoutError:
            initial_raw = b""
        transcript = collect_visible(sock, initial_raw) if initial_raw else b""
        if contains_any(transcript, TELNET_FAILURE_MARKERS):
            snippet = transcript.decode("utf-8", "replace")[:120].replace("\r", " ").replace("\n", " ").strip()
            raise RuntimeError(f"login failed for {settings.host}: failure marker in banner; transcript: {snippet!r}")
        if transcript:
            cleaned = transcript.decode("utf-8", "replace")
            if looks_like_output(cleaned):
                return "banner ready"
        return "connected"
    finally:
        close_socket(sock)


def incomplete_operations(surface: ProbeSurface) -> tuple[tuple[str, Callable[[RuntimeSettings], str]], ...]:
    if surface == ProbeSurface.SMOKE:
        return (("telnet_initial_read_classify", initial_read_classify),)
    operations = (
        ("telnet_f2_abort", lambda settings: abort_after_sequence(settings, TELNET_KEY_F2)),
        ("telnet_partial_f2_prefix_abort", lambda settings: abort_after_sequence(settings, TELNET_KEY_F2[:2])),
    )
    if surface == ProbeSurface.READ:
        return operations
    return (*operations, ("telnet_audio_mixer_abort", lambda settings: abort_after_sequence(settings, TELNET_KEY_F2, TELNET_KEY_DOWN, TELNET_KEY_ENTER)), ("telnet_right_arrow_abort", lambda settings: abort_after_sequence(settings, TELNET_KEY_F2, TELNET_KEY_RIGHT)), ("telnet_f2_abort", lambda settings: abort_after_sequence(settings, TELNET_KEY_F2)))


def run_open_surface_operation(
    settings: RuntimeSettings,
    runner_id: int,
    operation: Callable[[RuntimeSettings, TelnetRunnerSession], str],
) -> str:
    session = get_session(settings, runner_id)
    try:
        return operation(settings, session)
    finally:
        drop_session(runner_id)


def reset_to_home(sock) -> None:
    for _ in range(2):
        sock.sendall(TELNET_KEY_ESC)
        try:
            read_until_idle(sock, max_empty_reads=1)
        except RuntimeError:
            continue


def send_and_read(
    sock,
    payload: bytes,
    *,
    require_change: bool = False,
    initial_timeout_s: float = TELNET_COMMAND_RESPONSE_TIMEOUT_S,
) -> str:
    before = normalize_text(read_until_idle(sock, initial_timeout_s=initial_timeout_s)) if require_change else ""
    last_text = ""
    for _ in range(2):
        sock.sendall(payload)
        text = read_until_idle(sock, initial_timeout_s=initial_timeout_s)
        last_text = text
        if not require_change:
            return text
        normalized = normalize_text(text)
        if normalized and normalized != before:
            return text
    return last_text


def nav_down(sock) -> str:
    open_menu(sock)
    text = send_and_read(sock, TELNET_KEY_DOWN)
    text = send_and_read(sock, TELNET_KEY_DOWN) or text
    text = require_text(text, "Audio Mixer", "Speaker Settings")
    return f"visible_bytes={len(text.encode())}"


def open_audio_mixer(sock) -> str:
    open_menu(sock)
    send_and_read(sock, TELNET_KEY_DOWN, require_change=True)
    text = send_and_read(sock, TELNET_KEY_ENTER)
    return require_text(text, "Vol UltiSid 1")


def extract_audio_mixer_write_value(text: str) -> str:
    match = AUDIO_MIXER_WRITE_VALUE_PATTERN.search(text)
    if match is None:
        raise RuntimeError("missing Audio Mixer write value")
    return http_probe.normalize_audio_mixer_value(match.group(1))


def focus_audio_mixer_write_item(sock) -> tuple[str, str]:
    text = open_audio_mixer(sock)
    return text, extract_audio_mixer_write_value(text)


def read_audio_mixer_item(sock) -> str:
    _text, current = focus_audio_mixer_write_item(sock)
    return f"current={current}"


def write_audio_mixer_item(settings: RuntimeSettings, sock, target: str) -> str:
    text, current = focus_audio_mixer_write_item(sock)
    text = send_and_read(sock, TELNET_KEY_DOWN)
    adjustment_key, steps = audio_mixer_adjustment_sequence(settings, current, target)
    for _ in range(steps):
        text = send_and_read(sock, adjustment_key, require_change=True)
    first_left = send_and_read(sock, TELNET_KEY_LEFT)
    if is_save_flash_dialog(first_left):
        text = send_and_read(sock, TELNET_KEY_ENTER)
    elif is_post_audio_mixer_state(first_left):
        text = first_left
    else:
        second_left = send_and_read(sock, TELNET_KEY_LEFT)
        if is_save_flash_dialog(second_left):
            text = send_and_read(sock, TELNET_KEY_ENTER)
        elif is_post_audio_mixer_state(second_left):
            text = second_left
        else:
            dialog_text = second_left if is_save_flash_dialog(second_left) else first_left
            tail = read_until_idle(sock, max_empty_reads=2)
            if tail:
                dialog_text = f"{dialog_text} {tail}".strip() if dialog_text else tail
            if is_save_flash_dialog(dialog_text):
                text = send_and_read(sock, TELNET_KEY_ENTER)
            elif is_post_audio_mixer_state(dialog_text):
                text = dialog_text
            else:
                raise RuntimeError("missing telnet text: Save changes to Flash")
    updated = extract_audio_mixer_write_value(text)
    if updated != http_probe.normalize_audio_mixer_value(target):
        raise RuntimeError(f"verification mismatch expected={target} got={updated}")
    adjustment = "increase" if adjustment_key == TELNET_KEY_INCREASE else "decrease"
    return f"from={current} to={updated} adjustment={adjustment} steps={steps}"


def enter_speaker_settings(sock) -> str:
    open_menu(sock)
    send_and_read(sock, TELNET_KEY_DOWN)
    send_and_read(sock, TELNET_KEY_DOWN)
    text = send_and_read(sock, TELNET_KEY_ENTER)
    text = require_text(text, "Speaker Enable")
    return f"visible_bytes={len(text.encode())}"


def exit_menu(sock) -> str:
    open_menu(sock)
    text = send_and_read(sock, TELNET_KEY_LEFT)
    if "audio mixer" in text.lower() or "speaker settings" in text.lower():
        text = send_and_read(sock, TELNET_KEY_ESC)
    if text:
        require_text(text)
    return f"visible_bytes={len(text.encode())}"


def surface_operations(
    surface: ProbeSurface,
    *,
    concurrent_multi_runner: bool = False,
    shared_state: Any | None = None,
) -> tuple[tuple[str, Callable[[RuntimeSettings, TelnetRunnerSession], str]], ...]:
    read_operations = (
        ("telnet_smoke_connect", lambda settings, session: session_smoke_connect(session)),
        ("telnet_open_menu", lambda settings, session: session_open_menu(session)),
        (
            "telnet_open_audio_mixer",
            lambda settings, session: f"visible_bytes={len(session_open_audio_mixer(session).encode())}",
        ),
        ("telnet_read_vol_ultisid_1", lambda settings, session: session_read_audio_mixer_item(session, shared_state=shared_state)),
    )
    if surface == ProbeSurface.SMOKE:
        return (("telnet_smoke_connect", lambda settings, session: session_smoke_connect(session)),)
    if surface == ProbeSurface.READ:
        return read_operations
    return (*read_operations, ("set_vol_ultisid_1_0_db", lambda settings, session: session_write_audio_mixer_item(settings, session, "0 dB", shared_state=shared_state)), ("set_vol_ultisid_1_plus_1_db", lambda settings, session: session_write_audio_mixer_item(settings, session, "+1 dB", shared_state=shared_state)))


def run_probe(
    settings: RuntimeSettings,
    correctness: ProbeCorrectness,
    *,
    context: ProbeExecutionContext | None = None,
) -> ProbeOutcome:
    if correctness == ProbeCorrectness.VANISH:
        # True half-open reap lane. Single-shot regardless of surface/context:
        # it monopolises the whole session table for one fill/vanish/recover
        # cycle, so it must run single-runner, telnet-only.
        return run_vanish_probe(settings)
    if context is not None:
        surface = context.surface
        if correctness == ProbeCorrectness.OPEN:
            operations = surface_operations(surface, concurrent_multi_runner=has_multiple_runners(context), shared_state=context.state)
            index = select_operation_index(context, len(operations))
            op_name, operation = operations[index]
            return run_incomplete_surface_operation(
                "telnet",
                surface,
                op_name,
                lambda current_settings: run_open_surface_operation(current_settings, context.runner_id, operation),
                settings,
            )
        if correctness == ProbeCorrectness.INCOMPLETE:
            operations = incomplete_operations(surface)
            index = select_operation_index(context, len(operations))
            op_name, operation = operations[index]
            return run_incomplete_surface_operation("telnet", surface, op_name, operation, settings)
        operations = surface_operations(surface, concurrent_multi_runner=has_multiple_runners(context), shared_state=context.state)
        index = select_operation_index(context, len(operations))
        op_name, operation = operations[index]
        started_at = time.perf_counter_ns()
        try:
            def surface_operation(current_settings: RuntimeSettings) -> str:
                session = TelnetRunnerSession(sock=connect(current_settings))
                try:
                    return operation(current_settings, session)
                finally:
                    close_socket(session.sock)

            detail = run_surface_operation(
                "telnet",
                surface_operation,
                settings,
                on_error=lambda error: None,
            )
            elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
            return ProbeOutcome("OK", surface_detail(surface, op_name, detail), elapsed_ms)
        except Exception as error:
            elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
            return ProbeOutcome("FAIL", surface_detail(surface, op_name, str(error)), elapsed_ms)

    if correctness == ProbeCorrectness.INCOMPLETE:
        return run_probe_incomplete(settings)

    sock = None
    started_at = time.perf_counter_ns()
    try:
        sock = connect(settings)
        sock.sendall(b"\r\n")
        visible = bytearray()
        while True:
            try:
                chunk = sock.recv(4096)
            except TimeoutError:
                break
            if not chunk:
                break
            visible.extend(collect_visible(sock, chunk))
        text = bytes(visible).decode("utf-8", "ignore").strip()
        if not text:
            raise RuntimeError("empty telnet banner")
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("OK", f"banner_bytes={len(text.encode())}", elapsed_ms)
    except Exception as error:
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("FAIL", f"telnet failed: {error}", elapsed_ms)
    finally:
        if sock is not None:
            close_socket(sock)


def run_probe_incomplete(settings: RuntimeSettings) -> ProbeOutcome:
    started_at = time.perf_counter_ns()
    try:
        detail = initial_read_classify(settings)
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("OK", detail, elapsed_ms)
    except Exception as error:
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        if str(error) == "login failed":
            return ProbeOutcome("FAIL", "login failed", elapsed_ms)
        return ProbeOutcome("FAIL", f"telnet failed: {error}", elapsed_ms)


# ---------------------------------------------------------------------------
# True half-open (VANISH) telnet reap lane
#
# A telnet client that vanishes at the network level (WiFi sleep/drop, powered
# off phone, AP roam) never sends FIN/RST and never ACKs the device's keepalive
# probes, so without the reaping fix its run_remote() session polls recv() ->
# EAGAIN forever and permanently leaks a TELNET_MAX_SESSIONS slot. A plain
# close() sends FIN/RST and is torn down promptly - that is NOT this leak. We
# reproduce the real thing by sourcing the victim connections from a throwaway
# secondary IP alias, then deleting the alias so the host stops answering ARP
# and keepalive probes for that address. GREEN=device reaps the dead slots
# within the keepalive window; RED=it stays wedged past it. Needs CAP_NET_ADMIN
# (sudo -n ip) to add/remove the alias.
# ---------------------------------------------------------------------------

_VANISH_BUSY_MARKER = b"Too many connections"


def _vanish_run_ip(args: list[str], *, check: bool) -> subprocess.CompletedProcess:
    return subprocess.run(["sudo", "-n", "ip", *args], check=check, capture_output=True, text=True)


def _vanish_add_alias(iface: str, victim_ip: str) -> None:
    _vanish_run_ip(["addr", "add", f"{victim_ip}/24", "dev", iface], check=True)


def _vanish_del_alias(iface: str, victim_ip: str) -> subprocess.CompletedProcess:
    return _vanish_run_ip(["addr", "del", f"{victim_ip}/24", "dev", iface], check=False)


def _vanish_connect(host: str, port: int, *, source_ip: str | None = None, timeout: float = 4.0) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    if source_ip:
        sock.bind((source_ip, 0))
    sock.connect((host, port))
    return sock


# Tri-state reachability of the telnet listener from the primary IP.
_VANISH_FREE = "free"  # accepted the connection and served a banner (a slot is free)
_VANISH_BUSY = "busy"  # accepted the connection but reported the table full / wedged
_VANISH_UNREACHABLE = "unreachable"  # could not connect at all (listener outage, not busy)


def _vanish_probe_state(host: str, port: int) -> str:
    """Classify a fresh connection from the primary IP as free/busy/unreachable.

    Distinguishing "unreachable" from "busy" matters: a transient listener outage
    must not be reported as a saturation or keepalive-reaping (wedged) verdict.
    """
    try:
        sock = _vanish_connect(host, port)
    except (ConnectionRefusedError, TimeoutError, socket.gaierror):
        return _VANISH_UNREACHABLE
    except OSError:
        return _VANISH_UNREACHABLE
    try:
        sock.settimeout(1.5)
        data = sock.recv(128)
        if _VANISH_BUSY_MARKER in data or len(data) == 0:
            return _VANISH_BUSY
        return _VANISH_FREE
    except OSError:
        return _VANISH_UNREACHABLE
    finally:
        close_socket(sock)


def _vanish_measure_capacity(host: str, port: int, cap: int) -> int:
    """Count how many concurrent sessions the listener will currently accept."""
    conns: list[socket.socket] = []
    free = 0
    for _ in range(cap + 1):
        sock = None
        try:
            sock = _vanish_connect(host, port)
            time.sleep(0.15)
            data = sock.recv(64)
            if _VANISH_BUSY_MARKER in data:
                close_socket(sock)
            else:
                conns.append(sock)
                free += 1
        except OSError:
            # Release the socket opened this iteration before bailing out so a
            # partial-read failure does not leak the fd.
            if sock is not None:
                close_socket(sock)
            break
    for sock in conns:
        close_socket(sock)
    # These were clean closes; let the firmware reap them before capacity is
    # relied on again.
    time.sleep(2.0)
    return free


def run_vanish_probe(settings: RuntimeSettings) -> ProbeOutcome:
    started_at = time.perf_counter_ns()

    def outcome(result: str, detail: str) -> ProbeOutcome:
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome(result, f"telnet_vanish {detail}", elapsed_ms)

    host = settings.host
    port = settings.telnet_port
    iface = settings.lan_iface
    victim_ip = settings.victim_ip
    slots = settings.session_slots
    reap_timeout = settings.reap_timeout_s

    if os.name != "posix":
        return outcome("FAIL", "requires a posix host with ip(8)")
    if not iface or not victim_ip:
        return outcome("FAIL", "missing lan_iface/victim_ip for the half-open lane")

    victims: list[socket.socket] = []
    alias_added = False
    try:
        # 1. Baseline: the whole table must be free, else another client is using
        #    it and the reproduction would be invalid.
        free = _vanish_measure_capacity(host, port, slots)
        if free < slots:
            return outcome("FAIL", f"baseline not free: {free}/{slots} slots (another telnet client connected?)")

        # 2. Bring up the throwaway source IP and fill every slot with half-open
        #    victims sourced from it.
        try:
            _vanish_add_alias(iface, victim_ip)
        except subprocess.CalledProcessError as error:
            stderr = (error.stderr or "").strip()
            return outcome("FAIL", f"could not add victim IP alias (need passwordless sudo -n ip): {stderr}")
        alias_added = True
        for _ in range(slots):
            try:
                sock = _vanish_connect(host, port, source_ip=victim_ip)
            except OSError as error:
                return outcome("FAIL", f"could not open half-open victim: {error}")
            try:
                sock.settimeout(1.0)
                sock.recv(64)  # drain the banner so the firmware session is fully live
            except OSError:
                pass
            victims.append(sock)
            time.sleep(0.2)

        # 3. Confirm saturation: a fresh probe from the primary IP is not served.
        time.sleep(1.0)
        saturation_state = _vanish_probe_state(host, port)
        if saturation_state == _VANISH_FREE:
            return outcome("FAIL", "could not saturate the session table (fresh probe still free)")
        if saturation_state == _VANISH_UNREACHABLE:
            return outcome("FAIL", "listener unreachable while confirming saturation (connectivity problem, not a reap test)")

        # 4. Make the victims vanish at the network level: delete the source IP.
        #    If the delete fails the victims never vanish, so the reap test is
        #    invalid; surface the error and let the finally block retry cleanup.
        del_result = _vanish_del_alias(iface, victim_ip)
        if del_result.returncode != 0:
            stderr = (del_result.stderr or "").strip()
            return outcome("FAIL", f"could not delete victim IP alias to trigger vanish: {stderr}")
        alias_added = False

        # 5. Poll for recovery. GREEN = the device reaped the dead sessions and
        #    freed capacity; RED = it stays wedged past the reap window.
        deadline = time.time() + reap_timeout
        start = time.time()
        last_state = _VANISH_BUSY
        while time.time() < deadline:
            last_state = _vanish_probe_state(host, port)
            if last_state == _VANISH_FREE:
                recovered = time.time() - start
                return outcome("OK", f"reaped {slots} half-open slots in ~{recovered:.0f}s")
            time.sleep(3.0)

        if last_state == _VANISH_UNREACHABLE:
            return outcome("FAIL", f"listener unreachable during recovery; keepalive reaping inconclusive after {reap_timeout:.0f}s")
        return outcome("FAIL", f"still wedged {reap_timeout:.0f}s after peers vanished (half-open sessions not reaped)")
    finally:
        for sock in victims:
            close_socket(sock)
        if alias_added:
            _vanish_del_alias(iface, victim_ip)
