#!/usr/bin/env python3
# E2E: Command channel, block commands and partition directory over the real IEC bus.

"""Hardware regression for GideonZ/1541ultimate#875, #876 and #877.

What these defects have in common is the bytes a Commodore puts on the bus, which
is why a harness that builds its command strings by hand does not reach them:

  * PRINT# ends every command with a carriage return, and the firmware counted it
    as part of the command. "CD//OS" became a request for a directory whose name
    ends in a carriage return, and answered 71, DIRECTORY ERROR (#875).
  * BASIC prints a space before and after every number, so PRINT#15,"U1:";2;0;18;0
    arrives as "U1: 2  0  18  0 ". The parameter parser could not step over the
    colon, so the colon became the channel number and every value moved one place:
    the track number arrived as the partition number (#876).
  * The partition directory showed the path a partition is rooted at instead of
    its name, and typed every partition DIR instead of NAT or the drive model of
    the image at its root (#877).

Two more checks are here rather than on the host because they cannot fail there.
The device uses the firmware's own sscanf, which has no %c and counts a conversion
it did not make, while a host build links the C library's: a directory filtered by
a time stamp answered 30, SYNTAX ERROR, and the year the time commands reported
came straight from the real time clock, which counts from 1980.

The C64 runs iec_agent.asm and makes the KERNAL calls itself. REST only fills the
agent's mailbox, creates the disk image and reads settings; FTP creates the scratch
directory and fetches the image for comparison.

The suite needs a C64 with a standard KERNAL, REST and FTP, and exactly one
Software IEC partition numbered 1. It temporarily uses device 11, restores the
Software IEC settings and working directory, and deletes only its own fixtures.
"""
import argparse
import io
import re
import sys
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
from iec_agent import STATUS_BYTES, Agent  # noqa: E402
from report import Failure, check, detail, section, suite_fail, suite_ok, teardown_step  # noqa: E402

SUITE = "iec_dos_command_test"

# Track 18 sector 0 of a 1541 disk holds the block availability map. The tracks in
# front of it have 21 sectors each.
BAM_OFFSET = 17 * 21 * 256
DISKNAME = "BLOCKTEST"
# A directory line is 32 bytes when no time stamp was asked for: two link bytes,
# the block count, the quoted and padded name, and a three character type.
LINE = 32


def partition_path(api):
    for entry in api.rest.json("/v1/drives")["drives"]:
        if "IEC Drive" in entry:
            return entry["IEC Drive"]["partitions"][0]["path"]
    raise Failure("The device reports no Software IEC partition")


def parse_directory_line(line):
    """Return the block count, the quoted name and the three character type."""
    blocks = line[2] | (line[3] << 8)
    digits = len(str(blocks))
    quote = 4 + (3 - digits) + 1
    end = line.index(0x22, quote + 1)
    return blocks, bytes(line[quote + 1:end]).decode("latin-1"), bytes(line[27 - digits:30 - digits]).decode("latin-1")


def check_command_channel(agent, api, password, folder, root):
    """#875: a command terminated the way PRINT# terminates it."""
    here = folder.upper()
    with check("CD//<dir> with the carriage return PRINT# appends"):
        agent.command(f"CD//{here}\r")
        now = partition_path(api)
        if now.rstrip("/").casefold() != f"{root.rstrip('/')}/{folder}".casefold():
            raise Failure(f"CD//{here} left the working directory at {now}")
        agent.command(f"CD//{here}/\r")
        agent.command("CD:_\r")
        agent.command(f"CD/{here}\r")

    with check("MD stores the name without the carriage return"):
        agent.command(f"MD//{here}/:MADEDIR\r")
        with ftp.session(api.host, password) as client:
            entries = ftp.names(client, f"{root.rstrip('/')}/{folder}")
        if "MADEDIR" not in [name.upper() for name in entries]:
            raise Failure(f"MD created none of {entries}")

    with check("CD into the created directory, back out, and remove it"):
        agent.command(f"CD//{here}/MADEDIR\r")
        agent.command("CD_\r")
        agent.command(f"RD//{here}/:MADEDIR\r")
        with ftp.session(api.host, password) as client:
            entries = ftp.names(client, f"{root.rstrip('/')}/{folder}")
        if "MADEDIR" in [name.upper() for name in entries]:
            raise Failure(f"RD left {entries}")


