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
    also uses, and REST reads the drive list without pauses and resets the drive. The stress run
    fails only on death, on corruption of the read-only fixture files the lanes never
    touch, on the drive ceasing to answer the C64 while REST still answers, or on heap
    that does not come back after an idle settle. Fuzzed answers are only required to be
    two-digit statuses; the exact codes are recorded and reported, not asserted, because
    a hostile command has no single right answer.

  * both: the soak phase then the stress phase, each its own section and check with its
    own heap verdict. The fixture is built once. Defaults are 1800 s of soak and 600 s
    of stress.

Both modes cover the whole Software IEC surface a normal session and a hostile one reach:
LOAD and SAVE on secondaries 0 and 1, append, USR, x00 wrapped files, D71, D81 and DNP
images and DNP subdirectories, rename, copy, lock, the clock, XPWD, partition info with a
byte, listings with type and date filters and a many-entry directory, and, between bus
steps, the UCI SoftIEC target (software/io/command_interface/softiec_target.cc) reached
over the cartridge registers as tests/e2e/io/command_interface/uci_targets_test.py reaches
it. The stress mode adds the hostile forms: buffer exhaustion, records at the edges, locked
files, copy with many sources, N of every image type and inside an image, many images to
push the mount cache, FTP writing into the image the C64 uses, and a REST softiec reset
during traffic.

The device's own log is checked as well. --log chooses off, on, both (off then on, the
default), or toggle. In an off or on phase the setting "Log Every Operation" is fixed for
the phase; in a toggle phase a lane flips it every 10 to 20 seconds while the bus runs, so
a transfer overlaps a flip, which is the case Suite11-OperationLogNoReconfigure guards on
the host. The suite records every command, open and close it makes and correlates
the device's syslog lines against them: with logging off only the failure lines are
expected, with logging on every operation, and in a toggle phase each event is judged
against the setting in force while it ran. The syslog is read from a file an external
collector writes, one line per datagram as "<source ip> <HH:MM:SS> <text>", named with
--syslog-spool; without it the suite binds the UDP port the device's "Log to Syslog Server"
setting names when that address is one of this host's, and otherwise reports why it could
not correlate rather than failing the run. A unique marker command at the start and end of
each phase gives the device's source address in a shared spool and bounds the window. Every
line carries the drive's sequence number, so a line delivered twice is dropped, and only as
many expected lines may be missing as numbers are absent from the window.

--mode selects the run; --duration overrides the seconds of a single soak or stress
run; --soak-duration and --stress-duration set the two phases of a both run. --profile
is kept for run-tests: stress runs the stress mode for ten minutes and soak runs the
soak mode for four hours, and --profile overrides --mode; --log defaults to off under
--profile so an unattended gate needs no collector. --seed and --no-lanes work in
every mode; --no-lanes leaves out the REST and FTP lanes so the drive's heap use can be
told from theirs.

The caches fill during the first part of a session: in a 30 minute soak the free heap fell
by about 30 KB over the first 60 to 70 iterations, on an Ultimate 64 Elite and on a U2+L,
and then stayed flat. The soak judges its heap over the second half of the samples for
that reason, so a run shorter than about 20 minutes says little about a leak.

The suite needs a C64 with a standard KERNAL, REST and FTP, and one Software IEC
partition numbered 1. It uses device 11, and restores the settings, the partition's
working directory, and removes its own directory.
"""
import argparse
import contextlib
import os
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
from iec_agent import CLOSE, OPEN, READ_COUNT, READ_TO_EOI, STATUS_BYTES, WRITE, Agent, iec_drive, restorable_path  # noqa: E402

import softiec_log  # noqa: E402

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
# The stress lanes can fill the network stack until it refuses or resets a single REST
# request, and the C64 lane reaches its agent over REST too. Before the firmware is called
# dead, or the drive called deaf to the C64, the lanes are paused and REST is asked this many
# times, this far apart.
LIVENESS_ATTEMPTS = 6
# How long a phase waits for its end marker to reach the syslog before correlating.
LOG_FLUSH_SECONDS = 30.0
LIVENESS_RETRY_SECONDS = 2.0
# How long a pause of the lanes waits for their requests already on the way.
LANE_PAUSE_SECONDS = 1.0

REST_LANE_SECONDS = 1.5
FTP_LANE_SECONDS = 4.0
# How many operations the failure report names.
HISTORY = 25
# How many recorded anomalies a phase's report quotes in full, besides counting them all.
ANOMALY_EXAMPLES = 10

# The Software IEC log setting, in the store it lives in (software/io/iec/iec_drive.cc).
LOG_CATEGORY = "SoftIEC Drive Settings"
LOG_ITEM = "Log Every Operation"
# The Command Interface setting, needed for the UCI target (software/io/c64/c64.cc).
CMD_IF_CATEGORY = "C64 and Cartridge Settings"
CMD_IF_ITEM = "Command Interface"
# The network setting that names the syslog destination (software/network/network_config.cc).
SYSLOG_CATEGORY = "Network Settings"
SYSLOG_ITEM = "Log to Syslog Server"

# A toggle phase flips the log setting no more often than this, so the flash writes stay
# bounded, and no less often, so the phase spends time in each state.
LOG_FLIP_MIN_SECONDS = 10.0
LOG_FLIP_MAX_SECONDS = 20.0
# How long to wait for a read-back to confirm a flip before giving up on it.
LOG_CONFIRM_SECONDS = 6.0

# The x00 wrapped files the fixture lays down over FTP, by host name and CBM name. A P00,
# S00, U00 or R00 file carries "C64File" and a zero, its CBM name in the 16 bytes at offset
# 8, and a relative file's record length at offset 25 (SI-144).
FIXTURE_X00 = {"WRAPPED.P00": ("MY GAME", 0, 40), "TEXT.S00": ("NOTES", 0, 60),
               "TOOL.U00": ("UTILITY", 0, 48), "DATA.R00": ("RECORDS", 32, 320)}
# The disk images the fixture creates in the run's directory, by name and REST creator.
FIXTURE_IMAGES = {"DISK.D64": "create_d64", "DISK.D71": "create_d71",
                  "DISK.D81": "create_d81", "DISK.DNP": "create_dnp"}
# How many small files the fixture writes into MANY, to read a directory to its end.
MANY_ENTRIES = 40


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
# the stress FTP lanes and the C64 fight over, PCLANE the soak FTP lane's own file, X00 the
# x00 wrapped files, MANY a directory of dozens of files read to the end, IMAGES the disk
# images of every type.
FIXTURE_DIRS = ("OS", "OS/SETTINGS", "OS/LIBRARY", "OS/KERNAL", "OS/DRIVERS", "OS/TK",
                "OS/TEMPORARY", "WORK", "PCLANE", "RACE", "X00", "MANY", "IMAGES")


def x00_wrapper(cbm_name, record_length, body):
    """A P00/S00/U00/R00 file's bytes: the 26 byte header that names it, then the data."""
    header = b"C64File\0" + cbm_name.encode("ascii").ljust(16, b"\0") + b"\0" \
        + bytes([record_length])
    return header + body


class Dead(Failure):
    """The firmware stopped answering."""


class Corruption(Failure):
    """Data read back from the drive differed from what was written or stored.

    A subclass of Failure so that a soak reports it like any other failure, and a
    stress run, which tolerates most failures, still fails on this one.
    """


