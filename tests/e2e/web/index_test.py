#!/usr/bin/env python3
# E2E: what html/index.html sends, driven in real browsers. Needs no device.

"""The device's home page, checked against what it puts on the wire.

The page is the only client of several REST calls, and most of what it decides
is invisible in the page itself: which route an uploaded file goes to, which
address a monitor command asks for, whether the item that powers the machine
off is offered at all. `theme_test.py` drives the same page but only looks at
it.

The device is `tests/lib/device_double.py`, the fake Ultimate the observability
tests use, handed `html/` so that it serves the pages as well as the API, and
asked afterwards what it was sent. The browser reaches it through an HTTP
proxy at `http://ultimate.test/`, because the page builds every REST URL from
`window.location.hostname` with no port and so can only reach an API on port
80. Only `http` is proxied, so the CDN the page loads jQuery from over `https`
is still reached directly.

What the checks cover:

- the Power Off item is offered on the Ultimate 64 family and on nothing else.
  `machine:poweroff` reaches hardware only where `MENU_C64_POWEROFF` is
  compiled in, which is `U64 == 1` and `U64 == 2`; a cartridge answers 501. The
  page decides this from the product name, so every name `product_name[]` in
  `software/system/product.cc` can report is checked, both ways round;
- the firmware version reaches the side panel as text and not as markup;
- powering off asks first, sends `PUT /v1/machine:poweroff` when the question
  is answered yes, and sends nothing at all when it is answered no;
- a D64, G64, D71, G71 or D81 is mounted through `POST /v1/drives/a:mount` with
  the image type given explicitly, because the upload carries no file name for
  the device to take an extension from. A PRG still runs, an unknown extension
  sends nothing, a mount that failed is reported, and a mount that worked is
  reported too, which nothing else does: a mounted disk changes nothing on the
  screen;
- the monitor's `m`, `d` and `f` ask for the address that was typed. jQuery
  Terminal parses arguments by default, and a hex address such as `1e00`
  reaches the handler as the number 100 in scientific notation, so the page
  reads $0100;
- the Live Monitor's title bar is inside its header and its list is a list;
- the BASIC editor shows upper case when the browser restores a ticked box on
  a reload, which fires no change event.

Every condition is a browser setting rather than anything injected into the
page: the WebDriver unhandled-prompt behaviour decides what happens to the
`confirm()`, the proxy decides where the REST calls land, and the clicks, the
uploads and the typing are what a person would do.

Host packages: selenium. See tests/requirements.txt. Also needs Chrome or
Firefox installed with their WebDriver on PATH, and a route to that CDN.
Whatever is missing is reported as a skip.
"""

import argparse
import os
import pathlib
import sys
import tempfile
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
sys.path.insert(0, bootstrap.directory("e2e", "web"))

import browser as browser_lib  # noqa: E402
from device_double import DeviceDouble  # noqa: E402
from report import (Failure, check, check_skip, detail,  # noqa: E402
                    section, suite_fail, suite_ok)

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
PAGES = REPO_ROOT / "html"

# A reserved TLD, so a browser that ignored the proxy would fail to resolve it
# rather than reaching something real.
DEVICE_HOST = "ultimate.test"

# How long the page has to finish logging in. It fetches jQuery from a CDN
# first, and only then asks the device what it is.
READY_TIMEOUT = 20.0

# Every name product_name[] in software/system/product.cc can report, split by
# whether MENU_C64_POWEROFF does anything on that product.
POWER_PRODUCTS = ("Ultimate 64", "Ultimate 64 Elite", "Ultimate 64-II")
CARTRIDGE_PRODUCTS = ("Ultimate", "Ultimate II", "Ultimate II+", "Ultimate II+L")

# The disk images the page mounts rather than runs, and the drive it uses.
DISK_IMAGES = ("d64", "g64", "d71", "g71", "d81")
MOUNT_ROUTE = "/v1/drives/a:mount"
# The rest of what the file input takes, and the route each one goes to.
RUNNER_ROUTES = {"prg": "/v1/runners:run_prg",
                 "crt": "/v1/runners:run_crt",
                 "sid": "/v1/runners:sidplay"}


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--browser", default="all", choices=("all", *browser_lib.BROWSERS),
                        help="Which browsers to drive. Default: every one installed.")
    parser.add_argument("-t", "--timeout", type=float, default=READY_TIMEOUT,
                        help="How long a page has to become ready.")
    return parser.parse_args()


