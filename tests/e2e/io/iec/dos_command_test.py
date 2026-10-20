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

  * The partition commands that carry their number as a byte rather than as text
    were reported against the first version of this fix. CMD DOS spells Change
    Partition either "CPn" or "C" followed by a shifted P and the number, and that
    number can be 13, which is the same byte PRINT# appends as a terminator.

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
import datetime
import ftplib
import io
import random
import re
import sys
import time
import traceback
import uuid
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import ftp  # noqa: E402
import kernal  # noqa: E402
from api import UltimateApi  # noqa: E402
from config_snapshot import Snapshot  # noqa: E402
from iec_agent import CLOSE, MAILBOX_CAPACITY, OPEN, READ_COUNT, STATUS_BYTES, WRITE, Agent, Talker, cmd_swap, iec_drive, restorable_path  # noqa: E402
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


def switch_off_drives_at(api, devices):
    """Switch off the emulated drives that answer on one of `devices`, and name them.

    SI-100 and SI-101 move the Software IEC drive to 12 and 9, which a bench
    machine's drive b can hold. They go back on only in the teardown: a drive
    switching on resets, and the bus is not usable while it does.
    """
    busy = [slot for slot, drive in api.drives.list().items()
            if slot in ("a", "b") and drive.enabled and drive.bus_id in devices]
    for slot in busy:
        detail(f"drive {slot} is switched off for the run, it answers on a device the checks use")
        api.drives.off(slot)
    return busy


def restore_bus_number(api, agent):
    """Put the Software IEC drive back on the device its settings hold.

    A check that fails after U0> or S-9 leaves the drive on the number it moved
    to, and a settings write of the same number changes nothing (SI-103b), so
    the number is written through another value.
    """
    category = "SoftIEC Drive Settings"
    configured = api.configs.get(category, "Soft Drive Bus ID")
    live = iec_drive(api)["bus_id"]
    if live != configured:
        detail(f"the drive was left on device {live}; moving it back to {configured}")
        try:
            # The command channel is open on the number the drive is leaving.
            agent.call(CLOSE, channel=15, device=live)
        except Failure:
            pass
        api.configs.set(category, "Soft Drive Bus ID", 30 if configured != 30 else 29)
        api.configs.set(category, "Soft Drive Bus ID", configured)
        live = iec_drive(api)["bus_id"]
        if live != configured:
            raise Failure(f"the drive stayed on device {live}")
        agent.softiec_device = configured
        agent.call(OPEN, channel=15, device=configured)


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
        # RD takes no path; the working directory is the parent again after CD_.
        agent.command("RD:MADEDIR\r")
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

        with check("P with four position bytes past a buffer's end answers 30 (SI-090, SI-092)"):
            # The top byte makes the position negative as a signed number. Nothing is read
            # after an OK, because the pointer would then be outside the buffer.
            response = agent.command(b"P" + bytes([0x62, 0xFF, 0xFF, 0xFF, 0xFF]), allowed=range(100))
            detail(f"P to $FFFFFFFF answered {response!r}")
            if not response.startswith("30,"):
                raise Failure(f"P to $FFFFFFFF answered {response!r}")
            agent.command("B-P: 2  160 \r")
            if agent.read_exact(16, channel=2) != expected[160:176]:
                raise Failure("The channel did not read on at offset 160")
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
    # The long format puts three spaces between the date and the time (SI-139).
    stamped = re.search(rb'"STAMPED\s*"\s+PRG\s+(\d\d)/(\d\d)/(\d\d)\s+(\d\d)\.(\d\d) ([AP])M', listing)
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


def stamped_lines(listing, length, what):
    """The entry lines of a time stamped listing, which all have the same length."""
    body = listing[LINE:len(listing) - LINE]
    if not body or len(body) % length:
        raise Failure(f"{what} has {len(listing)} bytes, which is not a header, "
                      f"a blocks free line and whole lines of {length} bytes")
    return [body[i:i + length] for i in range(0, len(body), length)]


def check_stamped_line(line, what, type_name, gap, stamp_len):
    """SI-139: where the stamp sits in a line, and the filler behind it."""
    digits = len(str(line[2] | (line[3] << 8)))
    type_at = 27 - digits
    stamp_at = type_at + len(type_name) + gap
    if line[type_at:type_at + len(type_name)] != type_name:
        raise Failure(f"{what}: the type is not {type_name!r} at offset {type_at}: {line!r}")
    if line[type_at + len(type_name):stamp_at] != b" " * gap:
        raise Failure(f"{what}: the {gap} characters between the type and the stamp are "
                      f"{line[type_at + len(type_name):stamp_at]!r}: {line!r}")
    if not re.match(rb"\d\d/\d\d", line[stamp_at:stamp_at + 5]):
        raise Failure(f"{what}: no stamp at offset {stamp_at}: {line!r}")
    filler = line[stamp_at + stamp_len:len(line) - 1]
    if filler != b"\x01" * len(filler) or line[-1] != 0:
        raise Failure(f"{what}: the line ends {line[stamp_at + stamp_len:]!r}, expected "
                      f"{len(filler)} filler bytes of 01 and a zero: {line!r}")


def check_listing_layout(agent, folder):
    """#917, SI-139: a time stamped line is a fixed 64 bytes, or 42 in the short format.

    The stamp starts four characters behind a three character type and two behind the
    single letter of the short format, and CMD DOS fills what is left of the line with
    0x01. This drive wrote the long stamp two columns early and ended every line where
    its contents happened to end.
    """
    here = folder.upper()
    for name, length, type_name, gap, stamp_len in (
            (f"$//{here}:*=P,L", 64, b"PRG", 3, 19),
            (f"$=T//{here}:*=P", 42, b"P", 1, 13)):
        with check(f"SI-139: {name} lines are {length} bytes with 0x01 filler"):
            listing = listing_of(agent, name)
            lines = stamped_lines(listing, length, name)
            detail(f"{name}: {len(lines)} lines of {length} bytes, first {lines[0]!r}")
            for line in lines:
                check_stamped_line(line, name, type_name, gap, stamp_len)


