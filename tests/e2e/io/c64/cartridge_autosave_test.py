#!/usr/bin/env python3
"""E2E: a cartridge the C64 has written to is saved back to the file it came from.

A game that saves into its cartridge (EasyFlash through EAPI) only changes the
image in memory. Until the user runs *Save Cartridge* by hand and confirms an
overwrite, the file on disk still holds the old state, and forgetting that
loses the save. The firmware now hashes the cartridge when the menu opens and,
depending on *Memory Configuration -> Save Changed Cartridge*, offers to write
it back to its source file or does so silently.

The suite builds its own EasyFlash cartridge (CRT type 32) rather than shipping
a ROM. Its bank 0 is entered in Ultimax mode, copies a routine to $0800 and
runs it there; the routine programs one byte into the ROM window whenever the
harness asks for one, the same way the firmware's own EAPI does it. That gives
the suite a cartridge that changes on demand, which is what every scenario
below needs.

Checked here:

- Ask: the prompt names the source file, Yes writes it, and the file as it was
  first loaded is kept once beside it as NAME.crt.bak.
- The saved file carries the cartridge's own EAPI, not the Ultimate's. The
  firmware patches 768 bytes of EAPI into cartridge memory while a cartridge
  runs, and a save that swapped them in place would both hand a running C64
  foreign code and write the wrong bytes to disk.
- An unchanged cartridge produces no prompt, so the baseline is renewed by a
  save rather than asking again at the next menu open.
- Off: no prompt and no write, however much the C64 changed.
- Auto: written with no prompt at all.
- A cartridge whose source cannot be written back, such as one uploaded over
  REST and run from /Temp, says so once instead of prompting.

The writing scenarios need a writable medium on the device (an SD card or a USB
stick); they report SKIP without one. The /Temp scenario runs either way.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import cli                                                      # noqa: E402
import ftp as ftp_lib                                           # noqa: E402
import menu as menu_lib                                         # noqa: E402
import wait                                                     # noqa: E402
from api import UltimateApi                                     # noqa: E402
from assembler import assemble                                  # noqa: E402
from report import (Failure, check, check_ok, check_skip,       # noqa: E402
                    check_start, detail, format_exception, section,
                    suite_fail, suite_ok, teardown_step)

sys.path.insert(0, bootstrap.directory("e2e", "io", "c64"))
from easyflash_cartridge_test import chip                       # noqa: E402

SUITE = "cartridge_autosave_test"

SOURCE = Path(__file__).resolve().parent / "ef_flash_write.asm"

CONFIG_CATEGORY = "C64 and Cartridge Settings"   # the store; the menu groups it under Memory Configuration
CONFIG_ITEM = "Save Changed Cartridge"

CRT_NAME = "autosave.crt"           # short: the prompt prints the name, truncated to 28

# Addresses the stimulus and the harness share; see ef_flash_write.asm.
COMMAND = 0x0402
OFFSET = 0x0403
ACK = 0x0404
RUNNING = 0x0405

ROUTINE_ADDRESS = 0x0800
ROUTINE_OFFSET = 0x20               # of the routine in bank 0's ROMH chip
MARKER_PAGE = 0x1F00                # $9F00, in the bank 0 ROML chip
EAPI_OFFSET = 0x1800                # of the EAPI in bank 0's ROMH chip (c64_crt.cc:479)
EAPI_LENGTH = 768

# EasyFlash chunks are regenerated on load as 64 banks of ROML and ROMH
# (c64_crt.cc:372-403), and auto_mirror() fills the cartridge memory with
# copies of the image, so a saved file is far larger than the one uploaded
# here and holds bank 0 in its first two chunks.
CHUNK_HEADER = 0x10
CHUNK_DATA = 0x2000

PROMPT = "Cartridge changed. Save it to"
NOT_WRITABLE = "cannot be written back"

ROUTINE_TIMEOUT_SECONDS = 15.0
WRITE_TIMEOUT_SECONDS = 10.0
PROMPT_TIMEOUT_SECONDS = 30.0       # the hash runs before the prompt appears
SAVE_TIMEOUT_SECONDS = 120.0        # a megabyte onto an SD card
# No prompt is a negative: long enough that a device about to raise one has.
QUIET_SECONDS = 8.0

# Where a cartridge can be saved to. The USB volumes are asked for by name
# rather than assumed, because the device names each after the port its medium
# is in (ftp.usb_volumes). /Temp and /Flash are deliberately absent: the
# firmware refuses to write a cartridge back to either (c64_subsys.cc).
SD_ROOT = "/SD"


def boot(length: int) -> bytes:
    """Bank 0 at $E000 in Ultimax mode: copy the routine to RAM and run it."""
    return bytes([
        0x78,                           # E000  SEI
        0xA2, 0xFF,                     # E001  LDX #$FF
        0x9A,                           # E003  TXS
        0xD8,                           # E004  CLD
        0xA2, length - 1,               # E005  LDX #len-1
        0xBD, ROUTINE_OFFSET, 0xE0,     # E007  LDA $E020,X
        0x9D, ROUTINE_ADDRESS & 0xFF, ROUTINE_ADDRESS >> 8,   # E00A  STA $0800,X
        0xCA,                           # E00D  DEX
        0x10, 0xF7,                     # E00E  BPL $E007
        0x4C, ROUTINE_ADDRESS & 0xFF, ROUTINE_ADDRESS >> 8,   # E010  JMP $0800
        0x40,                           # E013  RTI       NMI and IRQ vectors
    ])


def eapi_block() -> bytes:
    """A cartridge's own EAPI: the signature the firmware looks for, and filler.

    The firmware replaces these 768 bytes in cartridge memory with its own EAPI
    (c64_crt.cc:476-485). Nothing here is ever called - the stimulus programs
    the flash directly - so the filler only has to be recognisable in the file
    that comes back, and different from the Ultimate's EAPI.
    """
    block = bytearray(b"eapi")
    block += b"E2E-CARTRIDGE-OWN-EAPI-"
    block += bytes((index * 7 + 1) & 0xFF for index in range(EAPI_LENGTH - len(block)))
    return bytes(block)


def easyflash_crt(routine: bytes) -> bytes:
    """A one-bank type 32 CRT that boots into `routine` and carries an EAPI."""
    header = bytearray(0x40)
    header[0x00:0x10] = b"C64 CARTRIDGE   "
    header[0x10:0x14] = (0x40).to_bytes(4, "big")
    header[0x14:0x16] = b"\x01\x00"
    header[0x16:0x18] = (32).to_bytes(2, "big")     # EasyFlash
    header[0x18] = 1                                # EXROM high, GAME low: Ultimax at start
    header[0x20:0x2D] = b"AUTOSAVE-TEST"

    roml = bytearray(b"\xff" * CHUNK_DATA)
    roml[0:13] = b"AUTOSAVE-ROML"                   # so a chunk can be told apart in the file

    romh = bytearray(b"\xff" * CHUNK_DATA)
    start = boot(len(routine))
    romh[0:len(start)] = start
    romh[ROUTINE_OFFSET:ROUTINE_OFFSET + len(routine)] = routine
    romh[EAPI_OFFSET:EAPI_OFFSET + EAPI_LENGTH] = eapi_block()
    rti = 0xE000 + len(start) - 1
    for vector in (0x1FFA, 0x1FFE):                 # NMI, IRQ
        romh[vector:vector + 2] = rti.to_bytes(2, "little")
    romh[0x1FFC:0x1FFE] = (0xE000).to_bytes(2, "little")      # RESET

    return bytes(header) + chip(0, 0x8000, bytes(roml)) + chip(0, 0xA000, bytes(romh))


def stimulus() -> bytes:
    """The assembled routine, without the .prg load address."""
    return assemble(SOURCE)[2:]


def chunks(image: bytes) -> dict[tuple[int, int], bytes]:
    """The CHIP chunks of a CRT, keyed by (bank, load address)."""
    if not image.startswith(b"C64 CARTRIDGE   "):
        raise Failure("the saved file does not begin with a CRT signature")
    offset = int.from_bytes(image[0x10:0x14], "big")
    found: dict[tuple[int, int], bytes] = {}
    while offset + CHUNK_HEADER <= len(image):
        if image[offset:offset + 4] != b"CHIP":
            raise Failure(f"no CHIP header at offset {offset} of the saved file")
        total = int.from_bytes(image[offset + 4:offset + 8], "big")
        bank = int.from_bytes(image[offset + 10:offset + 12], "big")
        load = int.from_bytes(image[offset + 12:offset + 14], "big")
        size = int.from_bytes(image[offset + 14:offset + 16], "big")
        found[(bank, load)] = image[offset + CHUNK_HEADER:offset + CHUNK_HEADER + size]
        offset += total
    return found


def saved_bank0(image: bytes) -> tuple[bytes, bytes]:
    """Bank 0's ROML and ROMH from a saved cartridge."""
    parsed = chunks(image)
    for key in ((0, 0x8000), (0, 0xA000)):
        if key not in parsed:
            raise Failure(f"the saved file has no bank {key[0]} chunk at ${key[1]:04X}")
    return parsed[(0, 0x8000)], parsed[(0, 0xA000)]


