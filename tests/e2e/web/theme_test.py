#!/usr/bin/env python3
# E2E: the web UI's light/dark theme, driven in real browsers. Needs no device.

"""What the two pages the device serves must do about their theme.

`html/index.html` and `html/api.html` each follow the browser's own
`prefers-color-scheme` setting, offer a button to override it, and remember an
overridden choice for the rest of the browser tab. The rules under test:

- a page loaded with no remembered choice follows the browser's setting and
  stores nothing, so a later change of that setting still takes effect;
- a choice made with the theme buttons is stored, and the other page picks it up;
- a browser set to block on-device site data still gets both pages, working.
  `sessionStorage` throws a SecurityError there, and each page reads it above the
  code that installs its handlers, so an unguarded read costs the whole page;
- the inline script of `api.html` runs at all, which it only does if the SHA-256
  digest in that page's own Content-Security-Policy matches the script beside it.
  A stale digest is refused silently by both engines, with no console error, and
  `tools/openapi/test_explorer.py` is the only other thing that catches it.

Every condition is a real browser setting rather than anything injected into the
page: `profile.default_content_setting_values.cookies` and
`network.cookie.cookieBehavior` for blocked site data, CDP `setEmulatedMedia` and
`ui.systemUsesDarkTheme` for the colour-scheme preference. The clicks and the
navigation are what a person would do.

The pages are served from a local static server over HTTP, so the suite needs no
device. `--base-url` points it at a running device instead, which additionally
establishes that the firmware on that device serves these pages.

Host packages: selenium. See tests/requirements.txt. Also needs Chrome or Firefox
installed with their WebDriver on PATH, and, for `index.html`, a route to the
CDN that page loads jQuery from. Whatever is missing is reported as a skip.
"""

import argparse
import pathlib
import sys
from pathlib import Path

# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
sys.path.insert(0, bootstrap.directory("e2e", "web"))

import browser as browser_lib  # noqa: E402
from report import (Failure, check, check_skip, detail,  # noqa: E402
                    section, suite_fail, suite_ok)

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
PAGES = REPO_ROOT / "html"

# How long a page has to finish applying its theme. index.html does it inside a
# jQuery ready handler, after fetching jQuery.
READY_TIMEOUT = 20.0


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--base-url", default="",
                        help="Where the pages are served from, e.g. http://ultimate64. "
                             "Default: serve html/ from this tree.")
    parser.add_argument("--browser", default="all", choices=("all", *browser_lib.BROWSERS),
                        help="Which browsers to drive. Default: every one installed.")
    parser.add_argument("-t", "--timeout", type=float, default=READY_TIMEOUT,
                        help="How long a page has to become ready.")
    return parser.parse_args()


class Session:
    """One browser, configured the way a reader's browser would be configured."""

    def __init__(self, webdriver, browser, base_url, timeout, dark=False, block_site_data=False):
        self.browser = browser
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.page = ""
        self.driver = browser_lib.make_driver(webdriver, browser, dark=dark,
                                              block_site_data=block_site_data)

    def close(self):
        self.driver.quit()

    def open(self, page):
        self.page = page
        self.driver.get("%s/%s" % (self.base_url, page))
        self.await_ready(page)

    def await_ready(self, page):
        """Wait until the page has applied a theme, or give up quietly."""
        from selenium.webdriver.support.ui import WebDriverWait
        probe = ("return document.body.className !== ''" if page == "index.html"
                 else "return typeof setDarkMode !== 'undefined'"
                      " || document.documentElement.className !== ''")
        try:
            WebDriverWait(self.driver, self.timeout).until(
                lambda driver: driver.execute_script(probe))
        except Exception:
            pass  # The checks say what a page that never got there means.

    def script(self, body):
        return self.driver.execute_script("return %s" % body)

    def press(self, element_id):
        from selenium.webdriver.common.by import By
        self.driver.find_element(By.ID, element_id).click()

    @property
    def theme(self):
        """The theme the page is showing, in the vocabulary the pages store."""
        if self.page == "api.html":
            return self.script("document.documentElement.classList.contains('dark-mode')"
                               " ? 'darkmode' : 'lightmode'")
        return self.script("document.body.className") or None

    @property
    def stored(self):
        return self.script("(() => { try { return sessionStorage.getItem('theme'); }"
                           " catch (e) { return 'REFUSED'; } })()")


def jquery_reached(session):
    """index.html loads jQuery from a CDN, and does nothing at all without it."""
    return bool(session.script("typeof window.jQuery !== 'undefined'"))


def expect(label, got, want):
    if got != want:
        raise Failure("%s is %r, expected %r" % (label, got, want))


