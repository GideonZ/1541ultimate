#!/usr/bin/env python3
"""Tests for what the API explorer page is allowed to load, and from where.

The page runs third-party code with the privileges of a page served by the device,
which include the device's own API and any password typed into Try it out. These
tests are what stops that bound being widened by accident: an unpinned version, a
missing digest, a policy that admits another origin or any inline script, all fail
the build rather than reaching a device.
"""

import pathlib
import re
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import explorer

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

# A version that is a range, such as @5 or @^5.1, resolves to whatever the newest
# matching release happens to be, which is not something a digest can pin.
_EXACT_VERSION = re.compile(r"@\d+\.\d+\.\d+/")


class PageTest(unittest.TestCase):
    def setUp(self):
        self.text = explorer.page_text(REPO_ROOT)
        self.references = explorer.external_references(self.text)
        self.policy = explorer.policy(self.text)

    def test_the_page_loads_something_from_another_origin(self):
        """Otherwise every test below would pass by describing nothing."""
        self.assertTrue(self.references)

    def test_every_external_url_names_an_exact_release(self):
        for kind, url, _ in self.references:
            self.assertRegex(url, _EXACT_VERSION, "%s %s is not pinned to one release" % (kind, url))

    def test_every_external_url_is_the_one_this_module_records(self):
        self.assertEqual(sorted(url for _, url, _ in self.references), sorted(explorer.SUBRESOURCES))

    def test_every_external_reference_carries_its_digest(self):
        for kind, url, attributes in self.references:
            self.assertEqual(
                attributes.get("integrity"), explorer.SUBRESOURCES[url],
                "%s %s does not carry the recorded digest" % (kind, url),
            )

    def test_every_external_reference_is_fetched_anonymously(self):
        """A cross-origin fetch without this is not checked against its digest."""
        for kind, url, attributes in self.references:
            self.assertEqual(attributes.get("crossorigin"), "anonymous", "%s %s" % (kind, url))

    def test_the_page_declares_a_content_security_policy(self):
        self.assertIsNotNone(self.policy)

    def test_the_policy_carries_the_directives_that_bound_the_page(self):
        for directive, sources in explorer.REQUIRED_POLICY.items():
            self.assertEqual(self.policy.get(directive), sources, directive)

    def test_the_policy_admits_no_script_origin_but_the_pinned_one(self):
        origins = [source for source in self.policy["script-src"] if not source.startswith("'")]
        self.assertEqual(origins, [explorer.ORIGIN])

    def test_the_policy_admits_the_page_own_inline_script_by_digest(self):
        scripts = explorer.inline_scripts(self.text)
        self.assertEqual(len(scripts), 1)
        self.assertIn("'%s'" % explorer.digest(scripts[0]), self.policy["script-src"])

    def test_the_policy_admits_no_other_inline_script(self):
        """'unsafe-inline' is ignored beside a digest, so its presence would be a trap."""
        self.assertNotIn("'unsafe-inline'", self.policy["script-src"])
        self.assertNotIn("'unsafe-eval'", self.policy["script-src"])

    def test_answers_never_leave_the_device(self):
        """The one directive that stops anything the page loads sending them on."""
        self.assertEqual(self.policy["connect-src"], ["'self'"])


class HelperTest(unittest.TestCase):
    """The reading these tests do, against pages written to fail."""

    def test_a_range_version_is_not_an_exact_release(self):
        self.assertNotRegex("https://unpkg.com/swagger-ui-dist@5/swagger-ui.css", _EXACT_VERSION)

    def test_a_reference_without_a_digest_is_seen_as_such(self):
        found = explorer.external_references('<script src="https://x.example/a.js"></script>')
        self.assertEqual(found, [("script", "https://x.example/a.js", {"src": "https://x.example/a.js"})])

    def test_a_relative_reference_is_not_external(self):
        self.assertEqual(explorer.external_references('<script src="local.js"></script>'), [])

    def test_the_digest_changes_with_the_script(self):
        self.assertNotEqual(explorer.digest("window.a = 1"), explorer.digest("window.a = 2"))

    def test_a_page_with_no_policy_reads_as_none(self):
        self.assertIsNone(explorer.policy("<html></html>"))


if __name__ == "__main__":
    unittest.main()
