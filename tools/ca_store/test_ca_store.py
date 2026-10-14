#!/usr/bin/env python3
"""Tests for the Mozilla root store's build gate (ca_store.py). Need no network."""

import base64
import contextlib
import datetime
import hashlib
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import ca_store


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
COMMITTED = (REPO_ROOT / ca_store.STORE_DIR / ca_store.PEM_NAME).read_text()
OLD_DATE = "Tue Jul 14 03:12:01 2026 GMT"
NEW_DATE = "Fri Sep 25 03:12:01 2026 GMT"
OLD_ISO = "2026-07-14T03:12:01Z"
NEW_ISO = "2026-09-25T03:12:01Z"
NOW = datetime.datetime(2026, 9, 29, 12, 0, 0)
OLD_REF = "mozilla-firefox/firefox@" + "1" * 40
NEW_REF = "mozilla-firefox/firefox@" + "2" * 40
BRIDGE = "software/wifi/raw_u64"

SDKCONFIG = """\
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
# CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL is not set
# CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN is not set
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE=y
CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH="%s"
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS=%d
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y
"""

HEADER = """\
#define IDENT_STRING "ESP32 WiFi Bridge V1.%d"
#define IDENT_MAJOR   1
// 1.%d: something earlier.
#define IDENT_MINOR   %d
"""


def pem(date, first=0, count=20):
    """A store in curl's layout: its header with the given date, then some real roots."""
    header, _, body = COMMITTED.partition("\n\n")
    header = "\n".join(ca_store.DATE_PREFIX + date if line.startswith(ca_store.DATE_PREFIX)
                       else line for line in header.split("\n"))
    blocks = body.strip("\n").split("\n\n")
    return header + "\n\n\n" + "\n\n".join(blocks[first:first + count]) + "\n"


def der_length(n):
    if n < 0x80:
        return bytes([n])
    body = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(body)]) + body


def with_key(der, key):
    """der with its SubjectPublicKeyInfo replaced (the signature no longer matches)."""
    _, start, _ = ca_store._tlv(der, 0)
    _, tbs_start, tbs_end = ca_store._tlv(der, start)
    fields, pos = [], tbs_start
    while pos < tbs_end:
        _, _, end = ca_store._tlv(der, pos)
        fields.append(der[pos:end])
        pos = end
    fields[6 if fields[0][0] == 0xA0 else 5] = key
    tbs = b"".join(fields)
    tbs = bytes([0x30]) + der_length(len(tbs)) + tbs
    body = tbs + der[tbs_end:]
    return bytes([0x30]) + der_length(len(body)) + body


def rekeyed(text, victim, donor):
    """text with certificate number `victim` given the public key of number `donor`, keeping its
    subject and label: the swap a compromised mirror would make."""
    ders = ca_store.certificates(text)
    swapped = with_key(ders[victim], ca_store.subject_and_key(ders[donor])[1])
    old_b64 = base64.encodebytes(ders[victim]).decode().replace("\n", "")
    new_b64 = base64.b64encode(swapped).decode()
    old_block = "\n".join(old_b64[i:i + 76] for i in range(0, len(old_b64), 76))
    new_block = "\n".join(new_b64[i:i + 76] for i in range(0, len(new_b64), 76))
    assert old_block in text
    return text.replace(old_block, new_block)


def pem_block(der):
    text = base64.b64encode(der).decode()
    return (ca_store.BEGIN + "\n" + "\n".join(text[i:i + 76] for i in range(0, len(text), 76))
            + "\n" + ca_store.END + "\n")


