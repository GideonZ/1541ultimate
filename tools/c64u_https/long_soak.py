#!/usr/bin/env python3
"""Timed native HTTP/HTTPS soak requiring fresh ESP32 memory telemetry.

Replaces the C64 program/RAM. No flashing. Stops on the first unexpected result,
telemetry loss, controller restart or operator stop file; none is a pass.
"""
import argparse
import json
import re
import time
from pathlib import Path

from soak import HardwareRun


def checked_metrics(sample, boot_id=None):
    metrics = sample.get('values', {}).get('esp32', {})
    if metrics.get('available') is not True:
        raise ValueError('Fresh ESP32 telemetry unavailable; install the metrics build first')
    for name in ('internal_free', 'internal_min_ever_free', 'internal_largest_free_block',
                 'free_8bit', 'min_ever_free_8bit', 'largest_free_block_8bit',
                 'tls_stack_min_free_bytes', 'uptime_seconds', 'sample_age_ms'):
        value = metrics.get(name)
        if type(value) is not int or value < 0:
            raise ValueError('Invalid ESP32 measurement: '+name)
    if metrics['sample_age_ms'] > 30000 or metrics['tls_stack_min_free_bytes'] == 0:
        raise ValueError('ESP32 sample stale or TLS stack exhausted')
    for name in ('boot_id', 'sample_sequence'):
        if not isinstance(metrics.get(name), str) or not re.fullmatch('[0-9a-f]{8}', metrics[name]):
            raise ValueError('Invalid ESP32 identity: '+name)
    if boot_id is not None and metrics['boot_id'] != boot_id:
        raise ValueError('ESP32 restarted during the soak')
    return metrics


def exercise(run, seconds, stop_file, clock=time.monotonic, sleep=time.sleep):
    baseline = checked_metrics(run.heap_samples[-1])
    started = clock()
    rounds = 0
    while clock()-started < seconds:
        if stop_file is not None and stop_file.exists():
            raise InterruptedError('Operator stop file; duration not completed')
        for scheme in ('http', 'https'):
            if rounds % 4 == 0:
                result = run.exchange(f'{scheme} JSON round {rounds+1}',
                    scheme+'://httpbingo.org/base64/eyJvayI6dHJ1ZX0=',
                    bytes.fromhex('0401026f6b0201'), object_mode=True)
            else:
                size = (894, 895, 896, 1024, 1790, 2048)[rounds % 6]
                result = run.exchange(f'{scheme} raw {size} round {rounds+1}',
                    f'{scheme}://httpbingo.org/range/{size}', bytes(97+i%26 for i in range(size)))
            if not result['passed']:
                raise RuntimeError('Unexpected HTTP/HTTPS result; see exchanges.jsonl')
        rounds += 1
        if rounds % 10 == 0:
            run.sample_heap(f'timed soak round {rounds}')
            checked_metrics(run.heap_samples[-1], baseline['boot_id'])
        sleep(0.5)
    run.sample_heap('timed soak complete')
    checked_metrics(run.heap_samples[-1], baseline['boot_id'])
    return {'duration_completed': True, 'elapsed_seconds': clock()-started, 'rounds': rounds,
            'controller_boot_id': baseline['boot_id']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--seconds', type=int, default=7200, help='60..14400; defaults to two hours')
    parser.add_argument('--stop-file', type=Path)
    args = parser.parse_args()
    if args.output.exists() or not 60 <= args.seconds <= 14400:
        parser.error('Use a new output directory and a duration of 60..14400 seconds')
    if args.stop_file is not None and args.stop_file.exists():
        parser.error('Stop file already exists')
    run = HardwareRun(args.host, args.output)
    (args.output/'long-runner.py').write_bytes(Path(__file__).read_bytes())
    (args.output/'exchange-runner.py').write_bytes(Path(__file__).with_name('soak.py').read_bytes())
    outcome = {'requested_seconds': args.seconds, 'duration_completed': False, 'passed': False}
    try:
        outcome.update(exercise(run, args.seconds, args.stop_file))
    except (Exception, KeyboardInterrupt) as error:  # noqa: BLE001 - preserve every failed run before cleanup
        outcome['error'] = str(error) or type(error).__name__
        (args.output/'fatal.json').write_text(json.dumps({'error': outcome['error']})+'\n')
    finally:
        try:
            summary = run.finish()
            for sample in run.heap_samples:
                checked_metrics(sample, outcome.get('controller_boot_id'))
            outcome['passed'] = (outcome['duration_completed'] and 'error' not in outcome
                                 and summary['failed'] == 0 and summary['configuration_unchanged'])
        except (Exception, KeyboardInterrupt) as error:  # noqa: BLE001 - cleanup failure must remain in the report
            outcome['cleanup_error'] = str(error) or type(error).__name__
        (args.output/'long-result.json').write_text(json.dumps(outcome, indent=2)+'\n')
    print(json.dumps(outcome), flush=True)
    if not outcome['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
