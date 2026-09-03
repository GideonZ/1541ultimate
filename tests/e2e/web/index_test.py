#!/usr/bin/env python3
# E2E: what html/index.html sends and shows, driven in real browsers. Needs no device.

"""The device's home page, checked against what it puts on the wire.

`html/index.html` is the only client of several REST calls, and most of what it
does is invisible in the page itself: which route an uploaded file goes to, which
address a monitor command asks for, whether a menu item that powers the machine
off is even offered. The browser here talks to a stub device
(`device_stub.py`) that records every call, so each check states both what the
page drew and what it asked the device for.

What the checks cover:

- the Power Off item is offered on the Ultimate 64 family and on nothing else.
  `machine:poweroff` reaches hardware only where `MENU_C64_POWEROFF` is compiled
  in, which is `U64 == 1` and `U64 == 2`; a cartridge answers 501. The page
  decides this from the product name, so every name `product_name[]` in
  `software/system/product.cc` can report is checked, both ways round;
- the firmware version reaches the side panel as text and not as markup;
- powering off asks first, sends `PUT /v1/machine:poweroff` when the question is
  answered yes, and sends nothing at all when it is answered no;
- a D64, G64, D71, G71 or D81 is mounted through `POST /v1/drives/a:mount` with
  the image type given explicitly, because the upload carries no file name for
  the device to take an extension from. A PRG still runs, an unknown extension
  sends nothing, a mount the device refuses is reported, and a mount that worked
  is reported too, which nothing else on the page does: a mounted disk changes
  nothing on the screen;
- the monitor's `m`, `d` and `f` ask for the address that was typed. jQuery
  Terminal parses arguments by default, and a hex address such as `1e00` reaches
  the handler as the number 100 in scientific notation, so the page reads $0100;
- the Live Monitor's title bar is inside its header and its list is a list;
- the BASIC editor shows upper case when the browser restores a ticked box on a
  reload, which fires no change event.

Every condition is a browser setting rather than anything injected into the page:
the WebDriver unhandled-prompt behaviour decides what happens to the `confirm()`,
an HTTP proxy decides where the page's REST calls land, and the clicks, the
uploads and the typing are what a person would do.

The page builds its REST URLs from `window.location.hostname` with no port, so
the browser is proxied and the pages are served from `http://ultimate.test/`
rather than from a high port on localhost. Only `http` is proxied, so the CDN
the page loads jQuery from is still reached directly.

Host packages: selenium. See tests/requirements.txt. Also needs Chrome or
Firefox installed with their WebDriver on PATH, and a route to that CDN.
Whatever is missing is reported as a skip.
"""

import argparse
import os
import pathlib
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))

import browser as browser_lib  # noqa: E402
from device_stub import DeviceStub  # noqa: E402
from report import (Failure, check, check_skip, detail,  # noqa: E402
                    section, suite_fail, suite_ok)

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
PAGES = REPO_ROOT / "html"

# How long the page has to finish logging in. It fetches jQuery from a CDN
# first, and only then asks the device what it is.
READY_TIMEOUT = 20.0

# Every name product_name[] in software/system/product.cc can report, split by
# whether MENU_C64_POWEROFF does anything on that product.
POWER_PRODUCTS = ("Ultimate 64", "Ultimate 64 Elite", "Ultimate 64-II")
CARTRIDGE_PRODUCTS = ("Ultimate", "Ultimate II", "Ultimate II+", "Ultimate II+L")

# The disk images the page mounts rather than runs, and what each is sent as.
DISK_IMAGES = ("d64", "g64", "d71", "g71", "d81")
# The rest of what the file input takes, and the route each one goes to.
RUNNER_ROUTES = {"prg": "/v1/runners:run_prg",
                 "crt": "/v1/runners:run_crt",
                 "sid": "/v1/runners:sidplay"}

MOUNT_ROUTE = "/v1/drives/a:mount"


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


