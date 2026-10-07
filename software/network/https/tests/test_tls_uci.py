"""Exercise target 6 through production HTTP rendering/parsing and real TLS.

Only the UART/TCP transport and trust-store source are host substitutes.
No device access, public endpoints, or installed system trust changes.
"""
import argparse
from pathlib import Path
import socket
import ssl
import tempfile
import threading
from contextlib import contextmanager
from test_tls import certificate, run
from body_fault_fixture import body_fault_server


@contextmanager
def server(cert, key, body, chunked=False, truncated=False, status=200, reason="OK", extra_header=False,
           wire=None, fragment=23):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen()
    listener.settimeout(0.1)
    stopped, errors = threading.Event(), []

    def serve():
        try:
            while not stopped.is_set():
                try:
                    connection, _ = listener.accept()
                    break
                except TimeoutError:
                    continue
            else:
                return
            with connection:
                connection.settimeout(3)
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.minimum_version = ssl.TLSVersion.TLSv1_2
                context.load_cert_chain(cert, key)
                with context.wrap_socket(connection, server_side=True) as channel:
                    request = b""
                    while b"\r\n\r\n" not in request:
                        part = channel.recv(512)
                        if not part:
                            return
                        request += part
                        assert len(request) <= 1024
                    assert request == b"GET /data HTTP/1.1\r\nHost: localhost\r\n\r\n", request
                    header = f"HTTP/1.1 {status} {reason}\r\nContent-Type: application/json\r\n".encode()
                    if extra_header:
                        header += b"X-Long: " + b"x"*400 + b"\r\n"
                    if chunked:
                        response = header + b"Transfer-Encoding: chunked\r\n\r\n"
                        for start in range(0, len(body), 3):
                            chunk = body[start:start+3]
                            response += f"{len(chunk):x}\r\n".encode() + chunk + b"\r\n"
                        response += b"0\r\n\r\n"
                    else:
                        response = header + f"Content-Length: {len(body)}\r\n\r\n".encode() + body
                    if truncated:
                        response = response[:-2]
                    if wire is not None:
                        response = wire
                    for start in range(0, len(response), fragment):
                        channel.sendall(response[start:start+fragment])
                    # Complete bodies may cause UCI to close before close_notify.
                    channel.unwrap().close()
        except (ssl.SSLError, ConnectionError, TimeoutError):
            pass
        except Exception as error:
            errors.append(error)

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        yield listener.getsockname()[1]
    finally:
        stopped.set()
        thread.join(5)
        listener.close()
        assert not thread.is_alive() and not errors, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    probe = args.probe.resolve()
    with tempfile.TemporaryDirectory(prefix="ultimate-uci-tls-") as tmp:
        folder = Path(tmp)
        trusted = certificate(folder, "trusted")
        other = certificate(folder, "other")
        expired = certificate(folder, "expired", expired=True)
        cases = [
            ("raw JSON", trusted, trusted[0], "raw", "localhost", b'{"x":1}', {}),
            ("object handles", trusted, trusted[0], "object", "localhost", b'{"x":1}', {}),
            ("bounded status", trusted, trusted[0], "long-status", "localhost", b'{"x":1}', {"reason": "x"*400}),
            ("binary body", trusted, trusted[0], "binary", "localhost", b'a\0b', {}),
            ("raw long headers", trusted, trusted[0], "raw-long-header", "localhost", b'{"x":1}', {"extra_header": True}),
            *[(f"raw boundary {n}", trusted, trusted[0], f"long-{n}", "localhost", b'x'*n, {})
              for n in (894, 895, 896, 1790, 2048)],
            ("chunked JSON", trusted, trusted[0], "raw", "localhost", b'{"x":1}', {"chunked": True}),
            ("HTTP error is a response", trusted, trusted[0], "http-error", "localhost", b'{"x":1}', {"status": 404}),
            ("truncated body", trusted, trusted[0], "failure", "localhost", b'{"x":1}', {"truncated": True}),
            ("untrusted", other, trusted[0], "failure", "localhost", b'{"x":1}', {}),
            ("wrong hostname", trusted, trusted[0], "failure", "wrong.test", b'{"x":1}', {}),
            ("expired", expired, expired[0], "failure", "localhost", b'{"x":1}', {}),
        ]
        prefix = b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        chunked = prefix + b"Transfer-Encoding: chunked\r\n\r\n"
        malformed = {
            "negative length": prefix + b"Content-Length: -1\r\n\r\n",
            "nondecimal length": prefix + b"Content-Length: 0x7\r\n\r\n{\"x\":1}",
            "length suffix": prefix + b"Content-Length: 7junk\r\n\r\n{\"x\":1}",
            "overflow length": prefix + b"Content-Length: 999999999999999999999\r\n\r\n",
            "conflicting lengths": prefix + b"Content-Length: 7\r\nContent-Length: 9\r\n\r\n{\"x\":1}",
            "ambiguous framing": chunked.replace(b"\r\n\r\n", b"\r\nContent-Length: 7\r\n\r\n") + b"7\r\n{\"x\":1}\r\n0\r\n\r\n",
            "unsupported coding": prefix + b"Transfer-Encoding: notchunked\r\n\r\n0\r\n\r\n",
            "invalid chunk": chunked + b"XYZ\r\n",
            "negative chunk": chunked + b"-1\r\n",
            "chunk suffix": chunked + b"7junk\r\n{\"x\":1}\r\n0\r\n\r\n",
            "overflow chunk": chunked + b"ffffffffffffffffffffffff\r\n",
            "missing chunk CRLF": chunked + b"7\r\n{\"x\":1}junk\r\n0\r\n\r\n",
            "missing final CRLF": chunked + b"7\r\n{\"x\":1}\r\n0\r\n",
            "truncated trailers": chunked + b"7\r\n{\"x\":1}\r\n0\r\nX-Trailer: yes\r\n",
            "invalid status line": b"INVALID 200 OK\r\nContent-Length: 7\r\n\r\n{\"x\":1}",
            "header field limit": prefix + b"X: a\r\n" * 24 + b"Content-Length: 7\r\n\r\n{\"x\":1}",
            "header without colon": prefix + b"Malformed\r\nContent-Length: 7\r\n\r\n{\"x\":1}",
            "equal duplicate lengths": prefix + b"Content-Length: 7\r\nContent-Length: 7\r\n\r\n{\"x\":1}",
            "empty length": prefix + b"Content-Length:\r\n\r\n",
            "signed positive length": prefix + b"Content-Length: +7\r\n\r\n{\"x\":1}",
            "header whitespace before colon": prefix + b"Content-Length : 7\r\n\r\n{\"x\":1}",
            "folded header": prefix + b" Content-Length: 7\r\n\r\n{\"x\":1}",
            "duplicate transfer coding": chunked.replace(b"\r\n\r\n", b"\r\nTransfer-Encoding: chunked\r\n\r\n") + b"0\r\n\r\n",
            "unsupported coding chain": chunked.replace(b"chunked", b"gzip, chunked") + b"0\r\n\r\n",
            "unfinished quoted extension": chunked + b'7;x="unfinished\r\n{"x":1}\r\n0\r\n\r\n',
            "trailer changes framing": chunked + b'7\r\n{"x":1}\r\n0\r\nContent-Length: 7\r\n\r\n',
            "oversized trailers": chunked + b'7\r\n{"x":1}\r\n0\r\n' + (b'X: ' + b'a'*500 + b'\r\n')*5 + b'\r\n',
            "oversized header": prefix + b'X: ' + b'a'*2050 + b'\r\nContent-Length: 7\r\n\r\n{"x":1}',
        }
        valid = {
            "decimal leading zero": prefix + b"Content-Length: 007\r\n\r\n{\"x\":1}",
            "length whitespace": prefix + b"Content-Length:\t7 \t\r\n\r\n{\"x\":1}",
            "chunk extension and trailer": chunked + b'7;name="test"\r\n{"x":1}\r\n0\r\nX-Trailer: yes\r\n\r\n',
            "mixed case chunked": chunked.replace(b"chunked", b"Chunked") + b'7\r\n{"x":1}\r\n0\r\n\r\n',
            "close delimited": prefix + b'Connection: close\r\n\r\n{"x":1}',
        }
        for fixtures, scenario in ((malformed, "failure"), (valid, "raw")):
            for name, wire in fixtures.items():
                cases.append((name, trusted, trusted[0], scenario, "localhost", b"", {"wire": wire, "fragment": 1}))
        cases.append(("decimal not octal", trusted, trusted[0], "long-8", "localhost", b"",
                      {"wire": prefix + b"Content-Length: 008\r\n\r\nxxxxxxxx"}))
        failures = []
        for name, pair, roots, scenario, host, body, options in cases:
            try:
                with server(*pair, body, **options) as port:
                    run(probe, "--real-tls", roots, port, scenario, host)
                print(f"PASS UCI + TLS: {name}", flush=True)
            except RuntimeError as error:
                failures.append(name)
                print(f"FAIL UCI + TLS: {name}\n{error}", flush=True)
        for scenario in ('body-clean', 'body-eof', 'body-reset', 'body-stall'):
            try:
                with body_fault_server(*trusted, scenario) as (port, events):
                    output = run(probe, "--real-tls", trusted[0], port, scenario, "localhost")
                assert len(events) == 2 and events[0]['action'] == scenario and events[1]['action'] == 'recovery'
                assert all(e['request_received'] and e['tls_version'] for e in events)
                print(output.strip(), flush=True)
                print(f"PASS UCI + TLS: {scenario}; fixture={events}", flush=True)
            except (RuntimeError, AssertionError) as error:
                failures.append(scenario)
                print(f"FAIL UCI + TLS: {scenario}\n{error}", flush=True)
        if failures:
            raise SystemExit(f"{len(failures)} failures: {', '.join(failures)}")
        print(f"{len(cases)+4} UCI/HTTP/TLS integration cases passed.")


if __name__ == "__main__":
    main()
