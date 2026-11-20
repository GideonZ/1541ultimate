"""Exercise the real campaign controller against loopback HTTP/TLS fixtures.

The UCI transport is replaced by a host Python TLS client. This tests runner,
fixture and audit integration, not firmware, ESP32 TLS or physical hardware.
Certificates are temporary test roots, never production trust configuration.
"""
import argparse
import hashlib
import http.client
import json
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import types
from unittest.mock import patch
from urllib.parse import urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import https_hardware_runner as runner
from https_test_endpoint import Endpoint, context
from audit_https_hardware import audit


def block(data=b'', status=b'00,OK', cancelled=False):
    result = bytearray(512)
    result[0] = 0x20
    result[1] = 0 if cancelled else 1
    result[2:4] = len(data).to_bytes(2, 'little')
    result[4] = len(status)
    result[6] = 8 if cancelled else 0
    result[0x30:0x32] = len(data).to_bytes(2, 'little')
    result[0x100:0x100+len(status)] = status
    return result, data


class LocalOutput:
    """Label every persisted runner report before writing simulated UCI results."""
    def __init__(self, directory):
        self.directory = directory

    def __truediv__(self, name):
        target = self.directory / name
        if name != 'run.json':
            return target
        def write_text(text):
            value = json.loads(text)
            value.update(hardware_tested=False, execution_scope='host-TLS with simulated UCI',
                         firmware_executed=False, device_contact=False)
            return target.write_text(json.dumps(value, indent=2)+'\n')
        return types.SimpleNamespace(write_text=write_text)


