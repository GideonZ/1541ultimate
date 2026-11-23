#!/usr/bin/env python3
"""Soak for LOAD from the Software IEC drive, driven by a C64 program, after #986.

GideonZ/1541ultimate#986 reports an intermittent ?LOAD ERROR with JiffyDOS on the
Software IEC drive: ST=66 while the error channel still says 00, OK. The JiffyDOS KERNAL
polls the bus at the start of each chunk of a LOAD with no timeout, and a host that comes
back from a stall after the drive's window has closed reads it as an end of file.

The suite runs a small C64 program (jiffy_load_soak.asm, assembled from source here) that
LOADs one file from the Software IEC drive with the KERNAL's own LOAD, --iterations times.
After every load it records the status the KERNAL left (carry and ST), the error channel,
the address the load ended at and a checksum of what arrived, over a load area it filled
with a pattern beforehand, so a load that stops short and says it succeeded is caught. It
reports the totals: clean loads, ST failures with their ST values, loads that succeeded with
the wrong data, loads that ended with an error status, and hangs, which are loads that made
no progress for --load-timeout seconds and are ended by resetting the C64. The host reads
C64 memory only between batches of at most 100 loads, never during one: a memory read
halts the 6510, and one that lands in a serial transfer spoils it.

The KERNAL is the one the device runs, and the suite says which it found. --kernal and
--command-interface work as in the other suites. --jiffydos-rom PATH names the user's own
JiffyDOS C64 KERNAL image, which is licensed: the suite uploads a temporary copy under a
name of its own, runs under it and removes it again, and prints and keeps none of it.

--host-delay CYCLES reproduces the rows of the #986 fix that slow the host on purpose. It
needs --jiffydos-rom, patches the temporary copy so that the LOAD routine's sample of the
bus after the drive releases DATA is CYCLES late (11 plus a multiple of 5: 61, 86 and 101
are the rows of the table), and refuses an image whose LOAD routine is not the one the
patch is written for. Without --jiffydos-rom the delay rows are skipped.

--source picks where the file lives: directory, a directory of the Software IEC drive's
partition 1, or d64, a D64 image in such a directory that the drive enters with CD.
--partition-path serves it from another directory of the device instead, added as a partition
for the run through the UCI SoftIEC target: on the bench the failing rows of the #986 table
need the file on a USB stick, where the Temp RAM disk behind partition 1 passed them. The
suite sets the Software IEC drive on and its bus id to --device, records what it changed, puts
it back, and removes its directory, its partition and its KERNAL copy. See tests/soak/README.md.
"""
import argparse
import contextlib
import sys
import tempfile
import time
import uuid
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import ftp  # noqa: E402
import kernal  # noqa: E402
from api import UltimateApi  # noqa: E402
from assembler import assemble  # noqa: E402
from config_snapshot import Snapshot  # noqa: E402
from report import Failure, check, detail, section, suite_fail, suite_ok, suite_skip, teardown_step  # noqa: E402

sys.path.insert(0, bootstrap.directory("e2e", "io", "iec"))
from iec_agent import CLOSE, OPEN, Agent, iec_drive, restorable_path  # noqa: E402

import jiffy_load  # noqa: E402
from jiffy_load import Expected, Tally  # noqa: E402

SUITE = "jiffy_load_soak_test"
PROGRAM = Path(__file__).with_name("jiffy_load_soak.asm")

SETTINGS = "SoftIEC Drive Settings"
# The partition numbers the run may take for --partition-path, and the UCI SoftIEC target's
# commands to add and remove one (software/io/command_interface/softiec_target.h).
TARGET_SOFTIEC = 0x05
CMD_ADD_PARTITION = 0x20
CMD_DEL_PARTITION = 0x21
PARTITION_NAME = "JIFFY"
SOFTIEC_OK = b"\x00"
FILE_NAME = "JIFFY986"
DISK_NAME = "DISK.D64"

