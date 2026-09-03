#!/usr/bin/env python3
# What the four cfg_* suites all do: upload a .cfg, load it, clean up.

"""The fixture the CFG loading suites share.

Four suites drive the same three device steps and differ only in what the .cfg
says and what they then assert:

  - cfg_single_group_test.py: a file naming one store loads;
  - cfg_unknown_items_test.py: a file naming things this machine has not still
    loads, and the settings it could apply were applied;
  - cfg_whitespace_test.py: an unpadded value matches its padded label;
  - cfg_partial_effectuate_test.py: only the named store is effectuated, which
    is manual because the firmware behaviour is in disrepair.

`upload` and `cleanup` were byte-identical copies in three of them, and `load`
differed only in whether it bracketed the load with the debug log. They live
here so that a change to the upload path is one edit rather than four, and so
the manual suite cannot drift away from the gate suite it shares a fixture
with.

Each suite keeps its own file names, its own .cfg text, its own assertions and
its own profile, so a run that dies part-way leaves a file its own next run
overwrites rather than one shared across four. The exception is
cfg_partial_effectuate_test.py, which deliberately reuses
cfg_single_group_test.py's fixture and names because it asserts a second thing
about the same load. This is the fixture, not the test.
"""

from __future__ import annotations

import ftp as ftp_lib
from report import best_effort

# The transfer is a few hundred bytes; a device that cannot finish it in twenty
# seconds has a problem the suite should report rather than wait out.
FTP_TIMEOUT_SECONDS = 20.0


def upload(host: str, password: str, name: str, body: str) -> None:
    """Put the .cfg where the browser can see it."""
    with ftp_lib.session(host, password, timeout=FTP_TIMEOUT_SECONDS) as ftp:
        ftp_lib.store(ftp, f"/Temp/{name}", body.encode("ascii"))


def load(browser, name: str, log_name: str | None = None) -> None:
    """Load the uploaded .cfg through the real browser action.

    `wait_for_text` is the assertion that matters and is here rather than in
    the callers: before the change these suites cover, a file with an unknown
    item answered "There were errors." and put the log in an editor, so
    reaching the success popup at all is the behaviour under test.

    `log_name` brackets the load with the device's debug log, which is the
    external record of which stores the loader considered. Only the suites
    that read that record pay for it.
    """
    if log_name:
        browser.invoke_task_action("Developer", "Clear Debug Log")
    browser.go_to_directory("Temp")
    browser.select_entry(name)
    browser.invoke_context_action("Load Settings")
    browser.wait_for_text("Loading configuration successful!")
    browser.press_popup_button("o")
    if log_name:
        browser.invoke_task_action("Developer", "Save Debug Log")
        browser.fill_edit_field(log_name)


def cleanup(host: str, password: str, *names: str) -> None:
    """Remove the files a run can leave behind, whatever else went wrong."""
    def remove() -> None:
        with ftp_lib.session(host, password,
                             timeout=FTP_TIMEOUT_SECONDS) as ftp:
            for name in names:
                if name:
                    ftp_lib.delete_quietly(ftp, f"/Temp/{name}")

    best_effort("remove the fixtures this run uploaded", remove)
