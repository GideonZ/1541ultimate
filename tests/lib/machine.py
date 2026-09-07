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

Two axes, and they answer different questions
---------------------------------------------

*Capability* is what a machine has. An Ultimate II+ is a cartridge with no
keyboard of its own and no Interface Type to choose. A C64 Ultimate keeps its
file browser inside a launcher, searches CommoServe rather than Assembly 64,
draws rounded window corners and maps the function keys differently. None of
that moves when firmware moves: it is what the product is. Capability is
answered by a property on `Machine`, and by a probe wherever a cheap and
reliable probe exists, which is the rule `api.find_padded_enum` already
follows: it asks the device which of its stores holds a padded enum rather
than assuming a name.

*Firmware vintage* is what a release does. A machine can run a release line
that lags this branch, and a gap like that closes when the fix is backported,
so it describes a release rather than a product.

One question decides which axis a difference belongs to: would flashing this
branch's firmware on the machine give it the behaviour? Yes makes it vintage,
no makes it a capability. A cartridge will never grow a keyboard, so that is a
capability; a lagging release lists long FTP names as soon as it takes the
commit, so that is vintage.

Tagging a check with the fix it needs
-------------------------------------

Vintage is declared rather than probed, and deliberately so: a probe for "does
readmem refuse length=0" is the assertion the check makes, so a check that
probed first would skip in exactly the cases where it would otherwise fail and
prove nothing at all. Capability keeps the probe; vintage gets the table.

FIXES below is that table. Each entry is one outstanding gap: a fix named
after the behaviour a machine gains from it rather than after a date, and the
machine kinds that do not have it yet. A fix every machine has is not in the
table at all. A check declares what it depends on in one line:

    LABEL = "a Telnet session survives a screen it cannot drain"
    if device.machine.skip_without_fix(
            machine.TELNET_SEND_TOLERATES_SLOW_PEER, LABEL):
        return
    with check(LABEL):
        ...

Where the machine lacks the fix, that line reports the check as SKIP with the
fix name and the machine in the reason, so it stands out in the log and never
reads as a pass. Everywhere else it runs as usual. The same line in front of a
scenario skips the group with one reason, for a scenario whose checks all need
the same fix.

To find out whether a backport has arrived, run with the fix assumed present:

    E2E_ASSUME_FIX=monitor-d-key-reserved      one fix, or a list of them
    E2E_ASSUME_FIX=all                         every fix in the table

`run-tests --assume-fix NAME` sets that variable for the suites it starts. The
tagged checks then run on the machine that was skipping them and either pass,
which says the fix has landed and the entry can be amended, or fail, which
says it has not. `skip_without_fix` tags the check it lets through with the
entry and the machine (`report.note_assumed_fix`), so `tools/stale_gates.py`
can read a run's JSONL afterwards and say which entries a landed backport
made stale without anyone having to comb the log for them.
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from collections.abc import Callable

from report import check_skip, check_start, note_assumed_fix

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


class UnknownFix(ValueError):
    """A fix name the table does not define. The message lists the real ones."""


@dataclass(frozen=True)
class Fix:
    """One firmware fix and the machines that do not have it yet."""

    name: str
    behaviour: str
    lacking: tuple[str, ...]


# The one table. Every entry is an outstanding gap, so an unlisted fix is one
# every machine has and its checks run everywhere.
#
# Amending it when a backport lands: confirm first with
# `run-tests --assume-fix <name>`, which runs the tagged checks on the machine
# that was skipping them, then delete that kind from `lacking`. Delete the
# whole entry once `lacking` would be empty; the checks tagged with it then
# run everywhere again and no suite needs editing.
FIXES: dict[str, Fix] = {}


def _fix(name: str, behaviour: str, lacking: tuple[str, ...]) -> str:
    """Add one entry to the table and hand back its tag, for a named constant."""
    FIXES[name] = Fix(name=name, behaviour=behaviour, lacking=lacking)
    return name

# What tests/e2e/network/telnet_sustained_input_test.py asserts, and an
# outstanding defect rather than a lagging release: GideonZ/1541ultimate#820.
# A screen that repaints on every keystroke outruns a slow link, SO_SNDTIMEO
# expires, and SocketStream::transmit treats the resulting EAGAIN as fatal and
# closes the session. Listed against the Ultimate II+ because that is the
# machine on WiFi here, where the check measures something: it failed about 25
# times across a soak with no passes. A wired machine drains faster than the
# suite can send and passes without exercising the path, which is why the entry
# does not list the others. Delete this entry when #820 is fixed and the check
# runs again everywhere.
TELNET_SEND_TOLERATES_SLOW_PEER = _fix(
    "telnet-send-tolerates-slow-peer",
    "a Telnet session survives a screen repainting faster than the link "
    "drains, rather than being closed when the send buffer stays full",
    (U2,))

