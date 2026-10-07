#!/usr/bin/env python3
"""Bounded native-UCI diagnostic checks for bridge 1.18, never a soak retry.

Replaces the C64 program/RAM and resets that program on completion. No firmware
installation or trust/configuration change. Start an optional LAN silent fixture
separately. Every unexpected result terminates the run and preserves evidence.
"""
import argparse
import sys
import json
import time
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import report  # noqa: E402
from https_native import evidence_path, run_suite  # noqa: E402

from https_native import checked_metrics
from https_native import HardwareRun, RestClient


def verify_failure(before, after, *, stages, statuses, certificate=False):
    previous = checked_metrics(before)
    current = checked_metrics(after, previous['boot_id'])
    fault = current['last_tls_failure']
    local = after['values']['https_bridge']['last_failure']
    old_local = before['values']['https_bridge']['last_failure']
    if fault['count'] <= previous['last_tls_failure']['count'] or local['count'] <= old_local['count']:
        raise ValueError('Failure telemetry did not advance')
    if fault['epoch'] != local['epoch'] or fault['session'] != local['session']:
        raise ValueError('Management/TLS failure identities do not match')
    if fault['stage'] not in stages or fault['status'] not in statuses:
        raise ValueError('Unexpected TLS failure stage/status')
    if certificate and (fault['code'] >= 0 or int(fault['verify_flags'], 16) in (0, 0xffffffff)):
        raise ValueError('Missing original certificate failure evidence')
    return {'management': local, 'tls': fault, 'boot_id': current['boot_id']}


def verify_silent_deadline(evidence, exchange, server_records):
    """Corroborate a deadline, never accept a generic transport error alone."""
    fault, local = evidence['tls'], evidence['management']
    if fault['stage'] != 8 or fault['status'] not in (3, 4) or not 14 <= exchange['seconds'] <= 19:
        raise ValueError('Silent fixture did not reach the bounded handshake deadline')
    if fault['status'] == 4 and (local['stage'], local['code'], local['detail']) != ('tcp_timeout', 0, 0):
        raise ValueError('Transport error lacks correlated TCP read-timeout evidence')
    started = datetime.fromisoformat(exchange['started_at']).timestamp()
    finished = datetime.fromisoformat(exchange['finished_at']).timestamp()
    hellos = [r for r in server_records if r.get('fixture') == 'silent' and r.get('tls_record') is True
              and r.get('received_bytes', 0) > 0 and started <= r['time'] <= finished]
    if len(hellos) != 1:
        raise ValueError('Missing or ambiguous server ClientHello evidence for this exchange')
    return hellos[0]


