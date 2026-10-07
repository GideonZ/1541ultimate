import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from https_native import HardwareRun
from https_smoke_test import main as smoke_main


class CleanupTests(unittest.TestCase):
    def test_failed_final_diagnostic_still_stops_program(self):
        run = HardwareRun.__new__(HardwareRun)
        run.sample_heap = Mock(side_effect=TimeoutError('telemetry lost'))
        run.stop_program = Mock()
        with self.assertRaisesRegex(TimeoutError, 'telemetry lost'):
            run.finish()
        run.stop_program.assert_called_once()

    def test_failed_configuration_read_still_stops_program(self):
        run = HardwareRun.__new__(HardwareRun)
        run.sample_heap = Mock()
        run.rest = SimpleNamespace(expect=Mock(side_effect=TimeoutError('configuration lost')))
        run.stop_program = Mock()
        with self.assertRaisesRegex(TimeoutError, 'configuration lost'):
            run.finish()
        run.stop_program.assert_called_once()

    def test_failed_reset_is_not_successful_cleanup(self):
        run = HardwareRun.__new__(HardwareRun)
        run.machine = SimpleNamespace(reset=Mock(side_effect=RuntimeError('cleanup failed')))
        with self.assertRaisesRegex(RuntimeError, 'cleanup failed'):
            run.stop_program()

    def test_failed_native_start_attempts_cleanup_and_preserves_primary_error(self):
        with tempfile.TemporaryDirectory() as folder:
            rest = Mock()
            rest.expect.return_value = b'{"wifi_password":"private"}'
            rest.request.return_value = (404, {}, b'no telemetry')
            native = Mock()
            native.start.side_effect = TimeoutError('agent did not start')
            with patch('https_native.RestClient', return_value=rest) as factory, \
                    patch('https_native.MachineApi') as machine, \
                    patch('https_native.NativeUci', return_value=native):
                with self.assertRaisesRegex(TimeoutError, 'agent did not start'):
                    HardwareRun('device', Path(folder), password='secret', timeout=7)
            factory.assert_called_once_with('device', password='secret', timeout=7)
            machine.return_value.reset.assert_called_once_with(force=True, wait=False)
            saved = (Path(folder)/'configs-before.json').read_text()
            self.assertNotIn('private', saved)
            self.assertEqual(len(json.loads(saved)['sha256']), 64)

    def test_split_target_is_rejected_before_loading_wrong_machine(self):
        with patch('https_native.RestClient') as transport:
            with self.assertRaisesRegex(ValueError, 'single device'):
                HardwareRun('cartridge@computer', Path('unused'))
            transport.assert_not_called()

    def test_no_url_fails_before_contacting_device(self):
        with patch('sys.argv', ['https_smoke_test.py']), \
                patch('https_smoke_test.RestClient') as transport, \
                contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                smoke_main()
            self.assertEqual(error.exception.code, 2)
            transport.assert_not_called()

    def test_smoke_preflight_failure_does_not_reset_running_program(self):
        with tempfile.TemporaryDirectory() as folder, \
                patch('sys.argv', ['https_smoke_test.py', '--url', 'https://fixture.test/',
                                   '--output', str(Path(folder)/'result.json')]), \
                patch('https_smoke_test.RestClient') as transport, \
                patch('https_smoke_test.MachineApi') as machine, \
                patch('https_smoke_test.NativeUci') as native:
            transport.return_value.expect.side_effect = TimeoutError('preflight unavailable')
            with self.assertRaisesRegex(TimeoutError, 'preflight unavailable'):
                smoke_main()
            machine.return_value.reset.assert_not_called()
            native.return_value.start.assert_not_called()
