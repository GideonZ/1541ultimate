#!/usr/bin/env python3
"""Exercise production menu state lifetimes with minimal UI/RTOS substitutes.

Extracts constructors, destructors and state replacement from the specified
source tree. This checks object ownership, not target heap bytes or menu caches.
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    if text.count(signature) != 1:
        raise ValueError(f"Expected one function: {signature}")
    start = text.index(signature)
    brace = text.index('{', start)
    level, end = 1, brace + 1
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def check(tree, expected_orphans=0):
    ui = tree / 'software/userinterface'
    tree_source = (ui / 'tree_browser.cc').read_text()
    state_source = (ui / 'tree_browser_state.cc').read_text()
    config_source = (ui / 'config_menu.cc').read_text()
    bodies = [
        function(tree_source, 'TreeBrowser :: TreeBrowser('),
        function(tree_source, 'TreeBrowser :: ~TreeBrowser('),
        function(state_source, 'TreeBrowserState :: TreeBrowserState('),
        function(state_source, 'TreeBrowserState :: ~TreeBrowserState('),
        function(state_source, 'void TreeBrowserState :: cleanup('),
        function(config_source, 'ConfigBrowser :: ConfigBrowser('),
        function(config_source, 'ConfigBrowser :: ~ConfigBrowser('),
        function(config_source, 'ConfigBrowserState :: ConfigBrowserState('),
        function(config_source, 'ConfigBrowserState :: ~ConfigBrowserState('),
    ]
    signature = 'void TreeBrowser :: replace_root_state('
    if signature in tree_source:
        bodies.append(function(tree_source, signature))
    production = '\n'.join(bodies)
    # Instrument construction/destruction without replacing their real cleanup.
    for old, new in (
        ('children = &emptyList;', 'children = &emptyList; assert(live.insert(this).second);'),
        ('TreeBrowserState :: ~TreeBrowserState()\n{',
         'TreeBrowserState :: ~TreeBrowserState()\n{\n    assert(live.erase(this) == 1);'),
    ):
        if production.count(old) != 1:
            raise ValueError('State lifetime instrumentation anchor changed')
        production = production.replace(old, new)
    fixture = Path(__file__).with_name('menu_state_fixture.cc').read_text()
    marker = '// INSERT_PRODUCTION_LIFETIMES'
    if fixture.count(marker) != 1:
        raise ValueError('Expected one fixture insertion marker')
    with tempfile.TemporaryDirectory(prefix='menu-state-test-') as directory:
        out = Path(directory)
        unit = out / 'test.cc'
        unit.write_text(fixture.replace(marker, production))
        subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-g',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                        f'-DEXPECTED_ORPHANS={expected_orphans}',
                        str(unit), '-o', str(out / 'test')], check=True)
        subprocess.run([str(out / 'test')], check=True, timeout=15)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, default=ROOT)
    check(parser.parse_args().tree)
