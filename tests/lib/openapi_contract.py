"""Checks what the device answers against the generated OpenAPI document.

`doc/api/rest_api_openapi_u2.yaml` and `..._u64.yaml` are generated from the
firmware route table, so they say what every call is supposed to return. This
module holds those documents to the device: for each answer it checks that the
status code is one the document declares for that operation, and that a JSON
body validates against the schema declared for it.

It is off unless `ULTIMATE_VALIDATE_OPENAPI` is set. `tests/lib/rest.py` is the
only HTTP client the suites use, so turning it on there turns it on for every
suite at once, without a suite being changed:

    ULTIMATE_VALIDATE_OPENAPI=1 ./run-tests -H u64
    ./run-tests -H u64 --validate-openapi

Response bodies are validated with `openapi-schema-validator`, which implements
the OpenAPI 3.1 dialect rather than plain JSON Schema.
"""

from __future__ import annotations

import json
import os
import pathlib
import re
from typing import Dict, Optional, Tuple

from report import Failure

ENV_FLAG = "ULTIMATE_VALIDATE_OPENAPI"
# Which document applies. Set it to skip the GET /v1/info that would otherwise
# be needed to find out, which is one request a suite did not ask for.
ENV_PROFILE = "ULTIMATE_OPENAPI_PROFILE"

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCUMENT_DIR = REPO_ROOT / "doc" / "api"
DOCUMENT_NAME = "rest_api_openapi_%s.yaml"

# Which document describes a device. `core_version` is added to GET /v1/info
# under `#ifdef U64`, which is the same switch that decides the call set, so it
# is the firmware's own answer to the question rather than a name match.
U64_ONLY_INFO_FIELD = "core_version"

METHODS = ("get", "put", "post", "delete", "patch", "head", "options")

_documents: Dict[str, "Contract"] = {}


def enabled() -> bool:
    return os.environ.get(ENV_FLAG, "").strip() not in ("", "0", "false", "no")


def document_path(profile: str) -> pathlib.Path:
    return DOCUMENT_DIR / (DOCUMENT_NAME % profile)


def profile_of(info: Dict[str, object]) -> str:
    return "u64" if U64_ONLY_INFO_FIELD in info else "u2"


def declared_profile() -> Optional[str]:
    name = os.environ.get(ENV_PROFILE, "").strip()
    if not name:
        return None
    if name not in ("u2", "u64"):
        raise Failure("%s must be u2 or u64, not %r" % (ENV_PROFILE, name))
    return name