class RecordingAgent:
    """A wrapper around the IEC agent that turns bus operations into the log events the phase
    correlates against, at the one boundary every operation crosses. Wrapping the agent
    catches the operations the Session helpers do not funnel: recover(), move_drive() (which
    calls self.call, so it routes through here too), the direct writes to channel 15, and an
    operation cut short by a Failure.

    What the bus sees, from tests/e2e/io/iec/iec_agent.asm and the KERNAL:

      * WRITE to channel 15 (CHKOUT $FFC9, CHROUT, UNLISTEN) sends a command; the drive runs
        and logs it when the write ends. The command's status is what the C64 reads next, so
        a WRITE to 15 opens a pending command finished by the next READ_TO_EOI on 15: the
        answer is a status line for most commands and the reply bytes for one that answers
        data (M-R, G-P, T-R*), told apart by whether the bytes parse as a two-digit status.
        If another operation comes first, the command is finished with status unknown.
      * OPEN (SETNAM $FFBD, SETLFS $FFBA, OPEN $FFC0) with a non-empty name sends LISTEN,
        the secondary with $F0, the name and UNLISTEN, which the drive opens and logs. On
        secondary 15 that is a command; on any other it is an open, whose status is the next
        status read. A zero-length name sends no bytes the drive parses, so it records
        nothing, but a successful OPEN still enters the logical file in the KERNAL table.
      * CLOSE (CLOSE $FFC3) sends nothing for a logical file that is not open, and LISTEN,
        the secondary with $E0 and UNLISTEN for one that is, whether or not it was opened with
        a name, which the drive logs as a close of that secondary address. So a close is
        recorded only for a logical file this recorder saw opened, with the secondary address
        it was opened on. Secondary 15, the command channel, does not log closes.
      * READ_TO_EOI and READ_COUNT read data and log nothing.
      * I (with anything after it) closes data channels 0 to 14 through the same close the bus
        uses (IecCommandChannel::do_initialize_buffers) and answers 00; UJ, U: and U with a
        shifted J do the same (do_reset) and answer 73. Each of those closes logs a close line
        for its channel, whether or not the channel had anything open, and all of them come
        before the command's own line. A later CLOSE from the C64 still logs its close.

    An operation that raises Failure may or may not have reached the drive, so it is recorded
    as an optional event. Events are appended only while session.recording is on, but the
    KERNAL table and the pending command are tracked throughout, so a close inside the window
    knows whether the file was opened before it.
    """

    def __init__(self, agent, session):
        self._agent = agent
        self._session = session
        self._open_files = {}  # logical file number -> the secondary address it was opened on
        self._pending = None  # (kind, txt, dev, t0, chan) awaiting a status read

    def __getattr__(self, name):
        return getattr(self._agent, name)

    @property
    def softiec_device(self):
        return self._agent.softiec_device

    @softiec_device.setter
    def softiec_device(self, value):
        self._agent.softiec_device = value

    move_drive = Agent.move_drive

    def start(self):
        """Starts the agent again. Uploading it resets the C64, which empties the KERNAL's file
        table, so the logical files this recorder tracked as open are gone, and a CLOSE of
        one of them afterwards sends nothing on the bus."""
        self._open_files.clear()
        self._pending = None
        return self._agent.start()

    def _append(self, op, chan, txt, dev, status, t0, t1, reply=None, optional=False):
        if not self._session.recording:
            return
        # U0> logs the device number it changed to, which this side cannot predict, so its
        # dev is not compared (SI-100).
        if bytes(txt).startswith(b"U0>"):
            dev = None
        self._session.log_events.append(softiec_log.Event(
            op=op, chan=chan, txt=bytes(txt), status=status, reply=reply,
            dev=dev, t0=t0, t1=t1, optional=optional))

    @staticmethod
    def closes_data_channels(txt, status):
        """Whether a command closed data channels 0 to 14 before its own line was logged: I
        answered 00, or UJ, U: or U with a shifted J answered 73. A command of 254 bytes or
        more is refused before it is dispatched (32), so it closes nothing."""
        if (not txt) or (len(txt) >= 254):
            return False
        if (txt[0] == ord("I")) and (status == 0):
            return True
        return (len(txt) >= 2) and (txt[0] == ord("U")) and (txt[1] in b"J:\xca") and (status == 73)

    def _finish_pending(self, result, now):
        pending = self._pending
        self._pending = None
        kind, txt, dev, t0, chan = pending
        text = result.decode("latin-1", "replace") if result else ""
        is_status = (len(text) >= 3) and text[:2].isdigit() and (text[2] == ",")
        if kind == "command":
            status = int(text[:2]) if is_status else 0
            reply = None if is_status else bytes(result)
            if is_status and self.closes_data_channels(txt, status):
                for data_channel in range(15):
                    self._append("close", data_channel, b"", dev, None, t0, now)
            self._append("command", 15, txt, dev, status, t0, now, reply=reply)
        else:
            status = int(text[:2]) if is_status else None
            self._append("open", chan, txt, dev, status, t0, now)

    def _flush_unknown(self, now):
        pending = self._pending
        self._pending = None
        kind, txt, dev, t0, chan = pending
        self._append(kind, 15 if kind == "command" else chan, txt, dev, None, t0, now)

    def call(self, op, channel=5, data=b"", device=11, count=None, secondary=None, expect=None):
        now = time.monotonic()
        is_status_read = (op == READ_TO_EOI) and (channel == 15)
        if (self._pending is not None) and not is_status_read:
            self._flush_unknown(now)
        try:
            result = self._agent.call(op, channel=channel, data=data, device=device,
                                      count=count, secondary=secondary, expect=expect)
        except Failure:
            if is_status_read and (self._pending is not None):
                # The command or open was sent but its status was not read; it may have logged.
                kind, txt, dev, t0, chan = self._pending
                self._pending = None
                self._append(kind, 15 if kind == "command" else chan, txt, dev, None, t0, now,
                             optional=True)
            elif (op == OPEN) and (len(data) > 0):
                sa = channel if secondary is None else secondary
                self._append("command" if sa == 15 else "open",
                             15 if sa == 15 else sa, data, device, None, now, now, optional=True)
            elif (op == WRITE) and (channel == 15):
                self._append("command", 15, data, device, None, now, now, optional=True)
            raise
        if is_status_read:
            if self._pending is not None:
                self._finish_pending(result, now)
        elif (op == WRITE) and (channel == 15) and data:
            self._pending = ("command", bytes(data), device, now, 15)
        elif op == OPEN:
            sa = channel if secondary is None else secondary
            self._open_files[channel] = sa
            if len(data) > 0:
                self._pending = ("command" if sa == 15 else "open", bytes(data), device, now, sa)
        elif op == CLOSE:
            sa = self._open_files.pop(channel, None)
            if (sa is not None) and (sa != 15):
                self._append("close", sa, b"", device, None, now, now)
        return result


