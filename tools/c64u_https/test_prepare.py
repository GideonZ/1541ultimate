"""Run with python3 -m unittest discover -s tools/c64u_https -v."""
import tempfile
import unittest
from pathlib import Path

from build import verify_sources
from check_menu_pages import check as check_menu_pages
from check_menu_state import check as check_menu_state
from check_menu_windows import check as check_menu_windows
from prepare import BASELINE, ROOT, git, prepare, replace_once
from verify import verify_architecture, verify_embedded, verify_recovery


class RetailSnapshotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="c64u-https-test-")
        cls.tree = Path(cls.temp.name) / "retail"
        cls.manifest = prepare(ROOT, cls.tree)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_retail_hardware_and_controller_behavior_preserved(self):
        for name in ("software/system/product.cc", "software/system/u64ii_init.cc",
                     "software/io/flash/w25q_flash.cc", "software/u64ctrl/main/pinout.h",
                     "software/u64ctrl/main/button_handler.c", "software/u64ctrl/main/control_main.c",
                     "software/u64ctrl/main/wifi_modem.c", "software/components/indexed_list.h",
                     "software/u64/bling_board.cc",
                     "target/u64ii/riscv/ultimate/linker.x", "target/u64ii/riscv/update/Makefile",
                     "software/application/update_u2p/update_u64ii.cc", "external/u64_mk2_artix.bit"):
            with self.subTest(path=name):
                self.assertEqual((self.tree / name).read_bytes(), git(ROOT, "show", BASELINE + ":" + name))

    def test_management_uses_retail_architecture_and_shared_target(self):
        text = (self.tree / "target/u64ii/riscv/ultimate/Makefile").read_text()
        for expected in ("MARCH ?= rv32i\n", "-DCOMMODORE=1", "-DULTIMATE_HTTPS", "http_request.cc http_target.cc https_management.cc"):
            self.assertIn(expected, text)
        self.assertNotIn("rv32im", text)
        self.assertEqual((self.tree / "software/io/command_interface/http_target.cc").read_bytes(),
                         (ROOT / "software/io/command_interface/http_target.cc").read_bytes())

    def test_reviewed_response_patch_and_heap_probe_are_included(self):
        from httpd_patch import apply
        parser = self.tree / 'software/httpd/c-version/lib/http_protocol.c'
        original = parser.read_bytes()
        self.assertIn(b'parse_response_header', original)
        apply(self.tree / 'software/httpd')  # applying twice is harmless
        self.assertEqual(parser.read_bytes(), original)
        self.assertIn('tools/c64u_https/httpd-response.patch', self.manifest['shared_inputs'])
        self.assertIn('API_CALL(GET, machine, heap',
                      (self.tree / 'software/api/route_machine.cc').read_text())
        try:
            parser.write_bytes(original.replace(b'parse_response_header', b'unrelated_local_edit'))
            with self.assertRaisesRegex(ValueError, 'preserve local edits'):
                apply(self.tree / 'software/httpd')
        finally:
            parser.write_bytes(original)

    def test_recorded_sources_match_and_mutation_is_rejected(self):
        verify_sources(self.tree, self.manifest)
        name = self.tree / "external/u64_mk2_artix.bit"
        original = name.read_bytes()
        try:
            name.write_bytes(original + b"wrong image")
            with self.assertRaisesRegex(ValueError, "Prepared source changed"):
                verify_sources(self.tree, self.manifest)
        finally:
            name.write_bytes(original)

    def test_metrics_controller_upgrade_and_retail_probe(self):
        identity = (self.tree / 'software/u64ctrl/main/rpc_dispatch.h').read_text()
        self.assertIn('#define IDENT_MINOR   18', identity)
        self.assertIn('V1.18 C64U HTTPS', identity)
        self.assertIn('tls_metrics.h', '\n'.join(self.manifest['shared_inputs']))
        self.assertIn('https_management_add_metrics(resp->json)',
                      (self.tree / 'software/api/route_machine.cc').read_text())
        self.assertIn('CMD_TLS_METRICS 0x31',
                      (self.tree / 'software/u64ctrl/main/rpc_calls.h').read_text())

    def test_existing_destination_is_never_overwritten(self):
        with self.assertRaisesRegex(ValueError, "Destination already exists"):
            prepare(ROOT, self.tree)

    def test_menu_state_lifetimes_and_original_regression(self):
        check_menu_state(self.tree)
        # The same assertions must expose one orphan per retail menu opening.
        with tempfile.TemporaryDirectory(prefix='retail-menu-before-') as folder:
            before = Path(folder)
            for name in ('tree_browser.cc', 'tree_browser_state.cc', 'config_menu.cc'):
                relative = 'software/userinterface/' + name
                path = before / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(git(ROOT, 'show', BASELINE + ':' + relative))
            check_menu_state(before, expected_orphans=5)

    def test_owned_retail_pages_and_borrowed_audio_root(self):
        check_menu_pages(self.tree)
        # Restore only page ownership, retaining the independently tested state
        # repair, to demonstrate the distinct page leak in the old menu actions.
        import shutil
        with tempfile.TemporaryDirectory(prefix='retail-page-before-') as folder:
            before = Path(folder)
            for name in ('config_menu.cc', 'tree_browser.cc', 'tree_browser_state.cc', 'browsable.h'):
                relative = 'software/userinterface/' + name
                dest = before / relative
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(self.tree / relative, dest)
            for name in ('software/userinterface/config_menu.h', 'software/userinterface/commodore_menu.cc',
                         'software/components/indexed_list.h'):
                dest = before / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(git(ROOT, 'show', BASELINE + ':' + name))
            check_menu_pages(before, expect_leak=True)

    def test_retail_menu_changes_are_limited_to_root_ownership(self):
        name = 'software/userinterface/commodore_menu.cc'
        actual = (self.tree / name).read_text()
        restored = actual.replace('#include "owned_config_browser.h"\n', '')
        restored = restored.replace('new OwnedConfigBrowser(', 'new ConfigBrowser(')
        self.assertEqual(restored, git(ROOT, 'show', BASELINE + ':' + name).decode().replace('\r\n', '\n'))

    def test_retail_window_lifetimes_and_original_regression(self):
        check_menu_windows(self.tree)
        with tempfile.TemporaryDirectory(prefix='retail-window-before-') as folder:
            before = Path(folder)
            for name in ('software/io/c64/screen.cc', 'software/userinterface/context_menu.cc',
                         'software/userinterface/commodore_menu.cc', 'software/userinterface/userinterface.cc'):
                path = before / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(git(ROOT, 'show', BASELINE + ':' + name))
            check_menu_windows(before, expect_leak=True)

    def test_context_menu_changes_only_release_windows_and_guard_drawing(self):
        name = 'software/userinterface/context_menu.cc'
        actual = (self.tree / name).read_text()
        restored = actual.replace('        delete window;\n        window = NULL;\n', '')
        restored = restored.replace('    delete window;\n    window = NULL;\n', '')
        restored = restored.replace('    if (!window) {\n        return;\n    }\n', '')
        self.assertEqual(restored, git(ROOT, 'show', BASELINE + ':' + name).decode().replace('\r\n', '\n'))

    def test_wrong_retail_identity_is_rejected(self):
        for key, value in (("baseline", "master"), ("product", "Ultimate 64 Elite II")):
            with self.subTest(key=key), self.assertRaises(ValueError):
                verify_sources(self.tree, dict(self.manifest, **{key: value}))


