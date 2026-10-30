# Controlled HTTPS hardware runner

This runner is opt-in. Local and emulator tests do not establish hardware readiness.
Installation and board-specific recovery qualification remain pending.

## Prepared bring-up runner

`tools/https_hardware_runner.py` validates a plan offline by default. Only
`--execute --installation RECORD` permits device access. The record must bind the
candidate and plan hashes, provide a current LAN address, expire within 24 hours,
and reference hashed local evidence for every installation/preflight gate. These
are operator/evidence gates, not remote attestation. Never mark an unverified gate
confirmed merely to start a test.

The runner uses the repository's native 6502 UCI agent, with opt-in
`WAIT_WRAPS=$80` and `EXTERNAL_ABORT=1`. Default assembly remains byte-identical.
The HTTPS mode accepts a host cancellation mailbox while the command is pending;
the 6502 still writes the actual UCI abort register. The host has a 35-second
request guard and a 15-minute campaign guard. The longer 6502 counter is a
secondary escape, not a measurement of wall time. Use ordinary C64 CPU speed.
No tests are automatically retried. `operator.stop` ends progression at the next
host poll. Failure preserves evidence and does not attempt a reset/reinstallation.

The fixed 43-exchange matrix covers:

- HTTP GET/HEAD baseline, five HTTPS GETs and five HEADs, two chunked responses.
- Wrong hostname and untrusted chain, with a valid recovery request after each.
- Stalled handshake, response headers and response body, each followed by recovery.
- Cancellation during a stalled handshake and body, each followed by recovery.
- Ten repeated successful requests and five authentication failures.

Each command's complete native result block, payload and host timing is retained.
The runner releases handles, observes a 180-second idle interval, checks release
again and requests a C64 CPU reset only when that cleanup action was explicitly
approved in the installation record. It never resets the management controller or
ESP32. A reset request alone is not confirmation that BASIC is visible.

`tools/https_test_endpoint.py` is a bounded, allowlisted fixture. It supports
ordinary HTTP, TLS and a stalled TLS handshake. TLS routes provide fixed 2048-byte
responses, chunked responses, incomplete bodies and stalled headers/bodies. It
defaults to loopback; LAN binding requires an explicit peer allowlist. Keep keys
outside the repository. Use the production-trusted certificate for the valid
endpoint, the same certificate with a mismatched hostname for the name rejection,
and a separate private certificate for the untrusted-chain endpoint. Do not alter
production roots or the clock to make the tests pass.

Endpoint logs must be on the same host, OS and monotonic clock as the runner.
The preflight rejects old/stopped/used logs, port mismatches and differing clock
domains before any device call. A Windows fixture and a WSL runner do not share
a clock domain; start the fixtures in the runner's environment instead. Cancel is
sent only after that endpoint records the relevant stall. After execution, stop
the fixtures and run `tools/audit_https_hardware.py` against their complete logs.
The audit requires exact requests, response bytes, one connection per exchange,
failed TLS before HTTP for authentication cases, observed client close during
stalls, complete closure and no extra/plaintext fallback connections. A run cannot
pass with a missing or truncated log. Hashes detect later changes relative to the
saved record; they do not authenticate the original evidence producer.

The 1.5-second cancellation/peer-close guard includes host observation overhead.
It does not prove the firmware's 500-ms internal cleanup target. The first matrix
does not qualify resource trends, physical restart identity, journal durability,
entropy, trusted certificate-date rejection or power-loss recovery. Those remain
explicit held phases, and a matrix pass is not a full hardware qualification.

## Device-free verification

### Standard runner entry

`run-tests -s https-hardware` is an explicit-only controlled campaign. No profile,
including deep/exhaustive, `--all`, or `--manual` selects it automatically. It must
run alone. Its dispatch occurs before target discovery, health probes, settings
capture/restoration, screen recording and reset/recovery orchestration. The
installation record supplies the sole target; do not supply a positional host or
`-H`. Ordinary execution options are rejected when they change defaults. The
campaign always has one attempt, regardless of the normal runner retry default.

