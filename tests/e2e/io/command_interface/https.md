# Native target-6 HTTP/HTTPS hardware tests

These suites belong to the command-interface subsystem. Functional cases live
in this directory, duration/repetition cases in `tests/soak/io/command_interface`,
and shared transport/evidence code in `tests/lib/https_native.py`.
Firmware preparation and host menu-lifetime fixtures remain in `tools/c64u_https`.

## Prerequisites and selection

Use Linux/WSL, Python 3.10 or newer and `64tass`, with the device reachable
through its REST API. Tests replace the running C64 program and RAM. Close the
Ultimate menu and stop other native-UCI tests first. Use a single device target:
`cartridge@computer` cannot identify the intended interface from native code and
is rejected. A cartridge in a physical C64 can be addressed by its own hostname;
this is not a claim of tested cartridge compatibility.

HTTPS requires this implementation, a supported TLS controller, configured trust
anchors and valid device time. Public HTTPBingo/HTTPBin/BadSSL fixtures require
Internet access. Diagnostic cases require bridge 1.18 telemetry. Missing
prerequisites, failed requests and incomplete cleanup fail rather than pass.
No test installs firmware or changes trust, time or Wi-Fi configuration.

All device suites use shared `-H/--host`, `-p/--password`, `-t/--timeout` and
`--color` arguments (`U64_HOST`, `U64_PASS`, `U64_TIMEOUT` defaults). Test results
use `tests/lib/report.py`, including `E2E_JSONL` under the common runner.

| Selector | Category | Cases / additional prerequisites |
| --- | --- | --- |
| `https-smoke` | E2E | Paired HTTP/HTTPS typed JSON; standalone options also exercise raw bytes and continuation limits |
| `https-lifecycle` | E2E | Abort after the first raw block, handle release, FREE_ALL, C64 reset and recovery |
| `https-diagnostic` | E2E | DNS and certificate failure, correlated diagnostics and recovery; optional controlled silent/close/reset listeners |
| `https-response` | E2E | Authenticated truncated response and deadline; probes public fixture behavior first |
| `https-wifi-loss` | E2E | Operator-assisted reconnect/recovery; controlled listener and explicit marker confirmations |
| `https-repetition` | Soak | Repeated paired requests; optional certificate, DNS and LAN framing faults |
| `https-timed` | Soak | Two hours by default, fresh memory metrics and unchanged controller boot identity |
| `https-harness` | Device-free E2E gate | Regression tests for evidence validation, cleanup and operator coordination |

Device suites are manual selections because they need configured HTTPS and
external services; Wi-Fi loss also needs an operator and a controlled listener.
They do not run in the ordinary `standard` profile; `deep` and `exhaustive`
include manual suites, so those profiles also require these prerequisites. The harness gate is automatic
from `quick` onward. Select an individual suite from the repository root:

```sh
python3 run-tests -H DEVICE -s https-smoke
python3 run-tests -H DEVICE -s https-lifecycle
python3 run-tests --soak -H DEVICE -s https-timed
python3 tests/lib/https_harness_test.py
```

Without `--output`, a standalone suite chooses a unique temporary evidence path;
under the runner it uses the directory containing `E2E_JSONL`. Explicit output
paths must be new. Keep evidence outside version control: response bodies may
contain whatever the selected endpoint returns. Historical hardware reports
refer to the original filenames under `tools/c64u_https`; relocating a suite
does not constitute a new hardware run. See [recorded device coverage](../../../../doc/https-device-validation.md).

## Hardware smoke test

After installation, `https_smoke_test.py` reuses the repository's native 6502 UCI agent and
REST clients. It replaces the C64's running program and RAM, but does not flash
firmware. Run on Linux/WSL with the device host and disposable GET URLs:

```sh
python3 tests/e2e/io/command_interface/https_smoke_test.py --host DEVICE \
  --url http://httpbingo.org/base64/eyJvayI6dHJ1ZX0= \
  --url https://httpbingo.org/base64/eyJvayI6dHJ1ZX0= \
  --expect-hex 0401026f6b0201 --output /private/path/json-result.json
```

