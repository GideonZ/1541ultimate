#!/usr/bin/env python3
"""Bounded native-UCI HTTP/HTTPS repetition, with individual handle cleanup.

Replaces the C64 program/RAM. No firmware or configuration changes. Public,
credential-free GET fixtures only; host-side REST transport may retry reads,
but an HTTP target exchange is never silently retried. Stops on UCI failure.
"""
import argparse
import hashlib
import json
import os
import statistics
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tests/lib'), str(ROOT / 'tests/e2e/lib')]
import uci_native
from api import MachineApi, RunnersApi
from rest import RestClient
from uci_native import NativeUci


class HardwareRun:
    def __init__(self, host, output):
        self.output = output
        output.mkdir(parents=True, exist_ok=True)
        self.rest = RestClient(host, password=os.environ.get('ULTIMATE_PASSWORD'), timeout=10)
        self.uci = NativeUci(MachineApi(self.rest), RunnersApi(self.rest),
                             busy_timeout=40, wait_wraps=32, first_status_only=True)
        self.records = []
        self.heap_samples = []
        self.started = time.monotonic()
        self.started_at = datetime.now(timezone.utc).isoformat()
        self.original_configs = self.rest.expect('GET', '/v1/configs/*')
        (output / 'configs-before.json').write_bytes(self.original_configs)
        # Available in newer upstream firmware, optional on the retail baseline.
        code, _, heap = self.rest.request('GET', '/v1/machine:heap')
        self.heap_available = code == 200
        (output / 'heap-before.json').write_text(json.dumps(
            {'http_status': code, 'body': heap.decode(errors='replace')}))
        uci_native.detail = lambda message: None
        self.uci.start()
        self.ok(bytes([6, 0x10]))
        ident = self.uci.transact(bytes([6, 1]))
        assert ident.data.rstrip(b'\0') == b'ULTIMATE HTTP TARGET V1.0'
        self.sample_heap('agent ready')

    def sample_heap(self, label):
        if not self.heap_available:
            return
        code, _, data = self.rest.request('GET', '/v1/machine:heap')
        if code != 200:
            raise RuntimeError(f'Heap diagnostic stopped responding: HTTP {code}')
        sample = {'label': label, 'exchange': len(self.records), 'values': json.loads(data),
                      'observed_at': datetime.now(timezone.utc).isoformat()}
        self.heap_samples.append(sample)
        with (self.output / 'heap-samples.jsonl').open('a') as file:
            file.write(json.dumps(sample)+'\n')

    def ok(self, command):
        reply = self.uci.transact(command)
        if reply.status_text != b'000 OK':
            raise RuntimeError(f'UCI command {command.hex()}: {reply.status_text!r}')
        return reply

    def exchange(self, label, url, expected, *, object_mode=False,
                 status_prefix=b'HTTP/1.1 200 ', unavailable=False, time_bounds=None):
        started_at = datetime.now(timezone.utc).isoformat()
        # Preserve attempts that stop before a complete exchange can be recorded.
        # UTC timestamps let device results be compared with independent network logs.
        with (self.output / 'attempts.jsonl').open('a') as file:
            file.write(json.dumps({'index': len(self.records)+1, 'label': label,
                                       'url': url, 'started_at': started_at})+'\n')
        created = self.ok(bytes([6, 0x11, 1]) + url.encode('ascii') + b'\0')
        assert len(created.data) == 1
        handle = created.data[0]
        self.ok(bytes([6, 0x13, handle]) + b'User-Agent: Ultimate-HTTPS-hardware-test/1.0\0')
        # Native transport failures terminate the run; do not push commands into
        # an interface whose completion has not been established.
        result = self.uci.transact(bytes([6, 0x31 if object_mode else 0x32, handle, 255]), False)
        data = result.data
        errors = []
        if handle != 0:
            errors.append('request header handle was not reused after cleanup')
        if unavailable:
            if result.status_text != b'503 SERVICE UNAVAILABLE' or data:
                errors.append('expected unavailable with no partial body')
        elif object_mode:
            if result.status_text != b'200 OK' or len(data) != 2:
                errors.append('object exchange failed')
            if len(data) == 2:
                rh, rb = data
                if result.status_text == b'200 OK':
                    queried = self.uci.transact(bytes([6, 0x2a, rb, 0]), False)
                    if queried.status_text != b'000 OK' or queried.data != expected:
                        errors.append('object query mismatch')
                self.ok(bytes([6, 0x12, rh]))
                self.ok(bytes([6, 0x22, rb]))
        else:
            if not result.status_text.startswith(status_prefix):
                errors.append('HTTP status mismatch')
            if data != expected:
                errors.append('raw body mismatch')
            lengths = [len(block.data) for block in result.blocks]
            wanted = [895] * (len(expected) // 895) + [len(expected) % 895]
            if lengths != wanted:
                errors.append('continuation lengths mismatch')
        if time_bounds and not time_bounds[0] <= result.elapsed <= time_bounds[1]:
            errors.append('exchange outside expected deadline window')
        self.ok(bytes([6, 0x12, handle]))
        record = {'index': len(self.records)+1, 'label': label, 'url': url, 'passed': not errors,
            'started_at': started_at, 'finished_at': datetime.now(timezone.utc).isoformat(),
            'errors': errors, 'seconds': round(result.elapsed, 3), 'status': result.status_text.decode(errors='replace'),
            'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
            'blocks': [len(b.data) for b in result.blocks], 'request_handle': handle}
        self.records.append(record)
        with (self.output / 'exchanges.jsonl').open('a') as file:
            file.write(json.dumps(record)+'\n')
        print(f"{record['index']:03} {'PASS' if record['passed'] else 'FAIL'} {label} "
              f"{record['seconds']:.2f}s {errors}", flush=True)
        if errors and self.heap_available:
            # Preserve the exchange first. Give the nonblocking one-second
            # telemetry publisher a bounded chance to expose its latched fault.
            # No UCI retry; an unavailable diagnostic must not erase this result.
            time.sleep(1.2)
            try:
                self.sample_heap('after unexpected exchange; telemetry settling')
            except Exception as error:  # noqa: BLE001 - preserve original exchange failure
                with (self.output / 'diagnostic-errors.jsonl').open('a') as file:
                    file.write(json.dumps({'exchange': record['index'], 'error': str(error)})+'\n')
        return record

    def finish(self):
        self.sample_heap('before final C64 reset')
        after = self.rest.expect('GET', '/v1/configs/*')
        (self.output / 'configs-after.json').write_bytes(after)
        unchanged = json.loads(after) == json.loads(self.original_configs)
        heap_after = None
        if self.heap_available:
            code, _, body = self.rest.request('GET', '/v1/machine:heap')
            heap_after = {'http_status': code, 'body': body.decode(errors='replace')}
        code, _, body = self.rest.request('PUT', '/v1/machine:reset')
        assert code == 200, (code, body)
        info = json.loads(self.rest.expect('GET', '/v1/info'))
        failure_file = self.output / 'fatal.json'
        fatal_error = json.loads(failure_file.read_text()).get('error') if failure_file.exists() else None
        summary = {'exchanges': len(self.records), 'failed': sum(not r['passed'] for r in self.records),
            'started_at': self.started_at, 'finished_at': datetime.now(timezone.utc).isoformat(),
            'completed_without_exception': not failure_file.exists(), 'fatal_error': fatal_error,
            'wall_seconds': round(time.monotonic()-self.started, 1), 'configuration_unchanged': unchanged,
            'heap_available': self.heap_available, 'heap_after': heap_after, 'final_info': info,
            'heap_samples': self.heap_samples,
            'c64_program_stopped': True, 'timings': {}}
        for scheme in ('http', 'https'):
            times = [r['seconds'] for r in self.records if r['url'].startswith(scheme+'://') and r['passed']]
            if times:
                summary['timings'][scheme] = {'count': len(times), 'median': statistics.median(times), 'max': max(times)}
        (self.output / 'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
        print(json.dumps(summary), flush=True)
        assert unchanged, 'Configuration changed during test'
        return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=40, help='Two exchanges per round; max 500')
    parser.add_argument('--faults', action='store_true', help='Known bad certificates followed by recovery')
    parser.add_argument('--dns-failure', action='store_true', help='Unresolvable reserved .invalid name')
    parser.add_argument('--http-fixtures', help='URL of the paired LAN fault_server.py HTTP listener')
    parser.add_argument('--framing-fixtures', help='LAN fault_server.py URL; requires the response framing fix')
    parser.add_argument('--stall-url', help='HTTPS URL of a controlled TCP peer silent for over 20 seconds')
    args = parser.parse_args()
    if not 1 <= args.rounds <= 500:
        parser.error('rounds must be 1..500')
    if args.output.exists():
        parser.error('output directory must be new, to preserve earlier evidence')
    run = HardwareRun(args.host, args.output)
    (args.output / 'runner.py').write_bytes(Path(__file__).read_bytes())
    try:
        for round_number in range(args.rounds):
            for scheme in ('http', 'https'):
                if round_number % 4 == 0:
                    run.exchange(f'{scheme} JSON {round_number+1}',
                        scheme+'://httpbingo.org/base64/eyJvayI6dHJ1ZX0=',
                        bytes.fromhex('0401026f6b0201'), object_mode=True)
                else:
                    n = (894, 895, 896, 1024, 1790, 2048)[round_number % 6]
                    run.exchange(f'{scheme} raw {n} round {round_number+1}',
                        f'{scheme}://httpbingo.org/range/{n}', bytes(97+i%26 for i in range(n)))
            time.sleep(0.5)
            if (round_number + 1) % 10 == 0:
                run.sample_heap(f'round {round_number+1}')
        if args.faults:
            for name in ('expired', 'wrong.host', 'self-signed'):
                run.exchange(name, f'https://{name}.badssl.com/', b'', unavailable=True)
                run.exchange('recovery after '+name, 'https://httpbingo.org/base64/QQBCfw==', b'A\0B\x7f')
        if args.dns_failure:
            run.exchange('unresolvable name', 'https://ultimate-https-test.invalid/',
                         b'', unavailable=True, time_bounds=(0, 19))
            run.exchange('recovery after DNS failure', 'https://httpbingo.org/base64/QQBCfw==', b'A\0B\x7f')
        if args.http_fixtures:
            base = args.http_fixtures.rstrip('/')
            run.exchange('LAN baseline', base+'/ok', b'{"ok":true}')
            run.exchange('fragmented chunked binary', base+'/chunked', b'A\0BCDEF')
            run.exchange('HTTP error is a response', base+'/error', b'{"error":"maintenance"}',
                         status_prefix=b'HTTP/1.1 503 ')
            run.exchange('redirect is not followed', base+'/redirect', b'', status_prefix=b'HTTP/1.1 302 ')
            for path in ('drop', 'truncated', 'slow-close'):
                run.exchange('connection fault '+path, base+'/'+path, b'', unavailable=True, time_bounds=(0, 8))
                run.exchange('LAN recovery after '+path, base+'/ok', b'{"ok":true}')
        if args.stall_url:
            run.exchange('silent peer handshake deadline', args.stall_url,
                         b'', unavailable=True, time_bounds=(12, 19))
            run.exchange('recovery after deadline', 'https://httpbingo.org/base64/QQBCfw==', b'A\0B\x7f')
        if args.framing_fixtures:
            base = args.framing_fixtures.rstrip('/')
            for path in ('negative-length', 'length-suffix', 'duplicate-length', 'invalid-chunk',
                         'chunk-suffix', 'missing-final-crlf', 'missing-chunk-crlf', 'ambiguous-framing'):
                run.exchange('malformed '+path, base+'/'+path, b'', unavailable=True, time_bounds=(0, 8))
                run.exchange('recovery after '+path, base+'/ok', b'{"ok":true}')
            for path in ('chunked-trailer', 'close-delimited'):
                run.exchange('valid '+path, base+'/'+path, b'{"ok":true}')
    except Exception as error:
        (args.output / 'fatal.json').write_text(json.dumps({'error': str(error)})+'\n')
        raise
    finally:
        summary = run.finish()
    if summary['failed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
