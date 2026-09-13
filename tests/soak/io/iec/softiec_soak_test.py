#!/usr/bin/env python3
"""Soak and stress for the Software IEC drive, driven from a real C64 over the bus.

GideonZ/1541ultimate#877 reports the whole firmware becoming unresponsive while C64
OS starts from a Software IEC partition, at a different point each time. This suite
drives the drive from a real C64 through the IEC agent, so every byte on the bus is
one the KERNAL puts there, while a REST lane and one or more FTP lanes load the device
from the PC side. None of the lanes touches C64 memory, which would disturb a transfer.

It offers two kinds of run, chosen with --mode, and a way to run both one after the
other:

  * soak: the pattern of use a C64 OS session and a person put the drive through. A
    session boots once from the #877 trace and then mostly browses directories, loads
    applications and documents and reads them to the end, saves small settings files
    with @, creates and scratches temporary files, polls the status channel, reads the
    clock, and changes partition or directory now and then, with a few seconds of think
    time between actions. It fails if the firmware stops answering, if data read back
    differs from what was stored, or if the heap keeps shrinking.

  * stress: the same interface pushed as hard as the agent allows, with no think time.
    It fuzzes the command channel and OPEN names, opens as many channels as the KERNAL
    table holds, abandons transfers part way, saves and reads back large files, edits a
    relative file at a high record number, drives direct access at the edges of a D64
    and past them, scratches with wide patterns in a full directory, nests directories
    deep, changes the device number and back, and sends the resets and I with channels
    open. The PC lanes race the bus: FTP fights over files in a shared directory the C64
    also uses, and REST toggles Log Every Operation during bus traffic. The stress run
    fails only on death, on corruption of the read-only fixture files the lanes never
    touch, on the drive ceasing to answer the C64 while REST still answers, or on heap
    that does not come back after an idle settle. Fuzzed answers are only required to be
    two-digit statuses; the exact codes are recorded and reported, not asserted, because
    a hostile command has no single right answer.

  * both: the soak phase then the stress phase, each its own section and check with its
    own heap verdict. The fixture is built once. Defaults are 1800 s of soak and 600 s
    of stress.

The UCI SoftIEC target (software/io/command_interface/softiec_target.cc) is exercised
over a different transport that this C64 agent cannot reach, so it is out of scope here
and is not faked.

--mode selects the run; --duration overrides the seconds of a single soak or stress
run; --soak-duration and --stress-duration set the two phases of a both run. --profile
is kept for run-tests: stress runs the stress mode for ten minutes and soak runs the
soak mode for four hours, and --profile overrides --mode. --seed and --no-lanes work in
every mode; --no-lanes leaves out the REST and FTP lanes so the drive's heap use can be
told from theirs.

On a U2+L, one first session after power-up lost about 25 KB of heap over its first 36
iterations, which failed the ten minute soak, and most of it came back when the session
removed its directory; another passed. A 45 minute session on the same firmware kept the
heap flat from its 20th iteration to its 158th. Judge a loss from a run that is not the
first after power-up, or from a longer one.

The suite needs a C64 with a standard KERNAL, REST and FTP, and one Software IEC
partition numbered 1. It uses device 11, and restores the settings, the partition's
working directory, and removes its own directory.
"""
import argparse
import random
import sys
import threading
import time
import traceback
import uuid
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import ftp  # noqa: E402
from api import UltimateApi  # noqa: E402
from config_snapshot import Snapshot  # noqa: E402
from report import Failure, check, detail, section, suite_fail, suite_ok, teardown_step  # noqa: E402

sys.path.insert(0, bootstrap.directory("e2e", "io", "iec"))
from iec_agent import CLOSE, OPEN, READ_COUNT, READ_TO_EOI, STATUS_BYTES, WRITE, Agent, iec_drive  # noqa: E402

SUITE = "softiec_soak_test"

# The default seconds for each mode, and the two phases of a both run.
SOAK_DEFAULT_SECONDS = 30 * 60
STRESS_DEFAULT_SECONDS = 10 * 60
# What --profile maps to, so run-tests keeps working: the stress profile is a ten
# minute stress run and the soak profile is a four hour soak run.
PROFILE_STRESS_SECONDS = 10 * 60
PROFILE_SOAK_SECONDS = 4 * 60 * 60

# The heap may move while caches fill; what is not allowed is a steady loss. The soak
# measurement starts after the warm-up iterations and allows this much per iteration.
WARMUP_ITERATIONS = 5
LEAK_TOLERANCE_BYTES_PER_ITERATION = 256
# The stress heap is judged before and after, measured after an idle settle rather than
# by a per-iteration slope, because a stress run allocates and frees hard and unevenly.
# The bound is loose on purpose: a first session after power-up can lose about 25 KB to
# caches that never come back (see the module docstring), and the value that matters for
# review is the reported before and after, so only a gross leak fails the run. Judge a
# tighter loss by comparing the reported numbers across runs on a warm device.
STRESS_HEAP_LOSS_TOLERANCE_BYTES = 65536
# How long to leave the drive idle after the lanes stop before the final heap reading,
# so buffers a transfer held are given back.
IDLE_SETTLE_SECONDS = 8.0

REST_LANE_SECONDS = 1.5
FTP_LANE_SECONDS = 4.0
# How many operations the failure report names.
HISTORY = 25


def pattern(name, size):
    """The content of a fixture file: deterministic, different for each file."""
    seed = sum(name.encode("ascii"))
    return bytes(((i * 31 + seed) & 0xFF) for i in range(size))


# The files C64 OS reads at boot, by the paths it uses, with their sizes from the trace.
FIXTURE = {
    "OS/SETTINGS/SYSTEM.T.seq": 400,
    "OS/SETTINGS/CONFIG.T.seq": 13,
    "OS/SETTINGS/COMPONENTS.T.seq": 683,
    "OS/LIBRARY/REDIRECT.O.prg": 18,
    "OS/LIBRARY/WORKSPACE.O.prg": 373,
    "OS/LIBRARY/IEC.LIB.R.prg": 624,
    "OS/KERNAL/MENU.O.prg": 2209,
    "OS/KERNAL/FILE.O.prg": 1338,
    "OS/DRIVERS/KBD.C64.prg": 567,
    "OS/TK/TKVIEW.O.prg": 1694,
}
# The applications and documents the soak loads and reads to the end, as host names.
FIXTURE_DOCUMENTS = ("OS/KERNAL/MENU.O.prg", "OS/KERNAL/FILE.O.prg", "OS/TK/TKVIEW.O.prg",
                     "OS/DRIVERS/KBD.C64.prg", "OS/LIBRARY/WORKSPACE.O.prg")
