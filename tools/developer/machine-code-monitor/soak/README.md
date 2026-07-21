# Machine Code Monitor Soak Runner

Local-only full-matrix soak runner for the U64 Machine Code Monitor Debug
lifecycle. This directory is excluded through `.git/info/exclude` and must not
be committed.

By default the runner executes the full unattended acceptance matrix:

- clean `overlay`, `freeze`, and `telnet` scenarios
- Overlay concurrency scenarios
- all three banking targets
- 25 consecutive iterations per scenario
- JTAG recovery enabled for any failed/degraded scenario state before retrying
  that scenario from iteration 1
- remaining scenarios are halted if a degraded device state persists after the
  recovery budget, so the runner does not keep interacting with a known-bad
  device

Preferred smoke test:

```bash
python3 tools/developer/machine-code-monitor/soak/run_full_matrix_soak.py \
  -H u64 \
  -n 1 \
  --view overlay \
  --banking ram \
  --no-concurrency
```

Full acceptance run:

```bash
python3 tools/developer/machine-code-monitor/soak/run_full_matrix_soak.py \
  -H u64 \
  -n 25 \
  -o doc/research/machine-monitor/debug/stabilization/6-final/soak-runs
```

Useful focused reruns:

```bash
python3 tools/developer/machine-code-monitor/soak/run_full_matrix_soak.py \
  -H u64 -n 25 --scenario freeze:ram --no-concurrency

python3 tools/developer/machine-code-monitor/soak/run_full_matrix_soak.py \
  -H u64 -n 25 --no-clean --scenario concurrency:overlay:rom
```

Outputs per run:

- `run.log`: live progress log.
- `events.jsonl`: one structured event per operation/probe.
- `manifest.json`: command line, git state, selected scenarios, preflight probes,
  and canonical log paths.
- `summary.md`: final scenario table and recovery/failure summary.
- `failures/<timestamp>-<scenario>/`: traceback, context, UI/Telnet screen
  captures, liveness probes, and last 200 syslog lines.
- `recoveries/<timestamp>-<scenario>/`: JTAG deploy context and combined
  deploy stdout/stderr.

The process exits non-zero when any selected scenario fails, but still writes the
same log folder shape. Pass the entire run directory to the next debugging
session, not just `summary.md`.
