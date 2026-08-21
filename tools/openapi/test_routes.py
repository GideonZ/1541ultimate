#!/usr/bin/env python3
"""Tests for reading the call table and the blocks that document it."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import fixture
import routes
from errors import OpenApiError

SMALL = fixture.PROFILES["small"]
LARGE = fixture.PROFILES["large"]


def documented(profile, **kwargs):
    with fixture.repository(**kwargs) as root:
        return routes.documented_calls(profile, root)


def refuses(test, profile, message, **kwargs):
    with test.assertRaises(OpenApiError) as raised:
        documented(profile, **kwargs)
    test.assertIn(message, str(raised.exception))


class CallTableTest(unittest.TestCase):
    def test_a_call_carries_its_verb_route_command_and_parameters(self):
        call, _ = documented(LARGE)[("PUT", "demo", "poke")]
        self.assertEqual(call.parameters, [("value", True)])
        self.assertFalse(call.takes_body)
        self.assertEqual(str(call), "PUT /v1/demo:poke")

    def test_an_optional_parameter_is_not_required(self):
        source = fixture.DEMO_SOURCE.replace(
            '{ "value", P_REQUIRED }', '{ "value", P_OPTIONAL }'
        )
        call, _ = documented(LARGE, sources={"route_demo.cc": source})[("PUT", "demo", "poke")]
        self.assertEqual(call.parameters, [("value", False)])

    def test_a_body_handler_marks_the_call_as_taking_a_body(self):
        call, _ = documented(LARGE)[("POST", "extra", "upload")]
        self.assertTrue(call.takes_body)

    def test_a_command_of_none_prints_as_the_bare_route(self):
        call, _ = documented(SMALL)[("GET", "demo", "none")]
        self.assertEqual(str(call), "GET /v1/demo")


class ConditionalTest(unittest.TestCase):
    def test_a_call_behind_an_if_belongs_only_to_the_product_that_defines_it(self):
        self.assertNotIn(("PUT", "demo", "poke"), documented(SMALL))
        self.assertIn(("PUT", "demo", "poke"), documented(LARGE))


class CompiledSourcesTest(unittest.TestCase):
    def test_only_the_sources_the_makefile_lists_are_read(self):
        self.assertNotIn(("POST", "extra", "upload"), documented(SMALL))
        self.assertIn(("POST", "extra", "upload"), documented(LARGE))

    def test_a_source_that_registers_nothing_is_ignored(self):
        with fixture.repository(sources={"json.cc": "int helper(void) { return 0; }\n"}) as root:
            self.assertNotIn("software/api/json.cc", routes.sources_with_calls(root))

    def test_a_family_whose_makefiles_disagree_is_refused(self):
        profile = dict(LARGE, targets=("target/large/Makefile", "target/small/Makefile"))
        with fixture.repository() as root:
            with self.assertRaises(OpenApiError) as raised:
                routes.compiled_sources(profile, root)
        self.assertIn("route_extra.cc", str(raised.exception))

    def test_a_target_that_compiles_no_route_source_is_refused(self):
        with fixture.repository(makefiles={"target/small/Makefile": "SRCS_CC = other.cc\n"}) as root:
            with self.assertRaises(OpenApiError):
                routes.compiled_sources(SMALL, root)


class DirectiveTest(unittest.TestCase):
    def test_directives_are_read_with_their_arguments(self):
        _, doc = documented(LARGE)[("PUT", "demo", "poke")]
        self.assertEqual(doc.value("SUMMARY"), "Poke a slot")
        self.assertEqual(doc.value("PATH"), ["/v1/demo/{slot}:poke", "pokeDemo", ""])
        self.assertEqual(doc.values("PARAM"), [["value", "integer(0..255)", "Byte to write.", "0", "64"]])

    def test_a_directive_with_the_wrong_number_of_arguments_is_refused(self):
        refuses(self, LARGE, "SUMMARY takes 1 arguments",
                sources={"route_demo.cc": fixture.DEMO_SOURCE.replace(
                    'SUMMARY("List demos")', 'SUMMARY("List demos", "extra")')})

    def test_an_unknown_directive_is_refused(self):
        refuses(self, LARGE, "unknown API_DOC directive NOTE",
                sources={"route_demo.cc": fixture.DEMO_SOURCE.replace(
                    'SUMMARY("List demos")', 'NOTE("hello")')})

    def test_value_insists_on_exactly_one_directive(self):
        _, doc = documented(SMALL)[("GET", "demo", "none")]
        with self.assertRaises(OpenApiError):
            doc.value("PARAM")


class PairingTest(unittest.TestCase):
    def test_a_call_without_a_block_is_refused(self):
        source = fixture.DEMO_SOURCE[fixture.DEMO_SOURCE.index("API_CALL"):]
        refuses(self, SMALL, "no API_DOC block for GET /v1/demo",
                sources={"route_demo.cc": source})

    def test_a_block_without_a_call_is_refused(self):
        orphan = ('API_DOC(GET, demo, ghost,\n'
                  '    TAG("Demo") SUMMARY("s") DESCRIPTION("d")\n'
                  '    PATH("/v1/demo:ghost", "ghost", "")\n'
                  '    RESPONSE("200", "application/json", "DemoResponse", "r", ""))\n')
        refuses(self, SMALL, "an API_DOC block with no API_CALL for GET /v1/demo:ghost",
                sources={"route_demo.cc": fixture.DEMO_SOURCE + orphan})

    def test_the_same_call_registered_twice_is_refused(self):
        refuses(self, SMALL, "already has an API_CALL",
                sources={"route_demo.cc": fixture.DEMO_SOURCE + fixture.DEMO_SOURCE})


if __name__ == "__main__":
    unittest.main()
