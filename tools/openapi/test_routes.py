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


class TargetDefinesTest(unittest.TestCase):
    """The macros a target compiles with come from its own makefile, not from a list here."""

    def test_a_flag_with_a_value_keeps_it(self):
        self.assertEqual(routes.target_defines("OPTIONS = -DU64=2\n"), {"U64": 2})

    def test_a_flag_without_a_value_is_one_as_the_compiler_makes_it(self):
        self.assertEqual(routes.target_defines("OPTIONS = -DRISCV\n"), {"RISCV": 1})

    def test_a_hexadecimal_value_is_read_as_a_number(self):
        self.assertEqual(routes.target_defines("OPTIONS = -DIOBASE=0x10000000\n"),
                         {"IOBASE": 0x10000000})

    def test_a_value_that_is_not_a_number_is_kept_as_written(self):
        self.assertEqual(routes.target_defines("OPTIONS = -DNAME=text\n"), {"NAME": "text"})

    def test_a_commented_out_line_defines_nothing(self):
        """The real u2plus makefile keeps an older OPTIONS line commented out."""
        self.assertEqual(
            routes.target_defines("OPTIONS = -DU2P=1\n# OPTIONS = -DDEVELOPER=1\n"), {"U2P": 1}
        )

    def test_a_later_flag_wins_the_way_the_command_line_does(self):
        self.assertEqual(routes.target_defines("OPTIONS = -DA=1\nOPTIONS += -DA=2\n"), {"A": 2})

    def test_a_flag_that_only_looks_like_one_is_not_a_define(self):
        self.assertEqual(routes.target_defines("OPTIONS = -gdwarf-2 -Wno-write-strings\n"), {})


class ProfileDefinesTest(unittest.TestCase):
    """What the target compiles decides the document, so the makefile is the authority."""

    MAKEFILE = "OPTIONS += -DBIG\nSRCS_CC = route_demo.cc\n"

    def test_a_makefile_define_reaches_the_document_without_being_restated(self):
        """The reviewed generator read a per-family list instead, so a target that
        compiled -DBIG got a document with the call it serves left out."""
        table = documented(SMALL, makefiles={"target/small/Makefile": self.MAKEFILE})
        self.assertIn(("PUT", "demo", "poke"), table)

    def test_targets_of_one_family_that_serve_different_calls_are_refused(self):
        profile = dict(SMALL, targets=("target/a/Makefile", "target/b/Makefile"))
        refuses(self, profile, "do not serve the same API",
                makefiles={"target/a/Makefile": "SRCS_CC = route_demo.cc\n",
                           "target/b/Makefile": self.MAKEFILE})

    #: The same call on both targets, described differently on one of them.
    SAME_CALL_DIFFERENT_TEXT = """\
#include "routes.h"

API_DOC(GET, demo, none,
    TAG("Demo")
#if BIG
    SUMMARY("List big demos")
#else
    SUMMARY("List demos")
#endif
    DESCRIPTION("Lists every demo.")
    PATH("/v1/demo", "listDemos", "")
    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")
)
API_CALL(GET, demo, none, NULL, ARRAY( { }))
{
}
"""

    def test_targets_of_one_family_that_document_a_call_differently_are_refused(self):
        """One document describes the whole family, so the same call has to read
        the same on every target in it, not only be present on every target."""
        profile = dict(SMALL, targets=("target/a/Makefile", "target/b/Makefile"))
        refuses(self, profile, "describe GET /v1/demo differently",
                sources={"route_demo.cc": self.SAME_CALL_DIFFERENT_TEXT},
                makefiles={"target/a/Makefile": "SRCS_CC = route_demo.cc\n",
                           "target/b/Makefile": "OPTIONS += -DBIG\nSRCS_CC = route_demo.cc\n"})

    def test_targets_that_agree_are_accepted(self):
        profile = dict(SMALL, targets=("target/a/Makefile", "target/b/Makefile"))
        table = documented(profile,
                           makefiles={"target/a/Makefile": self.MAKEFILE,
                                      "target/b/Makefile": "OPTIONS += -DBIG=1\nSRCS_CC = route_demo.cc\n"})
        self.assertIn(("PUT", "demo", "poke"), table)


class RepeatedParameterTest(unittest.TestCase):
    def test_a_registration_that_names_one_parameter_twice_is_refused(self):
        """Two entries of one name would otherwise become two query parameters of
        that name, which no OpenAPI reader accepts."""
        source = fixture.DEMO_SOURCE.replace(
            'ARRAY( { { "value", P_REQUIRED } })',
            'ARRAY( { { "value", P_REQUIRED }, { "value", P_OPTIONAL } })',
        )
        refuses(self, LARGE, "declares the parameter value twice",
                sources={"route_demo.cc": source})


if __name__ == "__main__":
    unittest.main()