# WORK is the soak's scratch area, OS/TEMPORARY its throwaway files, RACE the directory
# the stress FTP lanes and the C64 fight over, PCLANE the soak FTP lane's own file.
FIXTURE_DIRS = ("OS", "OS/SETTINGS", "OS/LIBRARY", "OS/KERNAL", "OS/DRIVERS", "OS/TK",
                "OS/TEMPORARY", "WORK", "PCLANE", "RACE")


class Dead(Failure):
    """The firmware stopped answering."""


class Corruption(Failure):
    """Data read back from the drive differed from what was written or stored.

    A subclass of Failure so that a soak reports it like any other failure, and a
    stress run, which tolerates most failures, still fails on this one.
    """


class Session:
    """One suite run: the device, the agent, the fixture, and what happened."""

    def __init__(self, args):
        self.args = args
        self.api = UltimateApi(args.host, args.password, args.timeout)
        self.agent = Agent(self.api)
        self.random = random.Random(args.seed)
        self.folder = "soak" + uuid.uuid4().hex[:6]
        self.here = self.folder.upper()
        self.root = None
        self.policy = None
        self.history = []
        self.iteration = 0
        self.counts = {}
        self.anomalies = {}
        self.statuses = {}
        self.lane_errors = []
        self.lane_counts = {"rest": 0, "ftp": 0}
        self.heap_before = None
        self.heap_after = None
        self.stop = threading.Event()

    def begin_phase(self):
        """Clear the per-phase tallies so a both run reports each phase on its own."""
        self.iteration = 0
        self.counts = {}
        self.anomalies = {}
        self.statuses = {}
        self.lane_errors = []
        self.lane_counts = {"rest": 0, "ftp": 0}
        self.heap_before = None
        self.heap_after = None

    # -- the C64 lane: shared helpers ------------------------------------------------

    def note(self, what):
        self.history.append(f"{self.iteration}: {what}")
        del self.history[:-HISTORY]

    def record_status(self, kind, response):
        """Tally a status a fuzzed step produced, so the report can list them all."""
        code = response[:2] if response else "--"
        self.statuses[code] = self.statuses.get(code, 0) + 1
        self.note(f"{kind} answered {response!r}")

    def anomaly(self, what, exc):
        """Record a KERNAL or DOS error that is an allowed outcome of a hostile step."""
        self.anomalies[what] = self.anomalies.get(what, 0) + 1
        self.note(f"{what} raised {exc}")

    def command(self, text, allowed=None):
        data = text if isinstance(text, bytes) else text.encode("latin-1")
        self.note(f"command {data!r}")
        self.agent.call(WRITE, 15, data)
        response = self.agent.call(READ_TO_EOI, 15, expect=STATUS_BYTES).decode("latin-1").strip()
        code = response[:2]
        if not code.isdigit():
            raise Failure(f"{data!r} answered {response!r}, which is not a status")
        if allowed is not None and int(code) not in allowed:
            raise Failure(f"{data!r} answered {response!r}")
        return response

    def status(self):
        return self.agent.call(READ_TO_EOI, 15, expect=STATUS_BYTES).decode("latin-1").strip()

    def open(self, channel, name, secondary=None):
        data = name if isinstance(name, bytes) else name.encode("latin-1")
        self.note(f"open {channel} {data!r}")
        self.agent.call(OPEN, channel=channel, data=data, secondary=secondary)
        return self.status()

    def close(self, channel):
        self.note(f"close {channel}")
        self.agent.call(CLOSE, channel=channel)

    def read(self, channel, limit=None):
        """Bytes from a channel up to its end, or at most `limit` of them."""
        out = b""
        while limit is None or len(out) < limit:
            want = 254 if limit is None else min(254, limit - len(out))
            block = self.agent.call(READ_COUNT, channel, count=want)
            out += block
            if len(block) < want or (self.agent.last_status & 64):
                break
        return out

    def write_stream(self, channel, data):
        """Sends data to a channel in mailbox sized pieces."""
        for start in range(0, len(data), 254):
            self.agent.call(WRITE, channel, data[start:start + 254])

    def read_file(self, channel, name):
        response = self.open(channel, name)
        try:
            if not response.startswith("00"):
                return response, None
            return response, self.read(channel)
        finally:
            self.close(channel)

    def expect_file(self, channel, name, fixture):
        response, data = self.read_file(channel, name)
        if data is None:
            raise Failure(f"{name} did not open: {response}")
        want = pattern(fixture, FIXTURE[fixture])
        if data != want:
            raise Corruption(f"{name} read {len(data)} bytes that differ from the {len(want)} stored")

    def save_and_verify(self, name, data, replace=False):
        """Writes data to a new or replaced sequential file, reads it back, compares.

        Reading dominates the time: a 254 byte read costs about 0.7 s on this C64, so a
        caller sizes data to keep one iteration bounded.
        """
        prefix = "@:" if replace else ""
        response = self.open(7, f"{prefix}{name},S,W")
        if not response.startswith("00"):
            raise Failure(f"{prefix}{name},S,W answered {response}")
        self.write_stream(7, data)
        self.close(7)
        response, back = self.read_file(8, f"{name},S")
        if back != data:
            raise Corruption(f"{name} saved {len(data)} bytes and read back "
                             f"{len(back or b'')} that differ ({response})")

    def partition(self):
        self.command(bytes([ord("C"), 0xD0, 1]), allowed=(2,))

    def boot(self):
        """The order of operations C64 OS follows at boot, from the #877 trace.

        Run once as the session start, and again as an occasional reboot.
        """
        here = self.here
        self.partition()
        self.command(f"CD//{here}/OS\r", allowed=(0,))
        for low, high in ((0xA4, 0xFE), (0xC5, 0xE5), (0xE8, 0xA6), (0x02, 0x00)):
            self.agent.call(WRITE, 15, bytes([ord("M"), ord("-"), ord("R"), low, high, 2]))
            reply = self.agent.call(READ_TO_EOI, 15, expect=2)
            self.note(f"M-R answered {reply!r}")
        self.command("UI", allowed=(73,))
        self.command("S/TEMPORARY/:*", allowed=(1,))
        self.open(3, "$", secondary=0)
        self.read(3, limit=5)
        self.close(3)
        held = self.open(2, "/SETTINGS/:SYSTEM.T")
        if not held.startswith("00"):
            raise Failure(f"/SETTINGS/:SYSTEM.T did not open: {held}")
        first = self.read(2, limit=40)
        for name, fixture in (("/LIBRARY/:WORKSPACE.O", "OS/LIBRARY/WORKSPACE.O.prg"),
                              ("/LIBRARY/:IEC.LIB.R", "OS/LIBRARY/IEC.LIB.R.prg"),
                              ("/KERNAL/:MENU.O", "OS/KERNAL/MENU.O.prg")):
            self.open(4, name)
            header = self.read(4, limit=2)
            self.close(4)
            if header != pattern(fixture, 2):
                raise Corruption(f"the first two bytes of {name} read {header!r}")
            self.expect_file(5, name, fixture)
        missing = self.open(6, "/TEMPORARY/:UPDATER")
        self.close(6)
        if not missing.startswith("62"):
            raise Failure(f"a file that is not there answered {missing}")
        self.expect_file(14, "/SETTINGS/:CONFIG.T", "OS/SETTINGS/CONFIG.T.seq")
        self.command(bytes([ord("C"), 0xD0, 1]), allowed=(2,))
        self.command(f"CD//{here}/OS/DRIVERS/", allowed=(0,))
        self.expect_file(5, "KBD.C64", "OS/DRIVERS/KBD.C64.prg")
        self.command(f"CD//{here}/OS/SETTINGS/", allowed=(0,))
        self.open(3, "$:CONFIG*", secondary=0)
        listing = self.read(3)
        self.close(3)
        self.command(f"CD//{here}/OS", allowed=(0,))
        if b"CONFIG.T" not in listing:
            raise Failure(f"$:CONFIG* in SETTINGS did not list CONFIG.T: {listing!r}")
        rest = self.read(2)
        self.close(2)
        if first + rest != pattern("OS/SETTINGS/SYSTEM.T.seq", 400):
            raise Corruption("the settings file held open during the boot read back changed")

    # -- the C64 lane: soak operations -----------------------------------------------

    def load_document(self):
        """Loads an application or document and reads it to the end, as a launch or a
        file open does, and checks it reads back byte for byte."""
        self.partition()
        fixture = self.random.choice(FIXTURE_DOCUMENTS)
        directory, _, base = fixture.rpartition("/")
        cbm_name = base.rsplit(".", 1)[0]  # the CBM name; the .prg extension is cut
        self.command(f"CD//{self.here}/{directory}/", allowed=(0,))
        self.expect_file(5, cbm_name, fixture)

    def poll_status(self):
        """Reads the status channel, and sometimes the clock and the partition info, as
        a program does between actions to see the drive is well."""
        self.status()
        if self.random.randrange(2):
            self.agent.call(WRITE, 15, b"T-RI")
            self.note(f"T-RI answered {self.agent.call(READ_TO_EOI, 15, expect=40)!r}")
        if self.random.randrange(3) == 0:
            self.agent.call(WRITE, 15, b"G-P")
            self.note(f"G-P answered {self.agent.call(READ_TO_EOI, 15, expect=40)!r}")

    def settings_write(self, replace=True):
        """Saves a small settings file, the way a session writes its preferences, and
        reads it back to check it."""
        self.partition()
        self.command(f"CD//{self.here}/OS/SETTINGS/", allowed=(0,))
        size = self.random.randrange(20, 400)
        data = bytes(self.random.randrange(256) for _ in range(size))
        self.save_and_verify(f"PREFS{self.random.randrange(3)}.T", data, replace=replace)

    def temporary_files(self):
        """Creates a few temporary files in OS/TEMPORARY and scratches them, as a session
        does with working files it does not keep."""
        self.partition()
        self.command(f"CD//{self.here}/OS/TEMPORARY/", allowed=(0,))
        made = []
        for _ in range(self.random.randrange(1, 4)):
            name = f"T{self.random.randrange(6)}"
            size = self.random.randrange(1, 300)
            response = self.open(7, f"@:{name},S,W")
            if response.startswith("00"):
                self.write_stream(7, bytes(self.random.randrange(256) for _ in range(size)))
                made.append(name)
            self.close(7)
        for name in made:
            self.command(f"S:{name}", allowed=(1,))

    def save_and_scratch(self):
        """Saves a working file, reads it back and compares, and scratches it now and
        then, as a session does with a document it saves."""
        size = self.random.randrange(1, 1500)
        name = f"F{self.random.randrange(4)}"
        data = bytes(self.random.randrange(256) for _ in range(size))
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        self.save_and_verify(name, data, replace=True)
        if self.random.randrange(3) == 0:
            self.command(f"S:{name}", allowed=(1,))

    def relative(self):
        length = self.random.randrange(2, 120)
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        name = f"R{length}"
        self.command(f"S:{name}", allowed=(1,))
        self.open(9, name.encode("ascii") + b",L," + bytes([length]))
        records = {}
        for _ in range(self.random.randrange(1, 5)):
            record = self.random.randrange(1, 40)
            payload = bytes(self.random.randrange(1, 256) for _ in range(self.random.randrange(1, length)))
            self.command(bytes([ord("P"), 0x60 | 9, record & 0xFF, record >> 8, 1]), allowed=(0, 50))
            self.agent.call(WRITE, 9, payload)
            records[record] = payload
        for record, payload in records.items():
            self.command(bytes([ord("P"), 0x60 | 9, record & 0xFF, record >> 8, 1]), allowed=(0,))
            back = self.read(9, limit=length)
            if back[:len(payload)] != payload:
                raise Corruption(f"record {record} of {name} read {back!r}, wrote {payload!r}")
        self.close(9)

    def direct_access(self):
        self.partition()
        self.command(f"CD//{self.here}/DISK.D64", allowed=(0,))
        self.open(10, "#")
        data = bytes(self.random.randrange(256) for _ in range(256))
        sector = self.random.randrange(0, 21)
        self.command("B-P 10 0", allowed=(0,))
        self.agent.call(WRITE, 10, data[:254])
        self.agent.call(WRITE, 10, data[254:])
        self.command(f"U2:10,0,1,{sector}", allowed=(0,))
        self.command(f"U1:10,0,1,{sector}", allowed=(0,))
        self.command("B-P 10 0", allowed=(0,))
        back = self.read(10, limit=256)
        self.close(10)
        self.command(f"CD//{self.here}", allowed=(0,))
        if back != data:
            raise Corruption(f"sector 1/{sector} read back differs after U2 and U1")

    def directories(self):
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        name = f"D{self.random.randrange(3)}"
        self.command(f"MD:{name}", allowed=(0, 63))
        self.command(f"CD:{name}", allowed=(0,))
        self.command("CD_", allowed=(0,))
        self.command(f"C:COPY=//{self.here}/OS/SETTINGS/:CONFIG.T", allowed=(0, 62, 63))
        self.command("R:MOVED=COPY", allowed=(0, 62, 63))
        self.command("S:MOVED", allowed=(1,))
        self.command(f"RD:{name}", allowed=(0, 62))

    def listings(self):
        """Directory listings of several kinds, some read whole and some abandoned part
        way, the way a session browses."""
        self.partition()
        name = self.random.choice([f"$//{self.here}/OS/", "$=P", f"$//{self.here}/:*=S",
                                   f"$//{self.here}/OS/KERNAL/:*=L", "$//", f"$//{self.here}/NOSUCH/"])
        secondary = 0 if self.random.randrange(4) else 3
        response = self.open(11, name, secondary=secondary)
        if response.startswith("00"):
            self.read(11, limit=None if self.random.randrange(2) else self.random.randrange(1, 60))
        self.close(11)

    def other_commands(self):
        for text, allowed in (("G-P", None), ("T-RI", None), ("I", (0,)), ("UJ", (73,)),
                              ("Z9", (31,)), ("E", (30,)), ("XYZ", (30,)), ("V", (31,))):
            if self.random.randrange(2):
                continue
            if text in ("G-P", "T-RI"):
                self.agent.call(WRITE, 15, text.encode("ascii"))
                self.note(f"{text} answered {self.agent.call(READ_TO_EOI, 15, expect=40)!r}")
            else:
                self.command(text, allowed=allowed)

    # -- the C64 lane: stress operations ---------------------------------------------
    #
    # These push the interface as hard as the agent allows. Any DOS or KERNAL error is an
    # allowed outcome of the edge being tested, so they raise Failure freely; the run loop
    # records that as an anomaly rather than a failure. Only a data mismatch of a file the
    # step owns raises Corruption, which is a real failure.

    def fuzz(self, data):
        """Sends raw bytes to the command channel and reads the status. Records the
        answer. Fails only if the answer is not a two-digit status, which would mean the
        drive lost the shape of a DOS reply."""
        data = data if isinstance(data, bytes) else data.encode("latin-1")
        self.note(f"fuzz command {data!r}")
        self.agent.call(WRITE, 15, data)
        response = self.agent.call(READ_TO_EOI, 15, expect=STATUS_BYTES).decode("latin-1").strip()
        if not response[:2].isdigit():
            raise Failure(f"{data!r} answered {response!r}, which is not a status")
        self.record_status("command", response)
        return response

    def fuzz_command(self):
        """Sends a command a well-behaved program would not: random bytes after a command
        letter, a command that fills the buffer, binary parameters including 13 and 0, a
        lone carriage return, a carriage return and line feed ending, and unknown letters.
        Runs in WORK so an accidentally valid destructive command hits disposable files
        rather than the read-only fixtures under OS."""
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        letters = b"BCGINPRSTUXELMW?$@"
        kind = self.random.randrange(6)
        if kind == 0:
            # A command of 250 to 254 bytes. The command buffer holds 254, so a command of
            # 254 or more answers 32 and is not executed (cbmdos_parser execute_command).
            # The mailbox carries at most 254 bytes, so 255 cannot be sent from here.
            length = self.random.randrange(250, 255)
            data = bytes([self.random.choice(letters)]) + \
                bytes(self.random.randrange(1, 256) for _ in range(length - 1))
        elif kind == 1:
            data = b"\r"  # a lone carriage return: the ROM answers 31
        elif kind == 2:
            letter = bytes([self.random.choice(b"BUPMR")])
            data = letter + bytes(self.random.choice([0, 13, 32, 255, self.random.randrange(256)])
                                  for _ in range(self.random.randrange(1, 6)))
        elif kind == 3:
            data = bytes([self.random.choice(letters)]) + \
                bytes(self.random.randrange(256) for _ in range(self.random.randrange(0, 20))) + b"\r\n"
        elif kind == 4:
            data = bytes([self.random.choice(b"ABDEFGHKQWYZ")]) + \
                bytes(self.random.randrange(65, 91) for _ in range(self.random.randrange(0, 8)))
        else:
            data = bytes(self.random.randrange(1, 256) for _ in range(self.random.randrange(1, 40)))
        self.fuzz(data)

    def fuzz_open(self):
        """Opens a channel under a name a program would not send: random PETSCII, a type
        or mode suffix, wildcards, doubled and single slashes, and names of 16, 17, 40 and
        250 bytes. A name of 254 bytes or more answers 32 and opens nothing. The mailbox
        holds 254 bytes, so 255 or more cannot be sent from here."""
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        kind = self.random.randrange(7)
        if kind == 0:
            name = bytes(self.random.randrange(1, 256) for _ in range(self.random.choice([16, 17, 40])))
        elif kind == 1:
            body = "".join(chr(self.random.randrange(65, 91)) for _ in range(self.random.randrange(1, 20)))
            name = f"@:{body}," + self.random.choice("PSUL") + "," + self.random.choice("RWA")
        elif kind == 2:
            name = self.random.choice(["$", "$:*", "$=P", "#", "#2", ":", ",", "*", "??", "@"])
        elif kind == 3:
            name = f"//{self.here}//OS//KERNAL//:MENU.O"
        elif kind == 4:
            name = "A" * self.random.choice([250, 253, 254])  # 254 answers 32
        elif kind == 5:
            name = self.random.choice(["ABC:DEF", "1:X", "9//:Y", "OS/:*=S"])
        else:
            name = bytes([self.random.randrange(0xA0, 0x100)]) + \
                bytes(self.random.randrange(1, 256) for _ in range(self.random.randrange(0, 16)))
        channel = self.random.choice([2, 3, 4, 5, 6])
        secondary = 0 if self.random.randrange(3) == 0 else None
        response = self.open(channel, name, secondary=secondary)
        if response and not response[:2].isdigit():
            raise Failure(f"open {name!r} answered {response!r}")
        self.record_status("open", response)
        if response.startswith("00"):
            self.read(channel, limit=self.random.choice([5, 254, None]))
        self.close(channel)

    def many_channels(self):
        """Opens as many channels as the KERNAL table allows (it holds ten logical files,
        and this agent uses the channel number as the logical file number and the secondary
        address, so the command channel leaves nine), reads across them, some abandoned,
        writes to a channel opened for reading, closes a channel never opened, and reopens
        an open one. Every channel it touched is closed at the end."""
        self.partition()
        self.command(f"CD//{self.here}/OS", allowed=(0,))
        names = ["/KERNAL/:MENU.O", "/KERNAL/:FILE.O", "/TK/:TKVIEW.O",
                 "/DRIVERS/:KBD.C64", "/LIBRARY/:WORKSPACE.O"]
        attempted = []
        opened = []
        for channel in range(2, 10):
            try:
                response = self.open(channel, self.random.choice(names))
            except Failure:
                break  # the KERNAL refused another open
            attempted.append(channel)
            if response.startswith("00"):
                opened.append(channel)
        for channel in opened:
            self.read(channel, limit=self.random.choice([2, 40, 254]))
        if opened:
            try:
                self.agent.call(WRITE, self.random.choice(opened), b"junk")  # write to a read channel
            except Failure as exc:
                self.anomaly("write to read channel", exc)
            self.close(12)  # a channel that was never opened
            try:
                self.open(opened[0], self.random.choice(names))  # reopen an open channel
            except Failure as exc:
                self.anomaly("reopen open channel", exc)
        for channel in attempted:
            self.close(channel)

    def abandoned_transfers(self):
        """Starts a read or a listing and closes it after a few bytes, the way a program
        that only needs a file's start does. The directory a listing left open must be
        released (close_file in the firmware does that)."""
        self.partition()
        if self.random.randrange(2):
            self.command(f"CD//{self.here}/OS/KERNAL", allowed=(0,))
            response = self.open(5, self.random.choice(["MENU.O", "FILE.O"]))
            if response.startswith("00"):
                self.read(5, limit=self.random.randrange(1, 10))
            self.close(5)
        else:
            name = self.random.choice([f"$//{self.here}/OS/", "$=P", "$//"])
            response = self.open(11, name, secondary=0)
            if response.startswith("00"):
                self.read(11, limit=self.random.randrange(1, 20))
            self.close(11)

    def large_file(self):
        """Saves a file of several kilobytes and reads it back byte for byte. A 254 byte
        read costs about 0.7 s on this C64, so a 30 KB file would be about two minutes of
        bus time; the size is capped near 8 KB to keep one iteration under about 30 s
        while still crossing many blocks."""
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        size = self.random.randrange(4000, 8001)
        data = bytes(self.random.randrange(256) for _ in range(size))
        self.save_and_verify(f"BIG{self.random.randrange(2)}", data, replace=True)

    def relative_edges(self):
        """A relative file with a long record and a high record number, to push the record
        layout and the file growth in seek_record. The record number is kept moderate so
        the firmware does not spend long filling the gap with empty records."""
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        length = self.random.choice([200, 254])
        name = f"RB{length}"
        self.command(f"S:{name}", allowed=(1,))
        self.open(9, name.encode("ascii") + b",L," + bytes([length]))
        record = self.random.choice([80, 160, 250])
        payload = bytes(self.random.randrange(1, 256) for _ in range(self.random.randrange(1, length)))
        self.command(bytes([ord("P"), 0x60 | 9, record & 0xFF, record >> 8, 1]), allowed=(0, 50))
        self.agent.call(WRITE, 9, payload)
        self.command(bytes([ord("P"), 0x60 | 9, record & 0xFF, record >> 8, 1]), allowed=(0, 50))
        back = self.read(9, limit=length)
        self.close(9)
        if back[:len(payload)] != payload:
            raise Corruption(f"record {record} of {name} read {back!r}, wrote {payload!r}")

    def direct_access_edges(self):
        """Direct access to a D64 at the edges of its geometry and past them: B-P, U1, U2,
        B-A and B-F at track and sector numbers that are valid, at the last valid ones, and
        at illegal ones. Any two-digit status is accepted; an illegal track or sector
        answers 66 in the firmware, but that is recorded rather than asserted."""
        self.partition()
        self.command(f"CD//{self.here}/DISK.D64", allowed=(0,))
        self.open(10, "#")
        data = bytes(self.random.randrange(256) for _ in range(256))
        track = self.random.choice([1, 18, 35, 36, 40, 0])
        sector = self.random.choice([0, 20, 21, 255])
        self.command("B-P 10 0", allowed=(0,))
        self.agent.call(WRITE, 10, data[:254])
        self.agent.call(WRITE, 10, data[254:])
        for cmd in (f"U2:10,0,{track},{sector}", f"U1:10,0,{track},{sector}",
                    f"B-A:0,{track},{sector}", f"B-F:0,{track},{sector}"):
            self.record_status("block", self.command(cmd, allowed=range(100)))
        self.command("B-P 10 0", allowed=(0,))
        self.read(10, limit=self.random.choice([16, 256]))
        self.close(10)
        self.command(f"CD//{self.here}", allowed=(0,))

    def scratch_storm(self):
        """Fills a directory, scratches it with wide patterns, and nests directories deep.
        A non-empty directory refuses removal (63), so the throwaway directory is left for
        the teardown to remove with the rest of the run's tree."""
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        storm = f"ST{self.random.randrange(6)}"
        self.command(f"MD:{storm}", allowed=(0, 63))
        self.command(f"CD:{storm}", allowed=(0,))
        for i in range(self.random.randrange(4, 10)):
            response = self.open(7, f"@:F{i},S,W")
            if response.startswith("00"):
                self.write_stream(7, bytes([i]) * self.random.randrange(1, 40))
            self.close(7)
        self.command(self.random.choice(["S:*", "S:F*", "S:*,*,*", "S:F?"]), allowed=(1,))
        if self.random.randrange(2):
            # Nest directories and change into each, so the working directory grows deep.
            for level in range(self.random.randrange(3, 8)):
                self.command(f"MD:N{level}", allowed=(0, 63))
                self.command(f"CD:N{level}", allowed=(0,))
            self.status()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        self.command(f"RD:{storm}", allowed=(0, 62, 63))

    def race_file(self):
        """Reads and writes a file in the RACE directory that the FTP lanes create,
        overwrite and delete at the same time, or lists a subdirectory the FTP lanes remove
        and create again while the drive is in it. A DOS error here is an expected outcome
        of the race, not a failure, and the content is not owned by the C64, so it is not
        compared."""
        self.partition()
        self.command(f"CD//{self.here}/RACE/", allowed=(0,))
        if self.random.randrange(3) == 0:
            self.command(f"CD:SUB{self.random.randrange(3)}", allowed=range(100))
            response = self.open(11, "$", secondary=0)
            if response.startswith("00"):
                self.read(11, limit=self.random.choice([10, None]))
            self.close(11)
            self.command(f"CD//{self.here}/RACE/", allowed=(0,))
            return
        name = f"C{self.random.randrange(3)}"
        if self.random.randrange(2):
            size = self.random.randrange(1, 500)
            response = self.open(7, f"@:{name},S,W")
            if response.startswith("00"):
                self.write_stream(7, bytes(self.random.randrange(256) for _ in range(size)))
            self.close(7)
        else:
            response = self.open(5, name)
            if response.startswith("00"):
                self.read(5, limit=self.random.choice([5, 254, None]))
            self.close(5)

    def resets_with_open_channels(self):
        """UJ, U and shifted J, and I sent while a data channel is open. None may
        reconfigure the bus (SI-103); all close the open data channels, so the KERNAL side
        is closed here afterwards."""
        self.partition()
        self.command(f"CD//{self.here}/OS/KERNAL", allowed=(0,))
        try:
            self.open(5, "MENU.O")
        except Failure as exc:
            self.anomaly("open before reset", exc)
        self.command(self.random.choice(["UJ", "I", "I0"]), allowed=(0, 73))
        self.fuzz(bytes([ord("U"), 0xCA]))  # U and shifted J, CHR$(202)
        self.close(5)

    def device_number(self):
        """U0> moves the drive to another device number and back (SI-100). The agent's
        move_drive follows the drive list rather than addressing a device that may not be
        there, and the move back always runs, because every later step addresses 11."""
        other = self.random.choice([12, 13, 30])
        try:
            moved = self.agent.move_drive(other)
            self.note(f"U0> to {other}: the drive list reports {moved}")
            if moved == other:
                self.record_status("status on the new number",
                                   self.agent.call(READ_TO_EOI, 15, device=other,
                                                   expect=STATUS_BYTES).decode("latin-1").strip())
        finally:
            self.agent.move_drive(11)
        self.status()

    SOAK_OPERATIONS = (("browse", listings, 6), ("load", load_document, 5),
                       ("poll", poll_status, 3), ("save", save_and_scratch, 2),
                       ("settings", settings_write, 2), ("temporary", temporary_files, 2),
                       ("relative", relative, 1), ("direct access", direct_access, 1),
                       ("directories", directories, 1), ("other commands", other_commands, 1),
                       ("reboot", boot, 1))

    STRESS_OPERATIONS = (("fuzz command", fuzz_command, 4), ("fuzz open", fuzz_open, 3),
                         ("many channels", many_channels, 2), ("abandoned", abandoned_transfers, 2),
                         ("direct access edges", direct_access_edges, 2), ("scratch storm", scratch_storm, 2),
                         ("race file", race_file, 2), ("large file", large_file, 1),
                         ("relative edges", relative_edges, 1), ("resets", resets_with_open_channels, 1),
                         ("device number", device_number, 1))

    # -- the per-step stress oracle --------------------------------------------------

    def checkpoint(self):
        """After a hostile step: check the drive still answers a plain status read, and, on
        every third step, that a small read-only fixture still reads back byte for byte. A
        drive that has stopped answering the C64 while REST still answers is dead; a fixture
        that reads back changed is corruption. The read-only fixture is under OS, which no
        lane touches, so a difference there is the drive's own doing."""
        try:
            self.status()
        except Failure:
            try:
                self.recover()
                self.status()
            except Failure as exc:
                if self.alive():
                    raise Dead(f"the drive stopped answering a status read after iteration "
                               f"{self.iteration} while REST answers: {exc}") from exc
                raise
        if self.iteration % 3 == 0:
            try:
                self.close(6)
                self.partition()
                self.expect_file(6, f"//{self.here}/OS/SETTINGS/:CONFIG.T", "OS/SETTINGS/CONFIG.T.seq")
            except Corruption:
                raise
            except Failure as exc:
                self.anomaly("checkpoint fixture", exc)

    # -- the PC lanes ----------------------------------------------------------------

    def rest_lane(self):
        # A client of its own: the agent's lane must not share a connection with this one.
        api = UltimateApi(self.args.host, self.args.password, self.args.timeout)
        log_on = False
        last_toggle = time.monotonic()
        while not self.stop.wait(self.policy.rest_seconds):
            try:
                api.rest.json("/v1/drives")
                api.machine.heap_free()
                self.lane_counts["rest"] += 1
                # Toggle Log Every Operation during bus traffic, which reaches
                # effectuate_settings and briefly cycles the IEC processor (CR-6). The bus
                # id and the enable flag are left alone, so the drive stays on device 11
                # enabled; only the log setting moves, no more than once every fifteen
                # seconds to bound the flash writes.
                if self.policy.toggle_log and (time.monotonic() - last_toggle > 15):
                    log_on = not log_on
                    api.configs.set("SoftIEC Drive Settings", "Log Every Operation",
                                    "Enabled" if log_on else "Disabled")
                    last_toggle = time.monotonic()
            except Exception as exc:  # the main lane decides whether this is a death
                self.lane_errors.append(f"rest at iteration {self.iteration}: {exc}")

    def ftp_lane(self, index):
        directory = f"{self.root.rstrip('/')}/{self.folder}"
        payload = pattern(f"pclane{index}", 3000)
        lane_random = random.Random(self.args.seed + index)  # the C64 lane owns self.random
        while not self.stop.wait(self.policy.ftp_seconds):
            try:
                with ftp.session(self.args.host, self.args.password, timeout=20) as client:
                    if self.policy.race:
                        # Fight over files and a directory the C64's stress lane also
                        # touches. These are owned by the stress lanes, so a difference in
                        # them is not the corruption the suite fails on; the fixture files
                        # under OS are never touched here.
                        # C0 to C2 are the names race_file reads and writes from the C64,
                        # stored by the drive as sequential files.
                        name = f"{directory}/RACE/C{lane_random.randrange(3)}.seq"
                        ftp.store(client, name, payload)
                        if lane_random.randrange(2):
                            ftp.quietly(lambda: client.delete(name))
                        ftp.quietly(lambda: client.rmd(f"{directory}/RACE/SUB{index}"))
                        ftp.quietly(lambda: client.mkd(f"{directory}/RACE/SUB{index}"))
                    else:
                        ftp.names(client, f"{directory}/OS/KERNAL")
                        data = ftp.retrieve(client, f"{directory}/OS/KERNAL/MENU.O.prg")
                        if data != pattern("OS/KERNAL/MENU.O.prg", 2209):
                            self.lane_errors.append(f"ftp at iteration {self.iteration}: MENU.O.prg differs")
                        ftp.store(client, f"{directory}/PCLANE/PC.BIN", payload)
                        client.delete(f"{directory}/PCLANE/PC.BIN")
                self.lane_counts["ftp"] += 1
            except Exception as exc:
                self.lane_errors.append(f"ftp at iteration {self.iteration}: {exc}")

    # -- the run ---------------------------------------------------------------------

    def alive(self):
        try:
            self.api.rest.json("/v1/info")
            return True
        except Exception:
            return False

    def fixture(self):
        directory = f"{self.root.rstrip('/')}/{self.folder}"
        with ftp.session(self.args.host, self.args.password) as client:
            client.mkd(directory)
            for sub in FIXTURE_DIRS:
                client.mkd(f"{directory}/{sub}")
            for name, size in FIXTURE.items():
                ftp.store(client, f"{directory}/{name}", pattern(name, size))
        self.api.files.create_d64(f"{directory}/DISK.D64", diskname="SOAK")

    def run(self, policy, duration):
        """Runs one phase: boot the session, then repeat the policy's operations under its
        pacing until the deadline, sampling the heap and, for stress, checking the drive
        after each step. Returns the heap samples and the failures, and records the heap
        before the load and after an idle settle."""
        self.policy = policy
        self.stop = threading.Event()
        heap = []
        failures = []

        # The C64 OS boot sequence is the session start (see #877), run once before timing.
        self.boot()
        time.sleep(2.0)  # a short settle so the first heap reading is not mid-cache-fill
        self.heap_before = self.api.machine.heap_free()

        deadline = time.monotonic() + duration
        lanes = []
        if not self.args.no_lanes:
            lanes.append(threading.Thread(target=self.rest_lane, daemon=True))
            for i in range(policy.ftp_lanes):
                lanes.append(threading.Thread(target=self.ftp_lane, args=(i,), daemon=True))
        for lane in lanes:
            lane.start()
        try:
            while time.monotonic() < deadline:
                self.iteration += 1
                label, action = policy.choose(self.random)
                self.counts[label] = self.counts.get(label, 0) + 1
                self.note(f"begin {label}")
                try:
                    action(self)
                except Corruption as exc:
                    failures.append(f"iteration {self.iteration} ({label}): {exc}")
                    self.recover()
                    if len(failures) > 5:
                        break
                except Dead:
                    raise
                except Failure as exc:
                    if not self.alive():
                        raise Dead(f"the firmware stopped answering during iteration {self.iteration} "
                                   f"({label}): {exc}") from exc
                    if policy.tolerant:
                        self.anomaly(label, exc)
                        self.recover()
                    else:
                        failures.append(f"iteration {self.iteration} ({label}): {exc}")
                        try:
                            self.recover()
                        except Failure as stuck:
                            raise Dead(f"the drive stopped answering the C64 during iteration "
                                       f"{self.iteration} ({label}) while REST still answers: "
                                       f"{exc}; then {stuck}") from stuck
                        if len(failures) > 5:
                            break
                if policy.checkpoint:
                    try:
                        self.checkpoint()
                    except Corruption as exc:
                        failures.append(f"iteration {self.iteration} checkpoint: {exc}")
                        if len(failures) > 5:
                            break
                if self.iteration >= WARMUP_ITERATIONS:
                    heap.append(self.api.machine.heap_free())
                if self.iteration % 10 == 0:
                    detail(f"{self.iteration} iterations, {dict(sorted(self.counts.items()))}, "
                           f"heap {heap[-1] if heap else '-'}, lanes {self.lane_counts}")
                think = policy.think_time(self.random)
                if think:
                    time.sleep(min(think, max(0.0, deadline - time.monotonic())))
        finally:
            self.stop.set()
            for lane in lanes:
                lane.join(timeout=30)

        # An idle settle, then the final heap reading for the before-and-after verdict.
        time.sleep(IDLE_SETTLE_SECONDS)
        try:
            self.recover()
        except Failure:
            pass
        self.heap_after = self.api.machine.heap_free()
        return heap, failures

    def recover(self):
        """Close what an interrupted operation left open, so the next one starts clean."""
        for channel in range(2, 15):
            try:
                self.agent.call(CLOSE, channel=channel)
            except Failure:
                pass
        try:
            self.agent.call(CLOSE, channel=15)
            self.agent.call(OPEN, channel=15)
            self.status()
        except Failure:
            self.agent.start()
            self.agent.call(OPEN, channel=15)
            self.status()


