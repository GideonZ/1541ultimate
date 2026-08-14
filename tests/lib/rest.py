"""The device's REST API over HTTP.

Every suite that speaks REST to the device goes through this: one place for the
`X-Password` rule, JSON encoding, and the retry policy.

The retry policy lives in `may_retry`, and nothing outside this module decides
it. The device serves only a few concurrent HTTP connections, so a request can
fail while it is busy, and whether it may be repeated depends on one thing:
whether the device can already have acted on it.

  request never went out   nothing was applied, so any method may be resent.
  request went out         the device may have acted, so only GET and calls the
                           caller declares idempotent may be resent.
  an HTTP status came back That is an answer, error status included, and never
                           a reason to retry.

This was rediscovered independently in seven suites, each with its own HTTP
client and each getting it wrong differently: some never retried a POST at all,
some retried nothing, and one keyed the decision on whether the request carried
a body rather than on whether it had been sent. `check_transport_usage.py`
fails the gate if a suite reaches for urllib or http.client directly again.

Two entry points, because the tree uses two HTTP libraries:
`retrying_urlopen` for urllib, `retrying_http_request` for http.client.
"""

from __future__ import annotations

import http.client
import json
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Dict, Optional, Tuple

import report
import targets
from report import Failure, format_exception

# Re-exported so a suite building its own URLs has one name for it.
INPUT_PATH = targets.INPUT_PATH

DEFAULT_TIMEOUT = 10.0
TRANSPORT_RETRIES = 3
TRANSPORT_RETRY_PAUSE_SECONDS = 0.5

# How much of what a request carried, or of what the device said back, is kept
# in one action record. Enough for the firmware's own error sentence and for a
# key batch, short enough that a suite hammering a refusing endpoint does not
# fill the run's JSONL with copies of one body. The password inside either is
# masked by report.py, which does it once for every record shape.
ACTION_TEXT_CHARS = 300

Response = Tuple[int, Dict[str, str], bytes]


def may_retry(method: str, request_sent: bool, idempotent: bool = False) -> bool:
    """Whether a failed attempt may be repeated. The only copy of this rule.

    `request_sent` is what makes the answer knowable: until the request has left
    the client, the device cannot have acted on it, so repeating it is safe
    whatever the method. Once it has gone out, repeating it can apply it twice,
    so only a GET or a call the caller declares idempotent may go again.
    """
    if not request_sent:
        return True
    return method.upper() == "GET" or idempotent


def record_action(method: str, path: str, started: float, attempts: int,
                  status: Optional[int], answer: Optional[bytes],
                  exc: Optional[BaseException] = None,
                  params: Optional[Dict[str, object]] = None,
                  payload: Optional[object] = None) -> None:
    """Record what a request did to the device, when it is worth keeping.

    Every request in the tree passes through one of the three entry points in
    this module, so this is where the harness's own acts reach one timeline
    whichever call made them. A run's reads are its bulk, so the rule is exact:
    a GET that answered 200 first time is dropped, and everything else is kept.
    A mutation changed the device, a retry says the device was busy, and a
    request that did not answer 200 carries the device's own words, which a
    suite that catches Failure and carries on otherwise destroys the only copy
    of.

    `started` is the start of the attempt that produced this outcome rather
    than of the first one, so `ms` is what the device took and not what the
    retry pauses did.
    """
    if not report.JSONL_PATH:
        return
    retried = attempts > 1
    answered = status == 200
    if method.upper() == "GET" and answered and not retried:
        return
    fields: Dict[str, object] = {
        "ms": round((time.monotonic() - started) * 1000.0, 1)}
    carried = params if params else payload
    if carried:
        fields["params"] = str(carried)[:ACTION_TEXT_CHARS]
    if status is not None:
        fields["status"] = status
    if retried:
        fields["retries"] = attempts
    if not answered:
        if exc is not None:
            fields["error"] = format_exception(exc)[:ACTION_TEXT_CHARS]
        elif answer:
            fields["error"] = answer.decode("utf-8", "replace")[:ACTION_TEXT_CHARS]
    report.action(method.upper(), path, **fields)


def path_of(url: str) -> str:
    """The path a URL names, for a caller that built its own URL."""
    return urllib.parse.urlsplit(url).path or url


