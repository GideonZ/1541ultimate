#!/usr/bin/env python3
"""Cross-run history for the machine-code-monitor debugger matrix.

The matrix already writes a detailed artifact directory per run, but each of
those describes one run in isolation. Deciding whether the debugger is getting
better or worse needs the runs side by side: same commit or not, how many cells
passed, which cells failed and why, and how long it took.

This module keeps that history in one place:

    <root>/history.jsonl          one JSON object per run, appended, newest last
    <root>/HISTORY.md             the same runs as a table, newest first
    <root>/<run_id>/run.json      the full record for a single run
    <root>/<run_id>/run.md        that record in prose

``run_id`` is ``<UTC start timestamp>-<short commit>[-dirty]``, so the folder
name alone identifies when a run happened and what tree it ran against.
"""

from __future__ import annotations

import json
import os
import subprocess
import time
from pathlib import Path
from typing import Any

HISTORY_JSONL = "history.jsonl"
HISTORY_MD = "HISTORY.md"

# Cell statuses that mean the debugger did the right thing.
_GOOD_STATUSES = ("PASS",)
# A cell that never ran. Recorded so a partial run is visible, but it is not a
# failure: counting one would make a crashed run look like a debugger that broke
# in 45 places.
_NOT_RUN_STATUS = "PENDING"
# Statuses that count as this run having found something.
_FAILED_STATUSES = ("FAIL", "BLOCKED_WITH_EVIDENCE")
# A cell this target cannot run at all. Not a failure and not a result: listing
# one in the trend table would report the same six cells as findings on every
# cartridge run.
_NOT_APPLICABLE_STATUS = "SKIPPED_UNSUPPORTED"


def _git(repo_root: Path, *args: str) -> str:
    try:
        out = subprocess.run(("git", *args), cwd=repo_root, capture_output=True,
                             text=True, timeout=30)
        return out.stdout.strip() if out.returncode == 0 else ""
    except Exception:  # noqa: BLE001 - a missing/failing git must not fail a run
        return ""


def git_state(repo_root: Path) -> dict[str, Any]:
    dirty = _git(repo_root, "status", "--porcelain")
    return {
        "commit": _git(repo_root, "rev-parse", "HEAD"),
        "commit_short": _git(repo_root, "rev-parse", "--short", "HEAD"),
        "branch": _git(repo_root, "rev-parse", "--abbrev-ref", "HEAD"),
        "subject": _git(repo_root, "log", "-1", "--format=%s"),
        "dirty": bool(dirty),
        # Porcelain lines are a two-character status field followed by the
        # path. Drop exactly those two columns, then the separator, rather
        # than a fixed slice that eats a character off some status codes.
        "dirty_files": [line[2:].strip() for line in dirty.splitlines() if line.strip()],
    }


def default_root(repo_root: Path) -> Path:
    """Where the history lives unless the caller says otherwise."""
    env = os.environ.get("MCM_RUN_LEDGER")
    if env:
        return Path(env)
    return repo_root / "doc" / "research" / "machine-code-monitor" / "matrix-runs"


def start_run(repo_root: Path, args: Any, artifact_dir: Path) -> dict[str, Any]:
    """Capture everything knowable before the first cell runs."""
    git = git_state(repo_root)
    started = time.time()
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime(started))
    suffix = git.get("commit_short") or "nogit"
    run_id = f"{stamp}-{suffix}" + ("-dirty" if git.get("dirty") else "")
    return {
        "run_id": run_id,
        "git": git,
        "started_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(started)),
        "started_epoch": started,
        "host": getattr(args, "host", None),
        "rest_host": getattr(args, "rest_host", None),
        "reps": getattr(args, "reps", None),
        "memory_selection": getattr(args, "memory", None),
        "ui_selection": getattr(args, "ui", None),
        # An explicit cell set, when one was given. It overrides the two
        # selections above, so a history row that shows only those would
        # describe a run that never happened.
        "cell_selection": getattr(args, "cells", "") or None,
        "required_step_into_depth": getattr(args, "required_step_into_depth", None),
        "opcode_run_target": getattr(args, "opcode_run", None),
        "artifact_dir": str(artifact_dir),
    }


