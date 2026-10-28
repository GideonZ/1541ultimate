"""Bounded loopback peers for the HTTP adapter; never contacts a device."""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import ssl
import subprocess
import sys
import threading
import time

REQUEST = b'GET /probe HTTP/1.1\r\nHost: tls.lab.test\r\nConnection: close\r\n\r\n'
CASES = ['valid', 'clean-eof', 'hostname', 'untrusted', 'expired', 'future', 'no-time',
         'no-entropy', 'allocation', 'pre-cancel', 'handshake-timeout',
         'handshake-cancel', 'read-timeout', 'total-timeout', 'read-cancel',
         'write-timeout', 'write-cancel', 'truncated', 'close-clean', 'close-truncated', 'closed-before-open']
ROUTING_CASES = [case for case in CASES if case not in ('clean-eof', 'allocation', 'write-timeout', 'write-cancel', 'closed-before-open')] + ['no-provider', 'no-create']
ROUTING_CASES += ['command-pre-cancel', 'command-handshake-cancel', 'command-read-cancel']
ROUTING_CASES += ['provider-handshake-cancel', 'provider-read-cancel']
OWNER_CASES = ['owner-handshake-cancel', 'owner-read-cancel', 'owner-loss-read-cancel']
WORKER_CASES = ['worker-handshake-cancel', 'worker-close-read-cancel', 'worker-loss-read-cancel']
CONNECTED_CASES = ['valid', 'hostname', 'untrusted', 'expired', 'future',
                   'handshake-timeout', 'read-timeout', 'total-timeout',
                   'command-handshake-cancel', 'command-read-cancel',
                   'provider-handshake-cancel', 'provider-read-cancel',
                   'manager-handshake-cancel', 'manager-read-cancel']


