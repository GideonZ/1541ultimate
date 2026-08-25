#!/usr/bin/env bash
# list_bitstreams.sh [good..bad]
#
# The FPGA bitstream, not the firmware commit, is the variable that tracks this
# family of defect, and only a dozen or so distinct ones exist across a release.
# Different branches also carry different bitstream lineages, so bisecting
# firmware commits spends most of its rounds on builds whose hardware is
# identical. This lists each distinct external/u64.sof in a range, oldest
# first, with the commit that introduced it.
set -uo pipefail
RANGE="${1:-v3.14..v3.15}"
# Deduplicated on the blob: a commit that touched the path without changing the
# bitstream, or a revert and reapply, would otherwise be listed as a separate
# build and cost a bisection round on hardware that is already known.
seen=""
git log --format="%H|%ad|%s" --date=short "$RANGE" -- external/u64.sof | tac |
while IFS='|' read -r sha date subject; do
    blob=$(git rev-parse --short "$sha:external/u64.sof")
    case " $seen " in *" $blob "*) continue ;; esac
    seen="$seen $blob"
    printf '%s  %s  blob=%s  %s\n' \
        "$(git rev-parse --short "$sha")" "$date" "$blob" "${subject:0:58}"
done