def _load_yaml(path: pathlib.Path) -> Dict[str, object]:
    try:
        import yaml
    except ImportError as exc:
        raise Failure(
            "response validation needs PyYAML: pip install -r tests/requirements.txt"
        ) from exc
    with path.open(encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def _oas_validator():
    try:
        from openapi_schema_validator import OAS31Validator
    except ImportError as exc:
        raise Failure(
            "response validation needs openapi-schema-validator: "
            "pip install -r tests/requirements.txt"
        ) from exc
    return OAS31Validator


class Operation:
    def __init__(self, template: str, method: str, body: Dict[str, object]) -> None:
        self.template = template
        self.method = method
        self.body = body
        self.operation_id = body.get("operationId", "")
        self.responses: Optional[Dict[str, object]] = None

    def __str__(self) -> str:
        return "%s %s (%s)" % (self.method.upper(), self.template, self.operation_id)


class Contract:
    """One product family's document, ready to check answers against."""

    def __init__(self, profile: str, document: Dict[str, object]) -> None:
        self.profile = profile
        self.document = document
        # Built once per schema. Constructing one costs milliseconds, and the
        # suites this runs under measure the device in milliseconds.
        self._validators: Dict[str, object] = {}
        self.operations: Dict[Tuple[str, str], Operation] = {}
        for template, item in document["paths"].items():
            for method, body in item.items():
                if method in METHODS:
                    self.operations[(method, template)] = Operation(template, method, body)
        self._strict = self._matchers(r"[^/]+")
        self._loose = self._matchers(r".+")

    @classmethod
    def load(cls, profile: str) -> "Contract":
        if profile not in _documents:
            path = document_path(profile)
            if not path.exists():
                raise Failure("no OpenAPI document at %s; run `make openapi`" % path)
            _documents[profile] = cls(profile, _load_yaml(path))
        return _documents[profile]

    def _matchers(self, placeholder: str):
        out = []
        for template in {key[1] for key in self.operations}:
            pattern = "".join(
                placeholder if part.startswith("{") else re.escape(part)
                for part in re.split(r"(\{\w+\})", template)
            )
            out.append((re.compile("^%s$" % pattern), template))
        return out

    def template_for(self, path: str) -> Optional[str]:
        """The path template a request path belongs to, or None when there is none."""
        for matchers in (self._strict, self._loose):
            found = [template for pattern, template in matchers if pattern.match(path)]
            if len(found) == 1:
                return found[0]
            if found:
                return sorted(found, key=len)[-1]
        return None

    def check(self, method: str, path: str, status: int,
              headers: Dict[str, str], body: bytes) -> None:
        """Raise `Failure` when an answer does not match what the document promises."""
        path = path.split("?", 1)[0]
        if not path.startswith("/v1/") and path != "/v1":
            return
        template = self.template_for(path)
        if template is None:
            if status == 404:
                return
            raise Failure(
                "%s %s answered HTTP %d but the %s document describes no such call"
                % (method.upper(), path, status, self.profile)
            )
        operation = self.operations.get((method.lower(), template))
        if operation is None:
            if status in (404, 405):
                return
            raise Failure(
                "%s %s answered HTTP %d but the %s document describes no %s on %s"
                % (method.upper(), path, status, self.profile, method.upper(), template)
            )
        self._check_status(operation, path, status)
        self._check_body(operation, path, status, headers, body)

    def _responses(self, operation: Operation) -> Dict[str, object]:
        if operation.responses is not None:
            return operation.responses
        out = {}
        for code, response in operation.body["responses"].items():
            reference = response.get("$ref") if isinstance(response, dict) else None
            if reference:
                response = self.document["components"]["responses"][reference.rsplit("/", 1)[-1]]
            out[str(code)] = response
        operation.responses = out
        return out

    def _validator(self, schema: Dict[str, object]):
        key = json.dumps(schema, sort_keys=True)
        if key not in self._validators:
            self._validators[key] = _oas_validator()(
                {"components": self.document["components"], **schema})
        return self._validators[key]

    def _check_status(self, operation: Operation, path: str, status: int) -> None:
        declared = self._responses(operation)
        if str(status) in declared or "default" in declared:
            return
        raise Failure(
            "%s answered HTTP %d, which %s does not declare (declared: %s)"
            % (path, status, operation, ", ".join(sorted(declared)))
        )

    def _check_body(self, operation: Operation, path: str, status: int,
                    headers: Dict[str, str], body: bytes) -> None:
        response = self._responses(operation).get(str(status))
        if not response:
            return
        content_type = ""
        for key, value in headers.items():
            if key.lower() == "content-type":
                content_type = value.split(";", 1)[0].strip().lower()
        if content_type != "application/json":
            return
        media = (response.get("content") or {}).get("application/json")
        if not media or "schema" not in media:
            raise Failure(
                "%s answered HTTP %d with JSON, which %s does not declare"
                % (path, status, operation)
            )
        try:
            decoded = json.loads(body.decode("utf-8", "replace"))
        except ValueError as exc:
            raise Failure("%s answered HTTP %d with unparsable JSON: %s" % (path, status, exc))
        errors = sorted(
            self._validator(media["schema"]).iter_errors(decoded),
            key=lambda error: list(error.absolute_path),
        )
        if errors:
            first = errors[0]
            where = "/".join(str(part) for part in first.absolute_path) or "the body"
            raise Failure(
                "%s answered HTTP %d with a body %s does not allow: %s at %s"
                % (path, status, operation, first.message, where)
            )
