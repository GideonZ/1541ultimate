#!/usr/bin/env python3
"""E2E: the published Doom C64U release runs on this machine.

Doom C64U is third-party software, but it is the most demanding public exercise
of the U64 bus that exists: it streams level geometry, textures and sprites out
of a 16 MB REU every frame while the CPU runs at turbo speed. Bitstreams that
pass every other suite here have broken it, so it is worth one check.

Manual, because it downloads a release from github.com and changes machine
settings while it runs. Both are put back.

The automated guard for the same defect is `reu_turbo_test.py`, which runs the
REU sequence the fix was made against and needs no network. Both were measured
red on the core before the fix and green on the core after it, so this suite is
the wider one rather than the necessary one: it exercises a real program's use
of the REU end to end, where the other exercises one transfer sequence.

What "works" means here is the engine's own verdict, evidence that it is
rendering, and a short look at the picture:

- `mapOK` is set by the engine's boot-time check of the REU image: it verifies
  the header and sums each resident block. A REU that answers every transfer
  with the wrong bytes fails here, and that is exactly the failure this suite
  exists to catch.
- `frameCnt` advancing proves the render loop is running rather than wedged
  behind a bad fetch, and its rate distinguishes a machine whose turbo took
  effect from one crawling at 1 MHz.
- the picture, briefly. With the player standing still it should not change,
  but a machine that flips its double buffer during a capture yields one
  differing frame, so counting differing frames alone would fail a healthy
  machine. What separates the two is how many *different* sets of pixels
  change, relative to how many frames differ at all. The game repeats one
  pattern; corrupt REU data alters a different set nearly every frame.

The published `doom.cfg` is written for a C64 Ultimate and names settings by
labels this machine does not necessarily share, so the settings are applied
through the config API instead. "Turbo Control" is the case in point: the
value that selects the turbo registers is called "C64U Turbo Registers" on
some firmware and "U64 Turbo Registers" on others, so it is matched rather
than hard coded.
"""

import argparse
import json
import os
import sys
import time
import urllib.request
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parents[2] / "lib"))
sys.path.insert(0, str(SCRIPT_DIR.parents[1] / "lib"))

from api import UltimateApi                                        # noqa: E402
from rest import retrying_urlopen                                  # noqa: E402
import streams                                                     # noqa: E402
import targets                                                     # noqa: E402
from report import (Failure, best_effort, check, check_ok, check_skip, check_start,   # noqa: E402
                    detail, format_exception, suite_fail, suite_ok, suite_skip)

SUITE = "doom_release_test"

RELEASE_API = "https://api.github.com/repos/slesinger/doom/releases/latest"
REU_ASSET = "game.reu"
PRG_ASSET = "launcher.prg"

# Engine state, from the Doom C64U sources (src/defs.asm).
FRAMECNT = 0x0F40      # 2 bytes, one increment per rendered frame
MAPOK = 0x0F47         # 1 = the REU image was found and verified at boot
MAPERR = 0x0F48        # why not, when mapOK is 0

# launcher.prg waits for SPACE by polling the keyboard matrix at $DC00/$DC01.
# Two NOPs over the loop's `bne` back-edge fall through into chainToGame, which
# is how this runs without a keyboard. The pattern is checked before it is
# overwritten, so a future release that moves the loop fails the check rather
# than corrupting whatever now lives at that address.
SPACE_BNE = 0x0945
SPACE_BNE_OPCODE = b"\xd0\xf4"

# Ultimate Audio channel registers. Loading a .reu through runners:modplay also
# starts the MOD player, whose sampler channels keep reading REU memory in the
# FPGA; silence them or that traffic competes with the engine's own streaming.
SAMPLER_BASE = 0xDF20
SAMPLER_END = 0xE000

# Applied by hand, in place of the published doom.cfg.
SETTINGS = (
    ("U64 Specific Settings", "System Mode", "PAL"),
    ("U64 Specific Settings", "Badline Timing", "Enabled"),
    ("C64 and Cartridge Settings", "RAM Expansion Unit", "Enabled"),
    ("C64 and Cartridge Settings", "REU Size", "16 MB"),
    ("C64 and Cartridge Settings", "Map Ultimate Audio $DF20-DFFF", "Enabled"),
    ("C64 and Cartridge Settings", "Command Interface", "Enabled"),
)
TURBO_STORE = "U64 Specific Settings"
TURBO_ITEM = "Turbo Control"
TURBO_MATCH = "Turbo Registers"     # the label carries a model prefix