class Page:
    """One browser looking at the stub device's home page."""

    def __init__(self, webdriver, browser, stub, timeout):
        # "ignore" leaves a confirm() or an alert() standing until a check
        # answers it, so a check can say what the page asked before answering.
        self.driver = browser_lib.make_driver(webdriver, browser, proxy=stub.proxy,
                                              prompts="ignore")
        self.stub = stub
        self.timeout = timeout

    def close(self):
        self.driver.quit()

    def open(self):
        self.driver.get(self.stub.base_url + "/index.html")
        self.await_ready()

    def reload(self):
        self.driver.refresh()
        self.await_ready()

    def reload_keeping_form_state(self):
        """Load the page again, and say whether it really was loaded again.

        A browser restores the controls of a page it reloads, and restoring one
        fires no change event, which is the case this exists for. Getting there
        under WebDriver is browser-specific and two of the ways look right and
        prove nothing: `refresh()` reloads but discards the control state, and
        going away and pressing Back hands the whole document back from the
        back/forward cache without running the page at all. `location.reload()`
        is the one that runs the page again, and whether the controls come back
        with it is then the browser's business. The marker says which happened,
        so a check can skip rather than pass on a page that never reloaded.
        """
        from selenium.webdriver.support.ui import WebDriverWait
        self.driver.execute_script("window.__reloadMarker = true")
        self.driver.execute_script("location.reload()")

        def gone(driver):
            try:
                return not driver.execute_script("return !!window.__reloadMarker")
            except Exception:
                return False

        try:
            WebDriverWait(self.driver, self.timeout).until(gone)
            reloaded = True
        except Exception:
            reloaded = False
        self.await_ready()
        return reloaded

    def await_ready(self):
        """Wait until the page has asked the device what it is and acted on it."""
        from selenium.webdriver.support.ui import WebDriverWait
        probe = ("return getComputedStyle(document.getElementById('left-nav'))"
                 ".visibility === 'visible'")
        try:
            WebDriverWait(self.driver, self.timeout).until(
                lambda driver: driver.execute_script(probe))
        except Exception:
            pass  # The checks say what a page that never got there means.

    def script(self, body):
        return self.driver.execute_script("return %s" % body)

    def element(self, element_id):
        from selenium.webdriver.common.by import By
        return self.driver.find_element(By.ID, element_id)

    def visible(self, element_id):
        return self.element(element_id).is_displayed()

    def await_visible(self, element_id):
        """Wait for a box the page shows once a request of its own has answered."""
        from selenium.webdriver.support.ui import WebDriverWait
        try:
            WebDriverWait(self.driver, self.timeout).until(
                lambda driver: self.visible(element_id))
            return True
        except Exception:
            return False

    def click(self, element_id):
        self.element(element_id).click()

    def show_page(self, link_id):
        self.click(link_id)

    def upload(self, path):
        self.element("runnerfile").send_keys(str(path))

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

    def dialog(self):
        """The question the page is asking, once it is on screen."""
        from selenium.webdriver.support import expected_conditions
        from selenium.webdriver.support.ui import WebDriverWait
        return WebDriverWait(self.driver, self.timeout).until(
            expected_conditions.alert_is_present())

    def jquery_reached(self):
        """index.html loads jQuery from a CDN, and does nothing at all without it."""
        return bool(self.script("typeof window.jQuery !== 'undefined'"))


def upload_directory():
    """Where a file a browser is asked to upload can live.

    A Snap-packaged Firefox has its own /tmp and reports anything under the
    system temp directory as "File not found", so the files go under the home
    directory, which it can read, and keep a name that does not start with a
    dot, which it cannot.
    """
    home = pathlib.Path.home()
    return str(home) if os.access(home, os.W_OK) else None


def image(directory, name, size=64):
    """A file for the page to upload, whose bytes say which file it was."""
    path = pathlib.Path(directory) / name
    path.write_bytes(bytes((i + len(name)) & 0xFF for i in range(size)))
    return path