def verify_handshake_close(evidence, exchange, server_records, mode):
    """Require a completed injection inside this exchange, not just a 503."""
    fault = evidence['tls']
    local = evidence['management']
    if fault['stage'] != 8 or fault['verify_flags'] != 'ffffffff':
        raise ValueError('Missing pre-authentication handshake failure')
    if mode == 'close':
        # A zero-byte TCP read becomes MBEDTLS_ERR_SSL_CONN_EOF (-0x7280).
        # HTTPS_TLS_ERROR is broader than certificate rejection.
        if ((fault['status'], fault['code']) != (5, -0x7280)
                or (local['stage'], local['code'], local['detail']) != ('tls_reply', -5, 1)):
            raise ValueError('Missing correlated handshake EOF evidence')
    elif mode == 'reset':
        if ((fault['status'], fault['code']) != (4, -0x6c00)
                or local['stage'] != 'tcp_read' or local['code'] <= 0 or local['detail'] != 0):
            raise ValueError('Missing correlated socket read-error evidence')
    else:
        raise ValueError('Unknown handshake close mode')
    if not 1.8 <= exchange['seconds'] <= 8:
        raise ValueError('Connection close was not observed before the handshake deadline')
    started = datetime.fromisoformat(exchange['started_at']).timestamp()
    finished = datetime.fromisoformat(exchange['finished_at']).timestamp()
    events = [r for r in server_records if r.get('fixture') == mode
              and started <= r.get('time', 0) <= finished]
    if len(events) != 1:
        raise ValueError('Missing or ambiguous connection-close evidence')
    event = events[0]
    if (event.get('action') != 'socket_closed' or event.get('tls_record') is not True
            or event.get('received_bytes', 0) < 9
            or not started <= event.get('hello_at', 0) <= event['time']
            or not 1.8 <= event['time']-event['hello_at'] <= 4):
        raise ValueError('Missing completed ClientHello and timed close evidence')
    return event


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser)
    parser.add_argument('--output', type=Path, default=evidence_path('https_diagnostic_test'))
    parser.add_argument('--stall-url', help='HTTPS URL of the LAN silent TCP fixture')
    parser.add_argument('--stall-log', type=Path, help='JSONL log from that controlled silent fixture')
    parser.add_argument('--close-url', help='LAN HTTPS listener closing after ClientHello')
    parser.add_argument('--reset-url', help='LAN HTTPS listener resetting after ClientHello')
    parser.add_argument('--close-log', type=Path, help='JSONL evidence for close/reset listeners')
    args = parser.parse_args()
    report.apply_colour(args.color)
    if args.output.exists():
        parser.error('Use a new output directory; previous evidence is never overwritten')
    if args.stall_url and not args.stall_url.startswith('https://'):
        parser.error('stall-url must use HTTPS')
    if bool(args.stall_url) != bool(args.stall_log):
        parser.error('Provide stall-url and stall-log together to correlate server evidence')
    if bool(args.close_url or args.reset_url) != bool(args.close_log):
        parser.error('Provide close-log with close-url and/or reset-url')
    if any(url and not url.startswith('https://') for url in (args.close_url, args.reset_url)):
        parser.error('Close/reset URLs must use HTTPS')
    # Read-only preflight before replacing any C64 RAM.
    rest = RestClient(args.host, password=args.password, timeout=args.timeout)
    heap = json.loads(rest.expect('GET', '/v1/machine:heap'))
    checked_metrics({'values': heap})
    if 'last_tls_failure' not in heap['esp32'] or 'last_failure' not in heap['https_bridge']:
        parser.error('Install the diagnostic bridge 1.18 candidate first')
    run = HardwareRun(args.host, args.output, password=args.password, timeout=args.timeout)
    (args.output/'diagnostic-runner.py').write_bytes(Path(__file__).read_bytes())
    checks = []
    outcome = {'passed': False, 'checks': checks}

    def exchange(*positional, **keywords):
        record = run.exchange(*positional, **keywords)
        if not record['passed']:
            raise RuntimeError('Unexpected exchange; see exchanges.jsonl')
        return record

    try:
        for scheme in ('http', 'https'):
            exchange(scheme+' baseline', scheme+'://httpbingo.org/range/894',
                     bytes(97+i%26 for i in range(894)))
        cases = [('DNS failure', 'https://ultimate-https-test.invalid/', {7}, {3, 4}, False),
                 ('certificate rejection', 'https://wrong.host.badssl.com/', {8}, {5}, True)]
        if args.stall_url:
            cases.append(('silent handshake', args.stall_url, {8}, {3, 4}, False))
        for mode, url in (('close', args.close_url), ('reset', args.reset_url)):
            if url:
                cases.append(('handshake '+mode, url, {8}, {5} if mode == 'close' else {4}, False))
        for name, url, stages, statuses, certificate in cases:
            run.sample_heap('before '+name)
            before = run.heap_samples[-1]
            record = exchange(name, url, b'', unavailable=True)
            time.sleep(1.2)  # Telemetry is asynchronous, at most one packet/second.
            run.sample_heap('after '+name)
            after = run.heap_samples[-1]
            evidence = verify_failure(before, after, stages=stages, statuses=statuses, certificate=certificate)
            if name == 'DNS failure' and not evidence['management']['stage'].startswith('dns_'):
                raise ValueError('Missing management DNS failure evidence')
            if name == 'silent handshake':
                server_records = [json.loads(line) for line in args.stall_log.read_text().splitlines()]
                evidence['server_hello'] = verify_silent_deadline(evidence, record, server_records)
            if name in ('handshake close', 'handshake reset'):
                server_records = [json.loads(line) for line in args.close_log.read_text().splitlines()]
                evidence['server_close'] = verify_handshake_close(
                    evidence, record, server_records, name.split()[1])
            checks.append(dict(name=name, **evidence))
            exchange('HTTPS recovery after '+name, 'https://httpbingo.org/range/894',
                     bytes(97+i%26 for i in range(894)))
            time.sleep(1.2)
            run.sample_heap('after recovery from '+name)
            recovered = run.heap_samples[-1]
            checked_metrics(recovered, checks[-1]['boot_id'])
            if (recovered['values']['esp32']['last_tls_failure'] != after['values']['esp32']['last_tls_failure']
                    or recovered['values']['https_bridge']['last_failure'] != after['values']['https_bridge']['last_failure']):
                raise ValueError('Successful recovery changed the latched failure record')
        outcome['passed'] = True
    except Exception as error:  # noqa: BLE001 - preserve evidence before cleanup
        outcome['error'] = str(error)
        (args.output/'fatal.json').write_text(json.dumps({'error': str(error)})+'\n')
    finally:
        try:
            run.finish()
        except Exception as error:  # noqa: BLE001 - cleanup must not hide the primary error
            outcome['passed'] = False
            outcome['cleanup_error'] = str(error)
        (args.output/'diagnostic-result.json').write_text(json.dumps(outcome, indent=2)+'\n')
    report.detail(json.dumps(outcome))
    if not outcome['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    run_suite("https-diagnostic", main)
