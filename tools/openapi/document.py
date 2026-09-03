"""Turns the documented call table of one product family into an OpenAPI 3.1 document."""

import contextlib
import json
import re

import routes
import schemas
from errors import OpenApiError

HTTP_REASON = {
    "200": "OK",
    "203": "Nothing to return (the firmware's own use of 203; see the document description)",
    "400": "Bad request",
    "403": "Forbidden",
    "404": "Not found",
    "405": "Method not allowed",
    "412": "Precondition failed",
    "415": "Unsupported media type",
    "423": "Locked",
    "424": "Failed dependency",
    "500": "Internal server error",
    "501": "Not implemented",
    "503": "Service unavailable",
    "507": "Insufficient storage",
}

_PLACEHOLDER = re.compile(r"\{(\w+)\}")
_PATH_SEGMENTS = re.compile(r"^(/(\{\w+\}|[\w%.-]+))+$")
_INTEGER_RANGE = re.compile(r"^integer\((-?\d+)\.\.(-?\d+)\)$")
_BINARY = {"type": "string", "format": "binary"}

_SCOPED = ("RESPONSE", "RESPONSE_EXAMPLE", "RESPONSE_ERROR")


def _reference(name):
    return {"$ref": "#/components/schemas/%s" % name}


def _schema_for(type_name):
    if type_name in ("string", "integer", "boolean", "number"):
        return {"type": type_name}
    ranged = _INTEGER_RANGE.match(type_name)
    if not ranged:
        raise OpenApiError("unsupported parameter type %r" % type_name)
    return {"type": "integer", "minimum": int(ranged.group(1)), "maximum": int(ranged.group(2))}


def _as_declared(type_name, text):
    """The literal a directive wrote, as the type it declared, or a refusal."""
    schema = _schema_for(type_name)
    if schema["type"] == "integer":
        try:
            value = int(text)
        except ValueError:
            raise OpenApiError("%r is not an integer, which %r declares" % (text, type_name))
        low, high = schema.get("minimum"), schema.get("maximum")
        if low is not None and not low <= value <= high:
            raise OpenApiError("%s is outside %r" % (value, type_name))
        return value
    if schema["type"] == "boolean":
        if text not in ("true", "false"):
            raise OpenApiError('%r is not a boolean; write "true" or "false"' % text)
        return text == "true"
    return text


def _example_value(payload, where):
    try:
        return json.loads(payload)
    except ValueError as error:
        raise OpenApiError("%s: example is not valid JSON: %s" % (where, error))


def _add_example(responses, code, name, value):
    entry = responses.setdefault(code, {"description": HTTP_REASON.get(code, "Error")})
    media = entry.setdefault("content", {}).setdefault(
        "application/json", {"schema": _reference("ErrorResponse")}
    )
    media.setdefault("examples", {})[name] = {"value": value}


def _add_error_example(responses, code, message):
    _add_example(responses, code, message, {"errors": [message]})


def _template_problem(call, template):
    prefix = "/v1/" + call.route
    tail = "" if call.command == "none" else ":" + call.command
    remainder = template[len(prefix):]
    if not template.startswith(prefix) or (remainder and remainder[0] not in "/:"):
        return "must start with %r" % prefix
    if not template.endswith(tail):
        return "must end with %r" % tail
    middle = template[len(prefix):len(template) - len(tail) if tail else len(template)]
    if middle and not _PATH_SEGMENTS.match(middle):
        return "has an unexpected path segment %r" % middle
    return None


def _enum(doc, directive, name):
    declared = _declared_once(doc, directive).get(name)
    return [value.strip() for value in declared[0].split(",")] if declared else None


def _parameter(where, name, type_name, description, example, enum, required, default=None):
    schema = _schema_for(type_name)
    if enum:
        schema["enum"] = enum
    if default:
        schema["default"] = _as_declared(type_name, default)
    parameter = {
        "name": name,
        "in": where,
        "required": required,
        "description": description,
        "schema": schema,
    }
    if example:
        parameter["example"] = _as_declared(type_name, example)
    return parameter


def _declared_once(doc, directive):
    """The directives of one kind, keyed by the name each one is about."""
    out = {}
    for name, *rest in doc.values(directive):
        if name in out:
            raise OpenApiError("%s: two %s directives for %r" % (doc.where, directive, name))
        out[name] = rest
    return out