def retrying_urlopen(request: "urllib.request.Request", timeout: float,
                     idempotent: bool = False):
    """urlopen under the shared retry policy, for callers not using RestClient.

    urllib reports how far the request got: a failure before any response was
    seen surfaces as URLError, while a timeout waiting for the response body
    raises TimeoutError directly.

    Returns the open response, which the caller closes; HTTPError is left to
    propagate, because an HTTP status is an answer rather than a failure.
    """
    method = request.get_method()
    path = path_of(request.full_url)
    last_exc: Optional[BaseException] = None
    for attempt in range(TRANSPORT_RETRIES):
        started = time.monotonic()
        try:
            answer = urllib.request.urlopen(request, timeout=timeout)
        except urllib.error.HTTPError as exc:
            record_action(method, path, started, attempt + 1, exc.code, None, None)
            raise
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            last_exc = exc
            sent = not isinstance(exc, urllib.error.URLError)
            if may_retry(method, sent, idempotent) and attempt + 1 < TRANSPORT_RETRIES:
                time.sleep(TRANSPORT_RETRY_PAUSE_SECONDS)
                continue
            record_action(method, path, started, attempt + 1, None, None, exc)
            break
        # The caller closes the response and reads the body itself, so the
        # record carries the status and not what came back.
        record_action(method, path, started, attempt + 1, answer.status, None)
        return answer
    raise last_exc


def retrying_http_request(host: "str | targets.Target", method: str, path: str, *,
                          body: Optional[bytes] = None,
                          headers: Optional[Dict[str, str]] = None,
                          timeout: float = DEFAULT_TIMEOUT,
                          idempotent: bool = False) -> Response:
    """One http.client request under the shared retry policy.

    http.client does not distinguish how far a request got, so connecting is
    done as its own step: a failure there cannot have been applied, whatever the
    request carries. Returns (status, headers, payload), with an HTTP status of
    any value returned rather than raised.

    `host` may be a target token, so callers that hold whatever the runner gave
    them do not each have to resolve it; see tests/lib/targets.py.
    """
    target = targets.resolve(host)
    host = target.host_for(path)
    last_exc: Optional[BaseException] = None
    for attempt in range(TRANSPORT_RETRIES):
        connection = http.client.HTTPConnection(host, target.rest_port, timeout=timeout)
        sent = False
        started = time.monotonic()
        try:
            connection.connect()
            sent = True
            connection.request(method, path, body=body, headers=headers or {})
            response = connection.getresponse()
            payload = response.read()
            answer = (response.status, dict(response.getheaders()), payload)
        except (OSError, http.client.HTTPException) as exc:
            last_exc = exc
            if may_retry(method, sent, idempotent) and attempt + 1 < TRANSPORT_RETRIES:
                time.sleep(TRANSPORT_RETRY_PAUSE_SECONDS)
                continue
            record_action(method, path, started, attempt + 1, None, None, exc)
            raise
        finally:
            connection.close()
        record_action(method, path, started, attempt + 1, answer[0], answer[2])
        return answer
    raise last_exc


def url_for(host: "str | targets.Target", path: str,
            params: Optional[Dict[str, object]] = None) -> str:
    """The URL for `path` on `host`, from the handle alone.

    One builder, because a caller that assembles its own URL is a caller that
    addresses port 80 of whatever it was given, and the handle is the thing
    that knows where a device actually is. The port is written only when it is
    not the default, so a URL against an ordinary device reads with no port in
    it.
    """
    target = targets.resolve(host)
    authority = target.host_for(path)
    if target.rest_port != targets.REST_PORT:
        authority = f"{authority}:{target.rest_port}"
    query = "?" + urllib.parse.urlencode(params) if params else ""
    return f"http://{authority}{path}{query}"


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


# What a transport error says when the device is not answering at all, as
# opposed to answering with something the caller did not want. Matched against
# the formatted exception text because urllib, http.client, socket and the
# resolver each report it as a different exception type.
#
# "not found" is deliberately absent: one copy of this list carried it, and it
# also matches an HTTP 404 body, which is the device answering.
UNREACHABLE_MARKERS = (
    "no route to host",
    "network is unreachable",
    "connection refused",
    "timed out",
    "temporary failure in name resolution",
)


def looks_unreachable(exc: BaseException) -> bool:
    """Whether `exc` reads as the device being off the network."""
    text = format_exception(exc).lower()
    return any(marker in text for marker in UNREACHABLE_MARKERS)


def json_object(label: str, body: bytes) -> Dict[str, object]:
    """Decode a response body that has to be a JSON object.

    For a suite asserting on the HTTP contract itself, where an unparsable body
    or a JSON array is the defect being looked for rather than a surprise.
    `label` names the call, so the failure says which one answered badly.
    """
    try:
        data = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as exc:
        raise Failure(f"{label}: response body is not valid JSON: {body[:160]!r}") from exc
    if not isinstance(data, dict):
        raise Failure(f"{label}: expected JSON object, got {data!r}")
    return data


