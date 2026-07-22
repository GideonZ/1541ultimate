#!/usr/bin/env python3
"""Production-REST-API harness primitives for the machine-code monitor.

Uses ONLY shipped/cherry-picked REST endpoints (no temporary E2E hooks):
  - keyboard injection: POST /v1/machine:input   (matrix key events; menu-active
    taps are routed to the firmware menu/monitor)
  - menu/overlay screen capture: GET /v1/machine:menu_screen  (2000-byte binary)
  - PUT /v1/machine:menu_button, :reset, :writemem ; GET :readmem

This replaces the deprecated input_ui / dump_ui_screen hooks.
"""
import socket
import time
import http.client
import urllib.error


SCREEN_W, SCREEN_H = 40, 25
SCREEN_CELLS = SCREEN_W * SCREEN_H


class Failure(RuntimeError):
    pass


# char -> machine:input keyboard "inputs" name (C64 matrix names from input_api.h)
_NAMED = {
    "\n": ["return"], "\r": ["return"], " ": ["space"],
    ":": ["colon"], ";": ["semicolon"], ",": ["comma"], ".": ["period"],
    "/": ["slash"], "+": ["plus"], "-": ["minus"], "*": ["star"], "@": ["at"],
    "=": ["equals"],
}


def char_inputs(ch: str):
    """Return the keyboard 'inputs' list for a single character, or None."""
    if ch in _NAMED:
        return _NAMED[ch]
    low = ch.lower()
    if ("a" <= low <= "z") or ("0" <= low <= "9"):
        return [low]
    return None


class Rest:
    def __init__(self, host="u64", timeout=10.0):
        self.host = host
        self.timeout = timeout
        self.connect_timeout = min(1.5, timeout)
        self.connect_attempts = 8

    def _split_host(self):
        if ":" in self.host:
            host, port = self.host.rsplit(":", 1)
            try:
                return host, int(port)
            except ValueError:
                pass
        return self.host, 80

    def _path(self, path, params=None):
        if params:
            return path + "?" + "&".join(f"{k}={v}" for k, v in params.items())
        return path

    def _url(self, path, params=None):
        return f"http://{self.host}{self._path(path, params)}"

    def _connect(self):
        host, port = self._split_host()
        last_error = None
        for attempt in range(self.connect_attempts):
            conn = http.client.HTTPConnection(host, port, timeout=self.connect_timeout)
            try:
                conn.connect()
                if conn.sock is not None:
                    conn.sock.settimeout(self.timeout)
                return conn
            except (OSError, TimeoutError) as exc:
                last_error = exc
                conn.close()
                if attempt + 1 < self.connect_attempts:
                    time.sleep(min(0.15 * (attempt + 1), 0.75))
        raise urllib.error.URLError(last_error)

    def req(self, method, path, params=None, body=None, ctype=None):
        data = body
        headers = {}
        if ctype:
            headers["Content-Type"] = ctype
        if data is not None:
            headers["Content-Length"] = str(len(data))
        conn = self._connect()
        try:
            conn.request(method, self._path(path, params), body=data, headers=headers)
            resp = conn.getresponse()
            payload = resp.read()
            if resp.status >= 400:
                raise urllib.error.HTTPError(
                    self._url(path, params), resp.status, resp.reason,
                    resp.headers, None
                )
            return resp.status, payload
        except urllib.error.HTTPError:
            raise
        except (OSError, TimeoutError, http.client.HTTPException) as exc:
            raise urllib.error.URLError(exc) from exc
        finally:
            conn.close()

    # --- liveness ---
    def alive(self, timeout=3):
        try:
            s = socket.create_connection((self.host, 80), timeout=timeout)
            s.close()
            return True
        except OSError:
            return False

    # --- basic machine ops ---
    def reset(self):
        self.req("PUT", "/v1/machine:reset")

    def menu_button(self):
        try:
            self.req("PUT", "/v1/machine:menu_button")
        except (OSError, TimeoutError, urllib.error.URLError):
            # The command may already have been accepted before the HTTP socket
            # closes. Callers verify the resulting menu state instead of
            # retrying blindly and toggling the UI back closed.
            pass

    def read_mem(self, addr, length):
        last_error = None
        for _ in range(3):
            try:
                _, b = self.req("GET", "/v1/machine:readmem",
                                params={"address": f"{addr:04X}", "length": length})
                break
            except urllib.error.HTTPError:
                raise
            except (OSError, TimeoutError, urllib.error.URLError) as exc:
                last_error = exc
                time.sleep(0.25)
        else:
            raise Failure(f"readmem transport error: {last_error}")
        return b

    def write_mem(self, addr, data: bytes):
        if len(data) <= 128:
            self.req("PUT", "/v1/machine:writemem",
                     params={"address": f"{addr:04X}", "data": data.hex().upper()})
        else:
            self.req("POST", "/v1/machine:writemem",
                     params={"address": f"{addr:04X}"}, body=data,
                     ctype="application/octet-stream")

    # --- keyboard (production input endpoint) ---
    def tap(self, inputs):
        body = ('{"events":[{"kind":"keyboard","inputs":['
                + ",".join(f'"{i}"' for i in inputs)
                + '],"transition":"tap"}]}').encode()
        self.req("POST", "/v1/machine:input", body=body,
                 ctype="application/json")

    def release_all(self):
        body = b'{"events":[{"kind":"release_all"}]}'
        self.req("POST", "/v1/machine:input", body=body,
                 ctype="application/json")

    def send_text(self, text, settle=0.12):
        for ch in text:
            inp = char_inputs(ch)
            if inp is None:
                raise Failure(f"no key mapping for {ch!r}")
            self.tap(inp)
            time.sleep(settle)

    # --- screen capture (menu_screen endpoint) ---
    def menu_screen_raw(self):
        last_error = None
        for _ in range(12):
            try:
                status, b = self.req("GET", "/v1/machine:menu_screen")
                break
            except urllib.error.HTTPError as exc:
                if exc.code != 404:
                    raise
                last_error = exc
                time.sleep(0.25)
            except (OSError, TimeoutError, urllib.error.URLError) as exc:
                last_error = exc
                time.sleep(0.25)
        else:
            raise Failure(f"menu_screen transport error: {last_error}")
        if status != 200 or len(b) != 2 * SCREEN_CELLS:
            raise Failure(f"menu_screen HTTP {status} len {len(b)}")
        return b

    def screen_lines(self):
        b = self.menu_screen_raw()
        chars = b[:SCREEN_CELLS]
        lines = []
        for y in range(SCREEN_H):
            row = []
            for x in range(SCREEN_W):
                c = chars[y * SCREEN_W + x] & 0x7F
                row.append(chr(c) if 0x20 <= c <= 0x7E else (" "))
            lines.append("".join(row).rstrip())
        return lines

    def screen_text(self):
        return "\n".join(self.screen_lines())


def wait_for(rest: Rest, needles, label, timeout=6.0, interval=0.2):
    needles = [needles] if isinstance(needles, str) else list(needles)
    deadline = time.time() + timeout
    last = ""
    while time.time() < deadline:
        try:
            last = rest.screen_text()
        except (Failure, OSError, urllib.error.URLError):
            time.sleep(interval)
            continue
        if all(n in last for n in needles):
            return last
        time.sleep(interval)
    raise Failure(f"{label}: timed out waiting for {needles}\n--- last screen ---\n{last}")
