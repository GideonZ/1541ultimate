"""Isolated run-tests entry for the evidence-gated HTTPS bring-up campaign."""

import json
import os
from pathlib import Path
import subprocess
import sys
import time

import report
from runtests.exits import EXIT_OK, EXIT_SUITE_FAILED, die

NAME = "https-hardware"
HTTPS_OPTIONS = {"https_action", "https_plan", "https_installation",
                 "https_evidence", "https_peer"}


def dispatch(args, defaults, root: Path) -> int | None:
    """Return None for ordinary runs; never enter their device orchestration."""
    requested = any(getattr(args, key) for key in HTTPS_OPTIONS)
    if NAME not in args.suite:
        if requested:
            die("HTTPS options require -s https-hardware alone")
        return None
    if args.suite != [NAME]:
        die("https-hardware must run alone")
    allowed = HTTPS_OPTIONS | {"suite", "output_dir", "password", "color"}
    for key, value in vars(args).items():
        if key in allowed or (key == "attempts" and value == 1):
            continue
        if value != getattr(defaults, key):
            die(f"https-hardware does not accept the ordinary {key} option")
    if not args.output_dir:
        die("https-hardware requires a new -o output directory")

    action = args.https_action or "prepare"
    if action == "audit":
        if not args.https_evidence or not args.https_peer:
            die("HTTPS audit requires --https-evidence and --https-peer")
        if args.https_plan or args.https_installation:
            die("HTTPS audit takes its plan from saved evidence")
    else:
        if not args.https_plan:
            die("HTTPS preparation/execution requires --https-plan")
        if args.https_evidence or args.https_peer:
            die("--https-evidence and --https-peer are audit-only")
        if action == "execute" and not args.https_installation:
            die("HTTPS execution requires --https-installation")
        if action == "prepare" and args.https_installation:
            die("Offline preparation does not consume an installation record")

    out = Path(args.output_dir).resolve()
    try:
        out.mkdir(parents=True, exist_ok=False)
    except OSError as exc:
        die(f"Cannot create new HTTPS output directory: {exc}")
    # Keep the standard target-directory shape so the common report loader
    # sees the suite verdict, rather than treating it as an empty parent run.
    target_out = out / "https"
    target_out.mkdir()
    report.set_jsonl_path(str(target_out / "run.jsonl"))
    report.set_target("saved-evidence" if action == "audit" else "installation-record")
    report.banner(f"HTTPS campaign: {action}")
    report.plan_result(
        [{"name": NAME, "category": "e2e", "path": "tests/lib/runtests/https.py",
          "run": True}],
        [{"category": "e2e", "suite": NAME, "label": "controlled", "mode": "controlled"}],
        action=action, attempts=1, recovery_commands=False, ordinary_device_setup=False)
    if action == "audit":
        command = [sys.executable, str(root / "tools/audit_https_hardware.py"),
                   str(Path(args.https_evidence).resolve()), "--output", str(out / "audit.json")]
        for peer in args.https_peer:
            command.extend(["--peer", str(Path(peer).resolve())])
    else:
        command = [sys.executable, str(root / "tools/https_hardware_runner.py"),
                   "--plan", str(Path(args.https_plan).resolve()),
                   "--output", str(out / "campaign")]
        if action == "execute":
            command.extend(["--execute", "--installation",
                            str(Path(args.https_installation).resolve())])
    environment = os.environ.copy()
    # Standard run-tests authentication wins; do not record it in argv/evidence.
    environment["U64_PASSWORD"] = args.password
    started = time.monotonic()
    started_wall = time.time()
    code, verdict, status, note = EXIT_SUITE_FAILED, "FAIL", "failed", ""
    try:
        with (target_out / "controlled-https-hardware.log").open("x", encoding="utf-8") as log:
            result = subprocess.run(command, cwd=root, env=environment,
                                    stdout=log, stderr=subprocess.STDOUT, check=False)
        if result.returncode:
            note = f"{action} exited {result.returncode}; see https/controlled-https-hardware.log; no retry"
        elif action == "prepare":
            code, verdict, status = EXIT_OK, "SKIP", "prepared-offline"
            note = "Plan validated offline; hardware suite not executed"
        elif action == "execute":
            verdict, status = "SKIP", "awaiting-independent-peer-audit"
            note = "Exchanges finished; stop the endpoints and audit complete logs; not a pass"
        else:
            code, verdict, status = EXIT_OK, "OK", "saved-evidence-audit-passed"
            note = "Saved bring-up evidence passed audit; full hardware qualification remains held"
    except (OSError, KeyboardInterrupt) as exc:
        note = f"{type(exc).__name__}: {exc}; no retry or recovery"
    fields = dict(action=action, status=status, attempt=1, recoveries=0, mode="controlled",
                  execution_scope="controlled-https", started=started_wall,
                  full_hardware_qualification=False)
    writer = {"OK": report.suite_ok, "FAIL": report.suite_fail, "SKIP": report.suite_skip}[verdict]
    writer(NAME, note, time.monotonic() - started, **fields)
    report.run_result(verdict, suites=1, passed=int(verdict == "OK"),
                      failed=int(verdict == "FAIL"), skipped=int(verdict == "SKIP"),
                      exit_code=code, seconds=time.monotonic() - started, **fields)
    (out / "result.json").write_text(json.dumps(dict(
        **fields, verdict=verdict, exit_code=code, note=note), indent=2) + "\n")
    return code
