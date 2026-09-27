import json
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import Mock, patch

from https_native import HardwareRun


class FailureEvidenceTests(unittest.TestCase):
    def test_diagnostic_failure_preserves_exchange_without_retry(self):
        with TemporaryDirectory() as folder:
            run = HardwareRun.__new__(HardwareRun)
            run.output = Path(folder)
            run.heap_available = True
            run.records = []
            run.ok = Mock(return_value=SimpleNamespace(data=b'\0'))
            run.uci = SimpleNamespace(transact=Mock(return_value=SimpleNamespace(
                data=b'', status_text=b'503 SERVICE UNAVAILABLE', elapsed=2.1,
                blocks=[SimpleNamespace(data=b'')])))
            run.sample_heap = Mock(side_effect=TimeoutError('read-only diagnostic unavailable'))
            with patch('https_native.time.sleep') as sleep:
                record = run.exchange('unexpected failure', 'https://fixture.test/', b'expected')
            self.assertFalse(record['passed'])
            run.uci.transact.assert_called_once()
            sleep.assert_called_once_with(1.2)
            saved = json.loads((run.output/'exchanges.jsonl').read_text())
            self.assertEqual(saved, record)
            diagnostic = json.loads((run.output/'diagnostic-errors.jsonl').read_text())
            self.assertEqual(diagnostic['exchange'], record['index'])
            self.assertIn('diagnostic unavailable', diagnostic['error'])


if __name__ == '__main__':
    unittest.main()