class PackageGuardTests(unittest.TestCase):
    def test_missing_or_ambiguous_patch_anchor_is_rejected(self):
        for text in ("absent", "anchor anchor"):
            with self.assertRaises(ValueError):
                replace_once(text, "anchor", "new")

    def test_wrong_isa_and_unsupported_instruction_rejected(self):
        attr = 'Tag_RISCV_arch: "rv32i2p0"'
        verify_architecture(attr, " 30000: addi sp,sp,-16\n 30004: csrr a0,mstatus")
        for arch in ("rv32i2p0_m2p0", "rv64i2p0", "rv32i2p0_c2p0", ""):
            with self.subTest(arch=arch), self.assertRaises(ValueError):
                verify_architecture(f'Tag_RISCV_arch: "{arch}"', " 30000: addi sp,sp,-16")
        for instruction in ("mul", "divu", "rem", "fence.i", "amoadd.w", "lr.w", "c.addi"):
            with self.subTest(instruction=instruction), self.assertRaises(ValueError):
                verify_architecture(attr, " 30000: " + instruction + " a0,a1,a2")

    def test_stale_missing_and_duplicate_embedded_image_rejected(self):
        verify_embedded(b"prefix APP suffix FPGA", {"management": b"APP", "fpga": b"FPGA"})
        for package in (b"prefix OLD suffix", b"APP APP", b""):
            with self.assertRaises(ValueError):
                verify_embedded(package, {"management": b"APP"})

    def test_unreviewed_recovery_rejected(self):
        with self.assertRaisesRegex(ValueError, "reviewed official"):
            verify_recovery(b"another firmware", b"fpga", b"partitions")


if __name__ == "__main__":
    unittest.main()
