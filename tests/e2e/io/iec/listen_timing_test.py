#!/usr/bin/env python3
"""Everything the Software IEC drive receives on the bus, at the edges of the bus timing.

A CMD FD or HD raises ATN for UNLISTEN about 40 us after the last byte is acknowledged
(#933), the KERNAL later than that, and the protocol sets no lower limit at all,
so the drive has to have taken a byte by the time it acknowledges it. iec_talker.asm sends
from the C64's own code, with the pause before UNLISTEN, the bit pace and the EOI chosen
here, and every check reads back over FTP or the drive list what the drive made of it:

  - commands on channel 15, with every pause from none to 500 us and at both bit paces;
  - files written in one session, from 1 to 254 bytes, with and without a pause;
  - files written in several sessions, each ending with EOI and an immediate UNLISTEN;
  - files written in sessions without EOI, which end with UNLISTEN alone.

Needs Software IEC on device 11 with one partition, numbered 1, and a C64 with any KERNAL.
"""
import argparse
import io
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
from iec_agent import OPEN, Agent, Talker, iec_drive, restorable_path  # noqa: E402
from report import Failure, check, detail, section, suite_fail, suite_ok, teardown_step  # noqa: E402

SUITE = "listen_timing_test"
DEVICE = 11
# Pauses before UNLISTEN in the talker's 5 us steps, on top of the about 45 us its own code
# takes, so no pause is about a CMD drive's own.
PAUSES = (0, 2, 6, 12, 20, 40, 100)
CHANNEL = 2


def payload(size, seed):
    """Bytes that are not text, so a lost, doubled or swapped byte shows."""
    return bytes((i * 37 + seed * 11 + 13) & 0xFF for i in range(size))


def host_file(client, directory, name):
    """The contents of the file the drive wrote for the CBM `name`, found by its stem."""
    matches = [n for n in ftp.names(client, directory)
               if n.lower() == name.lower() or n.lower().startswith(name.lower() + ".")]
    if len(matches) != 1:
        raise Failure(f"{directory} holds {matches} for {name!r}, expected one file")
    out = io.BytesIO()
    client.retrbinary(f"RETR {directory}/{matches[0]}", out.write)
    return out.getvalue()


def require_equal(actual, expected, context):
    if actual != expected:
        first = next((i for i, (a, b) in enumerate(zip(actual, expected)) if a != b), None)
        raise Failure(f"{context}: got {len(actual)} bytes, expected {len(expected)}; "
                      f"first difference at {first}")


def require_ok(api, context):
    status = iec_drive(api)["last_error"]
    if not status.startswith("00,"):
        raise Failure(f"{context}: the drive answers {status!r}")


def flush_command_channel(talker):
    """End whatever channel 15 holds, with a carriage return, EOI and a KERNAL-like pause.

    A command whose last byte or EOI was lost stays in the drive's command buffer and is
    run in front of the next one, so without this one lost byte would fail every later
    check as well.
    """
    talker.send(DEVICE, 0x6F, b"\r", pause_steps=40)


def write_file(talker, name, sessions, eoi=True, short_bits=False, pause_steps=0):
    """OPEN, the data in `sessions` one LISTEN each, CLOSE, all from the talker."""
    talker.send(DEVICE, 0xF0 | CHANNEL, f"{name},P,W".encode("ascii"),
                short_bits=short_bits, pause_steps=pause_steps)
    for data in sessions:
        talker.send(DEVICE, 0x60 | CHANNEL, data, eoi=eoi, short_bits=short_bits,
                    pause_steps=pause_steps)
    talker.send(DEVICE, 0xE0 | CHANNEL, short_bits=short_bits, pause_steps=pause_steps)