class Session:
    """One suite run: the device, the agent, the fixture, and what happened."""

    def __init__(self, args):
        self.args = args
        self.api = UltimateApi(args.host, args.password, args.timeout)
        agent = Agent(self.api)
        agent.verify_writes = True  # the stress lanes can fill the RAM disk REST uploads use
        self.agent = RecordingAgent(agent, self)
        self.random = random.Random(args.seed)
        self.folder = "soak" + uuid.uuid4().hex[:6]
        self.here = self.folder.upper()
        self.root = None
        self.policy = None
        self.history = []
        self.iteration = 0
        self.counts = {}
        self.anomalies = {}
        self.anomaly_examples = []
        self.statuses = {}
        self.lane_errors = []
        self.lane_counts = {"rest": 0, "ftp": 0}
        self.heap_before = None
        self.heap_after = None
        self.stop = threading.Event()
        self.quiet = threading.Event()  # set while the lanes must leave the network alone
        self.device = 11
        # The log correlation. `recording` gates event capture; `log_events` is what the C64
        # lane and the UCI steps did; `flips` is when a toggle phase changed the setting.
        self.recording = False
        self.log_events = []
        self.flips = []
        self.flip_lock = threading.Lock()
        self.log_mode = "off"
        self.log_on = False
        self.uci = None
        # Where the device log is read from: a spool file an external collector writes, or a
        # UDP socket this run binds. None when correlation is not configured.
        self.syslog_source = None

    def begin_phase(self):
        """Clear the per-phase tallies so a both run reports each phase on its own."""
        self.iteration = 0
        self.counts = {}
        self.anomalies = {}
        self.anomaly_examples = []
        self.statuses = {}
        self.lane_errors = []
        self.lane_counts = {"rest": 0, "ftp": 0}
        self.heap_before = None
        self.heap_after = None
        self.log_events = []
        self.flips = []

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
        if len(self.anomaly_examples) < ANOMALY_EXAMPLES:
            self.anomaly_examples.append(f"iteration {self.iteration} ({what}): {exc}; before it: "
                                         + " | ".join(self.history[-4:-1]))

    def emit(self, op, chan, txt, status=None, reply=None, optional=False):
        """Record an event the wrapped agent cannot see because it did not cross the bus: the
        UCI target's opens and closes reach the channels over the cartridge registers, not the
        agent. A no-op unless a phase is recording."""
        if not self.recording:
            return
        now = time.monotonic()
        self.log_events.append(softiec_log.Event(
            op=op, chan=chan, txt=bytes(txt), status=status, reply=reply,
            dev=self.device, t0=now, t1=now, optional=optional))

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

    def command_reply(self, text, size):
        """A command whose answer is data rather than a status line, such as G-P, T-RA and
        M-R. The wrapped agent records it as a command whose reply is the bytes read here."""
        data = text if isinstance(text, bytes) else text.encode("latin-1")
        self.note(f"command_reply {data!r}")
        self.agent.call(WRITE, 15, data)
        return self.agent.call(READ_TO_EOI, 15, expect=size)

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
            reply = self.command_reply(bytes([ord("M"), ord("-"), ord("R"), low, high, 2]), 2)
            self.note(f"M-R answered {reply!r}")
            if reply != bytes(2):
                raise Corruption(f"M-R ${high:02x}{low:02x},2 answered {reply!r}, expected two zeros")
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
        a program does between actions to see the drive is well. The clock is read in each
        of its four forms, and G-P is read with no byte and with a partition byte, so every
        RTC and partition-info path is covered over a run."""
        self.status()
        if self.random.randrange(2):
            clock = self.random.choice([b"T-RA", b"T-RI", b"T-RB", b"T-RD"])
            self.note(f"{clock!r} answered {self.command_reply(clock, 40)!r}")
        pick = self.random.randrange(4)
        if pick == 0:
            self.note(f"G-P answered {self.command_reply(b'G-P', 40)!r}")
        elif pick == 1:
            # G-P with a partition byte, with 255 (the current partition), and with 0 (the
            # system partition, which this drive does not have and answers type 0).
            byte = self.random.choice([1, 255, 0])
            self.note(f"G-P {byte} answered {self.command_reply(b'G-P' + bytes([byte]), 40)!r}")

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
        # The commands a session sends now and then, and the ones a person tries by mistake.
        # XPWD answers the working directory as data; I and UJ close channels; the rest are
        # the deliberately-unimplemented forms of section 18.1 (V is 31, E and X other than
        # XPWD are 30, T-W is 30). S-8 and S-9 move the drive (SI-101) onto a number another
        # drive may hold, so they stay with iec-dos-commands, which frees it first.
        for text, allowed in (("I", (0,)), ("I0", (0,)), ("UJ", (73,)), ("U9", (73,)),
                              ("UI+", (0,)), ("UI-", (0,)), ("Z9", (31,)), ("E", (30,)),
                              ("XYZ", (30,)), ("V", (31,)), ("T-W", (30,))):
            if self.random.randrange(2):
                continue
            self.command(text, allowed=allowed)
        if self.random.randrange(2):
            where = self.command_reply(b"XPWD\r", 40).decode("latin-1").strip()
            self.note(f"XPWD answered {where!r}")
            if not where.startswith("1:"):
                raise Failure(f"XPWD in partition 1 answered {where!r}")

    def secondaries(self):
        """A PRG LOAD on secondary 0 and a PRG SAVE on secondary 1, the KERNAL's own
        defaults: secondary 0 with no mode reads a PRG, secondary 1 with no mode writes one.
        The saved file is read back on secondary 0 and compared."""
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        fixture = self.random.choice(FIXTURE_DOCUMENTS)
        cbm = fixture.rpartition("/")[2].rsplit(".", 1)[0]
        directory = fixture.rpartition("/")[0]
        self.command(f"CD//{self.here}/{directory}/", allowed=(0,))
        response = self.open(5, cbm, secondary=0)
        loaded = self.read(5) if response.startswith("00") else None
        self.close(5)
        if loaded != pattern(fixture, FIXTURE[fixture]):
            raise Corruption(f"a LOAD of {cbm} on secondary 0 read {len(loaded or b'')} bytes")
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        data = bytes(self.random.randrange(256) for _ in range(self.random.randrange(2, 300)))
        name = f"SAVED{self.random.randrange(3)}"
        response = self.open(6, f"@0:{name}", secondary=1)
        if not response.startswith("00"):
            raise Failure(f"a SAVE of {name} on secondary 1 answered {response}")
        self.write_stream(6, data)
        self.close(6)
        response, back = self.read_file(8, name)
        if back != data:
            raise Corruption(f"{name} saved on secondary 1 read back {len(back or b'')} bytes ({response})")

    def usr_append(self):
        """A USR file written and read back, and an append that lands after existing data."""
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        name = f"U{self.random.randrange(3)}"
        head = bytes(self.random.randrange(256) for _ in range(self.random.randrange(2, 120)))
        tail = bytes(self.random.randrange(256) for _ in range(self.random.randrange(2, 120)))
        response = self.open(7, f"@:{name},U,W")
        if not response.startswith("00"):
            raise Failure(f"{name},U,W answered {response}")
        self.write_stream(7, head)
        self.close(7)
        response = self.open(7, f"{name},U,A")
        if response.startswith("00"):
            self.write_stream(7, tail)
        self.close(7)
        response, back = self.read_file(8, f"{name},U")
        if back != head + tail:
            raise Corruption(f"{name},U after an append read {len(back or b'')} bytes, "
                             f"expected {len(head + tail)} ({response})")

    def x00_files(self):
        """Reads an x00 wrapped fixture by the CBM name in its header and checks the data,
        and now and then makes a throwaway wrapper, renames it through the drive and scratches
        it, which is how the drive's rename rewrites the header and its scratch removes it."""
        self.partition()
        self.command(f"CD//{self.here}/X00/", allowed=(0,))
        host, (cbm, record_length, size) = self.random.choice(list(FIXTURE_X00.items()))
        body = pattern(host, size)
        if record_length == 0:
            response, data = self.read_file(5, cbm)
            if data != body:
                raise Corruption(f"the x00 file {host} read {len(data or b'')} bytes as {cbm} ({response})")
        if self.random.randrange(3) == 0:
            throwaway = f"TMP{self.random.randrange(4)}"
            payload = bytes(self.random.randrange(256) for _ in range(self.random.randrange(2, 80)))
            with ftp.session(self.args.host, self.args.password) as client:
                target = f"{self.root.rstrip('/')}/{self.folder}/X00/{throwaway}.S00"
                ftp.store(client, target, x00_wrapper(throwaway, 0, payload))
            response, data = self.read_file(5, throwaway)
            if data != payload:
                raise Corruption(f"the throwaway wrapper {throwaway} read {len(data or b'')} bytes")
            renamed = f"{throwaway}X"
            self.command(f"R:{renamed}={throwaway}", allowed=(0, 62))
            self.command(f"S:{renamed}", allowed=(1,))

    def images(self):
        """Enters a D71, D81 or DNP image, lists it, saves a file inside it and reads it back,
        makes and enters a subdirectory in the DNP image, and reads a sector by direct access
        at the image's own geometry, then leaves the image."""
        self.partition()
        host = self.random.choice(["DISK.D71", "DISK.D81", "DISK.DNP"])
        self.command(f"CD//{self.here}/IMAGES/{host}", allowed=(0,))
        self.open(11, "$", secondary=0)
        self.read(11)
        self.close(11)
        name = f"IN{self.random.randrange(3)}"
        data = bytes(self.random.randrange(256) for _ in range(self.random.randrange(2, 200)))
        self.save_and_verify(name, data, replace=True)
        if host == "DISK.DNP":
            sub = f"SUB{self.random.randrange(3)}"
            self.command(f"MD:{sub}", allowed=(0, 63))
            self.command(f"CD:{sub}", allowed=(0,))
            self.save_and_verify(f"DEEP{self.random.randrange(2)}",
                                 bytes(self.random.randrange(256) for _ in range(20)), replace=True)
            self.command("CD_", allowed=(0,))
        else:
            # A direct-access read at a track only this image type has: track 40 on a D71
            # (70 tracks), track 40 sector 30 on a D81 (80 tracks of 40 sectors).
            track, sector = (40, 5) if host == "DISK.D71" else (40, 30)
            self.open(10, "#")
            if self.command(f"U1:10,0,{track},{sector}", allowed=(0, 66)).startswith("00"):
                self.read(10, limit=256)
            self.close(10)
        self.command(f"CD//{self.here}", allowed=(0,))

    def many_entries(self):
        """Reads a directory of dozens of files to its end, which walks the listing across
        many blocks and produces the listing-end log line."""
        self.partition()
        response = self.open(11, f"$//{self.here}/MANY/", secondary=0)
        if response.startswith("00"):
            listing = self.read(11)
            quotes = listing.count(b"\"")
            if quotes < 2 * MANY_ENTRIES:
                raise Failure(f"the many-entry listing held {quotes} quotes, "
                              f"expected at least {2 * MANY_ENTRIES}")
        self.close(11)

    def uci_between(self):
        """The UCI SoftIEC target, reached between bus operations over the cartridge
        registers. Skipped with a reason where the command interface is not present."""
        if self.uci is None:
            return
        with self.lanes_paused():
            self.uci.exercise(self)

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
        for channel in range(2, 11):
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
            self.close(12)  # a channel that was never opened logs nothing, and records none
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
        """A relative file at the edges of the record layout: record length 1 and 254, and a
        position command to record 0, to a high record and to record 65535, which pushes
        seek_record and its file growth. The record number is kept moderate for the payload
        it verifies so the firmware does not spend long filling a huge gap with empty
        records; record 65535 is only positioned to, and 0, 50, 51 or 72 is allowed there."""
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        length = self.random.choice([1, 200, 254])
        name = f"RB{length}"
        self.command(f"S:{name}", allowed=(0, 1))
        try:
            self.open(9, name.encode("ascii") + b",L," + bytes([length]))
            # Record 0 is treated as record 1 by the firmware; record 65535 is the top of the
            # 16 bit record number, and growing the file that far fills a RAM disk partition,
            # which answers 72.
            for edge in (0, 65535):
                self.command(bytes([ord("P"), 0x60 | 9, edge & 0xFF, edge >> 8, 1]),
                             allowed=(0, 50, 51, 72))
            record = self.random.choice([80, 160, 250])
            payload = bytes(self.random.randrange(1, 256) for _ in range(self.random.randrange(1, length + 1)))
            self.command(bytes([ord("P"), 0x60 | 9, record & 0xFF, record >> 8, 1]), allowed=(0, 50))
            self.agent.call(WRITE, 9, payload)
            self.command(bytes([ord("P"), 0x60 | 9, record & 0xFF, record >> 8, 1]), allowed=(0, 50))
            back = self.read(9, limit=length)
        finally:
            # The file grown towards record 65535 is scratched whatever happened, so a full
            # medium does not outlast this step and starve the lanes of the phases after it.
            self.close(9)
            self.command(f"S:{name}", allowed=(0, 1))
        if back[:len(payload)] != payload:
            raise Corruption(f"record {record} of {name} read {back!r}, wrote {payload!r}")

    def buffer_exhaustion(self):
        """Opens direct-access buffers until the KERNAL table is full, and sends a block
        command to a channel that was never opened with # (70 NO CHANNEL). Every channel it
        opened is closed."""
        self.partition()
        self.command(f"CD//{self.here}/DISK.D64", allowed=(0,))
        opened = []
        for channel in range(2, 11):
            try:
                response = self.open(channel, "#")
            except Failure:
                break
            if response.startswith("00"):
                opened.append(channel)
            else:
                self.record_status("buffer open", response)
                self.close(channel)
        # A block command naming a channel that is not a buffer answers 70.
        self.record_status("block on non-buffer", self.command("U1:13,0,18,0", allowed=range(100)))
        for channel in opened:
            self.close(channel)
        self.command(f"CD//{self.here}", allowed=(0,))

    def locked_files(self):
        """Locks a file, checks a scratch refuses it and it lists with the lock marker,
        unlocks it and scratches it, and locks a directory so a remove refuses it. All in a
        throwaway directory the teardown removes."""
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        name = f"LK{self.random.randrange(4)}"
        response = self.open(7, f"@:{name},S,W")
        if response.startswith("00"):
            self.write_stream(7, bytes(self.random.randrange(256) for _ in range(20)))
        self.close(7)
        self.command(f"L:{name}", allowed=(0, 62))
        # A locked file is not scratched: the count is zero.
        locked = self.command(f"S:{name}", allowed=(1,))
        self.record_status("scratch locked", locked)
        self.command(f"L:{name}", allowed=(0, 62))  # unlock
        self.command(f"S:{name}", allowed=(1,))

    def copy_sources(self):
        """A copy that concatenates up to eight sources, some of which do not exist, and a
        copy whose destination already exists (63) and whose destination is a wildcard (33).
        The concatenation's own bytes are verified when every source was present."""
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        made = []
        parts = []
        for i in range(self.random.randrange(2, 6)):
            src = f"SRC{i}"
            body = bytes([65 + i]) * self.random.randrange(1, 30)
            response = self.open(7, f"@:{src},S,W")
            if response.startswith("00"):
                self.write_stream(7, body)
                made.append(src)
                parts.append(body)
            self.close(7)
        dest = f"CAT{self.random.randrange(3)}"
        self.command(f"S:{dest}", allowed=(1,))  # clear a previous run's destination
        sources = ",".join(made + [f"NONE{self.random.randrange(9)}"] * self.random.randrange(0, 2))
        expected = b"".join(parts)
        answer = self.command(f"C:{dest}={sources},S", allowed=range(100))
        self.record_status("copy sources", answer)
        if answer.startswith("00") and made and "NONE" not in sources:
            response, back = self.read_file(8, f"{dest},S")
            if back != expected:
                raise Corruption(f"a copy of {sources} read back {len(back or b'')} bytes, "
                                 f"expected {len(expected)} ({response})")
        # A destination that already exists and a wildcard destination.
        self.record_status("copy exists", self.command(f"C:{dest}={made[0] if made else 'SRC0'}",
                                                        allowed=range(100)))
        self.record_status("copy wildcard dest", self.command(f"C:D*={made[0] if made else 'SRC0'}",
                                                              allowed=(33,)))
        self.command(f"S:{dest}", allowed=(1,))

    def format_images(self):
        """N creates or formats an image of each type in a throwaway directory, and N inside a
        mounted image is refused (30). The created image is scratched again, because a D81
        takes 800 KB of a RAM disk partition."""
        self.partition()
        self.command(f"CD//{self.here}/WORK/", allowed=(0,))
        name = f"NEW{self.random.randrange(4)}"
        spec = self.random.choice([(f"N:{name}.D64,AA", (0, 63, 72)),
                                   (f"N:{name}.D71,AB", (0, 63, 72)),
                                   (f"N:{name}.D81,AC", (0, 63, 72)),
                                   (f"N:{name}.DNP,002", (0, 63, 72)),
                                   (f"N:{name}", (0, 63, 72)),          # no extension gets .D64
                                   (f"N:{name}.D64", (30,)),            # a new image needs an id
                                   ("NNOCOLON,AA", (34,))])
        self.record_status("format", self.command(spec[0], allowed=spec[1]))
        self.command(f"S:{name}*", allowed=(0, 1))
        # N inside a mounted image is refused rather than making an image in the image.
        self.command(f"CD//{self.here}/DISK.D64", allowed=(0,))
        self.record_status("format in image", self.command("N:INNER,01", allowed=(30,)))
        self.command(f"CD//{self.here}", allowed=(0,))

    def mount_churn(self):
        """Enters one image after another, more than the mount cache holds, while a listing
        of the first is still open, which is the eviction the firmware fixed. The extra
        images are created here and left for the teardown."""
        self.partition()
        images = []
        with ftp.session(self.args.host, self.args.password) as client:
            base = f"{self.root.rstrip('/')}/{self.folder}/IMAGES"
            for i in range(10):
                name = f"CH{i}.D64"
                if not ftp.quietly(lambda: client.size(f"{base}/{name}")):
                    self.api.files.create_d64(f"{base}/{name}", diskname=f"CHURN{i}")
                images.append(name)
        # Hold a listing of the first image open, then walk through the rest.
        self.command(f"CD//{self.here}/IMAGES/{images[0]}", allowed=(0,))
        held = self.open(11, "$", secondary=0)
        if held.startswith("00"):
            self.read(11, limit=self.random.choice([10, 20]))
        for name in images[1:]:
            self.command(f"CD//{self.here}/IMAGES/{name}", allowed=(0, 71))
            self.open(5, "$", secondary=0)
            self.read(5, limit=10)
            self.close(5)
        self.close(11)
        self.command(f"CD//{self.here}", allowed=(0,))

    def uci_stress(self):
        """The UCI target interleaved with the bus channels, which takes the drive lock while
        a bus channel may be open (CR-6). Skipped where the command interface is absent."""
        if self.uci is None:
            return
        with self.lanes_paused():
            self.uci.exercise(self, hostile=True)

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
                       ("secondaries", secondaries, 2), ("usr append", usr_append, 2),
                       ("x00", x00_files, 2), ("images", images, 2),
                       ("many entries", many_entries, 1), ("uci", uci_between, 2),
                       ("relative", relative, 1), ("direct access", direct_access, 1),
                       ("directories", directories, 1), ("other commands", other_commands, 1),
                       ("reboot", boot, 1))

    STRESS_OPERATIONS = (("fuzz command", fuzz_command, 4), ("fuzz open", fuzz_open, 3),
                         ("many channels", many_channels, 2), ("abandoned", abandoned_transfers, 2),
                         ("direct access edges", direct_access_edges, 2), ("scratch storm", scratch_storm, 2),
                         ("race file", race_file, 2), ("large file", large_file, 1),
                         ("relative edges", relative_edges, 1), ("resets", resets_with_open_channels, 1),
                         ("device number", device_number, 1), ("buffer exhaustion", buffer_exhaustion, 2),
                         ("locked files", locked_files, 2), ("copy sources", copy_sources, 2),
                         ("format images", format_images, 1), ("mount churn", mount_churn, 1),
                         ("uci stress", uci_stress, 2))

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
                self.recover_quietly()
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

    def flip_log(self, api, on):
        """Change Log Every Operation and wait for a read-back to confirm it, recording when
        the change was requested and when it took, so a toggle phase can say which setting was
        in force while each operation ran. Changing this setting alone does not reconfigure
        the IEC processor, so it does not drop a transfer (Suite11-OperationLogNoReconfigure)."""
        requested = time.monotonic()
        api.configs.set(LOG_CATEGORY, LOG_ITEM, "Enabled" if on else "Disabled")
        want = "Enabled" if on else "Disabled"
        deadline = time.monotonic() + LOG_CONFIRM_SECONDS
        confirmed = None
        while time.monotonic() < deadline:
            if str(api.configs.get(LOG_CATEGORY, LOG_ITEM)) == want:
                confirmed = time.monotonic()
                break
        with self.flip_lock:
            self.flips.append(softiec_log.Flip(requested=requested,
                                               confirmed=confirmed or time.monotonic(), on=on))
        self.log_on = on

    def rest_lane(self):
        # A client of its own: the agent's lane must not share a connection with this one, and
        # its own random source, because the C64 lane owns self.random.
        api = UltimateApi(self.args.host, self.args.password, self.args.timeout)
        lane_rnd = random.Random(self.args.seed + 100)
        next_flip = time.monotonic() + lane_rnd.uniform(LOG_FLIP_MIN_SECONDS, LOG_FLIP_MAX_SECONDS)
        flip_on = self.log_on
        last_reset = time.monotonic()
        while not self.stop.wait(self.policy.rest_seconds):
            if self.quiet.is_set():
                self.stop.wait(0.2)
                continue
            try:
                api.rest.json("/v1/drives")
                api.machine.heap_free()
                self.lane_counts["rest"] += 1
                # In a toggle phase, flip the log setting while the bus runs so a transfer
                # overlaps a flip. Only the log setting moves; the bus id and enable stay, so
                # the drive stays on device 11 enabled.
                if self.log_mode == "toggle" and time.monotonic() >= next_flip:
                    flip_on = not flip_on
                    self.flip_log(api, flip_on)
                    next_flip = time.monotonic() + lane_rnd.uniform(
                        LOG_FLIP_MIN_SECONDS, LOG_FLIP_MAX_SECONDS)
                # A stress phase resets the Software IEC drive over REST during traffic, which
                # takes the drive lock (CR-6), closes the data channels and returns every
                # partition to its root. The C64 lane reselects its partition and directory
                # each step, so it recovers; the reset is the hostile event.
                if self.policy.rest_reset and (time.monotonic() - last_reset > 20):
                    # The drives helper knows only the emulated slots a and b, so the softiec
                    # slot is reset through the route directly.
                    api.rest.request("PUT", "/v1/drives/softiec:reset")
                    last_reset = time.monotonic()
            except Exception as exc:  # the main lane decides whether this is a death
                self.lane_errors.append(f"rest at iteration {self.iteration}: {exc}")

    def ftp_lane(self, index):
        directory = f"{self.root.rstrip('/')}/{self.folder}"
        payload = pattern(f"pclane{index}", 3000)
        lane_random = random.Random(self.args.seed + index)  # the C64 lane owns self.random
        while not self.stop.wait(self.policy.ftp_seconds):
            if self.quiet.is_set():
                continue
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
                        # Write a file inside the disk image the C64's direct access uses, so
                        # an FTP mount of the image and the drive's sector access race. The
                        # image content is not owned by the C64, so a difference is not the
                        # corruption the suite fails on.
                        if lane_random.randrange(3) == 0:
                            image_file = f"{directory}/DISK.D64/FTP{index}.SEQ"
                            ftp.quietly(lambda: ftp.store(client, image_file, payload[:200]))
                            ftp.quietly(lambda: client.delete(image_file))
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
        """Whether REST still answers, asked with the lanes paused and several times, so a
        request the saturated network stack refused is not taken for a dead firmware."""
        self.quiet.set()
        try:
            for _attempt in range(LIVENESS_ATTEMPTS):
                try:
                    self.api.rest.json("/v1/info")
                    return True
                except Exception:
                    time.sleep(LIVENESS_RETRY_SECONDS)
            return False
        finally:
            self.quiet.clear()

    @contextlib.contextmanager
    def lanes_paused(self):
        """Pauses the REST and FTP lanes for a UCI step. The target is driven one register write
        per REST request, and with the stress lanes filling the network a queued command byte
        was seen not to arrive, so the next command's bytes ran as part of it."""
        self.quiet.set()
        try:
            time.sleep(LANE_PAUSE_SECONDS)  # let the requests already on the way finish
            yield
        finally:
            self.quiet.clear()

    def recover_quietly(self):
        """recover(), and if that fails, once more with the lanes paused: the agent is driven
        over REST, so a recovery the lanes starved of the network is not yet a drive that has
        stopped answering the C64."""
        try:
            self.recover()
        except Failure:
            self.quiet.set()
            try:
                time.sleep(LIVENESS_RETRY_SECONDS)
                self.recover()
            finally:
                self.quiet.clear()

    def fixture(self):
        directory = f"{self.root.rstrip('/')}/{self.folder}"
        with ftp.session(self.args.host, self.args.password) as client:
            client.mkd(directory)
            for sub in FIXTURE_DIRS:
                client.mkd(f"{directory}/{sub}")
            for name, size in FIXTURE.items():
                ftp.store(client, f"{directory}/{name}", pattern(name, size))
            # The x00 wrapped files, read by the CBM name in their header (SI-144).
            for host, (cbm, record_length, size) in FIXTURE_X00.items():
                ftp.store(client, f"{directory}/X00/{host}",
                          x00_wrapper(cbm, record_length, pattern(host, size)))
            # A directory of dozens of files, to read a listing to its end.
            for i in range(MANY_ENTRIES):
                ftp.store(client, f"{directory}/MANY/FILE{i:03d}.PRG", pattern(f"many{i}", 1 + i))
        # The disk images of every type: a D64 in the run root for direct access, and one of
        # each in IMAGES for the image operations. create_dnp needs a track count.
        self.api.files.create_d64(f"{directory}/DISK.D64", diskname="SOAK")
        for name, creator in FIXTURE_IMAGES.items():
            target = f"{directory}/IMAGES/{name}"
            if creator == "create_dnp":
                self.api.files.create_dnp(target, tracks=4, diskname="SOAKNAT")
            else:
                getattr(self.api.files, creator)(target, diskname="SOAKIMG")

    def marker(self, nonce):
        """A unique command that always fails (X is a command letter, so anything but XPWD
        answers 30), so it produces a "command failed" line whatever the log setting. The
        line's source address identifies this device in a shared spool, and the markers at
        the start and end of a phase bound the window of lines that belong to it."""
        data = softiec_log.marker_text(nonce)
        self.note(f"marker {data!r}")
        # The wrapped agent records this as a failed command; its "command failed" line is
        # what select_window finds by the nonce in the marker text.
        self.agent.call(WRITE, 15, data)
        return self.agent.call(READ_TO_EOI, 15, expect=STATUS_BYTES).decode("latin-1").strip()

    def dump_log_window(self, device_ip, texts):
        """With --log-dump, writes the operations this phase recorded and the device's log lines
        in its window side by side, so a verdict can be checked by hand."""
        if not getattr(self.args, "log_dump", None):
            return
        base = Path(self.args.log_dump)
        base.mkdir(parents=True, exist_ok=True)
        stem = base / f"{self.policy.name}-log-{self.log_mode}-{int(time.time())}"
        with open(f"{stem}-operations.txt", "w", encoding="utf-8", errors="replace") as out:
            for ev in self.log_events:
                out.write(f"{ev.what} chan={ev.chan} dev={ev.dev} len={len(ev.txt)} "
                          f"txt=\"{softiec_log.render_text(ev.txt)}\" status={ev.status} "
                          f"reply={None if ev.reply is None else softiec_log.render_text(ev.reply)!r} "
                          f"optional={ev.optional} hex={ev.txt.hex()}\n")
        with open(f"{stem}-lines.txt", "w", encoding="utf-8", errors="replace") as out:
            out.write(f"# from {device_ip}\n")
            for text in texts:
                out.write(text + "\n")

    def log_verdict(self, start_nonce, end_nonce, pre_nonce=None, post_nonce=None):
        """Read this phase's device log and correlate it with what the C64 lane did. Returns
        (a Correlation or None, a note). None with a reason where no syslog source is
        configured or the marker was not found, which the caller reports without failing."""
        source = self.syslog_source
        if source is None:
            return None, "no syslog source (pass --syslog-spool or configure the device's syslog to this host)"
        # The device's syslog task sends at most about 200 lines a second and waits between
        # rounds, so under load the end marker arrives seconds after the C64 sent it. The
        # window is read once the end marker is there, or after LOG_FLUSH_SECONDS.
        end_txt = softiec_log.render_text(softiec_log.marker_text(post_nonce or end_nonce))
        deadline = time.monotonic() + LOG_FLUSH_SECONDS
        while True:
            time.sleep(1.0)
            entries = source.entries()
            device_ip, texts = softiec_log.select_window(entries, start_nonce, end_nonce,
                                                         pre_nonce, post_nonce)
            if any(end_txt in text for text in texts[-50:]) or (time.monotonic() > deadline):
                break
        if device_ip is None:
            return None, ("the start marker was not found in the syslog; it may have been "
                          "split or dropped, or the device does not log to this collector")
        self.dump_log_window(device_ip, texts)
        if self.log_mode == "toggle":
            with self.flip_lock:
                flips = list(self.flips)
            result = softiec_log.correlate_toggle(self.log_events, texts, flips, initial_on=False,
                                                  label=f"toggle from {device_ip}")
        else:
            result = softiec_log.correlate(self.log_events, texts, logging_on=self.log_on,
                                          label=f"logging {'on' if self.log_on else 'off'} "
                                                f"from {device_ip}")
        return result, None

    def run(self, policy, duration, log_mode):
        """Runs one phase: boot the session, set the log setting the phase asks for, then
        repeat the policy's operations under its pacing until the deadline, sampling the heap
        and, for stress, checking the drive after each step. A start and an end marker bound
        the window the device log is correlated over. Returns the heap samples, the failures,
        and the log correlation (or None)."""
        self.policy = policy
        self.log_mode = log_mode
        self.stop = threading.Event()
        self.quiet = threading.Event()  # set while the lanes must leave the network alone
        heap = []
        failures = []

        # The C64 OS boot sequence is the session start (see #877), run once before timing and
        # before recording, so the boot's own lines are not looked for in the phase window.
        self.recording = False
        self.boot()
        # Fix the log setting for an off or an on phase; a toggle phase starts off and the
        # REST lane flips it. The setting is restored by the category snapshot at the end.
        self.log_on = (log_mode == "on")
        if log_mode in ("on", "off"):
            self.api.configs.set(LOG_CATEGORY, LOG_ITEM, "Enabled" if self.log_on else "Disabled")
        elif log_mode == "toggle":
            self.api.configs.set(LOG_CATEGORY, LOG_ITEM, "Disabled")
        time.sleep(2.0)  # a short settle so the first heap reading is not mid-cache-fill
        self.heap_before = self.api.machine.heap_free()

        # Four markers: an outer pair that is not recorded and bounds the window, and an inner
        # pair that is recorded. Another task's message can split a marker's line like any
        # other; with a numbered line on each side, a split inner marker shows as a gap in the
        # sequence numbers rather than as a line the window cannot account for.
        pre_nonce, start_nonce, end_nonce, post_nonce = (uuid.uuid4().hex[:8] for _ in range(4))
        if hasattr(self.syslog_source, "mark"):
            self.syslog_source.mark()
        self.marker(pre_nonce)
        self.recording = True
        self.marker(start_nonce)

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
                    self.recover_quietly()
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
                        self.recover_quietly()
                    else:
                        failures.append(f"iteration {self.iteration} ({label}): {exc}")
                        try:
                            self.recover_quietly()
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
            # The end marker closes the correlation window while the setting is still what
            # the phase held; anything after it is outside the window.
            try:
                self.marker(end_nonce)
                self.recording = False
                self.marker(post_nonce)
            except Failure:
                pass
            self.recording = False
        finally:
            self.stop.set()
            for lane in lanes:
                lane.join(timeout=30)

        correlation, note = self.log_verdict(start_nonce, end_nonce, pre_nonce, post_nonce)
        # An idle settle, then the final heap reading for the before-and-after verdict.
        time.sleep(IDLE_SETTLE_SECONDS)
        try:
            self.recover()
        except Failure:
            pass
        self.heap_after = self.api.machine.heap_free()
        return heap, failures, (correlation, note)

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
                 ftp_seconds, ftp_lanes, rest_reset, race, tolerant, heap_mode, checkpoint):
        self.name = name
        self.operations = operations
        self.think_min = think_min
        self.think_max = think_max
        self.rest_seconds = rest_seconds
        self.ftp_seconds = ftp_seconds
        self.ftp_lanes = ftp_lanes
        # Whether the REST lane resets the Software IEC drive during traffic (stress only).
        self.rest_reset = rest_reset
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
                     ftp_lanes=1, rest_reset=False, race=False, tolerant=False,
                     heap_mode="slope", checkpoint=False)

