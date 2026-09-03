#!/usr/bin/env python3
# E2E: Holds the device to the generated OpenAPI document, and drives it through a
# client generated from that document.

"""Three things this suite establishes, in order.

1. The committed documents are valid OpenAPI 3.1, checked with
   `openapi-spec-validator`.
2. A real code generator, `openapi-python-client`, turns the document for this
   device into a working client package.
3. The device answers as the document says it does. The generated client makes
   real calls, and every other call in the suite runs with the response
   validation in `tests/lib/openapi_contract.py` switched on, which checks the
   status code and the JSON body of each answer against the document.

The validation seam lives in `tests/lib/rest.py`, the client every suite uses,
so `./run-tests --validate-openapi` applies point 3 to the whole gate without a
suite being changed. This suite turns it on for itself either way.

Host packages: openapi-spec-validator, openapi-python-client, PyYAML. See
tests/requirements.txt. Runs on any target; the document it checks against is
chosen from what GET /v1/info reports.
"""

import argparse
import importlib
import os
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import machine as machine_lib  # noqa: E402  (needs tests/lib first)
import openapi_contract  # noqa: E402  (needs tests/lib on sys.path first)
import targets  # noqa: E402  (needs tests/lib on sys.path first)
from report import (Failure, check, check_skip, check_warn, detail,
                    format_exception, suite_fail, suite_ok)
from rest import RestClient

# Written to the generator's config so it does not try to run ruff over what it
# produced. Formatting the output is not what this suite is establishing, and
# ruff is not part of the harness.
GENERATOR_CONFIG = "post_hooks: []\n"

# A pair of bytes every C64 has, and that no other suite is holding.
BORDER_COLOUR_ADDRESS = "D020"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("-H", "--host", required=True, help="Device or target token.")
    parser.add_argument("-p", "--password", default="", help="Network password, if one is set.")
    parser.add_argument("-t", "--timeout", type=float, default=10.0, help="Per-request timeout.")
    return parser.parse_args()


def require(module: str, package: str):
    try:
        return importlib.import_module(module)
    except ImportError as exc:
        raise Failure(
            "%s is needed for this suite: pip install -r tests/requirements.txt" % package
        ) from exc


def validate_documents() -> None:
    validator = require("openapi_spec_validator", "openapi-spec-validator")
    yaml = require("yaml", "PyYAML")
    for profile in ("u2", "u64"):
        path = openapi_contract.document_path(profile)
        with check("%s document is valid OpenAPI 3.1" % profile):
            if not path.exists():
                raise Failure("%s is missing; run `make openapi`" % path)
            with path.open(encoding="utf-8") as handle:
                document = yaml.safe_load(handle)
            try:
                validator.validate(document)
            except Exception as exc:
                raise Failure("%s is not valid: %s" % (path, exc)) from exc
            detail("%d paths, %d schemas"
                   % (len(document["paths"]), len(document["components"]["schemas"])))


def generate_client(profile: str, into: pathlib.Path) -> str:
    """Generate a client package for `profile` and return its importable name."""
    config = into / "generator.yaml"
    config.write_text(GENERATOR_CONFIG, encoding="utf-8")
    result = subprocess.run(
        [sys.executable, "-m", "openapi_python_client", "generate",
         "--path", str(openapi_contract.document_path(profile)),
         "--config", str(config), "--meta", "none", "--overwrite"],
        cwd=str(into), capture_output=True, text=True)
    if result.returncode != 0:
        raise Failure("openapi-python-client failed: %s"
                      % (result.stderr.strip() or result.stdout.strip())[:600])
    packages = [entry for entry in into.iterdir()
                if entry.is_dir() and (entry / "__init__.py").exists()]
    if len(packages) != 1:
        raise Failure("expected one generated package, found %s"
                      % sorted(entry.name for entry in packages))
    return packages[0].name


def generated_client(module_name: str, host: str, password: str):
    client_module = importlib.import_module("%s.client" % module_name)
    headers = {"X-Password": password} if password else {}
    return client_module.Client(base_url="http://%s" % host, headers=headers)


def run_generated_client(session: RestClient, args: argparse.Namespace,
                         profile: str, workspace: pathlib.Path) -> None:
    require("openapi_python_client", "openapi-python-client")
    module_name: str | None = None

    with check("a client generates from the %s document" % profile):
        module_name = generate_client(profile, workspace)
        detail("package %s" % module_name)

    sys.path.insert(0, str(workspace))
    client = generated_client(module_name, session.host, args.password)

    with check("the generated client reads the interface version"):
        get_version = importlib.import_module("%s.api.about.get_version" % module_name)
        answer = get_version.sync_detailed(client=client)
        if answer.status_code != 200:
            raise Failure("generated getVersion returned HTTP %d" % answer.status_code)
        if not getattr(answer.parsed, "version", ""):
            raise Failure("generated getVersion parsed no version out of %r" % answer.content)
        detail("version %s" % answer.parsed.version)

    with check("the generated client reads memory and agrees with the raw call"):
        read_memory = importlib.import_module("%s.api.machine.read_memory" % module_name)
        answer = read_memory.sync_detailed(client=client, address=BORDER_COLOUR_ADDRESS, length=2)
        if answer.status_code != 200:
            raise Failure("generated readMemory returned HTTP %d" % answer.status_code)
        raw = session.expect("GET", "/v1/machine:readmem",
                             params={"address": BORDER_COLOUR_ADDRESS, "length": 2})
        if answer.content != raw:
            raise Failure("generated client read %r, raw call read %r" % (answer.content, raw))
        detail("$%s reads %s" % (BORDER_COLOUR_ADDRESS, answer.content.hex()))


