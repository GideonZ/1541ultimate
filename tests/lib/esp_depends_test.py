#!/usr/bin/env python3
# Gate check: every cached ESP32 project is watched by the build cache key.

"""Verify software/esp_depends.py covers every ESP32 project CI caches, no device.

The ESP32 build step in .github/workflows/build.yml is skipped on a cache hit,
and the key is a hash of the file list `software/esp_depends.py` prints. So the
two files have to agree: a project whose binaries are *stored* in that cache
but whose sources are *not* in the hash gets its stale binary handed back
forever, and the build step that would have refreshed it never runs.

They did not agree. The list said `wifi/raw_c64`, which does not exist, so no
raw_u64 file was ever hashed while raw_u64's bridge.bin was being cached. A
misspelling was enough because `glob()` on a missing directory returns an empty
list and says nothing about it.

Neither check here needs a device, so this runs at the start of the gate beside
check_transport_usage.py and costs nothing.
"""

import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from report import Failure, check, detail, suite_fail, suite_ok  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOFTWARE_DIR = os.path.join(ROOT, "software")
DEPENDS_SCRIPT = os.path.join(SOFTWARE_DIR, "esp_depends.py")
WORKFLOW = os.path.join(ROOT, ".github", "workflows", "build.yml")

# Paths in the workflow are repo-relative and always name the build directory,
# e.g. "software/wifi/raw_u64/build/bridge.bin". The project is what precedes
# "/build/", minus the leading "software/" that esp_depends.py runs inside of.
CACHED_ARTIFACT = re.compile(r"^\s*software/(\S+?)/build/\S+\s*$", re.MULTILINE)


def cached_projects():
    """The ESP32 projects whose build output CI stores in the ESP32 cache."""
    with open(WORKFLOW, encoding="utf-8") as handle:
        workflow = handle.read()

    # Only the ESP32 cache step; the FPGA caches list .bit files elsewhere.
    start = workflow.find("name: Cache ESP32 Targets")
    if start < 0:
        raise Failure("build.yml has no 'Cache ESP32 Targets' step any more; "
                      "this check needs updating to wherever the ESP32 cache moved")
    end = workflow.find("restore-keys:", start)
    projects = sorted(set(CACHED_ARTIFACT.findall(workflow[start:end])))
    if not projects:
        raise Failure("no software/<project>/build/... paths in the ESP32 cache step")
    return projects


def hashed_projects():
    """The projects esp_depends.py actually contributes files for."""
    result = subprocess.run([sys.executable, DEPENDS_SCRIPT], cwd=SOFTWARE_DIR,
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise Failure(f"esp_depends.py failed: {result.stderr.strip() or result.returncode}")

    # Each line is "<path>: <md5>", with the path relative to software/.
    seen = {}
    for line in result.stdout.splitlines():
        path = line.split(":", 1)[0]
        # Longest project prefix wins: raw_u64/main files belong to raw_u64.
        parts = path.split("/")
        for depth in range(1, len(parts)):
            seen["/".join(parts[:depth])] = seen.get("/".join(parts[:depth]), 0) + 1
    return seen


def run_coverage_check():
    with check("every cached ESP32 project contributes to the cache key"):
        cached = cached_projects()
        hashed = hashed_projects()
        missing = [p for p in cached if not hashed.get(p)]
        if missing:
            raise Failure(
                "cached in CI but absent from the esp_depends.py hash, so a change "
                f"to it can never invalidate its cached binary: {', '.join(missing)}")
        detail("watched: " + ", ".join(f"{p} ({hashed[p]} files)" for p in cached))


def run_missing_directory_check():
    """A directory that is not there has to be an error, not a quiet no-op."""
    with check("a misspelt directory fails instead of silently hashing nothing"):
        with open(DEPENDS_SCRIPT, encoding="utf-8") as handle:
            original = handle.read()
        if "wifi/raw_u64/main" not in original:
            raise Failure("esp_depends.py no longer lists wifi/raw_u64/main; "
                          "this check needs updating")
        # The copy runs from software/ because the globs are relative to it, but
        # it is a copy: a run interrupted here must not leave the real script
        # holding a deliberately broken directory name.
        broken_path = os.path.join(SOFTWARE_DIR, ".esp_depends_missing_dir_check.py")
        try:
            with open(broken_path, "w", encoding="utf-8") as handle:
                handle.write(original.replace("wifi/raw_u64/main",
                                              "wifi/raw_does_not_exist/main"))
            result = subprocess.run([sys.executable, broken_path], cwd=SOFTWARE_DIR,
                                    capture_output=True, text=True)
        finally:
            if os.path.exists(broken_path):
                os.remove(broken_path)
        if result.returncode == 0:
            raise Failure("a missing directory was accepted; the cache key would "
                          "silently stop depending on that project")


def main():
    for path in (DEPENDS_SCRIPT, WORKFLOW):
        if not os.path.isfile(path):
            suite_fail("esp_depends_test", f"missing {path}")
            return 1
    try:
        run_coverage_check()
        run_missing_directory_check()
    except Failure as exc:
        suite_fail("esp_depends_test", str(exc))
        return 1

    detail("a change to any ESP32 project invalidates the cached binaries CI "
           "would otherwise hand back unbuilt")
    suite_ok("esp_depends_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