def expect(label, got, want):
    if got != want:
        raise Failure("%s is %r, expected %r" % (label, got, want))


def rest_calls(device):
    """What the page asked the device for, without the page loads."""
    return [call for call in device.calls() if call.path.startswith("/v1/")]


def await_call(device, path, timeout=5.0):
    """The first call to `path` since the log was cleared, or None."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        found = device.calls(path=path)
        if found:
            return found[0]
        time.sleep(0.02)
    return None


def await_quiet(device, settle=0.4, timeout=15.0):
    """Wait until the browser has stopped calling.

    A monitor command reads memory in a loop, so a check that cleared the log
    the moment its first read arrived would attribute the rest of that loop to
    whatever it did next.
    """
    deadline = time.time() + timeout
    seen, quiet_since = len(device.calls()), time.time()
    while time.time() < deadline:
        time.sleep(0.05)
        now = len(device.calls())
        if now != seen:
            seen, quiet_since = now, time.time()
        elif time.time() - quiet_since >= settle:
            return
    raise Failure("the browser was still calling the device after %gs" % timeout)


def image(directory, name, size=64):
    """A file for the page to upload, whose bytes say which file it was."""
    path = pathlib.Path(directory) / name
    path.write_bytes(bytes((i + len(name)) & 0xFF for i in range(size)))
    return path


def upload_directory():
    """Where a file a browser is asked to upload can live.

    A Snap-packaged Firefox has its own /tmp and reports anything under the
    system temp directory as "File not found", so the files go under the home
    directory, which it can read, under a name that does not start with a dot,
    which it cannot.
    """
    home = pathlib.Path.home()
    return str(home) if os.access(home, os.W_OK) else None


class Page:
    """One browser looking at the double's home page."""

    def __init__(self, webdriver, browser, device, timeout):
        # "ignore" leaves a confirm() or an alert() standing until a check
        # answers it, so a check can say what the page asked before answering.
        self.driver = browser_lib.make_driver(
            webdriver, browser, proxy="127.0.0.1:%d" % device.rest_port, prompts="ignore")
        self.device = device
        self.timeout = timeout

    def close(self):
        self.driver.quit()

    def open(self):
        self.driver.get("http://%s/index.html" % DEVICE_HOST)
        self.await_ready()

    def await_ready(self):
        """Wait until the page has asked the device what it is and acted on it."""
        self.wait("getComputedStyle(document.getElementById('left-nav'))"
                  ".visibility === 'visible'")

    def wait(self, condition):
        from selenium.webdriver.support.ui import WebDriverWait

        def holds(driver):
            try:
                return bool(driver.execute_script("return %s" % condition))
            except Exception:
                return False

        try:
            WebDriverWait(self.driver, self.timeout).until(holds)
            return True
        except Exception:
            return False  # The checks say what a page that never got there means.

    def script(self, body):
        return self.driver.execute_script("return %s" % body)

    def element(self, element_id):
        from selenium.webdriver.common.by import By
        return self.driver.find_element(By.ID, element_id)

    def visible(self, element_id):
        return self.element(element_id).is_displayed()

    def click(self, element_id):
        self.element(element_id).click()

    def upload(self, path):
        self.element("runnerfile").send_keys(str(path))

    def dialog(self):
        """The question the page is asking, once it is on screen."""
        from selenium.webdriver.support import expected_conditions
        from selenium.webdriver.support.ui import WebDriverWait
        return WebDriverWait(self.driver, self.timeout).until(
            expected_conditions.alert_is_present())

    def command(self, text):
        """Type a monitor command the way a person at the terminal would.

        Into the terminal's own command line rather than into the terminal:
        once a command has printed anything, a click on the output area leaves
        the focus on the document and the keys go nowhere.
        """
        from selenium.webdriver.common.by import By
        from selenium.webdriver.common.keys import Keys
        found = self.driver.find_elements(
            By.CSS_SELECTOR, "#terminal .cmd-editable, #terminal .cmd textarea")
        if not found:
            raise Failure("the Live Monitor has no command line to type into")
        found[0].send_keys(text + Keys.ENTER)

    def reload(self):
        self.driver.refresh()
        self.await_ready()

    def reload_keeping_form_state(self):
        """Load the page again, and say whether it really was loaded again.

        A browser restores the controls of a page it reloads, and restoring one
        fires no change event, which is the case this exists for. Getting there
        under WebDriver is browser-specific, and two of the ways look right and
        prove nothing: refresh() reloads but discards the control state, and
        going away and pressing Back hands the whole document back from the
        back/forward cache without running the page at all. location.reload()
        is the one that runs the page again, and whether the controls come back
        with it is then the browser's business. The marker says which happened,
        so a check can skip rather than pass on a page that never reloaded.
        """
        self.driver.execute_script("window.__reloadMarker = true")
        self.driver.execute_script("location.reload()")
        reloaded = self.wait("!window.__reloadMarker")
        self.await_ready()
        return reloaded

    def jquery_reached(self):
        """index.html loads jQuery from a CDN, and does nothing at all without it."""
        return bool(self.script("typeof window.jQuery !== 'undefined'"))


