#!/usr/bin/env python3
"""Copy/paste names, mixed drive/browser writes and overlay exit (issue 145)."""

import argparse
import io
import json
import sys
import time
import uuid
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import ftp  # noqa: E402
from api import UltimateApi  # noqa: E402
from report import Failure, check, suite_fail, suite_ok, teardown_step  # noqa: E402
from ui_backend import make_browser, plan_overlay_navigation  # noqa: E402
from prg_context_menu_test import build_d64, sector_offset  # noqa: E402

sys.path.insert(0, bootstrap.directory("e2e", "io", "iec"))
from iec_agent import Agent  # noqa: E402

PAYLOADS = {"DREHER": b"\x01\x08plain", "DREHER.INFO": b"\x01\x08dotted",
            "A.B.C": b"\x01\x08multiple dots", "TAIL.PRG": b"\x01\x08type-like suffix"}
# petscii_to_fat appends {} to a native name ending in a CBM file-type suffix.
HOST_NAMES = {name: name + ("{}" if name == "TAIL.PRG" else "") + ".prg" for name in PAYLOADS}
EMPTY_PRG = b"\x01\x08\x00\x00"


def source_image():
    image = bytearray(build_d64("COPY TEST", "DREHER", PAYLOADS["DREHER"]))
    directory = sector_offset(18, 1)
    bam = sector_offset(18, 0) + 17 * 4
    for index, (name, data) in enumerate(list(PAYLOADS.items())[1:], 1):
        entry = directory + index * 32
        image[entry + 2:entry + 5] = bytes([0x82, 17, index])
        image[entry + 5:entry + 21] = name.encode().ljust(16, b"\xa0")
        image[entry + 30] = 1
        start = sector_offset(17, index)
        image[start:start + 2] = bytes([0, len(data) + 1])
        image[start + 2:start + 2 + len(data)] = data
        image[bam] -= 1
        image[bam + 1] &= ~(1 << index)
    return bytes(image)


def image_files(image):
    """Read file chains independently and require unique, allocated sectors."""
    result = {}
    used = set()
    track, sector = 18, 1
    directories = set()
    while track:
        if (track, sector) in directories:
            raise Failure("directory chain cycles")
        directories.add((track, sector))
        start = sector_offset(track, sector)
        track, sector = image[start:start + 2]
        for slot in range(8):
            entry = start + slot * 32
            if not image[entry + 2]:
                continue
            name = image[entry + 5:entry + 21].rstrip(b"\xa0").decode("ascii")
            if name in result:
                raise Failure(f"duplicate directory name {name!r}")
            t, s = image[entry + 3:entry + 5]
            data = bytearray()
            while t:
                if (t, s) in used:
                    raise Failure(f"{name}: sector {t}/{s} belongs to multiple file chains")
                used.add((t, s))
                bam = sector_offset(18, 0) + t * 4 + 1 + s // 8
                if image[bam] & (1 << (s % 8)):
                    raise Failure(f"{name}: sector {t}/{s} is marked free in BAM")
                block = sector_offset(t, s)
                t, s = image[block:block + 2]
                data += image[block + 2:block + (s + 1 if not t else 256)]
            result[name] = bytes(data)
    return result


def require_files(actual, expected):
    if actual != expected:
        raise Failure(f"file names/contents: {actual!r}; expected {expected!r}")


def activate(browser, api, action):
    labels = browser.open_context_menu()
    prefix, delta = plan_overlay_navigation(labels, action)
    for character in prefix:
        browser.type_menu_char(character)
    browser.press_many("DOWN" if delta > 0 else "UP", abs(delta))
    browser._settle_overlay_selection(labels, action)
    api.machine.press("return")


def require_menu(api, opened):
    deadline = time.monotonic() + 5
    while (api.machine.menu_screen() is not None) != opened:
        if time.monotonic() >= deadline:
            raise Failure(f"expected overlay {'open' if opened else 'closed'}")
        time.sleep(.1)


def copy(browser, source, destination, names, allow_refusal=False):
    browser.go_to_directory(source)
    for name in names:
        browser.select_entry(name)
        browser.type_char(" ")
    browser.press("COPY")
    browser.wait_for_text("clipboard")
    browser.press_popup_button("o")
    browser.go_to_directory(destination)
    browser.press("PASTE")
    deadline = time.monotonic() + 20
    while "Copying..." in browser.screen() and time.monotonic() < deadline:
        time.sleep(.1)
    if "Copy error" in browser.screen():
        browser.press_popup_button("n")
        if allow_refusal:
            return False
        raise Failure("copy/paste reported a copy error")
    return True


