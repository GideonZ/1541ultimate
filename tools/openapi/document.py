"""Turns the documented call table of one product family into an OpenAPI 3.1 document."""

import json
import re

import routes
import schemas
from errors import OpenApiError

HTTP_REASON = {
    "200": "OK",
    "203": "No content to return",
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
    declared = _schema_for(type_name)["type"]
    if declared == "integer":
        return int(text)
    if declared == "boolean":
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
    for declared, values in doc.values(directive):
        if declared == name:
            return [value.strip() for value in values.split(",")]
    return None


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


def _parameters(call, doc, template):
    declared_path = {name: rest for name, *rest in doc.values("PATH_PARAM")}
    declared_query = {name: rest for name, *rest in doc.values("PARAM")}

    out = []
    for name in _PLACEHOLDER.findall(template):
        if name not in declared_path:
            raise OpenApiError(
                "%s: path %s uses {%s} but declares no PATH_PARAM for it" % (doc.where, template, name)
            )
        type_name, description, example = declared_path[name]
        out.append(
            _parameter("path", name, type_name, description, example,
                       _enum(doc, "PATH_PARAM_ENUM", name), True)
        )
    for name, required in call.parameters:
        type_name, description, default, example = declared_query[name]
        out.append(
            _parameter("query", name, type_name, description, example,
                       _enum(doc, "PARAM_ENUM", name), required, default)
        )
    return out


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
    def mine(directive):
        return [values for values in doc.values(directive) if values[-1] in ("", operation_id)]

    out = {}
    for code, content_type, schema_name, description, _ in mine("RESPONSE"):
        entry = out.setdefault(code, {"description": description})
        if content_type:
            schema = _reference(schema_name) if schema_name else dict(_BINARY)
            entry.setdefault("content", {})[content_type] = {"schema": schema}
    for code, name, payload, _ in mine("RESPONSE_EXAMPLE"):
        if code not in out or "content" not in out[code]:
            raise OpenApiError(
                "%s: RESPONSE_EXAMPLE for %s, which has no response with content" % (doc.where, code)
            )
        media = out[code]["content"].get("application/json") or next(iter(out[code]["content"].values()))
        media.setdefault("examples", {})[name] = {
            "value": _example_value(payload, doc.where)
        }
    for code, message, _ in mine("RESPONSE_ERROR"):
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


def _operation(call, doc, template, operation_id, summary):
    operation = {
        "tags": [doc.value("TAG")],
        "summary": summary or doc.value("SUMMARY"),
        "description": doc.value("DESCRIPTION"),
        "operationId": operation_id,
    }
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
    for call, doc in routes.documented_calls(profile, repo_root).values():
        _verify_consistency(call, doc)
        for template, operation_id, summary in doc.values("PATH"):
            if operation_id in seen_ids:
                raise OpenApiError(
                    "%s: operationId %r is already used by %s"
                    % (doc.where, operation_id, seen_ids[operation_id])
                )
            seen_ids[operation_id] = doc.where
            out.setdefault(template, {})[call.method.lower()] = _operation(
                call, doc, template, operation_id, summary
            )
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
            "description": schemas.DESCRIPTION.format(products=profile["products"]),
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
        "security": [{"NetworkPassword": []}],
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
