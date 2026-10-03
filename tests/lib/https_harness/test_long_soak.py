import copy
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from https_timed_test import checked_metrics, exercise


def sample():
    return {'values': {'esp32': {
        'available': True, 'internal_free': 40000, 'internal_min_ever_free': 20000,
        'internal_largest_free_block': 30000, 'free_8bit': 100000, 'min_ever_free_8bit': 80000,
        'largest_free_block_8bit': 60000, 'tls_stack_min_free_bytes': 4096,
        'uptime_seconds': 40, 'sample_age_ms': 1000, 'boot_id': '1234abcd',
        'sample_sequence': '00000012'}}}


class FakeRun:
    def __init__(self, passed=True, restart=False):
        self.heap_samples = [sample()]
        self.passed, self.restart, self.exchanges, self.now = passed, restart, 0, 0

    def exchange(self, *args, **kwargs):
        self.exchanges += 1
        self.now += 1
        return {'passed': self.passed}

    def sample_heap(self, label):
        value = sample()
        if self.restart:
            value['values']['esp32']['boot_id'] = '00000001'
        self.heap_samples.append(value)

    def clock(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class LongSoakTests(unittest.TestCase):
    def test_absent_stale_exhausted_or_changed_controller_rejected(self):
        for name, value in (('available', False), ('sample_age_ms', 30001),
                            ('tls_stack_min_free_bytes', 0), ('internal_free', -1),
                            ('free_8bit', True), ('sample_sequence', 'nothex'),
                            ('boot_id', '00000001')):
            with self.subTest(field=name):
                broken = copy.deepcopy(sample())
                broken['values']['esp32'][name] = value
                with self.assertRaises(ValueError):
                    checked_metrics(broken, '1234abcd')
        with self.assertRaises(ValueError):
            checked_metrics({'values': {}})

    def test_duration_means_elapsed_work_not_request_count(self):
        run = FakeRun()
        result = exercise(run, 9, None, run.clock, run.sleep)
        self.assertTrue(result['duration_completed'])
        self.assertGreaterEqual(result['elapsed_seconds'], 9)
        self.assertEqual(run.exchanges, 8)

    def test_failed_exchange_stops_before_further_requests(self):
        run = FakeRun(passed=False)
        with self.assertRaises(RuntimeError):
            exercise(run, 9, None, run.clock, run.sleep)
        self.assertEqual(run.exchanges, 1)

    def test_controller_restart_is_not_a_completed_soak(self):
        run = FakeRun(restart=True)
        with self.assertRaisesRegex(ValueError, 'restarted'):
            exercise(run, 9, None, run.clock, run.sleep)

    def test_operator_stop_is_not_a_pass(self):
        with TemporaryDirectory() as directory:
            stop = Path(directory)/'stop'
            stop.touch()
            run = FakeRun()
            with self.assertRaises(InterruptedError):
                exercise(run, 9, stop, run.clock, run.sleep)
            self.assertEqual(run.exchanges, 0)