class Policy:
    """One kind of run: which operations, how they are paced, how the lanes load the
    device, and how the heap is judged. The two kinds share Session.run; only this data
    differs between them.

    tolerant: a Failure from an operation is recorded as an anomaly rather than a failure,
              because a hostile step is expected to draw DOS and KERNAL errors.
    checkpoint: after each step, check the drive still answers and the fixture is intact.
    heap_mode: "slope" judges a steady per-iteration loss; "settled" judges the drop from
               before the load to after an idle settle.
    """

    def __init__(self, name, operations, *, think_min, think_max, rest_seconds,
                 ftp_seconds, ftp_lanes, toggle_log, race, tolerant, heap_mode, checkpoint):
        self.name = name
        self.operations = operations
        self.think_min = think_min
        self.think_max = think_max
        self.rest_seconds = rest_seconds
        self.ftp_seconds = ftp_seconds
        self.ftp_lanes = ftp_lanes
        self.toggle_log = toggle_log
        self.race = race
        self.tolerant = tolerant
        self.heap_mode = heap_mode
        self.checkpoint = checkpoint

    def think_time(self, rnd):
        """Seconds to wait between actions. Zero for stress, a few seconds for soak."""
        if self.think_max <= 0:
            return 0.0
        return rnd.uniform(self.think_min, self.think_max)

    def choose(self, rnd):
        total = sum(weight for _, _, weight in self.operations)
        pick = rnd.randrange(total)
        for label, action, weight in self.operations:
            if pick < weight:
                return label, action
            pick -= weight
        raise AssertionError("unreachable")