# The checks.

def check_power_menu(page):
    """The Power Off item is offered where the call reaches hardware, and nowhere else."""
    for product in POWER_PRODUCTS:
        page.stub.product = product
        page.reload()
        with check("a %s offers Power Off Machine" % product):
            expect("the menu item", page.visible("menu-power"), True)
            expect("its link", page.script(
                "document.getElementById('dopoweroff').textContent"), "Power Off Machine")
    for product in CARTRIDGE_PRODUCTS:
        page.stub.product = product
        page.reload()
        with check("a %s does not offer Power Off Machine" % product):
            expect("the menu item", page.visible("menu-power"), False)
    detail("the names are product_name[] in software/system/product.cc")


def check_firmware_version(page):
    """The version the device reports reaches the side panel, as text."""
    page.stub.product = "Ultimate 64"
    page.stub.firmware_version = "3.14d"
    page.reload()
    with check("the side panel shows the firmware version the device reported"):
        expect("the panel", page.script(
            "document.getElementById('firmware-version').textContent"), "firmware v3.14d")

    # A version is a device-supplied string, and the panel is written from it.
    page.stub.firmware_version = "<img src=x onerror=1>"
    page.reload()
    with check("a version carrying markup is shown as text and not built into the page"):
        expect("elements inside the panel", page.script(
            "document.getElementById('firmware-version').children.length"), 0)
        expect("the panel", page.script(
            "document.getElementById('firmware-version').textContent"),
            "firmware v<img src=x onerror=1>")
    page.stub.firmware_version = "3.14d"


def check_power_off_sends(page):
    """Answering the question yes powers the machine off."""
    page.stub.product = "Ultimate 64"
    page.reload()
    page.stub.clear()
    with check("Power Off Machine asks first, then sends PUT /v1/machine:poweroff"):
        page.click("dopoweroff")
        dialog = page.dialog()
        expect("the question", dialog.text,
               "Are you sure you want to power off your Ultimate?")
        dialog.accept()
        call = page.stub.await_call("/v1/machine:poweroff")
        if call is None:
            raise Failure("no call arrived; the page sent %r" % page.stub.rest_calls())
        expect("the method", call.method, "PUT")


def check_power_off_cancelled(page):
    """Answering it no sends nothing. Reset proves the browser was still listening."""
    page.stub.product = "Ultimate 64"
    page.reload()
    page.stub.clear()
    with check("declining the question sends no power-off"):
        page.click("dopoweroff")
        page.dialog().dismiss()
        # Reset goes out under the same conditions and needs no confirmation, so
        # its arrival dates the absence of the power-off rather than a sleep.
        page.click("doreset")
        if page.stub.await_call("/v1/machine:reset") is None:
            raise Failure("the control check failed: Reset Machine sent nothing either, "
                          "so this says nothing about the power-off")
        expect("power-off calls", page.stub.calls_to("/v1/machine:poweroff"), [])


def check_disk_images_are_mounted(page, directory):
    """Each disk image is mounted on drive A, with its type stated."""
    for kind in DISK_IMAGES:
        page.stub.clear()
        source = image(directory, "disk.%s" % kind)
        with check("a .%s is mounted through POST %s" % (kind, MOUNT_ROUTE)):
            page.upload(source)
            call = page.stub.await_call(MOUNT_ROUTE)
            if call is None:
                raise Failure("no mount arrived; the page sent %r" % page.stub.rest_calls())
            expect("the method", call.method, "POST")
            # The upload carries no file name, so the device cannot infer this.
            expect("the type", call.query.get("type"), kind)
            expect("the body", call.body, source.read_bytes())


