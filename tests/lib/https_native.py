"""Native target-6 transport and evidence shared by E2E and soak suites."""
import hashlib
import re
import json
import os
import statistics
import time
from datetime import datetime, timezone
from pathlib import Path

import bootstrap  # noqa: F401
import report
from api import MachineApi, RunnersApi
from rest import RestClient
from uci_native import NativeUci


def utc_now():
    # Upstream's Ubuntu 22.04 build container supplies Python 3.10.
    return datetime.now(timezone.utc).isoformat()  # noqa: UP017


class HardwareRun:
    def __init__(self, host, output, *, password=None, timeout=10):
        if '@' in host:
            raise ValueError('Native UCI requires a single device; cartridge@computer would test the host interface')
        self.output = output
        output.mkdir(parents=True, exist_ok=True)
        self.rest = RestClient(host, password=password, timeout=timeout)
        self.machine = MachineApi(self.rest)
        self.uci = NativeUci(self.machine, RunnersApi(self.rest),
                             busy_timeout=40, wait_wraps=32, first_status_only=True)
        self.records = []
        self.heap_samples = []
        self.started = time.monotonic()
        self.started_at = utc_now()
        self.original_configs = self.rest.expect('GET', '/v1/configs/*')
        self.save_config_digest('configs-before.json', self.original_configs)
        # Available in newer upstream firmware, optional on the retail baseline.
        code, _, heap = self.rest.request('GET', '/v1/machine:heap')
        self.heap_available = code == 200
        (output / 'heap-before.json').write_text(json.dumps(
            {'http_status': code, 'body': heap.decode(errors='replace')}))
        try:
            self.uci.start()
            self.ok(bytes([6, 0x10]))
            ident = self.uci.transact(bytes([6, 1]))
            assert ident.data.rstrip(b'\0') == b'ULTIMATE HTTP TARGET V1.0'
            self.sample_heap('agent ready')
        except BaseException:
            # Preparation may already have replaced C64 RAM. Preserve the primary
            # error while making one bounded attempt to stop the program.
            try:
                self.stop_program()
            except Exception as error:
                (output / 'cleanup-error.txt').write_text(str(error))
            raise

    def save_config_digest(self, name, raw):
        canonical = json.dumps(json.loads(raw), sort_keys=True).encode()
        (self.output / name).write_text(json.dumps({'sha256': hashlib.sha256(canonical).hexdigest()})+'\n')

    def stop_program(self):
        self.machine.reset(force=True, wait=False)

    def sample_heap(self, label):
        if not self.heap_available:
            return
        code, _, data = self.rest.request('GET', '/v1/machine:heap')
        if code != 200:
            raise RuntimeError(f'Heap diagnostic stopped responding: HTTP {code}')
        sample = {'label': label, 'exchange': len(self.records), 'values': json.loads(data),
                      'observed_at': utc_now()}
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
        started_at = utc_now()
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
            'started_at': started_at, 'finished_at': utc_now(),
            'errors': errors, 'seconds': round(result.elapsed, 3), 'status': result.status_text.decode(errors='replace'),
            'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
            'blocks': [len(b.data) for b in result.blocks], 'request_handle': handle}
        self.records.append(record)
        with (self.output / 'exchanges.jsonl').open('a') as file:
            file.write(json.dumps(record)+'\n')
        report.check_start(label)
        (report.check_fail if errors else report.check_ok)("; ".join(errors))
        report.detail(f"{label}: {record['seconds']:.2f}s; status={record['status']}; errors={errors}")
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
        try:
            self.sample_heap('before final C64 reset')
            after = self.rest.expect('GET', '/v1/configs/*')
            self.save_config_digest('configs-after.json', after)
            unchanged = json.loads(after) == json.loads(self.original_configs)
            heap_after = None
            if self.heap_available:
                code, _, body = self.rest.request('GET', '/v1/machine:heap')
                heap_after = {'http_status': code, 'body': body.decode(errors='replace')}
        finally:
            self.stop_program()
        info = json.loads(self.rest.expect('GET', '/v1/info'))
        failure_file = self.output / 'fatal.json'
        fatal_error = json.loads(failure_file.read_text()).get('error') if failure_file.exists() else None
        summary = {'exchanges': len(self.records), 'failed': sum(not r['passed'] for r in self.records),
            'started_at': self.started_at, 'finished_at': utc_now(),
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
        report.detail(f"Evidence: {self.output}; {summary['exchanges']} exchanges")
        assert unchanged, 'Configuration changed during test'
        return summary



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



def evidence_path(name):
    """Separate evidence directory for each invocation, beside runner JSONL."""
    import tempfile
    import uuid
    destination = os.environ.get('E2E_JSONL')
    base = Path(destination).parent if destination else Path(tempfile.gettempdir())
    return base / f"{name}-{uuid.uuid4().hex[:12]}"


def run_suite(name, main):
    """Use the same console/JSONL verdicts for standalone and registered runs."""
    try:
        main()
    except SystemExit as error:
        if error.code in (None, 0):
            return
        report.suite_fail(name, f'Exited with status {error.code}; see evidence')
        raise
    except (Exception, KeyboardInterrupt) as error:
        report.suite_fail(name, str(error) or 'Interrupted')
        raise SystemExit(1) from error
    else:
        report.suite_ok(name)
