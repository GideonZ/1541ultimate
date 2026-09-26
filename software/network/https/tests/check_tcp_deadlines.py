#!/usr/bin/env python3
"""Execute production socket expiry decisions with a controlled clock and ownership."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
source = (root.parent/'https_management.cc').read_text()
start = source.index('static void expire_tcp_owner()')
end = source.index('static void socket_worker(void *)', start)
fixture = (root/'tcp_deadline_fixture.cc').read_text()
assert fixture.count('// PRODUCTION_DEADLINE_DECISIONS') == 1
with tempfile.TemporaryDirectory(prefix='tcp-deadlines-') as directory:
    folder = Path(directory)
    unit = folder/'test.cc'
    unit.write_text(fixture.replace('// PRODUCTION_DEADLINE_DECISIONS', source[start:end]))
    subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I'+str(root.parent), str(unit), '-o', str(folder/'test')], check=True)
    subprocess.run([str(folder/'test')], check=True, timeout=10)
