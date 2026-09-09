#!/usr/bin/env python3
"""Hardware regression for #655 via the C64 KERNAL's real IEC bus.

Requires a C64 with standard KERNAL, REST/FTP, Software IEC, and an empty drive A.
Tests FAT, D64 and D81 destinations; record lengths 1, 31, 164 and 254; channel
reuse; short-record padding; reopen/read; and emulated-drive interoperability.
Uses only generated fixtures. Saves byte evidence with --evidence-dir.
"""
import argparse
import json
import sys
import time
import traceback
import uuid
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import ftp  # noqa: E402
from api import UltimateApi  # noqa: E402
from config_snapshot import Snapshot  # noqa: E402
from iec_agent import Agent  # noqa: E402
from report import Failure, check, detail, section, suite_fail, suite_ok, teardown_step  # noqa: E402

SUITE = "rel_copy_test"


def require_equal(actual, expected, context):
    if actual != expected:
        first = next((i for i, (a, b) in enumerate(zip(actual, expected)) if a != b), None)
        raise Failure(f"{context}: got {len(actual)} bytes, expected {len(expected)}; first mismatch {first}")


def run(args):
    api = UltimateApi(args.host, args.password, args.timeout)
    agent = Agent(api)
    evidence = Path(args.evidence_dir)
    evidence.mkdir(parents=True, exist_ok=True)
    info = api.rest.json("/v1/info")
    original_drives = api.rest.json("/v1/drives")
    drives = {name: value for entry in original_drives["drives"] for name, value in entry.items()}
    if drives["a"]["image_file"]:
        raise Failure("Drive A must be empty; refusing to displace a mounted image")
    if any(d.get("enabled") and d.get("bus_id") in (10, 11)
           for name, d in drives.items() if name not in ("a", "IEC Drive")):
        raise Failure("Device 10 or 11 is already in use")
    saved = Snapshot(args.host, {name: api.configs.category(name) for name in
                                ("Drive A Settings", "SoftIEC Drive Settings")})
    old_parts = drives["IEC Drive"]["partitions"]
    if len(old_parts) != 1 or old_parts[0]["id"] != 1:
        raise Failure("This test currently requires one Software IEC partition, numbered 1")
    original_path = old_parts[0]["path"]
    (evidence / "before.json").write_text(json.dumps({"info": info, "drives": original_drives,
                                                     "configs": saved.settings}, indent=2))
    detail(json.dumps(info))
    folder = "rel" + uuid.uuid4().hex[:8]
    root = None
    created = []
    failed = False
    mounted = False
    started = False
    try:
        api.configs.set("SoftIEC Drive Settings", "Soft Drive Bus ID", 11)
        api.configs.set("SoftIEC Drive Settings", "IEC Drive", "Enabled")
        agent.start()
        started = True
        agent.call(1, 15)
        agent.status((0, 73))
        agent.command("CD//")
        current = api.rest.json("/v1/drives")
        root = next(e["IEC Drive"]["partitions"][0]["path"] for e in current["drives"] if "IEC Drive" in e)
        if not original_path.startswith(root):
            raise Failure("Cannot restore Software IEC path relative to its partition root")
        path = root.rstrip("/") + "/" + folder
        with ftp.session(args.host, args.password) as client:
            client.mkd(path)
        created.append((path, True))
        for kind in ("fat", "d64", "d81"):
            section(kind)
            target = path
            relative = folder
            if kind != "fat":
                target = f"{path}/case.{kind}"
                getattr(api.files, "create_" + kind)(target)
                created.append((target, False))
                relative += f"/case.{kind}"
            agent.command("CD//" + relative.upper())
            fixtures = []
            for index, size in enumerate((164, 31, 1, 254, 31)):
                name = f"COPY{index}"
                first = bytes(33 + i % 80 for i in range(size))
                second = bytes([97 + index]) * max(1, size - 3)
                expected = bytes([size, 0]) + first + second.ljust(size, b"\0")
                remote = target + "/" + name + ".rel"
                if kind == "fat":
                    created.append((remote, False))
                with check(f"{kind}: sequential writes, {size}-byte records, reused channel ({name})"):
                    agent.call(1, data=name.encode() + b",L," + bytes([size]))
                    agent.status()
                    for record in (first, second):
                        agent.call(2, data=record)
                        agent.status()
                    agent.call(4)
                    agent.status()
                with ftp.session(args.host, args.password) as client:
                    actual = ftp.retrieve(client, remote)
                (evidence / f"{kind}-{name}.actual.rel").write_bytes(actual)
                (evidence / f"{kind}-{name}.expected.rel").write_bytes(expected)
                try:
                    with check(f"{kind}: header, exact size, contents and padding ({name})"):
                        require_equal(actual, expected, name)
                except Failure:
                    failed = True
                else:
                    fixtures.append((name, size, first, second))
            # All files are created before any reopen, matching a copy utility.
            for name, _size, first, second in fixtures:
                with check(f"{kind}: reopen by name, position and read over Software IEC ({name})"):
                    agent.call(1, data=name.encode())
                    agent.status()
                    agent.command(b"P\x05\x01\x00\x01")
                    require_equal(agent.call(3), first, name + " record 1")
                    require_equal(agent.call(3), second, name + " record 2")
                    agent.call(4)
                    agent.status()
            agent.command("CD//")
            if kind != "fat" and len(fixtures) == 5:
                api.configs.set("Drive A Settings", "Drive Bus ID", 10)
                api.drives.set_mode("a", "1541" if kind == "d64" else "1581")
                api.drives.on("a")
                api.drives.mount("a", target, type=kind, mode="readonly")
                mounted = True
                time.sleep(2)
                for name, _size, first, second in fixtures:
                    with check(f"{kind}: read copied records through emulated drive 10 ({name})"):
                        agent.call(1, data=name.encode() + b",L", device=10)
                        require_equal(agent.call(3, device=10), first, name + " record 1")
                        require_equal(agent.call(3, device=10), second, name + " record 2")
                        agent.call(4, device=10)
                api.drives.remove("a")
                mounted = False
        if agent.overruns:
            detail(f"{agent.overruns} transactions needed longer than the estimated transfer time")
        return not failed
    finally:
        def restore_iec():
            if started:
                agent.call(4)
                if root is not None:
                    agent.command("CD//" + original_path[len(root):].upper())
                agent.call(4, 15)
        def restore_config():
            if mounted:
                api.drives.remove("a")
            _, refused = saved.restore(api)
            if refused:
                raise Failure(f"Settings restoration refused: {refused}")
            after = api.rest.json("/v1/drives")
            (evidence / "after.json").write_text(json.dumps(after, indent=2))
            path_after = next(e["IEC Drive"]["partitions"][0]["path"] for e in after["drives"] if "IEC Drive" in e)
            if path_after.casefold() != original_path.casefold():
                raise Failure(f"Software IEC path not restored: {path_after}")
        def remove_fixtures():
            with ftp.session(args.host, args.password) as client:
                for name, directory in reversed(created):
                    parent, _, leaf = name.rpartition("/")
                    if leaf in ftp.names(client, parent):
                        (client.rmd if directory else client.delete)(name)
        cleanup_ok = True
        for label, action in (("restore IEC working directory", restore_iec),
                              ("restore drive settings", restore_config),
                              ("remove only this run's fixtures", remove_fixtures),
                              ("return C64 to BASIC", lambda: api.machine.reset(force=True))):
            cleanup_ok = teardown_step(label, action) and cleanup_ok
        if not cleanup_ok:
            raise Failure("Hardware test cleanup incomplete")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser)
    parser.add_argument("--evidence-dir", default="rel-copy-evidence")
    args = parser.parse_args()
    try:
        if not run(args):
            raise Failure("Copied REL files differ from expected bytes")
    except Exception as exc:
        traceback.print_exc()
        suite_fail(SUITE, str(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
