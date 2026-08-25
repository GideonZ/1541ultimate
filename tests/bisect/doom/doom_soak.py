#!/usr/bin/env python3
"""Measure whether Doom's picture is stable while the player stands still.

Every streamed VIC frame is compared against the first one. With no input the
picture should be identical, so anything else is either the game animating or
the machine corrupting it, and those two are told apart by counting **distinct
change-masks**:

- an animation toggles the *same* pixels every time. E1M1 has a flickering
  light sector, and on a healthy machine it produces exactly one distinct mask,
  even when it changes over two thousand pixels.
- corruption changes a *different* set nearly every frame. A bitstream with the
  defect this harness was built for produces three hundred or more.

Counting deviating frames alone cannot separate them, and a plain BASIC screen
scores 50% deviating frames on any machine because the cursor blinks.

Assumes Doom is already running and settled; see doom_run.py.
"""
import argparse
import hashlib
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import doomlib as D                                            # noqa: E402

# Anything above this many differing pixels is reported separately: it is the
# difference between a few pixels on one raster line and a corrupted region.
LARGE_PIXELS = 200


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="u64")
    ap.add_argument("--seconds", type=float, default=25.0)
    ap.add_argument("--label", default="soak")
    ap.add_argument("--outdir", default=os.path.expanduser(
        "~/.cache/doom-bisect/shots"))
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    shot = lambda n: os.path.join(a.outdir, f"{a.label}-{n}.png")   # noqa: E731

    with D.VideoStream(a.host) as vs:
        ref = vs.frame()
        height = ref.shape[0]
        D.save_png(ref, shot("ref"))
        camera_before = D.camera(a.host)
        camera_at_first_deviation = None

        frames = deviating = worst = total = large = 0
        masks = {}
        runs = []
        run_length = 0
        saved = 0
        end = time.time() + a.seconds
        while time.time() < end:
            f = vs.frame(height)
            if f.shape[0] != height:
                continue
            frames += 1
            diff = int(np.count_nonzero(f != ref))
            if not diff:
                if run_length:
                    runs.append(run_length)
                    run_length = 0
                continue
            deviating += 1
            total += diff
            worst = max(worst, diff)
            run_length += 1
            if diff > LARGE_PIXELS:
                large += 1
            key = hashlib.md5(np.packbits(f != ref).tobytes()).hexdigest()[:8]
            masks[key] = masks.get(key, 0) + 1
            if camera_at_first_deviation is None:
                camera_at_first_deviation = D.camera(a.host)
            if saved < 3:
                D.save_png(f, shot(f"dev{saved}"))
                D.save_png(np.where(f != ref, 2, 0).astype(np.uint8), shot(f"mask{saved}"))
                saved += 1
        if run_length:
            runs.append(run_length)

        # The capture has to be able to show a change at all, or "stable" is
        # meaningless. Poking the VIC border is the one way that works on every
        # firmware in range: writing camX/camA does not move the view, because
        # the engine undoes an out-of-bounds position and renders from cached
        # sine and cosine values.
        before_border = D.readmem(a.host, 0xD020, 1)
        reference = vs.frame(height)
        D.writemem(a.host, 0xD020, "05")
        time.sleep(1.0)
        live = int(np.count_nonzero(vs.frame(height) != reference)) > 0
        if before_border:
            D.writemem(a.host, 0xD020, f"{before_border[0]:02X}")

        # Real input, where the firmware serves it. Absent before 3.15.
        input_reference = vs.frame(height)
        if D.inject_input(a.host):
            time.sleep(1.5)
            responds = ("changed"
                        if int(np.count_nonzero(vs.frame(height) != input_reference))
                        else "NO_CHANGE")
        else:
            responds = "n/a"

        camera_after = D.camera(a.host)

    moved = "unknown"
    if camera_before and camera_after:
        moved = "yes" if camera_before != camera_after else "no"
    moved_first = "n/a"
    if camera_before and camera_at_first_deviation:
        moved_first = "yes" if camera_before != camera_at_first_deviation else "no"

    print(f"SOAK {a.label}: frames={frames} deviating={deviating} "
          f"({100.0 * deviating / frames if frames else 0:.2f}%) "
          f"large={large} worst={worst}px "
          f"mean_px={total // deviating if deviating else 0} "
          f"distinct_masks={len(masks)} bursts={len(runs)} burst_lengths={runs[:15]} "
          f"height={height} cam_moved={moved} moved_at_first_deviation={moved_first} "
          f"live_border={'changed' if live else 'NO_CHANGE'} input={responds}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
