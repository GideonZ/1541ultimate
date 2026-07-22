#!/usr/bin/env bash
# Gate volume over REST (blocker C). Accumulates the Section-17 minimums using the
# REST stress runner, which survives telnet load-degradation. Each lane is bounded;
# a wedge (rc=2) stops the lane for JTAG post-mortem.
#
#   >=10,000 oracle-validated single-steps  (steps lanes, freeze+overlay)
#   >=500 JSR descend/unwind cycles, depth>=8 majority, >=16 targeted
#   >=200 natural-exit liveness validations across UI modes
#   matrix reps across UI modes x core ops (steps+jsr+liveness in both modes)
set -uo pipefail
cd /home/chris/dev/c64/1541ultimate
HOST="${1:-192.168.1.13}"
ART="${2:-doc/research/machine-monitor/debug/artifacts/2026-06-27_mcm-debug-final2/logs}"
S=tools/developer/machine-code-monitor/monitor_debug_stress.py
mkdir -p "$ART"
run() { echo ">>> $*"; python3 "$S" --host "$HOST" --artifact-dir "$ART" "$@"; echo "rc=$?"; }

# --- single-step volume: ~5000 steps per UI mode (prog-len 200 x 25 iters) ---
run --ui freeze  --focus steps --iterations 25 --prog-len 200 --seed 4001
run --ui overlay --focus steps --iterations 25 --prog-len 200 --seed 4002

# --- JSR descend/unwind cycles: depth 8 majority + depth 16 targeted ---
run --ui freeze  --focus jsr --iterations 220 --jsr-depths 8,8,8,16 --seed 5001
run --ui overlay --focus jsr --iterations 300 --jsr-depths 8,8,8,8,16 --seed 5002

# --- natural-exit liveness: >=200 validations across UI modes ---
run --ui freeze  --focus liveness --iterations 100 --seed 6001
run --ui overlay --focus liveness --iterations 100 --seed 6002

echo "===== GATE VOLUME LANES COMPLETE ====="