MIN_FPS = 5.0                       # a machine at 1 MHz renders about 0.3

# Corruption is judged from two numbers together, calibrated by sweeping every
# bitstream between v3.14 and v3.15 on an Ultimate 64 Elite:
#
#   candidate                     distinct masks   masks/differing frames
#   v3.14                                      0   no differing frames at all
#   every bitstream up to f2a14e51          1 - 3   0.003 - 0.25
#   c4be69a2                                 503   0.500
#
# The game's own output repeats one pattern however often it recurs, so a low
# ratio is normal however many frames differ. Corrupt REU data alters a
# different set of pixels nearly every frame, which drives the ratio up. Both a
# floor on the count and the ratio are required: a handful of masks appears on
# healthy bitstreams, and a high ratio over three differing frames means nothing.
MIN_CORRUPT_MASKS = 10
MIN_CORRUPT_RATIO = 0.30

# Maps every non-zero byte to 1, so two frames differing in the same pixels hash
# alike whatever the colours involved.
_NONZERO = bytes(0 if value == 0 else 1 for value in range(256))
CORRUPTION_FRAMES = 120             # about 2.5s of PAL output


def cache_dir() -> Path:
    root = os.environ.get("DOOM_ASSET_DIR")
    if root:
        return Path(root)
    return Path(os.environ.get("XDG_CACHE_HOME",
                               Path.home() / ".cache")) / "doom-c64u"


def fetch_release(timeout: float) -> tuple:
    """Return (tag, {asset name: local path}), downloading only what is missing."""
    request = urllib.request.Request(RELEASE_API)
    with retrying_urlopen(request, timeout, idempotent=True) as resp:
        release = json.load(resp)
    tag = release.get("tag_name", "unknown")
    wanted = {a["name"]: a["browser_download_url"]
              for a in release.get("assets", []) if a["name"] in (REU_ASSET, PRG_ASSET)}
    missing = {REU_ASSET, PRG_ASSET} - set(wanted)
    if missing:
        raise Failure(f"release {tag} has no {', '.join(sorted(missing))}")

    out = cache_dir() / tag
    out.mkdir(parents=True, exist_ok=True)
    paths = {}
    for name, url in wanted.items():
        path = out / name
        if not path.exists() or path.stat().st_size == 0:
            request = urllib.request.Request(url)
            with retrying_urlopen(request, timeout, idempotent=True) as resp, \
                    open(path, "wb") as fh:
                fh.write(resp.read())
        paths[name] = path
    return tag, paths


def serves(api: UltimateApi, store: str, item: str) -> bool:
    """Whether this machine has that setting, without raising if it has not."""
    try:
        return item in api.configs.category(store)
    except Failure:
        return False


def turbo_register_value(api: UltimateApi) -> str:
    values = api.configs.item(TURBO_STORE, TURBO_ITEM).get("values", [])
    for value in values:
        if isinstance(value, str) and TURBO_MATCH in value:
            return value
    raise Failure(f"{TURBO_STORE}/{TURBO_ITEM} offers no turbo-register "
                  f"setting: {values!r}")


def apply_settings(api: UltimateApi, previous: dict) -> None:
    """Apply what Doom needs, recording each previous value in `previous`.

    The caller owns the dict, so a failure part way through still leaves it
    holding everything already changed. Returning it instead would abandon the
    machine in PAL with a 16 MB REU and turbo selected.
    """
    wanted = [*list(SETTINGS), (TURBO_STORE, TURBO_ITEM, turbo_register_value(api))]
    for store, item, value in wanted:
        current = api.configs.current(store, item)
        previous[(store, item)] = current
        if current != value:
            api.configs.set(store, item, value)


def restore_settings(api: UltimateApi, previous: dict) -> None:
    for (store, item), value in previous.items():
        if not value:
            # current() answers "" when the device reported no value; writing
            # that back is refused, and silently swallowing it would hide a
            # setting this suite changed and did not put back.
            detail(f"cannot restore {store}/{item}: it had no readable value")
            continue
        try:
            api.configs.set(store, item, value)
        except Exception as exc:        # noqa: BLE001 - teardown must continue
            detail(f"could not restore {store}/{item} to {value!r}: {exc}")