class Tree:
    """A repository with the store, the bridge that builds its bundle from it (and attaches it),
    and a project still on ESP-IDF's own bundle, which the store leaves alone."""

    def __init__(self, root, date=OLD_DATE, max_certs=200):
        self.root = pathlib.Path(root)
        store = self.root / ca_store.STORE_DIR
        store.mkdir(parents=True)
        self.pem = store / ca_store.PEM_NAME
        self.pins = store / ca_store.PINS_NAME
        text = pem(date)
        self.pem.write_text(text)
        self.pins.write_text(
            "# comment kept\nCA_SOURCE_URL=https://curl.se/ca/cacert.pem\n"
            "CA_MOZILLA_DATE=%s\nCA_SHA256=%s\nCA_MAX_AGE_DAYS=180\nCA_CERTDATA_REF=%s\n"
            "CA_BUNDLE_SET_SHA256=%s\n"
            % (ca_store.iso(date), hashlib.sha256(text.encode()).hexdigest(), OLD_REF,
               ca_store.bundle_set_sha256(ca_store.certificates(text))))
        self.bridge = self.project(BRIDGE, "../ca_store/cacert.pem", 6, max_certs)
        (self.bridge / "main/tls.c").write_text(
            "void setup(void) { esp_crt_bundle_attach(&conf); }\n")
        self.sdkconfig = self.bridge / "sdkconfig"
        other = self.root / "software/u64ctrl"
        (other / "main").mkdir(parents=True)
        (other / "sdkconfig").write_text("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y\n"
                                         "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y\n")
        (other / "main/rpc_dispatch.h").write_text(HEADER % (14, 14, 14))

    def project(self, path, relative, minor, max_certs):
        project = self.root / path
        (project / "main").mkdir(parents=True)
        (project / "sdkconfig").write_text(SDKCONFIG % (relative, max_certs))
        (project / "main/rpc_dispatch.h").write_text(HEADER % (minor, minor, minor))
        return project

    def repin(self):
        """A pins file that agrees with cacert.pem's bytes again, but not its set pin."""
        self.pins.write_text(self.pins.read_text().replace(
            ca_store.read_pins(self.pins)["CA_SHA256"],
            hashlib.sha256(self.pem.read_bytes()).hexdigest()))

    def snapshot(self):
        return {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}


class CheckTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.tree = Tree(self.tmp)  # as of 2026-07-14, limit 180 days

    def errors(self):
        return ca_store.check(self.tree.root, today=datetime.date(2026, 9, 29), now=NOW)[0]

    def test_a_fresh_store_passes(self):
        self.assertEqual(ca_store.check(self.tree.root, today=datetime.date(2027, 1, 10), now=NOW), ([], []))

    def test_a_stale_store_warns(self):
        errors, warnings = ca_store.check(self.tree.root, today=datetime.date(2027, 1, 11), now=NOW)
        self.assertEqual(errors, [])
        self.assertIn("181 days old", warnings[0])

    def test_a_stale_store_fails_a_release(self):
        errors, warnings = ca_store.check(self.tree.root, release=True,
                                          today=datetime.date(2027, 1, 11), now=NOW)
        self.assertEqual(warnings, [])
        self.assertIn("181 days old", errors[0])

    def test_a_fresh_store_passes_a_release(self):
        self.assertEqual(ca_store.check(self.tree.root, release=True,
                                        today=datetime.date(2026, 9, 29), now=NOW), ([], []))

    def test_the_release_flag_sets_the_exit_status(self):
        arguments = ["check", "--repo-root", str(self.tree.root), "--today", "2027-06-01"]
        with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(ca_store.main(arguments), 0)
            self.assertEqual(ca_store.main([*arguments, "--release"]), 1)

    def test_a_pins_date_that_disagrees_with_the_store_fails(self):
        self.tree.pins.write_text(self.tree.pins.read_text().replace(OLD_ISO, NEW_ISO))
        self.assertIn("is from %s, CA_MOZILLA_DATE says %s" % (OLD_ISO, NEW_ISO), self.errors()[0])

    def test_a_pins_date_that_is_not_iso_fails(self):
        self.tree.pins.write_text(self.tree.pins.read_text().replace(OLD_ISO, "2026-07-14"))
        with self.assertRaisesRegex(ca_store.StoreError, "not of the form"):
            self.errors()

    def test_more_certificates_than_the_bundle_allows_fails(self):
        shutil.rmtree(self.tree.root)
        self.tree = Tree(self.tree.root, max_certs=19)
        errors = self.errors()
        self.assertEqual(len(errors), 1)
        self.assertIn("MAX_CERTS=19, the store has 20", errors[0])
        shutil.rmtree(self.tree.root)
        self.tree = Tree(self.tree.root, max_certs=20)
        self.assertEqual(self.errors(), [])

    def test_a_date_in_the_future_fails(self):
        errors, _ = ca_store.check(self.tree.root, now=datetime.datetime(2026, 7, 13, 3, 12, 0))
        self.assertIn("in the future", errors[0])

    def test_a_certdata_ref_that_is_not_a_commit_fails(self):
        self.tree.pins.write_text(self.tree.pins.read_text().replace(OLD_REF, "firefox@release"))
        self.assertIn("CA_CERTDATA_REF=firefox@release", self.errors()[0])

    def test_a_malformed_pins_file_is_an_error_not_a_crash(self):
        text = self.tree.pins.read_text()
        for broken in (text + "garbage\n",
                       text.replace("CA_MAX_AGE_DAYS=180", "CA_MAX_AGE_DAYS=x"),
                       text.replace("CA_BUNDLE_SET_SHA256", "# CA_BUNDLE_SET_SHA256")):
            self.tree.pins.write_text(broken)
            with self.assertRaises(ca_store.StoreError):
                ca_store.check(self.tree.root)

    @unittest.skipUnless(hasattr(__import__("time"), "tzset"), "needs time.tzset")
    def test_the_age_is_counted_in_utc_not_local_time(self):
        import time
        now = datetime.datetime.now(datetime.timezone.utc)
        # A zone whose date differs from UTC's at this moment.
        zone = "Etc/GMT-14" if now.hour >= 10 else "Etc/GMT+12"
        shutil.rmtree(self.tree.root)
        # Exactly CA_MAX_AGE_DAYS old by UTC's date, so any other date makes it stale or not.
        date = (now - datetime.timedelta(days=180)).strftime("%a %b %d 00:00:00 %Y GMT")
        self.tree = Tree(self.tree.root, date=date)
        saved = os.environ.get("TZ")
        os.environ["TZ"] = zone
        time.tzset()
        try:
            errors, warnings = ca_store.check(self.tree.root, release=True)
        finally:
            if saved is None:
                del os.environ["TZ"]
            else:
                os.environ["TZ"] = saved
            time.tzset()
        self.assertEqual((errors, warnings), ([], []))

    def test_a_store_edited_by_hand_fails(self):
        self.tree.pem.write_text(self.tree.pem.read_text() + "\n")
        self.assertIn("does not match CA_SHA256", self.errors()[0])

    def test_one_flipped_byte_in_a_certificate_fails(self):
        text = self.tree.pem.read_text()
        at = text.index(ca_store.BEGIN) + len(ca_store.BEGIN) + 40
        self.tree.pem.write_text(text[:at] + ("A" if text[at] != "A" else "B") + text[at + 1:])
        self.assertIn("does not match CA_SHA256", self.errors()[0])

    def test_a_flipped_byte_that_breaks_a_certificate_is_an_error_not_a_crash(self):
        # The flip the ESP32 build's tamper case made: the base64 still decodes, the DER does not.
        text = self.tree.pem.read_text()
        at = text.index(ca_store.BEGIN) + 40
        self.tree.pem.write_text(text[:at] + ("A" if text[at] != "A" else "B") + text[at + 1:])
        errors = self.errors()
        self.assertIn("does not match CA_SHA256", errors[0])
        self.assertIn("cacert.pem: bad DER length", errors[1])

    def test_a_truncated_store_fails(self):
        text = self.tree.pem.read_text()
        self.tree.pem.write_text(text[:text.rindex(ca_store.END)])
        self.tree.repin()
        self.assertTrue(any("truncated" in e for e in self.errors()), self.errors())

    def test_the_bridge_back_on_the_default_bundle_fails(self):
        self.tree.sdkconfig.write_text("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y\n"
                                       "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y\n"
                                       "# CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE is not set\n"
                                       "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y\n")
        errors = self.errors()
        self.assertEqual(len(errors), 2)
        self.assertIn("does not name software/wifi/ca_store/cacert.pem", errors[0])
        self.assertIn("DEFAULT_NONE", errors[1])

    def test_the_bridge_adding_the_default_bundle_to_the_store_fails(self):
        self.tree.sdkconfig.write_text(self.tree.sdkconfig.read_text().replace(
            "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE=y",
            "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y"))
        self.assertIn("DEFAULT_NONE", self.errors()[0])

    def test_a_store_path_without_the_custom_bundle_switch_fails(self):
        self.tree.sdkconfig.write_text(self.tree.sdkconfig.read_text().replace(
            "CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE=y\n",
            "# CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE is not set\n"))
        self.assertIn("does not set CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE=y", self.errors()[0])

    def test_a_path_to_another_file_or_to_the_directory_fails(self):
        for path in ("../ca_store", "../ca_store/other.pem", "cacert.pem"):
            self.tree.sdkconfig.write_text(SDKCONFIG % (path, 200))
            self.assertIn("does not name software/wifi/ca_store/cacert.pem", self.errors()[0], path)

    def test_the_bundle_off_fails(self):
        self.tree.sdkconfig.write_text("# CONFIG_MBEDTLS_CERTIFICATE_BUNDLE is not set\n")
        self.assertIn("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE is off", self.errors()[0])

    def test_without_cross_signed_verification_fails(self):
        self.tree.sdkconfig.write_text(self.tree.sdkconfig.read_text().replace(
            "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y",
            "# CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY is not set"))
        self.assertIn("needs CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y",
                      self.errors()[0])

    def test_a_project_outside_projects_is_left_alone(self):
        # u64ctrl takes ESP-IDF's own bundle; that is no error, and nothing reads it.
        self.assertEqual(self.errors(), [])
        self.assertEqual([p for p, _ in ca_store.projects(self.tree.root)], [self.tree.bridge])

    def test_a_project_named_but_missing_is_an_error_not_a_crash(self):
        self.tree.sdkconfig.unlink()
        with self.assertRaisesRegex(ca_store.StoreError, "no sdkconfig"):
            self.errors()


