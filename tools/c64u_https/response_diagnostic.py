#!/usr/bin/env python3
"""Bounded authenticated response failures and recovery on native target 6.

Replaces C64 RAM. No firmware, network configuration or trust-store changes.
Public fixture behavior is checked separately; it is not a device packet trace.
"""
import argparse
import hashlib
import http.client
import json
import os
import time
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

from long_soak import checked_metrics
from soak import HardwareRun, RestClient

SHORT_URL = 'https://httpbingo.org/response-headers?Content-Length=2048'
FAST_URL = 'https://httpbin.org/drip?duration=1&delay=0&numbytes=20&code=200'
SLOW_URL = 'https://httpbin.org/drip?duration=18&delay=0&numbytes=20&code=200'


def verify_response_failure(before, after, mode, seconds):
    previous = checked_metrics(before)
    current = checked_metrics(after, previous['boot_id'])
    old_tls, fault = previous['last_tls_failure'], current['last_tls_failure']
    old_local = before['values']['https_bridge']['last_failure']
    local = after['values']['https_bridge']['last_failure']
    # A freshly booted device exposes only count=0 until its first failure.
    previous_epoch = 0 if old_local['count'] == 0 else int(old_local['epoch'], 16)
    if local['count'] != old_local['count']+1 or int(local['epoch'], 16) <= previous_epoch:
        raise ValueError('Missing a new management failure for this response')
    if mode == 'short' and fault == old_tls:
        # eReq_Body is 3 in the production HTTP parser's receive-state enum.
        if (local['stage'], local['code'], local['detail']) != ('http_read', 0, 3):
            raise ValueError('Missing HTTP EOF evidence after a clean TLS close')
        category = 'HTTP body truncated after clean TLS close'
    else:
        if (fault['count'] != old_tls['count']+1 or fault['epoch'] != local['epoch']
                or fault['session'] != local['session'] or fault['stage'] != 9 or fault['verify_flags'] != '00000000'):
            raise ValueError('Missing correlated failure after successful TLS authentication')
        if mode == 'short':
            if (fault['status'], fault['code']) != (8, 0) or (local['stage'], local['code'], local['detail']) != ('tls_reply', -8, 2):
                raise ValueError('Missing abrupt TLS EOF evidence')
            category = 'TLS EOF during response'
        elif mode == 'slow':
            local_timeout = (local['stage'], local['code'], local['detail']) in (
                ('tcp_timeout', 0, 0), ('tcp_deadline', 0, 0),
                ('tcp_session_deadline', 0, 0), ('tcp_queue_deadline', 0, 2))
            if fault['status'] == 4:
                if fault['code'] != -0x6c00 or not local_timeout:
                    raise ValueError('Missing read-timeout evidence')
            elif fault['status'] == 3:
                if not local_timeout and (local['stage'], local['code'], local['detail']) != ('tls_reply', -3, 2):
                    raise ValueError('Unexpected source of authenticated read deadline')
            else:
                raise ValueError('Response did not fail at its deadline')
            category = 'Authenticated response read deadline'
        else:
            raise ValueError('Unknown response fault mode')
    if not ((mode == 'short' and 0 < seconds < 10) or (mode == 'slow' and 12 <= seconds <= 19)):
        raise ValueError('Response fault outside expected timing window')
    return {'category': category, 'management': local, 'tls': fault, 'boot_id': current['boot_id']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Use a new evidence directory')
    args.output.mkdir(parents=True)
    outcome = {'passed': False, 'checks': []}
    run = None
    try:
        # Normal system certificate verification remains enabled.
        with urllib.request.urlopen(SHORT_URL, timeout=10) as response:
            try:
                body = response.read()
                incomplete = False
            except http.client.IncompleteRead as error:
                body, incomplete = error.partial, True
            probe = {'observed_at': datetime.now(timezone.utc).isoformat(), 'url': SHORT_URL,
                     'status': response.status, 'declared': int(response.headers['Content-Length']),
                     'received': len(body), 'incomplete': incomplete, 'body_sha256': hashlib.sha256(body).hexdigest()}
        (args.output/'public-fixture-check.json').write_text(json.dumps(probe, indent=2)+'\n')
        if not (probe['status'] == 200 and probe['declared'] == 2048 and 0 < len(body) < 2048 and incomplete):
            raise ValueError('Public short-body fixture is not behaving as required')
        started = time.monotonic()
        with urllib.request.urlopen(SLOW_URL, timeout=25) as response:
            slow_body = response.read()
            slow_probe = {'observed_at': datetime.now(timezone.utc).isoformat(), 'url': SLOW_URL,
                          'status': response.status, 'seconds': time.monotonic()-started,
                          'received': len(slow_body), 'body_sha256': hashlib.sha256(slow_body).hexdigest()}
        (args.output/'slow-fixture-check.json').write_text(json.dumps(slow_probe, indent=2)+'\n')
        if not (slow_probe['status'] == 200 and slow_body == b'*'*20 and 17 <= slow_probe['seconds'] < 30):
            raise ValueError('Public slow-body fixture is not behaving as required')
        rest = RestClient(args.host, password=os.environ.get('ULTIMATE_PASSWORD'), timeout=10)
        heap = json.loads(rest.expect('GET', '/v1/machine:heap'))
        checked_metrics({'values': heap})
        if heap['https_bridge']['controller_minor'] != 18:
            raise ValueError('Requires diagnostic bridge 1.18')
        run = HardwareRun(args.host, args.output)
        (args.output/'response-runner.py').write_bytes(Path(__file__).read_bytes())

        def exchange(label, url, expected, **options):
            record = run.exchange(label, url, expected, **options)
            if not record['passed']:
                raise ValueError('Unexpected native response; original evidence preserved')
            return record

        exchange('authenticated fast body baseline', FAST_URL, b'*'*20)
        for mode, url in (('short', SHORT_URL), ('slow', SLOW_URL)):
            run.sample_heap('before '+mode)
            before = run.heap_samples[-1]
            record = exchange(mode+' authenticated response', url, b'', unavailable=True)
            time.sleep(1.2)
            run.sample_heap('after '+mode)
            after = run.heap_samples[-1]
            evidence = verify_response_failure(before, after, mode, record['seconds'])
            outcome['checks'].append({'mode': mode, **evidence})
            exchange('HTTPS recovery after '+mode, FAST_URL, b'*'*20)
            time.sleep(1.2)
            run.sample_heap('recovery '+mode)
            recovery = run.heap_samples[-1]
            checked_metrics(recovery, evidence['boot_id'])
            for component, field in (('esp32', 'last_tls_failure'), ('https_bridge', 'last_failure')):
                if recovery['values'][component][field] != after['values'][component][field]:
                    raise ValueError('Recovery erased or changed recorded failure')
        outcome['passed'] = True
    except Exception as error:  # noqa: BLE001 - retain every unexpected outcome without retry
        outcome['error'] = str(error)
        (args.output/'fatal.json').write_text(json.dumps({'error': str(error)})+'\n')
    finally:
        if run is not None:
            try:
                run.finish()
            except Exception as error:  # noqa: BLE001 - cleanup must not hide the original failure
                outcome['passed'] = False
                outcome['cleanup_error'] = str(error)
        (args.output/'response-result.json').write_text(json.dumps(outcome, indent=2)+'\n')
    print(json.dumps(outcome), flush=True)
    if not outcome['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