# The checks.

def check_power_menu(page):
    """The Power Off item is offered where the call reaches hardware, and nowhere else."""
    for product, offered in ([(p, True) for p in POWER_PRODUCTS]
                             + [(p, False) for p in CARTRIDGE_PRODUCTS]):
        page.device.product = product
        page.reload()
        with check("a %s %s Power Off Machine"
                   % (product, "offers" if offered else "does not offer")):
            expect("the menu item", page.visible("menu-power"), offered)
    detail("the names are product_name[] in software/system/product.cc")


def check_firmware_version(page):
    """The version the device reports reaches the side panel, as text."""
    page.device.firmware_version = "3.14d"
    page.reload()
    with check("the side panel shows the firmware version the device reported"):
        expect("the panel", page.script(
            "document.getElementById('firmware-version').textContent"), "firmware v3.14d")

    # A version is a device-supplied string, and the panel is written from it.
    page.device.firmware_version = "<img src=x onerror=1>"
    page.reload()
    with check("a version carrying markup is shown as text and not built into the page"):
        expect("elements inside the panel", page.script(
            "document.getElementById('firmware-version').children.length"), 0)
        expect("the panel", page.script(
            "document.getElementById('firmware-version').textContent"),
            "firmware v<img src=x onerror=1>")
    page.device.firmware_version = "3.14d"


def check_power_off(page):
    """The question is asked, and only a yes powers the machine off."""
    page.device.product = "Ultimate 64"
    page.reload()

    page.device.clear()
    with check("Power Off Machine asks first, then sends PUT /v1/machine:poweroff"):
        page.click("dopoweroff")
        dialog = page.dialog()
        expect("the question", dialog.text,
               "Are you sure you want to power off your Ultimate?")
        dialog.accept()
        call = await_call(page.device, "/v1/machine:poweroff")
        if call is None:
            raise Failure("no call arrived; the page sent %r" % rest_calls(page.device))
        expect("the method", call.method, "PUT")

    page.device.clear()
    with check("declining the question sends no power-off"):
        page.click("dopoweroff")
        page.dialog().dismiss()
        # Reset goes out under the same conditions and needs no confirmation, so
        # its arrival dates the absence of the power-off rather than a sleep.
        page.click("doreset")
        if await_call(page.device, "/v1/machine:reset") is None:
            raise Failure("the control check failed: Reset Machine sent nothing either, "
                          "so this says nothing about the power-off")
        expect("power-off calls", page.device.calls(path="/v1/machine:poweroff"), [])