def run(args):
    api = UltimateApi(args.host, args.password, args.timeout)
    evidence = args.evidence_dir
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / "info.json").write_text(json.dumps(api.rest.json("/v1/info"), indent=2))
    drives = {k: v for entry in api.rest.json("/v1/drives")["drives"] for k, v in entry.items()}
    if drives["a"]["image_file"]:
        raise Failure("Drive A must be empty")
    saved = api.configs.category("Drive A Settings")
    folder = "c145" + uuid.uuid4().hex[:4]
    path = "/Temp/" + folder
    browser = None

    def fetch(name):
        with ftp.session(args.host, args.password) as client:
            data = ftp.retrieve(client, path + "/" + name)
        (evidence / name.replace("/", "-")).write_bytes(data)
        return data

    try:
        api.machine.reset(force=True, wait=False)
        time.sleep(.5)
        with ftp.session(args.host, args.password) as client:
            client.mkd(path)
            client.mkd(path + "/host")
            client.storbinary("STOR " + path + "/source.d64", io.BytesIO(source_image()))
            client.storbinary("STOR " + path + "/empty.prg", io.BytesIO(EMPTY_PRG))
            client.storbinary("STOR " + path + "/run.d64",
                              io.BytesIO(build_d64("RUN TEST", "EMPTY", EMPTY_PRG)))
        api.files.create_d64(path + "/target.d64")
        browser = make_browser("overlay", args.host, args.password, args.timeout,
                               entry_rows=range(2, 24), status_row=24)
        relative = "Temp/" + folder
        if args.case in ("names-image", "names-host"):
            destination = relative + ("/target.d64" if args.case == "names-image" else "/host")
            with check(args.case + ": multi-selection preserves dotted names and distinct payloads"):
                copy(browser, relative + "/source.d64", destination, list(PAYLOADS))
                if args.case == "names-image":
                    require_files(image_files(fetch("target.d64")), PAYLOADS)
                else:
                    with ftp.session(args.host, args.password) as client:
                        names = set(ftp.names(client, path + "/host"))
                    expected = set(HOST_NAMES.values())
                    if names != expected:
                        raise Failure(f"host names {names!r}; expected {expected!r}")
                    for name, data in PAYLOADS.items():
                        if fetch("host/" + HOST_NAMES[name]) != data:
                            raise Failure(f"{name}: incorrect host payload")
        elif args.case == "mixed":
            api.configs.set("Drive A Settings", "Drive Bus ID", 8)
            api.drives.on("a")
            api.drives.mount("a", path + "/target.d64", type="d64", mode="readwrite")
            agent = Agent(api)
            agent.start()
            browser.backend.ensure_ready()
            # Cache the BAM after mounting, while the drive keeps its handle open.
            browser.go_to_directory(relative + "/target.d64")
            api.machine.close_menu_from_anywhere()
            payload = b"\x01\x08" + bytes(range(200))
            with check("C64 writes a PRG through the emulated 1541"):
                agent.call(1, data=b"DRIVE,P,W", device=8)
                agent.call(2, data=payload, device=8)
                agent.call(4, device=8)
                time.sleep(3)
                require_files(image_files(fetch("target.d64")), {"DRIVE": payload})
            browser.backend.ensure_ready()
            with check("paste while mounted preserves C64 data and BAM allocations"):
                copied = copy(browser, relative + "/source.d64", relative + "/target.d64",
                              ["DREHER"], allow_refusal=True)
                expected = {"DRIVE": payload}
                if copied:
                    expected["DREHER"] = PAYLOADS["DREHER"]
                require_files(image_files(fetch("target.d64")), expected)
            with check("eject permits paste, and remount retains both files"):
                api.drives.remove("a")
                if not copied:
                    copy(browser, relative + "/source.d64", relative + "/target.d64", ["DREHER"])
                api.drives.mount("a", path + "/target.d64", type="d64", mode="readwrite")
                require_files(image_files(fetch("target.d64")),
                              {"DRIVE": payload, "DREHER": PAYLOADS["DREHER"]})
        elif args.case == "overlay":
            for leave, filename in (("No", "target.d64"), ("Yes", "run.d64")):
                with check(f"Mount Disk obeys Leave Menu on Mount={leave}"):
                    api.configs.set("Drive A Settings", "Leave Menu on Mount", leave)
                    browser.backend.ensure_ready()
                    browser.go_to_directory(relative)
                    browser.select_entry(filename)
                    activate(browser, api, "Mount Disk")
                    deadline = time.monotonic() + 5
                    while api.drives.list()["a"].image_file != filename:
                        if time.monotonic() >= deadline:
                            raise Failure(f"Mount Disk did not mount {filename}")
                        time.sleep(.1)
                    require_menu(api, leave == "No")
            for filename, action in (("empty.prg", "Run"), ("empty.prg", "Load"),
                                     ("run.d64", "Run Disk")):
                with check(f"{action} closes the HDMI overlay"):
                    browser.backend.ensure_ready()
                    browser.go_to_directory(relative)
                    browser.select_entry(filename)
                    activate(browser, api, action)
                    require_menu(api, False)
    finally:
        teardown_step("remove test image from drive A", lambda: api.drives.remove("a"))
        if browser:
            teardown_step("close browser", browser.close)
        teardown_step("restore Drive A settings", lambda: api.configs.apply({"Drive A Settings": saved}))
        teardown_step("reset C64 to BASIC", lambda: api.machine.reset(force=True, wait=False))
        def cleanup():
            with ftp.session(args.host, args.password) as client:
                ftp.remove_tree(client, path)
        teardown_step("remove generated fixtures", cleanup)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser, timeout=12, colour=False)
    parser.add_argument("--case", choices=("all", "names-image", "names-host", "mixed", "overlay"), default="all")
    parser.add_argument("--evidence-dir", type=Path, default=Path("/tmp/image-copy-evidence"))
    args = parser.parse_args()
    try:
        cases = ("names-image", "names-host", "mixed", "overlay") if args.case == "all" else (args.case,)
        for case in cases:
            args.case = case
            run(args)
        suite_ok("image_copy_test")
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail("image_copy_test", str(exc))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
