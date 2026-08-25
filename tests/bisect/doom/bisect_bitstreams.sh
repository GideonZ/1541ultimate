#!/usr/bin/env bash
# bisect_bitstreams.sh <good-commit> <bad-commit> [candidates...]
#
# Binary search over the distinct FPGA bitstreams between two commits, testing
# each through verdict.sh. With no candidates given they are taken from
# list_bitstreams.sh over <good>..<bad>.
#
# Each candidate is the commit that introduced a bitstream, so the FPGA and the
# Nios application under test always come from the same commit.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GOOD="${1:?usage: bisect_bitstreams.sh <good> <bad> [candidates...]}"
BAD="${2:?usage: bisect_bitstreams.sh <good> <bad> [candidates...]}"
shift 2
LOG="${DOOM_BISECT_LOG:-$HOME/.cache/doom-bisect/bitstream-search.log}"
mkdir -p "$(dirname "$LOG")"
: > "$LOG"

if [ "$#" -gt 0 ]; then
    CANDIDATES=("$@")
else
    mapfile -t CANDIDATES < <(bash "$HERE/list_bitstreams.sh" "$GOOD..$BAD" | awk '{print $1}')
fi
[ "${#CANDIDATES[@]}" -gt 0 ] || { echo "no bitstream changes in $GOOD..$BAD"; exit 1; }

echo "candidates (oldest first): ${CANDIDATES[*]}" | tee -a "$LOG"
lo=0; hi=$(( ${#CANDIDATES[@]} - 1 )); first_bad=""; skipped=()
while [ "$lo" -le "$hi" ]; do
    mid=$(( (lo + hi) / 2 ))
    candidate="${CANDIDATES[$mid]}"
    echo "== testing $candidate (window $lo..$hi)" | tee -a "$LOG"
    verdict=""
    for attempt in 1 2; do
        out=$(bash "$HERE/verdict.sh" "$candidate" "bs-$candidate" 2>&1)
        echo "$out" | grep -E "^DEPLOYED|^RESULT|^SOAK|^FINAL" >> "$LOG"
        verdict=$(grep "^FINAL:" <<<"$out" | tail -1 | awk '{print $2}')
        case "$verdict" in GOOD|BAD|BROKEN) break ;; *) verdict="" ;; esac
    done
    echo "== $candidate -> ${verdict:-NO_VERDICT}" | tee -a "$LOG"
    case "$verdict" in
        GOOD)       lo=$(( mid + 1 )) ;;
        BAD|BROKEN) first_bad="$candidate"; hi=$(( mid - 1 )) ;;
        *)          echo "== skipping $candidate, it could not be judged" | tee -a "$LOG"
                    skipped+=("$candidate"); lo=$(( mid + 1 )) ;;
    esac
done
# A candidate that could not be judged moved the window like a GOOD one, so say
# so: the answer is only sound if the list below is empty.
if [ -z "$first_bad" ]; then
    echo "NO BAD BITSTREAM FOUND in $GOOD..$BAD" | tee -a "$LOG"
else
    echo "FIRST BAD BITSTREAM: $first_bad" | tee -a "$LOG"
fi
if [ "${#skipped[@]}" -gt 0 ]; then
    echo "UNTESTED CANDIDATES: ${skipped[*]}" | tee -a "$LOG"
fi