def check_listing_eof(agent, folder):
    """#917, SI-138: the last byte of a listing carries EOI.

    The tail is read one byte per transaction, which is what BASIC's GET# does: the
    KERNAL addresses the drive to talk, takes one byte and untalks again. Reading the
    listing in larger pieces cannot fail for this, because the drive then runs ahead
    within one talk and reaches the last byte on its own.
    """
    here = folder.upper()
    name = f"$//{here}:*"
    with check("SI-138: a listing read one byte at a time ends with status 64"):
        total = len(listing_of(agent, name))
        agent.call(OPEN, channel=3, data=name.encode("ascii"), secondary=0)
        try:
            agent.status()
            remaining = total - 4
            while remaining:
                step = min(remaining, MAILBOX_CAPACITY)
                agent.read_exact(step, channel=3)
                remaining -= step
            tail = b""
            for _ in range(8):
                tail += agent.call(READ_COUNT, channel=3, count=1)
                if agent.last_status & 64:
                    break
            else:
                raise Failure(f"{len(tail)} single byte reads at the end of a {total} byte "
                              f"listing all answered status 0: {tail!r}")
        finally:
            agent.call(CLOSE, channel=3)
        detail(f"the last {len(tail)} bytes of a {total} byte listing read one at a time: {tail!r}")
        if len(tail) != 4 or tail[-1] != 0:
            raise Failure(f"end of file arrived after {len(tail)} of the last 4 bytes: {tail!r}")


def check_image_lock(agent, api, password, folder, image_path):
    """#917, SI-077a: EL:$ write protects a disk image and EU:$ lifts the protection.

    The lock is the DOS version byte in the image's header, as in sd2iec, so the image
    carries it: the drive refuses a save into it, and so does FTP, which reaches the image
    through the same file system.
    """
    def header_version():
        with ftp.session(api.host, password) as client:
            return ftp.retrieve(client, image_path)[BAM_OFFSET + 2]

    agent.command(f"CD//{folder.upper()}/{Path(image_path).name.upper()}")
    try:
        with check("SI-077a: EL:$ writes the locked DOS version into the header"):
            agent.command("EL:$")
            version = header_version()
            if version != 0x3C:
                raise Failure(f"the DOS version byte is ${version:02X} after EL:$, expected $3C")
        with check("SI-077a: a save into the locked image answers 26"):
            agent.call(OPEN, channel=2, data=b"LOCKED,P,W")
            try:
                agent.status((26,))
            finally:
                agent.call(CLOSE, channel=2)
        with check("SI-077a: FTP cannot store a file in the locked image"):
            with ftp.session(api.host, password) as client:
                try:
                    client.storbinary(f"STOR {image_path}/ftpfile.prg", io.BytesIO(b"\x01\x08"))
                except (ftplib.error_perm, ftplib.error_temp) as exc:
                    detail(f"FTP answered: {exc}")
                else:
                    raise Failure("FTP stored a file in a locked image")
    finally:
        agent.command("EU:$")
    with check("SI-077a: FTP stores a file in the image once EU:$ lifts the lock"):
        with ftp.session(api.host, password) as client:
            client.storbinary(f"STOR {image_path}/ftpfile.prg", io.BytesIO(b"\x01\x08"))
            client.delete(f"{image_path}/ftpfile.prg")
    with check("SI-077a: EU:$ puts the DOS version back and a save works again"):
        version = header_version()
        if version != 0x41:
            raise Failure(f"the DOS version byte is ${version:02X} after EU:$, expected $41")
        agent.call(OPEN, channel=2, data=b"UNLOCKED,P,W")
        try:
            agent.status()
            agent.call(WRITE, channel=2, data=b"\x01\x08")
        finally:
            agent.call(CLOSE, channel=2)
        agent.status()


# Contents lengths around the drive's 512 byte buffers and the 254 byte blocks of
# a disk. With the two byte load address, 509, 1021 and 2045 fill the last buffer
# with one byte.
LOAD_SIZES = (1, 3, 252, 253, 254, 509, 510, 511, 1021, 1022, 2045, 2046)
LOAD_ADDRESS = 0x4000


def check_load(agent, api, password, folder, root):
    """#917, SI-153: a LOAD brings back every byte of the file, whatever its length.

    The KERNAL's LOAD is the one transfer JiffyDOS replaces with a block protocol of
    its own, which lost the last byte of a file whose final 512 byte buffer holds a
    single byte. Under the stock KERNAL this checks the ordinary LOAD; run the suite
    with --kernal to check another KERNAL's.
    """
    rnd = random.Random(917)
    contents = {size: bytes(rnd.randrange(256) for _ in range(size)) for size in LOAD_SIZES}
    directory = f"{root.rstrip('/')}/{folder}"
    with ftp.session(api.host, password) as client:
        for size, body in contents.items():
            client.storbinary(f"STOR {directory}/load{size}.prg",
                              io.BytesIO(LOAD_ADDRESS.to_bytes(2, "little") + body))
    agent.command(f"CD//{folder.upper()}")
    for size, body in contents.items():
        with check(f"SI-153: LOAD of a {size + 2} byte file brings back all of it"):
            end = agent.load(f"LOAD{size}", size)
            loaded = api.machine.readmem(LOAD_ADDRESS, size)
            wrong = [i for i in range(size) if loaded[i] != body[i]]
            if end != LOAD_ADDRESS + size or wrong:
                raise Failure(f"LOAD ended at ${end:04X}, expected ${LOAD_ADDRESS + size:04X}; "
                              f"{len(wrong)} bytes differ" + (f", first at {wrong[0]}" if wrong else ""))


