#!/usr/bin/env python3
# Gate check: the response validator agrees with the committed documents. No device.

"""Verify openapi_contract decides correctly, against answers written by hand.

`--validate-openapi` puts this validator in the path of every REST call the gate
makes. It only ever runs against a real device, where a wrong verdict looks
exactly like a firmware defect: a validator that accepts too much reports
nothing, and one that refuses too much fails an unrelated suite with a message
about the specification.

So the verdicts are pinned here, against the committed documents in `doc/api`
and against bodies written by hand. This needs no device: the documents and the
answers are both in this file's reach.
"""

import os
import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import openapi_contract  # noqa: E402  (needs tests/lib on sys.path first)
from report import Failure, check, detail, suite_fail, suite_ok  # noqa: E402

JSON = {"Content-Type": "application/json"}
BINARY = {"Content-Type": "application/octet-stream"}


def accepts(contract, method, path, status, headers, text):
    contract.check(method, path, status, headers, text.encode("utf-8"))


def refuses(contract, method, path, status, headers, text, expected):
    try:
        accepts(contract, method, path, status, headers, text)
    except Failure as exc:
        if expected not in str(exc):
            raise Failure(f"refused for the wrong reason: {exc}")
        return
    raise Failure(f"{method} {path} was accepted, expected a refusal about {expected!r}")


def run_profiles() -> None:
    with check("the core_version field decides which document applies"):
        # The same #ifdef U64 that decides the call set decides this field, so
        # the device answers the question rather than a name match.
        if openapi_contract.profile_of({"core_version": "1.4E"}) != "u64":
            raise Failure("a device reporting core_version is not read as an Ultimate 64")
        if openapi_contract.profile_of({"product": "Ultimate II+"}) != "u2":
            raise Failure("a device without core_version is not read as a cartridge")

    with check("a declared profile is used instead of asking the device"):
        os.environ[openapi_contract.ENV_PROFILE] = "u2"
        try:
            if openapi_contract.declared_profile() != "u2":
                raise Failure(f"{openapi_contract.ENV_PROFILE} was not honoured")
        finally:
            del os.environ[openapi_contract.ENV_PROFILE]
        if openapi_contract.declared_profile() is not None:
            raise Failure("a profile was declared when the environment named none")

    with check("a profile with no document is refused"):
        os.environ[openapi_contract.ENV_PROFILE] = "u128"
        try:
            openapi_contract.declared_profile()
            raise Failure("an unknown profile was accepted")
        except Failure as exc:
            if openapi_contract.ENV_PROFILE not in str(exc):
                raise
        finally:
            del os.environ[openapi_contract.ENV_PROFILE]


def run_matching(contract) -> None:
    cases = (
        ("/v1/version", "/v1/version"),
        ("/v1/drives/a:mount", "/v1/drives/{drive}:mount"),
        ("/v1/files/Usb0/games/disk.d64:info", "/v1/files/{path}:info"),
        ("/v1/configs", "/v1/configs"),
        ("/v1/configs/Drive%20A%20Settings", "/v1/configs/{category}"),
        ("/v1/configs/Drive%20A%20Settings/Drive", "/v1/configs/{category}/{item}"),
        ("/v1/configs:save_to_flash", "/v1/configs:save_to_flash"),
        ("/v1/configs/Drive:save_to_flash", "/v1/configs/{category}:save_to_flash"),
    )
    with check("a request path finds the call it belongs to"):
        for path, expected in cases:
            found = contract.template_for(path)
            if found != expected:
                raise Failure(f"{path} matched {found}, expected {expected}")
        detail(f"{len(cases)} paths, including the three configs depths")

    with check("a path the document does not describe matches nothing"):
        if contract.template_for("/v1/nonsense:call") is not None:
            raise Failure("an undescribed path was matched to a call")


def run_verdicts(contract) -> None:
    with check("an answer that matches is accepted"):
        accepts(contract, "GET", "/v1/version", 200, JSON, '{"version": "0.1", "errors": []}')
        accepts(contract, "GET", "/v1/machine:readmem?address=D020", 200, BINARY, "\x00\x01")

    with check("a body missing a required member is refused"):
        refuses(contract, "GET", "/v1/version", 200, JSON, '{"errors": []}',
                "'version' is a required property")

    with check("a member of the wrong type is refused"):
        refuses(contract, "GET", "/v1/machine:heap", 200, JSON,
                '{"free": "plenty", "errors": []}', "is not of type")

    with check("a status the call does not declare is refused"):
        refuses(contract, "GET", "/v1/version", 418, JSON, '{"errors": ["teapot"]}',
                "which GET /v1/version")

    with check("the password refusal every call shares is accepted"):
        accepts(contract, "GET", "/v1/version", 403, JSON, '{"errors": ["Forbidden."]}')

    with check("JSON where the document promises bytes is refused"):
        refuses(contract, "GET", "/v1/machine:readmem", 200, JSON, '{"errors": []}',
                "does not declare")

    with check("a body that will not parse is refused"):
        refuses(contract, "GET", "/v1/version", 200, JSON, "{not json", "unparsable JSON")

    with check("an undescribed call is refused unless the device says so too"):
        accepts(contract, "GET", "/v1/nonsense:call", 404, JSON, '{"errors": ["nope"]}')
        refuses(contract, "GET", "/v1/nonsense:call", 200, JSON, '{"errors": []}',
                "describes no such call")

    with check("a verb the call does not have is refused"):
        refuses(contract, "PUT", "/v1/version", 200, JSON, '{"errors": []}', "describes no PUT")

    with check("anything outside /v1 is left alone"):
        accepts(contract, "GET", "/openapi.yaml", 200, {"Content-Type": "text/plain"},
                "openapi: 3.1.0")


def run_documents() -> None:
    with check("both documents load and describe their own product"):
        cartridges = openapi_contract.Contract.load("u2")
        ultimate64 = openapi_contract.Contract.load("u64")
        if cartridges.template_for("/v1/machine:debugreg") is not None:
            raise Failure("the cartridge document describes the Ultimate 64 debug register")
        if ultimate64.template_for("/v1/machine:debugreg") is None:
            raise Failure("the Ultimate 64 document is missing the debug register")
        detail(f"u2 {len(cartridges.operations)} operations, "
               f"u64 {len(ultimate64.operations)} operations")


def main() -> int:
    run_profiles()
    contract = openapi_contract.Contract.load("u64")
    run_matching(contract)
    run_verdicts(contract)
    run_documents()
    suite_ok("openapi_contract_test")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("openapi_contract_test", str(exc))
        raise SystemExit(1)