def run_served_documents(session: RestClient, profile: str) -> None:
    """The device serves its own contract, written to /Flash/html by the updater."""
    committed = openapi_contract.document_path(profile).read_bytes()

    with check("the device serves its own OpenAPI document"):
        status, _, body = session.request("GET", "/openapi.yaml")
        if status == 404:
            check_skip("this firmware predates the update that writes /Flash/html/openapi.yaml")
        elif status != 200:
            raise Failure("GET /openapi.yaml returned HTTP %d" % status)
        elif body != committed:
            # /Flash/html is written by the updater, so a device that had only
            # its application replaced, which is what a JTAG load does, keeps
            # the document from whichever release last ran the updater.
            check_warn("the device serves %d bytes, the %s document in this tree is %d; "
                       "this device was last updated by a different build"
                       % (len(body), profile, len(committed)))
        else:
            detail("%d bytes, byte for byte the committed %s document" % (len(body), profile))

    with check("the device serves the API explorer"):
        status, _, body = session.request("GET", "/api.html")
        if status == 404:
            check_skip("this firmware predates the update that writes /Flash/html/api.html")
        elif status != 200:
            raise Failure("GET /api.html returned HTTP %d" % status)
        elif b"openapi.yaml" not in body:
            raise Failure("/api.html does not point at the document")


def run_contract(session: RestClient, args: argparse.Namespace,
                 machine: machine_lib.Machine) -> None:
    """Every call here is checked against the document by the seam in rest.py."""
    calls = (
        ("GET", "/v1/version", None),
        ("GET", "/v1/info", None),
        # Not served at all on a machine that lacks the heap reading, where it
        # answers 404 and the document declares only 200 and 403.
        ("GET", "/v1/machine:heap", None, machine_lib.MACHINE_HEAP_READING),
        ("GET", "/v1/drives", None),
        ("GET", "/v1/configs", None),
        ("GET", "/v1/configs/Drive%20A%20Settings", None),
        ("GET", "/v1/configs/Drive%20A%20Settings/*", None),
        ("GET", "/v1/machine:readmem", {"address": BORDER_COLOUR_ADDRESS, "length": 2}),
    )
    for call in calls:
        method, path, params = call[:3]
        needs_fix = call[3] if len(call) > 3 else None
        label = "%s %s matches the document" % (method, path)
        if needs_fix and machine.skip_without_fix(needs_fix, label):
            continue
        with check(label):
            status, _, _ = session.request(method, path, params=params)
            if status != 200:
                raise Failure("expected HTTP 200, got %d" % status)

    with check("GET /v1/machine:menu_screen matches the document"):
        status, _, _ = session.request("GET", "/v1/machine:menu_screen")
        if status not in (200, 404):
            raise Failure("expected HTTP 200 or 404, got %d" % status)
        detail("HTTP %d" % status)

    label = "a rejected call matches the document"
    if not machine.skip_without_fix(machine_lib.READMEM_REJECTS_ZERO_LENGTH, label):
        with check(label):
            status, _, _ = session.request("GET", "/v1/machine:readmem",
                                           params={"address": "D020", "length": 0})
            if status != 400:
                raise Failure(
                    "expected HTTP 400 for a zero length read, got %d" % status)

    with check("a refused call matches the document"):
        if not args.password:
            check_skip("no network password is configured on this device")
        else:
            status, _, _ = session.request("GET", "/v1/version", use_password=False)
            if status != 403:
                raise Failure("expected HTTP 403 without the password, got %d" % status)


def main() -> int:
    args = parse_args()
    validate_documents()

    # On for this suite whatever the runner was told, so the calls below are
    # checked even on a plain gate run.
    os.environ[openapi_contract.ENV_FLAG] = "1"
    session = RestClient(args.host, args.password, args.timeout)

    info = session.json("/v1/info")
    profile = openapi_contract.profile_of(info)
    detail("%s is described by the %s document" % (info.get("product", args.host), profile))
    machine = machine_lib.identify(
        targets.device_of(args.host),
        lambda: (str(info.get("product", "")), str(info.get("firmware_version", ""))))

    run_served_documents(session, profile)
    with tempfile.TemporaryDirectory(prefix="openapi-client-") as workspace:
        run_generated_client(session, args, profile, pathlib.Path(workspace))
        run_contract(session, args, machine)

    suite_ok("openapi_contract_test")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("openapi_contract_test", format_exception(exc))
        raise SystemExit(1)
