"""Run a suite under a KERNAL of the caller's choosing.

`--kernal` names the KERNAL image the C64 runs for the suite: a file the device
already lists in its ROM directory, or a local file, which is uploaded there
for the run and removed afterwards. `--command-interface` enables the Ultimate
Command Interface, which the UCI ("hyperspeed") KERNAL built from roms/c64rom
needs to reach the Software IEC drive; without it that KERNAL uses the serial
bus like the stock one.

The image is checked against what the C64 has at $E000 once it has booted, so
a run that asked for a KERNAL cannot pass while the machine ran another. The
settings are put back and the machine rebooted when the suite ends. For a
cartridge target the KERNAL is the computer's; see selected().
"""
import argparse
import ftplib
import io
import os
import sys
from contextlib import contextmanager

import ftp
from api import UltimateApi
from report import Failure, detail, warn

CATEGORY = "C64 and Cartridge Settings"
# A computer names the image it loads "Kernal ROM"; a cartridge, which can only
# replace the computer's own, names it "Alternate Kernal" (software/io/c64/c64.cc).
KERNAL_ITEMS = ("Kernal ROM", "Alternate Kernal")
COMMAND_INTERFACE_ITEM = "Command Interface"
# The firmware's ROMS_DIRECTORY, as the FTP server spells it.
ROMS_DIRECTORY = "/Flash/roms"
KERNAL_ADDRESS = 0xE000
KERNAL_SIZE = 8192
# With Fast Reset enabled the firmware replaces the KERNAL's RAM test with a short
# one, where the image carries the stock test (fastresetOrg and fastresetPatch in
# software/io/c64/c64.cc).
FAST_RESET_ITEM = "Fast Reset"
FAST_RESET_OFFSET = 0x1D6C
FAST_RESET_ORIGINAL = bytes((
    0xE6, 0xC2, 0xB1, 0xC1, 0xAA, 0xA9, 0x55, 0x91, 0xC1, 0xD1, 0xC1, 0xD0, 0x0F, 0x2A, 0x91, 0xC1,
    0xD1, 0xC1, 0xD0, 0x08, 0x8A, 0x91, 0xC1, 0xC8, 0xD0, 0xE8, 0xF0, 0xE4, 0x98, 0xAA, 0xA4, 0xC2,
    0x18, 0x20, 0x2D, 0xFE, 0xA9, 0x08, 0x8D, 0x82, 0x02, 0xA9, 0x04, 0x8D, 0x88, 0x02, 0x60))
FAST_RESET_PATCH = bytes((
    0xA2, 0x00, 0xA0, 0xA0, 0xAD, 0x00, 0x80, 0x49, 0xFF, 0x8D, 0x00, 0x80, 0xCD, 0x00, 0x80, 0xF0,
    0x02, 0xA0, 0x80, 0x4C, 0x8C, 0xFD))
# A reboot reloads the ROM images before the KERNAL starts, which takes longer
# than the reset the READY wait is sized for.
BOOT_SECONDS = 15.0


def add_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--kernal", default="", metavar="IMAGE",
        help="Run under this KERNAL: a file name in the device's ROM directory, or a "
             "local file uploaded there for the run (default: the device's own)")
    parser.add_argument(
        "--command-interface", action="store_true",
        help="Enable the Ultimate Command Interface for the run, which the UCI "
             "(hyperspeed) KERNAL needs to reach the Software IEC drive")


def _boot(api) -> None:
    """Reboot, which is what reloads the ROM images, and wait for BASIC."""
    api.machine.writemem(0x0400, bytes([0x20]) * 40)
    api.machine.reboot()
    if not api.machine.wait_until_ready(BOOT_SECONDS):
        raise Failure("the C64 did not reach READY after a reboot")


def _as_loaded(image: bytes, fast_reset: bool) -> bytes:
    """The image as the firmware puts it in the C64's memory."""
    at = FAST_RESET_OFFSET
    if fast_reset and image[at:at + len(FAST_RESET_ORIGINAL)] == FAST_RESET_ORIGINAL:
        return image[:at] + FAST_RESET_PATCH + image[at + len(FAST_RESET_PATCH):]
    return image