def check_block_commands(agent, api, password, folder, image_path):
    """#876: the block commands in the form the 1541 demo disk's VIEW BAM sends."""
    with ftp.session(api.host, password) as client:
        image = ftp.retrieve(client, image_path)
    expected = image[BAM_OFFSET:BAM_OFFSET + 256]
    if expected[:3] != bytes([18, 1, 0x41]):
        raise Failure(f"The created image has no 1541 BAM at 18/0: {expected[:3].hex()}")

    # Entering the image is setup, not what is under test here, so it uses the form
    # that works on firmware that still counts the carriage return.
    agent.command(f"CD//{folder.upper()}/{Path(image_path).name.upper()}")
    agent.call(1, channel=2, data=b"#")
    agent.status()
    try:
        with check('U1 in the form PRINT#15,"U1:";2;0;18;0 sends'):
            agent.command("U1: 2  0  18  0 \r")
            block = agent.read_exact(128, channel=2) + agent.read_exact(128, channel=2)
            if block != expected:
                first = next(i for i, (a, b) in enumerate(zip(block, expected)) if a != b)
                raise Failure(f"The block read differs from the image at byte {first}")

        with check('U1 in the form the 9/84 VIEW BAM sends, "U1:2,0,18,0"'):
            agent.command("U1:2,0,18,0\r")
            if agent.read_exact(128, channel=2) != expected[:128]:
                raise Failure("The comma separated form read a different sector")

        with check('B-P in the form PRINT#15,"B-P:";2;144 sends'):
            agent.command("B-P: 2  144 \r")
            if agent.read_exact(16, channel=2) != expected[144:160]:
                raise Failure("The buffer pointer did not move to offset 144")
    finally:
        agent.call(4, channel=2)
    agent.command("CD_")


def check_timestamp_filter(agent, api, password, folder, root):
    """The clock the drive reports, and a directory filtered by a time stamp.

    Both are device-only: a host build links the C library's sscanf, while the
    firmware brings its own, which has no %c and counts a conversion it did not make.
    The filter therefore parsed nothing and answered 30, SYNTAX ERROR. The year came
    from the real time clock, which counts from 1980, and reached the answer without
    being turned into a calendar year first.
    """
    here = folder.upper()
    with ftp.session(api.host, password) as client:
        client.storbinary(f"STOR {root.rstrip('/')}/{folder}/stamped.prg", io.BytesIO(b"stamped"))

    listing = listing_of(agent, f"$//{here}:*=L")
    stamped = re.search(rb'"STAMPED\s*"\s+PRG\s+(\d\d)/(\d\d)/(\d\d) (\d\d)\.(\d\d) ([AP])M', listing)
    if not stamped:
        raise Failure(f"the long listing carries no stamp for the file just written: {listing!r}")
    month, day, year, hour12, minute, half = (f.decode() for f in stamped.groups())
    detail(f"the file this run wrote is stamped {month}/{day}/{year} {hour12}.{minute} {half}M")

    with check("the clock the drive reports agrees with the stamp it writes"):
        agent.call(2, 15, b"T-RA")
        now = agent.call(3, 15, expect=STATUS_BYTES).decode("ascii").strip()
        reported = re.search(r"(\d\d)/(\d\d)/(\d\d) ", now)
        if not reported:
            raise Failure(f"T-RA answered {now!r}")
        if reported.group(3) != year:
            raise Failure(f"T-RA reports year {reported.group(3)} for a file stamped {year}: {now!r}")
        if (reported.group(1), reported.group(2)) != (month, day):
            raise Failure(f"T-RA reports {reported.group(0).strip()} for a file stamped {month}/{day}/{year}")

    with check("a time stamp filter tells morning from afternoon"):
        # A threshold the file falls on the late side of, and the same threshold with
        # the other half-day marker, which it falls on the early side of.
        # One minute before midnight is the one time of day where the late threshold
        # would land on the file itself, so a minute earlier is used there.
        clock = "12:00" if half == "A" else ("11:58" if (hour12, minute) == ("11", "59") else "11:59")
        date = f"{month}/{day}/{year}"
        if b"STAMPED" not in listing_of(agent, f"$//{here}:*=>{date} {clock} AM"):
            raise Failure(f"the file is not reported newer than {date} {clock} AM")
        if b"STAMPED" in listing_of(agent, f"$//{here}:*=>{date} {clock} PM"):
            raise Failure(f"the file is reported newer than {date} {clock} PM")


def listing_of(agent, name):
    """The whole of one directory stream, read in mailbox sized pieces."""
    agent.call(1, channel=3, data=name.encode("ascii"), secondary=0)
    try:
        agent.status()
        return agent.read_stream(channel=3)
    finally:
        # The logical file has to be closed even when the open was refused, or the
        # KERNAL answers "file open" to the next suite that wants the same number.
        agent.call(4, channel=3)