class EffectiveConfigTest(unittest.TestCase):
    """What the build actually uses: its sdkconfig.json (the build passes it), and RELEASE from the
    environment of every build, not only of the configure step."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.tree = Tree(self.tmp)
        self.json = pathlib.Path(self.tmp) / "sdkconfig.json"

    def write_json(self, **changes):
        config = {"MBEDTLS_CERTIFICATE_BUNDLE": True, "MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL": False,
                  "MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN": False,
                  "MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE": True,
                  "MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE": True,
                  "MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH": "../ca_store/cacert.pem",
                  "MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS": 200,
                  "MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY": True}
        config.update(changes)
        self.json.write_text(__import__("json").dumps(config))

    def errors(self, **kwargs):
        return ca_store.check(self.tree.root, today=datetime.date(2026, 9, 29), now=NOW,
                              effective={self.tree.bridge: self.json}, **kwargs)[0]

    def test_the_effective_config_passes_when_it_agrees(self):
        self.write_json()
        self.assertEqual(self.errors(), [])

    def test_an_effective_config_that_takes_the_default_bundle_fails(self):
        # idf.py -D SDKCONFIG=alt with DEFAULT_CMN: the project's own sdkconfig still says NONE.
        self.write_json(MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN=True,
                        MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE=False)
        self.assertIn("DEFAULT_NONE", self.errors()[0])

    def test_the_effective_limit_and_cross_signed_setting_count(self):
        self.write_json(MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS=19,
                        MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=False)
        errors = self.errors()
        self.assertTrue(any("CROSS_SIGNED_VERIFY=y" in e for e in errors), errors)
        self.assertTrue(any("MAX_CERTS=19, the store has 20" in e for e in errors), errors)

    def test_the_command_line_takes_the_effective_config(self):
        self.write_json(MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE=False,
                        MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=True)
        arguments = ["check", "--repo-root", str(self.tree.root), "--today", "2026-09-29"]
        with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(ca_store.main(arguments), 0)
            self.assertEqual(ca_store.main([*arguments, "--project", str(self.tree.bridge),
                                            "--sdkconfig-json", str(self.json)]), 1)

    def test_release_in_the_environment_fails_a_stale_store(self):
        arguments = ["check", "--repo-root", str(self.tree.root), "--today", "2027-06-01"]
        for value, status in (("", 0), ("0", 0), ("1", 1), ("yes", 1)):
            with mock.patch.dict(os.environ, {"RELEASE": value}), \
                    contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(ca_store.main(arguments), status, value)


class StoreDirTest(unittest.TestCase):
    """Only cacert.pem holds certificates in the store."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.tree = Tree(self.tmp)

    def errors(self):
        return ca_store.check(self.tree.root, today=datetime.date(2026, 9, 29), now=NOW)[0]

    def test_an_anchors_file_is_refused_as_retired(self):
        (self.tree.pem.parent / ca_store.ANCHORS_NAME).write_text(pem_block(
            ca_store.certificates(self.tree.pem.read_text())[0]))
        errors = self.errors()
        self.assertEqual(len(errors), 1)
        self.assertIn("anchors.pem: the anchors are retired on ESP-IDF 6", errors[0])

    def test_a_stray_certificate_file_is_refused(self):
        for name in ("extra.pem", "extra.der", "extra.crt"):
            (self.tree.pem.parent / name).write_text("x")
            self.assertIn("%s: only cacert.pem holds certificates" % name, self.errors()[0])
            (self.tree.pem.parent / name).unlink()

    def test_other_files_are_allowed(self):
        (self.tree.pem.parent / "README.md").write_text("notes\n")
        self.assertEqual(self.errors(), [])


