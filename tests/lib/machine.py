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

*Firmware vintage* is what a release does. The Ultimate 64 and the Ultimate
II+ under test run firmware built from this branch. A C64 Ultimate serves the
same endpoints from a separate release line that lags behind it: 1.2.0 has
neither the FTP listing fix nor the readmem length check below. That gap
closes when the fix is backported, so it describes a release rather than a
product.

One question decides which axis a difference belongs to: would flashing this
branch's firmware on the machine give it the behaviour? Yes makes it vintage,
no makes it a capability. A cartridge will never grow a keyboard, so that is a
capability; a C64 Ultimate will list long FTP names as soon as it takes the
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

    LABEL = "a 100-character name survives the listing"
    if device.machine.skip_without_fix(machine.FTP_LISTING_FULL_LENGTH, LABEL):
        return
    with check(LABEL):
        ...

Where the machine lacks the fix, that line reports the check as SKIP with the
fix name and the machine in the reason, so it stands out in the log and never
reads as a pass. Everywhere else it runs as usual. The same line in front of a
scenario skips the group with one reason, for a scenario whose checks all need
the same fix.

To find out whether a backport has arrived, run with the fix assumed present:

    E2E_ASSUME_FIX=ftp-listing-full-length     one fix, or a list of them
    E2E_ASSUME_FIX=all                         every fix in the table

`run-tests --assume-fix NAME` sets that variable for the suites it starts. The
tagged checks then run on the machine that was skipping them and either pass,
which says the fix has landed and the entry can be amended, or fail, which
says it has not. Running them as expected failures, so that a landed backport
is reported without anyone having to ask, is the natural next step; it is not
built, because it needs a verdict the report library does not have and a
runner that counts an expected failure as a pass.
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from typing import Callable, Dict, FrozenSet, Optional, Tuple, Union

from report import check_skip, check_start

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
    lacking: Tuple[str, ...]


# The one table. Every entry is an outstanding gap, so an unlisted fix is one
# every machine has and its checks run everywhere.
#
# Amending it when a backport lands: confirm first with
# `run-tests --assume-fix <name>`, which runs the tagged checks on the machine
# that was skipping them, then delete that kind from `lacking`. Delete the
# whole entry once `lacking` would be empty; the checks tagged with it then
# run everywhere again and no suite needs editing.
FIXES: Dict[str, Fix] = {}


def _fix(name: str, behaviour: str, lacking: Tuple[str, ...]) -> str:
    """Add one entry to the table and hand back its tag, for a named constant."""
    FIXES[name] = Fix(name=name, behaviour=behaviour, lacking=lacking)
    return name


# What tests/e2e/network/ftp_server_test.py asserts: a 100-character name is
# stored, listed unchanged, and removed by the name the listing reported. A
# C64 Ultimate copies 63 characters and a terminator into the listing, and
# DELE then refuses the truncated name, so the file cannot be removed at all.
FTP_LISTING_FULL_LENGTH = _fix(
    "ftp-listing-full-length",
    "the FTP server lists a name of up to 127 characters in full, so a name "
    "taken from a listing addresses the file it names",
    (C64U,))

# What the bounds scenario of tests/e2e/api/readmem_writemem_test.py asserts.
# The rejection keeps a zero-size request away from the allocator, because
# malloc returns NULL for one; a C64 Ultimate answers 200 with an empty body.
READMEM_REJECTS_ZERO_LENGTH = _fix(
    "readmem-rejects-zero-length",
    "GET /v1/machine:readmem answers HTTP 400 to length=0 rather than 200 "
    "with an empty body",
    (C64U,))

# Measured with tests/e2e/io/c64/freeze_menu_test.py, and the reason that
# suite must not run without the fix: on firmware without it, opening the menu
# while Interface Type is Freeze stops the device answering REST, ICMP and FTP
# altogether. Recovery is physical, a five-second menu-button hold or a power
# cycle, so a run that reaches this check loses the device and every suite
# after it. See GideonZ/1541ultimate#733.
FREEZE_MENU_OPENS = _fix(
    "freeze-menu-opens",
    "opening the menu with Interface Type set to Freeze answers, rather than "
    "taking the device off the network until it is power cycled",
    (C64U,))

