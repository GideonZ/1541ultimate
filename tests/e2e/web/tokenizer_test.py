#!/usr/bin/env python3
# E2E: what the BASIC editor of html/index.html uploads, driven in real browsers.

"""The tokenizer tab of the device's home page, checked against what it sends.

The editor turns typed BASIC into the bytes of a program in C64 memory and
posts them to `POST /v1/machine:writemem`. Nothing on the page shows those
bytes, so a keyword that was uploaded as plain characters, a link pointer that
points at the wrong address, or a program that was never sent at all all look
the same in the browser. The checks here read the request instead.

The keyword and special-character tables live in a profile that `html/special.js`
names and `index.html` loads at run time, so the profile is part of what is
under test: a page whose profile did not load has no tables at all.

What the checks cover:

- the profile named by `html/special.js` loads, fills the editor with the sample
  program it carries, and renders the clickable special-character table;
- a program using the tag syntax, the `?` and `goS` keyword abbreviations and
  the `{space*2}` repetition shorthand reaches the device as the exact bytes of
  a BASIC program: link pointer, line number, tokens, and the two zero bytes
  that end the program;
- `{null}` ends the line where it appears, and the rest of that line is dropped,
  which is what the C64's own interpreter does with a zero byte in a line;
- clicking a character in the table inserts its tag at the caret and leaves the
  caret after it, so the next click inserts after the first rather than before;
- an editor holding no numbered line is refused with a message, rather than
  ending in the code that writes the link pointer of a line that is not there;
- a page whose profile never loaded refuses to upload, rather than uploading a
  program in which every keyword became its plain characters.

The device is `device_stub.py`, the same stand-in the other web suites use,
handed `html/` so that it serves the pages as well as the REST calls and can be
asked afterwards what it was sent. The typing and the clicks are what a person
would do.

Host packages: selenium. See tests/requirements.txt. Also needs Chrome or
Firefox installed with their WebDriver on PATH, and a route to the CDN the page
loads jQuery from. Whatever is missing is reported as a skip.
"""

import argparse
import os
import pathlib
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))

import browser as browser_lib  # noqa: E402
from device_stub import DeviceStub  # noqa: E402
# The tokenizer is a tab of the page index_test.py drives, reached through the
# same stub and the same proxy, so it is driven by the same class.
from index_test import Page  # noqa: E402
from report import (Failure, check, check_skip, detail,  # noqa: E402
                    section, suite_fail, suite_ok)

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
PAGES = REPO_ROOT / "html"

READY_TIMEOUT = 20.0

WRITEMEM = "/v1/machine:writemem"

# Where a BASIC program starts on a C64, and what the page sends as the address.
BASIC_START = 0x0801

# A program that exercises what the profile adds: a tag inside a string, the
# `?` and `goS` abbreviations, and the `{space*2}` repetition shorthand.
PROGRAM = '10 print "{clear}hi"\n20 ? "{space*2}a"\n30 goS 100'

# What that program is in memory. Each line is a link pointer to the next line,
# a line number, its bytes and a zero; two zero bytes end the program. PRINT is
# 153, GOSUB is 141, {clear} is 147, and a lower-case letter reaches PETSCII as
# 0x41 upwards, so "hi" is 72, 73.
PROGRAM_BYTES = bytes([
    0x0D, 0x08, 10, 0, 153, 32, 34, 147, 72, 73, 34, 0,      # 10 print "{clear}hi"
    0x19, 0x08, 20, 0, 153, 32, 34, 32, 32, 65, 34, 0,       # 20 ? "{space*2}a"
    0x23, 0x08, 30, 0, 141, 32, 49, 48, 48, 0,               # 30 goS 100
    0, 0,
])

# A zero byte ends a line on a C64 whatever follows it, so the 'b"' is dropped.
NULL_PROGRAM = '10 print "a{null}b"'
NULL_PROGRAM_BYTES = bytes([0x0A, 0x08, 10, 0, 153, 32, 34, 65, 0, 0, 0])

# A character in the table to click, the line it is clicked into, and where the
# caret sits when it is clicked.
CLICKED_TAG = "{clear}"
INSERT_INTO = '10 print "hi"'
INSERT_AT = 10


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--browser", default="all", choices=("all",) + browser_lib.BROWSERS,
                        help="Which browsers to drive. Default: every one installed.")
    parser.add_argument("-t", "--timeout", type=float, default=READY_TIMEOUT,
                        help="How long a page has to become ready.")
    return parser.parse_args()


def expect(label, got, want):
    if got != want:
        raise Failure("%s is %r, expected %r" % (label, got, want))


def wait_for(page, condition):
    """Whether `condition` became true in the page within its timeout."""
    from selenium.webdriver.support.ui import WebDriverWait

    def holds(driver):
        try:
            return bool(driver.execute_script("return %s" % condition))
        except Exception:
            return False

    try:
        WebDriverWait(page.driver, page.timeout).until(holds)
        return True
    except Exception:
        return False


