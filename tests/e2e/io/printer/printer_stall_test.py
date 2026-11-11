#!/usr/bin/env python3
# E2E: A long print job must not make the IEC bus or the REST server wait for the printer.

"""Regression test for upstream issue #1052: printing blocks the IEC bus and REST.

The virtual printer used to hand its bytes to the printer task through a queue
one entry deep, so once a page break started an encode the C64 sat in CHROUT
until the encode finished, and anything else on the bus (a SoftIEC drive) or
behind the one REST task waited with it. The fix spools the bytes to the
printer task instead. This suite proves that on a device, with a job long
enough that no fixed queue can hide the effect:

* 240 lines of Commodore MPS text, page height 60, no form feed: page breaks at
  the 60th, 120th, 180th and 240th carriage return, so four pages. The bytes
  still to come behind the first three breaks are 13320, 8880 and 4440, all
  above any queue depth the firmware has had (asserted before the device is
  touched). The last break is the final byte, so an encode is still running at
  CLOSE, which is what makes the after-CLOSE probe mean something.
* It runs once with PNG B&W and once with PNG COLOR output (the colour encode is
  much slower, which makes the backlog worse).
* printer_stall_e2e.prg prints that job and, every 5 carriage returns, reads
  the status channel of a SoftIEC drive (OPEN 15,11,15 + read to CR + CLOSE),
  stamping the CIA 1 time of day clock around each read; it reads once more
  right after the printer's CLOSE. 48 probes land between pages, several on each
  page break.
* The host times GET /v1/drives at 5 Hz for the whole job.

A run passes when, for every leg:

1. the status read right after CLOSE took at most 0.5 s;
2. every one of the 48 status reads while printing took at most 0.5 s (the
   worst one counts, not the median);
3. the longest GET /v1/drives took at most 0.5 s (an error or a timeout fails);
4. the status text came back every time, the KERNAL status after OPEN + CHKOUT
   and after the last byte was 0, and the job finished (a hang is a failure);
5. exactly four pages were written, each a well-formed PNG, with the sha256 of
   the pages the firmware wrote before the change, with the same job and no
   probing, so the fix changes timing and not output.

`--control-drive BUS` repeats each leg against a second drive that does not go
through the printer path (a drive on that bus the machine already has, such as
an emulated 1541 on bus 8). It must meet 1 and 2 as well: if it does not, the
bench is at fault and the run reports that instead of judging the firmware.
Without the flag there is no control.

On firmware without the fix this suite fails 1 to 3 on the first break (8 to 9 s
measured on an Ultimate 64 Elite, longer in colour, where the C64 may not finish
at all). It is bounded: a job that is not done after --job-timeout seconds is a
failure that names the worst probe seen.

Settings are captured and set through REST, RAM only, and restored on exit; the
pages are written under a directory of this run's own and removed again.

Takes 5 to 10 minutes. `--bw-only` runs the B&W legs alone for a quick look; a
run that did not exercise colour does not count as the gate.
`--print-hashes` prints the page hashes of the run, to refresh REFERENCE_SHA256
when the renderer changes on purpose.
"""

import argparse
import hashlib
import http.client
import os
import sys
import threading
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402

sys.path.insert(0, bootstrap.directory("e2e", "io", "printer"))
import ftp as ftp_lib  # noqa: E402  (needs tests/lib on sys.path first)
import png_lite  # noqa: E402  (local module, needs the directory on sys.path first)
import printer_test  # noqa: E402  (local module: the client and the FTP helpers)
import targets  # noqa: E402  (needs tests/lib on sys.path first)
from api import UltimateApi  # noqa: E402  (needs tests/lib on sys.path first)
from report import (  # noqa: E402  (needs tests/lib on sys.path first)
    Failure, check_fail, check_ok, check_start, detail, section, suite_fail, suite_ok,
    suite_skip)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PRG_PATH = os.path.join(SCRIPT_DIR, "printer_stall_e2e.prg")

# Where printer_stall_e2e.asm keeps its input and its results.
TEXT_ADDR = 0x2000
FLAG_ADDR, COUNT_ADDR, ST_OPEN_ADDR, ST_END_ADDR, STAMPS_ADDR = 0xCF00, 0xCF01, 0xCF02, 0xCF03, 0xCF07
DEVICE_ADDR = 0xCF10
TM_ADDR, TS_ADDR, TT_ADDR = 0xCA00, 0xCB00, 0xCC00
BUF_ADDR, LINE_ADDR, LEN_ADDR = 0xCD00, 0xCE00, 0xCE80