# Measured with tests/e2e/filemanager/prg_context_menu_test.py. The boot cart
# shows a 16-character name, and firmware without the fix copies the whole
# name into that field: software/io/c64/c64_subsys.cc now trims into a fixed
# buffer, where it used to strcpy. Running a 101-character name on firmware
# without it leaks the machine subsystem lock, after which every machine: call
# answers HTTP 423, the UI stops taking injected keys, and only a power cycle
# recovers. Observed on a C64 Ultimate 1.2.0: machine:reset, machine:resume
# and machine:reboot all answered 423 while /v1/version and /v1/drives still
# answered 200.
BOOTCART_LONG_NAME_SAFE = _fix(
    "bootcart-long-name-safe",
    "running a PRG whose name is longer than the boot cart's display field "
    "leaves the machine subsystem usable, rather than leaking its lock",
    (C64U,))

# Measured with tests/e2e/filemanager/browser_filesystem_refresh_test.py. A
# browser drains no file-system events while a context menu and its string box
# are up, its queue holds eight, and putEvent drops the rest, so a rename typed
# while other traffic is arriving can lose its own notification. Firmware with
# the fix reconciles afterwards; without it the listing stays stale until the
# directory is left and re-entered, and every later row of that matrix then
# compares against a browser that never caught up.
BROWSER_REFRESH_AFTER_QUEUE_OVERFLOW = _fix(
    "browser-refresh-after-queue-overflow",
    "a browser whose event queue overflowed while a context menu was open "
    "still shows the committed directory afterwards",
    (C64U,))

# Measured with tests/e2e/filemanager/browser_filesystem_refresh_test.py, and
# directly: with a browser open on /Temp, a file created over FTP appeared in
# its listing after 0.42s, while a directory created the same way never
# appeared at all in twelve seconds, though FTP and REST both listed it. The
# rows that rename, delete or create a directory therefore cannot converge,
# and neither can the seed that puts one there in the first place.
BROWSER_REFRESH_ON_DIRECTORY_CHANGE = _fix(
    "browser-refresh-on-directory-change",
    "a directory added or removed by another writer appears in, or leaves, an "
    "open browser's listing without leaving and re-entering the directory",
    (C64U,))

# Measured with tests/e2e/filemanager/browser_filesystem_refresh_test.py. Only
# one direction is affected, which is what makes it a distinct gap rather than
# part of the two above: a write made from the Telnet browser did not reach
# the on-screen menu browser, which went on showing the old size, while the
# same write made from the menu or over FTP reached every observer including
# the menu.
BROWSER_REFRESH_FROM_TELNET_WRITER = _fix(
    "browser-refresh-from-telnet-writer",
    "a file written from the Telnet browser is re-read by the on-screen menu "
    "browser, so both show the committed size",
    (C64U,))

# The machine code monitor on the lagging line is an earlier revision of the
# same program, and tests/e2e/monitor/monitor_test.py asserts this one's
# behaviour throughout rather than in one place. Read from the two help
# screens side by side: this branch names "Back a level", "Copy/Paste" and
# "Follow/Return" at the foot of its help page, where the C64 Ultimate 1.2.0
# monitor names "Open monitor", "Close monitor" and "Leave edit" instead.
# Tagged once for the suite rather than per check, because nearly every check
# depends on some part of it.
MONITOR_EXIT_AND_BACK_KEYS = _fix(
    "monitor-exit-and-back-keys",
    "the machine code monitor offers the Back action and the layer model that "
    "tests/e2e/monitor/monitor_test.py drives",
    (C64U,))

# Measured with tests/e2e/filemanager/cfg_unknown_items_test.py and
# cfg_whitespace_test.py. A .cfg saved on one machine and loaded on another
# names stores and items the reader does not have, and pads its values; both
# are ordinary. Firmware with the fix loads such a file and says "Loading
# configuration successful!", where a C64 Ultimate 1.2.0 puts "There were
# errors." on screen and leaves a dialog the user has to answer.
CFG_LOADS_UNKNOWN_AND_PADDED = _fix(
    "cfg-loads-unknown-and-padded",
    "a CFG naming an item this machine does not have, or holding padded "
    "values, loads without being reported as an error",
    (C64U,))

# Measured with tests/e2e/io/c64/assembly64_test.py against a C64 Ultimate
# 1.2.0, driving the CommoServe query form: with the cursor in the form's Name
# field, PUT /v1/machine:menu_button answered HTTP 200 and the menu stayed
# open for the full 15 seconds the check waits. Firmware with the fix polls the
# button from inside UserInterface::string_edit, so the menu closes. RUN/STOP
# still leaves the field on the machine without it, which is what the suites
# recover with, but a check that presses the button and waits proves nothing
# there except that the fix is absent.
MENU_BUTTON_CLOSES_STRING_EDIT = _fix(
    "menu-button-closes-string-edit",
    "the menu button closes the menu while a modal edit field has focus, "
    "rather than being ignored until the field is left",
    (C64U,))

