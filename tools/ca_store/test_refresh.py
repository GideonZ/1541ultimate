#!/usr/bin/env python3
"""Tests for refresh.py. Need no network: the store is served from file:// URLs built from
the committed cacert.pem, and GitHub (the regeneration from certdata.txt and the revision
checks) is stood in for. CA_STORE_ONLINE=1 adds tests that run the real ones."""

import contextlib
import datetime
import hashlib
import io
import json
import os
import pathlib
import shutil
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import ca_store
import refresh
from test_ca_store import (BRIDGE, COMMITTED, HEADER, NEW_DATE, NEW_ISO, NEW_REF, NOW, OLD_DATE,
                           OLD_ISO, OLD_REF, REPO_ROOT, Tree, der_length, pem, pem_block, rekeyed)

REF_DATE = "2026-09-08T20:22:24Z"


def agree(text, now):
    """A regeneration that comes out the same as curl's file."""
    return NEW_REF, text.encode(), REF_DATE


def with_subject(der, subject):
    """der with its subject replaced (the signature no longer matches; nothing here checks it)."""
    _, start, _ = ca_store._tlv(der, 0)
    _, tbs_start, tbs_end = ca_store._tlv(der, start)
    fields, pos = [], tbs_start
    while pos < tbs_end:
        _, _, end = ca_store._tlv(der, pos)
        fields.append(der[pos:end])
        pos = end
    fields[5 if fields[0][0] == 0xA0 else 4] = subject
    tbs = b"".join(fields)
    tbs = bytes([0x30]) + der_length(len(tbs)) + tbs
    body = tbs + der[tbs_end:]
    return bytes([0x30]) + der_length(len(body)) + body


class Served:
    """A cacert.pem and its .sha256 as file:// URLs."""

    def __init__(self, directory, text, sha256=None):
        self.path = pathlib.Path(directory) / "cacert.pem"
        self.path.write_bytes(text.encode())
        digest = sha256 or hashlib.sha256(text.encode()).hexdigest()
        pathlib.Path(str(self.path) + ".sha256").write_text("%s  cacert.pem\n" % digest)
        self.url = self.path.as_uri()


class RefreshTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.tree = Tree(pathlib.Path(self.tmp) / "repo")
        self.serve_dir = pathlib.Path(self.tmp) / "served"
        self.serve_dir.mkdir()

    def refresh(self, text, sha256=None, **kwargs):
        kwargs.setdefault("regenerate", agree)
        kwargs.setdefault("descends", lambda old, new: True)
        kwargs.setdefault("now", NOW)
        kwargs.setdefault("ident_minor", 7)     # the bridge in Tree is 1.6
        return refresh.refresh(self.tree.root, Served(self.serve_dir, text, sha256).url, **kwargs)

    def test_a_new_date_writes_the_store_the_pins_and_bumps_the_bridge(self):
        new = pem(NEW_DATE, first=5, count=30)
        summary = pathlib.Path(self.tmp) / "summary.md"

        written = self.refresh(new, summary=summary)

        self.assertEqual(self.tree.pem.read_text(), new)
        pins = ca_store.read_pins(self.tree.pins)
        self.assertEqual(pins["CA_MOZILLA_DATE"], NEW_ISO)
        self.assertEqual(pins["CA_SHA256"], hashlib.sha256(new.encode()).hexdigest())
        self.assertEqual(pins["CA_SOURCE_URL"], "https://curl.se/ca/cacert.pem")
        self.assertEqual(pins["CA_CERTDATA_REF"], NEW_REF)
        self.assertIn("# comment kept\n", self.tree.pins.read_text())
        self.assertIn("\nCA_MOZILLA_DATE=%s\n" % NEW_ISO, self.tree.pins.read_text())
        bridge = (self.tree.bridge / "main/rpc_dispatch.h").read_text()
        self.assertEqual(bridge, '#define IDENT_STRING "ESP32 WiFi Bridge V1.7"\n'
                                "#define IDENT_MAJOR   1\n"
                                "// 1.7: the Mozilla root store as of 2026-09-25.\n"
                                "// 1.6: something earlier.\n"
                                "#define IDENT_MINOR   7\n")
        u64ctrl = (self.tree.root / "software/u64ctrl/main/rpc_dispatch.h").read_text()
        self.assertEqual(u64ctrl, HEADER % (14, 14, 14))
        self.assertEqual(len(written), 3)
        self.assertEqual(pins["CA_BUNDLE_SET_SHA256"],
                         ca_store.bundle_set_sha256(ca_store.certificates(new)))
        report = summary.read_text()
        self.assertIn("as of %s (was %s), 30 certificates" % (NEW_ISO, OLD_ISO), report)
        old_fps = set(ca_store.roots(pem(OLD_DATE)))
        new_fps = set(ca_store.roots(new))
        for fp in new_fps - old_fps:
            self.assertIn("- `%s` " % fp, report.split("## Added")[1].split("## Removed")[0])
        for fp in old_fps - new_fps:
            self.assertIn("- `%s` " % fp, report.split("## Removed")[1])
        self.assertEqual(len(new_fps - old_fps), 15)
        self.assertEqual(len(old_fps - new_fps), 5)
        self.assertNotIn("SAME SUBJECT", report)
        self.assertIn("`%s` (committed %s)" % (NEW_REF, REF_DATE), report)
        self.assertEqual(ca_store.check(self.tree.root, today=datetime.date(2026, 9, 29)), ([], []))

    def test_files_are_replaced_not_rewritten_in_place(self):
        # A build that reads the store meanwhile sees the old file or the new one, never half.
        before = {p: p.stat().st_ino for p in (self.tree.pem, self.tree.pins)}
        self.refresh(pem(NEW_DATE, first=5, count=30))
        for path, inode in before.items():
            self.assertNotEqual(path.stat().st_ino, inode, path.name)
        self.assertEqual(sorted(p.name for p in self.tree.pem.parent.iterdir()),
                         ["CA_PINS", "cacert.pem"])

    def test_a_second_run_with_the_same_data_changes_nothing(self):
        new = pem(NEW_DATE, first=5, count=30)
        self.refresh(new)
        before = self.tree.snapshot()

        self.assertEqual(self.refresh(new), [])
        self.assertEqual(self.tree.snapshot(), before)

    def test_the_same_date_changes_nothing(self):
        before = self.tree.snapshot()

        self.assertEqual(self.refresh(pem(OLD_DATE)), [])
        self.assertEqual(self.tree.snapshot(), before)

    def test_the_same_date_with_only_a_different_header_changes_nothing(self):
        before = self.tree.snapshot()

        self.assertEqual(self.refresh(pem(OLD_DATE).replace("version 1.33", "version 1.34")), [])
        self.assertEqual(self.tree.snapshot(), before)

    def assertRefused(self, message, text, sha256=None):
        before = self.tree.snapshot()
        with self.assertRaisesRegex(ca_store.StoreError, message):
            self.refresh(text, sha256)
        self.assertEqual(self.tree.snapshot(), before)

    def test_a_wrong_sha256_writes_nothing(self):
        self.assertRefused("sha256 mismatch", pem(NEW_DATE), sha256="0" * 64)

    def test_a_sum_file_that_is_not_a_sum_writes_nothing(self):
        self.assertRefused("not a sha256 sum", pem(NEW_DATE), sha256="<html>")

    def test_a_truncated_download_fails_its_sum(self):
        new = pem(NEW_DATE)
        good_sum = hashlib.sha256(new.encode()).hexdigest()
        self.assertRefused("sha256 mismatch", new[: len(new) // 2], sha256=good_sum)

    def test_a_truncated_file_is_refused_even_when_its_sum_matches(self):
        new = pem(NEW_DATE)
        self.assertRefused("truncated", new[: new.rindex(ca_store.END)])
        self.assertRefused("truncated", new[: new.rindex(ca_store.END) + 10])
        self.assertRefused("truncated", new[:-1])

    def test_a_certificate_that_is_not_der_is_refused(self):
        new = pem(NEW_DATE).replace("\nMII", "\nXII", 1)
        self.assertRefused("not DER", new)

    def test_a_file_without_the_mozilla_date_is_refused(self):
        self.assertRefused("Certificate data from Mozilla",
                           pem(NEW_DATE).replace("as of: ", "at: "))

    def test_an_older_store_is_refused(self):
        self.assertRefused("older than the committed store", pem("Mon Mar 11 15:25:27 2024 GMT"))

    def test_different_certificates_under_the_same_date_are_refused(self):
        self.assertRefused("certificates changed but the Mozilla date did not",
                           pem(OLD_DATE, first=1))

    def test_more_certificates_than_the_bundle_allows_are_refused(self):
        shutil.rmtree(self.tree.root)
        self.tree = Tree(self.tree.root, max_certs=10)
        self.assertRefused("11 certificates, but .* allows 10", pem(NEW_DATE, count=11))

    def test_a_project_whose_version_does_not_parse_writes_nothing(self):
        (self.tree.bridge / "main/rpc_dispatch.h").write_text(
            HEADER.replace("V1.%d", "V2.%d") % (6, 6, 6))
        self.assertRefused("IDENT_STRING says 2.6", pem(NEW_DATE, first=1))

    def test_a_network_failure_writes_nothing(self):
        before = self.tree.snapshot()
        for url in ((self.serve_dir / "missing.pem").as_uri(), "http://127.0.0.1:9/cacert.pem"):
            with mock.patch.object(refresh, "FETCH_TIMEOUT", 5):
                with self.assertRaisesRegex(ca_store.StoreError, "cannot fetch"):
                    refresh.refresh(self.tree.root, url)
        self.assertEqual(self.tree.snapshot(), before)

    def test_the_command_line_reports_a_failure_with_a_nonzero_status(self):
        served = Served(self.serve_dir, pem(NEW_DATE), sha256="0" * 64)
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            status = refresh.main(["--repo-root", str(self.tree.root),
                                    "--source", served.url])
        self.assertEqual(status, 1)
        self.assertIn("sha256 mismatch", stderr.getvalue())


    def test_a_version_given_for_the_project_by_name_bumps_it(self):
        self.refresh(pem(NEW_DATE, first=5, count=30), ident_minor={BRIDGE: 9})
        self.assertIn("#define IDENT_MINOR   9", (self.tree.bridge / "main/rpc_dispatch.h").read_text())

    def test_a_version_for_another_project_is_not_one_for_the_bridge(self):
        before = self.tree.snapshot()
        with self.assertRaisesRegex(ca_store.StoreError, "software/wifi/raw_u64 needs a new IDENT_MINOR"):
            self.refresh(pem(NEW_DATE, first=5, count=30), ident_minor={"software/u64ctrl": 9})
        self.assertEqual(self.tree.snapshot(), before)

    def test_the_command_line_takes_one_number_or_one_per_project(self):
        self.assertIsNone(refresh.parse_ident_minors(None))
        self.assertEqual(refresh.parse_ident_minors(["7"]), 7)
        self.assertEqual(refresh.parse_ident_minors(["a/b=7", "c=8"]), {"a/b": 7, "c": 8})
        for bad in (["7", "8"], ["a=x"], ["7", "c=8"]):
            with self.assertRaises(ca_store.StoreError):
                refresh.parse_ident_minors(bad)

    def test_changed_certificates_without_an_allocated_version_write_nothing(self):
        # The next minor is not necessarily free on a development line.
        before = self.tree.snapshot()
        with self.assertRaisesRegex(ca_store.StoreError, "--ident-minor"):
            self.refresh(pem(NEW_DATE, first=5, count=30), ident_minor=None)
        self.assertEqual(self.tree.snapshot(), before)

    def test_a_version_that_does_not_move_forward_is_refused(self):
        for minor in (6, 5):
            self.assertRefusedWith("IDENT_MINOR %d is not above 6" % minor, pem(NEW_DATE, first=5, count=30),
                                   agree, ident_minor=minor)

    def test_the_allocated_version_is_the_one_written(self):
        self.refresh(pem(NEW_DATE, first=5, count=30), ident_minor=309)
        header = (self.tree.bridge / "main/rpc_dispatch.h").read_text()
        self.assertIn('#define IDENT_STRING "ESP32 WiFi Bridge V1.309"', header)
        self.assertIn("#define IDENT_MINOR   309\n", header)
        self.assertIn("// 1.309: the Mozilla root store as of 2026-09-25.", header)

    def test_the_same_certificates_need_no_version(self):
        self.assertEqual(len(self.refresh(pem(NEW_DATE), ident_minor=None)), 2)

    def test_the_command_line_passes_the_version(self):
        served = Served(self.serve_dir, pem(NEW_DATE, first=5, count=30))
        real = refresh.refresh

        def offline(*args, **kwargs):
            return real(*args, regenerate=agree, descends=lambda old, new: True, now=NOW, **kwargs)
        with mock.patch.object(refresh, "refresh", offline), \
                contextlib.redirect_stderr(io.StringIO()) as err, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(refresh.main(["--repo-root", str(self.tree.root), "--source", served.url]), 1)
            self.assertIn("--ident-minor", err.getvalue())
            self.assertEqual(refresh.main(["--repo-root", str(self.tree.root), "--source", served.url,
                                           "--ident-minor", "8"]), 0)
        self.assertIn("V1.8", (self.tree.bridge / "main/rpc_dispatch.h").read_text())

    def test_a_new_date_with_the_same_certificates_bumps_nothing(self):
        new = pem(NEW_DATE)
        summary = pathlib.Path(self.tmp) / "summary.md"
        headers = {p: p.read_text() for p in self.tree.root.rglob("rpc_dispatch.h")}

        written = self.refresh(new, summary=summary)

        self.assertEqual(written, [self.tree.pem, self.tree.pins])
        self.assertEqual({p: p.read_text() for p in headers}, headers)
        self.assertEqual(ca_store.read_pins(self.tree.pins)["CA_MOZILLA_DATE"], NEW_ISO)
        self.assertIn("did not change", summary.read_text())

    def test_a_file_that_mozillas_data_does_not_give_is_refused(self):
        # A mirror serving its own key under ISRG Root X1's name, with a matching sum: the
        # regeneration from certdata.txt still gives the real one.
        honest = pem(NEW_DATE)
        forged = rekeyed(honest, 3, 7)
        self.assertNotEqual(forged, honest)
        before = self.tree.snapshot()
        with self.assertRaisesRegex(ca_store.StoreError, "is not what .* gives"):
            self.refresh(forged, regenerate=lambda text, now: (NEW_REF, honest.encode(), REF_DATE))
        self.assertEqual(self.tree.snapshot(), before)

    def test_a_different_header_alone_is_refused_too(self):
        honest = pem(NEW_DATE)
        with self.assertRaisesRegex(ca_store.StoreError, "is not what"):
            self.refresh(honest.replace("version 1.33", "version 1.34"),
                         regenerate=lambda text, now: (NEW_REF, honest.encode(), REF_DATE))

    def test_a_key_change_under_the_same_subject_gets_its_own_section(self):
        # When Mozilla itself makes the change, both origins agree and the store is written,
        # but the description puts it apart from ordinary additions and removals.
        new = rekeyed(pem(NEW_DATE), 3, 7)
        summary = pathlib.Path(self.tmp) / "summary.md"

        self.refresh(new, summary=summary)

        report = summary.read_text()
        section = report.split("## SAME SUBJECT, DIFFERENT CERTIFICATE")[1].split("## Added")[0]
        old_fp = hashlib.sha256(ca_store.certificates(pem(OLD_DATE))[3]).hexdigest()
        new_fp = hashlib.sha256(ca_store.certificates(new)[3]).hexdigest()
        self.assertIn("`%s` became `%s`" % (old_fp, new_fp), section)
        self.assertEqual(report.split("## Added")[1].split("## Removed")[0].strip(), "None.")
        self.assertIn("V1.7", (self.tree.bridge / "main/rpc_dispatch.h").read_text())

    def test_labels_cannot_inject_markup_into_the_description(self):
        new = pem(NEW_DATE, first=5, count=30)
        label = ca_store.names(new)[-1]
        new = new.replace("\n%s\n" % label, "\n[x](https://evil)<b>`\n", 1)
        summary = pathlib.Path(self.tmp) / "summary.md"
        self.refresh(new, summary=summary)
        self.assertNotIn("](https", summary.read_text())
        self.assertNotIn("<b>", summary.read_text())

    def test_a_date_in_the_future_is_refused(self):
        self.assertRefused("in the future", pem("Thu Oct 01 12:00:01 2026 GMT"))

    def test_a_date_within_a_day_of_the_clock_is_accepted(self):
        self.refresh(pem("Wed Sep 30 11:59:59 2026 GMT"))
        self.assertEqual(ca_store.read_pins(self.tree.pins)["CA_MOZILLA_DATE"],
                         "2026-09-30T11:59:59Z")

    def test_an_earlier_time_on_the_same_day_is_a_rollback(self):
        self.assertRefused("older than the committed store", pem("Tue Jul 14 03:12:00 2026 GMT"))

    def test_exactly_as_many_certificates_as_the_bundle_allows_is_accepted(self):
        shutil.rmtree(self.tree.root)
        self.tree = Tree(self.tree.root, max_certs=10)
        self.assertTrue(self.refresh(pem(NEW_DATE, count=10)))

    def test_a_failing_regeneration_writes_nothing(self):
        def broken(text, now):
            raise ca_store.StoreError("cannot fetch certdata.txt")
        self.assertRefusedWith("cannot fetch certdata", pem(NEW_DATE), broken)

    def assertRefusedWith(self, message, text, regenerate, **kwargs):
        before = self.tree.snapshot()
        with self.assertRaisesRegex(ca_store.StoreError, message):
            self.refresh(text, regenerate=regenerate, **kwargs)
        self.assertEqual(self.tree.snapshot(), before)

    def test_an_older_certdata_revision_is_refused(self):
        # curl.se alone serving an honest conversion of an older certdata.txt under a new date.
        seen = []

        def older(old, new):
            seen.append((old, new))
            return False
        before = self.tree.snapshot()
        with self.assertRaisesRegex(ca_store.StoreError, "not the committed .* or a later commit"):
            self.refresh(pem(NEW_DATE, first=5, count=30), descends=older)
        self.assertEqual(seen, [(OLD_REF, NEW_REF)])
        self.assertEqual(self.tree.snapshot(), before)

    def test_no_module_attaching_the_bundle_means_no_bump(self):
        (self.tree.shared / "tls_engine.c").unlink()
        summary = pathlib.Path(self.tmp) / "summary.md"
        written = self.refresh(pem(NEW_DATE, first=5, count=30), summary=summary)
        self.assertEqual(written, [self.tree.pem, self.tree.pins])
        self.assertIn("No project attaches the bundle", summary.read_text())

    def test_a_look_alike_added_beside_the_real_root_gets_its_own_section(self):
        # The same name spelled with UTF8String where the real root uses PrintableString is a
        # different subject, so it is neither the SAME SUBJECT case nor visible in a plain list.
        base = pem(NEW_DATE)
        victim = ca_store.certificates(base)[0]
        label = ca_store.names(base)[0]
        subject, _ = ca_store.subject_and_key(victim)
        look = subject.replace(b"\x13", b"\x0c")      # PrintableString -> UTF8String
        self.assertNotEqual(look, subject)
        forged = base.rstrip("\n") + "\n\n" + label + "\n" + "=" * len(label) + "\n" + \
            pem_block(with_subject(ca_store.certificates(base)[7], look))
        summary = pathlib.Path(self.tmp) / "summary.md"
        self.refresh(forged, summary=summary)
        section = summary.read_text().split("## ADDED UNDER THE NAME")[1].split("## Added")[0]
        self.assertIn(label, section)

    def test_a_look_alike_under_another_label_is_caught_by_its_name(self):
        base = pem(NEW_DATE)
        subject, _ = ca_store.subject_and_key(ca_store.certificates(base)[0])
        look = subject.replace(b"\x13", b"\x0c")
        forged = base.rstrip("\n") + "\n\nHarmless Root\n=============\n" + \
            pem_block(with_subject(ca_store.certificates(base)[7], look))
        summary = pathlib.Path(self.tmp) / "summary.md"
        self.refresh(forged, summary=summary)
        section = summary.read_text().split("## ADDED UNDER THE NAME")[1].split("## Added")[0]
        self.assertIn("Harmless Root", section)


class FindCertdataTest(unittest.TestCase):
    """The revision check against Firefox's release branch, with GitHub stood in for."""

    def serve(self, commits, blobs):
        listing = json.dumps([{"sha": sha, "commit": {"committer": {"date": date}}}
                              for sha, date in commits]).encode()

        def fetch(url, token=False):
            if "/commits?" in url:
                return listing
            return blobs[url.split("/")[5]]
        return mock.patch.object(refresh, "fetch", fetch)

    def test_the_newest_revision_is_taken(self):
        blob = b"certdata new"
        with self.serve([("a" * 40, "2026-09-08T20:22:24Z"), ("b" * 40, "2026-07-15T00:00:00Z")],
                        {"a" * 40: blob, "b" * 40: b"old"}):
            ref, data, date = refresh.find_certdata(hashlib.sha256(blob).hexdigest(), NOW)
        self.assertEqual((ref, data, date), ("mozilla-firefox/firefox@" + "a" * 40, blob,
                                             "2026-09-08T20:22:24Z"))

    def test_an_older_revision_is_taken_only_within_the_grace_period(self):
        old = b"certdata old"
        commits = [("a" * 40, "2026-09-20T00:00:00Z"), ("b" * 40, "2026-07-15T00:00:00Z")]
        blobs = {"a" * 40: b"new", "b" * 40: old}
        with self.serve(commits, blobs):
            ref, _, _ = refresh.find_certdata(hashlib.sha256(old).hexdigest(),
                                              datetime.datetime(2026, 10, 4))
        self.assertTrue(ref.endswith("b" * 40))
        with self.serve(commits, blobs):
            with self.assertRaisesRegex(ca_store.StoreError, "now carries a newer one"):
                refresh.find_certdata(hashlib.sha256(old).hexdigest(),
                                      datetime.datetime(2026, 10, 4, 0, 0, 1))

    def test_an_unknown_certdata_is_refused(self):
        with self.serve([("a" * 40, "2026-09-08T20:22:24Z")], {"a" * 40: b"x"}):
            with self.assertRaisesRegex(ca_store.StoreError, "none of the last 1 revisions"):
                refresh.find_certdata("0" * 64, NOW)

    def test_descent_is_asked_of_githubs_compare(self):
        answers = {"ahead": ({"status": "ahead", "behind_by": 0}, True),
                   "behind": ({"status": "behind", "behind_by": 3}, False),
                   "diverged": ({"status": "diverged", "behind_by": 1}, False)}
        for name, (answer, expected) in answers.items():
            with mock.patch.object(refresh, "fetch_json", lambda url: answer) as _:
                self.assertEqual(refresh.descends(OLD_REF, NEW_REF), expected, name)
        self.assertTrue(refresh.descends(OLD_REF, OLD_REF))
        with mock.patch.object(refresh, "fetch_json", lambda url: []):
            with self.assertRaisesRegex(ca_store.StoreError, "did not return an object"):
                refresh.descends(OLD_REF, NEW_REF)
        self.assertFalse(refresh.descends(OLD_REF, "someone/firefox@" + "2" * 40))


class OnlineTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("CA_STORE_ONLINE") == "1", "set CA_STORE_ONLINE=1")
    def test_the_committed_store_is_what_mozillas_certdata_gives(self):
        pins = ca_store.read_pins(REPO_ROOT / ca_store.STORE_DIR / ca_store.PINS_NAME)
        ref, again, _ = refresh.regenerate(COMMITTED, ca_store.utc_now())
        self.assertEqual(ref, pins["CA_CERTDATA_REF"])
        self.assertEqual(again, COMMITTED.encode())


    @unittest.skipUnless(os.environ.get("CA_STORE_ONLINE") == "1", "set CA_STORE_ONLINE=1")
    def test_the_committed_ref_descends_from_itself_and_not_from_a_later_one(self):
        pins = ca_store.read_pins(REPO_ROOT / ca_store.STORE_DIR / ca_store.PINS_NAME)
        older = "mozilla-firefox/firefox@4e9426ae522c67de47ff3e060774328b42df0134"
        self.assertTrue(refresh.descends(older, pins["CA_CERTDATA_REF"]))
        self.assertFalse(refresh.descends(pins["CA_CERTDATA_REF"], older))


if __name__ == "__main__":
    unittest.main()