`--raw` requires an HTTP 200 status, checks the 895-byte data/255-byte status
hardware limits and optionally compares exact body bytes with `--expect-hex`
or `--expect-file`. `--expect-blocks 895,129` also checks the exact continuation
lengths for a 1024-byte body. Without `--raw`, expected bytes are the existing
UCI typed object representation. `--expect-unavailable` requires status 503 and an
empty response, suitable for known certificate-failure fixtures. These are
network-dependent checks; a generic 503 alone does not identify its cause.
The agent wait covers the 15-second TLS deadline and drains are bounded at
4096 bytes. It retains first-block status for long raw headers, discarding
later continuation status strings. Output may contain complete response data;
use no credentials or private endpoints and keep logs outside version control.

## Repetition and controlled connection faults

`https_repetition_test.py` keeps one native 6502 agent running and alternates HTTP/HTTPS raw and
JSON exchanges. It frees request/response handles individually and checks handle
reuse, exact body bytes, continuation lengths, Idle completion, and configuration
preservation. It writes JSONL after every completed exchange; an application
failure is recorded without silently retrying it. A UCI transport failure stops
the run. At completion it resets only the C64 to stop the test program.

```sh
python3 tests/soak/io/command_interface/https_repetition_test.py --host DEVICE --rounds 40 --faults \
  --output /private/path/new-soak-directory
```

Each round has two requests. Output directories must be new. The optional heap
endpoint is probed read-only; a retail 404 is recorded as unavailable, not as zero
heap use or evidence of no leaks. Configuration equality is checked in memory; the output contains only canonical
SHA-256 digests, not configuration values or Wi-Fi credentials. This short repetition run is not an overnight soak.

For controlled LAN faults, run `https_fault_server.py` on a host address reachable by
the device (on Windows, use a Windows Python process rather than assuming WSL's
private address is reachable). It serves only hardcoded test responses, accepts
the named device and local host, and shuts down automatically after a bounded
interval. It changes neither firewall rules nor the device's trust store.

```sh
python3 tests/e2e/io/command_interface/https_fault_server.py --bind HOST_LAN_IP --device DEVICE_IP \
  --log /private/path/fixture.jsonl
python3 tests/soak/io/command_interface/https_repetition_test.py --host DEVICE_IP --rounds 1 --dns-failure \
  --http-fixtures http://HOST_LAN_IP:8765 --stall-url https://HOST_LAN_IP:8766/ \
  --output /private/path/new-fault-directory
```

The HTTP listener checks chunked binary data, HTTP errors, redirects and complete
connection loss/truncated bodies, with a healthy request after each failure.
The silent TCP listener records receipt of a TLS ClientHello and does not reply
for 22 seconds; HTTPS should expire at its 15-second deadline. This verifies a
handshake stall, not an authenticated server's stalled body. Stop the fixture
server after testing or allow its automatic shutdown. No Wi-Fi disconnect or
flash interruption is performed by these tools.

### Operator-controlled Wi-Fi interruption

`https_wifi_loss_test.py` repeats up to 20 silent-peer TLS handshakes on the C64,
storing up to 32 bounded result records in RAM. While the operator uses the
Ultimate Wi-Fi menu, the host waits for a local confirmation file and does not
read C64 RAM: the menu pauses the C64 and can change the memory visible to DMA.
Preparation and operator reconnection have no timeout. The 15-second HTTPS
deadline and bounded native history remain unchanged. Start the LAN fixture
when the operator is ready, with a lifetime sufficient for the test; an expired
fixture cannot establish interruption of an active request.

```sh
python3 tests/e2e/io/command_interface/https_wifi_loss_test.py --host DEVICE_IP \
  --stall-url https://HOST_LAN_IP:8766/ --output /private/path/new-wifi-run \
  --start-file /private/path/start --armed-file /private/path/armed \
  --finish-file /private/path/finished --cancel-file /private/path/cancel
```

All paths must be distinct and new. Before `start` exists, the runner waits
locally without accessing the device. When the operator is ready and outside
the Ultimate menu, atomically write `{"ready":true,"menu_exited":true}` to
`start`. Only then does native setup and the authenticated baseline begin.

