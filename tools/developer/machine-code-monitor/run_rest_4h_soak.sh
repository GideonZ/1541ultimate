#!/usr/bin/env bash
# Continuous REST debugger soak (default 4h). Runs the oracle-validated stress
# runner in alternating UI modes + foci, accumulating volume, until the time
# budget elapses or a WEDGE (rc=2) is seen. Any rc!=0 from a chunk other than a
# wedge is logged and the soak continues (the runner self-recovers per-iteration).
set -uo pipefail
cd /home/chris/dev/c64/1541ultimate
HOST="${1:-192.168.1.13}"
BUDGET="${2:-14400}"          # seconds (4h default)
ART="${3:-doc/research/machine-monitor/debug/artifacts/2026-06-27_mcm-debug-final2/logs/soak4h}"
S=tools/developer/machine-code-monitor/monitor_debug_stress.py
mkdir -p "$ART"
SECONDS=0
chunk=0
echo "SOAK4H start budget=${BUDGET}s host=$HOST $(date)"
while [ "$SECONDS" -lt "$BUDGET" ]; do
  chunk=$((chunk+1))
  ui=freeze; [ $((chunk % 2)) -eq 0 ] && ui=overlay
  seed=$((70000 + chunk))
  echo "--- chunk $chunk ui=$ui seed=$seed elapsed=${SECONDS}s ---"
  python3 "$S" --host "$HOST" --ui "$ui" --focus all \
      --iterations 6 --prog-len 120 --jsr-depths 8,8,16 \
      --seed "$seed" --artifact-dir "$ART"
  rc=$?
  echo "chunk $chunk rc=$rc elapsed=${SECONDS}s"
  if [ "$rc" -eq 2 ]; then
    echo "SOAK4H ABORT: WEDGE in chunk $chunk at ${SECONDS}s"
    exit 2
  fi
done
echo "SOAK4H COMPLETE: ${SECONDS}s, $chunk chunks, no wedge $(date)"
