#!/usr/bin/env python3
"""Start Doom C64U on a U64 and report whether the engine came up.

Loads the REU image, silences the MOD player that loading it starts, runs
launcher.prg, leaves the title screen without a keyboard, then reports the
engine's own integrity result, its frame rate, and how the first frame compares
against a stored reference frame.

    ./doom_run.py --host u64 --tag mytest
    ./doom_run.py --write-golden        # record the reference frame
"""
import argparse
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import doomlib as D                                            # noqa: E402

DEFAULT_ASSETS = os.path.expanduser("~/.cache/doom-bisect/assets")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="u64")
    ap.add_argument("--reu", default="/USB2/doom/game.reu",
                    help="path to game.reu on the device")
    ap.add_argument("--prg", default=os.path.join(DEFAULT_ASSETS, "launcher.prg"))
    ap.add_argument("--tag", default="run")
    ap.add_argument("--frames", type=int, default=20)
    ap.add_argument("--settle", type=float, default=15.0,
                    help="seconds between chaining in and measuring")
    ap.add_argument("--boot-timeout", type=float, default=45.0,
                    help="seconds to wait for the title screen")
    ap.add_argument("--golden", default=os.path.expanduser(
        "~/.cache/doom-bisect/golden.npy"))
    ap.add_argument("--write-golden", action="store_true")
    ap.add_argument("--outdir", default=os.path.expanduser(
        "~/.cache/doom-bisect/shots"))
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    shot = lambda n: os.path.join(a.outdir, f"{a.tag}-{n}.png")   # noqa: E731

    try:
        attempts = D.load_reu(a.host, a.reu)
    except RuntimeError as e:
        print(f"VERDICT: SETUP_FAILED ({e})")
        return 3
    print(f"[1] REU image loaded after {attempts} attempt(s)")

    D.silence_sampler(a.host)
    print("[2] sampler channels $DF20-$DFFF silenced")

    D.run_prg(a.host, a.prg)
    print("[3] launcher.prg started")
    time.sleep(10.0)

    landed, before, after = D.skip_title(a.host, a.boot_timeout)
    if not landed:
        print(f"[4] launcher.prg never reached its title screen within "
              f"{a.boot_timeout:.0f}s")
        print("VERDICT: SETUP_FAILED")
        return 3
    print(f"[4] title skipped: ${D.SPACE_BNE:04X} {before.hex()} -> {after.hex()}")

    print(f"[5] settling {a.settle:.0f}s")
    time.sleep(a.settle)

    mapok = D.readmem(a.host, D.MAPOK, 1)
    maperr = D.readmem(a.host, D.MAPERR, 1)
    if mapok is None or maperr is None:
        print("VERDICT: SETUP_FAILED (the device stopped answering readmem)")
        return 3
    mapok, maperr = mapok[0], maperr[0]

    c0 = D.framecnt(a.host)
    t0 = time.time()
    with D.VideoStream(a.host) as vs:
        first = vs.frame()
        height = first.shape[0]
        frames = [first]
        mismatched = 0
        while len(frames) < a.frames and mismatched < 40:
            nxt = vs.frame(height)
            # A geometry change mid-capture would make the comparison below
            # raise rather than report anything useful.
            if nxt.shape != first.shape:
                mismatched += 1
                continue
            frames.append(nxt)
        dropped = vs.assembler.counts()
    c1 = D.framecnt(a.host)
    if c0 is None or c1 is None:
        print("VERDICT: SETUP_FAILED (the device stopped answering readmem)")
        return 3
    # frameCnt is 16 bits and wraps about every 20 minutes at this frame rate.
    fps = ((c1 - c0) % 0x10000) / (time.time() - t0)

    base = frames[0]
    interframe = [int(np.count_nonzero(f != base)) for f in frames[1:]]
    D.save_png(base, shot("frame0"))

    golden_diff = None
    if a.write_golden:
        np.save(a.golden, base)
        print(f"    wrote reference frame {a.golden} {base.shape}")
    elif os.path.exists(a.golden):
        g = np.load(a.golden)
        golden_diff = int(np.count_nonzero(g != base)) if g.shape == base.shape else -1

    print(f"RESULT mapOK={mapok} mapErr={maperr} fps={fps:.2f} height={height} "
          f"dropped={dropped} interframe={interframe} golden_diff={golden_diff}")
    if mapok != 1 or fps < 5.0:
        print("VERDICT: BROKEN")
    else:
        print("VERDICT: STARTED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
