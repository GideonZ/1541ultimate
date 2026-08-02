"""The device's REST API over HTTP.

Every suite that speaks REST to the device goes through this: one place for the
`X-Password` rule, JSON encoding, and the retry policy.

The retry policy. The device serves only a few
concurrent HTTP connections, so a read can time out while it is busy. A GET is
retried because it has no effect on the device and a timeout says nothing about
whether it was served. A POST or PUT is not retried by default, because it
applies its input, and an HTTP status, including an error status, is a real
answer rather than a reason to try again. A caller that knows its own request
applies the same state however many times it arrives can opt in with
`idempotent=True`.
"""

from __future__ import annotations

import json
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Dict, Optional, Tuple

from report import Failure, format_exception

DEFAULT_TIMEOUT = 10.0
TRANSPORT_RETRIES = 3
TRANSPORT_RETRY_PAUSE_SECONDS = 0.5

Response = Tuple[int, Dict[str, str], bytes]


def multipart_body(field: str, filename: str, payload: bytes) -> Tuple[bytes, str]:
    """A single-file multipart/form-data body, and the Content-Type for it.

    The device's upload endpoints (machine:writemem, the runners, drive images)
    take a file part rather than a raw body.
    """
    boundary = "----ultimatetestsuite0123456789"
    body = b"".join((
        f"--{boundary}\r\n".encode(),
        f'Content-Disposition: form-data; name="{field}"; filename="{filename}"\r\n'.encode(),
        b"Content-Type: application/octet-stream\r\n\r\n",
        payload,
        f"\r\n--{boundary}--\r\n".encode(),
    ))
    return body, f"multipart/form-data; boundary={boundary}"


def header_value(headers: Dict[str, str], name: str) -> str:
    """Look a header up without depending on the case the device sent."""
    lowered = name.lower()
    for key, value in headers.items():
        if key.lower() == lowered:
            return value
    return ""


class RestClient:
    """One device's REST API.

    `request` returns `(status, headers, body)` rather than raising on an HTTP
    error status, because several suites assert on 403, 404 and 500 as the
    behaviour under test. It raises `Failure` only when no answer arrived at
    all.
    """

    def __init__(self, host: str, password: Optional[str] = None,
                 timeout: float = DEFAULT_TIMEOUT) -> None:
        self.host = host
        self.password = password or ""
        self.timeout = timeout

    def url(self, path: str, params: Optional[Dict[str, object]] = None) -> str:
        query = "?" + urllib.parse.urlencode(params) if params else ""
        return f"http://{self.host}{path}{query}"

    def request(self, method: str, path: str,
                params: Optional[Dict[str, object]] = None,
                payload: Optional[object] = None,
                body: Optional[bytes] = None,
                headers: Optional[Dict[str, str]] = None,
                use_password: bool = True,
                idempotent: bool = False,
                timeout: Optional[float] = None) -> Response:
        if payload is not None and body is not None:
            raise Failure("request takes payload or body, not both")

        sent_headers: Dict[str, str] = dict(headers or {})
        if payload is not None:
            body = json.dumps(payload).encode("utf-8")
            sent_headers.setdefault("Content-Type", "application/json")
        if self.password and use_password:
            sent_headers["X-Password"] = self.password

        target = self.url(path, params)
        request = urllib.request.Request(target, data=body, headers=sent_headers,
                                         method=method)
        # `idempotent` lets a caller opt a non-GET call into the same retry,
        # for a request that applies the same state however many times it
        # arrives. machine:writemem of a fixed block is the motivating case:
        # the device can be busy enough running a program to miss a 30 second
        # window, and rewriting the same bytes is indistinguishable from
        # writing them once.
        attempts = TRANSPORT_RETRIES if method == "GET" or idempotent else 1
        last_exc: Optional[BaseException] = None
        for attempt in range(attempts):
            try:
                with urllib.request.urlopen(
                        request, timeout=self.timeout if timeout is None else timeout) as response:
                    return response.status, dict(response.headers.items()), response.read()
            except urllib.error.HTTPError as exc:
                return exc.code, dict(exc.headers.items()), exc.read()
            except (OSError, TimeoutError, urllib.error.URLError) as exc:
                last_exc = exc
                if attempt + 1 < attempts:
                    time.sleep(TRANSPORT_RETRY_PAUSE_SECONDS)
        raise Failure(f"{method} {target} failed: {format_exception(last_exc)}") from last_exc

    # -- shorthands for the shapes suites actually use --

    def status(self, method: str, path: str, **kwargs) -> int:
        """Just the status code, for a call made for its effect."""
        code, _, _ = self.request(method, path, **kwargs)
        return code

    def json(self, path: str, **kwargs) -> object:
        """GET and decode a JSON body, failing on a non-200 or unparsable answer."""
        code, _, body = self.request("GET", path, **kwargs)
        if code != 200:
            raise Failure(f"GET {self.url(path)} returned HTTP {code}: {body[:160]!r}")
        try:
            return json.loads(body.decode("utf-8", "replace"))
        except ValueError as exc:
            raise Failure(f"GET {self.url(path)} returned unparsable JSON: {exc}") from exc

    def expect(self, method: str, path: str, expected: int = 200, **kwargs) -> bytes:
        """Make a call that has to succeed, returning its body."""
        code, _, body = self.request(method, path, **kwargs)
        if code != expected:
            raise Failure(f"{method} {self.url(path)} returned HTTP {code}, "
                          f"expected {expected}: {body[:160]!r}")
        return body
