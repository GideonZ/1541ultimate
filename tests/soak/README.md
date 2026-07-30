# Soak and stress tests

These tests exercise a real device repeatedly to expose failures that short,
deterministic E2E checks may not find: connection leaks, listener exhaustion,
transport degradation, concurrency races, and broken stream recovery. They are
kept outside `tests/e2e/` because duration and load are part of their purpose;
they are not a release-gate functional suite.

## Structure

| Directory | Scope |
| --- | --- |
| `network/` | Ultimate network surfaces: ICMP, UDP/64 identity discovery, the separate TCP/64 DMA command channel, Telnet, FTP, REST, the optional modem listener, and audio/video UDP streams |

`network/connection_test.py` is the entry point. The `*_probe.py` modules,
`stream_monitor.py`, and `connection_runtime.py` are its protocol drivers and
shared runtime. The harness was imported
from ViviPi's `scripts/u64/` at commit `00ca1962ffe4356f7cca0c592bfb6e8519564927`.

## Running

Use an explicit host so the target is unambiguous. Python 3.11 or newer and
the system `ping` command are required; the remaining probes use the standard
library.

```bash
# Inspect the complete CLI without contacting a device.
tests/soak/network/connection_test.py --help

# Twelve-hour, one-runner concurrent soak with read/write probes and streams.
tests/soak/network/connection_test.py --profile soak -H u64

# Two-minute stress profile that saturates the four FTP and Telnet session slots.
tests/soak/network/connection_test.py --profile stress -H u64

# Short, read-only investigation.
tests/soak/network/connection_test.py --profile soak -H u64 \
  --duration-s 300 --surface read --stream
```

The CLI help is the authority for profiles, individual probes, protocol
surfaces, correctness modes, ports, credentials, and concurrency overrides.
The process exits non-zero if any probe or stream reports `FAIL`.

Preserve timestamped output for diagnosis:

```bash
mkdir -p logs/soak
stamp=$(date -u +%Y%m%dT%H%M%SZ)
set -o pipefail
tests/soak/network/connection_test.py --profile stress -H u64 2>&1 \
  | tee "logs/soak/u64-network-$stamp.log"
```

## Safety

Run against a dedicated test device. The default profiles are active tests:
they create and remove `u64test_*` files under `/Temp`, write reserved C64
page-3 bytes, change the `Vol UltiSid 1` setting between `0 dB` and `+1 dB`,
open incomplete or malformed sessions, and enable network streams. Normal
shutdown disables streams and cleans up tracked FTP files, but an abrupt host
or device failure can interrupt cleanup.

The Telnet `vanish` mode is more invasive. It fills the device's Telnet session
table and temporarily changes a host network alias; it requires an unused LAN
address, an interface name, and passwordless `sudo -n ip`. Follow its CLI
constraints and never run it concurrently.

## Extending

- Put soak tests under the subsystem they load; add a sibling of `network/`
  only when another production surface needs sustained testing.
- Name executable entry points `*_test.py`; keep reusable protocol logic in
  narrowly named helper modules.
- Add a new network probe to the runner's choices, supported surfaces,
  correctness modes, and runner table together.
- Make generated resources unique per runner, verify every mutation, and
  clean up in `finally` or an exit handler.
- Keep runs bounded with timeouts, emit timestamped protocol/result details,
  and surface unexpected behavior as `FAIL`. Retries must be bounded and
  limited to documented transient protocol behavior.
- Document destructive behavior, privileges, target state, and cleanup
  limitations here; keep flag-by-flag detail in `--help`.

Before submitting changes, run `python3 -m py_compile tests/soak/network/*.py`
and exercise `--help`. Hardware validation should use a dedicated target and
retain its timestamped log.
