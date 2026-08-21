#!/usr/bin/env python3
"""Tests for the YAML writer."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import yaml_writer

try:
    import yaml
except ImportError:
    yaml = None


class ScalarTest(unittest.TestCase):
    def test_plain_text_is_left_alone(self):
        self.assertEqual(yaml_writer.dump({"summary": "Read C64 memory"}), "summary: Read C64 memory\n")

    def test_values_that_would_change_type_are_quoted(self):
        self.assertEqual(yaml_writer.dump({"a": "0.1"}), "a: '0.1'\n")
        self.assertEqual(yaml_writer.dump({"a": "true"}), "a: 'true'\n")
        self.assertEqual(yaml_writer.dump({"a": "no"}), "a: 'no'\n")
        self.assertEqual(yaml_writer.dump({"a": ""}), "a: ''\n")

    def test_punctuation_yaml_would_read_is_quoted(self):
        self.assertEqual(yaml_writer.dump({"a": "x: y"}), "a: 'x: y'\n")
        self.assertEqual(yaml_writer.dump({"a": "# not a comment"}), "a: '# not a comment'\n")
        self.assertEqual(yaml_writer.dump({"a": " padded "}), "a: ' padded '\n")

    def test_an_apostrophe_is_doubled(self):
        self.assertEqual(yaml_writer.dump({"a": "doesn't"}), "a: 'doesn''t'\n")

    def test_keys_follow_the_same_rule(self):
        self.assertEqual(yaml_writer.dump({"/v1/x:y": 1}), "'/v1/x:y': 1\n")
        self.assertEqual(yaml_writer.dump({"200": 1}), "'200': 1\n")
        self.assertEqual(yaml_writer.dump({"$ref": "x"}), "$ref: x\n")

    def test_numbers_and_booleans_keep_their_type(self):
        self.assertEqual(yaml_writer.dump({"a": 8, "b": True, "c": False}), "a: 8\nb: true\nc: false\n")


class BlockTest(unittest.TestCase):
    def test_multiple_lines_become_a_literal_block(self):
        self.assertEqual(yaml_writer.dump({"a": "one\n\ntwo"}), "a: |-\n  one\n\n  two\n")

    def test_a_trailing_newline_is_kept_by_the_clip_indicator(self):
        self.assertEqual(yaml_writer.dump({"a": "one\n"}), "a: |\n  one\n")

    def test_empty_containers_are_written_inline(self):
        self.assertEqual(yaml_writer.dump({"a": {}, "b": []}), "a: {}\nb: []\n")

    def test_a_list_of_mappings_indents_under_the_dash(self):
        self.assertEqual(
            yaml_writer.dump({"tags": [{"name": "About", "description": "x"}]}),
            "tags:\n  - name: About\n    description: x\n",
        )

    def test_nesting_is_two_spaces_per_level(self):
        self.assertEqual(yaml_writer.dump({"a": {"b": {"c": 1}}}), "a:\n  b:\n    c: 1\n")


@unittest.skipUnless(yaml, "PyYAML is not installed")
class RoundTripTest(unittest.TestCase):
    def test_awkward_values_survive_a_round_trip(self):
        original = {
            "/v1/machine:readmem": {"get": {"operationId": "readMemory"}},
            "200": "OK",
            "version": "0.1",
            "note": "line one\nline two",
            "quote": "it's \"quoted\"",
            "empty": "",
            "hash": "#1",
            "list": [{"a": 1}, "plain", ""],
            "flags": [True, False, 0],
        }
        self.assertEqual(yaml.safe_load(yaml_writer.dump(original)), original)


if __name__ == "__main__":
    unittest.main()