LINES = 240
PROBE_EVERY = 5
PAGE_HEIGHT = 60
PAGES = 4
SOFTIEC_BUS = 11
# The deepest queue the printer has had, in entries: every break must leave more bytes than that behind it.
QUEUE_DEPTH = 2048

SOFTIEC_LIMIT_SECONDS = 0.5
REST_LIMIT_SECONDS = 0.5
# The CIA clock and the host clock must agree this closely, or the stamps measure nothing.
TOD_SCALE_RANGE = (0.75, 1.25)
POLL_SECONDS = 1.0
WRITE_CHUNK = 4096
FLUSH_SETTLE_SECONDS = 5.0
REST_PROBE_HZ = 5.0
PROBE_TIMEOUT_SECONDS = 60.0
PAGE_WAIT_SECONDS = {"bw": 150.0, "color": 600.0}

# sha256 of the four pages of this job, from firmware before the spool change
# (584250385), same job and settings, no probing, Ultimate 64 Elite and C64
# Ultimate alike. Refresh with --print-hashes if the renderer changes on purpose.
REFERENCE_SHA256 = {
    "bw": (
        "de286ef514a9fb67d94431f8a4a4c830e400e891d13ae4723ec440d17820975a",
        "714a30707334a0bb2e5b6d40b9a1fd2670191539533e6f809e9e31f3679674b9",
        "2de1a50cba470c0c5fa299eb88ee0872f83476ed6221854ff69330bb3ffcb43d",
        "645d279a72b1288fc40e9dac30d0c03bb90df87a71ef4906566f35ff857e0278",
    ),
    "color": (
        "3af92dad270863baa131b21a6cef1c96719348d7e9c72d11a58ea6f1182051a1",
        "20f270ce81a4cc2c2d8324ff57d00ac2f771cb11e51041f9ddac7ffa661e39f7",
        "1852204f3518891516cb8a6bb479832346b794b00a69a868eb64a476e5c1f49d",
        "70aa356bc6552c36964993ec104c6c49c84902b6159c1ee6b24c3dbaa0ea8fb9",
    ),
}
OUTPUT_TYPES = {"bw": "PNG B&W", "color": "PNG COLOR"}

PRINTER = "Printer Settings"
SOFTIEC = "SoftIEC Drive Settings"


