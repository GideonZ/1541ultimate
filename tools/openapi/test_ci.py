#!/usr/bin/env python3
"""Tests that the build gate and the make target check the same thing.

`make openapi_validate` is what a developer runs; the `Validate OpenAPI
Specification` step is what CI runs. They cannot be the same command, because the
build image has no pip and so cannot install the validator, and the image that
can does not carry make. Two invocations of one check is exactly the arrangement
that drifts, so these tests hold them to each other: same validator, same
strictness, and the same list of documents, taken from `generate.py paths` rather
than written out in either place.
"""

import pathlib
import re
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKFLOW = REPO_ROOT / ".github" / "workflows" / "build.yml"
MAKEFILE = REPO_ROOT / "Makefile"

MODULE = "openapi_spec_validator"
STRICTNESS = "--validation-errors all"
PATH_SOURCE = "tools/openapi/generate.py paths"

_STEP = re.compile(r"- name: Validate OpenAPI Specification\n(.*?)(?=\n      - name:|\Z)", re.S)
_TARGET = re.compile(r"^openapi_validate:\r?\n((?:\t.*\r?\n)+)", re.M)
_VARIABLE = re.compile(r"^([A-Z_]+)\s*[:?]?=\s*(.*?)\r?$", re.M)


def _ci_step():
    found = _STEP.search(WORKFLOW.read_text())
    return found.group(1) if found else None


def _make_recipe():
    """The openapi_validate recipe with the makefile's own variables expanded.

    The recipe reaches the generator through $(OPENAPI), so expanding it is both
    what makes the comparison below meaningful and a check that the variable
    still points at the generator.
    """
    makefile = MAKEFILE.read_text()
    found = _TARGET.search(makefile)
    if not found:
        return None
    recipe = found.group(1)
    for name, value in _VARIABLE.findall(makefile):
        recipe = recipe.replace("$(%s)" % name, value.strip())
    return recipe


class ValidationGateTest(unittest.TestCase):
    def setUp(self):
        self.step = _ci_step()
        self.recipe = _make_recipe()

    def test_both_exist(self):
        """Otherwise every test below would pass by describing nothing."""
        self.assertIsNotNone(self.step, "the workflow has no Validate OpenAPI Specification step")
        self.assertIsNotNone(self.recipe, "the Makefile has no openapi_validate target")

    def test_both_run_the_same_validator(self):
        for where, text in (("the CI step", self.step), ("the make target", self.recipe)):
            self.assertIn(MODULE, text, where)

    def test_both_ask_for_every_validation_error(self):
        """One reporting only the first error would hide work the other reports."""
        for where, text in (("the CI step", self.step), ("the make target", self.recipe)):
            self.assertIn(STRICTNESS, text, where)

    def test_neither_writes_out_the_documents_to_check(self):
        """A hand-written list is how one of them ends up checking fewer files."""
        for where, text in (("the CI step", self.step), ("the make target", self.recipe)):
            self.assertIn(PATH_SOURCE, text, where)
            self.assertNotIn("rest_api_openapi", text, where)

    def test_the_ci_step_installs_what_the_requirements_file_pins(self):
        self.assertIn("tools/openapi/requirements.txt", self.step)


if __name__ == "__main__":
    unittest.main()
