# Soak and stress tests

Tests that exercise a real device repeatedly to expose failures short
deterministic checks do not reach: connection leaks, listener exhaustion,
transport degradation, concurrency races and broken stream recovery. Duration
and load are part of the result, so these are not part of the release gate and
are not registered in `run-tests`.

| Path | Scope |
| --- | --- |
| `network/connection_test.py` | Long-running soak and stress across ICMP, UDP/64 identity discovery, the TCP/64 DMA command channel, Telnet, FTP, REST, the optional modem listener, and audio/video UDP streams |
| `network/listener_soak_test.py` | Short soak (about two minutes) that churns abandoned Telnet/FTP connections while REST stays in use, checking no session slot is lost and REST latency does not degrade |

`network/`'s `*_probe.py` modules, `stream_monitor.py` and
`connection_runtime.py` are protocol drivers and shared runtime for
`connection_test.py`.

## Running

`./run-tests -H <host> --soak` includes this stage, using the bounded
`stress` profile. To invoke a suite directly, use an explicit host. Python 3.11
or newer and the system `ping` command are required.

```sh
# Inspect the complete CLI without contacting a device.
tests/soak/network/connection_test.py --help

# Twelve-hour, one-runner concurrent soak with read/write probes and streams.
tests/soak/network/connection_test.py --profile soak -H u64

# Two-minute stress profile that saturates the four FTP and Telnet session slots.
tests/soak/network/connection_test.py --profile stress -H u64

# ~2 min: churns abandoned listener connections and checks REST does not degrade.
tests/soak/network/listener_soak_test.py -H u64 -p PASSWORD
```

`--help` is authoritative for profiles, probes, protocol surfaces, correctness
modes, ports, credentials and concurrency overrides. A run exits non-zero if
any probe or stream reports `FAIL`.

Preserve timestamped output for diagnosis:

```sh
mkdir -p logs/soak
set -o pipefail
tests/soak/network/connection_test.py --profile stress -H u64 2>&1 \
  | tee "logs/soak/u64-network-$(date -u +%Y%m%dT%H%M%SZ).log"
```

## Safety

Run against a dedicated test device. The default profiles are active tests:
they create and remove `u64test_*` files under `/Temp`, write reserved C64
page-3 bytes, change the `Vol UltiSid 1` setting between `0 dB` and `+1 dB`,
open incomplete or malformed sessions, and enable network streams. Normal
shutdown disables streams and cleans up tracked FTP files, but an abrupt host
or device failure can interrupt cleanup.

The Telnet `vanish` mode is more invasive. It fills the device's Telnet session
table and temporarily changes a host network alias; it needs an unused LAN
address, an interface name and passwordless `sudo -n ip`. Follow its CLI
constraints and never run it concurrently.

## Rules for extending

- Put a soak test under the subsystem it loads. Add a sibling of `network/`
  only when another production surface needs sustained testing.
- Name executable entry points `*_test.py` and keep reusable protocol logic in
  narrowly named helper modules. Python only.
- Add a new network probe to the runner's choices, supported surfaces,
  correctness modes and runner table together.
- Make generated resources unique per runner, verify every mutation, and clean
  up in a `finally` path or exit handler.
- Keep runs bounded with timeouts, emit timestamped protocol and result detail,
  and surface unexpected behaviour as `FAIL`. Retries must stay bounded and
  limited to documented transient protocol behaviour.
- Document destructive behaviour, privileges, target state and cleanup
  limitations here; keep flag-by-flag detail in `--help`.