Validate a plan without device access (the default action is `prepare`):

```sh
python3 run-tests -s https-hardware --https-plan plan.json -o runs/https-preparation
```

The directory must be new. Preparation records `SKIP` for the hardware suite and
exits zero for successful offline validation; it is not a hardware pass. Invalid
arguments exit 64. Invalid plans or failed child operations exit 3 and retain logs.

Only after installation, recovery and every operator gate have been qualified,
explicitly authorize one execution using:

```sh
python3 run-tests -s https-hardware --https-action execute \
  --https-plan plan.json --https-installation installation.json -o runs/https-execution
```

The existing plan, evidence hashes, expiry, candidate identity and fresh peer-clock
checks still run before device access. Authentication uses the standard `-p` or
`U64_PASS`; it is passed through the child environment, not logged in its command.
There is no automatic retry, configuration write, recovery command or management
reset. The explicitly gated final C64 CPU reset remains part of the campaign.
Create `runs/https-execution/campaign/operator.stop` to stop the campaign at its
next guard. Preserve that directory; never reuse it or silently resume a stopped run.

Completed exchanges are recorded as `awaiting-independent-peer-audit`, with a
`SKIP` hardware verdict and exit 3, so an unaudited run cannot pass a release gate.
After stopping the fixtures normally, audit all their complete logs offline:

```sh
python3 run-tests -s https-hardware --https-action audit \
  --https-evidence runs/https-execution/campaign \
  --https-peer peers/http.jsonl --https-peer peers/trusted.jsonl \
  --https-peer peers/untrusted.jsonl --https-peer peers/stall.jsonl \
  -o runs/https-audit
```

Repeat `--https-peer` for every distinct peer log (the trusted and wrong-name
origins may share a listener). Audit rereads the raw evidence; it does not accept
an earlier audit verdict as proof. A successful audit exits zero and reports only
`saved-evidence-audit-passed`. It does not distinguish fabricated traces from
physical evidence and does not qualify the held hardware phases above.

Each invocation retains standard `https/run.jsonl` plan/suite/run records,
`result.json` and `https/controlled-https-hardware.log`. Preparation/execution artifacts live under `campaign/`;
successful audits add `audit.json`. Existing artifacts are never overwritten.
Run `python3 tools/tests/test_run_tests_https.py` directly for device-free entry,
gate and reporting regressions. This policy suite is also registered as
`https-runner-policy` from the quick profile upwards; ordinary `run-tests` profiles
still perform their normal bench setup, so use the direct script when offline.

### Component and loopback checks

Run `python3 tools/tests/test_https_hardware.py` for execution gates, full synthetic
trace audit, evidence mutations, orchestration stop behavior and loopback fixture
checks. These synthetic results must never be published as physical results.
The ordinary regression remains `tools/test_https_host.py` with the pinned TLS
tree. Retain failures alongside later successful attempts.

For runner-to-fixture integration, run
`python3 tools/tests/test_https_runner_loopback.py --output OUTPUT_DIRECTORY`
under Linux with OpenSSL installed. It executes the unchanged 43-case controller
against real loopback HTTP/TLS endpoints through a substituted host transport.
The substitute creates UCI-shaped results; it does not execute the 6502 agent,
management firmware or ESP32 backend. Every saved run is marked as host-only.
An in-process socket guard rejects non-loopback connections and binds. Temporary
test certificates are trusted only by that host client; private keys are removed
afterward. Production trust configuration and installation gates are unchanged.

This integration test uses a 750-ms host-client timeout for fault injection and
checks prompt cancellation separately, so it does not qualify firmware deadlines.
It retains the real 180-second idle interval, complete peer logs, raw simulated
transactions, an independent matrix audit and an operator-stop rejection case.
Only the simulated final CPU-reset request is recorded; no device is contacted.
