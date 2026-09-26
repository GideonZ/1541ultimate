import copy
import unittest

from response_diagnostic import verify_response_failure
from test_long_soak import sample


class ResponseEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.before = sample()
        self.before['values']['esp32']['last_tls_failure'] = {'count': 15, 'epoch': '0000000100000001'}
        self.before['values']['https_bridge'] = {'last_failure': {'count': 15, 'epoch': '0000000100000001'}}
        self.after = copy.deepcopy(self.before)
        self.after['values']['https_bridge']['last_failure'] = {
            'count': 16, 'epoch': '0000000100000002', 'session': '00000042',
            'stage': 'http_read', 'code': 0, 'detail': 3}

    def tls_failure(self, mode):
        self.after['values']['esp32']['last_tls_failure'] = {
            'count': 16, 'epoch': '0000000100000002', 'session': '00000042',
            'stage': 9, 'status': 8 if mode == 'short' else 4,
            'code': 0 if mode == 'short' else -27648, 'verify_flags': '00000000'}
        local = self.after['values']['https_bridge']['last_failure']
        local.update(stage='tls_reply' if mode == 'short' else 'tcp_timeout',
                     code=-8 if mode == 'short' else 0, detail=2 if mode == 'short' else 0)

    def test_first_failure_after_boot_has_no_previous_epoch(self):
        for mode, seconds in (('short', 2), ('slow', 15), ('clean', 2)):
            self.setUp()
            if mode != 'clean':
                self.tls_failure(mode)
            self.before['values']['https_bridge']['last_failure'] = {'count': 0}
            self.before['values']['esp32']['last_tls_failure'] = {'count': 0}
            self.after['values']['https_bridge']['last_failure']['count'] = 1
            if mode == 'clean':
                self.after['values']['esp32']['last_tls_failure'] = {'count': 0}
            else:
                self.after['values']['esp32']['last_tls_failure']['count'] = 1
            fault_mode = 'short' if mode == 'clean' else mode
            verify_response_failure(self.before, self.after, fault_mode, seconds)
            for change in ({'count': 0}, {'count': 2}, {'epoch': '0000000000000000'}):
                after = copy.deepcopy(self.after)
                after['values']['https_bridge']['last_failure'].update(change)
                with self.assertRaises(ValueError):
                    verify_response_failure(self.before, after, fault_mode, seconds)

    def test_clean_tls_close_requires_incomplete_http_body(self):
        self.assertIn('clean TLS', verify_response_failure(self.before, self.after, 'short', 2)['category'])
        for change in ({'count': 15}, {'epoch': '0000000100000001'}, {'detail': 0}, {'code': -1}, {'stage': 'http_framing'}):
            after = copy.deepcopy(self.after)
            after['values']['https_bridge']['last_failure'].update(change)
            with self.assertRaises(ValueError):
                verify_response_failure(self.before, after, 'short', 2)

    def test_tls_failures_require_read_stage_verified_peer_and_identity(self):
        for mode, seconds in (('short', 2), ('slow', 15)):
            self.setUp()
            self.tls_failure(mode)
            verify_response_failure(self.before, self.after, mode, seconds)
            for change in ({'stage': 8}, {'verify_flags': 'ffffffff'}, {'count': 15},
                           {'session': '00000043'}, {'epoch': '0000000100000001'}, {'status': 5}):
                after = copy.deepcopy(self.after)
                after['values']['esp32']['last_tls_failure'].update(change)
                with self.assertRaises(ValueError):
                    verify_response_failure(self.before, after, mode, seconds)

    def test_missing_freshness_restart_and_wrong_timing_rejected(self):
        for field, value in (('available', False), ('sample_age_ms', 30001), ('boot_id', '000000ff')):
            after = copy.deepcopy(self.after)
            after['values']['esp32'][field] = value
            with self.assertRaises(ValueError):
                verify_response_failure(self.before, after, 'short', 2)
        with self.assertRaises(ValueError):
            verify_response_failure(self.before, self.after, 'short', 15)
        self.tls_failure('slow')
        with self.assertRaises(ValueError):
            verify_response_failure(self.before, self.after, 'slow', 2)

    def test_session_and_queue_deadlines_require_primary_evidence(self):
        self.tls_failure('slow')
        for stage, detail in (('tcp_deadline', 0), ('tcp_session_deadline', 0), ('tcp_queue_deadline', 2)):
            self.after['values']['https_bridge']['last_failure'].update(stage=stage, code=0, detail=detail)
            verify_response_failure(self.before, self.after, 'slow', 15)
        for change in ({'stage': 'tls_reply', 'code': -4, 'detail': 2},
                       {'stage': 'tcp_queue_deadline', 'code': 0, 'detail': 3},
                       {'stage': 'tcp_session_deadline', 'code': 104, 'detail': 0}):
            self.after['values']['https_bridge']['last_failure'].update(change)
            with self.assertRaises(ValueError):
                verify_response_failure(self.before, self.after, 'slow', 15)


if __name__ == '__main__':
    unittest.main()
