#!/usr/bin/env python3
"""Hardware cleanup checks using native UCI abort, FREE_ALL and C64 reset.

Replaces the running C64 program/RAM, like soak.py. Never resets the management
CPU/controller, changes settings or flashes firmware. Heap samples cover only
the management allocator. Run separately from all other native-UCI tests.
"""
import argparse
import json
import time
from pathlib import Path

from soak import HardwareRun


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cycles', type=int, default=5)
    args = parser.parse_args()
    if args.output.exists() or not 1 <= args.cycles <= 20:
        parser.error('Choose a new output directory and 1..20 cycles')
    run = HardwareRun(args.host, args.output)
    (args.output / 'runner.py').write_bytes(Path(__file__).read_bytes())
    (args.output / 'soak.py.txt').write_bytes(Path(__file__).with_name('soak.py').read_bytes())
    operations = []

    def header(url):
        created = run.ok(bytes([6, 0x11, 1]) + url.encode('ascii') + b'\0')
        assert created.data == b'\0', 'Header slot zero was not released'
        run.ok(bytes([6, 0x13, 0]) + b'User-Agent: Ultimate-HTTPS-hardware-test/1.0\0')

    def note(kind, scheme, **details):
        record = dict(operation=kind, scheme=scheme, passed=True, **details)
        operations.append(record)
        with (args.output / 'lifecycle.jsonl').open('a') as file:
            file.write(json.dumps(record)+'\n')
        print(f'PASS {scheme} {kind}', flush=True)
        run.sample_heap(scheme+' '+kind)

    def recovery(scheme, label):
        run.exchange(label, scheme+'://httpbingo.org/base64/QQBCfw==', b'A\0B\x7f')

    try:
        for cycle in range(args.cycles):
            for scheme in ('http', 'https'):
                header(scheme+'://httpbingo.org/range/2048')
                count, idle = run.uci.probe_abort(bytes([6, 0x32, 0, 255]))
                assert count == 895 and idle, (count, idle)
                run.ok(bytes([6, 0x12, 0]))
                note('abort after first raw block', scheme, cycle=cycle+1, bytes_read=count)
                recovery(scheme, 'recovery after abort')
        for operation in ('FREE_ALL', 'C64 reset'):
            for scheme in ('http', 'https'):
                header(scheme+'://httpbingo.org/base64/eyJvayI6dHJ1ZX0=')
                reply = run.uci.transact(bytes([6, 0x31, 0, 255]))
                assert reply.status_text == b'200 OK' and len(reply.data) == 2
                queried = run.uci.transact(bytes([6, 0x2a, reply.data[1]])+b'ok\0')
                assert queried.status_text == b'000 OK' and queried.data == b'\x02\x01'
                run.sample_heap(scheme+' live object handles before '+operation)
                if operation == 'FREE_ALL':
                    run.ok(bytes([6, 0x10]))
                else:
                    code, _, body = run.rest.request('PUT', '/v1/machine:reset')
                    assert code == 200, (code, body)
                    time.sleep(0.5)
                    run.uci.start()
                    # Do not issue FREE_ALL here; the reset itself must clean up.
                note(operation+' with live object handles', scheme)
                recovery(scheme, 'recovery after '+operation)
    except Exception as error:
        (args.output / 'fatal.json').write_text(json.dumps({'error': str(error)})+'\n')
        raise
    finally:
        summary = run.finish()
    report = {'cleanup_checks': len(operations), 'recoveries': len(run.records),
                  'failed_recoveries': summary['failed'], 'operations': operations,
                  'configuration_unchanged': summary['configuration_unchanged']}
    (args.output / 'lifecycle-summary.json').write_text(json.dumps(report, indent=2)+'\n')
    if summary['failed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