SOAK_POLICY = Policy("soak", Session.SOAK_OPERATIONS, think_min=1.0, think_max=6.0,
                     rest_seconds=REST_LANE_SECONDS, ftp_seconds=FTP_LANE_SECONDS,
                     ftp_lanes=1, toggle_log=False, race=False, tolerant=False,
                     heap_mode="slope", checkpoint=False)

# The firmware has four FTP and four HTTP slots, so three racing FTP lanes leaves one
# free for the teardown's FTP session.
STRESS_POLICY = Policy("stress", Session.STRESS_OPERATIONS, think_min=0.0, think_max=0.0,
                       rest_seconds=0.0, ftp_seconds=0.3, ftp_lanes=3, toggle_log=True,
                       race=True, tolerant=True, heap_mode="settled", checkpoint=True)


def heap_verdict(session, policy, heap):
    """The heap section of a phase's report. Returns the failures it found, prints the
    numbers as detail either way."""
    if policy.heap_mode == "settled":
        if (session.heap_before is None) or (session.heap_after is None):
            return []
        loss = session.heap_before - session.heap_after
        detail(f"heap free {session.heap_before} before the stress, {session.heap_after} "
               f"after an idle settle, {loss} lost")
        if loss > STRESS_HEAP_LOSS_TOLERANCE_BYTES:
            return [f"the heap lost {loss} bytes over the stress run"]
        return []
    if len(heap) < 3:
        return []
    # A sample taken while a lane holds a transfer buffer reads up to about 20 KB low, so
    # the most free heap of the first and the last quarter of the samples are compared
    # rather than the first and the last sample.
    window = max(1, len(heap) // 4)
    first, last = max(heap[:window]), max(heap[-window:])
    slope = (first - last) / max(1, len(heap) - window)
    detail(f"heap free {first} after warm-up, {last} at the end, {slope:.0f} bytes lost per iteration")
    if slope > LEAK_TOLERANCE_BYTES_PER_ITERATION:
        return [f"the heap shrank by {slope:.0f} bytes per iteration"]
    return []


def run_phase(session, duration, policy):
    """Runs one phase and reports it as its own section and check with its own heap
    verdict. Returns True if the phase passed. A dead firmware stops the whole run and is
    not caught here."""
    session.begin_phase()
    section(f"{policy.name} ({duration:.0f} s)")
    try:
        with check("the drive keeps answering, keeps its data and keeps its heap"):
            heap, failures = session.run(policy, duration)
            detail(f"{session.iteration} iterations: {dict(sorted(session.counts.items()))}; "
                   f"lanes {session.lane_counts}")
            if session.statuses:
                detail(f"statuses seen: {dict(sorted(session.statuses.items()))}")
            if session.anomalies:
                detail(f"anomalies recorded: {dict(sorted(session.anomalies.items()))}")
            if session.lane_errors:
                detail("lane errors: " + "; ".join(session.lane_errors[:10]))
            failures = failures + heap_verdict(session, policy, heap)
            if failures:
                detail("recent operations: " + " | ".join(session.history))
                raise Failure("; ".join(failures))
        return True
    except Dead:
        detail("recent operations: " + " | ".join(session.history))
        raise
    except Failure:
        return False


def plan_phases(args):
    """The phases to run, in order, from the mode and the durations. --profile maps to a
    single phase for run-tests: stress is ten minutes of the stress mode, soak is four
    hours of the soak mode, and it overrides --mode."""
    if args.profile:
        if args.profile == "stress":
            return [(args.duration or PROFILE_STRESS_SECONDS, STRESS_POLICY)]
        return [(args.duration or PROFILE_SOAK_SECONDS, SOAK_POLICY)]
    if args.mode == "soak":
        return [(args.duration or SOAK_DEFAULT_SECONDS, SOAK_POLICY)]
    if args.mode == "stress":
        return [(args.duration or STRESS_DEFAULT_SECONDS, STRESS_POLICY)]
    return [(args.soak_duration, SOAK_POLICY), (args.stress_duration, STRESS_POLICY)]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    cli.add_device_arguments(parser)
    parser.add_argument("--mode", choices=("soak", "stress", "both"), default="both",
                        help="soak is the realistic C64 OS session, stress edge-tests the "
                             "interface as hard as the agent allows, both runs the soak "
                             "phase then the stress phase (default: both).")
    parser.add_argument("--profile", choices=("stress", "soak"),
                        help="Kept for run-tests: stress runs the stress mode for ten "
                             "minutes, soak runs the soak mode for four hours. Overrides "
                             "--mode.")
    parser.add_argument("--duration", type=float,
                        help="seconds of one soak or stress run, instead of the default")
    parser.add_argument("--soak-duration", type=float, default=SOAK_DEFAULT_SECONDS,
                        help=f"seconds of the soak phase of a both run (default: {SOAK_DEFAULT_SECONDS})")
    parser.add_argument("--stress-duration", type=float, default=STRESS_DEFAULT_SECONDS,
                        help=f"seconds of the stress phase of a both run (default: {STRESS_DEFAULT_SECONDS})")
    parser.add_argument("--seed", type=int, default=877)
    parser.add_argument("--no-lanes", action="store_true",
                        help="only the C64's operations, to tell the drive's heap use from REST's and FTP's")
    args = parser.parse_args()
    phases = plan_phases(args)
    session = Session(args)
    api = session.api
    try:
        section("setup")
        with check("the device has one Software IEC partition and device 11 is free"):
            drives = {name: value for entry in api.rest.json("/v1/drives")["drives"] for name, value in entry.items()}
            if any(d.get("enabled") and d.get("bus_id") == 11 for name, d in drives.items()
                   if name not in ("a", "IEC Drive")):
                raise Failure("Device 11 is already in use")
            partitions = drives["IEC Drive"]["partitions"]
            if len(partitions) != 1 or partitions[0]["id"] != 1:
                raise Failure("This suite requires one Software IEC partition, numbered 1")
            original_path = partitions[0]["path"]
            plan = [(policy.name, int(duration)) for duration, policy in phases]
            detail(f"{api.rest.json('/v1/info').get('firmware_version')}, seed {args.seed}, phases {plan}")
        saved = Snapshot(args.host, {"SoftIEC Drive Settings": api.configs.category("SoftIEC Drive Settings")})
        started = created = False
        try:
            with check("start the IEC agent and build the fixture"):
                api.configs.set("SoftIEC Drive Settings", "Soft Drive Bus ID", 11)
                api.configs.set("SoftIEC Drive Settings", "IEC Drive", "Enabled")
                session.agent.start()
                started = True
                session.agent.call(OPEN, channel=15)
                session.status()
                session.command("CD//")
                session.root = iec_drive(api)["partitions"][0]["path"]
                session.fixture()
                created = True
            ok = True
            for duration, policy in phases:
                ok = run_phase(session, duration, policy) and ok
            if not ok:
                raise Failure("one or more phases failed; see the sections above")
        except Dead:
            detail("recent operations: " + " | ".join(session.history))
            raise
        finally:
            def restore_directory():
                if started and session.alive():
                    session.agent.call(CLOSE, channel=15)
                    session.agent.call(OPEN, channel=15)
                    session.command("CD//" + original_path[len(session.root or ""):].upper())
                    session.agent.call(CLOSE, channel=15)

            def remove_fixture():
                if created and session.alive():
                    with ftp.session(args.host, args.password) as client:
                        ftp.remove_tree(client, f"{session.root.rstrip('/')}/{session.folder}")

            ok = True
            for label, action in (("restore the IEC working directory", restore_directory),
                                  ("restore the Software IEC settings", lambda: saved.restore(api)),
                                  ("remove this run's directory", remove_fixture),
                                  ("return the C64 to BASIC", lambda: api.machine.reset(force=True))):
                ok = teardown_step(label, action) and ok
    except Exception as exc:
        traceback.print_exc()
        suite_fail(SUITE, str(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