def _failures(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    out = []
    for row in rows:
        if row.get("status") in _GOOD_STATUSES:
            continue
        if row.get("status") == _NOT_APPLICABLE_STATUS:
            continue
        failure = row.get("failure") or {}
        out.append({
            "cell_id": row.get("cell_id"),
            "memory_mode": row.get("memory_mode"),
            "interface": row.get("interface"),
            "repetition": row.get("repetition"),
            "status": row.get("status"),
            "classification": failure.get("classification"),
            # One line is what a trend table can show; the artifact directory
            # keeps the traceback and the screen capture. A cell that never ran
            # has no message at all, so this must not assume a first line.
            "message": next(iter((failure.get("message") or "").splitlines()),
                            "")[:400],
            "failed_op": _first_failed_op(row),
        })
    return out


def _first_failed_op(row: dict[str, Any]) -> str | None:
    """The first flow operation this cell did not complete.

    A cell that never ran leaves every operation PENDING, which is not a failing
    operation - reporting the first one would name an operation that was never
    attempted.
    """
    if row.get("status") == _NOT_RUN_STATUS:
        return None
    for op in ("step_over", "step_into", "step_out", "continue_to_cursor",
               "continue_to_breakpoint", "continue", "reset"):
        value = row.get(op)
        if value not in ("PASS", None, _NOT_RUN_STATUS):
            return op
    return None


def finish_run(root: Path, record: dict[str, Any], rows: list[dict[str, Any]],
               opcode: dict[str, Any], verdict: str,
               exit_code: int) -> Path:
    """Close the record, write the run folder, and update the history files."""
    ended = time.time()
    statuses: dict[str, int] = {}
    for row in rows:
        status = str(row.get("status"))
        statuses[status] = statuses.get(status, 0) + 1

    record = dict(record)
    record.update({
        "ended_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(ended)),
        "duration_seconds": round(ended - record.get("started_epoch", ended), 1),
        "verdict": verdict,
        "exit_code": exit_code,
        "cells_total": len(rows),
        "cells_passed": statuses.get("PASS", 0),
        "cells_failed": statuses.get("FAIL", 0),
        "cells_blocked": statuses.get("BLOCKED_WITH_EVIDENCE", 0),
        "cells_not_run": statuses.get(_NOT_RUN_STATUS, 0),
        "cells_skipped": statuses.get(_NOT_APPLICABLE_STATUS, 0),
        "status_counts": statuses,
        "opcode_status": opcode.get("opcode_requirement_status"),
        "opcode_count": opcode.get("opcode_count"),
        "max_step_into_depth": max((int(r.get("step_into_depth") or 0) for r in rows),
                                   default=0),
        "max_straight_call_depth": max(
            (int(r.get("straight_call_depth") or 0) for r in rows), default=0),
        "failures": _failures(rows),
    })
    record.pop("started_epoch", None)

    # Two runs started in the same second on the same tree would otherwise
    # produce the same folder name and the second would overwrite the first,
    # silently losing a run from the history it exists to preserve.
    run_dir = root / str(record["run_id"])
    if run_dir.exists():
        for suffix in range(2, 1000):
            candidate = root / f"{record['run_id']}-{suffix}"
            if not candidate.exists():
                run_dir = candidate
                record["run_id"] = candidate.name
                break
    run_dir.mkdir(parents=True, exist_ok=True)
    (run_dir / "run.json").write_text(
        json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (run_dir / "run.md").write_text(_run_markdown(record), encoding="utf-8")

    with (root / HISTORY_JSONL).open("a", encoding="utf-8") as fh:
        fh.write(json.dumps(record, sort_keys=True) + "\n")
    _write_history_markdown(root)
    return run_dir


def _run_markdown(record: dict[str, Any]) -> str:
    git = record.get("git", {})
    lines = [
        f"# Matrix run {record['run_id']}",
        "",
        f"- Verdict: **{record.get('verdict')}** (exit code {record.get('exit_code')})",
        f"- Branch: `{git.get('branch')}`",
        f"- Commit: `{git.get('commit_short')}` {git.get('subject') or ''}".rstrip(),
        f"- Tree dirty: {git.get('dirty')}",
        f"- Started: {record.get('started_at')}  Ended: {record.get('ended_at')}"
        f"  Duration: {record.get('duration_seconds')}s",
        f"- Device: `{record.get('host')}` (REST `{record.get('rest_host')}`)",
        (f"- Selection: cells=`{record.get('cell_selection')}`"
         if record.get("cell_selection") else
         f"- Selection: memory=`{record.get('memory_selection')}` "
         f"ui=`{record.get('ui_selection')}` reps=`{record.get('reps')}`"),
        "",
        "## Result",
        "",
        f"- Cells: {record.get('cells_passed')}/{record.get('cells_total')} passed, "
        f"{record.get('cells_failed')} failed, {record.get('cells_blocked')} blocked, "
        f"{record.get('cells_skipped')} not supported on this target, "
        f"{record.get('cells_not_run')} not run",
        f"- Opcode gate: {record.get('opcode_status')} "
        f"({record.get('opcode_count')} opcodes)",
        f"- Deepest Step Into chain: {record.get('max_step_into_depth')}",
        f"- Longest straight-call Step Over run: {record.get('max_straight_call_depth')}",
        f"- Artifacts: `{record.get('artifact_dir')}`",
        "",
    ]
    if git.get("dirty_files"):
        lines += ["## Uncommitted Files At Run Time", ""]
        lines += [f"- `{name}`" for name in git["dirty_files"]]
        lines.append("")
    all_rows = record.get("failures") or []
    not_run = [item for item in all_rows if item.get("status") == _NOT_RUN_STATUS]
    failures = [item for item in all_rows if item.get("status") in _FAILED_STATUSES]
    if not_run:
        lines += [f"## Cells That Never Ran ({len(not_run)})", "",
                  "The run ended before these were reached, so they say nothing "
                  "about the debugger.", ""]
        lines += [f"- {item['cell_id']}" for item in not_run[:10]]
        if len(not_run) > 10:
            lines.append(f"- ... and {len(not_run) - 10} more")
        lines.append("")
    lines += ["## Failures", ""]
    if not failures:
        lines.append("None. Every cell that ran reached PASS.")
    else:
        lines.append("| cell | status | classification | failed op | message |")
        lines.append("| --- | --- | --- | --- | --- |")
        for item in failures:
            lines.append(
                f"| {item['cell_id']} | {item['status']} | "
                f"{item.get('classification') or ''} | "
                f"{item.get('failed_op') or ''} | "
                f"{_md_cell(item.get('message'))} |")
    lines.append("")
    return "\n".join(lines)


def _md_cell(text: str | None) -> str:
    return (text or "").replace("|", "\\|").replace("\n", " ")


def read_history(root: Path) -> list[dict[str, Any]]:
    path = root / HISTORY_JSONL
    if not path.exists():
        return []
    records = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return records


def _write_history_markdown(root: Path) -> None:
    records = read_history(root)
    lines = [
        "# Machine Code Monitor Matrix - Run History",
        "",
        "Newest run first. Each row links to that run's folder, which holds the "
        "full record; the matrix artifact directory named there holds the "
        "per-cell traces and screen captures.",
        "",
        "| run | verdict | pass/total | failed | blocked | opcode gate | depth | "
        "straight | duration | branch | commit | dirty |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for record in reversed(records):
        git = record.get("git", {})
        run_id = record.get("run_id", "")
        lines.append(
            f"| [{run_id}]({run_id}/run.md) | {record.get('verdict')} | "
            f"{record.get('cells_passed')}/{record.get('cells_total')} | "
            f"{record.get('cells_failed')} | {record.get('cells_blocked')} | "
            f"{record.get('opcode_status')} | {record.get('max_step_into_depth')} | "
            f"{record.get('max_straight_call_depth')} | "
            f"{record.get('duration_seconds')}s | {git.get('branch')} | "
            f"`{git.get('commit_short')}` | {'yes' if git.get('dirty') else 'no'} |")
    lines.append("")

    # A recurring failure is the signal worth surfacing without opening any run.
    counts: dict[str, dict[str, Any]] = {}
    for record in records:
        for item in record.get("failures") or []:
            if item.get("status") not in _FAILED_STATUSES:
                continue      # never ran; not something the debugger got wrong
            key = f"{item.get('cell_id')} / {item.get('failed_op') or item.get('status')}"
            entry = counts.setdefault(key, {"runs": 0, "last_message": ""})
            entry["runs"] += 1
            entry["last_message"] = item.get("message") or ""
    if counts:
        lines += ["## Failures Seen Across Runs", "",
                  "| cell / failing op | runs affected | most recent message |",
                  "| --- | --- | --- |"]
        for key, entry in sorted(counts.items(), key=lambda kv: -kv[1]["runs"]):
            lines.append(f"| {key} | {entry['runs']} | "
                         f"{_md_cell(entry['last_message'])} |")
        lines.append("")
    (root / HISTORY_MD).write_text("\n".join(lines), encoding="utf-8")
