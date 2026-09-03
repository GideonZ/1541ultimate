#!/usr/bin/env python3
# A stand-in device for the web suites: the pages, the REST calls, and a record
# of what the browser asked for.

"""What a browser talks to when there is no Ultimate on the desk.

`html/index.html` builds every REST URL from `window.location.hostname` with no
port, so the API it calls is only reachable when the page itself came from port
80. Rather than requiring a privileged port, the browser is given an HTTP proxy
and pointed at `http://ultimate.test/`. This server is that proxy: it answers
the page requests from the working tree and the REST calls itself, and records
every call so a check can say what the page sent rather than only what it drew.

Only `http` is proxied, so the CDN the pages load jQuery from over `https` is
still reached directly.

The stub answers the calls `html/index.html` makes and 404s everything else, so
a page that reaches for a route that does not exist fails a check rather than
being quietly humoured.
"""

import http.server
import json
import pathlib
import threading
import time
import urllib.parse

# A reserved TLD, so a browser that ignored the proxy would fail to resolve it
# rather than reaching something real.
DEVICE_HOST = "ultimate.test"

# The REST calls the pages make, and what this stub answers them with.
JSON_ROUTES = {
    ("GET", "/v1/version"),
    ("PUT", "/v1/machine:reset"),
    ("PUT", "/v1/machine:reboot"),
    ("PUT", "/v1/machine:menu_button"),
    ("PUT", "/v1/machine:poweroff"),
    ("PUT", "/v1/machine:writemem"),
    ("POST", "/v1/machine:writemem"),
    ("POST", "/v1/runners:run_prg"),
    ("POST", "/v1/runners:run_crt"),
    ("POST", "/v1/runners:sidplay"),
}
MOUNT_DRIVES = ("a", "b", "softiec")


class Call:
    """One request the browser made, as the stub saw it."""

    def __init__(self, method, path, query, body):
        self.method = method
        self.path = path
        self.query = query
        self.body = body

    def __repr__(self):
        return "%s %s%s" % (self.method, self.path,
                            "?" + urllib.parse.urlencode(self.query) if self.query else "")


class DeviceStub:
    """The device the browser sees, and the log of what it was asked."""

    def __init__(self, pages, product="Ultimate 64", firmware_version="3.14d"):
        self.pages = pathlib.Path(pages)
        self.product = product
        self.firmware_version = firmware_version
        self.calls = []
        self.refuse = {}  # path -> status code to answer with instead of 200
        self._lock = threading.Lock()
        stub = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *args):
                pass

            def do_GET(self):
                stub._answer(self, "GET")

            def do_PUT(self):
                stub._answer(self, "PUT")

            def do_POST(self):
                stub._answer(self, "POST")

        class Server(http.server.ThreadingHTTPServer):
            daemon_threads = True

        self._server = Server(("127.0.0.1", 0), Handler)
        threading.Thread(target=self._server.serve_forever, daemon=True).start()

    @property
    def proxy(self):
        return "127.0.0.1:%d" % self._server.server_address[1]

    @property
    def base_url(self):
        return "http://%s" % DEVICE_HOST

    def close(self):
        self._server.shutdown()

    # What the checks read.

    def rest_calls(self):
        with self._lock:
            return [c for c in self.calls if c.path.startswith("/v1/")]

    def calls_to(self, path):
        return [c for c in self.rest_calls() if c.path == path]

    def clear(self):
        with self._lock:
            self.calls = []

    def await_quiet(self, settle=0.4, timeout=15.0):
        """Wait until the browser has stopped calling.

        A monitor command reads memory in a loop, so a check that cleared the
        log the moment its first read arrived would attribute the rest of that
        loop to whatever it did next.
        """
        deadline = time.time() + timeout
        with self._lock:
            seen = len(self.calls)
        quiet_since = time.time()
        while time.time() < deadline:
            time.sleep(0.05)
            with self._lock:
                now = len(self.calls)
            if now != seen:
                seen, quiet_since = now, time.time()
            elif time.time() - quiet_since >= settle:
                return True
        return False

    def await_call(self, path, timeout=5.0):
        """The first call to `path` after the last clear, or None if none came."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            found = self.calls_to(path)
            if found:
                return found[0]
            time.sleep(0.02)
        return None

    # What the browser sees.

    def _answer(self, handler, method):
        url = urllib.parse.urlsplit(handler.path)
        path = url.path or "/"
        length = int(handler.headers.get("Content-Length") or 0)
        body = handler.rfile.read(length) if length else b""
        query = dict(urllib.parse.parse_qsl(url.query))
        with self._lock:
            self.calls.append(Call(method, path, query, body))

        if not path.startswith("/v1/"):
            return self._serve_page(handler, method, path)

        status = self.refuse.get(path, 200)
        if status != 200:
            return self._send(handler, status, "application/json",
                              json.dumps({"errors": ["refused by the stub"]}).encode())
        if method == "GET" and path == "/v1/info":
            return self._send(handler, 200, "application/json", json.dumps({
                "product": self.product,
                "firmware_version": self.firmware_version,
                "fpga_version": "124",
                "hostname": DEVICE_HOST,
                "errors": [],
            }).encode())
        if method == "GET" and path == "/v1/machine:readmem":
            start = int(query.get("address", "0"), 16)
            size = int(query.get("length", "0"))
            # Address-derived, so the bytes themselves say where they came from.
            return self._send(handler, 200, "application/octet-stream",
                              bytes((start + i) & 0xFF for i in range(size)))
        if method == "POST" and path in ["/v1/drives/%s:mount" % d for d in MOUNT_DRIVES]:
            return self._send(handler, 200, "application/json",
                              json.dumps({"errors": []}).encode())
        if (method, path) in JSON_ROUTES:
            return self._send(handler, 200, "application/json",
                              json.dumps({"errors": []}).encode())
        return self._send(handler, 404, "application/json",
                          json.dumps({"errors": ["no such route"]}).encode())

    def _serve_page(self, handler, method, path):
        name = "index.html" if path == "/" else path.lstrip("/")
        target = (self.pages / name).resolve()
        if method != "GET" or self.pages.resolve() not in target.parents or not target.is_file():
            return self._send(handler, 404, "text/plain", b"not found")
        kinds = {".html": "text/html", ".css": "text/css", ".js": "application/javascript",
                 ".json": "application/json", ".woff": "font/woff", ".svg": "image/svg+xml"}
        self._send(handler, 200, kinds.get(target.suffix, "application/octet-stream"),
                   target.read_bytes())

    @staticmethod
    def _send(handler, status, content_type, body):
        handler.send_response(status)
        handler.send_header("Content-Type", content_type)
        handler.send_header("Content-Length", str(len(body)))
        handler.end_headers()
        handler.wfile.write(body)