def check_uppercase_extensions(page, directory):
    """The extension is read without regard to case, as the file input accepts both."""
    page.stub.clear()
    source = image(directory, "DISK.D64")
    with check("an upper case .D64 is mounted the same way"):
        page.upload(source)
        call = page.stub.await_call(MOUNT_ROUTE)
        if call is None:
            raise Failure("no mount arrived; the page sent %r" % page.stub.rest_calls())
        expect("the type", call.query.get("type"), "d64")


def check_programs_still_run(page, directory):
    """A program still goes to its runner rather than to a drive."""
    for kind, route in sorted(RUNNER_ROUTES.items()):
        page.stub.clear()
        source = image(directory, "program.%s" % kind)
        with check("a .%s still goes to POST %s" % (kind, route)):
            page.upload(source)
            call = page.stub.await_call(route)
            if call is None:
                raise Failure("no call arrived; the page sent %r" % page.stub.rest_calls())
            expect("mount calls", page.stub.calls_to(MOUNT_ROUTE), [])


def check_mount_is_reported(page, directory):
    """A mount says so, a run does not, and a refused mount says that instead."""
    page.stub.clear()
    with check("a mount that worked is reported, because nothing else shows it"):
        page.upload(image(directory, "reported.d64"))
        if page.stub.await_call(MOUNT_ROUTE) is None:
            raise Failure("no mount arrived; the page sent %r" % page.stub.rest_calls())
        if not page.await_visible("runok"):
            raise Failure("the mount was not reported: the confirmation never appeared")
        expect("the error box", page.visible("runmsg"), False)

    page.stub.clear()
    with check("a program that ran is not reported the same way"):
        page.upload(image(directory, "reported.prg"))
        if page.stub.await_call(RUNNER_ROUTES["prg"]) is None:
            raise Failure("no run arrived; the page sent %r" % page.stub.rest_calls())
        page.stub.await_quiet()
        expect("the confirmation", page.visible("runok"), False)
        expect("the error box", page.visible("runmsg"), False)

    page.stub.clear()
    page.stub.refuse[MOUNT_ROUTE] = 500
    try:
        with check("a mount the device refuses is reported as an error"):
            page.upload(image(directory, "refused.d64"))
            if page.stub.await_call(MOUNT_ROUTE) is None:
                raise Failure("no mount arrived; the page sent %r" % page.stub.rest_calls())
            if not page.await_visible("runmsg"):
                raise Failure("the refusal was not reported: no error box appeared")
            expect("the confirmation", page.visible("runok"), False)
    finally:
        page.stub.refuse.pop(MOUNT_ROUTE, None)


def check_unknown_extension_sends_nothing(page, directory):
    """An extension the page does not handle reaches no route at all."""
    page.stub.clear()
    with check("an unsupported extension is refused and sends no request"):
        page.upload(image(directory, "notes.txt"))
        dialog = page.dialog()
        expect("the message", dialog.text, "Unsupported file extension: '.txt'")
        dialog.accept()
        # A file that is handled, uploaded after it, dates the absence.
        page.upload(image(directory, "control.prg"))
        if page.stub.await_call(RUNNER_ROUTES["prg"]) is None:
            raise Failure("the control check failed: the .prg sent nothing either, "
                          "so this says nothing about the .txt")
        expect("calls to a drive", page.stub.calls_to(MOUNT_ROUTE), [])
        expect("the number of REST calls", len(page.stub.rest_calls()), 1)


def check_accept_matches_what_is_handled(page):
    """The file picker offers exactly what submitRunner knows what to do with."""
    with check("the file input accepts every type the page handles, and no other"):
        offered = page.script(
            "document.getElementById('runnerfile').getAttribute('accept')")
        got = sorted({e.strip().lstrip(".").lower() for e in offered.split(",") if e.strip()})
        want = sorted(set(DISK_IMAGES) | set(RUNNER_ROUTES))
        expect("the accepted extensions", got, want)