# The bench Ultimate II+L's flashed 3.15 predates this tree's monitor rework:
# its help page names "Open monitor", "Close monitor" and "Leave edit" where
# this one names "Back a level", "Copy/Paste" and "Follow/Return". Goes when
# that machine is reflashed from this tree.
MONITOR_EXIT_AND_BACK_KEYS = _fix(
    "monitor-exit-and-back-keys",
    "the machine code monitor offers the Back action and the layer model that "
    "tests/e2e/monitor/monitor_test.py drives",
    (U2,))

# What tests/e2e/network/ident_service_switch_test.py asserts: turning the
# ident service on makes it answer within a few seconds, live, without a
# restart. Measured on the bench u2 running 3.15: the switch is accepted and
# ident never answers, so the suite fails on its first check.
IDENT_SWITCHES_LIVE = _fix(
    "ident-switches-live",
    "the ident service starts answering when it is switched on, without a "
    "firmware restart",
    (U2,))

# UCI_COMPLETES_AN_REU_COMMAND (issue #740) is closed: measured on an
# Ultimate II+L on c8b7551a, uci_targets_test passes all 37 checks ungated.

# The one entry where the machine is behind the tree rather than beside it.
# This branch's monitor has no Debug mode: "Dbg" appears nowhere in
# software/monitor/, and the key is reserved so that pressing D changes
# nothing. The Ultimate II+L on this bench runs a flashed 3.15 that still has
# it, and answers D by putting "Dbg" in the monitor header. Both report
# firmware 3.15, so the version cannot tell them apart; only the behaviour can.
# Reflashing that machine from this tree closes the gap and the entry goes.
MONITOR_D_KEY_RESERVED = _fix(
    "monitor-d-key-reserved",
    "the monitor reserves D for a future Debug mode and opens nothing with "
    "it, rather than entering the Debug mode this branch removed",
    (U2,))

# Every fix at once, for a sweep that asks whether the lagging line has caught
# up rather than about one behaviour.
ASSUME_ALL = "all"
# Read at import, because run-tests starts each suite as its own process and a
# flag it parsed cannot reach them any other way. Same convention as
# report.py's E2E_SUITE and E2E_JSONL.
ASSUME_ENV = "E2E_ASSUME_FIX"


def parse_assumptions(text: str) -> frozenset[str]:
    """The fix names in a comma or space separated list, checked against the table.

    A typo has to be refused rather than ignored. An assumption naming nothing
    leaves the check skipped, which is the answer the caller was trying to get
    past, and a run that silently did nothing is the hardest kind to notice.
    """
    names = {part for part in re.split(r"[,\s]+", text or "") if part}
    unknown = sorted(name for name in names
                     if name != ASSUME_ALL and name not in FIXES)
    if unknown:
        raise UnknownFix(
            f"no such fix: {', '.join(repr(name) for name in unknown)}; "
            f"tests/lib/machine.py lists {', '.join(sorted(FIXES))} "
            f"and {ASSUME_ALL!r}. A name that has gone from the table is a fix "
            f"every machine has, so its checks already run everywhere")
    return frozenset(names)


_assumed: frozenset[str] = parse_assumptions(os.environ.get(ASSUME_ENV, ""))


def assume(*names: str) -> None:
    """Treat these fixes as present wherever a check asks for one.

    For a harness taking the flag in its own process. The suites it starts as
    child processes are told through ASSUME_ENV, which is read at import.
    """
    global _assumed
    _assumed = _assumed | parse_assumptions(" ".join(names))


def assumed() -> frozenset[str]:
    """The fixes this run has been told to treat as present."""
    return _assumed


def forget_assumptions() -> None:
    """Drop them again, for a test that sets its own."""
    global _assumed
    _assumed = frozenset()


