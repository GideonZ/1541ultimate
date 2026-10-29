"""Validate raw production-task guest records and independent TLS peer evidence."""
from collections import Counter
import re

NAMES = ['valid', 'hostname', 'untrusted', 'timeout', 'renewed', 'missing',
         'close', 'restart', 'control-overflow', 'data-overflow', 'buffer-loss', 'send-loss']


def assess(text, peers, run, cipher, request):
    labels = [n for name in NAMES for n in (name, 'recovery')]
    expected = [(str(i+1), n, run) for i, n in enumerate(labels)]
    header = re.findall(r'^TASK_RUNTIME run=(\S+) profile=(\S+)\r?$', text, re.M)
    profile = 'ecdsa' if 'ECDSA' in cipher else 'rsa'
    attempts = re.findall(r'^TASK_ATTEMPT ordinal=(\d+) name=(\S+) run=(\S+)\r?$', text, re.M)
    rows = re.findall(r'^TASK_RESULT ordinal=(\d+) name=(\S+) elapsed_ms=(\d+) lease_ms=(\d+) renewals=(\d+) ack_ms=(\d+) signal_ms=(\d+) stack=(\d+) ticks=(\d+) gap_ms=(\d+) free=(\d+) run=(\S+)\r?$', text, re.M)
    complete = attempts == expected == [(r[0], r[1], r[-1]) for r in rows]
    timing = bool(rows)
    for _, name, elapsed, lease, renewals, ack, signal, stack, ticks, gap, _, _ in rows:
        elapsed, lease, renewals, ack, signal, stack, ticks, gap = map(int, (elapsed, lease, renewals, ack, signal, stack, ticks, gap))
        timing &= stack >= 1024 and ticks > 0 and gap <= 250 and ack <= 250
        if name == 'renewed':
            timing &= 6000 <= elapsed <= 8250 and renewals >= 6
        elif name == 'timeout':
            timing &= 2000 <= elapsed <= 2250
        elif name == 'missing':
            timing &= 5000 <= lease <= 5250 and renewals == 0
        elif name in ('close', 'restart'):
            timing &= 500 <= elapsed <= 1750 and signal <= 250
    kinds = Counter(e['kind'] for e in peers)
    peer_ok = kinds['accepted'] == kinds['closed'] == 24
    peer_ok &= kinds['authenticated'] == kinds['request'] == 22
    peer_ok &= kinds['tls-alert'] == 2 and not kinds['fixture-error'] and not kinds['client-hello']
    peer_ok &= all(e['exact'] and bytes.fromhex(e['hex']) == request for e in peers if e['kind'] == 'request')
    peer_ok &= all(e['cipher'] == cipher and e['protocol'] == 'TLSv1.2' for e in peers if e['kind'] == 'authenticated')
    peer_ok &= Counter(e['mode'] for e in peers if e['kind'] == 'request') == Counter(normal=15, read=6, delayed=1)
    summaries = re.findall(r'^SUMMARY checks=(\d+) failures=(\d+) run=(\S+)\r?$', text, re.M)
    schedule = re.findall(r'^TASK_SCHED ticks=(\d+) gap_ms=(\d+) uart_frames=(\d+) errors=(\d+) stack=(\d+) run=(\S+)\r?$', text, re.M)
    scheduling = len(schedule) == 1 and schedule[0][-1] == run and int(schedule[0][0]) > 0 and int(schedule[0][1]) <= 250 and int(schedule[0][2]) > 0 and schedule[0][3] == '0' and int(schedule[0][4]) >= 1024
    counts = re.findall(r'^TASK_COUNT before=(\d+) after=(\d+) run=(\S+)\r?$', text, re.M)
    scheduling &= len(counts) == 1 and counts[0][0] == counts[0][1] and int(counts[0][0]) > 0 and counts[0][2] == run
    policy_labels = ['policy rejects missing roots', 'policy rejects malformed roots', 'policy bounds trust input',
                     'policy parses explicit roots', 'policy blocks unqualified session', 'policy requires entropy qualification',
                     'policy permits qualified callbacks', 'policy retains entropy revocation across restore',
                     'policy permits fresh session after requalification', 'policy clock update retires old session',
                     'policy compares actual UTC with qualified anchor', 'policy has no entropy fallback',
                     'policy trust is immutable after configuration']
    source_markers = re.findall(r'^SOURCE_TRUST run=(\S+)\r?$', text, re.M)
    source_labels = []
    if source_markers:
        source_labels = ['system trust bundle configured', 'system trust replacement refused',
                         'system trust verifier attached', 'pinned public root parsed',
                         'pinned public root verified by system bundle', 'system verifier preserves expiry failure',
                         'private fixture root parsed', 'system bundle rejects private fixture root']
    expected_labels = ['private emulator network ready', 'fixture UTC installed'] + source_labels + policy_labels + ['boot trust configured',
                       'live trust replacement refused', 'independent scheduling task started']
    for name in labels:
        expected_labels += ['no stale UART output', 'production task offered grant',
                            'production task initialized', 'authenticated open outcome']
        if name not in ('hostname', 'untrusted'):
            expected_labels += ['copied request delivered', 'exact response and authenticated EOF' if name in ('valid', 'renewed', 'recovery') else 'failure publishes no body']
        extra = {'renewed': 'long call survives production lease with renewals',
                 'timeout': 'original deadline survives queue and renewals',
                 'missing': 'production lease expires without renewal',
                 'close': 'control cancellation return bound', 'restart': 'control cancellation return bound'}
        if name in extra:
            expected_labels.append(extra[name])
        if name in ('control-overflow', 'data-overflow', 'buffer-loss', 'send-loss'):
            expected_labels.append('delivery fault retired grant before explicit close')
        expected_labels += ['control acknowledged after TLS unwind' if name in ('close', 'restart') else 'owner cleanup acknowledged',
                            'injected UART fault consumed', 'renewals acknowledged within bound',
                            'independent task remains scheduled', 'production owner stack reserve', 'heap integrity after owner cleanup']
    expected_labels += ['scheduling task joined', 'UART ownership and copied replies intact', 'repeated start creates no duplicate task']
    # Five setup, three final and 24 scenario check blocks. Special cases add
    # deadline, lease or cancellation checks; certificate rejections skip I/O.
    checks = 5 + 3 + 24*12 - 2*2 + 5 + 4 + len(policy_labels) + len(source_labels)
    passed = (complete and timing and peer_ok and scheduling and header == [(run, profile)] and
              (not source_markers or source_markers == [run]) and
              text.count('main_task: Returned from app_main()') == 1 and
              summaries == [(str(checks), '0', run)] and
              re.findall(r'^PASS ([^\r\n]+)\r?$', text, re.M) == expected_labels and len(expected_labels) == checks and
              text.count('ESP-ROM:esp32s3-20210327') == 1 and
              not re.search(r'^FAIL|Guru Meditation|assert failed|abort\(\) was called', text, re.M))
    return dict(passed=bool(passed), complete_attempts=complete, timing_passed=bool(timing),
                peer_accounting_passed=bool(peer_ok), peer_counts=dict(kinds),
                scheduling_passed=bool(scheduling), checks=checks, observations=rows, schedule=schedule, task_counts=counts)