# What the program leaves in C64 memory; see the header of jiffy_load_soak.asm.
PARAMETERS = 0xC000
HEADER = 0xC080
FAILURES = 0xC100
RECORDS = 0xC200
MAGIC = 0x4A
DONE = 0xFF
# The first batch is short, so the real time of a load is measured before a long batch has
# to be waited for blind.
PROBE_LOADS = 2
# Before the first load has been timed, a load is expected to take this long per KiB under
# JiffyDOS and under the stock KERNAL (measured: 0.14 and 1.9 on an Ultimate 64 Elite), and
# a batch is waited for this much longer than the loads themselves take.
GUESS_SECONDS_PER_KIB = {"JiffyDOS": 0.4}
GUESS_OTHER_SECONDS_PER_KIB = 2.4
OVERHEAD_SECONDS = 0.5
OVERHEAD_SECONDS_PER_KIB = 0.05
MARGIN = 1.3
# How often a batch that has outrun its estimate is looked at. Each look halts the C64 for a
# moment, which is why it is not done earlier.
POLL_SECONDS = 10.0
START_SECONDS = 20.0
SHOWN_FAILURES = 12


def uci_session(args):
    """The UCI transport of tests/e2e/io/command_interface/uci_targets_test.py."""
    sys.path.insert(0, bootstrap.directory("e2e", "io", "command_interface"))
    import uci_targets_test as module  # noqa: PLC0415

    uci = module.Uci(module.RestSession(args.host, args.password, args.timeout),
                     module.BUSY_TIMEOUT_SECONDS)
    uci.release()
    return uci


def read_state(api):
    """The program's magic, done flag, loads finished and failure texts stored."""
    return tuple(api.machine.readmem(HEADER, 4))


def status_line(api, offset):
    raw = bytes(api.machine.readmem(HEADER + offset, 32))
    return raw.split(b"\r")[0].split(b"\0")[0].decode("latin-1")


class Soak:
    def __init__(self, args, api, kind, expected, cd, part):
        self.args = args
        self.api = api
        self.kind = kind
        self.expected = expected
        self.cd = cd
        self.part = part
        self.tally = Tally()
        self.loads = []
        self.seconds = []
        self.estimate = None
        self.shown = 0

    def estimate_per_load(self):
        if self.estimate is not None:
            return self.estimate
        guess = GUESS_SECONDS_PER_KIB.get(self.kind, GUESS_OTHER_SECONDS_PER_KIB)
        return (guess + OVERHEAD_SECONDS_PER_KIB) * self.args.size + OVERHEAD_SECONDS

    def batch(self, program, first, count, passes):
        """One batch of loads; returns the loads that finished and whether one hung."""
        api = self.api
        api.machine.reset(force=True)
        api.machine.writemem(PARAMETERS, jiffy_load.parameters(
            count, self.args.device, passes, self.args.size, FILE_NAME, self.part, self.cd))
        api.runners.upload("run_prg", program)
        time.sleep(count * self.estimate_per_load() * MARGIN)
        deadline = time.monotonic() + self.args.load_timeout
        finished = -1
        while True:
            magic, done, loads, _ = read_state(api)
            if magic != MAGIC and time.monotonic() > deadline:
                raise Failure("the C64 program did not start")
            if done == DONE:
                hung = False
                break
            if loads != finished:
                finished, deadline = loads, time.monotonic() + self.args.load_timeout
            elif time.monotonic() > deadline:
                hung = True
                break
            time.sleep(POLL_SECONDS)
        if magic == MAGIC:
            for offset, what in ((0x10, "CP"), (0x30, "CD")):
                answer = status_line(api, offset)
                if not answer.startswith(("00", "02")):
                    raise Failure(f"the {what} command before the loads answered {answer!r}")
        loads = min(loads, count)
        records = bytes(api.machine.readmem(RECORDS, jiffy_load.RECORD_SIZE * loads)) if loads else b""
        slots = bytes(api.machine.readmem(FAILURES, jiffy_load.FAILURE_SLOTS
                                          * jiffy_load.FAILURE_SLOT_SIZE))
        results = jiffy_load.decode_batch(first, loads, records, slots, self.expected)
        if hung:
            detail(f"load {first + loads + 1} made no progress for {self.args.load_timeout:.0f} s "
                   f"(ST ${api.machine.readmem(0x90, 1)[0]:02X} in the C64's zero page); "
                   "the C64 is reset")
        return results, hung

    def run(self, program, passes):
        remaining = self.args.iterations
        while remaining > 0:
            count = min(remaining, jiffy_load.MAX_BATCH,
                        PROBE_LOADS if not self.loads and not self.tally.hangs else remaining)
            first = self.tally.attempted
            results, hung = self.batch(program, first, count, passes)
            for load in results:
                kind = self.tally.add(load, self.expected)
                self.loads.append(load)
                self.seconds.append(load.seconds)
                if kind != "clean":
                    self.shown += 1
                    if self.shown <= SHOWN_FAILURES:
                        detail(load.describe(self.expected))
                    elif self.shown == SHOWN_FAILURES + 1:
                        detail("further loads that were not clean are counted, not listed")
            if hung:
                self.tally.hangs += 1
            remaining -= len(results) + hung
            if results and not hung:
                slowest = max(load.seconds for load in results)
                self.estimate = (slowest + OVERHEAD_SECONDS
                                 + OVERHEAD_SECONDS_PER_KIB * self.args.size)
            elif not results and not hung:
                raise Failure("a batch finished no loads")


