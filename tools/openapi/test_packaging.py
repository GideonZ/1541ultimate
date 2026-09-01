#!/usr/bin/env python3
"""Tests that each product ships the document that describes it.

An updater carries one of the two documents into `/Flash/html/openapi.yaml`, and
which one it carries is said twice: the target makefile names the file to embed,
and the updater source names the symbol that file becomes. If those two ever
disagree, a device serves the contract of the other product family, which is
worse than serving none. These tests hold them to each other.
"""

import pathlib
import re
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import generate
import schemas

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
APPLICATION_DIR = REPO_ROOT / "software" / "application"

_SRCS_YAML = re.compile(r"^\s*SRCS_YAML\s*[:+]?=\s*(\S+)\s*$", re.M)
_SRCS_HTML = re.compile(r"^\s*SRCS_HTML\s*[:+]?=\s*(.*)$", re.M)
_PRJ = re.compile(r"^\s*PRJ\s*[:+]?=\s*(\S+)\s*$", re.M)
_APPLICATION = re.compile(r"application/([A-Za-z0-9_]+)")
_EMBEDDED_SYMBOL = re.compile(r"_rest_api_openapi_(\w+)_yaml_start")


def _named_in(makefile, name):
    return bool(re.search(r"(?<![\w/.$])%s(?![\w])" % re.escape(name), makefile))


def _expanded(makefile):
    """The makefile text with $(PRJ) resolved, which is how some name their own source."""
    project = _PRJ.search(makefile)
    return makefile.replace("$(PRJ)", project.group(1)) if project else makefile


def _targets_embedding_a_document():
    """{target makefile: document file name} for every target that embeds one."""
    out = {}
    for makefile in sorted(REPO_ROOT.glob("target/**/Makefile")):
        found = _SRCS_YAML.search(makefile.read_text())
        if found:
            out[makefile.relative_to(REPO_ROOT)] = found.group(1)
    return out


def _sources_of(makefile_path):
    """The application sources a target compiles, from its VPATH and its source list."""
    makefile = _expanded((REPO_ROOT / makefile_path).read_text())
    out = []
    for directory in sorted(set(_APPLICATION.findall(makefile))):
        for source in sorted((APPLICATION_DIR / directory).glob("*.cc")):
            if _named_in(makefile, source.name):
                out.append(source)
    return out


def _documents_referenced(source):
    return set(_EMBEDDED_SYMBOL.findall(source.read_text()))


class EmbeddedDocumentTest(unittest.TestCase):
    def setUp(self):
        self.targets = _targets_embedding_a_document()

    def test_some_target_embeds_a_document(self):
        """Otherwise every test below would pass by describing nothing."""
        self.assertTrue(self.targets)

    def test_every_embedded_document_is_one_the_generator_writes(self):
        written = {generate.output_path(profile).name for profile in schemas.PROFILES}
        for target, document in self.targets.items():
            self.assertIn(document, written, "%s embeds %s" % (target, document))

    def test_every_target_that_embeds_a_document_compiles_a_source_that_uses_it(self):
        for target in self.targets:
            using = [source for source in _sources_of(target) if _documents_referenced(source)]
            self.assertTrue(using, "%s embeds a document no source it compiles refers to" % target)

    def test_the_embedded_document_is_the_one_the_source_asks_for(self):
        """The makefile picks the file, the source picks the symbol, and a symbol
        only resolves to the file the makefile embedded."""
        for target, document in self.targets.items():
            for source in _sources_of(target):
                for profile in _documents_referenced(source):
                    self.assertEqual(
                        generate.output_path(profile).name, document,
                        "%s embeds %s but %s refers to the %s document"
                        % (target, document, source.relative_to(REPO_ROOT), profile),
                    )

    def test_every_target_that_embeds_a_document_also_ships_the_explorer(self):
        """The page is what makes the document readable on the device."""
        for target in self.targets:
            declared = _SRCS_HTML.search((REPO_ROOT / target).read_text())
            self.assertIsNotNone(declared, str(target))
            self.assertIn("api.html", declared.group(1).split(), str(target))

    def test_no_source_refers_to_a_document_that_no_target_embeds(self):
        """A new updater that forgets SRCS_YAML would otherwise fail only at link time."""
        compiled = {source for target in self.targets for source in _sources_of(target)}
        for source in sorted(APPLICATION_DIR.glob("*/*.cc")):
            if _documents_referenced(source):
                self.assertIn(source, compiled, "%s is compiled by no target that embeds a document"
                              % source.relative_to(REPO_ROOT))


if __name__ == "__main__":
    unittest.main()