def check_compatibility(agent, api, password, folder, root):
    """doc/softiec_compatibility_spec.md, in the bytes a Commodore sends.

    The host suites check each requirement against the same code; these are the ones
    whose answer depends on something only the device has: the IEC processor's device
    number slots (U0>), the CPU the firmware runs on, whose char is signed on
    the Nios II of an Ultimate 64 and unsigned on the RISC-V of an Ultimate 64 II and an
    Ultimate II+L (the shifted space rule, SI-147), and the real bus timing of a command
    that fills the 254 byte buffer.
    """
    here = folder.upper().encode("ascii")
    directory = f"{root.rstrip('/')}/{folder}"

    def unrecognised_command():
        response = agent.command(bytes([0]), allowed=(31,))
        detail(response)

    def scratch_nothing():
        response = agent.command(b"S//" + here + b"/:NOSUCHFILE\r", allowed=(1,))
        if not response.startswith("01, FILES SCRATCHED,00"):
            raise Failure(f"answered {response!r}")

    def initialize():
        agent.command(b"I\r", allowed=(0,))
        agent.command(b"UI\r", allowed=(73,))

    def memory_read():
        probe = agent.command_reply(b"M-R" + bytes([0xA4, 0xFE, 2]) + b"\r", 2)
        detail(f"M-R $FEA4,2 answered {probe.hex()}")
        if probe != bytes(2):
            raise Failure(f"M-R answered {probe!r}")
        agent.status((0,))
        # SI-105: no drive code runs here, so M-E answers 98, as sd2iec answers for a
        # drive code it does not know, and M-W is refused.
        answer = agent.command(b"M-E" + bytes([0x00, 0x05]) + b"\r", allowed=(98,))
        detail(f"M-E $0500 answers {answer!r}")
        if not answer.startswith("98,UNKNOWN DRIVE CODE"):
            raise Failure(f"M-E answered {answer!r}")
        agent.command(b"M-W" + bytes([0x00, 0x05, 0x01, 0xEA]) + b"\r", allowed=(30,))

    def partition_directory():
        listing = listing_of(agent, "$=P")
        detail(f"{len(listing)} bytes, header {listing[:6].hex()}, last line {listing[-32:]!r}")
        # Every problem is reported, so #890's footer shows even when the header is wrong too.
        problems = []
        if b"BLOCKS FREE" in listing or listing[-2:] != bytes(2):
            problems.append("the partition directory ends with a blocks free line (#890)")
        if listing[4] != 1:
            problems.append(f"the header counts {listing[4]} partitions, expected 1")
        if listing[:4] != bytes([1, 4, 1, 1]):
            problems.append(f"the listing starts {listing[:4].hex()}, expected 01040101")
        if problems:
            raise Failure("; ".join(problems))

    def device_number():
        moved = agent.move_drive(12)
        detail(f"after U0>+CHR$(12) the drive list reports device {moved}")
        if moved != 12:
            agent.status((0,))
            raise Failure(f"the drive stayed at device {moved}")
        try:
            reply = agent.call(3, 15, device=12, expect=STATUS_BYTES).decode("ascii").strip()
            detail(f"device 12 answered {reply!r}")
            if not reply.startswith("00,"):
                raise Failure(f"device 12 answered {reply!r}")
        finally:
            # Back to device 11 whatever happened, so the checks after this one still
            # have their drive.
            agent.move_drive(11)
        agent.status((0,))

    def device_aliases():
        # SI-101: S-8, S-9 and S-D are the typed aliases of the same move, and they are
        # exactly three characters, so S:-8 is still a scratch.
        moved = agent.move_drive(9, command=b"S-9\r")
        detail(f"after S-9 the drive list reports device {moved}")
        if moved != 9:
            agent.status((0,))
            raise Failure(f"the drive stayed at device {moved}")
        try:
            reply = agent.call(3, 15, device=9, expect=STATUS_BYTES).decode("ascii").strip()
            detail(f"device 9 answered {reply!r}")
            if not reply.startswith("00,"):
                raise Failure(f"device 9 answered {reply!r}")
        finally:
            # S-D returns the drive to the number the settings hold, which this suite
            # has set to 11.
            back = agent.move_drive(11, command=b"S-D\r")
            detail(f"after S-D the drive list reports device {back}")
        if back != 11:
            raise Failure(f"S-D left the drive at device {back}")
        agent.status((0,))

    def memory_write_device():
        # SI-100a: an M-W to $0077 moves the drive to the number of the listen address it
        # writes. These are the bytes a CMD drive's SWAP button sends, without a
        # terminator, and the swap back goes to the number the drive moved to.
        def swap_to(device):
            return b"M-W" + bytes([0x77, 0x00, 0x02, 0x20 | device, 0x40 | device])

        moved = agent.move_drive(12, command=swap_to(12))
        detail(f"after M-W $0077 with $2C $4C the drive list reports device {moved}")
        if moved != 12:
            agent.status((0,))
            raise Failure(f"the drive stayed at device {moved}")
        try:
            reply = agent.call(3, 15, device=12, expect=STATUS_BYTES).decode("ascii").strip()
            detail(f"device 12 answered {reply!r}")
            if not reply.startswith("00,"):
                raise Failure(f"device 12 answered {reply!r}")
        finally:
            back = agent.move_drive(11, command=swap_to(11))
            detail(f"after M-W $0077 with $2B $4B the drive list reports device {back}")
        if back != 11:
            raise Failure(f"M-W $0077 left the drive at device {back}")
        agent.status((0,))

    def memory_write_device_cmd_timing():
        # SI-100a with a CMD drive's own timing (#933). A CMD FD or HD runs at 2 MHz and raises
        # ATN for UNLISTEN about 40 us after the last byte is acknowledged; the KERNAL above
        # leaves more time, so only this check sees a byte lost in between.
        old = agent.softiec_device
        try:
            moved = cmd_swap(api, old, 12)
            detail(f"after a CMD drive's SWAP sequence with $2C $4C the drive list reports device {moved}")
            if moved != 12:
                raise Failure(f"the drive stayed at device {moved}")
            back = cmd_swap(api, 12, old)
            detail(f"after the sequence back the drive list reports device {back}")
            if back != old:
                raise Failure(f"the sequence back left the drive at device {back}")
        finally:
            agent.start()
            agent.call(OPEN, channel=15, device=agent.softiec_device)
        agent.status((0,))

    def clock_write():
        # SI-120 on the device: a write sets the drive's own clock, an offset from the
        # system clock, which it leaves alone; UJ returns the drive to the system clock.
        # The clocks run while the check does, so every answer is decoded to a moment and
        # compared as one: a second that carries into the minute, hour or day is not a
        # difference.
        def parsed(answer, what):
            stamp = re.match(r"(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d) ([A-Z]{3})$", answer)
            if not stamp:
                raise Failure(f"{what} answered {answer!r}")
            fields = [int(f) for f in stamp.groups()[:6]]
            return datetime.datetime(*fields), stamp.group(7)

        # How far a clock may read past the moment it was given: the time that has passed
        # since, which a stalled request lengthens, and a second either side of the moment.
        def allowed(since):
            return time.monotonic() - since + 2

        def near(answer, wanted, what, since):
            when, _ = parsed(answer, what)
            drift = (when - wanted).total_seconds()
            if not 0 <= drift <= allowed(since):
                raise Failure(f"{what} answered {answer!r}, which is {drift:.0f} seconds "
                              f"from the {wanted.isoformat()} that was written")
            return when

        agent.command(b"UJ\r", allowed=(73,))  # a drive left with an offset reads the system clock
        before_answer = agent.command_reply(b"T-RI\r", 24).decode("ascii").strip()
        before, before_day = parsed(before_answer, "T-RI")
        started = time.monotonic()
        detail(f"the clock reads {before_answer!r}")
        try:
            written = datetime.datetime(2026, 9, 12, 13, 2, 3)
            agent.command(b"T-WI2026-09-12T13:02:03\r", allowed=(0,))
            wrote_at = time.monotonic()
            iso = agent.command_reply(b"T-RI\r", 24).decode("ascii").strip()
            ascii_form = agent.command_reply(b"T-RA\r", 26).decode("ascii").strip()
            decimal = agent.command_reply(b"T-RD\r", 9)
            bcd = agent.command_reply(b"T-RB\r", 9)
            detail(f"after T-WI the clock reads {iso!r}, {ascii_form!r}, "
                   f"{decimal.hex()}, {bcd.hex()}")
            days = ("SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT")
            when = near(iso, written, "T-RI", wrote_at)
            if not iso.endswith(" " + days[(when.weekday() + 1) % 7]):
                raise Failure(f"T-RI answered {iso!r}, whose day of week is not its date's")

            def twelve_hour(hour, pm):
                return (hour % 12) + (12 if pm else 0)

            def binary_moment(answer, bcd_coded, what):
                if len(answer) != 9 or answer[8] != 13:
                    raise Failure(f"{what} answered {answer.hex()}")
                f = [((b >> 4) * 10) + (b & 15) for b in answer[1:7]] if bcd_coded else list(answer[1:7])
                year = (2000 + f[0]) if bcd_coded else (1900 + f[0])
                if bcd_coded and f[0] >= 80:
                    year = 1900 + f[0]
                moment = datetime.datetime(year, f[1], f[2], twelve_hour(f[3], answer[7]), f[4], f[5])
                if answer[0] != (moment.weekday() + 1) % 7:
                    raise Failure(f"{what} answered {answer.hex()}, whose day of week is not its date's")
                return moment

            stamp = re.match(r"([A-Z]{3})[A-Z.] (\d\d)/(\d\d)/(\d\d) (\d\d):(\d\d):(\d\d) ([AP])M$",
                             ascii_form)
            if not stamp:
                raise Failure(f"T-RA answered {ascii_form!r} after a clock write")
            month, day, year, hour, minute, second = (int(v) for v in stamp.groups()[1:7])
            moment = datetime.datetime(2000 + year, month, day,
                                       twelve_hour(hour, stamp.group(8) == "P"), minute, second)
            if stamp.group(1) != days[(moment.weekday() + 1) % 7]:
                raise Failure(f"T-RA answered {ascii_form!r}, whose day of week is not its date's")
            for what, got in (("T-RA", moment),
                              ("T-RD", binary_moment(decimal, False, "T-RD")),
                              ("T-RB", binary_moment(bcd, True, "T-RB"))):
                drift = (got - written).total_seconds()
                if not 0 <= drift <= allowed(wrote_at):
                    raise Failure(f"{what} read {got.isoformat()}, {drift:.0f} seconds from "
                                  f"the {written.isoformat()} that was written")
            # The other three write forms set the same clock. Each is written from a
            # different moment so that a form that writes nothing cannot pass by leaving
            # the previous one in place.
            for form, command, moment, day in (
                    ("T-WA", b"T-WAFRI. 03/04/05 02:30:00 PM",
                     datetime.datetime(2005, 3, 4, 14, 30, 0), "FRI"),
                    ("T-WD", bytes([ord("T"), ord("-"), ord("W"), ord("D"),
                                    2, 118, 11, 27, 11, 45, 0, 0]),
                     datetime.datetime(2018, 11, 27, 11, 45, 0), "TUE"),
                    ("T-WB", bytes([ord("T"), ord("-"), ord("W"), ord("B"),
                                    2, 0x22, 0x07, 0x19, 0x09, 0x15, 0x00, 1]),
                     datetime.datetime(2022, 7, 19, 21, 15, 0), "TUE"),
            ):
                agent.command(command, allowed=(0,))
                wrote_at = time.monotonic()
                answer = agent.command_reply(b"T-RI\r", 24).decode("ascii").strip()
                detail(f"{form} then T-RI reads {answer!r}")
                near(answer, moment, f"T-RI after {form}", wrote_at)
                if not answer.endswith(" " + day):
                    raise Failure(f"{form} wrote a {day} and T-RI answered {answer!r}")

            # A day the month does not have is refused and does not move the clock.
            agent.command(b"T-WI2026-02-30T00:00:00\r", allowed=(30,))
            near(agent.command_reply(b"T-RI\r", 24).decode("ascii").strip(),
                 datetime.datetime(2022, 7, 19, 21, 15, 0),
                 "T-RI after a refused write", wrote_at)
            # The system clock did not move: a file written over FTP after the clock write
            # carries the system clock's date in its time stamp, not the written one.
            with ftp.session(api.host, password) as client:
                ftp.store(client, f"{directory}/CLOCK.PRG", b"stamped by the system clock")
            stamped = listing_of(agent, f"$//{folder.upper()}/:CLOCK*=L")
            detail(f"a file written after the clock write lists as {stamped[LINE:2 * LINE]!r}")
            system_date = before.strftime("%m/%d/%y").encode("ascii")
            if system_date not in stamped or b"07/19/22" in stamped:
                raise Failure(f"a file written after the clock write is not stamped "
                              f"{system_date.decode()}, the system clock's date")
            agent.command(b"S//" + here + b"/:CLOCK\r", allowed=(1,))
        finally:
            # UJ resets the drive, which returns it to the system clock.
            agent.command(b"UJ\r", allowed=(73,))
            back_at = time.monotonic()
            back = before + datetime.timedelta(seconds=round(back_at - started))
            restored = agent.command_reply(b"T-RI\r", 24).decode("ascii").strip()
            detail(f"after UJ the clock reads {restored!r}, from {before_answer!r}")
        near(restored, back, "the clock after UJ", back_at)
        if not restored.endswith(" " + before_day):
            raise Failure(f"after UJ the clock reads {restored!r}, not a {before_day}")

    def write_protect():
        # SI-102 over the real bus: while W-1 is set nothing that changes a medium runs.
        with ftp.session(api.host, password) as client:
            ftp.store(client, f"{directory}/WRAP.S00", x00_header(b"WRAPPED") + b"text")
        agent.command(b"CD//" + here + b"\r")
        agent.command(b"W-1\r", allowed=(0,))
        try:
            for command in (b"MD:PROTECTED\r", b"S:*\r", b"R:X=Y\r"):
                response = agent.command(command, allowed=(26,))
                detail(f"{command!r} answers {response!r}")
            # A replace of a file in an x00 wrapper is refused and leaves the file.
            agent.call(1, channel=3, data=b"@:WRAPPED,S,W")
            try:
                response = agent.status(allowed=range(100))
            finally:
                agent.call(4, channel=3)
            with ftp.session(api.host, password) as client:
                names = ftp.names(client, directory)
            detail(f"@:WRAPPED,S,W answers {response!r}; WRAP.S00 is "
                   f"{'still there' if 'WRAP.S00' in names else 'gone'}")
            if not response.startswith("26,") or "WRAP.S00" not in names:
                raise Failure(f"@:WRAPPED,S,W answered {response!r} and left {sorted(names)}")
            listing = listing_of(agent, f"$//{folder.upper()}")
            if b"PROTECTED" in listing:
                raise Failure("a directory was created while the drive was write protected")
        finally:
            agent.command(b"W-0\r", allowed=(0,))
        agent.command(b"MD:PROTECTED\r", allowed=(0,))
        agent.command(b"RD:PROTECTED\r", allowed=(0,))
        agent.command(b"S:WRAPPED\r", allowed=(1,))
        agent.command(b"CD//\r")

    def left_arrow():
        agent.command(b"CD//" + here + b"\r")
        agent.command(b"MD:_\r")
        agent.command(b"CD/_\r")
        agent.command(b"CD:_\r")
        agent.command(b"RD:_\r")
        agent.command(b"CD//\r")

    def rename_directory():
        # HD 9-26 heads its section "Renaming Files and Subdirectories", and sd2iec
        # matches an entry of any type, so R renames a subdirectory as well as a file.
        agent.command(b"CD//" + here + b"\r")
        agent.command(b"MD:DIRA\r")
        agent.command(b"R:DIRB=DIRA\r", allowed=(0,))
        agent.command(b"CD:DIRB\r")
        agent.command(b"CD//" + here + b"\r")
        response = agent.command(b"R:DIRC=DIRA\r", allowed=(62,))
        detail(f"a name that belongs to nothing answers {response!r}")
        agent.command(b"RD:DIRB\r")
        agent.command(b"CD//\r")

    def shifted_space():
        for name, host in ((b"PAD\xa0", "PAD.seq"), (b"IN\xa0SIDE", "IN{A0}SIDE.seq")):
            agent.call(1, channel=3, data=b"//" + here + b"/:" + name + b",S,W")
            try:
                agent.status((0,))
                agent.call(2, channel=3, data=b"X")
            finally:
                agent.call(4, channel=3)
            with ftp.session(api.host, password) as client:
                entries = ftp.names(client, directory)
            detail(f"{name!r} is stored as one of {sorted(entries)}")
            if host not in entries:
                raise Failure(f"{name!r} was not stored as {host}: {entries}")

    def command_length():
        pad = 253 - len(b"S//" + here + b"/:")
        agent.command(b"S//" + here + b"/:" + b"N" * pad, allowed=(1,))
        response = agent.command(b"S//" + here + b"/:" + b"N" * (pad + 1), allowed=(32,))
        detail(response)

    def format_image():
        agent.command(b"N//" + here + b"/:MADE.D64,AB\r")
        with ftp.session(api.host, password) as client:
            image = ftp.retrieve(client, f"{directory}/MADE.D64")
        detail(f"MADE.D64 is {len(image)} bytes, label {image[BAM_OFFSET + 144:BAM_OFFSET + 148]!r}")
        if len(image) != 174848 or image[BAM_OFFSET + 144:BAM_OFFSET + 148] != b"MADE":
            raise Failure("N did not create a formatted 1541 image")

    def resets():
        agent.call(1, channel=4, data=b"//" + here + b"/:KEPT,S,W")
        try:
            agent.status()
            agent.call(2, channel=4, data=b"abc")
            response = agent.command(b"UJ\r", allowed=(73,))
            detail(f"UJ answered {response!r} with a file open for writing")
        finally:
            agent.call(4, channel=4)
        agent.call(1, channel=3, data=b"//" + here + b"/:KEPT,S,R")
        try:
            agent.status()
            kept = agent.read_stream(channel=3)
        finally:
            agent.call(4, channel=3)
        if kept != b"abc":
            raise Failure(f"the file UJ closed holds {kept!r}")
        agent.command(b"CD//" + here + b"\r")
        response = agent.command(bytes([ord("U"), 0xCA]) + b"\r", allowed=(73,))
        where = agent.command_reply(b"XPWD\r", 32).decode("ascii").strip()
        detail(f"U+shifted J answered {response!r}, then XPWD {where!r}")
        if where != "1:/":
            raise Failure(f"after U+shifted J the working directory is {where!r}")

    def x00_header(name, record_length=0):
        return b"C64File\0" + name.ljust(16, b"\0") + b"\0" + bytes([record_length])

    def x00_read():
        with ftp.session(api.host, password) as client:
            ftp.store(client, f"{directory}/GAME.P00", x00_header(b"MY GAME") + b"PAYLOAD")
        listing = listing_of(agent, f"$//{folder.upper()}/:MY*")
        lines = [parse_directory_line(listing[i:i + LINE]) for i in range(LINE, len(listing) - LINE, LINE)]
        detail(f"$:MY* lists {lines}")
        if (1, "MY GAME", "PRG") not in lines:
            raise Failure(f"GAME.P00 is not listed as MY GAME PRG: {lines}")
        agent.call(1, channel=3, data=b"//" + here + b"/:MY GAME,P,R")
        try:
            agent.status()
            data = agent.read_stream(channel=3)
        finally:
            agent.call(4, channel=3)
        detail(f"MY GAME reads {data!r}")
        if data != b"PAYLOAD":
            raise Failure(f"MY GAME reads {data!r}")
        agent.command(b"R//" + here + b"/:TUNE=//" + here + b"/:MY GAME\r")
        # The host file takes the new name as well, so the two names agree (SI-144c).
        with ftp.session(api.host, password) as client:
            after = ftp.names(client, directory)
            renamed = ftp.retrieve(client, f"{directory}/TUNE.P00")
        detail(f"after R:TUNE=MY GAME the directory holds {sorted(after)} "
               f"and the header names {renamed[8:24]!r}")
        if "GAME.P00" in after:
            raise Failure(f"the rename left GAME.P00 behind: {sorted(after)}")
        if renamed[:26] != x00_header(b"TUNE"):
            raise Failure(f"the rename left the header as {renamed[:26]!r}")
        response = agent.command(b"S//" + here + b"/:TUNE\r", allowed=(1,))
        with ftp.session(api.host, password) as client:
            left = ftp.names(client, directory)
        detail(f"S:TUNE answered {response!r}")
        if not response.startswith("01, FILES SCRATCHED,01") or "TUNE.P00" in left:
            raise Failure(f"S:TUNE answered {response!r} and left {sorted(left)}")

        # A host file already holding the new spelling pushes the wrapper to the next
        # extension digit and is left as it is (SI-144c).
        with ftp.session(api.host, password) as client:
            ftp.store(client, f"{directory}/WRAP.S00", x00_header(b"WRAPPED") + b"text")
            ftp.store(client, f"{directory}/OCCUPIED.S00", b"not a wrapper")
        agent.command(b"R//" + here + b"/:OCCUPIED=//" + here + b"/:WRAPPED\r")
        with ftp.session(api.host, password) as client:
            names = ftp.names(client, directory)
            moved = ftp.retrieve(client, f"{directory}/OCCUPIED.S01")
            blocked = ftp.retrieve(client, f"{directory}/OCCUPIED.S00")
        detail(f"after the rename onto a taken name the directory holds {sorted(names)}")
        if "WRAP.S00" in names:
            raise Failure(f"the rename left WRAP.S00 behind: {sorted(names)}")
        if moved[:26] != x00_header(b"OCCUPIED") or moved[26:] != b"text":
            raise Failure(f"OCCUPIED.S01 holds {moved!r}")
        if blocked != b"not a wrapper":
            raise Failure(f"the rename wrote over OCCUPIED.S00, which holds {blocked!r}")
        agent.command(b"S//" + here + b"/:OCCUPIED\r", allowed=(1,))
        with ftp.session(api.host, password) as client:
            ftp.delete_quietly(client, f"{directory}/OCCUPIED.S00")

    def write_protect_open_channel():
        # A file opened for writing before W-1 takes no more bytes once it is set, and its
        # close writes nothing (SI-102a).
        agent.call(1, channel=4, data=b"//" + here + b"/:OPENWRITE,S,W")
        try:
            agent.status()
            agent.call(WRITE, channel=4, data=b"before")
            agent.command(b"W-1\r", allowed=(0,))
            try:
                agent.call(WRITE, channel=4, data=b"after")
                response = agent.status(allowed=range(100))
                # P would write what the channel holds and move past the end of the file.
                positioned = agent.command(bytes([ord("P"), 96 + 4, 0, 1, 0, 0]), allowed=range(100))
            finally:
                agent.call(4, channel=4)
                agent.command(b"W-0\r", allowed=(0,))
        except Failure:
            agent.command(b"W-0\r", allowed=(0,))
            raise
        with ftp.session(api.host, password) as client:
            names = {n.lower(): n for n in ftp.names(client, directory)}
            held = ftp.retrieve(client, f"{directory}/{names['openwrite.seq']}") \
                if "openwrite.seq" in names else None
        detail(f"a write after W-1 answered {response!r}, a P {positioned!r}; the file holds {held!r}")
        agent.command(b"S//" + here + b"/:OPENWRITE\r", allowed=(1,))
        if not response.startswith("26,") or not positioned.startswith("26,") or held != b"":
            raise Failure(f"a write after W-1 answered {response!r}, a P {positioned!r}, "
                          f"and the file holds {held!r}")

    def copy_record_lengths():
        # Relative files of two record lengths answer 64 and leave no target (SI-075).
        with ftp.session(api.host, password) as client:
            ftp.store(client, f"{directory}/RTEN.R00", x00_header(b"RTEN", 10) + b"T" * 10)
            ftp.store(client, f"{directory}/RFIVE.R00", x00_header(b"RFIVE", 5) + b"F" * 5)
        try:
            response = agent.command(b"C//" + here + b"/:RBOTH=//" + here + b"/:RTEN,//" + here + b"/:RFIVE\r",
                                     allowed=range(100))
            with ftp.session(api.host, password) as client:
                left = [n for n in ftp.names(client, directory) if n.lower().startswith("rboth")]
        finally:
            with ftp.session(api.host, password) as client:
                for name in ("RTEN.R00", "RFIVE.R00", "RBOTH.rel"):
                    ftp.delete_quietly(client, f"{directory}/{name}")
        detail(f"the copy answered {response!r} and left {left}")
        if not response.startswith("64,") or left:
            raise Failure(f"the copy answered {response!r} and left {left}")

    def delete_open_image():
        # A file open for writing inside a disk image keeps the image from being deleted.
        agent.command(b"N//" + here + b"/:OPENIMG.D64,OI\r")
        agent.call(1, channel=5, data=b"//" + here + b"/OPENIMG.D64/:INSIDE,S,W")
        try:
            agent.status()
            with ftp.session(api.host, password) as client:
                try:
                    client.delete(f"{directory}/OPENIMG.D64")
                except (ftplib.error_perm, ftplib.error_temp) as exc:
                    detail(f"FTP DELE of the image answered {exc}")
                else:
                    raise Failure("FTP deleted a disk image with a file open for writing inside it")
        finally:
            agent.call(4, channel=5)
        with ftp.session(api.host, password) as client:
            ftp.delete_quietly(client, f"{directory}/OPENIMG.D64")

    def x00_rename_open():
        # A rename refused because the file is open for writing leaves its header (SI-144c).
        with ftp.session(api.host, password) as client:
            ftp.store(client, f"{directory}/WOPEN.S00", x00_header(b"WRAPOPEN") + b"text")
        agent.call(1, channel=5, data=b"//" + here + b"/:WRAPOPEN,S,A")
        try:
            agent.status()
            response = agent.command(b"R//" + here + b"/:RENAMEDOPEN=//" + here + b"/:WRAPOPEN\r",
                                     allowed=range(100))
        finally:
            agent.call(4, channel=5)
        with ftp.session(api.host, password) as client:
            names = [n for n in ftp.names(client, directory) if n.lower().endswith(".s00")]
            header = ftp.retrieve(client, f"{directory}/WOPEN.S00")[:26] if "WOPEN.S00" in names else b""
            for name in names:
                ftp.delete_quietly(client, f"{directory}/{name}")
        detail(f"the rename answered {response!r}; host files {names}, header {header[8:24]!r}")
        if not response.startswith("60,") or header != x00_header(b"WRAPOPEN"):
            raise Failure(f"the rename answered {response!r} and left {names} with the header {header!r}")

    def rename_header_checks():
        # R-H on a host directory refuses what R refuses a directory (SI-064, SI-141, SI-074).
        agent.command(b"CD//" + here + b"\r")
        agent.command(b"MD:HEADDIR\r")
        agent.command(b"MD:HEADDIR2\r")
        agent.call(1, channel=3, data=b"HEADTAKEN,P,W")
        agent.status()
        agent.call(4, channel=3)
        try:
            # One directory each, so a rename the first probe makes does not move the second.
            dot = agent.command(b"R-H/HEADDIR/:NEWNAME.\r", allowed=range(100))
            taken = agent.command(b"R-H/HEADDIR2/:HEADTAKEN\r", allowed=range(100))
            detail(f"R-H to NEWNAME. answered {dot!r}, to HEADTAKEN {taken!r}")
            if not dot.startswith("33,") or not taken.startswith("63,"):
                raise Failure(f"R-H answered {dot!r} for a trailing dot and {taken!r} for a taken name")
        finally:
            agent.command(b"CD//" + here + b"\r")
            for name in (b"HEADDIR", b"HEADDIR2", b"NEWNAME", b"HEADTAKEN"):
                agent.command(b"RD:" + name + b"\r", allowed=range(100))
            agent.command(b"S:HEADTAKEN\r", allowed=range(100))
            agent.command(b"CD//\r")

    def settings_x_lock():
        # SDM's XL and XU are settings commands, out of scope, so they lock nothing (SI-077).
        for command in (b"XL:*\r", b"XU:*\r"):
            response = agent.command(command, allowed=range(100))
            detail(f"{command!r} answered {response!r}")
            if not response.startswith("30,"):
                raise Failure(f"{command!r} answered {response!r}")

    def delete_open_file():
        # A file the drive holds open for writing is not deleted from FTP: a new directory
        # would take its slot, and the close would then write the file's entry over it.
        agent.call(1, channel=5, data=b"//" + here + b"/:OPENDEL,S,W")
        try:
            agent.status()
            with ftp.session(api.host, password) as client:
                try:
                    client.delete(f"{directory}/OPENDEL.seq")
                    deleted = True
                except ftplib.all_errors as exc:
                    deleted = False
                    detail(f"FTP DELE of the open file answered {exc}")
                if deleted:
                    # No directory is made in the freed slot, which the close would damage.
                    raise Failure("FTP deleted a file the drive has open for writing")
                ftp.quietly(lambda: client.mkd(f"{directory}/OPENSUB"))
        finally:
            agent.call(4, channel=5)
        with ftp.session(api.host, password) as client:
            inside = ftp.names(client, f"{directory}/OPENSUB")
            ftp.quietly(lambda: client.rmd(f"{directory}/OPENSUB"))
        agent.command(b"S//" + here + b"/:OPENDEL\r", allowed=(0, 1))
        detail(f"a directory made beside the open file lists {inside}")
        if inside:
            raise Failure(f"a directory made beside the open file lists {inside[:6]}")

    def reset_drops_partial_command():
        # SI-103b: the drive's own Reset drops a command that was still being received. The
        # talker sends two command bytes without EOI, so the command has not ended, and the
        # REST reset follows. Reopening channel 15 without a name sends nothing on the bus.
        talker = Talker(api)
        try:
            talker.start()
            talker.send(agent.softiec_device, 0x6F, b"XY", eoi=False)
            api.rest.request("PUT", "/v1/drives/softiec:reset")
        finally:
            agent.start()
            agent.call(OPEN, channel=15, device=agent.softiec_device)
        response = agent.command(b"CD//\r", allowed=range(100))
        detail(f"CD// after a reset that cut a command short answered {response!r}")
        if not response.startswith("00"):
            raise Failure(f"CD// after a reset that cut a command short answered {response!r}")

    def x00_rename_case():
        # A new name whose host spelling differs from the file's host name in case only
        # writes the header and keeps that one host file (SI-144c).
        with ftp.session(api.host, password) as client:
            ftp.store(client, f"{directory}/lower.p00", x00_header(b"OTHER LOWER") + b"PAYLOAD")
        response = agent.command(b"R//" + here + b"/:LOWER=//" + here + b"/:OTHER LOWER\r",
                                 allowed=range(100))
        with ftp.session(api.host, password) as client:
            names = [n for n in ftp.names(client, directory) if n.lower() == "lower.p00"]
            header = ftp.retrieve(client, f"{directory}/{names[0]}")[:26] if names else b""
        detail(f"R:LOWER=OTHER LOWER answered {response!r}; host files {names}, "
               f"header names {header[8:24]!r}")
        if not response.startswith("00"):
            raise Failure(f"R:LOWER=OTHER LOWER answered {response!r}")
        if len(names) != 1 or header != x00_header(b"LOWER"):
            raise Failure(f"the rename left {names} with the header {header!r}")
        agent.command(b"S//" + here + b"/:LOWER\r", allowed=(1,))

    def rel_layouts():
        # sd2iec's layout: the record length in one byte, then records of three bytes.
        with ftp.session(api.host, password) as client:
            ftp.store(client, f"{directory}/SDREL.rel", bytes([3]) + b"\0aabbb")
        agent.call(1, channel=3, data=b"//" + here + b"/:SDREL,L")
        try:
            agent.status()
            agent.command(bytes([ord("P"), 96 + 3, 2, 0, 1]))
            record = agent.read_stream(channel=3)
        finally:
            agent.call(4, channel=3)
        detail(f"record 2 of SDREL reads {record!r}")
        if record != b"bbb":
            raise Failure(f"record 2 of a one byte layout file reads {record!r}")

    # Each check reports on its own, so a firmware that fails one still shows the rest.
    failed = []
    for label, action in (
            ("SI-031: a command that is not a command letter answers 31", unrecognised_command),
            ("SI-033: a scratch that matches nothing answers 01", scratch_nothing),
            ("SI-053: I answers OK, UI the DOS version", initialize),
            ("SI-105: M-R answers the two bytes C64 OS asks for, all zero", memory_read),
            ("SI-045, SI-046, SI-130: the partition directory", partition_directory),
            ("SI-100: U0> moves the drive to device 12 on the bus, and U0> moves it back", device_number),
            ("SI-101: S-9 and S-D move the drive on the bus", device_aliases),
            ("SI-100a: M-W to $0077 from the KERNAL moves the drive on the bus", memory_write_device),
            ("SI-100a: a CMD drive's SWAP sequence, with its bus timing, moves the drive", memory_write_device_cmd_timing),
            ("SI-120: T-W sets the drive's own clock and UJ returns it to the system clock", clock_write),
            ("SI-102: W-1 refuses every command that changes a medium", write_protect),
            ("SI-014: a left arrow between slashes is a directory name", left_arrow),
            ("SI-147, SI-148: a shifted space in a name, on this CPU", shifted_space),
            ("SI-021, SI-022: a 253 byte command runs, a 254 byte one is refused", command_length),
            ("SI-071: N creates a D64 image", format_image),
            ("SI-103: UJ closes the channels and U+shifted J returns to the root, and the drive still answers", resets),
            ("SI-074: R renames a subdirectory", rename_directory),
            ("SI-144, SI-144c: a P00 file lists, loads, renames with its host file and scratches under the name in its header", x00_read),
            ("SI-144c: a rename to a host name that differs in case only keeps the host file", x00_rename_case),
            ("a file open for writing is not deleted from under the drive", delete_open_file),
            ("SI-103b: a reset drops a command that was still being received", reset_drops_partial_command),
            ("SI-102a: a file opened for writing before W-1 writes nothing more", write_protect_open_channel),
            ("SI-075: relative files of two record lengths are not copied", copy_record_lengths),
            ("a disk image with a file open for writing inside is not deleted", delete_open_image),
            ("SI-144c: a refused rename of a wrapped file leaves its header", x00_rename_open),
            ("SI-064: R-H refuses a directory a trailing dot and a taken name", rename_header_checks),
            ("SI-077: XL and XU lock and unlock nothing", settings_x_lock),
            ("SI-084: a relative file in sd2iec's one byte layout reads its records", rel_layouts),
    ):
        try:
            with check(label):
                action()
        except Failure as exc:
            failed.append(str(exc))
            agent.call(4, channel=3)
            # A check that fails between moving the drive and moving it back
            # would otherwise fail every check after it too.
            restore_bus_number(api, agent)
    if failed:
        raise Failure(f"{len(failed)} compatibility checks failed")


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