def _parameters(call, doc, template):
    declared_path = _declared_once(doc, "PATH_PARAM")
    declared_query = _declared_once(doc, "PARAM")

    out = []
    for name in _PLACEHOLDER.findall(template):
        if name not in declared_path:
            raise OpenApiError(
                "%s: path %s uses {%s} but declares no PATH_PARAM for it" % (doc.where, template, name)
            )
        type_name, description, example = declared_path[name]
        with _blaming(doc, name):
            out.append(_parameter("path", name, type_name, description, example,
                                  _enum(doc, "PATH_PARAM_ENUM", name), True))
    for name, required in call.parameters:
        type_name, description, default, example = declared_query[name]
        with _blaming(doc, name):
            out.append(_parameter("query", name, type_name, description, example,
                                  _enum(doc, "PARAM_ENUM", name), required, default))
    return out


@contextlib.contextmanager
def _blaming(doc, name):
    """Names the block and the parameter in whatever the body refuses."""
    try:
        yield
    except OpenApiError as error:
        raise OpenApiError("%s: parameter %r: %s" % (doc.where, name, error))


def _request_body(doc):
    declared = doc.values("BODY")
    if not declared:
        return None
    content = {}
    for content_type, schema_name, _ in declared:
        if content_type in content:
            raise OpenApiError("%s: two BODY directives for %s" % (doc.where, content_type))
        content[content_type] = {"schema": _reference(schema_name) if schema_name else dict(_BINARY)}
    return {
        "required": True,
        "description": declared[0][2],
        "content": dict(sorted(content.items())),
    }


def _responses(call, doc, operation_id):
    def applying(directive):
        """The directives of one kind that apply here: the unscoped ones, then the scoped.

        A directive scoped to this operation refines the unscoped one for the same
        status code, and does so whichever order the block happens to write them in.
        Two directives in the same scope for the same code are a conflict, because
        the block would then be saying two things and only one could survive.
        """
        applies = [values for values in doc.values(directive) if values[-1] in ("", operation_id)]
        return [values for values in applies if not values[-1]], [values for values in applies if values[-1]]

    def once(seen, key, complaint):
        if key in seen:
            raise OpenApiError("%s: %s in operation %r" % (doc.where, complaint, operation_id))
        seen.add(key)

    out = {}
    for group in applying("RESPONSE"):
        seen = set()
        for code, content_type, schema_name, description, _ in group:
            once(seen, code, "two RESPONSE directives for %s" % code)
            entry = {"description": description}
            if content_type:
                schema = _reference(schema_name) if schema_name else dict(_BINARY)
                entry["content"] = {content_type: {"schema": schema}}
            out[code] = entry
    for group in applying("RESPONSE_EXAMPLE"):
        seen = set()
        for code, name, payload, _ in group:
            once(seen, (code, name), "two RESPONSE_EXAMPLE directives named %r for %s" % (name, code))
            if code not in out or "content" not in out[code]:
                raise OpenApiError(
                    "%s: RESPONSE_EXAMPLE for %s, which has no response with content" % (doc.where, code)
                )
            media = out[code]["content"].get("application/json") or next(iter(out[code]["content"].values()))
            media.setdefault("examples", {})[name] = {"value": _example_value(payload, doc.where)}
    for group in applying("RESPONSE_ERROR"):
        seen = set()
        for code, message, _ in group:
            once(seen, (code, message), "two RESPONSE_ERROR directives say %r for %s" % (message, code))
            _add_error_example(out, code, message)
    if not out:
        raise OpenApiError("%s: no RESPONSE applies to operation %r" % (doc.where, operation_id))

    # The firmware checks the network password before it dispatches, and refuses a
    # POST whose body never arrived, so both apply to every matching call and are
    # added here rather than repeated in every block.
    if "403" in out:
        _add_error_example(out, "403", "Forbidden.")
    else:
        out["403"] = {"$ref": "#/components/responses/Forbidden"}
    if call.takes_body:
        _add_error_example(out, "412", "Expected Body, but got none.")
    return dict(sorted(out.items()))


