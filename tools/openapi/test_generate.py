#!/usr/bin/env python3
"""Tests for the command line entry point and for the documents of the real firmware."""

import io
import pathlib
import re
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import document
import fixture
import generate
import routes
import schemas

try:
    import yaml
except ImportError:
    yaml = None


class CommandTest(unittest.TestCase):
    def test_generate_writes_one_document_per_product(self):
        with fixture.repository() as root:
            written = generate.generate(root)
            self.assertEqual(
                sorted(path.name for path in written),
                ["rest_api_openapi_large.yaml", "rest_api_openapi_small.yaml"],
            )
            self.assertTrue(written[0].read_text().startswith("# yaml-language-server:"))

    def test_check_passes_directly_after_generate(self):
        with fixture.repository() as root:
            generate.generate(root)
            self.assertEqual(generate.stale(root), [])

    def test_check_reports_a_document_that_was_edited_by_hand(self):
        with fixture.repository() as root:
            generate.generate(root)
            edited = generate.output_path("small", root)
            edited.write_text(edited.read_text().replace("List demos", "Something else"))
            self.assertEqual(generate.stale(root), ["doc/api/rest_api_openapi_small.yaml is out of date"])

    def test_check_reports_a_document_that_is_not_there(self):
        with fixture.repository() as root:
            generate.generate(root)
            generate.output_path("large", root).unlink()
            self.assertEqual(generate.stale(root), ["doc/api/rest_api_openapi_large.yaml is missing"])

    def test_generating_twice_produces_the_same_bytes(self):
        with fixture.repository() as root:
            generate.generate(root)
            first = generate.output_path("small", root).read_text()
            generate.generate(root)
            self.assertEqual(generate.output_path("small", root).read_text(), first)

    def test_the_exit_code_follows_the_check(self):
        with fixture.repository() as root, mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(generate.main(["generate", "--repo-root", str(root)]), 0)
            with mock.patch("sys.stderr", io.StringIO()):
                self.assertEqual(generate.main(["check", "--repo-root", str(root)]), 0)
                generate.output_path("small", root).unlink()
                self.assertEqual(generate.main(["check", "--repo-root", str(root)]), 1)

    def test_a_source_the_generator_rejects_becomes_a_non_zero_exit(self):
        broken = fixture.DEMO_SOURCE.replace('SUMMARY("List demos")', "")
        with fixture.repository(sources={"route_demo.cc": broken}) as root:
            with mock.patch("sys.stderr", io.StringIO()) as errors:
                self.assertEqual(generate.main(["generate", "--repo-root", str(root)]), 1)
            self.assertIn("expected exactly one SUMMARY", errors.getvalue())


class CommittedDocumentTest(unittest.TestCase):
    def test_the_committed_documents_match_the_firmware_sources(self):
        self.assertEqual(generate.stale(), [])


