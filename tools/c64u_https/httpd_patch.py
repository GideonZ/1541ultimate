"""Apply the reviewed response-parser patch to the pinned HTTPD submodule.

Needed until the dependency publishes a revision containing the fix. The patch
is tracked in the parent repository, so a fresh clone can reproduce the build.
Never overwrite unrelated submodule edits. No network or device operations.
"""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATCH = Path(__file__).with_name('httpd-response.patch')


def apply(tree, patch=PATCH):
    names = ('c-version/lib/http_protocol.c', 'c-version/lib/server.h')
    # Git for Windows can check out CRLF. Check normalized copies first, so a
    # failed match never changes the real dependency (including its line ends).
    with tempfile.TemporaryDirectory(prefix='ultimate-httpd-patch-') as folder:
        staged = Path(folder)
        for name in names:
            target = staged / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((tree / name).read_text().encode())
        def check(*args):
            return subprocess.run(['git', 'apply', '--check', *args, str(patch.resolve())],
                                  cwd=staged, capture_output=True, check=False).returncode == 0
        if check():
            subprocess.run(['git', 'apply', str(patch.resolve())], cwd=staged, check=True)
            for name in names:
                (tree / name).write_bytes((staged / name).read_bytes())
        elif not check('--reverse'):
            raise ValueError('HTTPD response patch does not match; preserve local edits and inspect the dependency')


if __name__ == '__main__':
    apply(ROOT / 'software/httpd')
    print('Reviewed HTTP response parser patch is applied.')