def _caution(doc):
    declared = doc.values("CAUTION")
    if not declared:
        return None
    if len(declared) > 1:
        raise OpenApiError("%s: expected at most one CAUTION" % doc.where)
    tokens, note = declared[0]
    hints = [token.strip() for token in tokens.split(",") if token.strip()]
    unknown = [hint for hint in hints if hint not in schemas.CAUTION_HINTS]
    if unknown:
        raise OpenApiError(
            "%s: CAUTION uses %s, which is not one of %s"
            % (doc.where, ", ".join(unknown), ", ".join(sorted(schemas.CAUTION_HINTS)))
        )
    if not hints:
        raise OpenApiError("%s: CAUTION names no hint" % doc.where)
    return {"hints": hints, "note": note}


def _operation(call, doc, template, operation_id, summary):
    operation = {
        "tags": [doc.value("TAG")],
        "summary": summary or doc.value("SUMMARY"),
        "description": doc.value("DESCRIPTION"),
        "operationId": operation_id,
    }
    deprecated = doc.values("DEPRECATED")
    if deprecated:
        if len(deprecated) > 1:
            raise OpenApiError("%s: expected at most one DEPRECATED" % doc.where)
        operation["deprecated"] = True
        operation["description"] += "\n\nDeprecated: " + deprecated[0][0]
    caution = _caution(doc)
    if caution:
        # Twice over, from the one directive: as prose for whoever is reading the
        # rendered document, and as a field for whatever is calling it.
        operation["description"] += "\n\n**Caution (%s):** %s" % (
            ", ".join(caution["hints"]), caution["note"])
        operation[schemas.CAUTION_FIELD] = caution
    parameters = _parameters(call, doc, template)
    if parameters:
        operation["parameters"] = parameters
    body = _request_body(doc)
    if body:
        operation["requestBody"] = body
    operation["responses"] = _responses(call, doc, operation_id)
    return operation


def _verify_consistency(call, doc):
    declared = {name for name, *_ in doc.values("PARAM")}
    if declared != call.parameter_names:
        raise OpenApiError(
            "%s: documents parameters %s but %s registers %s"
            % (doc.where, sorted(declared), call.where, sorted(call.parameter_names))
        )
    if call.takes_body and not doc.values("BODY"):
        raise OpenApiError("%s: the call takes a request body but no BODY is documented" % doc.where)
    if not call.takes_body and doc.values("BODY"):
        raise OpenApiError("%s: a BODY is documented but the call takes none" % doc.where)

    templates = doc.values("PATH")
    if not templates:
        raise OpenApiError("%s: at least one PATH is required" % doc.where)
    placeholders = {name for template, _, _ in templates for name in _PLACEHOLDER.findall(template)}
    for name, *_ in doc.values("PATH_PARAM"):
        if name not in placeholders:
            raise OpenApiError("%s: PATH_PARAM %r is used by no PATH" % (doc.where, name))
    operation_ids = {operation_id for _, operation_id, _ in templates}
    for directive in _SCOPED:
        for values in doc.values(directive):
            if values[-1] and values[-1] not in operation_ids:
                raise OpenApiError(
                    "%s: %s is scoped to %r, which is not an operationId of this call"
                    % (doc.where, directive, values[-1])
                )
    for template, _, _ in templates:
        problem = _template_problem(call, template)
        if problem:
            raise OpenApiError(
                "%s: path %r %s, as registered at %s" % (doc.where, template, problem, call.where)
            )


def _paths(profile, repo_root):
    out = {}
    seen_ids = {}
    seen_where = {}
    for call, doc in routes.documented_calls(profile, repo_root).values():
        _verify_consistency(call, doc)
        for template, operation_id, summary in doc.values("PATH"):
            if operation_id in seen_ids:
                raise OpenApiError(
                    "%s: operationId %r is already used by %s"
                    % (doc.where, operation_id, seen_ids[operation_id])
                )
            seen_ids[operation_id] = doc.where
            item = out.setdefault(template, {})
            method = call.method.lower()
            if method in item:
                where, first_id = seen_where[(template, method)]
                raise OpenApiError(
                    "%s: %s %s is described twice, as %r and as %r; the second declaration is at %s"
                    % (where, call.method, template, first_id, operation_id, doc.where)
                )
            seen_where[(template, method)] = (doc.where, operation_id)
            item[method] = _operation(call, doc, template, operation_id, summary)
    return {template: dict(sorted(out[template].items())) for template in sorted(out)}