def silence_sampler(api: UltimateApi) -> None:
    """Gate off every channel in $DF20-$DFFF.

    The last chunk is clamped. Doom runs with RAM under the KERNAL, so a full
    128-byte write from $DFA0 would zero $E000-$E01F of engine memory, and the
    firmware accepts it: route_machine.cc only rejects writes past $FFFF.
    """
    for addr in range(SAMPLER_BASE, SAMPLER_END, 128):
        api.machine.writemem(addr, b"\x00" * min(128, SAMPLER_END - addr),
                             idempotent=True)


def wait_for_launcher(api: UltimateApi, deadline: float) -> None:
    """Wait until launcher.prg is in RAM and sitting in its SPACE loop."""
    end = time.monotonic() + deadline
    while time.monotonic() < end:
        if api.machine.readmem(SPACE_BNE, 2) == SPACE_BNE_OPCODE:
            return
        time.sleep(0.5)
    raise Failure(
        f"launcher.prg never reached its title screen: ${SPACE_BNE:04X} is not "
        f"{SPACE_BNE_OPCODE.hex()} within {deadline:.0f}s")


def distinct_change_masks(host, frames_wanted: int, timeout: float) -> tuple:
    """Capture frames of a still picture and count how many distinct sets of
    pixels differ from the first one.

    Frames are assembled by header offset and packets from any other device on
    the shared multicast group are discarded, so neither reordering nor another
    Ultimate streaming into the same group reads as a change.
    """
    import hashlib

    addresses = streams.source_addresses(host)
    handle = targets.resolve(host)
    sock = streams.stream_socket(handle.video_group, handle.video_port, timeout=2.0)
    assembler = streams.FrameAssembler()
    reference = None
    masks = {}
    frames = deviating = worst = 0
    try:
        for _sock, data, mine in streams.receive([sock], addresses, timeout):
            if not mine:
                continue
            frame = assembler.push(data)
            if frame is None or not frame.complete:
                continue
            pixels = streams.unpack(frame.packed)
            if reference is None or len(pixels) != len(reference):
                reference = pixels
                continue
            frames += 1
            # Done with whole-buffer operations. An interpreted pass over
            # 104,448 pixels costs more than the 20 ms a PAL frame allows, and a
            # reader that falls behind loses packets; FrameAssembler then
            # completes almost no frame and the suite fails for its own slowness.
            size = len(pixels)
            xored = (int.from_bytes(pixels, "big")
                     ^ int.from_bytes(reference, "big")).to_bytes(size, "big")
            unchanged = xored.count(0)
            if unchanged != size:
                deviating += 1
                worst = max(worst, size - unchanged)
                key = hashlib.md5(xored.translate(_NONZERO)).hexdigest()[:8]
                masks[key] = masks.get(key, 0) + 1
            if frames >= frames_wanted:
                break
    finally:
        sock.close()
    return frames, deviating, len(masks), worst


def read_framecnt(api: UltimateApi) -> int:
    raw = api.machine.readmem(FRAMECNT, 2)
    return raw[0] | (raw[1] << 8)


def wait_for_engine(api: UltimateApi, deadline: float) -> None:
    end = time.monotonic() + deadline
    previous = None
    while time.monotonic() < end:
        now = read_framecnt(api)
        if previous is not None and now != previous:
            return
        previous = now
        time.sleep(0.5)
    mapok = api.machine.readmem(MAPOK, 1)[0]
    maperr = api.machine.readmem(MAPERR, 1)[0]
    raise Failure(
        f"the engine never rendered a frame within {deadline:.0f}s "
        f"(mapOK={mapok}, mapErr={maperr}). A garbled picture with mapOK=0 is "
        f"the REU image arriving corrupt.")


def measure_fps(api: UltimateApi, seconds: float) -> float:
    start_count = read_framecnt(api)
    start = time.monotonic()
    time.sleep(seconds)
    return (read_framecnt(api) - start_count) / (time.monotonic() - start)


