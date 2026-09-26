import copy
import unittest
from datetime import datetime

from diagnostic_smoke import (
    verify_failure,
    verify_handshake_close,
    verify_silent_deadline,
)
from fault_server import receive_client_hello
from test_long_soak import sample


class DiagnosticEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.before = sample()
        self.before['values']['esp32']['last_tls_failure'] = {'count': 0}
        self.before['values']['https_bridge'] = {'last_failure': {'count': 0}}
        self.after = copy.deepcopy(self.before)
        self.after['values']['esp32']['last_tls_failure'] = {
            'count': 1, 'epoch': '0000000100000002', 'session': '00000042',
            'stage': 8, 'status': 5, 'code': -9984, 'verify_flags': '00000004'}
        self.after['values']['https_bridge']['last_failure'] = {
            'count': 1, 'epoch': '0000000100000002', 'session': '00000042',
            'stage': 'tls_reply', 'code': -5, 'detail': 1}

    def verify(self):
        return verify_failure(self.before, self.after, stages={8}, statuses={5}, certificate=True)

    def test_correlated_certificate_failure(self):
        self.assertEqual(self.verify()['tls']['code'], -9984)

    def test_reject_uncorrelated_or_old_evidence(self):
        for key, value in [('count', 0), ('epoch', '0000000100000001'), ('session', '00000043'),
                           ('stage', 7), ('status', 4), ('code', 0), ('verify_flags', '00000000'),
                           ('verify_flags', 'ffffffff')]:
            with self.subTest(key=key):
                self.setUp()
                self.after['values']['esp32']['last_tls_failure'][key] = value
                with self.assertRaises(ValueError):
                    self.verify()

    def test_silent_deadline_requires_timing_and_correlated_socket_and_server(self):
        evidence = {'tls': {'stage': 8, 'status': 4},
                    'management': {'stage': 'tcp_timeout', 'code': 0, 'detail': 0}}
        exchange = {'seconds': 15.3, 'started_at': '2026-09-23T00:00:00+00:00',
                    'finished_at': '2026-09-23T00:00:16+00:00'}
        stamp = datetime.fromisoformat(exchange['started_at']).timestamp()
        hello = {'time': stamp+1, 'fixture': 'silent', 'tls_record': True, 'received_bytes': 246}
        self.assertEqual(verify_silent_deadline(evidence, exchange, [hello]), hello)
        for events in ([], [hello, hello], [dict(hello, time=stamp-1)], [dict(hello, tls_record=False)]):
            with self.assertRaises(ValueError):
                verify_silent_deadline(evidence, exchange, events)
        for change in ({'seconds': 2.4}, {'seconds': 20}):
            with self.assertRaises(ValueError):
                verify_silent_deadline(evidence, dict(exchange, **change), [hello])
        for change in ({'stage': 'tcp_read'}, {'code': 104}, {'detail': 1}):
            invalid = dict(evidence, management=dict(evidence['management'], **change))
            with self.assertRaises(ValueError):
                verify_silent_deadline(invalid, exchange, [hello])

    def test_restart_and_stale_metrics_are_not_passes(self):
        for key, value in [('boot_id', '000000ff'), ('sample_age_ms', 30001), ('available', False)]:
            with self.subTest(key=key):
                self.setUp()
                self.after['values']['esp32'][key] = value
                with self.assertRaises(ValueError):
                    self.verify()

    def test_close_requires_completed_injection_in_the_exchange_window(self):
        evidence = {'tls': {'stage': 8, 'status': 4, 'code': -27648, 'verify_flags': 'ffffffff'},
                    'management': {'stage': 'tcp_read', 'code': 104, 'detail': 0}}
        exchange = {'seconds': 2.4, 'started_at': '2026-09-26T00:00:00+00:00',
                    'finished_at': '2026-09-26T00:00:03+00:00'}
        stamp = datetime.fromisoformat(exchange['started_at']).timestamp()
        event = {'fixture': 'reset', 'time': stamp+2.2, 'hello_at': stamp+0.2,
                 'action': 'socket_closed', 'tls_record': True, 'received_bytes': 246}
        self.assertEqual(verify_handshake_close(evidence, exchange, [event], 'reset'), event)
        for events in ([], [event, event], [dict(event, fixture='close')],
                       [dict(event, action='fixture_error')], [dict(event, time=stamp+4)],
                       [dict(event, hello_at=stamp-1)], [dict(event, hello_at=stamp+2)],
                       [dict(event, tls_record=False)], [dict(event, received_bytes=0)]):
            with self.subTest(events=events), self.assertRaises(ValueError):
                verify_handshake_close(evidence, exchange, events, 'reset')
        for change in ({'stage': 9}, {'status': 5}, {'code': 0}):
            with self.assertRaises(ValueError):
                verify_handshake_close(dict(evidence, tls=dict(evidence['tls'], **change)), exchange, [event], 'reset')
        for change in ({'stage': 'tcp_timeout'}, {'code': 0}, {'detail': 1}):
            with self.assertRaises(ValueError):
                verify_handshake_close(dict(evidence, management=dict(evidence['management'], **change)), exchange, [event], 'reset')
        with self.assertRaises(ValueError):
            verify_handshake_close(evidence, dict(exchange, seconds=15), [event], 'reset')
        eof = {'tls': dict(evidence['tls'], status=5, code=-0x7280),
               'management': {'stage': 'tls_reply', 'code': -5, 'detail': 1}}
        closed = dict(event, fixture='close')
        self.assertEqual(verify_handshake_close(eof, exchange, [closed], 'close'), closed)
        for change in ({'code': -9984}, {'verify_flags': '00000004'}, {'status': 4}):
            with self.assertRaises(ValueError):
                verify_handshake_close(dict(eof, tls=dict(eof['tls'], **change)), exchange, [closed], 'close')

    def test_injection_waits_for_complete_client_hello(self):
        class Fragmented:
            def __init__(self, data):
                self.data = data

            def recv(self, count):
                chunk, self.data = self.data[:min(count, 2)], self.data[min(count, 2):]
                return chunk

        wire = bytes.fromhex('1603030006010000020303')
        self.assertEqual(receive_client_hello(Fragmented(wire[1:]), wire[:1]), wire)
        for broken in (wire[:4], wire[:-1], b'\x15'+wire[1:], wire[:5]+b'\x02'+wire[6:],
                       wire[:3]+b'\xff\xff'+wire[5:], wire[:8]+b'\x03'+wire[9:]):
            with self.subTest(wire=broken), self.assertRaises(ValueError):
                receive_client_hello(Fragmented(broken), b'')


if __name__ == '__main__':
    unittest.main()
