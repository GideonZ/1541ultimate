#!/usr/bin/env python3
"""Check retail window lifetime through actual menu/UI lifecycle methods.

Rendering and external UI dependencies are substitutes. Production window
construction/destruction and menu appearance/hide/cleanup methods are extracted.
No device operations.
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

from check_menu_state import function

ROOT = Path(__file__).resolve().parents[2]


def check(tree, *, expect_leak=False):
    ui = tree / 'software/userinterface'
    definitions = []
    for path, signatures in (
        (tree / 'software/io/c64/screen.cc', ('Window :: Window(', 'Window :: ~Window(')),
        (ui / 'context_menu.cc', ('ContextMenu :: ~ContextMenu(', 'void ContextMenu :: deinit(',
                                  'void ContextMenu :: draw(')),
        (ui / 'commodore_menu.cc', ('void CommodoreMenu :: init(',)),
        (ui / 'userinterface.cc', ('void UserInterface :: appear(', 'void UserInterface :: release_host(')),
    ):
        text = path.read_text()
        definitions.extend(function(text, sig) for sig in signatures)
    text = '\n'.join(definitions)
    text = text.replace('    old_color = parent->get_color();',
                        '    live.insert(this);\n    old_color = parent->get_color();')
    text = text.replace('Window :: ~Window()\n{',
                        'Window :: ~Window()\n{\n    assert(live.erase(this) == 1);')
    fixture = Path(__file__).with_name('menu_windows_fixture.cc').read_text()
    source = fixture.replace('// INSERT_LIFECYCLES', text)
    with tempfile.TemporaryDirectory(prefix='menu-window-test-') as folder:
        out = Path(folder)
        (out / 'test.cc').write_text(source)
        subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-g',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                        f'-DEXPECT_WINDOW_LEAK={int(expect_leak)}', str(out / 'test.cc'),
                        '-o', str(out / 'test')], check=True)
        subprocess.run([str(out / 'test')], check=True, timeout=15)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, default=ROOT)
    parser.add_argument('--expect-leak', action='store_true')
    args = parser.parse_args()
    check(args.tree.resolve(), expect_leak=args.expect_leak)