def check_uploads(page, directory):
    """Each file type reaches the route that handles it, with what it needs."""
    for kind in (*DISK_IMAGES, "D64"):
        page.device.clear()
        source = image(directory, "disk.%s" % kind)
        with check("a .%s is mounted through POST %s" % (kind, MOUNT_ROUTE)):
            page.upload(source)
            call = await_call(page.device, MOUNT_ROUTE)
            if call is None:
                raise Failure("no mount arrived; the page sent %r" % rest_calls(page.device))
            expect("the method", call.method, "POST")
            # The upload carries no file name, so the device cannot infer this.
            expect("the type", call.params.get("type"), kind.lower())
            expect("the body", call.body, source.read_bytes())

    for kind, route in sorted(RUNNER_ROUTES.items()):
        page.device.clear()
        with check("a .%s still goes to POST %s" % (kind, route)):
            page.upload(image(directory, "program.%s" % kind))
            if await_call(page.device, route) is None:
                raise Failure("no call arrived; the page sent %r" % rest_calls(page.device))
            expect("mount calls", page.device.calls(path=MOUNT_ROUTE), [])

    page.device.clear()
    with check("an unsupported extension is refused and sends no request"):
        page.upload(image(directory, "notes.txt"))
        dialog = page.dialog()
        expect("the message", dialog.text, "Unsupported file extension: '.txt'")
        dialog.accept()
        # A file that is handled, uploaded after it, dates the absence.
        page.upload(image(directory, "control.prg"))
        if await_call(page.device, RUNNER_ROUTES["prg"]) is None:
            raise Failure("the control check failed: the .prg sent nothing either, "
                          "so this says nothing about the .txt")
        expect("the number of REST calls", len(rest_calls(page.device)), 1)

    with check("the file input accepts every type the page handles, and no other"):
        offered = page.script("document.getElementById('runnerfile').getAttribute('accept')")
        got = sorted({e.strip().lstrip(".").lower() for e in offered.split(",") if e.strip()})
        expect("the accepted extensions", got, sorted(set(DISK_IMAGES) | set(RUNNER_ROUTES)))


def check_mount_is_reported(page, directory):
    """A mount says so, a run does not, and a mount that failed says that instead."""
    page.device.clear()
    with check("a mount that worked is reported, because nothing else shows it"):
        page.upload(image(directory, "reported.d64"))
        if await_call(page.device, MOUNT_ROUTE) is None:
            raise Failure("no mount arrived; the page sent %r" % rest_calls(page.device))
        if not page.wait("document.getElementById('runok').offsetParent !== null"):
            raise Failure("the mount was not reported: the confirmation never appeared")
        expect("the error box", page.visible("runmsg"), False)
        # The drive the page names has to be the drive it mounted to.
        expect("the confirmation", page.script(
            "document.getElementById('runok').textContent"),
            "The disk image has been mounted in drive A.")

    page.device.clear()
    with check("a program that ran is not reported the same way"):
        page.upload(image(directory, "reported.prg"))
        if await_call(page.device, RUNNER_ROUTES["prg"]) is None:
            raise Failure("no run arrived; the page sent %r" % rest_calls(page.device))
        await_quiet(page.device)
        expect("the confirmation", page.visible("runok"), False)
        expect("the error box", page.visible("runmsg"), False)

    page.device.clear()
    page.device.faults.offline = True
    try:
        with check("a mount the device does not answer is reported as an error"):
            page.upload(image(directory, "unanswered.d64"))
            if await_call(page.device, MOUNT_ROUTE) is None:
                raise Failure("no mount arrived; the page sent %r" % rest_calls(page.device))
            if not page.wait("document.getElementById('runmsg').offsetParent !== null"):
                raise Failure("the failure was not reported: no error box appeared")
            expect("the confirmation", page.visible("runok"), False)
    finally:
        page.device.faults.offline = False


def check_monitor_reads_the_address_typed(page):
    """A hex address that looks like a number is read as the address it is."""
    page.click("showterminal")
    readmem = "/v1/machine:readmem"

    for command, address in (("m 1e00", "1e00"), ("d 1e00", "1e00")):
        await_quiet(page.device)
        page.device.clear()
        with check("%s asks for $%s" % (command, address)):
            page.command(command)
            call = await_call(page.device, readmem)
            if call is None:
                raise Failure("no read arrived; the page sent %r" % rest_calls(page.device))
            # Parsed as a number, 1e00 is 100, and the page would read $0100.
            expect("the address", call.params.get("address"), address)

    await_quiet(page.device)
    page.device.clear()
    with check("m 1e00 1e10 reads the 16 bytes between them"):
        page.command("m 1e00 1e10")
        call = await_call(page.device, readmem)
        if call is None:
            raise Failure("no read arrived; the page sent %r" % rest_calls(page.device))
        expect("the address", call.params.get("address"), "1e00")
        expect("the length", call.params.get("length"), "16")
        # Parsed, 1e10 is 10000000000, which the page clamps to $10000 and then
        # walks to in 16-byte reads.
        await_quiet(page.device)
        expect("the number of reads", len(page.device.calls(path=readmem)), 1)

    await_quiet(page.device)
    page.device.clear()
    with check("f 1e00 1e01 ff fills from $1e00"):
        page.command("f 1e00 1e01 ff")
        call = await_call(page.device, "/v1/machine:writemem")
        if call is None:
            raise Failure("no write arrived; the page sent %r" % rest_calls(page.device))
        expect("the method", call.method, "POST")
        expect("the address", call.params.get("address"), "1e00")
        expect("the bytes", call.body, b"\xff\xff")


