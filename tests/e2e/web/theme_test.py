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
import contextlib
import functools
import http.server
import os
import pathlib
import sys
import threading

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
from report import (Failure, check, check_skip, detail,  # noqa: E402
                    section, suite_fail, suite_ok)

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
PAGES = REPO_ROOT / "html"

# Ubuntu and Debian ship Firefox as a Snap whose /usr/bin/firefox is a wrapper
# script, and geckodriver rejects a wrapper with "binary is not a Firefox
# executable". These are where the real binary sits when that happens.
FIREFOX_FALLBACKS = (
    "/snap/firefox/current/usr/lib/firefox/firefox",
    "/usr/lib/firefox/firefox",
    "/usr/lib64/firefox/firefox",
    "/Applications/Firefox.app/Contents/MacOS/firefox",
)

# The content setting each browser calls "block site data", which is what makes
# sessionStorage throw. Chrome numbers its content settings, 2 being block.
CHROME_BLOCK_SITE_DATA = {"profile.default_content_setting_values.cookies": 2}
FIREFOX_BLOCK_SITE_DATA = {"network.cookie.cookieBehavior": 2}

# How long a page has to finish applying its theme. index.html does it inside a
# jQuery ready handler, after fetching jQuery.
READY_TIMEOUT = 20.0


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--base-url", default="",
                        help="Where the pages are served from, e.g. http://ultimate64. "
                             "Default: serve html/ from this tree.")
    parser.add_argument("--browser", default="all", choices=("all", "chrome", "firefox"),
                        help="Which browsers to drive. Default: every one installed.")
    parser.add_argument("-t", "--timeout", type=float, default=READY_TIMEOUT,
                        help="How long a page has to become ready.")
    return parser.parse_args()


def require_selenium():
    try:
        import selenium  # noqa: F401
        from selenium import webdriver
        return webdriver
    except ImportError as exc:
        raise Failure("selenium is needed for this suite: "
                      "pip install -r tests/requirements.txt") from exc


@contextlib.contextmanager
def serving(directory):
    """Serve `directory` over HTTP for the length of the run."""
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(directory))
    handler.func.log_message = lambda *args, **kwargs: None

    class Server(http.server.ThreadingHTTPServer):
        daemon_threads = True

    server = Server(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        yield "http://127.0.0.1:%d" % server.server_address[1]
    finally:
        server.shutdown()


class Session:
    """One browser, configured the way a reader's browser would be configured."""

    def __init__(self, webdriver, browser, base_url, timeout, dark=False, block_site_data=False):
        self.browser = browser
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.page = ""
        if browser == "chrome":
            options = webdriver.ChromeOptions()
            options.add_argument("--headless=new")
            if block_site_data:
                options.add_experimental_option("prefs", dict(CHROME_BLOCK_SITE_DATA))
            self.driver = webdriver.Chrome(options=options)
            # Chrome has no preference for the colour scheme, so it is set the
            # way its own developer tools set it.
            self.driver.execute_cdp_cmd(
                "Emulation.setEmulatedMedia",
                {"features": [{"name": "prefers-color-scheme",
                               "value": "dark" if dark else "light"}]})
        else:
            options = webdriver.FirefoxOptions()
            options.add_argument("-headless")
            options.set_preference("ui.systemUsesDarkTheme", 1 if dark else 0)
            for name, value in (FIREFOX_BLOCK_SITE_DATA.items() if block_site_data else ()):
                options.set_preference(name, value)
            binary = firefox_binary()
            if binary:
                options.binary_location = binary
            self.driver = webdriver.Firefox(options=options)

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


def firefox_binary():
    """The Firefox to drive: the one named in the environment, or a packaged one."""
    named = os.environ.get("FIREFOX_BINARY", "")
    if named:
        return named
    for candidate in FIREFOX_FALLBACKS:
        if pathlib.Path(candidate).exists():
            return candidate
    return ""


def available(webdriver, browser):
    """Whether this browser and its driver are here, without asserting anything."""
    try:
        session = Session(webdriver, browser, "http://127.0.0.1", 1.0)
    except Exception as exc:
        return False, str(exc).strip().split("\n")[0]
    session.close()
    return True, ""


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
    webdriver = require_selenium()
    wanted = ("chrome", "firefox") if args.browser == "all" else (args.browser,)
    drove_one = False
    for browser in wanted:
        here, why = available(webdriver, browser)
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
            with serving(PAGES) as base_url:
                run(args, base_url)
    except Failure as exc:
        suite_fail("web_theme", str(exc))
        return 1
    suite_ok("web_theme")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