def open_tokenizer(page):
    """Show the tokenizer tab, once the profile it needs has loaded."""
    ready = wait_for(page, "window.CBM_SIGNATURE === 'READY'")
    page.show_page("showtokenizer")
    return ready


def type_program(page, text):
    """Replace whatever is in the editor with `text`, by typing it."""
    editor = page.element("basiceditor")
    editor.clear()
    editor.send_keys(text)


def uploaded(stub, timeout=5.0):
    """The POST that carries the program, or None if none was sent.

    The page writes the variable pointer with a PUT to the same route, so the
    method is what tells the program upload from the write that follows it.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        posts = [call for call in stub.calls_to(WRITEMEM) if call.method == "POST"]
        if posts:
            return posts[0]
        time.sleep(0.02)
    return None


def upload(page, text):
    """Type a program, press Upload, and return the POST it produced."""
    type_program(page, text)
    page.stub.clear()
    page.click("submitBASIC")
    return uploaded(page.stub)


def error_shown(page):
    """The message the editor is showing, or "" when the box is hidden."""
    if not page.visible("basicmsg"):
        return ""
    return page.element("basicmsg").text


def check_the_profile_loads(page):
    """The page reaches its profile, and takes the editor and the table from it."""
    with check("the profile named in special.js loads and fills the editor"):
        if not wait_for(page, "window.CBM_SIGNATURE === 'READY'"):
            raise Failure("the profile never reported READY, so the page has no "
                          "keyword or special character tables at all")
        expect("the number of keywords", page.script("window.TOKENS.length > 0"), True)
        editor = page.script("document.getElementById('basiceditor').value")
        if editor != page.script("window.BASIC_CODE"):
            raise Failure("the editor holds %r, which is not the sample program the "
                          "profile carries" % editor[:60])

    with check("the special character table is rendered and its tags are clickable"):
        count = page.script("document.querySelectorAll('#tableContainer "
                            ".special-token').length")
        if not count:
            raise Failure("the table has no tags in it")
        tagged = page.script("!!document.querySelector('#tableContainer "
                             ".special-token[data-token=\"%s\"]')" % CLICKED_TAG)
        expect("whether %s is offered" % CLICKED_TAG, tagged, True)
        detail("%d tags rendered" % count)


def check_a_program_reaches_the_device(page):
    """Tags, abbreviations and the repetition shorthand reach memory as bytes."""
    with check("a program using tags and abbreviations is uploaded as C64 bytes"):
        call = upload(page, PROGRAM)
        if call is None:
            raise Failure("no program was sent; the page sent %r"
                          % page.stub.rest_calls())
        expect("the address", call.query.get("address"), "%04x" % BASIC_START)
        if call.body != PROGRAM_BYTES:
            raise Failure("the program reached the device as %r, expected %r"
                          % (list(call.body), list(PROGRAM_BYTES)))
        detail("%d bytes, three lines linked from $%04x" % (len(call.body), BASIC_START))


def check_null_ends_the_line(page):
    """A zero byte ends the line, and what follows it on that line is dropped."""
    with check("{null} ends the line and the rest of that line is not uploaded"):
        call = upload(page, NULL_PROGRAM)
        if call is None:
            raise Failure("no program was sent; the page sent %r"
                          % page.stub.rest_calls())
        if call.body != NULL_PROGRAM_BYTES:
            raise Failure("the program reached the device as %r, expected %r"
                          % (list(call.body), list(NULL_PROGRAM_BYTES)))


def check_clicking_a_tag_inserts_it(page):
    """A tag clicked in the table lands at the caret, which then sits after it."""
    from selenium.webdriver.common.by import By

    # Into the middle of a line rather than into an empty editor: setting the
    # value of a textarea leaves the caret at the end of it by itself, so an
    # empty editor would report the caret in the right place however the page
    # got there.
    type_program(page, INSERT_INTO)
    page.driver.execute_script(
        "const e = document.getElementById('basiceditor');"
        "e.focus(); e.setSelectionRange(%d, %d);" % (INSERT_AT, INSERT_AT))

    with check("clicking %s inserts it at the caret, which then sits after it"
               % CLICKED_TAG):
        page.driver.find_element(
            By.CSS_SELECTOR,
            '#tableContainer .special-token[data-token="%s"]' % CLICKED_TAG).click()
        expect("the editor", page.script("document.getElementById('basiceditor').value"),
               INSERT_INTO[:INSERT_AT] + CLICKED_TAG + INSERT_INTO[INSERT_AT:])
        # The caret is what a second click depends on: left where the insert
        # happened to leave it, the next tag lands somewhere else again.
        expect("the caret",
               page.script("document.getElementById('basiceditor').selectionStart"),
               INSERT_AT + len(CLICKED_TAG))


def check_an_editor_without_a_line_number(page):
    """Text with no numbered line is refused, and nothing is sent."""
    with check("an editor holding no numbered line is refused with a message"):
        type_program(page, "hello")
        page.stub.clear()
        page.click("submitBASIC")
        if uploaded(page.stub, timeout=1.0) is not None:
            raise Failure("the page uploaded something for text that holds no "
                          "BASIC line")
        message = error_shown(page)
        if not message:
            raise Failure("nothing was sent and nothing was said: the page ended in "
                          "the code that links the last line, which is not there")
        detail(message)


def check_a_page_without_a_profile(webdriver, browser, timeout, directory):
    """A page whose profile did not load refuses to upload anything."""
    pages = pages_without_a_profile(directory)
    stub = DeviceStub(pages)
    try:
        page = Page(webdriver, browser, stub, timeout)
        try:
            page.open()
            if not page.jquery_reached():
                with check("a page with no profile refuses to upload"):
                    check_skip("this host has no route to the CDN the page loads "
                               "jQuery from")
                return
            page.show_page("showtokenizer")
            with check("a page whose profile did not load refuses to upload"):
                type_program(page, "10 print 1")
                stub.clear()
                page.click("submitBASIC")
                if uploaded(stub, timeout=1.0) is not None:
                    raise Failure("the page uploaded a program with no keyword table "
                                  "loaded, so every keyword in it became its plain "
                                  "characters")
                message = error_shown(page)
                if not message:
                    raise Failure("nothing was sent and nothing was said")
                detail(message)
        finally:
            page.close()
    finally:
        stub.close()


def check_clear_asks_first(page):
    """Clear asks before emptying the editor, and No leaves the text alone."""
    type_program(page, "10 print 1")
    with check("Clear asks first, and answering No leaves the program alone"):
        page.click("clearBASIC")
        if not page.visible("customModalOverlay"):
            raise Failure("Clear emptied the editor without asking")
        page.click("modalNo")
        expect("the editor", page.script("document.getElementById('basiceditor').value"),
               "10 print 1")

    with check("answering Yes empties the editor"):
        page.click("clearBASIC")
        page.click("modalYes")
        expect("the editor", page.script("document.getElementById('basiceditor').value"),
               "")


def pages_without_a_profile(directory):
    """A copy of html/ whose special.js names no profile to load."""
    target = pathlib.Path(directory) / "html"
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(PAGES, target)
    special = target / "special.js"
    text = special.read_text()
    emptied = text.replace('var SCRIPT_TO_LOAD = "cbm_prg_studio.js";',
                           'var SCRIPT_TO_LOAD = "";')
    if emptied == text:
        raise Failure("%s no longer sets SCRIPT_TO_LOAD the way this check empties it, "
                      "so the page it serves would still load a profile" % special)
    special.write_text(emptied)
    return target


def run_browser(webdriver, browser, timeout, directory):
    section(browser)
    stub = DeviceStub(PAGES)
    try:
        page = Page(webdriver, browser, stub, timeout)
        try:
            page.open()
            if not page.jquery_reached():
                with check("index.html reaches the CDN it loads jQuery from"):
                    check_skip("this host has no route to that CDN, and the page "
                               "does nothing at all without it")
                return
            if not open_tokenizer(page):
                with check("the profile named in special.js loads"):
                    raise Failure("the profile never reported READY")
                return
            check_the_profile_loads(page)
            check_a_program_reaches_the_device(page)
            check_null_ends_the_line(page)
            check_clicking_a_tag_inserts_it(page)
            check_an_editor_without_a_line_number(page)
            check_clear_asks_first(page)
        finally:
            page.close()
    finally:
        stub.close()
    check_a_page_without_a_profile(webdriver, browser, timeout, directory)


def run(args, directory):
    webdriver = browser_lib.require_selenium()
    wanted = browser_lib.BROWSERS if args.browser == "all" else (args.browser,)
    drove_one = False
    for browser in wanted:
        here, why = browser_lib.available(webdriver, browser)
        if not here:
            section(browser)
            with check("%s and its WebDriver are installed" % browser):
                check_skip(why)
            continue
        drove_one = True
        run_browser(webdriver, browser, args.timeout, directory)
    if not drove_one:
        raise Failure("no browser this suite can drive is installed")


def main():
    args = parse_args()
    try:
        if not (PAGES / "index.html").exists():
            raise Failure("%s is missing" % PAGES)
        with tempfile.TemporaryDirectory(prefix="web-tokenizer-") as directory:
            run(args, directory)
    except (Failure, browser_lib.Unavailable) as exc:
        suite_fail("web_tokenizer", str(exc))
        return 1
    suite_ok("web_tokenizer")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
