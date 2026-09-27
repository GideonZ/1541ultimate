#!/usr/bin/env python3
"""Repeat TLS handshakes locally on C64 through an operator Wi-Fi loss.

Operator preparation and reconnection have no deadline. No host RAM reads or
reset while the operator may be in the menu. Collect saved outcomes after
explicit reconnection/menu-exit confirmation and recover without reset/FREE_ALL.
Independent evidence must establish that physical loss overlapped a handshake.
"""
import argparse
import os
import sys
import json
import time
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import report  # noqa: E402
from https_native import evidence_path, run_suite  # noqa: E402

from https_native import HardwareRun
from uci_native import (
    CMDBUF,
    CMDLEN,
    DEFAULT_DRAIN_CAP,
    GO,
    MAX_BLOCKS,
    OPT_ABRT,
    OPT_REPEAT,
    OPT_SFIRST,
    READY,
    SEQ,
)


class OperatorCancelled(Exception):
    pass


def wait_for_operator(marker, cancel_file, required, *, sleep=time.sleep):
    """Wait on local markers only; never contact or reset the device here."""
    while True:
        if cancel_file.exists():
            raise OperatorCancelled('Operator cancelled; test incomplete')
        if marker.exists():
            # Markers must be atomically written JSON, not a bare touch file.
            confirmation = json.loads(marker.read_text())
            if not isinstance(confirmation, dict) or any(confirmation.get(key) is not True for key in required):
                raise ValueError('Operator marker lacks explicit confirmation: '+', '.join(required))
            return confirmation
        sleep(0.25)


