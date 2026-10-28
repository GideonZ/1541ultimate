"""Run one prepared ESP32-S3 image on the isolated test TAP; never flash."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import select
import signal
import socket
import ssl
import subprocess
import sys
import time

CASES = ['valid', 'hostname', 'untrusted', 'expired', 'future', 'no-roots',
         'no-clock', 'no-entropy', 'handshake-timeout', 'read-timeout',
         'clock-handshake-revoke', 'entropy-read-revoke', 'handshake-cancel', 'read-cancel']


def assess_resources(text, run):
    fields = ('phase', 'batch', 'ms', 'free', 'largest', 'tasks', 'active',
              'time_wait', 'bound', 'pcb_size', 'pcb_limit', 'run')
    pattern = (r'^RESOURCE phase=(\S+) batch=(\d+) ms=(\d+) free=(\d+) largest=(\d+) '
               r'tasks=(\d+) active=(\d+) time_wait=(\d+) bound=(\d+) pcb_size=(\d+) '
               r'pcb_limit=(\d+) run=(\S+)\r?$')
    rows = [dict(zip(fields, row)) for row in re.findall(pattern, text, re.M)]
    for row in rows:
        for field in fields[1:-1]:
            row[field] = int(row[field])
    def require(condition, message):
        if not condition:
            raise ValueError(message)
    require(rows and all(r['run'] == run for r in rows), 'Missing or foreign resource records')
    require([r['ms'] for r in rows] == sorted(r['ms'] for r in rows), 'Resource time went backwards')
    require([r['phase'] for r in rows if r['phase'] not in ('warmup-cleanup', 'repeat-cleanup')]
            == ['cold', 'batch', 'batch', 'batch'], 'Unexpected resource phases')
    require([r['batch'] for r in rows if r['phase'] == 'batch'] == [1, 2, 3], 'Missing repetition batch')
    settled = []
    for phase in ('warmup-cleanup', 'repeat-cleanup'):
        samples = [r for r in rows if r['phase'] == phase]
        require(len(samples) >= 2 and [r['batch'] for r in samples] == [0]*(len(samples)-1)+[1], 'Incomplete cleanup samples')
        last = samples[-1]
        require(last['ms']-samples[0]['ms'] <= 152500, 'Cleanup exceeded its fixed limit')
        require(last['active'] == last['time_wait'] == last['bound'] == 0, 'TCP objects retained after cleanup')
        settled.append(last)
    before, after = settled
    require(after['free'] >= before['free'] and after['largest'] >= before['largest'], 'Quiescent memory did not recover')
    require(after['tasks'] == before['tasks'], 'Temporary tasks remain')
    require(all(r['active']+r['time_wait'] <= r['pcb_limit'] and not r['bound'] for r in rows), 'TCP retention exceeds limit')
    return dict(passed=True, snapshots=rows, baseline=before, final=after)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--tls-root', type=Path, required=True)
    parser.add_argument('--qemu', type=Path, required=True)
    parser.add_argument('--uid', type=int, default=1000)
    args = parser.parse_args()
    directory = args.fixture.resolve()
    if os.geteuid() != 0 or args.uid <= 0:
        parser.error('Private TAP setup requires root and a non-root guest uid')
    build = json.loads((directory / 'build.json').read_text())
    heap_check = build.get('heap_check', False)
    renewal_check = build.get('renewal_check', False)
    task_check = build.get('task_check', False)
    if sum(map(bool, (heap_check, renewal_check, task_check))) > 1:
        raise ValueError('Use separate fixed heap, renewal and owner-task profiles')
    run_limit = 480 if heap_check else 150
    if not build['passed'] or abs(time.time() - build['utc']) > 3600 or (directory / 'result.json').exists():
        raise ValueError('Require a fresh, unused, successfully built fixture')
    for name, digest in build['artifacts'].items():
        if hashlib.sha256((directory / name).read_bytes()).hexdigest() != digest:
            raise ValueError('Changed artifact: ' + name)
    sys.path.insert(0, str(args.tls_root / 'tests'))
    from run_probe import Peers, REQUEST
    import pwd
    user = pwd.getpwuid(args.uid).pw_name

    class RuntimePeers(Peers):
        def server(self, listener, name, mode='normal'):
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
            context.set_ciphers(self.cipher)
            context.load_cert_chain(directory / (name + '.pem'), directory / (('trusted' if name in ('expired', 'future') else name) + '.key'))
            context.set_servername_callback(lambda stream, host, config: self.record(kind='sni', name=host, server=name))
            while not self.stop.is_set():
                try:
                    connection, _ = listener.accept()
                except socket.timeout:
                    continue
                self.record(kind='accepted', server=name, mode=mode)
                connection.settimeout(10 if task_check else 6)
                try:
                    if mode == 'handshake':
                        hello = connection.recv(4096)
                        self.record(kind='client-hello', tls_record=hello[:1] == b'\x16')
                        while connection.recv(4096):
                            pass
                        continue
                    with context.wrap_socket(connection, server_side=True) as stream:
                        self.record(kind='authenticated', server=name, cipher=stream.cipher()[0], protocol=stream.version())
                        request = b''
                        while b'\r\n\r\n' not in request and len(request) < 2048:
                            part = stream.recv(512)
                            if not part:
                                break
                            request += part
                        self.record(kind='request', server=name, exact=request == REQUEST, hex=request.hex(), mode=mode)
                        if request != REQUEST:
                            raise ValueError('Unexpected application bytes')
                        if mode == 'read':
                            while stream.recv(512):
                                pass
                        else:
                            if mode == 'delayed':
                                time.sleep(6 if task_check else 2.2)
                            for offset in range(0, len(self.response), 7):
                                stream.sendall(self.response[offset:offset+7])
                            try:
                                stream.unwrap().close()
                            except (ssl.SSLEOFError, ConnectionResetError):
                                pass
                except ssl.SSLError as error:
                    self.record(kind='tls-alert', server=name, reason=error.reason)
                except ConnectionResetError:
                    pass
                except (OSError, ValueError) as error:
                    self.record(kind='fixture-error', error=str(error))
                finally:
                    connection.close()
                    self.record(kind='closed', server=name, mode=mode)

    peers = RuntimePeers(directory, ecdsa=build['profile'] == 'ecdsa')
    tap = args.tls_root / 'tools/tap.py'
    command = ['runuser', '-u', user, '--', str(args.qemu), '-machine', 'esp32s3',
               '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
               '-drive', f'file={directory / "flash.bin"},if=mtd,format=raw', '-snapshot',
               '-nic', 'tap,model=open_eth,ifname=ult-tls0,script=no,downscript=no']
    output = bytearray()
    process = None
    failure = None
    completed = False
    start = time.monotonic()
    subprocess.run([sys.executable, str(tap), 'create', '--uid', str(args.uid)], check=True)
    try:
        peers.listen(socket.SOCK_DGRAM, 53, peers.dns)
        for port, name, mode in [(8443, 'trusted', 'normal'), (8445, 'untrusted', 'normal'),
                                 (8456, 'expired', 'normal'), (8457, 'future', 'normal'),
                                 (8446, 'trusted', 'handshake'), (8447, 'trusted', 'read')]:
            peers.listen(socket.SOCK_STREAM, port, peers.server, name, mode)
        if renewal_check or task_check:
            peers.listen(socket.SOCK_STREAM, 8448, peers.server, 'trusted', 'delayed')
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        while time.monotonic() - start < run_limit:
            if (directory / 'operator.stop').exists():
                failure = 'Operator stopped incomplete run'
                break
            if select.select([process.stdout], [], [], .25)[0]:
                chunk = os.read(process.stdout.fileno(), 65536)
                if not chunk:
                    break
                output.extend(chunk)
                with (directory / 'guest-progress.log').open('ab') as log:
                    log.write(chunk)
                if b'main_task: Returned from app_main()' in output:
                    completed = True
                    break
            if process.poll() is not None:
                break
        if not completed and not failure:
            failure = f'Guest did not complete within {run_limit} seconds'
    except Exception as error:
        failure = repr(error)
    finally:
        if process is not None:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
            output.extend(process.stdout.read())
        peers.stop.set()
        for thread in peers.threads:
            thread.join(7)
        for listener in peers.sockets:
            listener.close()
        subprocess.run([sys.executable, str(tap), 'remove'], check=True)
    (directory / 'guest.log').write_bytes(output)
    (directory / 'peers.json').write_text(json.dumps(peers.events, indent=2) + '\n')
    text = output.decode(errors='replace')
    if task_check:
        from assess_provider_task import assess
        result = assess(text, peers.events, directory.name, peers.cipher, REQUEST)
        result.update(completed=completed, failure=failure, seconds=time.monotonic()-start,
                      command=command, hardware_tested=False,
                      qemu_sha256=hashlib.sha256(args.qemu.read_bytes()).hexdigest())
        result['passed'] = result['passed'] and completed and not failure
        (directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2))
        return 0 if result['passed'] else 1
    labels = [name for case in CASES for name in (case, 'recovery')]
    if heap_check:
        labels += ['repeat'] * 96
    if renewal_check:
        labels += ['recovery'] * 6
    attempts = re.findall(r'^ATTEMPT name=([\w-]+) run=(\S+)\r?$', text, re.M)
    rows = re.findall(r'^RESULT name=([\w-]+) status=(\d+) flags=(\d+) elapsed_ms=(\d+) cancel_return=(\d+) stack=(\d+) run=(\S+)\r?$', text, re.M)
    summaries = re.findall(r'^SUMMARY checks=(\d+) failures=(\d+) run=(\S+)\r?$', text, re.M)
    complete = ([r[0] for r in attempts] == labels == [r[0] for r in rows] and
                all(r[1] == directory.name for r in attempts) and all(r[-1] == directory.name for r in rows))
    heaps = re.findall(r'^HEAP name=([\w-]+) before=(\d+) after=(\d+) minimum=(\d+) run=(\S+)\r?$', text, re.M)
    complete &= ([r[0] for r in heaps] == labels and all(r[-1] == directory.name for r in heaps))
    timing = True
    for name, status, flags, elapsed, cancel_return, stack, _ in rows:
        status, flags, elapsed, cancel_return, stack = map(int, (status, flags, elapsed, cancel_return, stack))
        timing &= stack >= 1024
        if name in ('valid', 'recovery', 'repeat'):
            timing &= status == flags == 0
        elif name.endswith('timeout'):
            budget = 3000 if name.startswith('read') else 1500
            timing &= status == 3 and budget <= elapsed <= budget+250
        elif name.endswith(('cancel', 'revoke')):
            timing &= status == 2 and cancel_return <= 250
        elif name.startswith('no-'):
            timing &= status != 0 and elapsed <= 500
        else:
            timing &= status == 5 and flags != 0
    kinds = Counter(e['kind'] for e in peers.events)
    extra = 96 if heap_check else 12 if renewal_check else 0
    peer_ok = kinds['accepted'] == kinds['closed'] == 25+extra and kinds['request'] == kinds['authenticated'] == 18+extra
    peer_ok &= kinds['client-hello'] == 3 and all(e['tls_record'] for e in peers.events if e['kind'] == 'client-hello')
    peer_ok &= kinds['tls-alert'] == 4 and not kinds['fixture-error']
    peer_ok &= all(e['exact'] and bytes.fromhex(e['hex']) == REQUEST for e in peers.events if e['kind'] == 'request')
    peer_ok &= all(e['cipher'] == peers.cipher and e['protocol'] == 'TLSv1.2' for e in peers.events if e['kind'] == 'authenticated')
    resources = None
    if heap_check:
        try:
            resources = assess_resources(text, directory.name)
        except ValueError as error:
            resources = dict(passed=False, error=str(error))
    long_results = []
    if renewal_check:
        long_results = re.findall(r'^LONG_RESULT name=(\S+) status=(\d+) elapsed_ms=(\d+) accepted=(\d+) rejected=(\d+) sent=(\d+) signal_return=(\d+) stack=(\d+) run=(\S+)\r?$', text, re.M)
        names = ['renewed-read', 'renewed-timeout', 'missing-renewal', 'replayed-renewal', 'renewed-close', 'renewed-offer']
        timing &= [r[0] for r in long_results] == names and all(r[-1] == directory.name for r in long_results)
        timing &= re.findall(r'^LONG_ATTEMPT name=(\S+) run=(\S+)\r?$', text, re.M) == [(n, directory.name) for n in names]
        for name, status, elapsed, accepted, rejected, sent, delay, stack, _ in long_results:
            status, elapsed, accepted, rejected, sent, delay, stack = map(int, (status, elapsed, accepted, rejected, sent, delay, stack))
            timing &= stack >= 1024
            if name == 'renewed-read':
                timing &= status == 0 and 2200 <= elapsed <= 4000 and accepted >= 4
            elif name == 'renewed-timeout':
                timing &= status == 3 and 3500 <= elapsed <= 3750 and accepted >= 6
            elif name == 'missing-renewal':
                timing &= status == 2 and 1450 <= elapsed <= 1750 and accepted == sent == 0
            elif name == 'replayed-renewal':
                timing &= status == 2 and 1950 <= elapsed <= 2250 and accepted == 1 and rejected >= 1
            else:
                timing &= status == 2 and delay <= 250 and accepted >= 2
    expected_checks = 1405 if heap_check else 505 if renewal_check else 337
    passed = (completed and not failure and complete and timing and peer_ok and
              (not heap_check or resources['passed']) and len(summaries) == 1 and
              summaries[0] == (str(expected_checks), '0', directory.name) and
              len(re.findall(r'^PASS ', text, re.M)) == int(summaries[0][0]) and
              text.count('ESP-ROM:esp32s3-20210327') == 1 and
              not re.search(r'^FAIL|Guru Meditation|assert failed|abort\(\) was called', text, re.M))
    result = dict(passed=bool(passed), completed=completed, failure=failure, complete_attempts=complete,
                  timing_passed=bool(timing), peer_accounting_passed=bool(peer_ok), peer_counts=dict(kinds),
                  checks=int(summaries[0][0]) if len(summaries) == 1 else None, observations=rows,
                  resources=resources, long_results=long_results, seconds=time.monotonic()-start, command=command, hardware_tested=False,
                  qemu_sha256=hashlib.sha256(args.qemu.read_bytes()).hexdigest())
    (directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
