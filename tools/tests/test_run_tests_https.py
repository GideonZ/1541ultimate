"""Offline regressions for run-tests' controlled HTTPS entry and reports."""

import contextlib
import copy
import hashlib
import importlib.machinery
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

from test_https_hardware import plan, runner as campaign, synthetic

ROOT = Path(__file__).resolve().parents[2]
loader = importlib.machinery.SourceFileLoader("https_runner_harness", str(ROOT / "run-tests"))
spec = importlib.util.spec_from_loader(loader.name, loader)
harness = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = harness
loader.exec_module(harness)
from runtests import https as controlled  # noqa: E402 (loaded by run-tests)


class Policy(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.plan_path = self.directory / "plan.json"
        self.plan_path.write_text(json.dumps(plan()))
        self.output = self.directory / "output"
        # Applies to CLI and children. A forbidden operation is recorded even
        # if the program catches the exception and reports an expected failure.
        guard = self.directory / "guard"
        guard.mkdir()
        self.marker = self.directory / "network-attempted"
        (guard / "sitecustomize.py").write_text(
            "import os, sys\nfrom pathlib import Path\n"
            "def guard(event, args):\n"
            "    if event in ('socket.connect','socket.bind','socket.getaddrinfo'):\n"
            "        Path(os.environ['NETWORK_MARKER']).write_text(event)\n"
            "        raise RuntimeError('Network forbidden in policy tests')\n"
            "sys.addaudithook(guard)\n")
        self.environment = dict(os.environ, PYTHONPATH=str(guard), NETWORK_MARKER=str(self.marker))

    def cli(self, *args):
        result = subprocess.run([sys.executable, str(ROOT / "run-tests"), *map(str, args)],
                                env=self.environment, cwd=ROOT, capture_output=True, text=True,
                                timeout=30, check=False)
        self.assertFalse(self.marker.exists(), "CLI attempted network access")
        return result

    def command(self, *extra):
        return ["-s", "https-hardware", "--https-plan", str(self.plan_path),
                "-o", str(self.output), *extra]

    def read_result(self):
        result = json.loads((self.output / "result.json").read_text())
        rows = [json.loads(line) for line in (self.output / "https/run.jsonl").read_text().splitlines()]
        final = next(row for row in rows if row['kind'] == 'run')
        self.assertEqual(final['exit_code'], result['exit_code'])
        self.assertEqual(final['verdict'], result['verdict'])
        self.assertEqual(final['recoveries'], 0)
        self.assertFalse(final['full_hardware_qualification'])
        return result, rows

    def test_profiles_never_select_hardware(self):
        suite = next(s for s in harness.SUITES if s.name == controlled.NAME)
        self.assertTrue(suite.needs_device)
        cover = harness.coverage.build(harness.SUITES, harness.CATEGORIES)
        for profile in harness.profiles.ORDER:
            for manual in (False, True):
                chosen = harness.selected_suites('e2e', [], manual, profile)
                self.assertNotIn(suite, chosen)
            self.assertNotIn(suite.name, cover.included[profile])
        self.assertEqual(harness.selected_suites('e2e', [suite.name], False), [suite])
        self.assertIn('explicit-only', self.cli('--list').stdout)

    def test_invalid_cli_stops_before_ordinary_setup(self):
        combinations = [[], ['--https-action', 'execute'], ['--https-action', 'audit'],
                        ['--https-plan', 'missing', '-s', 'lint'],
                        ['-s', controlled.NAME, '-s', 'lint']]
        combinations += [self.command(*flags) for flags in (
            ['device-name'], ['-H', 'different-device'], ['--all'], ['--manual'],
            ['--profile', 'deep'], ['-m', 'all'], ['--attempts', '2'],
            ['--recover-command', 'must-not-run'], ['--record'], ['--syslog'],
            ['--command-interface'], ['--kernal', 'must-not-upload'],
            ['--https-action', 'execute'], ['--https-installation', 'ignored'],
            ['--https-peer', 'ignored'])]
        combinations[0] = ['-s', controlled.NAME]
        for args in combinations:
            with self.subTest(args=args), contextlib.ExitStack() as stack:
                for name in ('resolve_targets', 'Device', 'capture_settings', 'start_recorder',
                             'start_syslog', 'reset_to_clean_slate', 'restore_settings'):
                    stack.enter_context(patch.object(harness, name, side_effect=AssertionError(name)))
                launch = stack.enter_context(patch.object(controlled.subprocess, 'run'))
                stack.enter_context(contextlib.redirect_stderr(io.StringIO()))
                with self.assertRaises(SystemExit) as caught:
                    harness.main(args)
                self.assertEqual(caught.exception.code, 64)
                launch.assert_not_called()
                self.assertFalse(self.output.exists())

    def test_prepare_cli_is_offline_and_not_a_hardware_pass(self):
        result = self.cli(*self.command())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        saved, rows = self.read_result()
        self.assertEqual(saved['status'], 'prepared-offline')
        self.assertEqual(saved['verdict'], 'SKIP')
        self.assertFalse(any(row.get('verdict') == 'OK' for row in rows))
        prepared = json.loads((self.output / 'campaign/preparation.json').read_text())
        self.assertFalse(prepared['device_contact'])
        self.assertEqual(prepared['case_count'], 43)
        rendered = subprocess.run([sys.executable, str(ROOT / 'tools/e2e_report.py'), str(self.output)],
                                  env=self.environment, capture_output=True, text=True, check=False)
        self.assertEqual(rendered.returncode, 0, rendered.stderr)
        summary = (self.output / 'index.md').read_text()
        self.assertIn('HTTPS campaign (prepare): SKIP', summary)
        self.assertNotIn('every suite passed', summary)
        self.assertIn('offline plan validated; hardware not executed', summary)
        self.assertIn('suites=1  ok=0  fail=0  warn=0  skip=1', summary)
        before = (self.output / 'result.json').read_bytes()
        self.assertEqual(self.cli(*self.command()).returncode, 64)
        self.assertEqual((self.output / 'result.json').read_bytes(), before)

    def installation(self):
        evidence = self.directory / 'confirmation.txt'
        evidence.write_text('Synthetic test record; not operator authorization')
        return dict(candidate_sha256='a'*64,
                    plan_sha256=hashlib.sha256(json.dumps(plan(), sort_keys=True).encode()).hexdigest(),
                    device='192.168.1.107', expires_unix=time.time()+300,
                    gates={name: dict(confirmed=True, evidence=evidence.name,
                                     sha256=campaign.digest(evidence))
                           for name in campaign.REQUIRED_GATES})

    def test_real_cli_rejects_every_installation_gate_without_contact(self):
        original = self.installation()
        bad_records = []
        for name in campaign.REQUIRED_GATES:
            bad = copy.deepcopy(original)
            bad['gates'][name]['confirmed'] = False
            bad_records.append(bad)
        for key, value in (('expires_unix', 0), ('candidate_sha256', 'b'*64),
                           ('plan_sha256', 'b'*64), ('device', '127.0.0.1')):
            bad = copy.deepcopy(original)
            bad[key] = value
            bad_records.append(bad)
        for key, value in (('sha256', 'b'*64), ('evidence', 'missing'),
                           ('evidence', '../outside')):
            bad = copy.deepcopy(original)
            bad['gates'][campaign.REQUIRED_GATES[0]][key] = value
            bad_records.append(bad)
        # Even a valid installation cannot reach the device without peer logs.
        bad_records.append(original)
        for index, record in enumerate(bad_records):
            with self.subTest(index=index):
                self.output = self.directory / f'rejected-{index}'
                installation = self.directory / 'installation.json'
                installation.write_text(json.dumps(record))
                result = self.cli(*self.command('--https-action', 'execute',
                                               '--https-installation', installation))
                self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
                saved, _ = self.read_result()
                self.assertEqual(saved['verdict'], 'FAIL')
                self.assertFalse((self.output / 'campaign/run.json').exists())

    def test_execute_once_and_pending_audit_is_not_pass(self):
        for child_code in (0, 1, -2):
            self.output = self.directory / f'child-{child_code}'
            with patch.object(controlled.subprocess, 'run',
                              return_value=subprocess.CompletedProcess([], child_code)) as launch:
                with contextlib.redirect_stdout(io.StringIO()):
                    code = harness.main(self.command('--https-action', 'execute',
                                        '--https-installation', str(self.directory / 'record.json'),
                                        '--no-retry', '-p', 'synthetic-password'))
            self.assertEqual(code, 3)
            launch.assert_called_once()
            command = launch.call_args.args[0]
            self.assertIn('--execute', command)
            self.assertNotIn('synthetic-password', command)
            self.assertEqual(launch.call_args.kwargs['env']['U64_PASSWORD'], 'synthetic-password')
            saved, rows = self.read_result()
            self.assertNotIn('synthetic-password', (self.output / 'https/run.jsonl').read_text())
            self.assertEqual(saved['verdict'], 'SKIP' if child_code == 0 else 'FAIL')
            self.assertEqual(next(row for row in rows if row['kind'] == 'run')['passed'], 0)
            rendered = subprocess.run([sys.executable, str(ROOT / 'tools/e2e_report.py'), str(self.output)],
                                      env=self.environment, capture_output=True, text=True, check=False)
            self.assertEqual(rendered.returncode, 0, rendered.stderr)
            self.assertIn('HTTPS campaign (execute): FAIL', (self.output / 'index.md').read_text())

    def test_audit_cli_uses_raw_evidence_and_rejects_mutation(self):
        source = self.directory / 'synthetic-evidence'
        source.mkdir()
        peers = synthetic(source)
        args = ['-s', controlled.NAME, '--https-action', 'audit', '--https-evidence', str(source)]
        for peer in peers:
            args.extend(['--https-peer', str(peer)])
        result = self.cli(*args, '-o', self.output)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        saved, _ = self.read_result()
        self.assertEqual(saved['status'], 'saved-evidence-audit-passed')
        self.assertFalse(json.loads((self.output / 'audit.json').read_text())['full_hardware_qualification'])
        (source / 'transactions.jsonl').write_text('')
        self.output = self.directory / 'bad-audit'
        self.assertEqual(self.cli(*args, '-o', self.output).returncode, 3)
        saved, _ = self.read_result()
        self.assertEqual(saved['verdict'], 'FAIL')


if __name__ == '__main__':
    unittest.main(verbosity=2)