def _referenced(paths, components):
    """The component names the paths reach, following references between components."""
    used = {kind: set() for kind in components}

    def walk(node):
        if isinstance(node, dict):
            reference = node.get("$ref")
            if isinstance(reference, str):
                parts = reference.split("/")
                kind, name = (parts[2], parts[3]) if len(parts) == 4 else ("", "")
                if parts[:2] != ["#", "components"] or name not in components.get(kind, {}):
                    raise OpenApiError("unresolved reference %r" % reference)
                used[kind].add(name)
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)

    walk(paths)
    pending = [(kind, name) for kind, names in used.items() for name in names]
    while pending:
        kind, name = pending.pop()
        before = {k: set(v) for k, v in used.items()}
        walk(components[kind][name])
        pending.extend(
            (k, n) for k, names in used.items() for n in names - before[k]
        )
    return used


def _caution_legend(paths):
    """Explain the caution field in the document that uses it, or say nothing."""
    used = {
        hint
        for item in paths.values()
        for operation in item.values()
        for hint in operation.get(schemas.CAUTION_FIELD, {}).get("hints", [])
    }
    if not used:
        return ""
    lines = ["", "## Calls that need care", "",
             "An operation that has consequences beyond returning an answer carries a",
             "`%s` field naming what those are. The vocabulary is closed:" % schemas.CAUTION_FIELD, ""]
    lines += ["- `%s`: %s" % (hint, schemas.CAUTION_HINTS[hint]) for hint in sorted(used)]
    return "\n".join(lines) + "\n"


def _tags(paths):
    used = {tag for item in paths.values() for operation in item.values() for tag in operation["tags"]}
    unknown = used - {name for name, _ in schemas.TAGS}
    if unknown:
        raise OpenApiError("API_DOC uses undefined tags: %s" % ", ".join(sorted(unknown)))
    return [{"name": name, "description": text} for name, text in schemas.TAGS if name in used]


def build(name, repo_root):
    profile = schemas.PROFILES[name]
    paths = _paths(profile, repo_root)
    defined = {"schemas": schemas.SCHEMAS, "responses": schemas.RESPONSES}
    used = _referenced(paths, defined)
    return {
        "openapi": "3.1.0",
        "info": {
            "title": profile["title"],
            "version": schemas.API_VERSION,
            "summary": "REST interface of the Ultimate firmware on %s." % profile["products"],
            "description": (schemas.DESCRIPTION.format(products=profile["products"])
                            + _caution_legend(paths)),
            "contact": schemas.CONTACT,
            "license": schemas.LICENSE,
        },
        # The path keys carry the /v1 prefix themselves, so the server URL must
        # not repeat it: a generated client joins the two.
        "servers": [
            {
                "url": "http://{host}",
                "description": "An Ultimate device on the local network.",
                "variables": {
                    "host": {
                        "default": profile["default_host"],
                        "description": "Host name or address of the device.",
                    }
                },
            }
        ],
        "tags": _tags(paths),
        "externalDocs": schemas.EXTERNAL_DOCS,
        # A device with no network password configured accepts every request
        # without one, so the empty requirement is an alternative rather than the
        # scheme being demanded unconditionally.
        "security": [{}, {"NetworkPassword": []}],
        "paths": paths,
        "components": {
            "securitySchemes": schemas.SECURITY_SCHEMES,
            "responses": {key: schemas.RESPONSES[key] for key in sorted(used["responses"])},
            "schemas": {key: schemas.SCHEMAS[key] for key in sorted(used["schemas"])},
        },
    }


def build_all(repo_root):
    documents = {name: build(name, repo_root) for name in sorted(schemas.PROFILES)}
    for kind, defined in (("schemas", schemas.SCHEMAS), ("responses", schemas.RESPONSES)):
        used = set().union(*(document["components"][kind] for document in documents.values()))
        unused = set(defined) - used
        if unused:
            raise OpenApiError(
                "schemas.py defines %s no product refers to: %s" % (kind, ", ".join(sorted(unused)))
            )
    return documents