class BundleSetTest(unittest.TestCase):
    """CA_BUNDLE_SET_SHA256 and the IDENT rule it drives."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.tree = Tree(self.tmp)

    def errors(self):
        return ca_store.check(self.tree.root, today=datetime.date(2026, 9, 29), now=NOW)[0]

    def test_a_certificate_change_without_the_set_pin_fails_and_names_what_to_bump(self):
        # A hand edit that moved CA_SHA256 along with the file, but not the set pin.
        text = self.tree.pem.read_text()
        self.tree.pem.write_text(text[:text.rindex("\n\n") + 1])
        self.tree.repin()
        errors = self.errors()
        self.assertEqual(len(errors), 1)
        self.assertIn("CA_BUNDLE_SET_SHA256 did not", errors[0])
        self.assertIn("IDENT_MINOR of software/wifi/raw_u64", errors[0])

    def test_a_comment_only_edit_changes_no_certificate(self):
        self.tree.pem.write_text("## another comment\n" + self.tree.pem.read_text())
        self.tree.repin()
        self.assertEqual(self.errors(), [])

    def test_the_set_does_not_depend_on_order(self):
        ders = ca_store.certificates(COMMITTED)[:5]
        self.assertEqual(ca_store.bundle_set_sha256(ders),
                         ca_store.bundle_set_sha256(list(reversed(ders))))
        self.assertNotEqual(ca_store.bundle_set_sha256(ders), ca_store.bundle_set_sha256(ders[1:]))

    def test_attaching_reads_only_the_projects_own_sources(self):
        self.assertTrue(ca_store.attaching(self.tree.bridge))
        self.assertFalse(ca_store.attaching(self.tree.root / "software/u64ctrl"))

    def test_two_roots_with_one_subject_fail(self):
        store = ca_store.certificates(self.tree.pem.read_text())
        double = with_key(store[0], ca_store.subject_and_key(store[1])[1])
        self.tree.pem.write_text(self.tree.pem.read_text() + "\nDouble\n======\n"
                                 + pem_block(double))
        self.tree.repin()
        self.assertTrue(any("same subject" in e for e in self.errors()), self.errors())

    def test_bump_ident_refuses_a_header_whose_string_disagrees(self):
        header = self.tree.bridge / "main/rpc_dispatch.h"
        header.write_text(HEADER.replace("V1.%d", "V1.5") % (6, 6))
        with self.assertRaisesRegex(ca_store.StoreError, "IDENT_STRING says 1.5"):
            ca_store.bump_ident(self.tree.bridge, "2026-09-29", 7)


class CommittedStoreTest(unittest.TestCase):
    """The tree as committed."""

    def test_the_committed_store_is_consistent(self):
        errors, _ = ca_store.check(REPO_ROOT)
        self.assertEqual(errors, [])

    def test_the_committed_store_holds_no_anchors(self):
        store = REPO_ROOT / ca_store.STORE_DIR
        self.assertEqual(sorted(p.name for p in store.iterdir() if p.suffix == ".pem"),
                         [ca_store.PEM_NAME])

    def test_the_committed_pins_parse_like_pins_and_as_shell(self):
        path = REPO_ROOT / ca_store.STORE_DIR / ca_store.PINS_NAME
        pins = ca_store.read_pins(path)
        for value in pins.values():
            self.assertNotRegex(value, "[\"' ]")
        shell = subprocess.run(
            ["sh", "-c", '. "$1" && echo "$CA_MOZILLA_DATE $CA_MAX_AGE_DAYS"', "sh", str(path)],
            capture_output=True, text=True, check=True)
        self.assertEqual(shell.stdout.split(),
                         [pins["CA_MOZILLA_DATE"], pins["CA_MAX_AGE_DAYS"]])

    def test_the_bridge_builds_its_bundle_from_the_store_and_attaches_it(self):
        found = [(str(p.relative_to(REPO_ROOT)), ca_store.attaching(p))
                 for p, _ in ca_store.projects(REPO_ROOT)]
        self.assertEqual(found, [(BRIDGE, True)])

    def test_every_project_that_uses_the_store_has_a_version_to_bump(self):
        for project, _ in ca_store.projects(REPO_ROOT):
            _, text, version = ca_store.bump_ident(project, "2026-09-25", 999)
            self.assertIn('%s"' % version, text)

    def test_the_bridge_cmake_runs_the_check_at_configure_and_at_every_build(self):
        cmake = (REPO_ROOT / BRIDGE / "CMakeLists.txt").read_text()
        self.assertIn("tools/ca_store/ca_store.py", cmake)
        self.assertIn("FATAL_ERROR", cmake)
        self.assertIn("--sdkconfig-json", cmake)
        self.assertIn("add_custom_target(ca_store_check ALL", cmake)
        self.assertIn("add_dependencies(custom_bundle ca_store_check)", cmake)


if __name__ == "__main__":
    unittest.main()
