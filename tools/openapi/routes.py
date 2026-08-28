"""The REST call table the firmware declares, paired with the API_DOC blocks beside it."""

import pathlib
import re

import cpp
from errors import OpenApiError

API_SOURCE_DIR = "software/api"

DIRECTIVE_ARITY = {
    "TAG": 1,
    "SUMMARY": 1,
    "DESCRIPTION": 1,
    "DEPRECATED": 1,
    "CAUTION": 2,
    "PATH": 3,
    "PATH_PARAM": 4,
    "PATH_PARAM_ENUM": 2,
    "PARAM": 5,
    "PARAM_ENUM": 2,
    "BODY": 3,
    "RESPONSE": 5,
    "RESPONSE_EXAMPLE": 4,
    "RESPONSE_ERROR": 3,
}

_REGISTERS_A_CALL = re.compile(r"(?<![A-Za-z0-9_])API_CALL\s*\(")
_DECLARED_PARAM = re.compile(r'\{\s*"([^"]+)"\s*,\s*([^}]*?)\s*\}')
_DEFINE = re.compile(r"(?<![\w-])-D[ \t]*([A-Za-z_]\w*)(?:=(\S*))?")


class Entry:
    def __init__(self, origin, line, method, route, command):
        self.origin = origin
        self.line = line
        self.method = method
        self.route = route
        self.command = command

    @property
    def key(self):
        return (self.method, self.route, self.command)

    @property
    def where(self):
        return "%s:%d" % (self.origin, self.line)

    def __str__(self):
        tail = "" if self.command == "none" else ":" + self.command
        return "%s /v1/%s%s" % (self.method, self.route, tail)


class ApiCall(Entry):
    def __init__(self, origin, line, method, route, command, takes_body, parameters):
        Entry.__init__(self, origin, line, method, route, command)
        self.takes_body = takes_body
        self.parameters = parameters
        names = [name for name, _ in parameters]
        repeated = sorted({name for name in names if names.count(name) > 1})
        if repeated:
            raise OpenApiError(
                "%s: %s declares the parameter %s twice"
                % (self.where, self, ", ".join(repeated))
            )

    @property
    def parameter_names(self):
        return {name for name, _ in self.parameters}

    @property
    def fingerprint(self):
        return (self.takes_body, tuple(self.parameters))


class ApiDoc(Entry):
    def __init__(self, origin, line, method, route, command, directives):
        Entry.__init__(self, origin, line, method, route, command)
        self.directives = directives

    @property
    def fingerprint(self):
        return tuple((name, tuple(arguments)) for name, arguments in self.directives)

    def values(self, name):
        return [arguments for directive, arguments in self.directives if directive == name]

    def value(self, name):
        found = self.values(name)
        if len(found) != 1:
            raise OpenApiError(
                "%s: expected exactly one %s, found %d" % (self.where, name, len(found))
            )
        return found[0][0] if DIRECTIVE_ARITY[name] == 1 else found[0]


def _parse_calls(origin, text):
    calls = []
    for line, arguments in cpp.invocations(text, "API_CALL"):
        parts = cpp.split_arguments(arguments)
        if len(parts) != 5:
            raise OpenApiError("%s:%d: API_CALL takes 5 arguments, found %d" % (origin, line, len(parts)))
        method, route, command, body_handler, declared = parts
        parameters = [
            (name, "P_REQUIRED" in flags) for name, flags in _DECLARED_PARAM.findall(declared)
        ]
        calls.append(ApiCall(origin, line, method, route, command, body_handler != "NULL", parameters))
    return calls


def _parse_directives(origin, line, text):
    directives = []
    for _, name, body in cpp.calls(text):
        if name not in DIRECTIVE_ARITY:
            raise OpenApiError("%s:%d: unknown API_DOC directive %s" % (origin, line, name))
        arguments = [cpp.string_literal(part) for part in cpp.split_arguments(body)]
        if len(arguments) != DIRECTIVE_ARITY[name]:
            raise OpenApiError(
                "%s:%d: %s takes %d arguments, found %d"
                % (origin, line, name, DIRECTIVE_ARITY[name], len(arguments))
            )
        directives.append((name, arguments))
    return directives


def _parse_docs(origin, text):
    docs = []
    for line, arguments in cpp.invocations(text, "API_DOC"):
        parts = cpp.split_arguments(arguments)
        if len(parts) != 4:
            raise OpenApiError("%s:%d: API_DOC takes a verb, route, command and body" % (origin, line))
        method, route, command, body = parts
        docs.append(ApiDoc(origin, line, method, route, command, _parse_directives(origin, line, body)))
    return docs


def sources_with_calls(repo_root):
    found = [
        "%s/%s" % (API_SOURCE_DIR, path.name)
        for path in sorted((pathlib.Path(repo_root) / API_SOURCE_DIR).glob("*.cc"))
        if _REGISTERS_A_CALL.search(cpp.without_comments(path.read_text()))
    ]
    if not found:
        raise OpenApiError("no REST call is registered under %s" % API_SOURCE_DIR)
    return found


