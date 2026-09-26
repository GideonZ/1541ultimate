#!/usr/bin/env python3
"""Disposable LAN fixtures: HTTP responses and a silent TCP/TLS endpoint.

Bind explicitly to a test-machine address. Only the named device may connect.
No file serving, credentials, firmware writes, TLS trust changes or firewall edits.
"""
import argparse
import json
import os
import socket
import socketserver
import struct
import threading
import time
from pathlib import Path


def receive_client_hello(connection, received):
    """Read one complete, bounded TLS record before injecting a handshake fault."""
    while len(received) < 5:
        part = connection.recv(5-len(received))
        if not part:
            raise ValueError('Incomplete TLS record header')
        received += part
    length = int.from_bytes(received[3:5], 'big')
    if received[:1] != b'\x16' or not 4 <= length <= 4096:
        raise ValueError('Expected a bounded TLS handshake record')
    while len(received) < length+5:
        part = connection.recv(length+5-len(received))
        if not part:
            raise ValueError('Incomplete TLS handshake record')
        received += part
    if received[5] != 1 or int.from_bytes(received[6:9], 'big') != length-4:
        raise ValueError('Expected one complete ClientHello')
    return received[:length+5]


class Fixture(socketserver.BaseRequestHandler):
    def handle(self):
        if self.client_address[0] not in self.server.allowed:
            return
        self.request.settimeout(5)
        received = self.request.recv(2048)
        mode = getattr(self.server, 'handshake_close', None)
        if mode:
            try:
                received = receive_client_hello(self.request, received)
                hello_at = time.time()
                time.sleep(2)
                if mode == 'reset':
                    # Winsock uses unsigned shorts; POSIX uses ints for linger.
                    linger = struct.pack('HH' if os.name == 'nt' else 'ii', 1, 0)
                    self.request.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, linger)
                else:
                    self.request.shutdown(socket.SHUT_WR)
                self.request.close()
                self.server.record(mode, hello_at=hello_at, received_bytes=len(received),
                                   tls_record=True, action='socket_closed')
            except (OSError, ValueError) as error:
                self.server.record(mode, action='fixture_error', error=str(error))
            return
        if self.server.silent:
            self.server.record('silent', received_bytes=len(received),
                               tls_record=received[:1] == b'\x16')
            time.sleep(22)
            return
        while b'\r\n\r\n' not in received and len(received) < 4096:
            part = self.request.recv(2048)
            if not part:
                return
            received += part
        path = received.split(b' ', 2)[1].decode('ascii')
        self.server.record(path)
        prefix = b'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n'
        chunked = prefix + b'Transfer-Encoding: chunked\r\n\r\n'
        framing = {
            '/negative-length': prefix + b'Content-Length: -1\r\n\r\n',
            '/length-suffix': prefix + b'Content-Length: 11junk\r\n\r\n{"ok":true}',
            '/duplicate-length': prefix + b'Content-Length: 11\r\nContent-Length: 12\r\n\r\n{"ok":true}',
            '/invalid-chunk': chunked + b'XYZ\r\n',
            '/chunk-suffix': chunked + b'bjunk\r\n{"ok":true}\r\n0\r\n\r\n',
            '/missing-final-crlf': chunked + b'b\r\n{"ok":true}\r\n0\r\n',
            '/missing-chunk-crlf': chunked + b'b\r\n{"ok":true}BAD\r\n0\r\n\r\n',
            '/ambiguous-framing': prefix + b'Content-Length: 11\r\nTransfer-Encoding: chunked\r\n\r\nb\r\n{"ok":true}\r\n0\r\n\r\n',
            '/chunked-trailer': chunked.replace(b'chunked', b'Chunked') + b'b;test="yes"\r\n{"ok":true}\r\n0\r\nX-Trailer: yes\r\n\r\n',
            '/close-delimited': prefix + b'Connection: close\r\n\r\n{"ok":true}',
        }
        if path in framing:
            wire = framing[path]
            for start in range(0, len(wire), 13):
                self.request.sendall(wire[start:start+13])
                time.sleep(0.005)
            return
        if path == '/drop':
            return
        if path == '/truncated':
            self.request.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 64\r\n\r\npartial')
            return
        if path == '/slow-close':
            self.request.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 64\r\n\r\npartial')
            time.sleep(2)
            return
        if path == '/chunked':
            self.request.sendall(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n')
            for chunk in (b'A', b'\0BC', b'DEF'):
                self.request.sendall(f'{len(chunk):x}\r\n'.encode()+chunk+b'\r\n')
                time.sleep(0.03)
            self.request.sendall(b'0\r\n\r\n')
            return
        if path == '/redirect':
            self.request.sendall(b'HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 0\r\n\r\n')
            return
        if path in ('/ok', '/error'):
            status = b'200 OK' if path == '/ok' else b'503 Maintenance'
            body = b'{"ok":true}' if path == '/ok' else b'{"error":"maintenance"}'
            self.request.sendall(b'HTTP/1.1 '+status+b'\r\nContent-Type: application/json\r\nContent-Length: '
                                 + str(len(body)).encode()+b'\r\n\r\n'+body)
            return
        self.request.sendall(b'HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n')


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def record(self, path, **details):
        record = dict(time=time.time(), fixture=path, **details)
        with self.log_lock, self.log.open('a') as file:
            file.write(json.dumps(record)+'\n')
        print(json.dumps(record), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', required=True)
    parser.add_argument('--device', required=True)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--silent-port', type=int, default=8766)
    parser.add_argument('--close-port', type=int, help='Close after a complete ClientHello and two seconds')
    parser.add_argument('--reset-port', type=int, help='Request TCP reset after ClientHello and two seconds')
    parser.add_argument('--stop-file', type=Path, help='Stop all listeners when this marker exists')
    parser.add_argument('--seconds', type=int, default=900)
    parser.add_argument('--log', type=Path, required=True)
    args = parser.parse_args()
    if args.log.exists() or (args.stop_file and args.stop_file.exists()):
        parser.error('Use new log and stop-marker paths to preserve earlier evidence')
    args.log.parent.mkdir(parents=True, exist_ok=True)
    servers = []
    log_lock = threading.Lock()
    try:
        listeners = [(args.port, False, None), (args.silent_port, True, None)]
        listeners += [(port, False, mode) for port, mode in
                      ((args.close_port, 'close'), (args.reset_port, 'reset')) if port is not None]
        for port, silent, mode in listeners:
            server = Server((args.bind, port), Fixture)
            server.silent = silent
            server.handshake_close = mode
            server.allowed = {args.device, args.bind, '127.0.0.1'}
            server.log, server.log_lock = args.log, log_lock
            threading.Thread(target=server.serve_forever, daemon=True).start()
            servers.append(server)
        print('Fixtures ready; automatic shutdown enabled.', flush=True)
        deadline = time.monotonic()+args.seconds
        while time.monotonic() < deadline and not (args.stop_file and args.stop_file.exists()):
            time.sleep(0.2)
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()


if __name__ == '__main__':
    main()