@dataclass(frozen=True)
class Machine:
    """What a device is, and the properties a suite adapts to."""

    kind: str
    product: str
    # What /v1/info reported, when the caller had it. It names the machine in
    # a skip reason and decides nothing: which firmware carries which fix is
    # the table's business, because the two release lines number themselves
    # independently and 1.2.0 is not an older 3.15.
    firmware: str = ""

    @property
    def launcher_browser_entry(self) -> str | None:
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
    def search_menu_entry(self) -> str:
        """The menu entry that opens the online search's query form.

        Both services draw the same form: a column of criteria fields over a
        "<<  Submit  >>" button. Where the entry lives differs, which is what
        `search_in_launcher` says.
        """
        return "COMMOSERVE FILE SEARCH" if self.kind == C64U else "Assembly 64"

    @property
    def search_in_launcher(self) -> bool:
        """Whether the search entry is in the launcher or in the task menu.

        A C64 Ultimate lists "COMMOSERVE FILE SEARCH" as the launcher's second
        entry, under the file browser, and its task menu has no search entry at
        all. The other two put "Assembly 64" first in the task menu and have no
        launcher.
        """
        return self.kind == C64U

    @property
    def search_form_title(self) -> str:
        """The title the online search's query form draws."""
        return ("CommoServe File Search" if self.kind == C64U
                else "Assembly 64 Query Form")

    @property
    def rest_workers(self) -> int:
        """How many REST calls this machine can be asked for at once.

        An Ultimate II+ has only a wireless link: three concurrent workers
        through the 77-route sweep took an Ultimate II+L off the network for
        several minutes. The others answer three at a time.
        """
        return 1 if self.kind == U2 else 3

    @property
    def task_menu_key(self) -> str:
        """The key that opens the task menu over the file browser.

        The status row says so: "F1=MENU F3/F5=PGUP/DN F7=HELP" on a C64
        Ultimate, "F5=MENU F1/F7=PGUP/DN F3=HELP" on the others.
        """
        return "F1" if self.kind == C64U else "F5"

    # Which letters the file browser reads as cursor movement follows the
    # "Navigation Style" setting, so it is read from the device; see
    # tests/lib/navigation.py.

    @property
    def page_up_key(self) -> str:
        return "F3" if self.kind == C64U else "F1"

    @property
    def page_down_key(self) -> str:
        return "F5" if self.kind == C64U else "F7"

    @property
    def help_key(self) -> str:
        return "F7" if self.kind == C64U else "F3"

    @property
    def described(self) -> str:
        """The machine and its firmware, for a reason someone has to act on."""
        return f"{self.product} {self.firmware}".strip()

    def has_fix(self, name: str) -> bool:
        """Whether this machine's firmware carries the fix `name` tags.

        A name with no table entry is a fix nothing lacks, so it answers True:
        that is what makes deleting a propagated fix from the table a one-line
        edit rather than an edit per tagged check.
        """
        entry = FIXES.get(name)
        if entry is None:
            return True
        return (ASSUME_ALL in _assumed or name in _assumed
                or self.kind not in entry.lacking)

    def missing_fix(self, name: str) -> str | None:
        """Why a check tagged `name` cannot run here, or None when it can."""
        if self.has_fix(name):
            return None
        return f"needs the {name} fix, which {self.described} does not have"

    def assumed_fix(self, name: str) -> bool:
        """Whether `name` answers has_fix() True here only because it was
        assumed, rather than because this machine has it or never lacked it.

        The one case skip_without_fix runs the check instead of skipping it
        without the firmware having actually changed; see note_assumed_fix.
        """
        entry = FIXES.get(name)
        return (entry is not None and self.kind in entry.lacking
                and (ASSUME_ALL in _assumed or name in _assumed))

    def skip_without_fix(self, name: str, label: str) -> bool:
        """Report `label` as skipped, and answer True, when the fix is absent.

        The one line a tagged check needs, and the caller returns on True:

            if device.machine.skip_without_fix(
                    machine.MONITOR_D_KEY_RESERVED, LABEL):
                return
            with check(LABEL):
                ...

        The skipped check keeps its own numbered line, and the reason on it
        names both the fix and the machine, so a check that did not run is
        never left looking green. Answering with a bool rather than raising is
        what keeps this to the reporting the library already has.
        """
        reason = self.missing_fix(name)
        if reason is None:
            if self.assumed_fix(name):
                note_assumed_fix(name, self.kind)
            return False
        check_start(label)
        check_skip(reason)
        return True

    def __str__(self) -> str:
        return self.product


# What a caller can hand back from `fetch_product`: the product on its own, or
# the product and the firmware version when it has both.
Reported = str | tuple[str, str]


def classify(product: str, firmware: str = "") -> Machine:
    """The machine a `/v1/info` product string names."""
    for needle, kind in _PRODUCTS:
        if needle.lower() in product.lower():
            return Machine(kind=kind, product=product, firmware=firmware)
    raise UnknownMachine(
        f"unknown product {product!r}: this run cannot tell which machine it "
        f"is aimed at, so it cannot choose the right menu layout")


_cache: dict[str, Machine] = {}


def identify(host: str, fetch_product: Callable[[], Reported]) -> Machine:
    """The machine `host` is, fetching its product string at most once.

    `fetch_product` is supplied by the caller rather than built here, so this
    module needs no REST client of its own and a test can drive it without a
    device. It returns the product, or the product and the firmware version as
    a pair when the caller has both: one `/v1/info` answer carries the two, and
    the version is what a skip reason names the machine by.
    """
    cached = _cache.get(host)
    if cached is None:
        reported = fetch_product()
        if isinstance(reported, str):
            cached = classify(reported)
        else:
            product, firmware = reported
            cached = classify(product, firmware)
        _cache[host] = cached
    return cached


def forget(host: str | None = None) -> None:
    """Drop what was learnt, for a test that identifies more than one machine."""
    if host is None:
        _cache.clear()
    else:
        _cache.pop(host, None)