class RestClient:
    """One device's REST API.

    `request` returns `(status, headers, body)` rather than raising on an HTTP
    error status, because several suites assert on 403, 404 and 500 as the
    behaviour under test. It raises `Failure` only when no answer arrived at
    all.
    """

    def __init__(self, host: "str | targets.Target", password: Optional[str] = None,
                 timeout: float = DEFAULT_TIMEOUT) -> None:
        # `host` is a target rather than a bare name: "u2@c64u" resolves to a
        # cartridge under test and the computer it is plugged into. See
        # tests/lib/targets.py. Everything addresses the device except
        # keyboard injection, which the cartridge does not implement and which
        # therefore goes to the computer.
        #
        # A resolved handle is accepted as well as a token, which is what lets
        # a caller point this client at a device serving on another port
        # without every suite changing: the suites keep passing the token they
        # parsed from their own -H.
        target = targets.resolve(host)
        self.target = target
        self.host = target.device
        self.input_host = target.input_host
        self.password = password or ""
        # No artefact may carry it, and the record writer is the one place that
        # can enforce that for every shape at once. See report.mask_secret.
        report.mask_secret(self.password)
        self.timeout = timeout
        # Requests that could have changed the device, counted. A GET does
        # not: reading memory, the menu screen or a config value leaves the
        # machine exactly as it was. Anything else may. A caller comparing this
        # against a value it saw earlier learns whether the device could have
        # moved since; MachineApi.reset uses it to skip a reset that cannot
        # accomplish anything.
        self.mutations = 0

    def url(self, path: str, params: Optional[Dict[str, object]] = None) -> str:
        return url_for(self.target, path, params)

    def request(self, method: str, path: str,
                params: Optional[Dict[str, object]] = None,
                payload: Optional[object] = None,
                body: Optional[bytes] = None,
                headers: Optional[Dict[str, str]] = None,
                use_password: bool = True,
                idempotent: bool = False,
                timeout: Optional[float] = None,
                retries: Optional[int] = None) -> Response:
        """One request, with the transport's retry rule applied.

        `retries` is for a caller that must bound how long one call can take
        rather than get an answer: the recorder issues its requests from a
        loop that has to keep draining sockets, so it asks for one attempt and
        treats a failure as an answer. Everything else takes the default,
        which is the transport rule the rest of the harness relies on.
        """
        if payload is not None and body is not None:
            raise Failure("request takes payload or body, not both")

        sent_headers: Dict[str, str] = dict(headers or {})
        if payload is not None:
            body = json.dumps(payload).encode("utf-8")
            sent_headers.setdefault("Content-Type", "application/json")
        if self.password and use_password:
            sent_headers["X-Password"] = self.password

        target = self.url(path, params)
        if method.upper() != "GET":
            self.mutations += 1
        request = urllib.request.Request(target, data=body, headers=sent_headers,
                                         method=method)
        # `idempotent` lets a caller opt a non-GET call into the same retry,
        # for a request that applies the same state however many times it
        # arrives. machine:writemem of a fixed block is the motivating case:
        # the device can be busy enough running a program to miss a 30 second
        # window, and rewriting the same bytes is indistinguishable from
        # writing them once.
        # Retryability is decided by may_retry, the one copy of that rule.
        last_exc: Optional[BaseException] = None
        allowed = TRANSPORT_RETRIES if retries is None else max(1, retries)
        for attempt in range(allowed):
            started = time.monotonic()
            try:
                with urllib.request.urlopen(
                        request, timeout=self.timeout if timeout is None else timeout) as response:
                    answer = (response.status, dict(response.headers.items()),
                              response.read())
            except urllib.error.HTTPError as exc:
                answer = (exc.code, dict(exc.headers.items()), exc.read())
            except (OSError, TimeoutError, urllib.error.URLError) as exc:
                last_exc = exc
                sent = not isinstance(exc, urllib.error.URLError)
                if may_retry(method, sent, idempotent) and attempt + 1 < allowed:
                    time.sleep(TRANSPORT_RETRY_PAUSE_SECONDS)
                    continue
                record_action(method, path, started, attempt + 1, None, None, exc,
                              params=params, payload=payload)
                break
            # Outside the response, so the record's file append is not part of
            # the time this connection holds one of the device's four slots.
            record_action(method, path, started, attempt + 1, answer[0], answer[2],
                          params=params, payload=payload)
            return answer
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