def check_partition_commands(agent):
    """#877 in its programmatic form, and the partition commands that carry a byte."""
    # CMD DOS has two spellings of Change Partition: "CPn" with the number in ASCII,
    # and "C" followed by a shifted P and the number as a byte. The byte can be 13,
    # which is the same carriage return BASIC appends, so a drive that drops the
    # terminator without looking at the command loses the parameter.
    # SI-017 and SI-034: the binary Change Partition keeps a parameter byte of 13, and a
    # partition that is not there answers 77.
    with check("C<shift-P> reads a partition number of 13 as a number"):
        response = agent.command(bytes([ord("C"), 0xD0, 13]), allowed=(77,))
        detail(response)
        if not response.startswith("77,SELECTED PARTITION ILLEGAL,13"):
            raise Failure(f"Selecting partition 13 answered {response!r}")
        response = agent.command(bytes([ord("C"), 0xD0, 1]), allowed=(2,))
        if not response.startswith("02,PARTITION SELECTED,01"):
            raise Failure(f"Selecting partition 1 answered {response!r}")

    # G-P answers with thirty bytes and a carriage return: the type, a reserved byte,
    # the partition number and the name the partition directory shows.
    with check("G-P reports the type, the number and the name of the partition"):
        info = agent.command_reply(b"G-P" + bytes([1]), 40)
        detail(f"{len(info)} bytes, type {info[0]}, partition {info[2]}, name {info[3:19]!r}")
        if len(info) != 31 or info[30] != 13:
            raise Failure(f"G-P answered {len(info)} bytes ending {info[-1:]!r}, expected 31 ending in a carriage return")
        if info[0] != 1 or info[1] != 0:
            raise Failure(f"G-P reports type {info[0]} in byte 0 and {info[1]} in the reserved byte 1, expected 1 and 0")
        if info[2] != 1:
            raise Failure(f"G-P reports partition {info[2]}, expected 1")
        name = info[3:19].rstrip(b"\xa0").decode("ascii", "replace")
        if b"\0" in info[3:19]:
            raise Failure(f"G-P pads the partition name with zero bytes, not shifted spaces: {info[3:19]!r}")
        if name.startswith("/"):
            raise Failure(f"G-P names the partition by its path, {name!r}")


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
    switched_off = []
    try:
        switched_off = switch_off_drives_at(api, (9, 12))
        api.configs.set("SoftIEC Drive Settings", "Soft Drive Bus ID", 11)
        api.configs.set("SoftIEC Drive Settings", "IEC Drive", "Enabled")
        agent.start()
        started = True
        agent.call(1, channel=15)
        # The error channel still holds whatever the last program left there, which after
        # an aborted run on a firmware without these fixes can be any error.
        agent.status(tuple(range(100)))
        agent.command("CD//")
        root = partition_path(api)
        original_path = restorable_path(api, original_path, root)
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
                ("image lock", lambda: check_image_lock(agent, api, args.password, folder, image)),
                ("time stamp filter", lambda: check_timestamp_filter(agent, api, args.password, folder, root)),
                ("listing layout", lambda: check_listing_layout(agent, folder)),
                ("listing end of file", lambda: check_listing_eof(agent, folder)),
                ("load", lambda: check_load(agent, api, args.password, folder, root)),
                ("partition directory", lambda: check_partition_directory(agent, api)),
                ("partition commands", lambda: check_partition_commands(agent)),
                ("compatibility specification", lambda: check_compatibility(agent, api, args.password, folder, root))):
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
                restore_bus_number(api, agent)
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
                              ("switch the drives on the checks' devices back on",
                               lambda: [api.drives.on(slot) for slot in switched_off]),
                              ("return the C64 to BASIC", lambda: api.machine.reset(force=True))):
            ok = teardown_step(label, action) and ok
        if not ok:
            raise Failure("Hardware test cleanup incomplete")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser)
    kernal.add_arguments(parser)
    args = parser.parse_args()
    try:
        with kernal.selected(UltimateApi(args.host, args.password, args.timeout), args, args.password):
            run(args)
    except Exception as exc:
        traceback.print_exc()
        suite_fail(SUITE, str(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
