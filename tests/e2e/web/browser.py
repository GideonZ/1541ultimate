#!/usr/bin/env python3
# Shared Selenium plumbing for the suites that drive the pages the device serves.

"""Starting a browser, and serving the pages to it.

Both web suites need the same three things and neither of them is the subject of
a test: a Selenium import that reports its absence as a skip rather than a stack
trace, a Firefox that is the real binary rather than a Snap wrapper, and a way to
put `html/` in front of a browser.

Everything a suite varies about a browser is a browser setting rather than
anything injected into a page, so what the checks establish is what a reader's
browser would do.
"""

import contextlib
import functools
import http.server
import os
import pathlib
import threading

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

BROWSERS = ("chrome", "firefox")


class Unavailable(Exception):
    """Selenium, a browser or a driver is not installed. Suites report a skip."""


def require_selenium():
    try:
        import selenium  # noqa: F401
        from selenium import webdriver
        return webdriver
    except ImportError as exc:
        raise Unavailable("selenium is needed for this suite: "
                          "pip install -r tests/requirements.txt") from exc


def firefox_binary():
    """The Firefox to drive: the one named in the environment, or a packaged one."""
    named = os.environ.get("FIREFOX_BINARY", "")
    if named:
        return named
    for candidate in FIREFOX_FALLBACKS:
        if pathlib.Path(candidate).exists():
            return candidate
    return ""


def make_driver(webdriver, browser, dark=False, block_site_data=False,
                proxy="", prompts=""):
    """A headless browser configured the way a reader's browser would be.

    `proxy` is `host:port` and is set for `http` only, so a page loaded through
    it still reaches an `https` CDN directly. `prompts` is the WebDriver
    unhandled-prompt behaviour, `accept` or `dismiss`, which is how a check
    decides what happens to a `confirm()` without the page knowing.
    """
    if browser == "chrome":
        options = webdriver.ChromeOptions()
        options.add_argument("--headless=new")
        if proxy:
            options.add_argument("--proxy-server=http=%s" % proxy)
        if block_site_data:
            options.add_experimental_option("prefs", dict(CHROME_BLOCK_SITE_DATA))
        if prompts:
            options.set_capability("unhandledPromptBehavior", prompts)
        driver = webdriver.Chrome(options=options)
        # Chrome has no preference for the colour scheme, so it is set the way
        # its own developer tools set it.
        driver.execute_cdp_cmd(
            "Emulation.setEmulatedMedia",
            {"features": [{"name": "prefers-color-scheme",
                           "value": "dark" if dark else "light"}]})
        return driver

    options = webdriver.FirefoxOptions()
    options.add_argument("-headless")
    options.set_preference("ui.systemUsesDarkTheme", 1 if dark else 0)
    if proxy:
        host, _, port = proxy.rpartition(":")
        options.set_preference("network.proxy.type", 1)
        options.set_preference("network.proxy.http", host)
        options.set_preference("network.proxy.http_port", int(port))
        options.set_preference("network.proxy.allow_hijacking_localhost", True)
    for name, value in (FIREFOX_BLOCK_SITE_DATA.items() if block_site_data else ()):
        options.set_preference(name, value)
    if prompts:
        options.set_capability("unhandledPromptBehavior", prompts)
    binary = firefox_binary()
    if binary:
        options.binary_location = binary
    return webdriver.Firefox(options=options)


def available(webdriver, browser, proxy=""):
    """Whether this browser and its driver are here, without asserting anything."""
    try:
        driver = make_driver(webdriver, browser, proxy=proxy)
    except Exception as exc:
        return False, str(exc).strip().split("\n")[0]
    driver.quit()
    return True, ""


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