class Device:
    """The device under test, plus everything this suite keeps on it."""

    def __init__(self, args) -> None:
        self.api = UltimateApi(args.host, args.password or None, args.timeout)
        self.host = args.host
        self.password = args.password or None
        self.timeout = args.timeout
        self.image = easyflash_crt(stimulus())
        self.root: str | None = None
        self.previous_mode: str | None = None

    # -- device files ----------------------------------------------------

    def ftp(self):
        return ftp_lib.session(self.host, self.password, self.timeout)

    def find_root(self) -> str | None:
        """The first candidate root a file can actually be written to.

        Decided by writing one, rather than by listing: an empty medium lists
        like one that is not there.
        """
        with self.ftp() as client:
            for root in [SD_ROOT, *ftp_lib.usb_volumes(client)]:
                probe = f"{root}/{CRT_NAME}.probe"
                try:
                    ftp_lib.store(client, probe, b"probe")
                except Failure:
                    continue
                ftp_lib.delete_quietly(client, probe)
                return root
        return None

    def place_cartridge(self) -> str:
        """Upload the cartridge, with nothing of an earlier run left beside it."""
        path = f"{self.root}/{CRT_NAME}"
        with self.ftp() as client:
            for name in (CRT_NAME, CRT_NAME + ".bak", CRT_NAME + ".tmp"):
                ftp_lib.delete_quietly(client, f"{self.root}/{name}")
            ftp_lib.store(client, path, self.image)
        return path

    def retrieve(self, path: str) -> bytes:
        with self.ftp() as client:
            return ftp_lib.retrieve(client, path)

    def retrieve_saved(self, path: str, offset: int, value: int) -> bytes:
        """The file, once it carries `value` at `offset` of bank 0's ROML.

        The firmware writes NAME.tmp and renames it over NAME, so a read taken
        while a save is still running returns the previous file rather than a
        torn one. Waiting for the byte is therefore waiting for the rename.
        """
        saved = [b""]

        def carries_value() -> bool:
            saved[0] = self.retrieve(path)
            return saved_bank0(saved[0])[0][offset] == value

        wait.wait_until(carries_value,
                        f"${value:02X} at ${offset:04X} of bank 0 in the saved file",
                        timeout=SAVE_TIMEOUT_SECONDS, interval=2.0,
                        detail=lambda: f"it reads ${saved_bank0(saved[0])[0][offset]:02X}")
        return saved[0]

    def exists(self, path: str) -> bool:
        with self.ftp() as client:
            directory, _, name = path.rpartition("/")
            return name in ftp_lib.names(client, directory or "/")

    def clean_up(self) -> None:
        if not self.root:
            return
        with self.ftp() as client:
            for name in (CRT_NAME, CRT_NAME + ".bak", CRT_NAME + ".tmp"):
                ftp_lib.delete_quietly(client, f"{self.root}/{name}")

    # -- the running cartridge -------------------------------------------

    def wait_running(self) -> None:
        wait.wait_until(lambda: self.api.machine.readmem(RUNNING, 1)[0] == 0x01,
                        "the cartridge reaches its routine",
                        timeout=ROUTINE_TIMEOUT_SECONDS)

    def program(self, offset: int, value: int) -> None:
        """Have the C64 write one byte into the cartridge, and wait for it."""
        before = self.api.machine.readmem(ACK, 1)[0]
        self.api.machine.writemem(OFFSET, bytes([offset]))
        self.api.machine.writemem(COMMAND, bytes([value]))
        wait.wait_until(lambda: self.api.machine.readmem(ACK, 1)[0] != before,
                        "the cartridge routine acknowledges the write",
                        timeout=WRITE_TIMEOUT_SECONDS)

    # -- the menu --------------------------------------------------------

    def set_menu(self, want_open: bool) -> None:
        """Open or close the menu, with one press of its button.

        menu_lib.toggle_menu presses once and then polls, which is the only
        safe way to drive a toggle: pressing again because the state has not
        changed yet closes a menu that was still on its way open. The menu can
        take longer than one poll to appear, and on this branch the hash runs
        before it does.
        """
        if not menu_lib.toggle_menu(self.api.machine.menu_button,
                                    self.api.machine.menu_open, want_open):
            raise Failure("the menu did not "
                          + ("open" if want_open else "close"))

    def open_menu(self) -> None:
        """Leave the menu freshly open, whatever it held before.

        Backing out with F8 instead leaves the overlay reporting an open menu
        that no user interface object has focus in, and every key sent into
        that goes to the C64 rather than to the firmware.
        """
        self.set_menu(False)
        self.set_menu(True)

    def wait_for_text(self, text: str, timeout: float) -> list[str]:
        wait.wait_until(lambda: any(text in row for row in self.api.machine.menu_rows()),
                        f"{text!r} appears on the menu screen", timeout=timeout,
                        detail=self.screen_text)
        return self.api.machine.menu_rows()

    def screen_text(self) -> str:
        """The screen, for a wait that failed to say what it was looking at."""
        return "\n".join(row.rstrip() for row in self.api.machine.menu_rows()
                          if row.strip())

    def answer(self, key: str, text: str, timeout: float) -> None:
        """Answer the popup carrying `text` with `key`, once.

        The caller has waited for the popup, so the user interface owns the
        keyboard and the key reaches it. A second key would arrive after the
        popup has gone and land in the browser behind it, where RETURN opens
        whatever the cursor is on. A key lost here is a defect to report, not
        one to press through.
        """
        self.api.machine.press(key)
        wait.wait_until(lambda: not any(text in row for row in self.api.machine.menu_rows()),
                        f"{text!r} goes away after {key!r}", timeout=timeout,
                        detail=self.screen_text)

    def expect_quiet(self) -> None:
        """No prompt of this feature's, for as long as one could still appear."""
        deadline = time.monotonic() + QUIET_SECONDS
        while time.monotonic() < deadline:
            rows = self.api.machine.menu_rows()
            for row in rows:
                for text in (PROMPT, NOT_WRITABLE):
                    if text in row:
                        raise Failure(f"the menu offered {text!r} when it should not have")
            time.sleep(0.5)

    def dismiss_prompt(self) -> None:
        """Answer whatever this feature may have left on screen.

        No RETURN: the Save prompt's first button is Yes, so RETURN would save
        the cartridge from the teardown. The notice has Ok alone, which its
        own hotkey answers.
        """
        rows = self.api.machine.menu_rows()
        if any(PROMPT in row for row in rows):
            self.answer("n", PROMPT, PROMPT_TIMEOUT_SECONDS)
        elif any(NOT_WRITABLE in row for row in rows):
            self.answer("o", NOT_WRITABLE, PROMPT_TIMEOUT_SECONDS)

    def close_menu(self) -> None:
        self.api.machine.close_menu_from_anywhere(confirm_key="return")

    # -- configuration ---------------------------------------------------

    def capture_mode(self) -> None:
        self.previous_mode = str(self.api.configs.current(CONFIG_CATEGORY, CONFIG_ITEM))

    def set_mode(self, value: str) -> None:
        self.api.configs.set(CONFIG_CATEGORY, CONFIG_ITEM, value)

    def restore_mode(self) -> None:
        if self.previous_mode is not None:
            self.api.configs.set(CONFIG_CATEGORY, CONFIG_ITEM, self.previous_mode)