def check_partition_directory(agent, api):
    """#877: what the partition directory says about a partition."""
    with check("$=P names the partition and types it NAT"):
        listing = listing_of(agent, "$=P")
        if len(listing) < 2 * LINE:
            raise Failure(f"The partition directory returned {len(listing)} bytes")
        blocks, name, kind = parse_directory_line(listing[LINE:2 * LINE])
        detail(f"partition {blocks} listed as {name!r} type {kind!r}")
        root = partition_path(api)
        if blocks != 1:
            raise Failure(f"The first partition line reports partition {blocks}")
        if name.startswith("/") or name.casefold() == root.casefold():
            raise Failure(f"The partition is listed by its path {name!r}, not by its name")
        if kind != "NAT":
            raise Failure(f"A partition rooted at a directory is typed {kind!r}, expected 'NAT'")


def run(args):
    api = UltimateApi(args.host, args.password, args.timeout)
    agent = Agent(api)
    detail(api.rest.json("/v1/info").get("firmware_version", "unknown firmware"))

    drives = {name: value for entry in api.rest.json("/v1/drives")["drives"] for name, value in entry.items()}
    if any(d.get("enabled") and d.get("bus_id") == 11
           for name, d in drives.items() if name not in ("a", "IEC Drive")):
        raise Failure("Device 11 is already in use")
    partitions = drives["IEC Drive"]["partitions"]
    if len(partitions) != 1 or partitions[0]["id"] != 1:
        raise Failure("This test requires one Software IEC partition, numbered 1")
    original_path = partitions[0]["path"]
    saved = Snapshot(args.host, {"SoftIEC Drive Settings": api.configs.category("SoftIEC Drive Settings")})

    folder = "iec" + uuid.uuid4().hex[:8]
    root = None
    started = False
    created = False
    try:
        api.configs.set("SoftIEC Drive Settings", "Soft Drive Bus ID", 11)
        api.configs.set("SoftIEC Drive Settings", "IEC Drive", "Enabled")
        agent.start()
        started = True
        agent.call(1, channel=15)
        agent.status((0, 73))
        agent.command("CD//")
        root = partition_path(api)
        if not original_path.casefold().startswith(root.casefold()):
            raise Failure("Cannot restore the Software IEC path relative to its partition root")

        directory = f"{root.rstrip('/')}/{folder}"
        with ftp.session(args.host, args.password) as client:
            client.mkd(directory)
        created = True
        image = f"{directory}/blocks.d64"
        api.files.create_d64(image, diskname=DISKNAME)

        # Each area is reported on its own, so one broken area does not hide the rest.
        # The reset between them uses the command form that works either way.
        failed = []
        for label, action in (
                ("command channel", lambda: check_command_channel(agent, api, args.password, folder, root)),
                ("block commands", lambda: check_block_commands(agent, api, args.password, folder, image)),
                ("time stamp filter", lambda: check_timestamp_filter(agent, api, args.password, folder, root)),
                ("partition directory", lambda: check_partition_directory(agent, api))):
            section(label)
            try:
                agent.command("CD//")
                action()
            except Failure as exc:
                failed.append(f"{label}: {exc}")
        if agent.overruns:
            detail(f"{agent.overruns} transactions needed longer than the estimated transfer time")
        if failed:
            raise Failure("; ".join(failed))
    finally:
        def restore_directory():
            if started:
                agent.call(4, channel=15)
                agent.call(1, channel=15)
                agent.command("CD//" + original_path[len(root or ""):].upper())
                agent.call(4, channel=15)

        def restore_settings():
            _, refused = saved.restore(api)
            if refused:
                raise Failure(f"Settings restoration refused: {refused}")
            after = partition_path(api)
            if after.casefold() != original_path.casefold():
                raise Failure(f"The Software IEC path was not restored: {after}")

        def remove_fixtures():
            if not created:
                return
            with ftp.session(args.host, args.password) as client:
                ftp.remove_tree(client, f"{root.rstrip('/')}/{folder}")

        ok = True
        for label, action in (("restore the IEC working directory", restore_directory),
                              ("restore the Software IEC settings", restore_settings),
                              ("remove only this run's fixtures", remove_fixtures),
                              ("return the C64 to BASIC", lambda: api.machine.reset(force=True))):
            ok = teardown_step(label, action) and ok
        if not ok:
            raise Failure("Hardware test cleanup incomplete")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser)
    args = parser.parse_args()
    try:
        run(args)
    except Exception as exc:
        traceback.print_exc()
        suite_fail(SUITE, str(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