def job_text():
    """The print job as PETSCII bytes, ended by the $00 the C64 program stops at."""
    body = [f"LINE {n:03d} ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            for n in range(LINES)]
    return ("\r".join(body) + "\r").encode("ascii") + b"\x00"


def bytes_behind_breaks(text):
    """How many bytes follow the carriage return that ends each page but the last."""
    per_line = text.index(b"\r") + 1
    return [(LINES - n) * per_line for n in range(PAGE_HEIGHT, LINES, PAGE_HEIGHT)]


def bcd(value):
    return (value >> 4) * 10 + (value & 15)


def stamps_in_tenths(minutes, seconds, tenths, count):
    return [bcd(minutes[i]) * 600 + bcd(seconds[i]) * 10 + (tenths[i] & 15) for i in range(count)]


class RestProbe(threading.Thread):
    """GET /v1/drives at 5 Hz, each call timed. A timeout is a result, not a crash."""

    def __init__(self, host, password):
        super().__init__(daemon=True)
        self.api = UltimateApi(host, password, PROBE_TIMEOUT_SECONDS)
        self.stopping = threading.Event()
        self.timings = []
        self.errors = 0

    def run(self):
        while not self.stopping.is_set():
            began = time.monotonic()
            try:
                self.api.drives.list()
            except (Failure, OSError, http.client.HTTPException):
                self.errors += 1
            took = time.monotonic() - began
            self.timings.append(took)
            self.stopping.wait(max(0.0, 1.0 / REST_PROBE_HZ - took))

    def finish(self):
        self.stopping.set()
        self.join(PROBE_TIMEOUT_SECONDS + 10)
        return max(self.timings, default=None), self.errors


def read_retrying(client, address, length, attempts=8):
    """readmem that outlasts a REST task still blocked by an encode, which is the effect under test."""
    for attempt in range(attempts):
        try:
            return client.readmem(address, length)
        except (Failure, OSError, http.client.HTTPException):
            if attempt == attempts - 1:
                raise
            time.sleep(2.0)
    return b""


class Leg:
    """One job: which output type, which drive is probed, and what came out of it."""

    def __init__(self, kind, bus, role):
        self.kind, self.bus, self.role = kind, bus, role
        self.label = f"{kind} {role} (bus {bus})"
        self.finished = False
        self.kernal_status = None
        self.after_close = None
        self.between = []
        self.between_lines = []
        self.text_lengths = []
        self.status_text = ""
        self.rest_max = None
        self.rest_errors = 0
        self.tod_scale = None
        self.pages = []


def worst_probe(leg):
    if not leg.between:
        return "no probe completed"
    worst = max(range(len(leg.between)), key=leg.between.__getitem__)
    return f"worst {leg.between[worst]:.1f} s at carriage return {leg.between_lines[worst]}"


def run_job(client, text, leg, args):
    """Run the C64 program once and fill `leg` from what it recorded."""
    client.close_menu_from_anywhere()
    client.api.machine.reset(force=True)
    client.writemem(FLAG_ADDR, bytes(16))
    # The text sits below $8000: running a PRG on the device changes memory from $8005 up, which cut a
    # copy at $4000 short.
    for offset in range(0, len(text), WRITE_CHUNK):
        client.writemem(TEXT_ADDR + offset, text[offset:offset + WRITE_CHUNK])
    client.writemem(DEVICE_ADDR, bytes([leg.bus]))
    prg = printer_test.load_prg(PRG_PATH)
    began = time.monotonic()
    client.run_prg(prg)
    # Started once the PRG is loaded: run_prg itself holds the REST task for a moment, and that is not
    # the printer's doing.
    probe = RestProbe(args.host, args.password)
    probe.start()
    wall = None
    while time.monotonic() - began < args.job_timeout:
        try:
            if client.readmem(FLAG_ADDR, 1)[0] == 1:
                wall = time.monotonic() - began
                break
        except (Failure, OSError, http.client.HTTPException):
            pass
        time.sleep(POLL_SECONDS)
    leg.rest_max, leg.rest_errors = probe.finish()
    if wall is None:
        return
    leg.finished = True
    count = read_retrying(client, COUNT_ADDR, 1)[0]
    total = read_retrying(client, STAMPS_ADDR, 1)[0]
    leg.kernal_status = (read_retrying(client, ST_OPEN_ADDR, 1)[0],
                         read_retrying(client, ST_END_ADDR, 1)[0])
    tables = [read_retrying(client, addr, total) for addr in (TM_ADDR, TS_ADDR, TT_ADDR)]
    tenths = stamps_in_tenths(*tables, total)
    leg.between = [(tenths[2 + 2 * i] - tenths[1 + 2 * i]) / 10.0 for i in range(count)]
    leg.between_lines = list(read_retrying(client, LINE_ADDR, count + 1))
    leg.after_close = (tenths[-1] - tenths[-2]) / 10.0
    leg.text_lengths = list(read_retrying(client, LEN_ADDR, count + 1))
    length = leg.text_lengths[-1]
    leg.status_text = bytes(read_retrying(client, BUF_ADDR, max(length, 1))).decode("latin-1")
    clock = (tenths[-1] - tenths[0]) / 10.0
    leg.tod_scale = wall / clock if clock > 0 else None


def ftp_names(args, directory):
    with ftp_lib.session(args.host, args.ftp_password, 15, user=args.ftp_user) as ftp:
        return sorted(ftp_lib.names(ftp, directory))


def collect_pages(args, directory, base_name, kind):
    """Wait for the page files, then a little longer for an extra one, and download them."""
    deadline = time.monotonic() + PAGE_WAIT_SECONDS[kind]
    while time.monotonic() < deadline:
        if len([n for n in ftp_names(args, directory) if n.startswith(base_name)]) >= PAGES:
            break
        time.sleep(2.0)
    time.sleep(3.0)
    found = [n for n in ftp_names(args, directory) if n.startswith(base_name)]
    inspector = printer_test.FtpInspector(args.host, args.ftp_user, args.ftp_password)
    return [(name, inspector.download(f"{directory}/{name}")) for name in found]


def judge_leg(leg, args, pages):
    """Report every threshold of one leg. Returns the failures, as sentences."""
    failures = []

    def verdict(label, ok, got, limit):
        check_start(f"{leg.label}: {label}")
        if ok:
            check_ok(f"{got}{limit}")
        else:
            check_fail(f"{got}{limit}")
            failures.append(f"{leg.label}: {label}: {got}{limit}")

    verdict("job finished", leg.finished, "yes" if leg.finished else f"not done after {args.job_timeout:.0f} s", "")
    if leg.finished:
        verdict("KERNAL status after OPEN/CHKOUT and after the last byte",
                leg.kernal_status == (0, 0), leg.kernal_status, " (want (0, 0))")
        verdict("status text read back every time",
                bool(leg.text_lengths) and min(leg.text_lengths) > 0,
                f"shortest {min(leg.text_lengths or [0])} bytes, last {leg.status_text.strip()!r}", "")
        verdict("drive status read after the printer CLOSE",
                leg.after_close <= SOFTIEC_LIMIT_SECONDS, f"{leg.after_close:.1f} s",
                f" (limit {SOFTIEC_LIMIT_SECONDS} s)")
        verdict(f"drive status reads between pages ({len(leg.between)} probes)",
                bool(leg.between) and max(leg.between) <= SOFTIEC_LIMIT_SECONDS,
                worst_probe(leg), f" (limit {SOFTIEC_LIMIT_SECONDS} s)")
        low, high = TOD_SCALE_RANGE
        verdict("C64 clock agrees with the host clock",
                leg.tod_scale is not None and low <= leg.tod_scale <= high,
                "unreadable" if leg.tod_scale is None else f"{leg.tod_scale:.2f}", f" (want {low}..{high})")
    else:
        detail(f"{worst_probe(leg)} before the job was given up on")
    rest = "no answer" if leg.rest_max is None else f"{leg.rest_max:.3f} s, {leg.rest_errors} errors"
    verdict("longest GET /v1/drives during the job",
            leg.rest_max is not None and leg.rest_max <= REST_LIMIT_SECONDS and leg.rest_errors == 0,
            rest, f" (limit {REST_LIMIT_SECONDS} s)")
    if pages is not None:
        judge_pages(leg, args, pages, verdict)
    return failures


def judge_pages(leg, args, pages, verdict):
    hashes = [hashlib.sha256(data).hexdigest() for _name, data in pages]
    if args.print_hashes:
        detail(f"{leg.kind} page hashes: {hashes}")
    verdict("pages written", len(pages) == PAGES, len(pages), f" (want {PAGES})")
    bad = []
    for name, data in pages:
        ok, reason = png_lite.png_is_well_formed(data)
        if not ok or png_lite.decode_png_dimensions(data) is None:
            bad.append(f"{name}: {reason}")
    verdict("pages are well-formed PNGs", not bad, "yes" if not bad else "; ".join(bad), "")
    if not args.no_reference:
        same = tuple(hashes) == REFERENCE_SHA256[leg.kind]
        verdict("pages byte-identical to the reference", same,
                "yes" if same else f"hashes {hashes}", "")


def capture(client, wanted):
    values = {}
    for category, item in wanted:
        values[(category, item)] = client.get_config(category, item)
    return values


def main():
    parser = argparse.ArgumentParser(
        description="Printing must not make the IEC bus or REST wait (issue #1052).",
        epilog="Settings are set over REST (RAM only) and restored on exit.")
    cli.add_device_arguments(parser, colour=False, timeout=None)
    parser.add_argument("--control-drive", type=int, default=None, metavar="BUS",
                        help="Also probe a second drive on this bus, one that does not use the "
                             "printer path. It must meet the same limits, or the bench is at fault.")
    parser.add_argument("--bw-only", action="store_true",
                        help="Run the B&W legs only (quick; not the gate)")
    parser.add_argument("--job-timeout", type=float, default=420.0,
                        help="Seconds a job may take before it counts as hung (default: 420)")
    parser.add_argument("--no-reference", action="store_true",
                        help="Do not compare the pages with the reference hashes (page count and "
                             "PNG structure are still checked)")
    parser.add_argument("--print-hashes", action="store_true",
                        help="Print the sha256 of every page written, to refresh REFERENCE_SHA256")
    parser.add_argument("--ftp-user", default=printer_test.FTP_USER_DEFAULT)
    parser.add_argument("--ftp-password", default=printer_test.FTP_DEFAULT_PASSWORD)
    args = parser.parse_args()

    text = job_text()
    behind = bytes_behind_breaks(text)
    if min(behind) <= QUEUE_DEPTH:
        suite_fail("printer_stall_test", f"the job leaves {min(behind)} bytes behind a page break, "
                   f"not more than the {QUEUE_DEPTH}-entry queue: it proves nothing")
        return 1

    if targets.is_cartridge(args.host):
        suite_skip("printer_stall_test", "a cartridge's printer is not the one under test here; "
                   "see the skip in printer_test.py")
        return 0

    client = printer_test.U64Client(args.host, args.password, timeout=30)
    check_start(f"REST reachable at {args.host}")
    if not client.is_alive():
        check_fail("no response from /v1/version")
        return 1
    check_ok()

    root = printer_test.FtpInspector(args.host, args.ftp_user, args.ftp_password).printer_root()
    directory = f"{root}/printer/s{int(time.time()) % 0xFFFF:04x}"
    inspector = printer_test.FtpInspector(args.host, args.ftp_user, args.ftp_password)
    inspector.ensure_directory(directory)

    settings = [(PRINTER, item) for item in (
        "IEC printer", "Bus ID", "Emulation", "Output file", "Output type", "Ink density",
        "Commodore charset", "Page top margin (default is 5)", "Page height (default is 60)")]
    settings += [(SOFTIEC, "IEC Drive"), (SOFTIEC, "Soft Drive Bus ID")]
    check_start("capture original settings")
    snapshot = capture(client, settings)
    check_ok()

    kinds = ["bw"] if args.bw_only else ["bw", "color"]
    failures = []
    unresponsive = False
    try:
        for kind in kinds:
            plan = [Leg(kind, SOFTIEC_BUS, "SoftIEC")]
            if args.control_drive is not None:
                plan.insert(0, Leg(kind, args.control_drive, "control"))
            for leg in plan:
                section(leg.label)
                base_name = f"{kind[:2]}{leg.role[0].lower()}"
                base = f"{directory}/{base_name}"
                printer_test.require_output_file_length(base)
                # A page the printer still holds from an earlier job (a C64 reset does not drop it) would
                # shift every page break of this one. Closing the output by changing its type writes it
                # out, to a file of its own that this run ignores.
                client.set_config(PRINTER, "IEC printer", "Enabled")
                client.set_config(PRINTER, "Output file", f"{directory}/old")
                client.set_config(PRINTER, "Output type", "RAW")
                client.set_config(PRINTER, "Output type", OUTPUT_TYPES[kind])
                time.sleep(FLUSH_SETTLE_SECONDS)
                for item, value in (("IEC printer", "Enabled"), ("Bus ID", "4"),
                                    ("Emulation", "Commodore MPS"), ("Output file", base),
                                    ("Output type", OUTPUT_TYPES[kind]), ("Ink density", "Medium"),
                                    ("Commodore charset", "USA/UK"),
                                    ("Page top margin (default is 5)", "5"),
                                    ("Page height (default is 60)", str(PAGE_HEIGHT))):
                    client.set_config(PRINTER, item, value)
                client.set_config(SOFTIEC, "IEC Drive", "Enabled")
                client.set_config(SOFTIEC, "Soft Drive Bus ID", str(SOFTIEC_BUS))
                check_start(f"{leg.label}: run the job, bounded at {args.job_timeout:.0f} s")
                run_job(client, text, leg, args)
                check_ok("done" if leg.finished else "given up on")
                pages = None
                if not client.is_alive(timeout=10.0):
                    unresponsive = True
                elif leg.finished:
                    pages = collect_pages(args, directory, base_name, kind)
                leg_failures = judge_leg(leg, args, pages if not unresponsive else None)
                failures.extend(leg_failures)
                if unresponsive:
                    failures.append(f"{leg.label}: the device stopped answering: power-cycle it")
                    detail("REST is unreachable after the job. Recover by redeploying the firmware "
                           "over JTAG, or by power-cycling the device.")
                    return finish(failures)
                if leg.role == "control" and any(
                        "drive status read" in f or "drive status reads" in f for f in leg_failures):
                    failures.append("the control drive itself was slow: the bench is at fault, "
                                    "this run says nothing about the firmware")
                    return finish(failures)
    finally:
        if not unresponsive:
            section("restoring original settings")
            for (category, item), value in snapshot.items():
                try:
                    client.set_config(category, item, value)
                    detail(f"{category} / {item}: {value}")
                except Failure as exc:
                    failures.append(f"could not restore {category} / {item}: {exc}")
            try:
                with ftp_lib.session(args.host, args.ftp_password, 15, user=args.ftp_user) as ftp:
                    if not ftp_lib.remove_tree(ftp, directory):
                        detail(f"left {directory} behind: the printer still holds a file open")
            except Failure as exc:
                detail(f"could not clean up {directory}: {exc}")
    return finish(failures)


def finish(failures):
    section("summary")
    if failures:
        suite_fail("printer_stall_test", "; ".join(failures))
        return 1
    suite_ok("printer_stall_test", "no drive or REST probe waited more than "
             f"{SOFTIEC_LIMIT_SECONDS} s and every page matched")
    return 0


if __name__ == "__main__":
    sys.exit(main())