def scenario_ask_and_save(device: Device, path: str) -> None:
    section("Ask: the prompt names the file, and Yes writes it back")
    device.set_mode("Ask")
    device.program(0x00, 0xA5)

    with check("the menu offers to save the changed cartridge, naming its file"):
        device.open_menu()
        rows = device.wait_for_text(PROMPT, PROMPT_TIMEOUT_SECONDS)
        if not any(CRT_NAME in row for row in rows):
            for row in rows:
                if row.strip():
                    detail(f"  {row}")
            raise Failure(f"the prompt does not name {CRT_NAME}")

    with check("Yes writes the cartridge back to its own file"):
        device.answer("y", PROMPT, SAVE_TIMEOUT_SECONDS)
        device.close_menu()
        saved = device.retrieve_saved(path, MARKER_PAGE, 0xA5)
        detail(f"{len(saved)} bytes written")

    with check("the file as it was first loaded is kept once, as NAME.crt.bak"):
        backup = device.retrieve(path + ".bak")
        if backup != device.image:
            raise Failure(f"the backup is {len(backup)} bytes and does not match the "
                          f"{len(device.image)} bytes that were uploaded")

    with check("the saved cartridge carries its own EAPI, not the Ultimate's"):
        _, romh = saved_bank0(device.retrieve(path))
        written = romh[EAPI_OFFSET:EAPI_OFFSET + EAPI_LENGTH]
        if written != eapi_block():
            detail(f"the file holds {written[:16].hex(' ')} where the cartridge had "
                   f"{eapi_block()[:16].hex(' ')}")
            raise Failure("the saved file holds the EAPI the firmware patched in")

    with check("an unchanged cartridge is not offered again"):
        device.open_menu()
        device.expect_quiet()
        device.close_menu()


