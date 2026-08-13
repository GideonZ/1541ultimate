"""Which machine a target is, asked of the device rather than configured.

Three machines serve the same REST API and the same menu, and they differ in
ways a suite has to know about:

    Ultimate 64      its own computer, its own keyboard, Assembly 64
    Ultimate II+     a cartridge: no keyboard of its own (see targets.py)
    C64 Ultimate     its own computer, CommoServe, and a launcher menu in
                     front of the file browser

Nothing here is configured or passed on a command line. `GET /v1/info` names
the product, so a run aimed at a host discovers what that host is, and a suite
asks for the property it needs rather than for the model name. That keeps the
model checks out of the suites: a suite that wants to know where the file
browser is asks `menu_opens_on_launcher`, not `is_c64u`.

The answer is a property of the machine and cannot change during a run, so it
is fetched once per host and kept.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Dict, Optional

# The three machines, by the name used in messages and in this module's API.
U64 = "Ultimate 64"
U2 = "Ultimate II+"
C64U = "C64 Ultimate"

# Matched against the `product` field of /v1/info, most specific first: an
# Ultimate 64 reports "Ultimate 64" or "Ultimate 64 Elite", an Ultimate II+
# reports "Ultimate II+" or "Ultimate II+L", and a C64 Ultimate reports
# "C64 Ultimate". "Ultimate 64" is tested after "C64 Ultimate" only for
# clarity; the strings do not overlap.
_PRODUCTS = (
    ("C64 Ultimate", C64U),
    ("Ultimate II", U2),
    ("Ultimate 64", U64),
)


class UnknownMachine(ValueError):
    """A product string this module has no rules for. The message is shown."""


@dataclass(frozen=True)
class Machine:
    """What a device is, and the properties a suite adapts to."""

    kind: str
    product: str

    @property
    def launcher_browser_entry(self) -> Optional[str]:
        """The launcher entry leading to the file browser, or None.

        A C64 Ultimate does not put the file browser behind the menu button.
        The button opens a launcher listing the browser, the online search and
        the settings screens, and the browser is its first entry. The other
        two machines open the browser directly and have no launcher.
        """
        return "DISK FILE BROWSER" if self.kind == C64U else None

    @property
    def menu_opens_on_launcher(self) -> bool:
        """Whether the menu button opens something other than the browser."""
        return self.launcher_browser_entry is not None

    @property
    def back_presses_to_close_menu(self) -> int:
        """Back presses from the file browser until the menu closes.

        One on most machines: the browser is the top of the object stack, so
        leaving it leaves the menu. Two on a C64 Ultimate, where the launcher
        sits between the browser and the closed menu. A caller uses this to
        prove nothing is stacked on top of the browser, which is a thing the
        screen cannot show: measured on a C64 Ultimate, RUN/STOP in the
        browser returns to the launcher rather than closing the menu.
        """
        return 2 if self.kind == C64U else 1

    @property
    def search_service(self) -> str:
        """The name of the online search this machine offers."""
        return "CommoServe" if self.kind == C64U else "Assembly 64"

    @property
    def task_menu_key(self) -> str:
        """The key that opens the task menu over the file browser.

        F5 on an Ultimate 64 and an Ultimate II+. A C64 Ultimate maps the
        function keys differently and says so on its own status row: "F1=MENU
        F3/F5=PGUP/DN F7=HELP". F5 there is Page Down, so pressing it over a
        listing shorter than a screen does nothing at all, which is what a
        suite written for the other two sees.
        """
        return "F1" if self.kind == C64U else "F5"

    @property
    def page_up_key(self) -> str:
        """The key that scrolls a listing back by a screen."""
        return "F3" if self.kind == C64U else "PGUP"

    @property
    def page_down_key(self) -> str:
        """The key that scrolls a listing on by a screen."""
        return "F5" if self.kind == C64U else "PGDN"

    def __str__(self) -> str:
        return self.product


def classify(product: str) -> Machine:
    """The machine a `/v1/info` product string names."""
    for needle, kind in _PRODUCTS:
        if needle.lower() in product.lower():
            return Machine(kind=kind, product=product)
    raise UnknownMachine(
        f"unknown product {product!r}: this run cannot tell which machine it "
        f"is aimed at, so it cannot choose the right menu layout")


_cache: Dict[str, Machine] = {}


def identify(host: str, fetch_product: Callable[[], str]) -> Machine:
    """The machine `host` is, fetching its product string at most once.

    `fetch_product` is supplied by the caller rather than built here, so this
    module needs no REST client of its own and a test can drive it without a
    device.
    """
    cached = _cache.get(host)
    if cached is None:
        cached = classify(fetch_product())
        _cache[host] = cached
    return cached


def forget(host: Optional[str] = None) -> None:
    """Drop what was learnt, for a test that identifies more than one machine."""
    if host is None:
        _cache.clear()
    else:
        _cache.pop(host, None)