After `armed` appears, the operator may disconnect and reconnect at their own
pace. After Link Up and exit from the Ultimate menu, atomically write
`{"reconnected":true,"menu_exited":true}` to `finished`. Write each marker to
a temporary sibling file, then rename it into place; a bare empty marker is
not confirmation. The controller of the test can write these files after
receiving the operator's messages. Waiting longer does not itself fail the test.

The runner makes no device calls during this wait. It then collects saved
outcomes and checks HTTP/HTTPS recovery without resetting or freeing all handles
between the interruption and recovery. Final cleanup resets the C64 only after
the explicit menu-exit confirmation. A failed recovery stops the test.

Creating `cancel` or interrupting the runner marks the test incomplete. If
already armed, it does not read RAM or reset the potentially open menu; cleanup
remains pending. Preserve evidence, confirm the operator has exited the menu,
and inspect device state before stopping the program or running another native
test. Exit status 2 denotes operator cancellation, rather than a device failure.

Separate timestamped network observations and the fixture's ClientHello log
must establish overlap with actual Wi-Fi loss. Expected 503 results alone do
not prove an interruption occurred. Native repetition still stops at 20 attempts
and keeps bounded history; it does not run indefinitely while waiting for a
person. The Ultimate menu pauses C64 execution, so an unhurried manual cycle
can verify reconnect/recovery without proving interruption of an active request.
`completed` in `fault-result.json` describes collection and recovery;
`active_tls_interruption_proven` remains false until independent assessment.

The native repetition control has a CPU-executed host regression test:

```sh
python3 -m pip install py65==1.2.0
python3 tools/c64u_https/check_native_repeat.py
```

It assembles the real agent and checks one-shot compatibility, bounded history,
error termination and host stop handling. The UCI transaction is substituted;
this does not replace hardware validation.

### Timed soak with controller memory measurements

Bridge 1.17 adds an optional `esp32` object to `GET /v1/machine:heap`.
The controller sends one nonblocking telemetry packet at most each second;
TLS work may delay sampling. Samples older than 30 seconds report
`available=false`, without invented zero measurements. Heap figures cover all
ESP32 allocations; the TLS stack figure is its worker's historical minimum
free space, in bytes. Largest free blocks are snapshots. Internal and all
8-bit-accessible heaps overlap and must not be added together. Controller
boot IDs distinguish a restart from normal changes in memory usage.

After installing and smoke-testing the metrics image:

```sh
python3 tests/soak/io/command_interface/https_timed_test.py --host DEVICE_IP --seconds 7200 \
  --output /private/path/new-two-hour-run --stop-file /private/path/stop-soak
```

The duration is elapsed test time, excluding initial setup; a final pair can
extend it by its bounded transaction time. The runner samples both processors'
memory every ten rounds, stops on an unexpected result without retrying the
exchange, and checks settings before its final C64 reset. Creating the stop
file stops at a round boundary and records an incomplete run, not a pass.
`long-result.json` preserves the outcome even if cleanup loses contact with
the device. A successful run proves only its observed duration and conditions;
heap samples require separate trend analysis.

### Failure diagnostics (bridge 1.18)

The public target-6 HTTP/HTTPS commands and response format are unchanged.
`GET /v1/machine:heap` adds `https_bridge.last_failure`: the first management
failure in the latest failed epoch, with stage, stage-specific code and detail.
Later successes, cleanup errors and delayed older work cannot erase it.
`esp32.last_tls_failure` latches the TLS worker's stage, `https_status`, original
Mbed TLS/trust error and certificate verification flags before stream teardown.
Both include epoch/session identifiers for correlation; they contain no URLs,
headers, body contents, keys or certificates. Counts reset at processor boot.
Zero count means no failure observed. Missing/stale ESP32 metrics are not proof
that no TLS failure occurred; management still accepts heap-only v1 telemetry.

TLS stage numbers are: 0 validation/allocation, 1 clock, 2 random seeding,
3 configuration, 4 trust store, 5 TLS setup, 6 hostname, 7 TCP open,
8 handshake, 9 response read, 10 request write. A zero code means no original
Mbed TLS/trust error was recorded, not success. A transport failure may have
a generic Mbed TLS wrapper code; consult management's socket/DNS evidence.
The OpenAPI schema documents management code/detail units. HTTP parser failures
are separate from authenticated TLS transport errors.

