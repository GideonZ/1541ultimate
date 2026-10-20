#!/usr/bin/env python3
"""SoftIEC partition paths persist in CFG profiles and select their layouts (#982)."""

import argparse
import json
import sys
import time
import uuid
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
sys.path.insert(0, bootstrap.directory("e2e", "filemanager"))
import cfg_fixture  # noqa: E402
import cli  # noqa: E402
import ftp  # noqa: E402
from api import UltimateApi, identify_machine  # noqa: E402
from report import Failure, check, suite_fail, suite_ok, warn  # noqa: E402
from ui_backend import add_mode_argument  # noqa: E402

STORE = "SoftIEC Drive Settings"
ITEM = "Partition config file"
DEFAULT = "/Flash/config/iec_partitions.ipr"
SAVE_TO_FLASH_QUESTION = "Save changes to Flash?"


def partitions(api):
    for entry in api.rest.json("/v1/drives")["drives"]:
        if "IEC Drive" in entry:
            return [{"id": p["id"], "path": p["path"].rstrip("/")}
                    for p in entry["IEC Drive"]["partitions"]]
    raise Failure("SoftIEC drive missing")


def expect_layout(api, path):
    actual = partitions(api)
    expected = [{"id": 1, "path": path}]
    if actual != expected:
        raise Failure(f"partitions {actual!r}, expected {expected!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser)
    parser.add_argument("--telnet-port", type=int, default=23)
    add_mode_argument(parser)
    args = parser.parse_args()
    api = UltimateApi(args.host, args.password or None, args.timeout)
    browser = None
    original = None
    saved_partitions = False
    default_touched = False
    default_contents = None
    names = []
    long_dirs = []
    tag = "p982" + uuid.uuid4().hex[:5]
    restore = tag + "r.ipr"
    saved_cfg = tag + "s.cfg"

    def upload(name, body):
        names.append(name)
        with ftp.session(args.host, args.password) as client:
            ftp.store(client, "/Temp/" + name, body.encode("ascii"))

    def load_ipr(name):
        browser.go_to_directory("Temp")
        browser.select_entry(name)
        browser.invoke_context_action("Load")

    def save_file(category, action, name):
        browser.go_to_directory("Temp")
        browser.invoke_task_action(category, action)
        browser.fill_edit_field(name, clear_taps=7 if action == "Save Partitions" else 0)
        browser.wait_until_gone("Give filename")

    def press_and_wait_for_redraw(key, send=None):
        """Press `key` until the screen changes, then wait for the redraw to finish.

        A key is pressed again only while the screen is exactly as it was, so a key
        that took effect is never repeated. `send` types it instead of `press`.
        """
        for _ in range(3):
            before = browser.rows()
            (send or browser.press)(key)
            deadline = time.monotonic() + 3
            while browser.rows() == before and time.monotonic() < deadline:
                time.sleep(0.15)
            if browser.rows() != before:
                settled = browser.rows()
                time.sleep(0.5)
                while browser.rows() != settled:
                    settled = browser.rows()
                    time.sleep(0.5)
                return
            warn(f"{key} changed nothing on screen; pressing it again")
        raise Failure(f"{key} changed nothing on screen:\n{browser.screen()}")

    def replace_setting_tail_in_menu(item, old, new):
        """Replace the last characters of `item` in the settings menu's string editor.

        The editor opens with its cursor at the end of the value. A Telnet screen
        is not repainted when the editor closes, so the result is read over REST.
        """
        browser.press(identify_machine(args.host, args.password or None, args.timeout).settings_key)
        browser.select_framed_entry(STORE)
        press_and_wait_for_redraw("RIGHT")
        browser.select_framed_entry(item)
        press_and_wait_for_redraw("ENTER")
        for _ in old:
            press_and_wait_for_redraw("BACKSPACE")
        for character in new:
            press_and_wait_for_redraw(character, browser.type_char)
        browser.press("ENTER")
        browser.press("LEFT")
        browser.press("LEFT")
        try:
            browser.wait_for_text(SAVE_TO_FLASH_QUESTION, timeout=3)
        except Failure:
            return  # Auto Save Config is on, so nothing asks
        browser.press_popup_button("n")

    try:
        with check("SoftIEC exposes the persistent partition file setting"):
            original = api.configs.category(STORE)
            if ITEM not in original:
                raise Failure(f"{STORE} has no {ITEM!r}: {original!r}")

        browser = cfg_fixture.browser_for(args)
        names.extend([restore, saved_cfg])
        save_file("Software IEC", "Save Partitions", restore)
        saved_partitions = True
        # Short FTP commands build a path of the setting's maximum length, 255 characters.
        long_dirs = [tag, "a" * 60, "b" * 60, "c" * 60, "d" * 45]
        long_ipr = "/".join([*long_dirs, "layout.ipr"])
        if len("/Temp/" + long_ipr) != 255:
            raise Failure(f"long path fixture is {len('/Temp/' + long_ipr)} characters, not 255")
        short_ipr = tag + "b.ipr"
        with ftp.session(args.host, args.password) as client:
            client.cwd("/Temp")
            for directory in long_dirs:
                client.mkd(directory)
                client.cwd(directory)
            ftp.store(client, "layout.ipr", json.dumps({"version": 1, "partitions": [
                {"number": 1, "path": "/Temp", "name": "PROFILE"}]}).encode("ascii"))
            # Independently verify the fixture before the configuration loader uses it.
            if json.loads(ftp.retrieve(client, "layout.ipr"))["partitions"][0]["path"] != "/Temp":
                raise Failure("long-path IPR fixture could not be read back")
        upload(short_ipr, json.dumps({"version": 1, "partitions": [
            {"number": 1, "path": "/Flash", "name": "PROFILE"}]}))

        with check("CFG profiles select their partition layout through Reboot C64"):
            for suffix, ipr, root in (("a", long_ipr, "/Temp"),
                                      ("b", short_ipr, "/Flash")):
                name = tag + suffix + ".cfg"
                upload(name, f"[{STORE}]\n{ITEM}=/Temp/{ipr}\n")
                cfg_fixture.load(browser, name)
                api.machine.reboot()
                expect_layout(api, root)
                if api.configs.get(STORE, ITEM) != "/Temp/" + ipr:
                    raise Failure("profile path was not applied intact")

        with check("Save to File and Load Settings round-trip a long partition path"):
            api.configs.set(STORE, ITEM, "/Temp/" + long_ipr)
            expect_layout(api, "/Temp")
            save_file("Configuration", "Save to File", saved_cfg)
            with ftp.session(args.host, args.password) as client:
                contents = ftp.retrieve(client, "/Temp/" + saved_cfg).decode("ascii")
            if f"{ITEM}=/Temp/{long_ipr}\n" not in contents:
                raise Failure("saved CFG does not contain the complete partition path")
            api.configs.set(STORE, ITEM, "/Temp/" + short_ipr)
            cfg_fixture.load(browser, saved_cfg)
            api.machine.reboot()
            expect_layout(api, "/Temp")

        with check("the settings menu edits a 255-character path without truncating it"):
            replace_setting_tail_in_menu(ITEM, "ipr", "cfg")
            value = api.configs.get(STORE, ITEM)
            if value != "/Temp/" + long_ipr[:-3] + "cfg":
                raise Failure(f"the menu stored {len(value)} characters: {value!r}")

        with check("unrelated settings and drive resets preserve manual IPR loads"):
            # Start from a path that loads, so a reload of it would show.
            api.configs.set(STORE, ITEM, "/Temp/" + long_ipr)
            expect_layout(api, "/Temp")
            load_ipr(short_ipr)
            expect_layout(api, "/Flash")
            api.configs.set(STORE, "Log Every Operation", "Enabled")
            code, _, body = api.rest.request("PUT", "/v1/drives/softiec:reset")
            if code != 200:
                raise Failure(f"SoftIEC reset returned HTTP {code}: {body!r}")
            expect_layout(api, "/Flash")

        with check("missing and invalid IPR files preserve the active layout"):
            api.configs.set(STORE, ITEM, "/Temp/" + tag + "missing.ipr")
            expect_layout(api, "/Flash")
            invalid = tag + "i.ipr"
            upload(invalid, '{"partitions":[]}')
            api.configs.set(STORE, ITEM, "/Temp/" + invalid)
            expect_layout(api, "/Flash")

        with check("a repaired file is read when its path is applied again, not by other settings"):
            with ftp.session(args.host, args.password) as client:
                ftp.store(client, "/Temp/" + invalid, json.dumps({"version": 1, "partitions": [
                    {"number": 1, "path": "/Temp", "name": "REPAIRED"}]}).encode("ascii"))
            api.configs.set(STORE, "Log Every Operation", "Disabled")
            expect_layout(api, "/Flash")
            api.configs.set(STORE, ITEM, "/Temp/" + tag + "missing.ipr")
            api.configs.set(STORE, ITEM, "/Temp/" + invalid)
            expect_layout(api, "/Temp")

        with check("an empty path selects the standard partition file"):
            with ftp.session(args.host, args.password) as client:
                if api.files.info(DEFAULT) is not None:
                    default_contents = ftp.retrieve(client, DEFAULT)
                    default_touched = True
                    client.delete(DEFAULT)
            api.configs.set(STORE, ITEM, "")
            expect_layout(api, "/Temp")
            with ftp.session(args.host, args.password) as client:
                default_touched = True
                ftp.store(client, DEFAULT, json.dumps({"version": 1, "partitions": [
                    {"number": 1, "path": "/Flash", "name": "DEFAULT"}]}).encode("ascii"))
            # The standard file is read when the path is cleared, not by unrelated settings.
            api.configs.set(STORE, "Log Every Operation", "Enabled")
            expect_layout(api, "/Temp")
            api.configs.set(STORE, ITEM, "/Temp/" + invalid)
            api.configs.set(STORE, ITEM, "")
            expect_layout(api, "/Flash")

        suite_ok("partition_config_test")
        return 0
    except Exception as exc:
        suite_fail("partition_config_test", str(exc))
        return 1
    finally:
        # Let cleanup failures fail the process as well as the feature checks.
        if default_touched:
            with ftp.session(args.host, args.password) as client:
                if default_contents is None:
                    client.delete(DEFAULT)
                else:
                    ftp.store(client, DEFAULT, default_contents)
        if original is not None and ITEM in original:
            api.configs.apply({STORE: original})
        if saved_partitions:
            load_ipr(restore)
        if browser is not None:
            browser.close()
        cfg_fixture.cleanup(args.host, args.password, *names)
        if long_dirs:
            with ftp.session(args.host, args.password) as client:
                client.cwd("/Temp")
                for directory in long_dirs:
                    client.cwd(directory)
                client.delete("layout.ipr")
                for directory in reversed(long_dirs):
                    client.cwd("..")
                    client.rmd(directory)


if __name__ == "__main__":
    raise SystemExit(main())
