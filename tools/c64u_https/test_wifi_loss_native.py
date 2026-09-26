import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from wifi_loss_native import GO, READY, SEQ, execute, wait_for_operator


class ManualWifiTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.args = SimpleNamespace(host='device', stall_url='https://fixture:8766/',
                                    output=root/'evidence', start_file=root/'start',
                                    armed_file=root/'armed', finish_file=root/'finished',
                                    cancel_file=root/'cancel')
        self.args.output.mkdir()
        self.calls = []
        self.elapsed = 0
        self.recovery_ok = True

    def marker(self, path, **fields):
        path.write_text(json.dumps(fields))

    def factory(self, host, output):
        self.calls.append('start')
        self.assertTrue(self.args.start_file.exists())
        self.assertFalse(self.args.finish_file.exists())
        outer = self

        class Machine:
            def writemem(self, address, data):
                outer.calls.append(('write', address))

            def readmem(self, address, length):
                outer.assertTrue(outer.args.finish_file.exists())
                outer.calls.append(('read', address))
                if address == READY:
                    return b'\xa5'
                if address == GO:
                    return b'\0'
                if address == SEQ:
                    return b'\2'
                if address == 0xC00C:
                    return b'\1\0'
                if address == 0xC600:
                    return bytes([0, 0, 0, 0, 23])+b'503 SERVICE UNAVAILABLE'+bytes(4)
                raise AssertionError('Unexpected read')

        class Run:
            def __init__(self):
                self.uci = SimpleNamespace(machine=Machine())

            def ok(self, command):
                return SimpleNamespace(data=b'\0')

            def exchange(self, label, *args):
                outer.calls.append(label)
                return {'passed': outer.recovery_ok or label == 'HTTPS before interruption'}

            def sample_heap(self, label):
                outer.calls.append('heap')

            def finish(self):
                outer.calls.append('finish')
                return {'failed': 0, 'configuration_unchanged': True}

        return Run()

    def test_long_preparation_and_reconnection_do_not_expire(self):
        def delay(_seconds):
            self.elapsed += 3600
            if not self.args.start_file.exists():
                self.assertEqual(self.calls, [])
                self.marker(self.args.start_file, ready=True, menu_exited=True)
            else:
                self.assertTrue(self.args.armed_file.exists())
                self.assertFalse(any(isinstance(call, tuple) and call[0] == 'read' for call in self.calls))
                self.assertNotIn('finish', self.calls)
                self.marker(self.args.finish_file, reconnected=True, menu_exited=True)

        result = execute(self.args, run_factory=self.factory, sleep=delay, monotonic=lambda: self.elapsed)
        self.assertEqual(self.elapsed, 7200)
        self.assertTrue(result['completed'])
        self.assertTrue(result['recovery_passed'])
        self.assertFalse(result['active_tls_interruption_proven'])
        self.assertTrue(result['overlap_requires_server_and_network_evidence'])
        self.assertLess(self.calls.index('https recovery without reset'), self.calls.index('finish'))

    def test_cancel_before_start_never_contacts_device(self):
        self.args.cancel_file.touch()
        result = execute(self.args, run_factory=self.factory)
        self.assertTrue(result['incomplete'])
        self.assertFalse(result['completed'])
        self.assertFalse(result['device_state_uncertain'])
        self.assertEqual(self.calls, [])

    def test_cancel_after_arming_never_reads_ram_or_resets_in_menu(self):
        self.marker(self.args.start_file, ready=True, menu_exited=True)
        result = execute(self.args, run_factory=self.factory,
                         sleep=lambda _: self.args.cancel_file.touch())
        self.assertTrue(result['incomplete'])
        self.assertTrue(result['cleanup_deferred_until_menu_exit'])
        self.assertFalse(result['cleanup_completed'])
        self.assertNotIn('finish', self.calls)
        self.assertFalse(any(isinstance(call, tuple) and call[0] == 'read' for call in self.calls))

    def test_reconnect_without_menu_exit_is_not_permission_to_read_ram(self):
        self.marker(self.args.start_file, ready=True, menu_exited=True)
        result = execute(self.args, run_factory=self.factory,
                         sleep=lambda _: self.marker(self.args.finish_file, reconnected=True))
        self.assertFalse(result['completed'])
        self.assertTrue(result['cleanup_deferred_until_menu_exit'])
        self.assertNotIn('finish', self.calls)
        self.assertFalse(any(isinstance(call, tuple) and call[0] == 'read' for call in self.calls))

    def test_failed_recovery_is_not_reported_as_success(self):
        self.recovery_ok = False
        self.marker(self.args.start_file, ready=True, menu_exited=True)
        result = execute(self.args, run_factory=self.factory,
                         sleep=lambda _: self.marker(self.args.finish_file, reconnected=True, menu_exited=True))
        self.assertFalse(result['completed'])
        self.assertFalse(result['recovery_passed'])
        self.assertIn('http recovery failed', result['error'])
        self.assertNotIn('https recovery without reset', self.calls)
        self.assertIn('finish', self.calls)

    def test_truthy_values_do_not_replace_explicit_confirmation(self):
        for value in ('yes', 1, False):
            self.marker(self.args.start_file, ready=value, menu_exited=True)
            with self.assertRaises(ValueError):
                wait_for_operator(self.args.start_file, self.args.cancel_file, ('ready', 'menu_exited'))

    def test_partial_setup_failure_does_not_claim_device_was_untouched(self):
        self.marker(self.args.start_file, ready=True, menu_exited=True)

        def failed_setup(*args):
            raise RuntimeError('Connection lost while starting native program')

        result = execute(self.args, run_factory=failed_setup)
        self.assertFalse(result['completed'])
        self.assertTrue(result['device_state_uncertain'])
        self.assertFalse(result['cleanup_completed'])


if __name__ == '__main__':
    unittest.main()