def scenario_off(device: Device, path: str) -> None:
    section("Off: nothing is offered and nothing is written")
    device.set_mode("Off")
    device.program(0x01, 0x5A)

    with check("no prompt appears while the feature is Off"):
        device.open_menu()
        device.expect_quiet()
        device.close_menu()

    with check("the file on disk is left as it was"):
        roml, _ = saved_bank0(device.retrieve(path))
        if roml[MARKER_PAGE + 1] != 0xFF:
            raise Failure("the second byte reached the file although the feature is Off")


def scenario_auto(device: Device, path: str) -> None:
    section("Auto: written back without a prompt")
    device.set_mode("Auto")

    with check("the cartridge is saved with no prompt at all"):
        device.open_menu()
        device.expect_quiet()
        device.close_menu()
        roml, _ = saved_bank0(device.retrieve_saved(path, MARKER_PAGE + 1, 0x5A))
        if roml[MARKER_PAGE] != 0xA5:
            raise Failure("the first byte is no longer in the saved file")

    with check("the backup still holds the file as it was first loaded"):
        backup = device.retrieve(path + ".bak")
        if backup != device.image:
            raise Failure("the backup was overwritten by the second save")


def scenario_not_writable(device: Device) -> None:
    section("A source that cannot be written back says so instead of prompting")
    device.set_mode("Ask")

    with check("a cartridge uploaded over REST runs from a source it cannot write"):
        code, _, body = device.api.runners.upload(
            "run_crt", device.image, timeout=SAVE_TIMEOUT_SECONDS)
        if code != 200:
            raise Failure(f"uploading the cartridge returned HTTP {code}: {body[:160]!r}")
        device.wait_running()
        device.program(0x00, 0x3C)
        device.open_menu()
        device.wait_for_text(NOT_WRITABLE, PROMPT_TIMEOUT_SECONDS)

    with check("the notice is given once, not at every menu open"):
        device.answer("return", NOT_WRITABLE, PROMPT_TIMEOUT_SECONDS)
        device.close_menu()
        device.open_menu()
        device.expect_quiet()
        device.close_menu()


