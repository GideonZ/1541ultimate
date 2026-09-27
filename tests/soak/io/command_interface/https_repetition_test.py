#!/usr/bin/env python3
"""Bounded native-UCI HTTP/HTTPS repetition, with individual handle cleanup.

Replaces the C64 program/RAM. No firmware or configuration changes. Public,
credential-free GET fixtures only; host-side REST transport may retry reads,
but an HTTP target exchange is never silently retried. Stops on UCI failure.
"""
import argparse
import json
import sys
import time
from pathlib import Path
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import report  # noqa: E402
from https_native import evidence_path, run_suite  # noqa: E402

from https_native import HardwareRun

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser)
    parser.add_argument('--output', type=Path, default=evidence_path('https_repetition_test'))
    parser.add_argument('--rounds', type=int, default=40, help='Two exchanges per round; max 500')
    parser.add_argument('--faults', action='store_true', help='Known bad certificates followed by recovery')
    parser.add_argument('--dns-failure', action='store_true', help='Unresolvable reserved .invalid name')
    parser.add_argument('--http-fixtures', help='URL of the paired LAN fault_server.py HTTP listener')
    parser.add_argument('--framing-fixtures', help='LAN fault_server.py URL; requires the response framing fix')
    parser.add_argument('--stall-url', help='HTTPS URL of a controlled TCP peer silent for over 20 seconds')
    args = parser.parse_args()
    report.apply_colour(args.color)
    if not 1 <= args.rounds <= 500:
        parser.error('rounds must be 1..500')
    if args.output.exists():
        parser.error('output directory must be new, to preserve earlier evidence')
    run = HardwareRun(args.host, args.output, password=args.password, timeout=args.timeout)
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
    run_suite("https-repetition", main)