def check_monitor_reads_the_address_typed(page):
    """A hex address that looks like a number is read as the address it is."""
    page.show_page("showterminal")

    page.stub.await_quiet()
    page.stub.clear()
    with check("m 1e00 reads from $1e00"):
        page.command("m 1e00")
        call = page.stub.await_call("/v1/machine:readmem")
        if call is None:
            raise Failure("no read arrived; the page sent %r" % page.stub.rest_calls())
        # Parsed as a number, 1e00 is 100, and the page would read $0100.
        expect("the address", call.query.get("address"), "1e00")

    page.stub.await_quiet()
    page.stub.clear()
    with check("d 1e00 disassembles from $1e00"):
        page.command("d 1e00")
        call = page.stub.await_call("/v1/machine:readmem")
        if call is None:
            raise Failure("no read arrived; the page sent %r" % page.stub.rest_calls())
        expect("the address", call.query.get("address"), "1e00")

    page.stub.await_quiet()
    page.stub.clear()
    with check("m 1e00 1e10 reads the 16 bytes between them"):
        page.command("m 1e00 1e10")
        call = page.stub.await_call("/v1/machine:readmem")
        if call is None:
            raise Failure("no read arrived; the page sent %r" % page.stub.rest_calls())
        expect("the address", call.query.get("address"), "1e00")
        expect("the length", call.query.get("length"), "16")
        # Parsed, 1e10 is 10000000000, which the page clamps to $10000 and then
        # walks to in 16-byte reads.
        expect("the number of reads", len(page.stub.calls_to("/v1/machine:readmem")), 1)

    page.stub.await_quiet()
    page.stub.clear()
    with check("f 1e00 1e01 ff fills from $1e00"):
        page.command("f 1e00 1e01 ff")
        call = page.stub.await_call("/v1/machine:writemem")
        if call is None:
            raise Failure("no write arrived; the page sent %r" % page.stub.rest_calls())
        expect("the method", call.method, "POST")
        expect("the address", call.query.get("address"), "1e00")
        expect("the bytes", call.body, b"\xff\xff")


def check_monitor_title_bar(page):
    """The dialog's title is a list item in its header, and sits inside it."""
    page.show_page("showterminal")
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
    page.show_page("showtokenizer")
    page.click("uppercaseBASIC")
    with check("ticking Uppercase puts the BASIC editor in upper case"):
        expect("the editor", page.script(
            "document.getElementById('basiceditor').classList.contains('uppercase')"), True)

    reloaded = page.reload_keeping_form_state()
    page.show_page("showtokenizer")
    with check("the editor is still in upper case after the page is loaded again"):
        if not reloaded:
            check_skip("this browser handed the whole page back from its cache instead "
                       "of running it again, so nothing here would be the page's doing")
        elif not page.script("document.getElementById('uppercaseBASIC').checked"):
            check_skip("this browser restored no tick for the page to follow")
        else:
            expect("the editor", page.script(
                "document.getElementById('basiceditor').classList.contains('uppercase')"),
                True)
    page.click("uppercaseBASIC")


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
            check_power_menu(page)
            check_firmware_version(page)
            check_power_off_sends(page)
            check_power_off_cancelled(page)

            page.show_page("showrunner")
            check_disk_images_are_mounted(page, directory)
            check_uppercase_extensions(page, directory)
            check_programs_still_run(page, directory)
            check_mount_is_reported(page, directory)
            check_unknown_extension_sends_nothing(page, directory)
            check_accept_matches_what_is_handled(page)

            check_monitor_reads_the_address_typed(page)
            check_monitor_title_bar(page)
            check_uppercase_survives_a_reload(page)
        finally:
            page.close()
    finally:
        stub.close()


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
        with tempfile.TemporaryDirectory(prefix="web-index-", dir=upload_directory()) as directory:
            run(args, directory)
    except (Failure, browser_lib.Unavailable) as exc:
        suite_fail("web_index", str(exc))
        return 1
    suite_ok("web_index")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