def check_monitor_title_bar(page):
    """The dialog's title is a list item in its header, and sits inside it."""
    page.click("showterminal")
    with check("the Live Monitor's header is a list of list items"):
        stray = page.script(
            "Array.from(document.querySelectorAll('#terminal header ul > *'))"
            ".filter(e => e.tagName !== 'LI').map(e => e.tagName)")
        if stray:
            raise Failure("a <ul> in the header has %s as a direct child, "
                          "which is not valid list markup" % ", ".join(stray))

    with check("the title sits inside the header, beside the button and not over it"):
        boxes = page.script("""(() => {
            const box = e => { const b = e.getBoundingClientRect();
                return {left: b.left, right: b.right, top: b.top, bottom: b.bottom}; };
            return {
                header: box(document.querySelector('#terminal header')),
                title: box(document.querySelector('#terminal header .title')),
                button: box(document.querySelector('#terminal header li:not(.title-item)')),
            };
        })()""")
        header, title, button = boxes["header"], boxes["title"], boxes["button"]
        if not (header["left"] <= title["left"] and title["right"] <= header["right"]
                and header["top"] <= title["top"] and title["bottom"] <= header["bottom"]):
            raise Failure("the title is at %r, outside the header at %r" % (title, header))
        if title["left"] < button["right"]:
            raise Failure("the title starts at %.0f, over the button that ends at %.0f"
                          % (title["left"], button["right"]))
        detail("title %.0f-%.0f in header %.0f-%.0f, after the button at %.0f"
               % (title["left"], title["right"], header["left"], header["right"],
                  button["right"]))


def check_uppercase_survives_a_reload(page):
    """A browser restores a ticked box without firing change, and the editor follows."""
    uppercase = "document.getElementById('basiceditor').classList.contains('uppercase')"
    page.click("showtokenizer")
    page.click("uppercaseBASIC")
    with check("ticking Uppercase puts the BASIC editor in upper case"):
        expect("the editor", page.script(uppercase), True)

    reloaded = page.reload_keeping_form_state()
    page.click("showtokenizer")
    with check("the editor is still in upper case after the page is loaded again"):
        if not reloaded:
            check_skip("this browser handed the whole page back from its cache instead "
                       "of running it again, so nothing here would be the page's doing")
        elif not page.script("document.getElementById('uppercaseBASIC').checked"):
            check_skip("this browser restored no tick for the page to follow")
        else:
            expect("the editor", page.script(uppercase), True)
    page.click("uppercaseBASIC")


def run_browser(webdriver, browser, timeout, directory):
    section(browser)
    with DeviceDouble(pages=str(PAGES)) as device:
        page = Page(webdriver, browser, device, timeout)
        try:
            page.open()
            if not page.jquery_reached():
                with check("index.html reaches the CDN it loads jQuery from"):
                    check_skip("this host has no route to that CDN, and the page "
                               "does nothing at all without it")
                return
            check_power_menu(page)
            check_firmware_version(page)
            check_power_off(page)

            page.click("showrunner")
            check_uploads(page, directory)
            check_mount_is_reported(page, directory)

            check_monitor_reads_the_address_typed(page)
            check_monitor_title_bar(page)
            check_uppercase_survives_a_reload(page)
        finally:
            page.close()


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
        with tempfile.TemporaryDirectory(prefix="web-index-",
                                         dir=upload_directory()) as directory:
            run(args, directory)
    except (Failure, browser_lib.Unavailable) as exc:
        suite_fail("web_index", str(exc))
        return 1
    suite_ok("web_index")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