def _without_comments(makefile):
    """A makefile comment runs from an unescaped # to the end of the line."""
    return "\n".join(line.split("#", 1)[0] for line in makefile.split("\n"))


def target_defines(makefile):
    """The macros a target compiles with, from the -D flags in its own makefile.

    A -D flag with no value defines the macro as 1, which is what the compiler
    does. A value that is not an integer literal is kept as written; #if only
    rejects it if an expression actually compares against it.
    """
    defines = {}
    for name, value in _DEFINE.findall(_without_comments(makefile)):
        if not value:
            defines[name] = 1
            continue
        try:
            defines[name] = int(value, 0)
        except ValueError:
            defines[name] = value
    return defines


def target_sources(makefile, candidates):
    text = _without_comments(makefile)
    return {
        source
        for source in candidates
        if re.search(r"(?<![\w/.])%s(?![\w])" % re.escape(pathlib.Path(source).name), text)
    }


def compiled_sources(profile, repo_root):
    """The route sources a product family builds, as its own makefiles list them."""
    candidates = sources_with_calls(repo_root)
    per_target = {
        target: target_sources((pathlib.Path(repo_root) / target).read_text(), candidates)
        for target in profile["targets"]
    }
    reference_target, reference = next(iter(per_target.items()))
    for target, sources in per_target.items():
        if sources != reference:
            raise OpenApiError(
                "%s and %s do not compile the same route sources: %s"
                % (reference_target, target, ", ".join(sorted(sources ^ reference)))
            )
    if not reference:
        raise OpenApiError("%s compiles no route source" % reference_target)
    return [source for source in candidates if source in reference]


def _by_key(entries, what):
    table = {}
    for entry in entries:
        if entry.key in table:
            raise OpenApiError(
                "%s: %s already has %s at %s" % (entry.where, entry, what, table[entry.key].where)
            )
        table[entry.key] = entry
    return table


def _table(sources, defines, repo_root):
    """The documented calls that survive the preprocessor for one set of macros."""
    calls, docs = [], []
    for source in sources:
        text = cpp.active_lines(
            cpp.without_comments((pathlib.Path(repo_root) / source).read_text()), defines
        )
        calls.extend(_parse_calls(source, text))
        docs.extend(_parse_docs(source, text))

    by_call = _by_key(calls, "an API_CALL")
    by_doc = _by_key(docs, "an API_DOC")
    _reject(set(by_call) - set(by_doc), by_call, "no API_DOC block for")
    _reject(set(by_doc) - set(by_call), by_doc, "an API_DOC block with no API_CALL for")
    return {key: (by_call[key], by_doc[key]) for key in sorted(by_call)}


def _fingerprints(table):
    return {key: (call.fingerprint, doc.fingerprint) for key, (call, doc) in table.items()}


def _call_name(key):
    method, route, command = key
    return "%s /v1/%s%s" % (method, route, "" if command == "none" else ":" + command)


def _divergence(reference_target, reference_marks, target, marks):
    """What to say about two targets of one family that do not serve the same API."""
    missing = sorted(set(reference_marks) - set(marks))
    added = sorted(set(marks) - set(reference_marks))
    if missing or added:
        return "%s serves %s, which %s does not" % (
            reference_target if missing else target,
            ", ".join(_call_name(key) for key in (missing or added)),
            target if missing else reference_target,
        )
    differing = sorted(key for key in reference_marks if reference_marks[key] != marks[key])
    return "%s and %s describe %s differently" % (
        reference_target, target, ", ".join(_call_name(key) for key in differing)
    )


def documented_calls(profile, repo_root):
    """Every call the product family compiles, each with the block that documents it.

    Which calls survive the preprocessor is decided per target, from the macros
    that target's own makefile passes to the compiler. Every target in the family
    has to arrive at the same table, because one document describes all of them.
    """
    sources = compiled_sources(profile, repo_root)
    per_target = {}
    for target in profile["targets"]:
        defines = target_defines((pathlib.Path(repo_root) / target).read_text())
        per_target[target] = _table(sources, defines, repo_root)

    reference_target, reference = next(iter(per_target.items()))
    reference_marks = _fingerprints(reference)
    for target, table in per_target.items():
        marks = _fingerprints(table)
        if marks != reference_marks:
            raise OpenApiError(
                "the targets of one product family do not serve the same API: %s"
                % _divergence(reference_target, reference_marks, target, marks)
            )
    return reference


def _reject(keys, table, complaint):
    if keys:
        raise OpenApiError(
            "%s %s" % (complaint, ", ".join("%s (%s)" % (table[k], table[k].where) for k in sorted(keys)))
        )
