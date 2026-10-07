#!/usr/bin/env python3
"""Exercise retail menu factories and production page/container destruction.

Uses real IndexedList, Browsable and configuration-page implementations with
substituted configuration stores and UI rendering. No device operations.
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

from check_menu_state import function

ROOT = Path(__file__).resolve().parents[2]


def check(tree, *, expect_leak=False):
    ui = tree / 'software/userinterface'
    tree_text = (ui / 'tree_browser.cc').read_text()
    state_text = (ui / 'tree_browser_state.cc').read_text()
    config_text = (ui / 'config_menu.cc').read_text()
    header = (ui / 'config_menu.h').read_text()
    menu = (ui / 'commodore_menu.cc').read_text()
    definitions = []
    for text, signatures in (
        (tree_text, ('TreeBrowser :: TreeBrowser(', 'TreeBrowser :: ~TreeBrowser(',
                     'void TreeBrowser :: replace_root_state(')),
        (state_text, ('TreeBrowserState :: TreeBrowserState(', 'TreeBrowserState :: ~TreeBrowserState(',
                      'void TreeBrowserState :: cleanup(')),
        (config_text, ('ConfigBrowser :: ConfigBrowser(', 'ConfigBrowser :: ~ConfigBrowser(',
                       'ConfigBrowserState :: ConfigBrowserState(', 'ConfigBrowserState :: ~ConfigBrowserState(',
                       'void ConfigBrowser :: start(')),
    ):
        definitions.extend(function(text, signature) for signature in signatures)
    fixture = Path(__file__).with_name('menu_state_fixture.cc').read_text().split('int main()')[0]
    fixture = fixture.replace('#include <set>', '#include <set>\n#include <cstring>\n#include <vector>')
    indexed = (tree / 'software/components/indexed_list.h').read_text()
    fixture = fixture.replace('template<class T> struct IndexedList {};',
                              '#define ENTER_SAFE_SECTION\n#define LEAVE_SAFE_SECTION\n' + indexed)
    browsable = function((ui / 'browsable.h').read_text(), 'class Browsable\n') + ';'
    fixture = fixture.replace(function(fixture, 'struct Browsable {') + ';',
                              'struct Action { int function = 0; };\n' + browsable)
    fixture = fixture.replace('int Browsable::instances = 0;', '')
    fixture = fixture.replace('IndexedList<Browsable *> emptyList;',
                              'IndexedList<Browsable *> emptyList(0, NULL);')
    fixture = fixture.replace('struct UserInterface {};', '''struct TreeBrowser;
struct UserInterface {
    TreeBrowser *active = NULL;
    void activate_uiobject(TreeBrowser *browser) { assert(!active); active = browser; }
};''')
    fixture = fixture.replace('    ~ConfigBrowser();',
                              '    ~ConfigBrowser();\n    void init();\n    static void start(UserInterface *);')
    fixture = fixture.replace('ConfigBrowser(UserInterface *, Browsable *, int);',
                              'ConfigBrowser(UserInterface *, Browsable *, int = 0);')
    definitions = '\n'.join(definitions)
    definitions = definitions.replace('children = &emptyList;', 'children = &emptyList; live.insert(this);')
    definitions = definitions.replace('TreeBrowserState :: ~TreeBrowserState()\n{',
                                      'TreeBrowserState :: ~TreeBrowserState()\n{\n    assert(live.erase(this) == 1);')
    item = 'class BrowsableConfigItem : public Browsable {\n    ConfigItem *item;\npublic:\n'
    item += function(header, '    BrowsableConfigItem(ConfigItem *i)')
    item += function(header, '    ~BrowsableConfigItem()') + '\n};\n'
    pages = item + '\n'.join(function(header, 'class ' + name) + ';' for name in (
        'BrowsableConfigStore:', 'BrowsableConfigGroup:', 'BrowsableConfigRoot:',
        'BrowsableConfigRootPredefined :'))
    # Page constructor reads the configuration item's type, but does not own it.
    setup = Path(__file__).with_name('menu_pages_fixture.cc').read_text()
    setup = setup.replace('// INSERT_PAGE_CLASSES', pages)
    setup = setup.replace('// INSERT_LIFETIMES', definitions)
    factories = '\n'.join(function(menu, 'SubsysResultCode_e CommodoreMenu :: ' + name + '(')
                           for name in ('S_cfg_page', 'S_cfg_group', 'S_cfg_audio', 'S_advanced'))
    setup = setup.replace('// INSERT_FACTORIES', factories)
    owned = ui / 'owned_config_browser.h'
    setup = setup.replace('// INSERT_OWNED_BROWSER', owned.read_text() if owned.exists() else '')
    source = fixture.replace('// INSERT_PRODUCTION_LIFETIMES', setup)
    with tempfile.TemporaryDirectory(prefix='menu-page-test-') as folder:
        out = Path(folder)
        (out / 'config_menu.h').write_text('// Types supplied by the extracted production fixture.\n')
        (out / 'test.cc').write_text(source)
        subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Wno-unused-parameter',
                        '-Wno-reorder', '-Werror', '-g', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                        f'-DEXPECT_PAGE_LEAK={int(expect_leak)}', str(out / 'test.cc'),
                        '-o', str(out / 'test')], check=True)
        subprocess.run([str(out / 'test')], check=True, timeout=15)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, default=ROOT)
    parser.add_argument('--expect-leak', action='store_true', help='Negative comparison against old retail page ownership')
    args = parser.parse_args()
    check(args.tree, expect_leak=args.expect_leak)
