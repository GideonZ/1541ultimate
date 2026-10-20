#!/usr/bin/env python3
"""#999: C64 OS directory rename commands over IEC, on host folders and DNP."""
import argparse
import sys
import uuid
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli
import ftp
import kernal
from api import UltimateApi, ensure_cartridge_preference
from config_snapshot import Snapshot
from dos_command_test import partition_path
from iec_agent import Agent, OPEN, WRITE, CLOSE, iec_drive, restorable_path
from report import Failure, detail


def run(args):
    api = UltimateApi(args.host, args.password, args.timeout)
    category = "C64 and Cartridge Settings"
    saved_interface = api.configs.get(category, "Command Interface")
    saved = Snapshot(args.host, {"SoftIEC Drive Settings": api.configs.category("SoftIEC Drive Settings")})
    original = partition_path(api)
    partitions = iec_drive(api)["partitions"]
    if len(partitions) != 1 or partitions[0]["id"] != 1:
        raise Failure("Requires one Software IEC partition numbered 1")
    agent = Agent(api)
    folder = "rename" + uuid.uuid4().hex[:8]
    root = directory = None
    started = False
    errors = []
    try:
        api.configs.set(category, "Command Interface", "Disabled")
        api.configs.set("SoftIEC Drive Settings", "Soft Drive Bus ID", 11)
        api.configs.set("SoftIEC Drive Settings", "IEC Drive", "Enabled")
        agent.start()
        started = True
        agent.call(OPEN, channel=15)
        agent.status(range(100))
        agent.command("CD//")
        root = partition_path(api)
        original = restorable_path(api, original, root)
        directory = f"{root.rstrip('/')}/{folder}"
        with ftp.session(api.host, args.password) as client:
            client.mkd(directory)
        api.files.create_dnp(f"{directory}/native.dnp", tracks=4, diskname="NATIVE")
        detail(str(api.rest.json("/v1/info")))
        for backing in (folder, folder + "/native.dnp"):
            agent.command("CD//" + backing.upper())
            for command in ("MD:NEW DIRECTORY", "R:.^RNAME^=NEW DIRECTORY",
                            "R:NEW NAME=.^RNAME^", "CD:NEW NAME", "R-H:NEW NAME", "CD_"):
                response = agent.command(command + "\r", allowed=range(100))
                detail(f"{backing}: {command} -> {response}")
                if not response.startswith("00,"):
                    errors.append(f"{backing}: {command}: {response}")
                elif command == "MD:NEW DIRECTORY":
                    agent.call(OPEN, channel=2, data=b"/NEW DIRECTORY/:KEEP,S,W")
                    agent.status()
                    agent.call(WRITE, channel=2, data=b"payload")
                    agent.call(CLOSE, channel=2)
                    agent.status()
                elif command == "CD:NEW NAME":
                    agent.call(OPEN, channel=2, data=b"KEEP,S,R")
                    agent.status()
                    if agent.read_stream(channel=2) != b"payload":
                        raise Failure("The renamed directory lost its file")
                    agent.call(CLOSE, channel=2)
                elif command == "R-H:NEW NAME" and not errors:
                    agent.call(OPEN, channel=5, secondary=0, data=b"$")
                    listing = agent.read_stream(channel=5)
                    agent.call(CLOSE, channel=5)
                    if listing[8:24] != b"NEW NAME        ":
                        raise Failure(f"Wrong directory header: {listing[8:24]!r}")
            agent.command("CD//")
    finally:
        try:
            if started and root:
                for channel in (2, 5):
                    agent.call(CLOSE, channel=channel)
                agent.command("CD//" + original[len(root):].upper())
                agent.call(CLOSE, channel=15)
            if directory:
                with ftp.session(api.host, args.password) as client:
                    if not ftp.remove_tree(client, directory):
                        raise Failure("Could not remove fixtures")
        finally:
            _, refused = saved.restore(api)
            api.configs.set(category, "Command Interface", saved_interface)
            api.machine.reset(force=True)
            if refused:
                raise Failure(f"Could not restore settings: {refused}")
    if errors:
        raise Failure("; ".join(errors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser)
    kernal.add_arguments(parser)
    args = parser.parse_args()
    api = UltimateApi(args.host, args.password, args.timeout)
    computer = UltimateApi(api.target.computer, args.password) if api.target.split else None
    preference = None
    try:
        if computer:
            preference = computer.configs.get(kernal.CATEGORY, "Cartridge Preference")
            detail(ensure_cartridge_preference(api.target, args.password, args.timeout))
        with kernal.selected(api, args, args.password):
            run(args)
    finally:
        if computer and preference is not None:
            computer.configs.set(kernal.CATEGORY, "Cartridge Preference", preference)
            kernal._boot(computer)


if __name__ == "__main__":
    main()
