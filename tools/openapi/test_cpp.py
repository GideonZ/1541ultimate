#!/usr/bin/env python3
"""Tests for reading the C++ sources."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import cpp
from errors import OpenApiError


class WithoutCommentsTest(unittest.TestCase):
    def test_line_comments_go_and_the_line_stays(self):
        self.assertEqual(cpp.without_comments("a = 1; // note\nb = 2;\n"), "a = 1; \nb = 2;\n")

    def test_block_comments_keep_the_lines_they_spanned(self):
        self.assertEqual(cpp.without_comments("a;\n/* one\ntwo */b;\n"), "a;\n\nb;\n")

    def test_comment_markers_inside_a_string_survive(self):
        self.assertEqual(cpp.without_comments('s = "http://x/*y*/";'), 's = "http://x/*y*/";')

    def test_an_apostrophe_in_a_comment_is_not_a_character_literal(self):
        self.assertEqual(cpp.without_comments("x; // doesn't matter\ny;"), "x; \ny;")

    def test_an_escaped_quote_does_not_end_the_string(self):
        self.assertEqual(cpp.without_comments(r'"a\"//b"; // c'), r'"a\"//b"; ')

    def test_an_unterminated_block_comment_is_refused(self):
        with self.assertRaises(OpenApiError):
            cpp.without_comments("a; /* never closed")


class ActiveLinesTest(unittest.TestCase):
    def blank_count(self, text, defines):
        return [line for line in cpp.active_lines(text, defines).split("\n") if line]

    def test_an_undefined_identifier_is_zero(self):
        self.assertEqual(self.blank_count("#if U64\nkept\n#endif\n", {}), [])
        self.assertEqual(self.blank_count("#if U64\nkept\n#endif\n", {"U64": 1}), ["kept"])

    def test_ifdef_and_ifndef(self):
        self.assertEqual(self.blank_count("#ifdef X\na\n#else\nb\n#endif\n", {"X": 0}), ["a"])
        self.assertEqual(self.blank_count("#ifndef X\na\n#else\nb\n#endif\n", {"X": 0}), ["b"])

    def test_elif_takes_the_first_true_branch_only(self):
        text = "#if A\na\n#elif B\nb\n#elif C\nc\n#else\nd\n#endif\n"
        self.assertEqual(self.blank_count(text, {"B": 1, "C": 1}), ["b"])
        self.assertEqual(self.blank_count(text, {}), ["d"])

    def test_a_nested_region_inside_an_inactive_one_stays_inactive(self):
        text = "#if A\n#if B\nboth\n#endif\nouter\n#endif\n"
        self.assertEqual(self.blank_count(text, {"B": 1}), [])
        self.assertEqual(self.blank_count(text, {"A": 1, "B": 1}), ["both", "outer"])

    def test_defined_in_either_spelling(self):
        self.assertEqual(self.blank_count("#if defined(X)\na\n#endif\n", {"X": 0}), ["a"])
        self.assertEqual(self.blank_count("#if defined X\na\n#endif\n", {"X": 0}), ["a"])

    def test_comparisons_and_boolean_operators(self):
        self.assertEqual(self.blank_count("#if U64 == 2\na\n#endif\n", {"U64": 2}), ["a"])
        self.assertEqual(self.blank_count("#if U64 == 2\na\n#endif\n", {"U64": 1}), [])
        self.assertEqual(self.blank_count("#if A && !B\na\n#endif\n", {"A": 1}), ["a"])

    def test_line_numbering_is_preserved(self):
        text = "one\n#if X\ntwo\n#endif\nthree\n"
        self.assertEqual(cpp.active_lines(text, {}).split("\n"), ["one", "", "", "", "three", ""])

    def test_an_unbalanced_region_is_refused(self):
        with self.assertRaises(OpenApiError):
            cpp.active_lines("#if X\na\n", {})
        with self.assertRaises(OpenApiError):
            cpp.active_lines("a\n#endif\n", {})

    def test_an_expression_outside_the_grammar_is_refused_with_its_line(self):
        with self.assertRaises(OpenApiError) as raised:
            cpp.active_lines("a\n#if X ** Y\nb\n#endif\n", {})
        self.assertIn("line 2", str(raised.exception))


class InvocationsTest(unittest.TestCase):
    def test_reports_the_arguments_and_the_line(self):
        text = "\n\nAPI_CALL(GET, demo, none, NULL, ARRAY( { } ))\n"
        self.assertEqual(
            list(cpp.invocations(text, "API_CALL")), [(3, "GET, demo, none, NULL, ARRAY( { } )")]
        )

    def test_a_longer_identifier_is_not_a_match(self):
        self.assertEqual(list(cpp.invocations("MY_API_CALL(x)", "API_CALL")), [])

    def test_parentheses_inside_a_string_do_not_close_the_call(self):
        self.assertEqual(list(cpp.invocations('API_DOC("a)b", c)', "API_DOC")), [(1, '"a)b", c')])

    def test_an_unbalanced_call_is_refused(self):
        with self.assertRaises(OpenApiError):
            list(cpp.invocations("API_CALL(a, b", "API_CALL"))


class CallsTest(unittest.TestCase):
    def test_reports_every_top_level_call_and_not_the_nested_ones(self):
        found = [(name, args) for _, name, args in cpp.calls('TAG("a") PARAM(f(1), 2)')]
        self.assertEqual(found, [("TAG", '"a"'), ("PARAM", "f(1), 2")])

    def test_a_call_written_inside_a_string_is_not_one(self):
        # DESCRIPTION("Returns HTTP(S) ...") must not read as an HTTP directive.
        found = [name for _, name, _ in cpp.calls('DESCRIPTION("Returns HTTP(S) text")')]
        self.assertEqual(found, ["DESCRIPTION"])


class SplitArgumentsTest(unittest.TestCase):
    def test_splits_only_at_the_top_level(self):
        self.assertEqual(cpp.split_arguments("a, f(b, c), { d, e }"), ["a", "f(b, c)", "{ d, e }"])

    def test_a_comma_in_a_string_is_not_a_separator(self):
        self.assertEqual(cpp.split_arguments('"a, b", c'), ['"a, b"', "c"])

    def test_an_empty_argument_list_is_one_empty_part(self):
        self.assertEqual(cpp.split_arguments(""), [""])


class StringLiteralTest(unittest.TestCase):
    def test_adjacent_literals_are_concatenated(self):
        self.assertEqual(cpp.string_literal('"one "\n    "two"'), "one two")

    def test_escapes_are_resolved(self):
        self.assertEqual(cpp.string_literal(r'"a\nb\"c\\d"'), 'a\nb"c\\d')

    def test_anything_that_is_not_a_literal_is_refused(self):
        with self.assertRaises(OpenApiError):
            cpp.string_literal("NULL")
        with self.assertRaises(OpenApiError):
            cpp.string_literal('"text" + variable')


if __name__ == "__main__":
    unittest.main()
