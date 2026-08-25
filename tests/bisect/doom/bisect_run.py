#!/usr/bin/env python3
"""Sweep the FPGA bitstreams of a commit range and record the evidence.

Produces a run directory laid out the way `run-tests --record` lays one out: a
directory per subject, a recording and machine-readable metadata inside each,
and one Markdown index over the whole run.

    ./bisect_run.py --range v3.14..v3.15 -o runs/
    ./bisect_run.py --candidates v3.14 c4be69a2 --launches 2

    runs/<stamp>/
      index.md                 the whole run, for a person
      index.json               the whole run, for a program
      <utc commit time>-<commit>-<bitstream blob>/
        video.mp4              25s of the game, with sound
        metadata.json          verdict, measurements, device identity
        deploy.txt             what deploy_commit.sh printed
        launch-N.txt           what each launch measured
        frame0.png             the first captured frame
        mask0.png              which pixels changed, if any did

A sweep rather than a binary search: the defect's rate varies from one launch to
the next, so a candidate can pass by luck, and a bisection that assumes a
monotonic boundary would then step over the answer without saying so. Every
candidate is measured, and the index shows all of them.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parents[1] / "lib"))

REPO = SCRIPT_DIR.parents[2]
VERDICTS = {"GOOD": "pass", "BAD": "corrupt", "BROKEN": "does not run",
            "SETUP_FAILED": "not measured"}


def git(*args, cwd=None):
    return subprocess.run(["git", *args], cwd=cwd or REPO, check=False,
                          capture_output=True, text=True,
                          env={**os.environ, "TZ": "UTC"}).stdout.strip()


def commit_stamp(commit):
    """The commit's own time in UTC, as YYYYMMDDTHHMMSSZ.

    Folders are named with this so that listing the run directory puts the
    candidates in the order the changes were actually made. A bitstream's
    position in a range is not obvious from its hash, and the candidates are not
    all on one branch.
    """
    return git("log", "-1", "--format=%cd",
               "--date=format-local:%Y%m%dT%H%M%SZ", commit) or "00000000T000000Z"


def candidates_for(commit_range):
    """Every commit in the range that introduces a distinct bitstream."""
    out = subprocess.run(["bash", str(SCRIPT_DIR / "list_bitstreams.sh"), commit_range],
                         cwd=REPO, check=True, capture_output=True, text=True).stdout
    return [line.split()[0] for line in out.splitlines() if line.strip()]


def measure(commit, directory, launches, soak_seconds, record_seconds, host, reu):
    """Deploy one candidate, measure it, and record it. Returns its metadata."""
    entry = {"commit": commit,
             "subject": git("log", "-1", "--format=%s", commit),
             "date": git("log", "-1", "--format=%ad", "--date=short", commit),
             "launches": [], "verdict": "SETUP_FAILED", "reason": ""}

    deployed = subprocess.run(
        ["bash", str(SCRIPT_DIR / "deploy_commit.sh"), commit],
        cwd=REPO, capture_output=True, text=True,
        env={**os.environ, "DOOM_HOST": host})
    (directory / "deploy.txt").write_text(deployed.stdout + deployed.stderr)
    line = next((l for l in deployed.stdout.splitlines() if l.startswith("DEPLOYED")), "")
    if not line:
        entry["reason"] = "the bitstream could not be deployed; see deploy.txt"
        return entry
    for field in line.split():
        if field.startswith(("sof=", "blob=")):
            entry[field.split("=")[0]] = field.split("=", 1)[1]
    entry["device"] = line.split(" ", 4)[-1]

    worst_masks = 0
    for launch in range(1, launches + 1):
        run = subprocess.run(
            [sys.executable, str(SCRIPT_DIR / "doom_run.py"), "--host", host,
             "--reu", reu, "--tag", f"{commit}-l{launch}"],
            cwd=REPO, capture_output=True, text=True)
        soak = subprocess.run(
            [sys.executable, str(SCRIPT_DIR / "doom_soak.py"), "--host", host,
             "--seconds", str(soak_seconds), "--label", f"{commit}-l{launch}"],
            cwd=REPO, capture_output=True, text=True)
        text = run.stdout + run.stderr + "\n" + soak.stdout + soak.stderr
        (directory / f"launch-{launch}.txt").write_text(text)

        record = {"launch": launch}
        for source in (run.stdout, soak.stdout):
            for token in source.split():
                key, _, value = token.partition("=")
                if key in ("mapOK", "fps", "frames", "deviating", "distinct_masks",
                           "worst", "bursts", "cam_moved", "live_border"):
                    record[key] = value.rstrip("px%")
        entry["launches"].append(record)

        if "VERDICT: BROKEN" in run.stdout or record.get("mapOK") not in (None, "1"):
            entry["verdict"] = "BROKEN"
            entry["reason"] = "the engine rejected the REU image or never rendered"
            break
        if "SETUP_FAILED" in run.stdout or "SETUP_FAILED" in soak.stdout:
            entry["reason"] = "a launch did not happen; see launch-%d.txt" % launch
            break
        masks = int(record.get("distinct_masks", "0") or 0)
        differing = int(record.get("deviating", "0") or 0)
        ratio = masks / differing if differing else 0.0
        record["ratio"] = f"{ratio:.3f}"
        worst_masks = max(worst_masks, masks)
        if masks >= MIN_CORRUPT_MASKS and ratio >= MIN_CORRUPT_RATIO:
            entry["verdict"] = "BAD"
            entry["reason"] = (f"{masks} distinct change-masks over {differing} "
                               f"differing frames (ratio {ratio:.2f}): the picture "
                               f"changes a different set of pixels nearly every frame")
            break
    else:
        entry["verdict"] = "GOOD"
        entry["reason"] = (f"at most {worst_masks} distinct change-mask over "
                           f"{launches} launches")
    entry["worst_distinct_masks"] = worst_masks

    if entry["verdict"] != "SETUP_FAILED":
        recorded = subprocess.run(
            [sys.executable, str(SCRIPT_DIR / "record_run.py"), "--host", host,
             "--seconds", str(record_seconds), "-o", str(directory / "video.mp4")],
            cwd=REPO, capture_output=True, text=True)
        entry["recording"] = ("video.mp4" if (directory / "video.mp4").exists()
                              else f"failed: {recorded.stderr.strip()[:200]}")

    for name in ("frame0", "mask0"):
        for launch in range(1, launches + 1):
            source = Path(os.path.expanduser("~/.cache/doom-bisect/shots")) / \
                f"{commit}-l{launch}-{name}.png"
            if source.exists():
                shutil.copy(source, directory / f"{name}.png")
                break
    return entry


def write_index(run_dir, entries, meta):
    (run_dir / "index.json").write_text(json.dumps(
        {"run": meta, "candidates": entries}, indent=2) + "\n")

    first_bad = next((e for e in entries if e["verdict"] in ("BAD", "BROKEN")), None)
    lines = [
        "# Doom C64U bitstream sweep", "",
        f"- range: `{meta['range']}`",
        f"- device: {meta['host']}",
        f"- launches per candidate: {meta['launches']}, "
        f"soak {meta['soak_seconds']}s, recording {meta['record_seconds']}s",
        f"- a candidate is BAD when at least {meta['min_corrupt_masks']} distinct "
        f"change-masks appear and they account for at least "
        f"{meta['min_corrupt_ratio']:.0%} of the differing frames", "",
    ]
    if first_bad:
        lines += [f"**First bad bitstream: `{first_bad['commit']}` "
                  f"({first_bad['date']}) {first_bad['subject']}**", ""]
    else:
        lines += ["**No bad bitstream in this range.**", ""]

    lines += ["| candidate | date | bitstream | verdict | most different "
              "pixel sets seen | recording |", "|---|---|---|---|---|---|"]
    for e in entries:
        folder = e["folder"]
        lines.append(
            f"| [`{e['commit']}`]({folder}/) | {e['date']} | `{e.get('blob', '?')}` | "
            f"**{e['verdict']}** ({VERDICTS.get(e['verdict'], '')}) | "
            f"{e.get('worst_distinct_masks', '?')} | "
            f"[video]({folder}/video.mp4) |")

    lines += ["", "## What the numbers mean", "",
              "The player never moves, so every frame of video should look the same.",
              "Each frame is compared against the first one, and for every frame that",
              "differs the harness records *which* pixels differed.",
              "",
              "`different pixel sets` counts how many **different** such sets were seen.",
              "A working machine changes the same pixels over and over, so this stays in",
              "single figures however many frames differ. A machine corrupting data from",
              "the REU changes a different set nearly every frame, so this runs into the",
              "hundreds. `ratio` is that count divided by the number of frames that",
              "differed at all.", "",
              "## Per candidate", ""]
    for e in entries:
        lines += [f"### `{e['commit']}` {e['subject']}", "",
                  f"- {e['date']}, bitstream `{e.get('blob', '?')}`",
                  f"- verdict: **{e['verdict']}** - {e['reason']}",
                  f"- device: `{e.get('device', 'not deployed')}`", ""]
        if e["launches"]:
            lines += ["| launch | fps | frames | frames that differed | "
                      "different pixel sets | ratio | worst pixels |",
                      "|---|---|---|---|---|---|---|"]
            for l in e["launches"]:
                lines.append(
                    f"| {l['launch']} | {l.get('fps', '?')} | {l.get('frames', '?')} | "
                    f"{l.get('deviating', '?')} | {l.get('distinct_masks', '?')} | "
                    f"{l.get('ratio', '-')} | {l.get('worst', '?')} |")
            lines.append("")
    (run_dir / "index.md").write_text("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    ap.add_argument("--range", default="v3.14..v3.15",
                    help="commit range to take bitstream candidates from")
    ap.add_argument("--candidates", nargs="*",
                    help="test these commits instead of the range")
    ap.add_argument("-o", "--out", default=os.path.expanduser("~/.cache/doom-bisect/runs"),
                    help="where the run directory is created")
    ap.add_argument("--launches", type=int, default=3,
                    help="independent launches per candidate; the defect can hide "
                         "for a whole launch")
    ap.add_argument("--soak-seconds", type=float, default=25.0)
    ap.add_argument("--record-seconds", type=float, default=25.0)
    ap.add_argument("--no-record", dest="record", action="store_false")
    ap.add_argument("--min-corrupt-masks", type=int, default=MIN_CORRUPT_MASKS)
    ap.add_argument("--min-corrupt-ratio", type=float, default=MIN_CORRUPT_RATIO)
    ap.add_argument("--reu", default=os.environ.get("DOOM_REU", "/USB2/doom/game.reu"))
    ap.add_argument("--stamp", default=None, help="run directory name")
    args = ap.parse_args()
    globals()["MIN_CORRUPT_MASKS"] = args.min_corrupt_masks
    globals()["MIN_CORRUPT_RATIO"] = args.min_corrupt_ratio

    candidates = args.candidates or candidates_for(args.range)
    stamp = args.stamp or time.strftime("%Y%m%d-%H%M%S")
    run_dir = Path(args.out) / stamp
    run_dir.mkdir(parents=True, exist_ok=True)
    meta = {"range": args.range, "host": args.host, "launches": args.launches,
            "soak_seconds": args.soak_seconds,
            "record_seconds": args.record_seconds if args.record else 0,
            "min_corrupt_masks": args.min_corrupt_masks,
            "min_corrupt_ratio": args.min_corrupt_ratio, "candidates": candidates}

    print(f"run directory: {run_dir}")
    entries = []
    for index, commit in enumerate(candidates, 1):
        blob = git("rev-parse", "--short", f"{commit}:external/u64.sof") or "nosof"
        # <commit time in UTC>-<the commit tested>-<the bitstream it carries>
        folder = f"{commit_stamp(commit)}-{commit}-{blob}"
        directory = run_dir / folder
        directory.mkdir(exist_ok=True)
        print(f"[{index}/{len(candidates)}] {commit} ({blob})", flush=True)
        entry = measure(commit, directory,
                        args.launches, args.soak_seconds,
                        args.record_seconds if args.record else 0,
                        args.host, args.reu)
        entry["folder"] = folder
        entry["blob"] = entry.get("blob", blob)
        (directory / "metadata.json").write_text(json.dumps(entry, indent=2) + "\n")
        entries.append(entry)
        print(f"    -> {entry['verdict']}: {entry['reason']}", flush=True)
        write_index(run_dir, entries, meta)

    write_index(run_dir, entries, meta)
    print(f"\nindex: {run_dir / 'index.md'}")
    return 0


# Corruption is judged from two numbers together, calibrated by sweeping every
# bitstream between v3.14 and v3.15 on an Ultimate 64 Elite:
#
#   candidate                     distinct masks   masks/differing frames
#   v3.14                                      0   no differing frames at all
#   every bitstream up to f2a14e51          1 - 3   0.003 - 0.25
#   c4be69a2                                 503   0.500
#
# The game's own output repeats one pattern however often it recurs, so a low
# ratio is normal however many frames differ. Corrupt REU data alters a
# different set of pixels nearly every frame, which drives the ratio up. Both a
# floor on the count and the ratio are required: a handful of masks appears on
# healthy bitstreams, and a high ratio over three differing frames means nothing.
MIN_CORRUPT_MASKS = 10
MIN_CORRUPT_RATIO = 0.30

if __name__ == "__main__":
    sys.exit(main())