def run(args):
    """Run the suite, or answer why this machine could not run it.

    A product without an REU or turbo control is a skip, not a pass: the runner
    cannot tell the two apart from an exit code, so the closing line has to.
    """
    api = UltimateApi(args.host, args.password or None, args.timeout)

    info = api.info()
    # Asked before the check opens: a machine without these stores answers with
    # an error, and ConfigsApi.category raises on it. Inside the check that
    # would report FAIL on exactly the machines meant to be skipped, and leave
    # the check open.
    equipped = (serves(api, "C64 and Cartridge Settings", "REU Size")
                and serves(api, TURBO_STORE, TURBO_ITEM))
    check_start("the machine has the REU and turbo this release needs")
    if not equipped:
        reason = (f"{info.product} does not serve both a REU Size and a "
                  f"{TURBO_ITEM} setting")
        check_skip(reason)
        return reason
    check_ok(f"{info.product}, firmware {info.firmware_version}, "
             f"FPGA {info.fpga_version}")

    with check("download the published release"):
        tag, assets = fetch_release(args.timeout)
        detail(f"release {tag}: " + ", ".join(
            f"{name} ({path.stat().st_size} bytes)" for name, path in sorted(assets.items())))

    previous = {}
    try:
        with check("apply the settings the release documents"):
            apply_settings(api, previous)

        with check("load the level image into the REU"):
            # POST to runners:modplay puts the body straight into REU memory,
            # which is what the file browser's "Load into REU" does, without
            # needing the file on the device first.
            payload = assets[REU_ASSET].read_bytes()
            code, _, body = api.runners.upload("modplay", payload,
                                               timeout=args.upload_timeout)
            if code != 200:
                raise Failure(f"runners:modplay returned HTTP {code}: {body[:160]!r}")
            silence_sampler(api)

        with check("start the game and reach the engine"):
            code, _, body = api.runners.upload("run_prg", assets[PRG_ASSET].read_bytes())
            if code != 200:
                raise Failure(f"runners:run_prg returned HTTP {code}: {body[:160]!r}")
            wait_for_launcher(api, args.boot_timeout)
            api.machine.writemem(SPACE_BNE, b"\xea\xea", idempotent=True)
            wait_for_engine(api, args.boot_timeout)

        with check("the engine verified its REU image"):
            mapok = api.machine.readmem(MAPOK, 1)[0]
            maperr = api.machine.readmem(MAPERR, 1)[0]
            if mapok != 1:
                raise Failure(
                    f"mapOK={mapok}, mapErr={maperr}: the engine rejected the REU "
                    f"image it just read back, so the REU returned wrong bytes")

        with check(f"the engine renders faster than {MIN_FPS:.0f} fps"):
            fps = measure_fps(api, args.measure_seconds)
            detail(f"{fps:.2f} fps")
            if fps < MIN_FPS:
                raise Failure(
                    f"{fps:.2f} fps: the engine renders but the turbo is not in "
                    f"effect (a 1 MHz machine measures about 0.3)")

        with check("the picture is not corrupted while the player stands still"):
            # Arming knows whether the stream was already running, so a
            # recording run does not lose its video when this suite ends.
            with streams.Arming(api, args.host) as arming:
                arming.start("video")
                frames, deviating, masks, worst = distinct_change_masks(
                    args.host, CORRUPTION_FRAMES, args.capture_seconds)
            detail(f"{frames} frames, {deviating} differing, {masks} distinct "
                   f"change-mask(s), ratio "
                   f"{masks / deviating if deviating else 0:.2f}, worst {worst} px")
            if frames < CORRUPTION_FRAMES // 2:
                raise Failure(f"only {frames} frames arrived; the video stream "
                              f"did not deliver enough to judge the picture")
            ratio = masks / deviating if deviating else 0.0
            if masks >= MIN_CORRUPT_MASKS and ratio >= MIN_CORRUPT_RATIO:
                raise Failure(
                    f"{masks} distinct change-masks over {deviating} differing "
                    f"frames (ratio {ratio:.2f}): the picture changes a different "
                    f"set of pixels nearly every frame, which is REU data "
                    f"arriving corrupt, not the game's own output")
        return None
    finally:
        restore_settings(api, previous)
        best_effort("reset the machine", api.machine.reset)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the published Doom C64U release and check it works.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float, default=30.0)
    parser.add_argument("--upload-timeout", type=float, default=240.0,
                        help="the REU image is megabytes and the device answers "
                             "only once all of it is written")
    parser.add_argument("--boot-timeout", type=float, default=45.0,
                        help="seconds to wait for the title screen and the engine")
    parser.add_argument("--measure-seconds", type=float, default=3.0,
                        help="seconds over which the frame rate is measured")
    parser.add_argument("--capture-seconds", type=float, default=15.0,
                        help="deadline for collecting the frames the picture "
                             "check compares; not a per-request timeout")
    args = parser.parse_args()
    try:
        skipped = run(args)
        if skipped:
            suite_skip(SUITE, skipped)
            return 0
        suite_ok(SUITE)
        return 0
    except Failure as exc:
        suite_fail(SUITE, str(exc))
        return 1
    except Exception as exc:            # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