class FirmwareTest(unittest.TestCase):
    """The real tree. These fail when the firmware and the documents disagree.

    Built in memory rather than parsed back from the committed files, so they run
    where PyYAML is not installed, which includes the firmware CI container.
    """

    @classmethod
    def setUpClass(cls):
        cls.documents = document.build_all(generate.REPO_ROOT)

    def paths_of(self, profile):
        return set(self.documents[profile]["paths"])

    def test_every_registered_call_appears_in_its_own_document(self):
        for profile, definition in schemas.PROFILES.items():
            document = self.documents[profile]
            for call, _ in routes.documented_calls(definition, generate.REPO_ROOT).values():
                served = [
                    template
                    for template, item in document["paths"].items()
                    if template.startswith("/v1/" + call.route) and call.method.lower() in item
                ]
                self.assertTrue(served, "%s is missing from the %s document" % (call, profile))

    def test_the_debug_register_belongs_to_the_ultimate_64_only(self):
        self.assertIn("/v1/machine:debugreg", self.paths_of("u64"))
        self.assertNotIn("/v1/machine:debugreg", self.paths_of("u2"))

    def test_the_streams_route_belongs_to_the_ultimate_64_only(self):
        self.assertTrue(any(p.startswith("/v1/streams/") for p in self.paths_of("u64")))
        self.assertFalse(any(p.startswith("/v1/streams/") for p in self.paths_of("u2")))

    def test_a_call_that_can_only_refuse_declares_no_success(self):
        """Pinned, because getting this wrong is invisible until a client is generated.

        A call whose handler compiles down to a refusal on one product must not
        promise a success there. Adding a product conditional to a handler
        without splitting its API_DOC block the same way lands here.
        """
        expected = {
            "u2": {
                ("get", "/v1/machine:input"),
                ("post", "/v1/machine:input"),
                ("put", "/v1/machine:poweroff"),
            },
            "u64": set(),
        }
        for profile, document in self.documents.items():
            refusing = {
                (method, template)
                for template, item in document["paths"].items()
                for method, operation in item.items()
                if not any(code.startswith("2") for code in operation["responses"])
            }
            self.assertEqual(refusing, expected[profile], profile)

    def test_the_only_deprecated_call_is_the_one_that_was_never_implemented(self):
        for profile, document in self.documents.items():
            deprecated = {
                "%s %s" % (method.upper(), template)
                for template, item in document["paths"].items()
                for method, operation in item.items()
                if operation.get("deprecated")
            }
            self.assertEqual(deprecated, {"GET /v1/help"}, profile)

    def test_every_caution_names_hints_from_the_vocabulary(self):
        seen = set()
        for profile, document in self.documents.items():
            for template, item in document["paths"].items():
                for method, operation in item.items():
                    caution = operation.get(schemas.CAUTION_FIELD)
                    if not caution:
                        continue
                    where = "%s %s in %s" % (method, template, profile)
                    self.assertTrue(caution["note"], where)
                    for hint in caution["hints"]:
                        self.assertIn(hint, schemas.CAUTION_HINTS, where)
                        seen.add(hint)
        self.assertEqual(seen, set(schemas.CAUTION_HINTS),
                         "a hint in the vocabulary that nothing uses")

    def test_powering_the_machine_off_is_flagged_on_the_product_that_can(self):
        def hints(profile):
            operation = self.documents[profile]["paths"]["/v1/machine:poweroff"]["put"]
            return operation.get(schemas.CAUTION_FIELD, {}).get("hints", [])
        self.assertEqual(hints("u64"), ["power"])
        self.assertEqual(hints("u2"), [])

    def test_calls_the_hand_written_specifications_missed_are_present(self):
        for path in ("/v1/help", "/v1/machine:measure", "/v1/machine:heap",
                     "/v1/drives/{drive}:unlink"):
            self.assertIn(path, self.paths_of("u2"))
            self.assertIn(path, self.paths_of("u64"))

    def test_operation_ids_are_unique_within_a_document(self):
        for profile, document in self.documents.items():
            identifiers = [
                operation["operationId"]
                for item in document["paths"].values()
                for operation in item.values()
            ]
            self.assertEqual(len(identifiers), len(set(identifiers)), profile)

    def test_every_operation_carries_prose_and_at_least_one_response(self):
        for profile, document in self.documents.items():
            for template, item in document["paths"].items():
                for method, operation in item.items():
                    where = "%s %s in %s" % (method, template, profile)
                    self.assertTrue(operation["summary"], where)
                    self.assertTrue(operation["description"], where)
                    self.assertTrue(operation["responses"], where)

    def test_a_path_parameter_is_declared_for_every_placeholder(self):
        for profile, document in self.documents.items():
            for template, item in document["paths"].items():
                for method, operation in item.items():
                    declared = {
                        parameter["name"]
                        for parameter in operation.get("parameters", [])
                        if parameter["in"] == "path"
                    }
                    expected = set(re.findall(r"\{(\w+)\}", template))
                    self.assertEqual(declared, expected, "%s %s in %s" % (method, template, profile))


if __name__ == "__main__":
    unittest.main()