def scenario_reboot(device: Device, path: str) -> None:
    section("A reboot leaves no cartridge, and so nothing to save")
    device.set_mode("Ask")

    with check("a reboot is not reported as a save made by the C64"):
        # Reboot C64 disarms the ROM that was running by writing into its
        # image. Unless the firmware drops the cartridge with it, the next
        # menu open finds that write and offers to save a cartridge the
        # machine no longer has: Yes, or Auto without asking, then writes a
        # file whose $8005 is zero, which for a normal cartridge is inside
        # CBM80 and stops it autostarting.
        device.api.runners.run_crt(path)
        device.wait_running()
        device.api.machine.reboot()
        device.api.machine.wait_until_ready()
        device.open_menu()
        device.expect_quiet()
        device.close_menu()


def run(args) -> None:
    device = Device(args)
    try:
        device.capture_mode()
        device.root = device.find_root()
        check_start("a writable medium to save the cartridge to")
        if device.root is None:
            check_skip("the device serves no SD card and no USB volume to save to")
        else:
            check_ok(device.root)
            path = device.place_cartridge()
            device.api.runners.run_crt(path)
            device.wait_running()
            scenario_ask_and_save(device, path)
            scenario_off(device, path)
            scenario_auto(device, path)
            scenario_reboot(device, path)
        scenario_not_writable(device)
    finally:
        # Before anything else: a check that failed may have left the Save
        # prompt or the notice on screen, and a reboot from there reaches
        # release_host() while the user interface task is still inside the
        # popup the hook opened.
        teardown_step("answer an open prompt", device.dismiss_prompt)
        teardown_step("close the menu", lambda: device.set_menu(False))
        teardown_step("restore Save Changed Cartridge", device.restore_mode)
        teardown_step("remove the test cartridge", device.clean_up)
        # A reboot removes the cartridge; a reset would boot straight back into it.
        teardown_step("restore the configured cartridge", device.api.machine.reboot)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that a cartridge the C64 has written to is saved back to "
                    "the file it came from.")
    cli.add_device_arguments(parser)
    args = parser.parse_args()
    try:
        run(args)
    except Exception as exc:            # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