def execute(args, *, run_factory=HardwareRun, sleep=time.sleep, monotonic=time.monotonic):
    run = None
    preparation_started = False
    safe_to_cleanup = False
    outcome = {'completed': False, 'native_results_passed': False,
               'recovery_passed': False, 'active_tls_interruption_proven': False,
               'overlap_requires_server_and_network_evidence': True}

    def state(phase):
        with (args.output/'operator-events.jsonl').open('a') as file:
            file.write(json.dumps({'phase': phase, 'time': time.time()})+'\n')

    try:
        state('waiting_for_start_without_deadline')
        report.detail('Waiting for operator readiness outside the menu; no deadline.')
        wait_for_operator(args.start_file, args.cancel_file, ('ready', 'menu_exited'), sleep=sleep)
        state('preparing_native_test')
        preparation_started = True
        run = run_factory(args.host, args.output, password=args.password, timeout=args.timeout)
        machine = run.uci.machine
        safe_to_cleanup = True
        control = run.exchange('HTTPS before interruption',
                               'https://httpbingo.org/base64/QQBCfw==', b'A\0B\x7f')
        if not control['passed']:
            raise RuntimeError('Authenticated baseline failed')
        header = run.ok(bytes([6, 0x11, 1]) + args.stall_url.encode('ascii') + b'\0')
        assert header.data == b'\0'
        machine.writemem(CMDBUF, bytes([6, 0x32, 0, 255]))
        machine.writemem(CMDLEN, bytes([4, 0, 0, MAX_BLOCKS,
                                       DEFAULT_DRAIN_CAP & 255, DEFAULT_DRAIN_CAP >> 8]))
        machine.writemem(OPT_ABRT, b'\0')
        machine.writemem(OPT_SFIRST, b'\1')
        machine.writemem(0xC00C, b'\0\0')
        machine.writemem(OPT_REPEAT, bytes([20]))
        started = time.time()
        outcome['started_at'] = started
        # Once arming starts, do not assume the operator has stayed out of the
        # menu, even if the network write or marker publication fails.
        safe_to_cleanup = False
        machine.writemem(GO, b'\1')
        args.armed_file.write_text(json.dumps({'started': started})+'\n')
        state('waiting_for_reconnection_without_deadline')
        report.detail('ARMED: disconnect/reconnect at your own pace; exit the menu before confirming. '
              'No operator timeout. Native attempts remain bounded; active-request overlap is not guaranteed.')
        wait_for_operator(args.finish_file, args.cancel_file, ('reconnected', 'menu_exited'), sleep=sleep)
        safe_to_cleanup = True
        state('collecting_after_menu_exit')
        until = monotonic() + 90
        while machine.readmem(READY, 1) != b'\xa5':
            if monotonic() >= until:
                raise RuntimeError('C64 agent unavailable; exit the Ultimate menu')
            sleep(0.5)
        machine.writemem(OPT_REPEAT, b'\1')
        while machine.readmem(GO, 1) != b'\0':
            if monotonic() >= until:
                raise RuntimeError('Native repetition did not finish')
            sleep(0.5)
        count, error = machine.readmem(0xC00C, 2)
        if error or not 1 <= count <= 32:
            raise RuntimeError(f'Invalid native history: count={count}, flags={error}')
        raw = machine.readmem(0xC600, 32*count)
        (args.output / 'native-history.bin').write_bytes(raw)
        history = []
        for index in range(count):
            entry = raw[index*32:(index+1)*32]
            flags, low, high, final, length = entry[:5]
            status = entry[5:5+min(length, 27)]
            ok = (flags == 0 and low == 0 and high == 0 and (final & 0x34) == 0
                  and length == len(b'503 SERVICE UNAVAILABLE') and status == b'503 SERVICE UNAVAILABLE')
            history.append({'index': index+1, 'flags': flags, 'bytes': low+256*high,
                                'final': final, 'status': status.decode(errors='replace'), 'passed': ok})
        (args.output / 'native-history.json').write_text(json.dumps(history, indent=2)+'\n')
        if not all(entry['passed'] for entry in history):
            raise RuntimeError('Unexpected native handshake outcome')
        outcome.update(native_results_passed=True, native_handshakes=count)
        run.uci._sequence = machine.readmem(SEQ, 1)[0]
        run.ok(bytes([6, 0x12, 0]))
        for scheme in ('http', 'https'):
            record = run.exchange(scheme+' recovery without reset',
                                  scheme+'://httpbingo.org/base64/QQBCfw==', b'A\0B\x7f')
            if not record['passed']:
                raise RuntimeError(scheme+' recovery failed')
        run.sample_heap('after operator reconnect and recovery')
        outcome.update(recovery_passed=True, collected_at=time.time())
    except (OperatorCancelled, KeyboardInterrupt) as error:
        outcome.update(incomplete=True, error=str(error) or 'Interrupted by operator')
    except Exception as error:  # noqa: BLE001 - retain failures before safe cleanup
        outcome['error'] = str(error)
        (args.output / 'fatal.json').write_text(json.dumps({'error': str(error)})+'\n')
    finally:
        if run is not None and safe_to_cleanup:
            try:
                summary = run.finish()
                outcome['cleanup_completed'] = True
                outcome['completed'] = (not outcome.get('error') and not summary['failed']
                                        and summary['configuration_unchanged'])
            except Exception as error:  # noqa: BLE001 - never discard the original outcome
                outcome['cleanup_error'] = str(error)
        else:
            outcome['cleanup_completed'] = False
            outcome['device_state_uncertain'] = preparation_started
            outcome['cleanup_deferred_until_menu_exit'] = preparation_started
        state('finished' if outcome['completed'] else 'incomplete_or_failed')
        (args.output/'fault-result.json').write_text(json.dumps(outcome, indent=2)+'\n')
    return outcome


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser)
    parser.add_argument('--stall-url', default=os.environ.get('UCI_HTTPS_STALL_URL'))
    parser.add_argument('--output', type=Path, default=evidence_path('https_wifi_loss_test'))
    parser.add_argument('--start-file', type=Path)
    parser.add_argument('--armed-file', type=Path)
    parser.add_argument('--finish-file', type=Path)
    parser.add_argument('--cancel-file', type=Path)
    args = parser.parse_args()
    report.apply_colour(args.color)
    if not args.stall_url or not args.stall_url.startswith('https://'):
        parser.error('Provide --stall-url or UCI_HTTPS_STALL_URL for the controlled HTTPS listener')
    for name in ('start', 'armed', 'finish', 'cancel'):
        if getattr(args, name+'_file') is None:
            setattr(args, name+'_file', args.output.with_name(args.output.name+'.'+name+'.json'))
        report.detail(f'{name} marker: {getattr(args, name+"_file")}')
    paths = [args.output, args.start_file, args.armed_file, args.finish_file, args.cancel_file]
    if len({path.resolve() for path in paths}) != len(paths) or any(path.exists() for path in paths):
        parser.error('Use distinct, new output and marker paths')
    args.output.mkdir(parents=True)
    (args.output/'runner.py').write_bytes(Path(__file__).read_bytes())
    outcome = execute(args)
    report.detail(json.dumps(outcome))
    if not outcome['completed']:
        raise SystemExit(2 if outcome.get('incomplete') else 1)


if __name__ == '__main__':
    run_suite("https-wifi-loss", main)
