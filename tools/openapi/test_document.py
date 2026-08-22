#!/usr/bin/env python3
"""Tests for building an OpenAPI document out of the call table."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import document
import fixture
from errors import OpenApiError


def build(profile="small", replacements=(), **kwargs):
    source = fixture.DEMO_SOURCE
    for old, new in replacements:
        assert old in source, old
        source = source.replace(old, new)
    sources = dict(kwargs.pop("sources", {}))
    sources.setdefault("route_demo.cc", source)
    with fixture.repository(sources=sources, **kwargs) as root:
        return document.build(profile, root)


def refuses(test, message, **kwargs):
    with test.assertRaises(OpenApiError) as raised:
        build(**kwargs)
    test.assertIn(message, str(raised.exception))


class ShapeTest(unittest.TestCase):
    def setUp(self):
        self.built = build("large")
        self.poke = self.built["paths"]["/v1/demo/{slot}:poke"]["put"]

    def test_the_document_declares_openapi_and_the_product(self):
        self.assertEqual(self.built["openapi"], "3.1.0")
        self.assertEqual(self.built["info"]["title"], "Large API")
        self.assertIn("the large product", self.built["info"]["summary"])

    def test_the_password_header_is_the_security_scheme(self):
        self.assertEqual(self.built["security"], [{}, {"NetworkPassword": []}])
        self.assertEqual(
            self.built["components"]["securitySchemes"]["NetworkPassword"]["name"], "X-Password"
        )

    def test_prose_comes_from_the_block(self):
        self.assertEqual(self.poke["summary"], "Poke a slot")
        self.assertEqual(self.poke["description"], "Writes one byte into a slot.")
        self.assertEqual(self.poke["operationId"], "pokeDemo")
        self.assertEqual(self.poke["tags"], ["Demo"])

    def test_a_path_placeholder_becomes_a_required_path_parameter(self):
        slot = self.poke["parameters"][0]
        self.assertEqual((slot["name"], slot["in"], slot["required"]), ("slot", "path", True))
        self.assertEqual(slot["schema"]["enum"], ["a", "b"])

    def test_a_query_parameter_takes_its_requiredness_from_the_registration(self):
        value = self.poke["parameters"][1]
        self.assertEqual((value["name"], value["in"], value["required"]), ("value", "query", True))
        self.assertEqual(
            value["schema"], {"type": "integer", "minimum": 0, "maximum": 255, "default": 0}
        )
        self.assertEqual(value["example"], 64)

    def test_a_documented_error_becomes_an_example_of_the_errors_array(self):
        example = self.poke["responses"]["400"]["content"]["application/json"]["examples"]
        self.assertEqual(example["Invalid slot"]["value"], {"errors": ["Invalid slot"]})

    def test_a_body_handler_produces_a_request_body(self):
        upload = self.built["paths"]["/v1/extra:upload"]["post"]["requestBody"]
        self.assertTrue(upload["required"])
        self.assertEqual(
            upload["content"]["application/octet-stream"]["schema"],
            {"type": "string", "format": "binary"},
        )

    def test_the_server_url_does_not_repeat_the_path_prefix(self):
        # The path keys carry /v1 themselves; a client joins server and path.
        self.assertEqual(self.built["servers"][0]["url"], "http://{host}")
        self.assertTrue(all(path.startswith("/v1") for path in self.built["paths"]))

    def test_the_document_names_who_publishes_it(self):
        self.assertIn("url", self.built["info"]["contact"])
        self.assertIn("name", self.built["info"]["license"])
        self.assertIn("url", self.built["externalDocs"])

    def test_every_example_is_a_value_rather_than_a_string_of_json(self):
        for item in self.built["paths"].values():
            for operation in item.values():
                for code, response in operation["responses"].items():
                    if "$ref" in response:
                        continue
                    for media in (response.get("content") or {}).values():
                        for name, example in (media.get("examples") or {}).items():
                            self.assertIsInstance(example["value"], (dict, list), name)

    def test_an_example_that_is_not_json_is_refused(self):
        refuses(self, "not valid JSON",
                replacements=[('    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n',
                               '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n'
                               '    RESPONSE_EXAMPLE("200", "broken", "{not json}", "")\n')])

    def test_only_the_schemas_and_tags_in_use_are_carried(self):
        self.assertEqual(sorted(self.built["components"]["schemas"]), ["DemoResponse", "ErrorResponse"])
        self.assertEqual([tag["name"] for tag in self.built["tags"]], ["Demo"])


class SharedResponseTest(unittest.TestCase):
    """The firmware answers 403 for every call and 412 for every unfed body."""

    def setUp(self):
        self.built = build("large")

    def test_every_operation_declares_the_password_refusal(self):
        for template, item in self.built["paths"].items():
            for method, operation in item.items():
                self.assertIn("403", operation["responses"], "%s %s" % (method, template))

    def test_a_call_with_no_403_of_its_own_refers_to_the_shared_response(self):
        listing = self.built["paths"]["/v1/demo"]["get"]
        self.assertEqual(listing["responses"]["403"],
                         {"$ref": "#/components/responses/Forbidden"})
        self.assertIn("Forbidden", self.built["components"]["responses"])

    def test_a_call_that_documents_its_own_403_keeps_it_and_gains_the_refusal(self):
        poke = self.built["paths"]["/v1/demo/{slot}:poke"]["put"]
        examples = poke["responses"]["403"]["content"]["application/json"]["examples"]
        self.assertIn("Slot is write protected", examples)
        self.assertIn("Forbidden.", examples)

    def test_only_a_call_that_takes_a_body_declares_the_missing_body(self):
        upload = self.built["paths"]["/v1/extra:upload"]["post"]
        examples = upload["responses"]["412"]["content"]["application/json"]["examples"]
        self.assertIn("Expected Body, but got none.", examples)
        self.assertNotIn("412", self.built["paths"]["/v1/demo"]["get"]["responses"])


class CautionTest(unittest.TestCase):
    """What a call does beyond answering, for a reader and for a caller."""

    CAUTION = ('    SUMMARY("List demos")\n',
               '    SUMMARY("List demos")\n'
               '    CAUTION("destructive,persistent", "Throws the demos away.")\n')
    DEPRECATED = ('    SUMMARY("List demos")\n',
                  '    SUMMARY("List demos")\n'
                  '    DEPRECATED("Never implemented.")\n')

    def listing(self, built):
        return built["paths"]["/v1/demo"]["get"]

    def test_a_caution_reaches_the_reader_and_the_caller(self):
        listing = self.listing(build(replacements=[self.CAUTION]))
        self.assertEqual(listing["x-ultimate-caution"],
                         {"hints": ["destructive", "persistent"],
                          "note": "Throws the demos away."})
        self.assertIn("**Caution (destructive, persistent):** Throws the demos away.",
                      listing["description"])

    def test_the_vocabulary_is_explained_in_the_document_that_uses_it(self):
        built = build(replacements=[self.CAUTION])
        self.assertIn("## Calls that need care", built["info"]["description"])
        self.assertIn("`destructive`", built["info"]["description"])
        self.assertNotIn("`power`", built["info"]["description"])

    def test_a_document_with_no_cautions_explains_nothing(self):
        self.assertNotIn("## Calls that need care", build()["info"]["description"])

    def test_a_hint_outside_the_vocabulary_is_refused(self):
        refuses(self, "which is not one of",
                replacements=[('    SUMMARY("List demos")\n',
                               '    SUMMARY("List demos")\n'
                               '    CAUTION("scary", "Boo.")\n')])

    def test_a_caution_that_names_no_hint_is_refused(self):
        refuses(self, "names no hint",
                replacements=[('    SUMMARY("List demos")\n',
                               '    SUMMARY("List demos")\n'
                               '    CAUTION("", "Boo.")\n')])

    def test_two_cautions_are_refused(self):
        refuses(self, "at most one CAUTION",
                replacements=[('    SUMMARY("List demos")\n',
                               '    SUMMARY("List demos")\n'
                               '    CAUTION("destructive", "One.")\n'
                               '    CAUTION("persistent", "Two.")\n')])

    def test_a_deprecated_call_says_so_and_says_why(self):
        listing = self.listing(build(replacements=[self.DEPRECATED]))
        self.assertTrue(listing["deprecated"])
        self.assertIn("Deprecated: Never implemented.", listing["description"])

    def test_a_call_that_is_not_deprecated_carries_no_flag(self):
        self.assertNotIn("deprecated", self.listing(build()))


class ProductTest(unittest.TestCase):
    def test_each_product_describes_only_what_it_serves(self):
        self.assertNotIn("/v1/demo/{slot}:poke", build("small")["paths"])
        self.assertIn("/v1/demo/{slot}:poke", build("large")["paths"])

    def test_a_schema_no_product_uses_is_refused(self):
        with fixture.repository() as root:
            fixture.SCHEMAS["Unused"] = {"type": "object"}
            try:
                with self.assertRaises(OpenApiError) as raised:
                    document.build_all(root)
            finally:
                del fixture.SCHEMAS["Unused"]
        self.assertIn("Unused", str(raised.exception))


class ConsistencyTest(unittest.TestCase):
    def test_a_registered_parameter_with_no_documentation_is_refused(self):
        refuses(self, "registers ['value']", profile="large",
                replacements=[('    PARAM("value", "integer(0..255)", "Byte to write.", "0", "64")\n', "")])

    def test_a_documented_parameter_that_is_not_registered_is_refused(self):
        refuses(self, "documents parameters", profile="large",
                replacements=[('ARRAY( { { "value", P_REQUIRED } })', "ARRAY( { })")])

    def test_a_body_that_the_call_does_not_take_is_refused(self):
        refuses(self, "the call takes none",
                replacements=[('    PATH("/v1/demo", "listDemos", "")\n',
                               '    PATH("/v1/demo", "listDemos", "")\n'
                               '    BODY("application/json", "", "Nope.")\n')])

    def test_a_call_that_takes_a_body_without_one_documented_is_refused(self):
        source = fixture.EXTRA_SOURCE.replace(
            '    BODY("application/octet-stream", "", "The payload.")\n', "")
        refuses(self, "no BODY is documented", profile="large",
                sources={"route_extra.cc": source})

    def test_a_path_that_does_not_match_its_route_is_refused(self):
        refuses(self, "must start with '/v1/demo'",
                replacements=[('PATH("/v1/demo", "listDemos", "")', 'PATH("/v1/demos", "listDemos", "")')])

    def test_a_path_that_drops_the_command_is_refused(self):
        refuses(self, "must end with ':poke'", profile="large",
                replacements=[('PATH("/v1/demo/{slot}:poke"', 'PATH("/v1/demo/{slot}"')])

    def test_a_placeholder_without_a_path_parameter_is_refused(self):
        refuses(self, "declares no PATH_PARAM for it",
                replacements=[('PATH("/v1/demo", "listDemos", "")',
                               'PATH("/v1/demo/{name}", "listDemos", "")')])

    def test_a_path_parameter_no_path_uses_is_refused(self):
        refuses(self, "is used by no PATH",
                replacements=[('    PATH("/v1/demo", "listDemos", "")\n',
                               '    PATH("/v1/demo", "listDemos", "")\n'
                               '    PATH_PARAM("name", "string", "Unused.", "x")\n')])

    def test_a_repeated_operation_id_is_refused(self):
        refuses(self, "operationId 'listDemos' is already used", profile="large",
                replacements=[('PATH("/v1/demo/{slot}:poke", "pokeDemo", "")',
                               'PATH("/v1/demo/{slot}:poke", "listDemos", "")')])

    def test_a_response_scoped_to_an_unknown_operation_is_refused(self):
        refuses(self, "is scoped to 'nowhere'",
                replacements=[('"The demos.", ""', '"The demos.", "nowhere"')])

    def test_an_example_for_a_response_that_has_no_content_is_refused(self):
        refuses(self, "which has no response with content",
                replacements=[('    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n',
                               '    RESPONSE("200", "", "", "No content.", "")\n'
                               '    RESPONSE_EXAMPLE("200", "empty", "{}", "")\n')])

    def test_a_call_with_no_response_at_all_is_refused(self):
        refuses(self, "no RESPONSE applies",
                replacements=[('    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n', "")])

    def test_a_schema_that_does_not_exist_is_refused(self):
        refuses(self, "unresolved reference",
                replacements=[('"DemoResponse"', '"NoSuchResponse"')])

    def test_a_tag_that_is_not_defined_is_refused(self):
        refuses(self, "undefined tags: Nonsense",
                replacements=[('TAG("Demo")\n    SUMMARY("List demos")',
                               'TAG("Nonsense")\n    SUMMARY("List demos")')])

    def test_an_unsupported_parameter_type_is_refused(self):
        refuses(self, "unsupported parameter type", profile="large",
                replacements=[('"integer(0..255)"', '"octet"')])


class MultiplePathTest(unittest.TestCase):
    """One registration can be reachable under more than one path."""

    SECOND_PATH = (
        '    PATH("/v1/demo", "listDemos", "")\n',
        '    PATH("/v1/demo", "listDemos", "")\n'
        '    PATH("/v1/demo/{name}", "getDemo", "Read one demo")\n'
        '    PATH_PARAM("name", "string", "Which demo.", "first")\n'
        '    RESPONSE("200", "application/json", "DemoResponse", "One demo.", "getDemo")\n',
    )

    def setUp(self):
        self.built = build(replacements=[self.SECOND_PATH])

    def test_both_paths_are_emitted(self):
        self.assertEqual(sorted(self.built["paths"]), ["/v1/demo", "/v1/demo/{name}"])

    def test_a_path_may_override_the_shared_summary(self):
        self.assertEqual(self.built["paths"]["/v1/demo"]["get"]["summary"], "List demos")
        self.assertEqual(self.built["paths"]["/v1/demo/{name}"]["get"]["summary"], "Read one demo")

    def test_a_scoped_response_reaches_only_its_own_path(self):
        self.assertEqual(
            self.built["paths"]["/v1/demo"]["get"]["responses"]["200"]["description"], "The demos."
        )
        self.assertEqual(
            self.built["paths"]["/v1/demo/{name}"]["get"]["responses"]["200"]["description"],
            "One demo.",
        )

    def test_a_path_parameter_appears_only_where_the_template_uses_it(self):
        self.assertNotIn("parameters", self.built["paths"]["/v1/demo"]["get"])
        self.assertEqual(
            [p["name"] for p in self.built["paths"]["/v1/demo/{name}"]["get"]["parameters"]], ["name"]
        )


class DuplicateDeclarationTest(unittest.TestCase):
    """A second declaration is refused rather than replacing the first.

    Every case here built a document before, in which one declaration had quietly
    replaced another, so the document said something the block did not.
    """

    def test_two_paths_with_the_same_template_and_verb_are_refused(self):
        refuses(self, "GET /v1/demo is described twice", replacements=[(
            '    PATH("/v1/demo", "listDemos", "")\n',
            '    PATH("/v1/demo", "listDemos", "")\n'
            '    PATH("/v1/demo", "listDemosAgain", "Second summary")\n',
        )])

    def test_two_param_directives_for_one_name_are_refused(self):
        refuses(self, "two PARAM directives for 'value'", profile="large", replacements=[(
            '    PARAM("value", "integer(0..255)", "Byte to write.", "0", "64")\n',
            '    PARAM("value", "integer(0..255)", "Byte to write.", "0", "64")\n'
            '    PARAM("value", "string", "Something else.", "", "x")\n',
        )])

    def test_two_path_param_directives_for_one_name_are_refused(self):
        refuses(self, "two PATH_PARAM directives for 'slot'", profile="large", replacements=[(
            '    PATH_PARAM("slot", "string", "Which slot.", "a")\n',
            '    PATH_PARAM("slot", "string", "Which slot.", "a")\n'
            '    PATH_PARAM("slot", "integer", "Something else.", "1")\n',
        )])

    def test_two_param_enum_directives_for_one_name_are_refused(self):
        refuses(self, "two PARAM_ENUM directives for 'value'", profile="large", replacements=[(
            '    PARAM("value", "integer(0..255)", "Byte to write.", "0", "64")\n',
            '    PARAM("value", "integer(0..255)", "Byte to write.", "0", "64")\n'
            '    PARAM_ENUM("value", "1,2")\n'
            '    PARAM_ENUM("value", "3,4")\n',
        )])

    def test_two_responses_for_one_code_in_one_scope_are_refused(self):
        refuses(self, "two RESPONSE directives for 200", replacements=[(
            '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n',
            '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n'
            '    RESPONSE("200", "application/json", "ErrorResponse", "Something else.", "")\n',
        )])

    def test_two_response_examples_of_one_name_are_refused(self):
        refuses(self, "two RESPONSE_EXAMPLE directives named 'one' for 200", replacements=[(
            '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n',
            '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n'
            '    RESPONSE_EXAMPLE("200", "one", "{\\"demos\\": []}", "")\n'
            '    RESPONSE_EXAMPLE("200", "one", "{\\"demos\\": [\\"x\\"]}", "")\n',
        )])

    def test_two_response_errors_with_the_same_message_are_refused(self):
        refuses(self, "two RESPONSE_ERROR directives say 'Bad' for 400", replacements=[(
            '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n',
            '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n'
            '    RESPONSE_ERROR("400", "Bad", "")\n'
            '    RESPONSE_ERROR("400", "Bad", "")\n',
        )])

    def test_two_response_errors_with_different_messages_are_two_examples(self):
        built = build(replacements=[(
            '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n',
            '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n'
            '    RESPONSE_ERROR("400", "Bad", "")\n'
            '    RESPONSE_ERROR("400", "Worse", "")\n',
        )])
        examples = built["paths"]["/v1/demo"]["get"]["responses"]["400"]["content"]
        self.assertEqual(sorted(examples["application/json"]["examples"]), ["Bad", "Worse"])


class ScopedResponseTest(unittest.TestCase):
    """A response scoped to an operation refines the unscoped one for that code."""

    UNSCOPED_FIRST = (
        '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n',
        '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n'
        '    RESPONSE("200", "application/json", "DemoResponse", "One demo.", "getDemo")\n',
    )
    SCOPED_FIRST = (
        '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n',
        '    RESPONSE("200", "application/json", "DemoResponse", "One demo.", "getDemo")\n'
        '    RESPONSE("200", "application/json", "DemoResponse", "The demos.", "")\n',
    )
    SECOND_PATH = (
        '    PATH("/v1/demo", "listDemos", "")\n',
        '    PATH("/v1/demo", "listDemos", "")\n'
        '    PATH("/v1/demo/{name}", "getDemo", "Read one demo")\n'
        '    PATH_PARAM("name", "string", "Which demo.", "first")\n',
    )

    def descriptions(self, ordering):
        paths = build(replacements=[self.SECOND_PATH, ordering])["paths"]
        return (paths["/v1/demo"]["get"]["responses"]["200"]["description"],
                paths["/v1/demo/{name}"]["get"]["responses"]["200"]["description"])

    def test_the_scoped_response_wins_when_it_is_written_last(self):
        self.assertEqual(self.descriptions(self.UNSCOPED_FIRST), ("The demos.", "One demo."))

    def test_the_scoped_response_wins_when_it_is_written_first(self):
        """Which one applies is decided by scope, not by which line came first."""
        self.assertEqual(self.descriptions(self.SCOPED_FIRST), ("The demos.", "One demo."))


class DeclaredTypeTest(unittest.TestCase):
    """A literal that is not the type its directive declared is refused, not coerced."""

    def test_a_misspelt_boolean_is_refused(self):
        refuses(self, 'is not a boolean', replacements=[(
            '    RESPONSE("200"',
            '    PARAM("flag", "boolean", "A flag.", "ture", "")\n'
            '    RESPONSE("200"',
        ), (
            'ARRAY( { })',
            'ARRAY( { { "flag", P_OPTIONAL } })',
        )])

    def test_a_boolean_default_is_read_as_a_boolean(self):
        built = build(replacements=[(
            '    RESPONSE("200"',
            '    PARAM("flag", "boolean", "A flag.", "false", "true")\n'
            '    RESPONSE("200"',
        ), (
            'ARRAY( { })',
            'ARRAY( { { "flag", P_OPTIONAL } })',
        )])
        flag = built["paths"]["/v1/demo"]["get"]["parameters"][0]
        self.assertIs(flag["schema"]["default"], False)
        self.assertIs(flag["example"], True)

    def test_an_example_outside_the_declared_range_is_refused(self):
        refuses(self, "is outside 'integer(0..255)'", profile="large", replacements=[(
            'PARAM("value", "integer(0..255)", "Byte to write.", "0", "64")',
            'PARAM("value", "integer(0..255)", "Byte to write.", "0", "300")',
        )])

    def test_a_value_that_is_not_an_integer_is_refused(self):
        refuses(self, "is not an integer", profile="large", replacements=[(
            'PARAM("value", "integer(0..255)", "Byte to write.", "0", "64")',
            'PARAM("value", "integer(0..255)", "Byte to write.", "0", "many")',
        )])

    def test_a_refusal_names_the_block_and_the_parameter(self):
        with self.assertRaises(OpenApiError) as raised:
            build(profile="large", replacements=[(
                'PARAM("value", "integer(0..255)", "Byte to write.", "0", "64")',
                'PARAM("value", "integer(0..255)", "Byte to write.", "0", "many")',
            )])
        self.assertIn("route_demo.cc", str(raised.exception))
        self.assertIn("parameter 'value'", str(raised.exception))


class SecurityTest(unittest.TestCase):
    def test_the_password_is_an_alternative_and_not_a_demand(self):
        """A device with no password configured answers every call without one, so
        the empty requirement has to stand beside the scheme."""
        self.assertEqual(build()["security"], [{}, {"NetworkPassword": []}])


if __name__ == "__main__":
    unittest.main()
