"""Exercise parser preparation in fresh exports and reused submodule checkouts."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("prepare_http_parser", ROOT / "tools/prepare_http_parser.py")
HELPER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPER)


class ParserPreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads((ROOT / "patches/http-response-framing.json").read_text())
        cls.archive = subprocess.check_output([
            "git", "-C", str(ROOT / "software/httpd"), "archive", "--format=zip", cls.manifest["base"]])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="http-parser-preparation-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.parser = self.root / "software/httpd"
        zipfile.ZipFile(io.BytesIO(self.archive)).extractall(self.parser)
        shutil.copytree(ROOT / "patches", self.root / "patches")
        self.added = [name for name, value in self.manifest["before"].items() if value is None]

    def git(self, *args, cwd=None):
        return subprocess.check_output(["git", *args], cwd=cwd or self.root, stderr=subprocess.STDOUT)

    def snapshot(self):
        return {str(p.relative_to(self.root)): p.read_bytes()
                for p in self.root.rglob("*") if p.is_file() and ".git" not in p.relative_to(self.root).parts}

    def check_after(self):
        for name, expected in self.manifest["after"].items():
            raw = (self.parser / name).read_bytes().replace(b"\r\n", b"\n")
            self.assertEqual(hashlib.sha256(raw).hexdigest(), expected, name)

    def mixed_state(self):
        HELPER.prepare(self.root)
        # A forced submodule checkout restores tracked files but leaves additions.
        with zipfile.ZipFile(io.BytesIO(self.archive)) as archive:
            for name, value in self.manifest["before"].items():
                if value is not None:
                    (self.parser / name).write_bytes(archive.read(name))

    def rejects_without_writes(self):
        before = self.snapshot()
        with self.assertRaises((RuntimeError, FileNotFoundError)):
            HELPER.prepare(self.root)
        self.assertEqual(self.snapshot(), before)

    def test_fresh_export_and_repeat(self):
        HELPER.prepare(self.root)
        self.check_after()
        before = self.snapshot()
        HELPER.prepare(self.root)
        self.assertEqual(self.snapshot(), before)

    def test_export_in_parent_repository(self):
        self.git("init", "-q")
        HELPER.prepare(self.root)
        self.check_after()

    def test_reused_submodule_checkout(self):
        self.git("init", "-q")
        # Use the real public commit and a real gitlink, without a network fetch.
        self.assertTrue(self.parser.resolve().is_relative_to(self.root.resolve()))
        shutil.rmtree(self.parser)
        self.git("-c", "protocol.file.allow=always", "submodule", "add", "--quiet",
                 str(ROOT / "software/httpd"), "software/httpd")
        self.git("checkout", "--force", self.manifest["base"], cwd=self.parser)
        self.git("add", ".gitmodules", "software/httpd", "patches")
        self.git("-c", "user.name=Parser Test", "-c", "user.email=parser@example.invalid",
                 "commit", "-qm", "Record test fixture")
        for _ in range(3):
            HELPER.prepare(self.root)
            self.check_after()
            # Match the checkout actions visible in the failing hosted job.
            self.git("clean", "-ffdx")
            self.git("reset", "--hard", "HEAD")
            self.git("-c", "protocol.file.allow=always", "submodule", "update", "--init", "--force", "--recursive")
            for name in self.added:
                self.assertTrue((self.parser / name).exists())
        HELPER.prepare(self.root)
        self.check_after()

    def test_known_leftover_in_export(self):
        self.mixed_state()
        HELPER.prepare(self.root)
        self.check_after()

    def test_crlf_base(self):
        for name, value in self.manifest["before"].items():
            if value is not None:
                path = self.parser / name
                path.write_bytes(path.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
        HELPER.prepare(self.root)
        self.check_after()

    def test_crlf_leftover(self):
        self.mixed_state()
        for name in self.added:
            path = self.parser / name
            path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        HELPER.prepare(self.root)
        self.check_after()

    def test_unrelated_file_preserved(self):
        self.mixed_state()
        extra = self.parser / "notes.txt"
        extra.write_bytes(b"keep my notes\n")
        HELPER.prepare(self.root)
        self.check_after()
        self.assertEqual(extra.read_bytes(), b"keep my notes\n")

    def test_modified_leftover_rejected(self):
        self.mixed_state()
        path = self.parser / self.added[0]
        path.write_bytes(path.read_bytes() + b"\n// local change\n")
        self.rejects_without_writes()

    def test_arbitrary_file_at_added_path_rejected(self):
        (self.parser / self.added[0]).write_bytes(b"unrelated test\n")
        self.rejects_without_writes()

    def test_modified_tracked_file_rejected(self):
        self.mixed_state()
        path = self.parser / "c-version/lib/http_protocol.c"
        path.write_bytes(path.read_bytes() + b"\n/* local change */\n")
        self.rejects_without_writes()

    def test_missing_tracked_file_rejected(self):
        (self.parser / "c-version/lib/http_protocol.c").unlink()
        self.rejects_without_writes()

    def test_partial_tracked_patch_rejected(self):
        HELPER.prepare(self.root)
        with zipfile.ZipFile(io.BytesIO(self.archive)) as archive:
            name = "c-version/lib/http_protocol.c"
            (self.parser / name).write_bytes(archive.read(name))
        self.rejects_without_writes()

    def test_modified_completed_patch_rejected(self):
        HELPER.prepare(self.root)
        path = self.parser / "c-version/lib/http_protocol.c"
        path.write_bytes(path.read_bytes() + b"\n/* local change */\n")
        self.rejects_without_writes()

    def test_altered_patch_rejected(self):
        path = self.root / "patches/http-response-framing.patch"
        path.write_bytes(path.read_bytes() + b"\n")
        self.rejects_without_writes()


if __name__ == "__main__":
    unittest.main(verbosity=2)
