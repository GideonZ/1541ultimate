#!/usr/bin/env python3
"""Run generated 1 MiB and 2 MiB Megabyter self-test cartridges through REST.

The test invokes ``software/6502/megabyter/build.sh``. The generated program
checks UCI is initially hidden,
unlocks it with $D038=$AB/$D036=$CD, verifies $DF1D=$C9, checks every bank,
then reports green border ($D020=$05) and debug register ($D7FF=$00).
"""

import argparse
import os
import subprocess
import sys
import time
from typing import Tuple

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tests", "lib"))

from api import UltimateApi  # noqa: E402
from report import Failure, check, detail, format_exception, suite_fail, suite_ok  # noqa: E402


BANK_BYTES = 0x2000
BORDER = 0xD020
DEBUG = 0xD7FF
GREEN = 0x05
BEFORE_BORDER = 0x02
BEFORE_DEBUG = 0xA5


def generated_megabyter_crt(banks: int) -> bytes:
    """Build a UCI-enabled Megabyter self-test image using its real generator."""
    if banks not in (128, 256):
        raise ValueError(f"Megabyter test needs 128 or 256 banks, not {banks}")
    megabyter_dir = os.path.join(ROOT, "software", "6502", "megabyter")
    mode = "1m" if banks == 128 else ""
    binary_name = "megabyter_1m.bin" if mode else "megabyter.bin"
    cartridge_name = "megabyter_1m.crt" if mode else "megabyter.crt"
    # The `vice` mode explicitly defines SKIP_UCI_TEST=1 and is never used.
    # `1m` retains the source default SKIP_UCI_TEST=0; no argument produces
    # the standard 2 MiB UCI-enabled hardware image.
    subprocess.run(
        ["./build.sh"] + ([mode] if mode else []),
        check=True, cwd=megabyter_dir, capture_output=True, text=True,
    )
    with open(os.path.join(megabyter_dir, binary_name), "rb") as generated:
        rom = generated.read()
    expected_rom_bytes = banks * BANK_BYTES
    if len(rom) != expected_rom_bytes:
        raise Failure(f"generated ROM is {len(rom)} bytes, expected {expected_rom_bytes}")
    # Require assembled bytes for the real UCI check, not merely a source-level
    # configuration assumption.
    for description, opcode in (
        ("UCI unlock writes", bytes.fromhex("A9AB8D38D0A9CD8D36D0")),
        ("UCI $DF1D signature check", bytes.fromhex("AD1DDFC9C9")),
    ):
        if opcode not in rom:
            raise Failure(f"generated ROM is missing {description}")
    with open(os.path.join(megabyter_dir, cartridge_name), "rb") as generated:
        image = generated.read()
    expected_crt_bytes = 0x40 + banks * (0x10 + BANK_BYTES)
    if len(image) != expected_crt_bytes:
        raise Failure(f"generated CRT is {len(image)} bytes, expected {expected_crt_bytes}")
    return image


def wait_for_result(device: UltimateApi, timeout: float) -> Tuple[int, int]:
    deadline = time.monotonic() + timeout
    observed = (None, None)
    while time.monotonic() < deadline:
        border = device.machine.readmem(BORDER, 1)[0]
        debug = device.machine.readmem(DEBUG, 1)[0]
        observed = (border, debug)
        # The VIC-II colour registers only implement four bits.  The upper
        # nibble of a DMA read is open bus (for example, writing $02 returns
        # $F2 on this U64), so only the low nibble is an observable colour.
        if border & 0x0F == GREEN and debug == 0:
            return observed
        time.sleep(0.10)
    return observed  # type: ignore[return-value]


def run_cartridge(device: UltimateApi, banks: int, timeout: float) -> None:
    size_mib = banks * BANK_BYTES // (1024 * 1024)
    image = generated_megabyter_crt(banks)
    detail(f"generated {size_mib} MiB Megabyter CRT: {len(image)} bytes, {banks} banks; UCI enabled")
    # These writes use machine:writemem, so success must be caused by the
    # cartridge rather than inherited state.  The reads below use that same
    # DMA REST route, not a menu screenshot or an FPGA-side shortcut.
    device.machine.writemem(BORDER, bytes((BEFORE_BORDER,)), idempotent=True)
    device.machine.writemem(DEBUG, bytes((BEFORE_DEBUG,)), idempotent=True)
    before = (device.machine.readmem(BORDER, 1)[0], device.machine.readmem(DEBUG, 1)[0])
    if before[0] & 0x0F != BEFORE_BORDER or before[1] != BEFORE_DEBUG:
        raise Failure(f"could not seed DMA observables: got ${before[0]:02X}, ${before[1]:02X}")

    code, _, body = device.runners.upload("run_crt", image)
    if code != 200:
        raise Failure(f"runners:run_crt for {size_mib} MiB returned HTTP {code}: {body[:160]!r}")
    border, debug = wait_for_result(device, timeout)
    if debug != 0:
        raise Failure(f"{size_mib} MiB cartridge left debug register ${debug:02X}, expected $00")
    if border & 0x0F != GREEN:
        raise Failure(f"{size_mib} MiB cartridge left border ${border:02X}, expected green low nibble $05")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float, default=float(os.environ.get("U64_TIMEOUT", "30")))
    args = parser.parse_args()
    device = UltimateApi(args.host, args.password, args.timeout)
    try:
        failures = []
        for banks in (128, 256):
            size_mib = banks * BANK_BYTES // (1024 * 1024)
            try:
                with check(f"a generated {size_mib} MiB UCI Megabyter cartridge runs via REST"):
                    run_cartridge(device, banks, args.timeout)
            except Failure as exc:
                failures.append(f"{size_mib} MiB: {exc}")
        if failures:
            raise Failure("; ".join(failures))
    except Failure as exc:
        suite_fail("megabyter_cartridge_test", format_exception(exc))
        return 1
    finally:
        take_the_cartridge_off(device)
    suite_ok("megabyter_cartridge_test")
    return 0


def take_the_cartridge_off(device: UltimateApi) -> None:
    """Leave the machine at BASIC rather than holding the cartridge.

    `runners:run_crt` maps a cartridge that survives `machine:reset`, because a
    reset boots the machine and the machine boots the cartridge. Only
    `machine:reboot` takes it off. The runner's own state gate between suites
    resets, so without this the next suite meets a C64 that never reaches the
    BASIC prompt and fails for a reason that has nothing to do with it.

    Measured: after this suite ran, the next run's `readmem-writemem` and
    `input` suites failed with "BASIC READY prompt not visible; device may be
    running a cartridge", and one `machine:reboot` cleared it.

    In a `finally`, and it swallows what it cannot do, because a suite that
    already has a verdict must not lose it to its own tidying.
    """
    try:
        device.machine.reboot()
        device.machine.wait_until_ready(timeout=15.0)
    except Exception as exc:  # noqa: BLE001
        detail(f"could not take the cartridge off the machine: {exc}")


if __name__ == "__main__":
    raise SystemExit(main())