def run(args):
    api = UltimateApi(args.host, args.password, args.timeout)
    agent = Agent(api)
    drives = {name: value for entry in api.rest.json("/v1/drives")["drives"]
              for name, value in entry.items()}
    if any(d.get("enabled") and d.get("bus_id") == DEVICE
           for name, d in drives.items() if name != "IEC Drive"):
        raise Failure(f"Device {DEVICE} is already in use")
    partitions = drives["IEC Drive"]["partitions"]
    if len(partitions) != 1 or partitions[0]["id"] != 1:
        raise Failure("This test requires one Software IEC partition, numbered 1")
    original_path = partitions[0]["path"]
    saved = Snapshot(args.host, {"SoftIEC Drive Settings": api.configs.category("SoftIEC Drive Settings")})
    folder = "lt" + uuid.uuid4().hex[:8]
    root = None
    directory = None
    started = False
    talker = Talker(api)
    try:
        api.configs.set("SoftIEC Drive Settings", "Soft Drive Bus ID", DEVICE)
        api.configs.set("SoftIEC Drive Settings", "IEC Drive", "Enabled")
        agent.start()
        started = True
        agent.call(OPEN, channel=15)
        agent.command("UI", allowed=(73,))
        agent.command("CD//")
        root = iec_drive(api)["partitions"][0]["path"]
        original_path = restorable_path(api, original_path, root)
        directory = f"{root.rstrip('/')}/{folder}"
        with ftp.session(args.host, args.password) as client:
            client.mkd(directory)
        agent.command("CD//" + folder.upper())
        talker.start()
        failed = []

        def attempt(label, action):
            try:
                with check(label):
                    action()
            except Failure:
                failed.append(label)

        section("commands")
        for short_bits in (False, True):
            pace = "KERNAL" if short_bits else "CMD"
            for pause in PAUSES:
                name = f"C{pause:03d}{pace[0]}"

                def make_directory(name=name, pause=pause, short_bits=short_bits):
                    flush_command_channel(talker)
                    talker.send(DEVICE, 0x6F, f"MD:{name}".encode("ascii"),
                                short_bits=short_bits, pause_steps=pause)
                    with ftp.session(args.host, args.password) as client:
                        found = [n for n in ftp.names(client, directory) if n.upper() == name]
                    if not found:
                        raise Failure(f"no directory {name} after MD:{name}")
                    require_ok(api, f"MD:{name}")
                attempt(f"MD with {pause * 5} us more before UNLISTEN, {pace} bit pace", make_directory)

        section("one session")
        cases = [(size, 0, False) for size in (1, 2, 3, 127, 254)]
        cases += [(size, 0, True) for size in (1, 254)]
        cases += [(size, 100, False) for size in (1, 254)]
        for number, (size, pause, short_bits) in enumerate(cases):
            name = f"S{number:02d}"
            data = payload(size, number)

            def one_session(name=name, data=data, pause=pause, short_bits=short_bits):
                write_file(talker, name, [data], short_bits=short_bits, pause_steps=pause)
                with ftp.session(args.host, args.password) as client:
                    require_equal(host_file(client, directory, name), data, name)
                require_ok(api, name)
            attempt(f"write {size} bytes in one session, {pause * 5} us more before UNLISTEN, "
                    f"{'KERNAL' if short_bits else 'CMD'} bit pace", one_session)

        section("several sessions")
        def several(name, sessions, eoi):
            write_file(talker, name, sessions, eoi=eoi)
            with ftp.session(args.host, args.password) as client:
                require_equal(host_file(client, directory, name), b"".join(sessions), name)
            require_ok(api, name)
        attempt("write 600 bytes in three sessions, each with EOI and UNLISTEN at once",
                lambda: several("M00", [payload(200, 40 + n) for n in range(3)], True))
        attempt("write 200 bytes in two sessions without EOI, ended by UNLISTEN at once",
                lambda: several("M01", [payload(100, 50 + n) for n in range(2)], False))
        if failed:
            raise Failure(f"{len(failed)} checks failed: {'; '.join(failed)}")
        return True
    finally:
        def restore_iec():
            if started:
                agent.start()
                agent.call(OPEN, channel=15)
                agent.command("UI", allowed=(73,))
                if root is not None:
                    agent.command("CD//" + original_path[len(root):].upper())
        def restore_config():
            _, refused = saved.restore(api)
            if refused:
                raise Failure(f"Settings restoration refused: {refused}")
        def remove_fixtures():
            if directory is None:
                return
            with ftp.session(args.host, args.password) as client:
                for name in ftp.names(client, directory):
                    try:
                        client.delete(f"{directory}/{name}")
                    except Exception:  # noqa: BLE001
                        client.rmd(f"{directory}/{name}")
                client.rmd(directory)
        cleanup_ok = True
        for label, action in (("restore IEC working directory", restore_iec),
                              ("restore drive settings", restore_config),
                              ("remove only this run's fixtures", remove_fixtures),
                              ("return C64 to BASIC", lambda: api.machine.reset(force=True))):
            cleanup_ok = teardown_step(label, action) and cleanup_ok
        if not cleanup_ok:
            raise Failure("Hardware test cleanup incomplete")
        detail(f"fixtures were in {folder}")


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
