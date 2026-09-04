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
from collections.abc import Callable

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

# What the machine:measure leak check of tests/e2e/api/readmem_writemem_test.py
# asserts, and the second reason a check must not run without the fix: the
# unsupported path allocated 64KB before answering 501 and never freed it, so
# the 25 calls the check makes leak 1.6MB. On a C64 Ultimate 1.2.0 that
# exhausts the heap and the device stops answering REST, ICMP and FTP
# altogether; recovery is a mains power cycle by hand. Observed on 2026-09-02,
# where the check reported FAIL after 91s and took the machine with it.
MEASURE_FREES_ITS_BUFFER = _fix(
    "measure-frees-its-buffer",
    "GET /v1/machine:measure frees its buffer when it answers 501, so "
    "repeating an unsupported call does not exhaust the heap",
    (C64U,))

# Measured with a socket probe against each machine, and the reason
# browser-filesystem-refresh cannot watch a directory through FTP and Telnet at
# the same time on a C64 Ultimate. Telnet and FTP are served from one pool, and
# a passive transfer's data connection counts against it:
#
#     telnet 0 + 2 FTP controls + data   ok on c64u and u64
#     telnet 0 + 3 FTP controls + data   reset on c64u, ok on u64
#     telnet 1 + 1 FTP control  + data   ok on c64u and u64
#     telnet 1 + 2 FTP controls + data   reset on c64u, ok on u64
#
# So a C64 Ultimate 1.2.0 serves three, an Ultimate 64 at least five. HTTP is a
# separate pool and does not compete. A suite that holds a Telnet session and
# reads a directory over FTP is already at three with nothing spare, and the
# device resets whichever socket asked for the fourth.
SERVES_FOUR_TELNET_FTP_SOCKETS = _fix(
    "serves-four-telnet-ftp-sockets",
    "Telnet and FTP together can hold four concurrent sockets, so a directory "
    "can be watched over FTP while a Telnet session is open",
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

# Measured with tests/e2e/filemanager/browser_filesystem_refresh_test.py: a
# write made from the Telnet browser did not reach the on-screen menu browser,
# which went on showing the old size, while the same write made over FTP
# reached every observer including the menu.
BROWSER_REFRESH_FROM_TELNET_WRITER = _fix(
    "browser-refresh-from-telnet-writer",
    "a file written from the Telnet browser is re-read by the on-screen menu "
    "browser, so both show the committed size",
    (C64U,))

# The same pair of browsers, the other way round, and a separate entry because
# a machine can have one and not the other: when this gap was first written
# down, a write made from the menu did reach the Telnet browser. Measured on a
# C64 Ultimate 1.2.0, it does not: with the menu as the writer, the Telnet
# observer showed each of wmenu1.d64, pmenu1.tst and vmenu1.tst at size 0 and
# never re-read it, while the menu's own browser, FTP and REST all saw the
# committed size, and an FTP writer reached the Telnet observer normally.
BROWSER_REFRESH_FROM_MENU_WRITER = _fix(
    "browser-refresh-from-menu-writer",
    "a file written from the on-screen menu browser is re-read by the Telnet "
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
#
# The Ultimate II+L on this bench is here for the same reason and not because
# it is a cartridge: its flashed 3.15 predates this tree's monitor rework, and
# Back leaves the monitor from a memory view instead of returning a layer.
# Measured on u2@c64u: one ARROW_LEFT from the hex view put the file browser on
# screen, and the check that retypes a command argument uses that key, so the
# checks behind it ran against a browser and failed on a monitor that was
# working. It also still answers D with the Debug mode this tree removed; see
# MONITOR_D_KEY_RESERVED. Reflashing that machine from this tree closes both,
# and 17 checks of the suite pass there in the meantime, so this entry costs
# real coverage and should go as soon as it can.
MONITOR_EXIT_AND_BACK_KEYS = _fix(
    "monitor-exit-and-back-keys",
    "the machine code monitor offers the Back action and the layer model that "
    "tests/e2e/monitor/monitor_test.py drives",
    (C64U, U2))

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
# fix is c90d834a in software/api/routes.h: `ArgsURI::ClearAll` released the
# disk name that `enforce_diskname` had duplicated with strdup using delete,
# which reaches heap_4 with an address that is not a block start. The
# rest-api-coverage cases that only ask for a refusal take the device down the
# same way, because the name is duplicated before the path is checked.
#
# A run that reaches one of these on a machine without the fix loses the device
# and every suite after it, so the whole of create-disk-image is tagged rather
# than one case in it.
FILES_CREATE_IMAGE_SURVIVES = _fix(
    "files-create-image-survives",
    "PUT /v1/files/{path}:create_* answers, rather than taking the device off "
    "the network until it is power cycled",
    (C64U,))

# Measured with tests/e2e/api/rest_api_coverage_test.py: GET
# /v1/configs/No%20Such%20Category answers HTTP 404 on firmware with the fix,
# and HTTP 200 with an empty errors list on a C64 Ultimate 1.2.0, so a caller
# cannot tell a store it does not have from one that is empty.
CONFIGS_REFUSE_UNKNOWN_CATEGORY = _fix(
    "configs-refuse-unknown-category",
    "GET /v1/configs/<store> answers HTTP 404 for a store this machine does "
    "not have, rather than 200 with nothing in it",
    (C64U,))

# Measured with tests/e2e/api/rest_api_coverage_test.py against a C64 Ultimate
# 1.2.0: PUT /v1/configs/<store>/<item>/<value>, the form that carries the
# value as a third path element, answers HTTP 400 "Function none requires
# parameter value". Only the ?value= form is served there, so the route that
# software/api/route_configs.cc names setConfigItemByPath does not exist yet.
CONFIGS_SET_VALUE_IN_PATH = _fix(
    "configs-set-value-in-path",
    "PUT /v1/configs/<store>/<item>/<value> sets the item, rather than "
    "refusing the request for want of a value argument",
    (C64U,))

# Measured the same way: PUT /v1/configs:load_from_flash closes the connection
# on a C64 Ultimate 1.2.0, which reaches the client as
# "[Errno 104] Connection reset by peer". The device answers again afterwards,
# so this is the request failing rather than the machine going down, but a
# check cannot tell a flash round trip happened.
CONFIGS_FLASH_ROUNDTRIP = _fix(
    "configs-flash-roundtrip",
    "PUT /v1/configs:load_from_flash answers, so a save and load round trip "
    "can be read back",
    (C64U,))

# Two fields GET /v1/info carries on the 3.15 line and not on a C64 Ultimate
# 1.2.0. Measured on the bench: u64 and u2 both report `git_commit_hash`,
# `ethernet_mac` and `wifi_mac`; c64u reports product, firmware_version,
# fpga_version, core_version, hostname and unique_id and none of the three.
# They are separate entries because they are separate additions to the route
# and can be backported one at a time.
INFO_REPORTS_INTERFACES = _fix(
    "info-reports-interfaces",
    "GET /v1/info reports each network interface's MAC address, so a run can "
    "say which machine it was talking to from the answer alone",
    (C64U,))

INFO_NAMES_ITS_COMMIT = _fix(
    "info-names-its-commit",
    "GET /v1/info reports git_commit_hash, so what is running can be tied to "
    "a commit rather than to a version string two release lines share",
    (C64U,))

# What tests/e2e/network/ident_service_switch_test.py asserts: turning the
# ident service on makes it answer within a few seconds, live, without a
# restart. Measured on the bench u2 running 3.15: the switch is accepted and
# ident never answers, so the suite fails on its first check.
IDENT_SWITCHES_LIVE = _fix(
    "ident-switches-live",
    "the ident service starts answering when it is switched on, without a "
    "firmware restart",
    (U2,))

# What tests/e2e/uci/uci_targets_test.py's issue-740 matrix drives. A command
# naming an absent REU image leaves the command interface in Command Busy and
# it never returns to idle: every later command on every target is refused with
# $11/$15 until the firmware restarts. The suite names the issue itself, and a
# suite that wedges the interface it is testing cannot then test it.
UCI_SURVIVES_A_MISSING_IMAGE = _fix(
    "uci-survives-a-missing-image",
    "the command interface returns to idle after a command that names a file "
    "the device does not have, rather than wedging until a restart",
    (U2,))

# What tests/e2e/io/command_interface/uci_targets_test.py's
# save-reu-offset-past-end scenario drives. Measured on u2@c64u, 2026-09-04:
# SAVE_REU with the preload offset at the end of a 128 KB REU never leaves
# Command Busy, and the interface stays wedged for every target afterwards.
# The same wedge as the entry above, reached through a different command, so
# it is named separately: an Ultimate II+ that gains one need not gain both.
UCI_REPORTS_AN_OVERSIZE_REU_OFFSET = _fix(
    "uci-reports-an-oversize-reu-offset",
    "SAVE_REU answers with the offset and saves nothing when the preload "
    "offset is at the end of the REU, rather than wedging until a restart",
    (U2,))

# The heap reading the health sweep already reports as absent on this machine:
# GET /v1/machine:heap is not served by a C64 Ultimate 1.2.0, so nothing can
# assert a plausible figure from it.
MACHINE_HEAP_READING = _fix(
    "machine-heap-reading",
    "GET /v1/machine:heap reports the free and total heap",
    (C64U,))

# The one entry here that names an FPGA core rather than firmware. A write to
# $DF01 has to stop the CPU in its own cycle, or the register writes behind it
# land on a transfer that is still running; tests/e2e/io/c64/reu_turbo_test.py
# is the discriminator. Measured by swapping bitstreams on an Ultimate 64 with
# the same Nios ELF after each: core 1.4E fails at round 0 while the same
# program is clean at 1 MHz, and core 1.4F passes. A C64 Ultimate serves core
# 1.4D, which is older than either, and fails the same way.
REU_TURBO_STOPS_CPU_IN_CYCLE = _fix(
    "reu-turbo-stops-cpu-in-cycle",
    "a write to $DF01 stops the CPU in its own cycle, so an REU transfer "
    "started at full speed returns what it was given",
    (C64U,))

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

# What tests/e2e/network/ident_service_switch_test.py asserts. Firmware without
# the fix reads each network service switch once, when that listener's task
# starts, so turning the Ultimate Ident Service off leaves the listener
# answering until the device restarts; with it, the listener closes its socket
# when the setting changes and opens one again when it is turned back on.
# Measured on a C64 Ultimate 1.2.0, which kept answering for the 3 seconds the
# check waits after the switch was set to Disabled, three attempts out of
# three.
SERVICE_SWITCHES_APPLY_LIVE = _fix(
    "service-switches-apply-live",
    "a network service switch takes effect while the device runs, so the "
    "ident listener stops answering when it is disabled and answers again "
    "when it is re-enabled",
    (C64U,))

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
    def rest_workers(self) -> int:
        """How many REST calls this machine can be asked for at once.

        Two machines are asked for one at a time, for different reasons.

        An Ultimate II+ has no wired interface, so every call crosses its
        wireless link. Measured on an Ultimate II+L in a C64 Ultimate: three
        concurrent workers through a 77-route sweep took the cartridge off the
        network entirely for several minutes, with ping getting no answer at
        all, and it came back by itself as soon as the load stopped.

        A C64 Ultimate 1.2.0 mixes two answers together under three workers.
        Measured on the bench: a request for `/v1/help` came back with status
        404 carrying another request's whole 200 response as its body, headers
        and all. That is a firmware defect and asking for one call at a time
        does not fix it, it only stops this sweep tripping over it; the checks
        themselves all still run.

        An Ultimate 64 answers three at a time and is unaffected.
        """
        return 1 if self.kind in (U2, C64U) else 3

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

    def missing_fix(self, name: str) -> str | None:
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