# The firmware has four FTP and four HTTP slots, so three racing FTP lanes leaves one
# free for the teardown's FTP session.
STRESS_POLICY = Policy("stress", Session.STRESS_OPERATIONS, think_min=0.0, think_max=0.0,
                       rest_seconds=0.0, ftp_seconds=0.3, ftp_lanes=3, rest_reset=True,
                       race=True, tolerant=True, heap_mode="settled", checkpoint=True)


class SpoolSource:
    """A device log read from a file an external collector writes, one line per datagram as
    softiec_log.SPOOL_FORMAT. mark() remembers where the file ends when a phase starts, and
    entries() reads what the collector has written since, so a phase sees its own lines
    without reading a spool that long runs have grown to tens of megabytes."""

    def __init__(self, path):
        self.path = path
        self.offset = 0

    def mark(self):
        try:
            self.offset = os.path.getsize(self.path)
        except OSError:
            self.offset = 0

    def entries(self):
        return softiec_log.read_spool(self.path, self.offset)

    def stop(self):
        pass


# The UCI SoftIEC target command bytes, from software/io/command_interface/softiec_target.h.
# uci_targets_test defines only the few it uses, so the rest are named here.
UCI_TARGET_SOFTIEC = 0x05
UCI_CMD_OPEN = 0x13
UCI_CMD_CLOSE = 0x14
UCI_CMD_CHKIN = 0x15
UCI_CMD_ADD_PARTITION = 0x20
UCI_CMD_DEL_PARTITION = 0x21


