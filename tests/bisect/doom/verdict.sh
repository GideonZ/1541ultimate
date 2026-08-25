#!/usr/bin/env bash
# verdict.sh <commit> [tag] [launches] [seconds]
#
# Deploy one commit, then launch Doom several times and judge the picture.
#   BROKEN  the engine rejected the REU image or never rendered
#   BAD     the picture is randomly corrupted
#   GOOD    every launch was clean
#   SETUP_FAILED  the launch itself did not happen; never counted as a defect
#
# Several launches, because the corruption rate varies from one launch to the
# next on one and the same bitstream: 0%, 4.7% and 7.8% were measured on three
# launches of the same build. One clean launch proves nothing.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMMIT="${1:?usage: verdict.sh <commit> [tag] [launches] [seconds]}"
TAG="${2:-$COMMIT}"
LAUNCHES="${3:-3}"
SECONDS_PER="${4:-25}"
HOST="${DOOM_HOST:-u64}"      # or pass --host through DOOM_HOST
REU="${DOOM_REU:-/USB2/doom/game.reu}"

deployed=$(NO_CHECKOUT="${NO_CHECKOUT:-0}" bash "$HERE/deploy_commit.sh" "$COMMIT" 2>&1)
echo "$deployed" | tail -2
grep -q "^DEPLOYED" <<<"$deployed" || { echo "FINAL: SETUP_FAILED"; exit 3; }

judged=0
for i in $(seq 1 "$LAUNCHES"); do
    run=$(python3 "$HERE/doom_run.py" --host "$HOST" --reu "$REU" \
              --tag "$TAG-r$i" 2>&1)
    echo "$run" | grep -E "^RESULT|^VERDICT"
    grep -q "SETUP_FAILED" <<<"$run" && { echo "FINAL: SETUP_FAILED"; exit 3; }
    grep -q "VERDICT: BROKEN" <<<"$run" && { echo "FINAL: BROKEN (launch $i)"; exit 0; }

    soak=$(python3 "$HERE/doom_soak.py" --host "$HOST" --seconds "$SECONDS_PER" \
               --label "$TAG-r$i" 2>&1)
    echo "$soak" | grep "^SOAK"
    masks=$(grep -oP 'distinct_masks=\K\d+' <<<"$soak" | tail -1)
    differing=$(grep -oP 'deviating=\K\d+' <<<"$soak" | tail -1)
    moved=$(grep -oP 'moved_at_first_deviation=\K\S+' <<<"$soak" | tail -1)
    live=$(grep -oP 'live_border=\K\S+' <<<"$soak" | tail -1)
    [ -z "${masks:-}" ] && { echo "FINAL: SETUP_FAILED (no soak)"; exit 3; }
    if [ "${live:-}" != "changed" ]; then
        echo "FINAL: SETUP_FAILED (the capture cannot show a change)"; exit 3
    fi
    if [ "${moved:-no}" = "yes" ]; then
        echo "   launch $i void: the player moved, so the picture changed legitimately"
        continue
    fi
    judged=$(( judged + 1 ))
    # Calibrated by sweeping every bitstream between v3.14 and v3.15: healthy
    # ones produce 1 to 3 masks over 0.3% to 25% of their differing frames,
    # while the corrupt one produced 503 over 50%. Both a floor on the count and
    # the ratio are needed, because a few masks appear on healthy bitstreams and
    # a high ratio over three differing frames means nothing.
    if [ "${masks:-0}" -ge 10 ] && [ "${differing:-0}" -gt 0 ] \
       && [ "$(( masks * 10 / differing ))" -ge 3 ]; then
        echo "FINAL: BAD (distinct_masks=$masks over $differing differing frames, launch $i)"
        exit 0
    fi
done
# A voided launch judged nothing, so concluding GOOD from a run where every
# launch was voided would report a candidate as clean that was never tested.
if [ "$judged" -eq 0 ]; then
    echo "FINAL: SETUP_FAILED (every launch was void)"; exit 3
fi
echo "FINAL: GOOD ($judged clean launches)"