# Measured with tests/e2e/api/create_disk_image_test.py against a C64 Ultimate
# 1.2.0: the first PUT /v1/files/{path}:create_d64 timed out, and the device
# then answered nothing at all, ICMP included, until it was power cycled. The
# fix is in software/api/route_files.cc's enforce_diskname, where the name
# duplicated with strdup used to be released with delete, which trips heap_4's
# own assertion and stops the firmware. A run that reaches this check on a
# machine without the fix loses the device and every suite after it, which is
# why the whole suite is tagged rather than one case.
FILES_CREATE_IMAGE_SURVIVES = _fix(
    "files-create-image-survives",
    "PUT /v1/files/{path}:create_* answers, rather than taking the device off "
    "the network until it is power cycled",
    (C64U,))

# Every fix at once, for a sweep that asks whether the lagging line has caught
# up rather than about one behaviour.
ASSUME_ALL = "all"
# Read at import, because run-tests starts each suite as its own process and a
# flag it parsed cannot reach them any other way. Same convention as
# report.py's E2E_SUITE and E2E_JSONL.
ASSUME_ENV = "E2E_ASSUME_FIX"


def parse_assumptions(text: str) -> FrozenSet[str]:
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


_assumed: FrozenSet[str] = parse_assumptions(os.environ.get(ASSUME_ENV, ""))


def assume(*names: str) -> None:
    """Treat these fixes as present wherever a check asks for one.

    For a harness taking the flag in its own process. The suites it starts as
    child processes are told through ASSUME_ENV, which is read at import.
    """
    global _assumed
    _assumed = _assumed | parse_assumptions(" ".join(names))


def assumed() -> FrozenSet[str]:
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
    def min_search_result_rows(self) -> int:
        """How many rows a result list has to fill to be one at all.

        The two services do not hold the same corpus, and the count is the
        service's business rather than the firmware's, so this is the floor
        below which the screen is more likely to be an incidental match than a
        listing. Measured live with the term "turrican": Assembly 64 answered
        with 20 matching rows, and CommoServe with the single entry "Turrican
        intro speech (Tel_Jeroen)".
        """
        return 1 if self.kind == C64U else 3

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

    # Which letters the file browser reads as cursor movement is not here: it
    # follows the "Navigation Style" setting, which every machine offers and a
    # person can change, so it is read from the device. See
    # tests/lib/navigation.py.

    @property
    def page_up_key(self) -> str:
        """The key that scrolls a listing back by a screen."""
        return "F3" if self.kind == C64U else "PGUP"

    @property
    def page_down_key(self) -> str:
        """The key that scrolls a listing on by a screen."""
        return "F5" if self.kind == C64U else "PGDN"

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

    def missing_fix(self, name: str) -> Optional[str]:
        """Why a check tagged `name` cannot run here, or None when it can."""
        if self.has_fix(name):
            return None
        return f"needs the {name} fix, which {self.described} does not have"

    def skip_without_fix(self, name: str, label: str) -> bool:
        """Report `label` as skipped, and answer True, when the fix is absent.

        The one line a tagged check needs, and the caller returns on True:

            if device.machine.skip_without_fix(machine.FTP_LISTING_FULL_LENGTH,
                                               LABEL):
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
            return False
        check_start(label)
        check_skip(reason)
        return True

    def __str__(self) -> str:
        return self.product


# What a caller can hand back from `fetch_product`: the product on its own, or
# the product and the firmware version when it has both.
Reported = Union[str, Tuple[str, str]]


def classify(product: str, firmware: str = "") -> Machine:
    """The machine a `/v1/info` product string names."""
    for needle, kind in _PRODUCTS:
        if needle.lower() in product.lower():
            return Machine(kind=kind, product=product, firmware=firmware)
    raise UnknownMachine(
        f"unknown product {product!r}: this run cannot tell which machine it "
        f"is aimed at, so it cannot choose the right menu layout")


_cache: Dict[str, Machine] = {}


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


def forget(host: Optional[str] = None) -> None:
    """Drop what was learnt, for a test that identifies more than one machine."""
    if host is None:
        _cache.clear()
    else:
        _cache.pop(host, None)