def fixture(api, args, root, folder, prg):
    """The folder in the partition's root, and the file in it or in a D64 image there."""
    directory = f"{root.rstrip('/')}/{folder}"
    with ftp.session(args.host, args.password) as client:
        client.mkd(directory)
        if args.source == "d64":
            api.files.create_d64(f"{directory}/{DISK_NAME}", diskname="JIFFY")
            ftp.store(client, f"{directory}/{DISK_NAME}/{FILE_NAME}.PRG", prg)
        else:
            ftp.store(client, f"{directory}/{FILE_NAME}.PRG", prg)
    return directory


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    cli.add_device_arguments(parser)
    parser.add_argument("--iterations", type=int, default=100,
                        help="loads to run (default: 100)")
    parser.add_argument("--device", type=int, default=11,
                        help="bus id the Software IEC drive is given for the run (default: 11)")
    parser.add_argument("--source", choices=("directory", "d64"), default="directory",
                        help="load the file from a directory of the drive's partition 1, or "
                             "from a D64 image in one (default: directory)")
    parser.add_argument("--partition-path", metavar="PATH",
                        help="serve the file from this directory of the device, for instance one "
                             "on a USB stick, as a partition added for the run through the UCI "
                             "SoftIEC target (it enables the Command Interface, and removes "
                             "the partition and its folder afterwards). The default is partition "
                             "1 as it is, which is the Temp RAM disk on some machines")
    parser.add_argument("--size", type=int, default=16,
                        help=f"KiB in the file, 1 to {jiffy_load.MAX_KIB} (default: 16)")
    parser.add_argument("--load-timeout", type=float, default=120.0,
                        help="seconds a load may make no progress before it counts as a hang "
                             "(default: 120; a 32 KiB load under the stock KERNAL takes a minute)")
    parser.add_argument("--host-delay", type=int, default=0, metavar="CYCLES",
                        help="slow the host's poll in the JiffyDOS LOAD routine by this many "
                             "cycles, 11 plus a multiple of 5 (default: 0, none); needs "
                             "--jiffydos-rom")
    parser.add_argument("--jiffydos-rom", metavar="PATH",
                        help="your own JiffyDOS C64 KERNAL image: a temporary copy, patched "
                             "for --host-delay, is run and removed again")
    parser.add_argument("--seed", type=int, default=986,
                        help="seed of the file's content (default: 986)")
    kernal.add_arguments(parser)
    args = parser.parse_args()
    if not 1 <= args.size <= jiffy_load.MAX_KIB:
        parser.error(f"--size is 1 to {jiffy_load.MAX_KIB}")
    if args.iterations < 1:
        parser.error("--iterations is at least 1")
    try:
        passes = jiffy_load.delay_passes(args.host_delay)
    except ValueError as exc:
        parser.error(str(exc))
    if args.jiffydos_rom and args.kernal:
        parser.error("--jiffydos-rom and --kernal both name the KERNAL to run")
    if args.host_delay and not args.jiffydos_rom:
        suite_skip(SUITE, f"--host-delay {args.host_delay} needs --jiffydos-rom: the delay is "
                          "patched into a copy of your own JiffyDOS image, and none is shipped")
        return 0
    rom_directory = None
    if args.jiffydos_rom:
        try:
            image = jiffy_load.patched_kernal(Path(args.jiffydos_rom).read_bytes(), args.host_delay)
        except OSError as exc:
            parser.error(f"--jiffydos-rom: {exc}")
        except jiffy_load.RomRefused as exc:
            parser.error(f"--jiffydos-rom: {exc}")
        rom_directory = tempfile.TemporaryDirectory(prefix="jiffy-soak-")
        copy = Path(rom_directory.name) / f"jiffy_soak_{uuid.uuid4().hex[:6]}.bin"
        copy.write_bytes(image)
        args.kernal = str(copy)
        del image

    api = UltimateApi(args.host, args.password, args.timeout)
    folder = "JIFFY" + uuid.uuid4().hex[:6].upper()
    prg = jiffy_load.payload(args.size, args.seed)
    expected = Expected.of(prg)
    moved = created = False
    added = None
    part = 1
    uci = None
    saved = None
    original_path = root = None
    soak = None
    kernal_run = contextlib.ExitStack()
    try:
        try:
            section("setup")
            with check("the Software IEC drive has a partition 1 and the bus id is free"):
                drives = {name: value for entry in api.rest.json("/v1/drives")["drives"]
                          for name, value in entry.items()}
                if any(d.get("enabled") and d.get("bus_id") == args.device
                       for name, d in drives.items() if name != "IEC Drive"):
                    raise Failure(f"Device {args.device} is already in use")
                partition = next((p for p in drives["IEC Drive"]["partitions"] if p["id"] == 1), None)
                if partition is None:
                    raise Failure("The Software IEC drive has no partition 1")
                original_path = partition["path"]
                kept = {SETTINGS: api.configs.category(SETTINGS)}
                if args.partition_path:
                    kept[kernal.CATEGORY] = {kernal.COMMAND_INTERFACE_ITEM: api.configs.get(
                        kernal.CATEGORY, kernal.COMMAND_INTERFACE_ITEM)}
                saved = Snapshot(args.host, kept)
                taken = {p["id"] for p in drives["IEC Drive"]["partitions"]}
                detail(f"{api.rest.json('/v1/info').get('firmware_version')}; found "
                       f"{', '.join(f'{k}={v!r}' for k, v in saved.settings[SETTINGS].items())}")
            kernal_run.enter_context(kernal.selected(api, args, args.password))
            with check("the KERNAL in use is identified"):
                kind = jiffy_load.kernal_kind(bytes(api.machine.readmem(
                    kernal.KERNAL_ADDRESS, kernal.KERNAL_SIZE)))
                setting = next((api.configs.get(kernal.CATEGORY, name)
                                for name in kernal.KERNAL_ITEMS
                                if name in api.configs.category(kernal.CATEGORY)), "?")
                detail(f"KERNAL: {kind} (setting {setting})")
                if args.host_delay and kind != "JiffyDOS with the host delay patch":
                    raise Failure(f"the host delay is not in the KERNAL the C64 runs: {kind}")
            with check("turn the Software IEC drive on and build the fixture"):
                for item, value in (("Soft Drive Bus ID", args.device), ("IEC Drive", "Enabled")):
                    if saved.settings[SETTINGS].get(item) != value:
                        api.configs.set(SETTINGS, item, value)
                        detail(f"changed {item}: {saved.settings[SETTINGS].get(item)!r} -> {value!r}")
                if args.partition_path:
                    if saved.settings[kernal.CATEGORY][kernal.COMMAND_INTERFACE_ITEM] != "Enabled":
                        api.configs.set(kernal.CATEGORY, kernal.COMMAND_INTERFACE_ITEM, "Enabled")
                        detail(f"changed {kernal.COMMAND_INTERFACE_ITEM}: "
                               f"{saved.settings[kernal.CATEGORY][kernal.COMMAND_INTERFACE_ITEM]!r}"
                               " -> 'Enabled'")
                    part = next(i for i in range(2, 100) if i not in taken)
                    uci = uci_session(args)
                    command = bytes((TARGET_SOFTIEC, CMD_ADD_PARTITION, part)) + (
                        f"{PARTITION_NAME}:{args.partition_path}".encode("ascii"))
                    added = part
                    _, answer = uci.transact(command)
                    if answer != SOFTIEC_OK:
                        raise Failure(f"the UCI SoftIEC target would not add the partition: {answer!r}")
                    root = args.partition_path
                    detail(f"partition {part} added for the run: {root}")
                else:
                    # The drive keeps the directory it was last in; the fixture goes in the
                    # partition's root, which is also where the program's CD starts from.
                    agent = Agent(api, args.device)
                    agent.start()
                    moved = True
                    agent.call(OPEN, channel=15, device=args.device)
                    agent.command(bytes((0x43, 0xD0, 1)), allowed=(0, 2))  # 02 is PARTITION SELECTED
                    agent.command("CD//")
                    root = next(p for p in iec_drive(api)["partitions"] if p["id"] == 1)["path"]
                    original_path = restorable_path(api, original_path, root)
                    agent.call(CLOSE, channel=15, device=args.device)
                created = True
                directory = fixture(api, args, root, folder, prg)
                cd = f"CD//{folder}" + (f"/{DISK_NAME}" if args.source == "d64" else "")
                detail(f"{len(prg) - 2} bytes, end ${expected.end:04X}, in {directory} "
                       f"({args.source}); {args.iterations} loads, host delay {args.host_delay} cycles")
            soak = Soak(args, api, kind, expected, cd, part)
            section("loads")
            program = assemble(PROGRAM)
            with check("the loads"):
                soak.run(program, passes)
                tally = soak.tally
                detail(tally.summary())
                if soak.seconds:
                    ordered = sorted(soak.seconds)
                    detail(f"seconds per load: min {ordered[0]:.1f}, median "
                           f"{ordered[len(ordered) // 2]:.1f}, max {ordered[-1]:.1f}")
                if tally.st_failures and tally.error_codes.get("00") == tally.attempted:
                    detail("ST failures with 00, OK on the error channel are the signature of #986")
                if tally.attempted != args.iterations:
                    raise Failure(f"{tally.attempted} of {args.iterations} loads were accounted for")
                if tally.clean != args.iterations:
                    raise Failure(f"{args.iterations - tally.clean} of {args.iterations} loads "
                                  "were not clean")
        finally:
            def restore_directory():
                if moved and root is not None:
                    agent = Agent(api, args.device)
                    agent.start()
                    agent.call(OPEN, channel=15, device=args.device)
                    agent.command("CD//" + restorable_path(api, original_path, root)[len(root):].upper())
                    agent.call(CLOSE, channel=15, device=args.device)

            def remove_partition():
                if added is not None:
                    uci.transact(bytes((TARGET_SOFTIEC, CMD_DEL_PARTITION, added)))
                    # Back to the partition the drive started on, as far as it is known.
                    agent = Agent(api, args.device)
                    agent.start()
                    agent.call(OPEN, channel=15, device=args.device)
                    agent.command(bytes((0x43, 0xD0, 1)), allowed=(0, 2))
                    agent.call(CLOSE, channel=15, device=args.device)

            def remove_fixture():
                if created and root is not None:
                    with ftp.session(args.host, args.password) as client:
                        ftp.remove_tree(client, f"{root.rstrip('/')}/{folder}")

            def restore_settings():
                restored, refused = saved.restore(api)
                for change in restored:
                    detail(f"restored {change}")
                if refused:
                    raise Failure("not restored: " + "; ".join(f"{c}: {why}" for c, why in refused))

            for label, action in (("remove this run's directory and put the drive's directory back",
                                   lambda: (restore_directory(), remove_fixture(), remove_partition())),
                                  ("restore the Software IEC settings",
                                   lambda: saved is not None and restore_settings()),
                                  ("restore the KERNAL", kernal_run.close),
                                  ("return the C64 to BASIC", lambda: api.machine.reset(force=True))):
                try:
                    teardown_step(label, action)
                except Exception as exc:  # noqa: BLE001
                    # One step that cannot run must not leave the later ones undone.
                    detail(f"teardown: {label}: {exc!r}")
            if rom_directory is not None:
                rom_directory.cleanup()
    except Exception as exc:
        suite_fail(SUITE, str(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