The hardware runner saves an unexpected exchange before taking a bounded,
delayed heap sample, allowing the one-second telemetry publisher to catch up.
It does not retry that exchange. Controlled local real-TLS host tests verify
failure stage/code/flags and unchanged UCI behavior under the production parser.
These host tests supply fixture roots only to their test process. They do not
change the device trust store or establish hardware recovery.

After operator installation, run the bounded diagnostic check with a new output
directory (never while another native test owns the C64):

```sh
python3 tests/e2e/io/command_interface/https_diagnostic_test.py --host DEVICE_IP \
  --output /private/path/new-diagnostic-run \
  --stall-url https://HOST_LAN_IP:8766/ --stall-log /private/path/fixture.jsonl
```

The optional stall URL uses the already-running silent listener from
`https_fault_server.py`. It never authenticates or serves a TLS response. Baseline and
recovery requests use the public test endpoint; certificate rejection uses
BadSSL. The runner compares fresh failure counters, epoch/session identities,
controller boot ID, TLS stage/status, certificate flags and persistence after
recovery. An unexpected failure stops it; a later recovery cannot convert a
failed earlier soak into a pass.

The silent-peer check also requires one server-side ClientHello within the
exchange window and a 14–19 second measured duration. Its internal status may
be timeout or transport error: the management TCP wait can expire before the
controller's remaining budget. Transport error is accepted here only alongside
the correlated `tcp_timeout` read event with code zero. Other transport failures,
early failures and absent/ambiguous server evidence remain failures. Certificate
verification flags `ffffffff` mean verification was unavailable, not evidence
of certificate rejection.

For controlled interruption during the handshake, start `https_fault_server.py` on
the LAN test computer with `--close-port 8777 --reset-port 8778`, an explicit
`--bind HOST_LAN_IP --device DEVICE_IP`, a new `--log` path and a bounded
`--seconds` lifetime. An optional `--stop-file` ends its listeners early.
Add these arguments to `https_diagnostic_test.py`:

```sh
--close-url https://HOST_LAN_IP:8777/ \
--reset-url https://HOST_LAN_IP:8778/ --close-log /private/path/fixture.jsonl
```

Each listener waits for a complete ClientHello, waits two seconds, then closes
the socket normally or requests an abortive close with `SO_LINGER`. It records
the receipt/action times and byte count without storing the handshake bytes.
The check requires one completed injection inside that request's time window,
an empty native 503, fresh matching failure identities, and a subsequent
authenticated HTTPS recovery preserving the failure record. A normal close
must report handshake EOF (`-0x7280`), while a reset must correlate a management
socket read error with the TLS transport wrapper error (`-0x6c00`). General
TLS status 5 alone does not prove certificate rejection.

These fixtures interrupt TLS before authentication. They do not test loss
during an authenticated HTTPS response, physically disconnect device Wi-Fi,
or establish the cause of an earlier unexplained 503. The reset log records
the socket operation, not a packet capture proving the wire flags.

### Authenticated response failures

`https_response_test.py --host DEVICE_IP --output /private/path/new-response-run`
checks an authenticated short response and a read deadline, each followed by
a successful HTTPS request. It replaces C64 RAM, preserves existing evidence,
stops on unexpected results and resets the test program during cleanup.
It uses the public HTTPBingo response-header fixture to declare 2,048 bytes
while returning a shorter body, and HTTPBin's 18-second drip fixture. Both
fixture behaviors are checked from the host before device execution; public
service behavior can change, and a failed preflight is not a firmware failure.

The runner requires an empty 503, new diagnostic evidence in the authenticated
read stage (or HTTP body EOF after clean TLS closure), unchanged boot identity
and persistence after recovery. A handshake or certificate failure cannot pass
as a response-read fault. The slow check requires a 12–19 second duration and
correlated read-timeout evidence. Host fixture observations are separate
connections, not measurements of how many body bytes the device received.