def campaign(directory, cert, key, other_cert, other_key, stop_during_stall=False):
    directory.mkdir()
    peers = []
    threads = []
    peer_paths = {}
    resets = []
    trace = []
    cancel = threading.Event()
    transport = {'connection': None}
    client_context = ssl.create_default_context(cafile=str(cert))
    client_context.minimum_version = ssl.TLSVersion.TLSv1_2
    class InterruptibleHTTPS(http.client.HTTPSConnection):
        def connect(self):
            raw = self._create_connection((self.host, self.port), self.timeout)
            # Publish the TLS socket before handshake so the host-side cancel
            # substitute closes it immediately, rather than waiting for timeout.
            self.sock = self._context.wrap_socket(raw, server_hostname=self.host,
                                                 do_handshake_on_connect=False)
            self.sock.do_handshake()
    for name, mode, ctx in [('http', 'http', None),
                            ('trusted', 'tls', context(cert, key)),
                            ('untrusted', 'tls', context(other_cert, other_key)),
                            ('stall', 'handshake-stall', None)]:
        path = directory / (name+'.jsonl')
        peer = Endpoint(('127.0.0.1', 0), path, mode=mode, context=ctx, fault_seconds=5)
        thread = threading.Thread(target=peer.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        peers.append((name, peer))
        threads.append(thread)
        peer_paths[name] = str(path)
    ports = {name: peer.server_address[1] for name, peer in peers}
    peer_paths['wrong_name'] = peer_paths['trusted']
    plan = dict(schema=1, candidate_sha256='a'*64, cases=runner.cases(), held=runner.HELD,
                request_guard_seconds=35, idle_seconds=180, endpoints={
                    'http':f'http://127.0.0.1:{ports["http"]}',
                    'trusted':f'https://localhost:{ports["trusted"]}',
                    'wrong_name':f'https://127.0.0.1:{ports["trusted"]}',
                    'untrusted':f'https://localhost:{ports["untrusted"]}',
                    'stall':f'https://localhost:{ports["stall"]}'})
    (directory/'plan.json').write_text(json.dumps(plan, indent=2)+'\n')
    installation = dict(device='loopback-transport-substitute', peer_logs=peer_paths)

    def close_connection():
        connection = transport['connection']
        if connection is not None and connection.sock is not None:
            try:
                connection.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            connection.close()

    class Machine:
        def writemem(self, address, data):
            assert address == 0xc00a and data == b'\1'
            cancel.set()
            close_connection()

        def reset(self, force):
            resets.append(force)

    class Api:
        def __init__(self, *args, **kwargs):
            self.machine = Machine()
            self.runners = object()
            self.configs = types.SimpleNamespace(current=lambda *args:'Enabled')

    class Native:
        def __init__(self, *args, **kwargs):
            assert kwargs['agent_defines'] == runner.DEFINES
            self.poll_hook = None

        def start(self):
            pass

        def _run(self, command, *args):
            if command[1] == 0x10:
                return block()
            if command[1] == 0x11:
                self.method = 'HEAD' if command[2] == 6 else 'GET'
                self.url = command[3:-1].decode('ascii')
                return block(b'\0')
            assert command == bytes.fromhex('063200ff')
            cancel.clear()
            parsed = urlsplit(self.url)
            assert parsed.hostname in ('localhost', '127.0.0.1')
            outcome = {}
            def request():
                connection = None
                try:
                    if parsed.scheme == 'https':
                        connection = InterruptibleHTTPS(parsed.hostname, parsed.port,
                                                        timeout=.75, context=client_context)
                    else:
                        connection = http.client.HTTPConnection(parsed.hostname, parsed.port, timeout=.75)
                    def connect(address, timeout, source_address=None):
                        assert address == (parsed.hostname, parsed.port)
                        return socket.create_connection(('127.0.0.1', parsed.port), timeout)
                    connection._create_connection = connect
                    transport['connection'] = connection
                    connection.request(self.method, parsed.path)
                    response = connection.getresponse()
                    data = response.read()
                    assert response.status == 200
                    outcome.update(data=data, status=b'HTTP/1.1 200 OK')
                except Exception as error:
                    outcome.update(data=b'', status=b'503 SERVICE UNAVAILABLE',
                                   error_type=type(error).__name__, error=str(error))
                finally:
                    if connection is not None:
                        connection.close()
            worker = threading.Thread(target=request)
            worker.start()
            try:
                while worker.is_alive():
                    if stop_during_stall:
                        for path in set(peer_paths.values()):
                            if '"stall_started"' in Path(path).read_text():
                                (directory/'operator.stop').touch()
                    if self.poll_hook:
                        self.poll_hook()
                    worker.join(.005)
            finally:
                close_connection()
                worker.join(3)
                assert not worker.is_alive(), 'Host transport did not settle'
                trace.append(dict(url=self.url, method=self.method, cancelled=cancel.is_set(),
                                  error_type=outcome.get('error_type'), error=outcome.get('error'),
                                  body_bytes=len(outcome.get('data', b''))))
            if cancel.is_set():
                return block(cancelled=True, status=b'')
            return block(outcome['data'], outcome['status'])

    modules = {'bootstrap':types.ModuleType('bootstrap'),
               'api':types.SimpleNamespace(UltimateApi=Api),
               'rest':types.SimpleNamespace(TRANSPORT_RETRIES=3),
               'uci_native':types.SimpleNamespace(NativeUci=Native)}
    expected_stop = False
    try:
        runner.validate_plan(plan)
        runner.validate_peers(installation, plan)
        with patch.dict(sys.modules, modules):
            try:
                runner.run(plan, installation, LocalOutput(directory))
            except ValueError as error:
                if not stop_during_stall or 'Operator stop' not in str(error):
                    raise
                expected_stop = True
    finally:
        for name, peer in peers:
            peer.shutdown()
            peer.close_fixture()
        for thread in threads:
            thread.join(3)
            assert not thread.is_alive()
        (directory/'host-client.json').write_text(json.dumps(trace, indent=2)+'\n')
    saved = json.loads((directory/'run.json').read_text())
    if stop_during_stall:
        assert expected_stop and saved['status'] == 'failed-or-stopped' and resets == []
        try:
            audit(directory, peer_paths.values())
        except ValueError:
            pass
        else:
            raise AssertionError('Stopped run was accepted')
        result = dict(operator_stop_preserved=True, stopped_run_rejected=True, reset_requested=False)
    else:
        assert resets == [True] and len(trace) == 43
        assert sum(row['error_type'] == 'SSLCertVerificationError' for row in trace) == 7
        assert sum(row['cancelled'] for row in trace) == 2
        assert all(case['end']-case['cancel_at'] < .5 for case in saved['cases']
                   if case['cancel_at'] is not None), 'Host substitute cancellation waited for timeout'
        result = audit(directory, peer_paths.values())
    result.update(hardware_tested=False, device_contact=False, execution_scope='host-TLS with simulated UCI')
    (directory/'audit.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    # Prevent accidental LAN/Internet contact even if imported code changes.
    def network_guard(event, values):
        if event in ('socket.connect', 'socket.bind'):
            address = values[1]
            if not isinstance(address, tuple) or address[0] != '127.0.0.1':
                raise RuntimeError('Loopback-only test rejected network operation')
    sys.addaudithook(network_guard)
    with tempfile.TemporaryDirectory(prefix='https-loopback-') as tmp:
        tmp = Path(tmp)
        for name in ('trusted', 'untrusted'):
            subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1',
                            '-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost',
                            '-keyout',str(tmp/(name+'.key')),'-out',str(tmp/(name+'.crt'))],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
            shutil.copyfile(tmp/(name+'.crt'), args.output/(name+'.crt'))
        arguments = [tmp/'trusted.crt',tmp/'trusted.key',tmp/'untrusted.crt',tmp/'untrusted.key']
        print('Running 43 loopback exchanges plus the real 180-second idle interval.', flush=True)
        full = campaign(args.output/'matrix', *arguments)
        print('Matrix audit passed; checking operator stop.', flush=True)
        stopped = campaign(args.output/'operator-stop', *arguments, stop_during_stall=True)
    summary = dict(passed=True, hardware_tested=False, device_contact=False,
                   matrix=full, operator_stop=stopped, private_test_keys_removed=True)
    (args.output/'result.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary), flush=True)


if __name__ == '__main__':
    main()