def _image(api, password, kernal: str) -> tuple[str, bytes, bool]:
    """The image's name on the device, its contents, and whether it was uploaded."""
    with ftp.session(api.host, password) as client:
        present = ftp.names(client, ROMS_DIRECTORY)
        if os.path.isfile(kernal):
            name = os.path.basename(kernal)
            with open(kernal, "rb") as handle:
                data = handle.read()
            if name in present:
                on_device = ftp.retrieve(client, f"{ROMS_DIRECTORY}/{name}")
                if on_device != data:
                    raise Failure(f"{ROMS_DIRECTORY}/{name} on the device is not {kernal}; "
                                  "rename the local file rather than replace the device's")
                return name, data, False
            try:
                client.storbinary(f"STOR {ROMS_DIRECTORY}/{name}", io.BytesIO(data))
            except ftplib.all_errors as exc:
                raise Failure(f"uploading {kernal} to {ROMS_DIRECTORY} failed: {exc}") from exc
            return name, data, True
        if kernal not in present:
            raise Failure(f"{kernal} is neither a local file nor in the device's "
                          f"{ROMS_DIRECTORY}, which holds {', '.join(sorted(present))}")
        return kernal, ftp.retrieve(client, f"{ROMS_DIRECTORY}/{kernal}"), False


@contextmanager
def selected(api, args, password=None):
    """Run the body under the KERNAL and Command Interface state `args` asks for.

    The KERNAL belongs to the computer: measured on an Ultimate II+L in a C64
    Ultimate, the cartridge's Alternate Kernal left the computer's own KERNAL
    running. The Command Interface belongs to the device under test, because
    that is what serves the UCI registers.
    """
    if not args.kernal and not args.command_interface:
        yield
        return
    handle = api.target
    computer = UltimateApi(handle.computer, password) if handle.split else api
    kernal_settings = computer.configs.category(CATEGORY)
    device_settings = api.configs.category(CATEGORY) if handle.split else kernal_settings
    item = next((name for name in KERNAL_ITEMS if name in kernal_settings), None)
    saved_kernal = {}
    saved_device = {}
    uploaded = None
    try:
        if args.kernal:
            if item is None:
                raise Failure(f"{CATEGORY} has no KERNAL setting on {computer.host}")
            name, data, fresh = _image(computer, password, args.kernal)
            if len(data) != KERNAL_SIZE:
                raise Failure(f"{args.kernal} holds {len(data)} bytes, a KERNAL {KERNAL_SIZE}")
            uploaded = name if fresh else None
            saved_kernal[item] = kernal_settings[item]
            computer.configs.set(CATEGORY, item, name)
        if args.command_interface:
            if COMMAND_INTERFACE_ITEM not in device_settings:
                raise Failure(f"{CATEGORY} has no {COMMAND_INTERFACE_ITEM} setting on {api.host}")
            saved_device[COMMAND_INTERFACE_ITEM] = device_settings[COMMAND_INTERFACE_ITEM]
            api.configs.set(CATEGORY, COMMAND_INTERFACE_ITEM, "Enabled")
        if args.kernal:
            _boot(computer)
            running = computer.machine.readmem(KERNAL_ADDRESS, KERNAL_SIZE)
            expected = _as_loaded(data, kernal_settings.get(FAST_RESET_ITEM) == "Enabled")
            differ = sum(a != b for a, b in zip(running, expected))
            if differ:
                raise Failure(f"the C64 is not running {name}: {differ} of {KERNAL_SIZE} "
                              f"bytes at ${KERNAL_ADDRESS:04X} differ from it")
            detail(f"KERNAL {name} on {computer.host}, checked at ${KERNAL_ADDRESS:04X}")
        if args.command_interface:
            detail(f"Command Interface enabled on {api.host}")
        yield
    finally:
        def remove_upload():
            with ftp.session(computer.host, password) as client:
                if not ftp.delete_quietly(client, f"{ROMS_DIRECTORY}/{uploaded}"):
                    warn(f"{ROMS_DIRECTORY}/{uploaded} was uploaded to {computer.host} for the "
                         "run and could not be removed")
        # Each step runs even when one before it failed, so one refusal does not leave the rest.
        errors = []
        for step in ([lambda k=k, v=v: api.configs.set(CATEGORY, k, v) for k, v in saved_device.items()]
                     + [lambda k=k, v=v: computer.configs.set(CATEGORY, k, v) for k, v in saved_kernal.items()]
                     + ([lambda: _boot(computer)] if saved_kernal else [])
                     + ([remove_upload] if uploaded else [])):
            try:
                step()
            except Exception as exc:  # noqa: BLE001
                errors.append(exc)
        if errors and sys.exc_info()[0] is None:
            raise Failure(f"restoring the KERNAL settings failed: {errors[0]}") from errors[0]
        for exc in errors:
            warn(f"restoring the KERNAL settings failed: {exc}")