class UciProbe:
    """The UCI SoftIEC target, reached over the cartridge registers ($DF1B-$DF1F) with the
    Uci and RestSession classes from tests/e2e/io/command_interface/uci_targets_test.py. Its
    steps run only between bus operations, on the C64 lane's own thread, so a register access
    never lands in an agent transaction. Its opens and closes go through the same channels the
    bus does, so they log too; they are recorded as optional events."""

    def __init__(self, module, uci, session_ref):
        self.module = module
        self.uci = uci
        self.session = session_ref

    @classmethod
    def create(cls, args, session):
        """Set up the probe, or return (None, reason) where the command interface is not
        present or the SoftIEC target is not loaded. The caller enables the Command Interface
        setting and restores it; this only uses it."""
        try:
            sys.path.insert(0, bootstrap.directory("e2e", "io", "command_interface"))
            import uci_targets_test as module  # noqa: PLC0415
        except Exception as exc:  # noqa: BLE001 - any import trouble means skip UCI
            return None, f"the UCI test module could not be imported: {exc}"
        try:
            rest_session = module.RestSession(args.host, args.password, args.timeout)
            uci = module.Uci(rest_session, module.BUSY_TIMEOUT_SECONDS)
            # The registers read $FF everywhere when nothing decodes them; that is a machine
            # without the interface at these addresses.
            if all(rest_session.peek(0xDF1B + offset) == 0xFF for offset in range(5)):
                return None, "the command interface registers read $FF, so it is not present"
            uci.release()
            reply, status = uci.transact(bytes([module.TARGET_SOFTIEC, module.SOFTIEC_CMD_IDENTIFY]))
            if status == module.SOFTIEC_NOT_LOADED:
                return None, "the SoftIEC target reports the drive is not loaded"
            if not reply.startswith(b"SOFTWARE IEC TARGET"):
                return None, f"the SoftIEC target did not identify itself: {reply!r}"
        except Exception as exc:  # noqa: BLE001 - a device that cannot answer skips UCI
            return None, f"the command interface did not answer: {exc}"
        return cls(module, uci, session), None

    def _read_through_target(self, name, secondary=3):
        """Open a file through the target, read it with CHKIN, close it, and return the bytes
        the response FIFO carried. The open and close log lines, recorded as optional."""
        module = self.module
        target = module.TARGET_SOFTIEC
        self.uci.transact(bytes([target, UCI_CMD_OPEN, secondary, 0]) + name)
        self.session.emit("open", secondary, name, status=0, optional=True)
        data, _status = self.uci.transact(bytes([target, UCI_CMD_CHKIN, secondary]))
        self.uci.transact(bytes([target, UCI_CMD_CLOSE, secondary]))
        self.session.emit("close", secondary, b"", optional=True)
        return data

    def exercise(self, session, hostile=False):
        """Run a handful of target commands. IDENTIFY, GET_FATNAME and GET_IECNAME touch no
        channel and log nothing; the read through the target opens and closes a channel. In
        hostile mode a partition is added and removed, which is a lock path (CR-6)."""
        module = self.module
        target = module.TARGET_SOFTIEC
        here = session.here
        self.uci.transact(bytes([target, module.SOFTIEC_CMD_IDENTIFY]))
        self.uci.transact(bytes([target, module.SOFTIEC_CMD_GET_FATNAME, 0x02]) + b"#")
        # GET_IECNAME on an x00 fixture: its reply is the CBM name in the header (SI-144).
        host = "WRAPPED.P00"
        full = f"{session.root.rstrip('/')}/{session.folder}/X00/{host}".encode("ascii")
        self.uci.transact(bytes([target, module.SOFTIEC_CMD_GET_IECNAME]) + full)
        # Read a small fixture through the target and compare it byte for byte.
        data = self._read_through_target(f"//{here}/OS/SETTINGS/:CONFIG.T".encode("ascii"))
        want = pattern("OS/SETTINGS/CONFIG.T.seq", FIXTURE["OS/SETTINGS/CONFIG.T.seq"])
        if data != want:
            raise Corruption(f"a UCI CHKIN of CONFIG.T read {data!r}, expected {want!r}")
        if hostile:
            index = 200
            path = f"{session.root.rstrip('/')}/{session.folder}/WORK".encode("ascii")
            self.uci.transact(bytes([target, UCI_CMD_ADD_PARTITION, index]) + b"UCITMP:" + path)
            self.uci.transact(bytes([target, UCI_CMD_DEL_PARTITION, index]))

    def stop(self):
        try:
            self.uci.release()
        except Exception:  # noqa: BLE001 - leaving the interface is best effort
            pass


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
    # The caches fill during the first part of a session: in a 30 minute soak the free heap
    # fell by about 30 KB over the first 60 to 70 iterations on an Ultimate 64 Elite and on a
    # U2+L (the mount cache alone keeps eight disk images) and then stayed flat to the end. So
    # the second half of the samples is judged, where a steady loss is a leak.
    tail = heap[len(heap) // 2:]
    if len(tail) < 3:
        return []
    # A sample taken while a lane holds a transfer buffer reads up to about 20 KB low, so
    # the most free heap of the first and the last quarter of the second half are compared
    # rather than two single samples.
    window = max(1, len(tail) // 4)
    first, last = max(tail[:window]), max(tail[-window:])
    slope = (first - last) / max(1, len(tail) - window)
    detail(f"heap free {heap[0]} after warm-up, {first} at the start of the second half, {last} "
           f"at the end, {slope:.0f} bytes lost per iteration over the second half")
    if slope > LEAK_TOLERANCE_BYTES_PER_ITERATION:
        return [f"the heap shrank by {slope:.0f} bytes per iteration"]
    return []


def log_verdict_report(correlation, note):
    """Print the log correlation and return the failures it found. A missing source or an
    unfound marker is reported as detail, not a failure, so a run without a collector still
    passes; a real mismatch between the log and the operations is a failure."""
    if correlation is None:
        detail(f"log correlation skipped: {note}")
        return []
    detail(f"log correlation, {correlation.summary()}")
    for problem in correlation.contradictions[:5]:
        detail(f"  contradiction: {problem}")
    for problem in correlation.logging_off_success[:5]:
        detail(f"  {problem}")
    for problem in correlation.unexpected_bad[:5]:
        detail(f"  {problem}")
    return correlation.problems()


def run_phase(session, duration, policy, log_mode):
    """Runs one phase and reports it as its own section and check with its own heap and log
    verdicts. Returns True if the phase passed. A dead firmware stops the whole run and is
    not caught here."""
    session.begin_phase()
    section(f"{policy.name}, log {log_mode} ({duration:.0f} s)")
    try:
        with check("the drive keeps answering, keeps its data, its heap and a log that matches"):
            heap, failures, (correlation, note) = session.run(policy, duration, log_mode)
            detail(f"{session.iteration} iterations: {dict(sorted(session.counts.items()))}; "
                   f"lanes {session.lane_counts}")
            if session.statuses:
                detail(f"statuses seen: {dict(sorted(session.statuses.items()))}")
            if session.anomalies:
                detail(f"anomalies recorded: {dict(sorted(session.anomalies.items()))}")
                for example in session.anomaly_examples:
                    detail(f"  {example}")
            if session.lane_errors:
                detail("lane errors: " + "; ".join(session.lane_errors[:10]))
            failures = failures + heap_verdict(session, policy, heap)
            failures = failures + log_verdict_report(correlation, note)
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
    """The phases to run, in order, from the mode, the durations and the log setting.

    A base phase is one mode run for one duration; --log then splits it into log phases:
    off, on and toggle each give one phase in that state, and both gives an off phase then an
    on phase. --profile maps to a single mode for run-tests and defaults --log to off so an
    unattended gate needs no collector; --profile overrides --mode.
    """
    if args.profile:
        if args.profile == "stress":
            base = [(args.duration or PROFILE_STRESS_SECONDS, STRESS_POLICY)]
        else:
            base = [(args.duration or PROFILE_SOAK_SECONDS, SOAK_POLICY)]
    elif args.mode == "soak":
        base = [(args.duration or SOAK_DEFAULT_SECONDS, SOAK_POLICY)]
    elif args.mode == "stress":
        base = [(args.duration or STRESS_DEFAULT_SECONDS, STRESS_POLICY)]
    else:
        base = [(args.soak_duration, SOAK_POLICY), (args.stress_duration, STRESS_POLICY)]

    log = args.log or ("off" if args.profile else "both")
    modes = {"off": ["off"], "on": ["on"], "toggle": ["toggle"], "both": ["off", "on"]}[log]
    return [(duration, policy, log_mode)
            for duration, policy in base for log_mode in modes]


def setup_syslog_source(session, api, args):
    """Where this phase's device log comes from, and a note for the report. A spool file when
    --syslog-spool is given; otherwise the UDP port the device's syslog setting names, when
    that address is one of this host's; otherwise None with the reason, which leaves the
    phases running with the log verdict skipped rather than failing the run."""
    if args.syslog_spool:
        return SpoolSource(args.syslog_spool), f"reading the spool {args.syslog_spool}"
    if SYSLOG_CATEGORY not in api.configs.category_names():
        return None, "this device has no Network Settings; pass --syslog-spool"
    parsed = softiec_log.parse_syslog_server(str(api.configs.get(SYSLOG_CATEGORY, SYSLOG_ITEM)))
    if parsed is None:
        return None, "the device's 'Log to Syslog Server' setting is empty; pass --syslog-spool"
    ip, port = parsed
    if ip not in softiec_log.local_addresses():
        return None, (f"the device logs to {ip}, which is not this host; run the collector "
                      f"there and pass --syslog-spool")
    try:
        source = softiec_log.UdpLogSource("", port)
        source.start()
    except OSError as exc:
        return None, (f"could not bind UDP {port}: {exc}; two suites cannot share the port, "
                      f"so pass --syslog-spool with an external collector")
    return source, f"binding UDP {port} for the log the device sends to {ip}"


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
    parser.add_argument("--log", choices=("off", "on", "both", "toggle"),
                        help="whether the drive logs every operation: off, on, both (off "
                             "then on, the default), or toggle (a lane flips it during the "
                             "phase). --profile defaults this to off.")
    parser.add_argument("--syslog-spool",
                        help="a file an external collector writes, one line per datagram as "
                             f"'{softiec_log.SPOOL_FORMAT}', to read the device log from; "
                             "without it the UDP port the device's syslog setting names is "
                             "bound when it points at this host.")
    parser.add_argument("--log-dump",
                        help="a directory to write each phase's recorded operations and the "
                             "device log lines of its window to, for checking a verdict by hand.")
    args = parser.parse_args()
    phases = plan_phases(args)
    session = Session(args)
    api = session.api
    cmd_if_snapshot = {}
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
            plan = [(policy.name, log_mode, int(duration)) for duration, policy, log_mode in phases]
            detail(f"{api.rest.json('/v1/info').get('firmware_version')}, seed {args.seed}, phases {plan}")
        # The snapshot restores both the Software IEC settings (the log setting included) and,
        # where it exists, the Command Interface setting the UCI steps enable.
        snapshot = {LOG_CATEGORY: api.configs.category(LOG_CATEGORY)}
        if CMD_IF_CATEGORY in api.configs.category_names():
            cmd_if_snapshot = api.configs.category(CMD_IF_CATEGORY)
            snapshot[CMD_IF_CATEGORY] = cmd_if_snapshot
        saved = Snapshot(args.host, snapshot)
        started = created = False
        try:
            with check("start the IEC agent and build the fixture"):
                api.configs.set(LOG_CATEGORY, "Soft Drive Bus ID", 11)
                api.configs.set(LOG_CATEGORY, "IEC Drive", "Enabled")
                session.agent.start()
                started = True
                session.agent.call(OPEN, channel=15)
                session.status()
                session.command("CD//")
                session.root = iec_drive(api)["partitions"][0]["path"]
                original_path = restorable_path(api, original_path, session.root)
                session.fixture()
                created = True
            with check("set up the device log source and the UCI target"):
                source, note = setup_syslog_source(session, api, args)
                session.syslog_source = source
                detail(f"device log: {note}")
                if cmd_if_snapshot:
                    api.configs.set(CMD_IF_CATEGORY, CMD_IF_ITEM, "Enabled")
                    session.uci, uci_note = UciProbe.create(args, session)
                else:
                    session.uci, uci_note = None, "no Command Interface setting on this device"
                detail(f"UCI target: {'available' if session.uci else 'skipped, ' + uci_note}")
            ok = True
            for duration, policy, log_mode in phases:
                ok = run_phase(session, duration, policy, log_mode) and ok
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

            if session.uci is not None:
                session.uci.stop()
            if session.syslog_source is not None:
                session.syslog_source.stop()
            ok = True
            for label, action in (("restore the IEC working directory", restore_directory),
                                  ("restore the Software IEC and Command Interface settings",
                                   lambda: saved.restore(api)),
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