def check_follows_browser_setting(webdriver, browser, base_url, timeout, dark):
    """A page with nothing stored shows the browser's theme and stores nothing."""
    setting = "darkmode" if dark else "lightmode"
    session = Session(webdriver, browser, base_url, timeout, dark=dark)
    try:
        session.open("index.html")
        with check("index.html follows a %s browser and stores nothing" % setting[:-4]):
            if not jquery_reached(session):
                check_skip("this host has no route to the CDN index.html loads jQuery from")
            else:
                expect("the theme", session.theme, setting)
                expect("what is stored", session.stored, None)

        session.open("api.html")
        with check("api.html follows a %s browser and stores nothing" % setting[:-4]):
            expect("the theme", session.theme, setting)
            expect("what is stored", session.stored, None)
    finally:
        session.close()


def check_a_choice_is_remembered(webdriver, browser, base_url, timeout):
    """A theme picked on either page is stored and honoured by the other."""
    session = Session(webdriver, browser, base_url, timeout, dark=False)
    try:
        session.open("index.html")
        if not jquery_reached(session):
            with check("a theme picked on index.html is remembered"):
                check_skip("this host has no route to the CDN index.html loads jQuery from")
            return

        with check("picking dark on index.html stores the choice"):
            session.press("darkmode-button")
            expect("the theme", session.theme, "darkmode")
            expect("what is stored", session.stored, "darkmode")

        session.open("api.html")
        with check("api.html opens in the theme picked on index.html"):
            expect("the theme", session.theme, "darkmode")

        with check("picking light on api.html stores the choice"):
            session.press("lightmode-button")
            expect("the theme", session.theme, "lightmode")
            expect("what is stored", session.stored, "lightmode")

        session.open("index.html")
        with check("index.html opens in the theme picked on api.html"):
            expect("the theme", session.theme, "lightmode")
    finally:
        session.close()


def check_explorer_script_runs(webdriver, browser, base_url, timeout):
    """The inline script of api.html is admitted by that page's own CSP digest."""
    session = Session(webdriver, browser, base_url, timeout, dark=False)
    try:
        session.open("api.html")
        with check("api.html runs its inline script under its own CSP"):
            if not session.script("!!document.getElementById('darkmode-button').onclick"):
                raise Failure("the theme buttons have no handler: the script did not run, "
                              "which is what a stale sha256 digest in the page's "
                              "Content-Security-Policy looks like")
            if not session.script("!!window.onload"):
                raise Failure("nothing is registered on window.onload, so Swagger UI "
                              "would never start")
            detail("the theme buttons and the Swagger UI startup are both installed")
    finally:
        session.close()


def check_site_data_blocked(webdriver, browser, base_url, timeout):
    """A browser told to block site data still gets both pages, working."""
    session = Session(webdriver, browser, base_url, timeout, dark=True, block_site_data=True)
    try:
        session.open("index.html")
        with check("index.html is usable with site data blocked"):
            if not jquery_reached(session):
                check_skip("this host has no route to the CDN index.html loads jQuery from")
            else:
                expect("what sessionStorage does", session.stored, "REFUSED")
                expect("the theme", session.theme, "darkmode")
                expect("the navigation", session.script(
                    "getComputedStyle(document.getElementById('left-nav')).visibility"),
                    "visible")

        with check("the theme buttons of index.html work with site data blocked"):
            if not jquery_reached(session):
                check_skip("this host has no route to the CDN index.html loads jQuery from")
            else:
                session.press("lightmode-button")
                expect("the theme", session.theme, "lightmode")
    finally:
        session.close()

    # Started light, so the button has to change something for the check below to
    # mean anything rather than reporting the theme the page already had.
    session = Session(webdriver, browser, base_url, timeout, dark=False, block_site_data=True)
    try:
        session.open("api.html")
        with check("api.html is usable with site data blocked"):
            expect("what sessionStorage does", session.stored, "REFUSED")
            expect("the theme", session.theme, "lightmode")
            if not session.script("!!window.onload"):
                raise Failure("nothing is registered on window.onload, so Swagger UI "
                              "would never start")

        with check("the theme buttons of api.html work with site data blocked"):
            session.press("darkmode-button")
            expect("the theme", session.theme, "darkmode")
    finally:
        session.close()


def run_browser(webdriver, browser, base_url, timeout):
    section(browser)
    for dark in (False, True):
        check_follows_browser_setting(webdriver, browser, base_url, timeout, dark)
    check_a_choice_is_remembered(webdriver, browser, base_url, timeout)
    check_explorer_script_runs(webdriver, browser, base_url, timeout)
    check_site_data_blocked(webdriver, browser, base_url, timeout)


def run(args, base_url):
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
        run_browser(webdriver, browser, base_url, args.timeout)
    if not drove_one:
        raise Failure("no browser this suite can drive is installed")


def main():
    args = parse_args()
    try:
        if args.base_url:
            run(args, args.base_url)
        else:
            if not (PAGES / "index.html").exists():
                raise Failure("%s is missing" % PAGES)
            with browser_lib.serving(PAGES) as base_url:
                run(args, base_url)
    except (Failure, browser_lib.Unavailable) as exc:
        suite_fail("web_theme", str(exc))
        return 1
    suite_ok("web_theme")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