class Peer:
    def __init__(self, directory, kind, ecdsa=False):
        self.kind = kind
        self.events = []
        self.stop = threading.Event()
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.port = self.listener.getsockname()[1]
        self.listener.listen(4)
        self.listener.settimeout(.1)
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version = self.context.maximum_version = ssl.TLSVersion.TLSv1_2
        self.context.set_ciphers('ECDHE-ECDSA-AES128-GCM-SHA256' if ecdsa else 'ECDHE-RSA-AES128-GCM-SHA256')
        self.context.load_cert_chain(directory / (kind + '.pem' if kind in ('expired', 'future') else 'trusted.pem'), directory / 'trusted.key')
        self.context.set_servername_callback(lambda stream, host, context: self.events.append({'kind': 'sni', 'host': host}))
        self.thread = threading.Thread(target=self.serve)
        self.thread.start()

    def serve(self):
        while not self.stop.is_set():
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            self.events.append({'kind': 'accepted'})
            connection.settimeout(5)
            stream = None
            try:
                if self.kind == 'handshake':
                    hello = connection.recv(4096)
                    self.events.append({'kind': 'client-hello', 'tls_record': hello[:2] == b'\x16\x03'})
                    while connection.recv(4096):
                        pass
                else:
                    stream = self.context.wrap_socket(connection, server_side=True)
                    self.events.append({'kind': 'authenticated', 'cipher': stream.cipher()[0]})
                    request = b''
                    while b'\r\n\r\n' not in request and len(request) <= 4096:
                        data = stream.recv(4096)
                        if not data:
                            break
                        request += data
                    if self.kind == 'write-stall' and not request:
                        self.events.append({'kind': 'no-application-data'})
                        continue
                    self.events.append({'kind': 'request', 'hex': request.hex(), 'exact': request == REQUEST})
                    if request != REQUEST:
                        raise ValueError('Unexpected application request')
                    if self.kind == 'stall':
                        while stream.recv(4096):
                            pass
                    else:
                        framing = b'' if self.kind.startswith('close-') else b'Content-Length: 8\r\n'
                        response = b'HTTP/1.1 200 OK\r\n' + framing + b'Connection: close\r\n\r\n' + (b'tls' if self.kind == 'truncated' else b'tls-ok!\n')
                        for offset in range(0, len(response), 7):
                            stream.sendall(response[offset:offset + 7])
                        self.events.append({'kind': 'response', 'bytes': len(response)})
                        if self.kind in ('truncated', 'close-truncated'):
                            with socket.socket(fileno=stream.detach()) as raw:
                                raw.shutdown(socket.SHUT_WR)
                        elif self.kind in ('clean-eof', 'close-clean'):
                            self.events.append({'kind': 'close-notify'})
                            stream.unwrap()
                        else:
                            # HTTP Content-Length defines completion; the client
                            # destroys its TLS session without waiting for shutdown.
                            while stream.recv(4096):
                                pass
            except (OSError, ValueError) as error:
                self.events.append({'kind': 'peer-error', 'type': type(error).__name__, 'error': str(error)})
            finally:
                if stream is not None:
                    stream.close()
                connection.close()
                self.events.append({'kind': 'closed'})

    def close(self):
        self.stop.set()
        self.thread.join(6)
        self.listener.close()
        if self.thread.is_alive():
            raise RuntimeError('Peer did not terminate')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--tls-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--case', choices=CASES + ROUTING_CASES + OWNER_CASES + WORKER_CASES + CONNECTED_CASES)
    parser.add_argument('--ecdsa', action='store_true')
    parser.add_argument('--routing', action='store_true')
    parser.add_argument('--owner', action='store_true')
    parser.add_argument('--worker', action='store_true')
    parser.add_argument('--connected', action='store_true')
    parser.add_argument('--uart', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(args.tls_root / 'tests'))
    from run_probe import make_certificate
    from security_peers import make_dated_certificates
    make_certificate(args.output, 'trusted', args.ecdsa)
    make_certificate(args.output, 'untrusted', args.ecdsa)
    make_dated_certificates(args.output)
    results = []
    for case in [args.case] if args.case else (CONNECTED_CASES if args.connected or args.uart else WORKER_CASES if args.worker else OWNER_CASES if args.owner else ROUTING_CASES if args.routing else CASES):
        peer_case = case.removeprefix('command-').removeprefix('provider-').removeprefix('owner-').removeprefix('worker-').removeprefix('loss-').removeprefix('manager-')
        if case.startswith('worker-close-'): peer_case = peer_case.removeprefix('close-')
        kind = peer_case if peer_case in ('expired', 'future', 'truncated', 'clean-eof', 'close-clean', 'close-truncated') else ('handshake' if peer_case.startswith('handshake-') else ('stall' if peer_case in ('read-timeout', 'total-timeout', 'read-cancel') else 'valid'))
        if case in ('write-timeout', 'write-cancel'):
            kind = 'write-stall'
        primary = Peer(args.output, kind, args.ecdsa)
        recovery = Peer(args.output, 'valid', args.ecdsa)
        try:
            ca = args.output / ('untrusted-ca.pem' if case == 'untrusted' else 'trusted-ca.pem')
            command = [str(args.binary), case, str(ca), str(primary.port), str(args.output / 'trusted-ca.pem'), str(recovery.port)]
            if args.routing:
                command.append('--routing')
            if args.owner:
                command.append('--owner')
            if args.worker:
                command.append('--worker')
            if args.connected:
                command.append('--connected')
            if args.uart:
                command.append('--uart')
            begin = time.monotonic()
            run = subprocess.run(command, capture_output=True, timeout=12, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1'})
            elapsed = time.monotonic() - begin
        finally:
            primary.close()
            recovery.close()
        raw = run.stdout + run.stderr
        (args.output / (case + '.log')).write_bytes(raw)
        events = {'primary': primary.events, 'recovery': recovery.events}
        (args.output / (case + '-peers.json')).write_text(json.dumps(events, indent=2) + '\n')
        no_open = peer_case in ('no-time', 'no-entropy', 'allocation', 'pre-cancel', 'no-provider', 'no-create', 'closed-before-open')
        application = peer_case in ('valid', 'clean-eof', 'read-timeout', 'total-timeout', 'read-cancel', 'truncated', 'close-clean', 'close-truncated')
        peer_ok = True
        for name, expected_accepts, expected_request in [('primary', int(not no_open), application), ('recovery', 1, True)]:
            observed = events[name]
            requests = [event for event in observed if event['kind'] == 'request']
            peer_ok &= sum(event['kind'] == 'accepted' for event in observed) == expected_accepts
            peer_ok &= sum(event['kind'] == 'closed' for event in observed) == expected_accepts
            peer_ok &= len(requests) == int(expected_request) and all(event['exact'] for event in requests)
            if expected_request:
                peer_ok &= sum(event['kind'] == 'authenticated' for event in observed) == 1
            peer_ok &= not any(event['kind'] == 'peer-error' and event['type'] not in ('SSLError', 'SSLEOFError', 'ConnectionResetError') for event in observed)
        if kind == 'handshake':
            peer_ok &= any(event['kind'] == 'client-hello' and event['tls_record'] for event in primary.events)
        if kind == 'write-stall':
            peer_ok &= any(event['kind'] == 'no-application-data' for event in primary.events)
        if kind in ('clean-eof', 'close-clean'):
            peer_ok &= sum(event['kind'] == 'close-notify' for event in primary.events) == 1
        summaries = re.findall(rb'TLS (?:adapter|routing|owner|worker|connected|uart): (\d+) checks, (\d+) failures', raw)
        passed = run.returncode == 0 and peer_ok and len(summaries) == 1 and summaries[0][1] == b'0'
        passed &= not re.search(rb'ERROR: (?:AddressSanitizer|LeakSanitizer)|runtime error:', raw)
        results.append({'case': case, 'returncode': run.returncode, 'checks': int(summaries[0][0]) if summaries else 0, 'peer_ok': bool(peer_ok), 'elapsed_seconds': elapsed, 'passed': bool(passed)})
        print(case, results[-1], flush=True)
        if not passed:
            print(raw.decode(errors='replace'), flush=True)
    (args.output / 'results.json').write_text(json.dumps({'ecdsa': args.ecdsa, 'results': results, 'passed': all(r['passed'] for r in results)}, indent=2) + '\n')
    return 0 if all(r['passed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
