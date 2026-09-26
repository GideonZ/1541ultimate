# Retail C64 Ultimate HTTPS test build

This is the device build path for the **Commodore 64 Ultimate** retail 1.1.0
baseline, including Slimline/BASIC Beige. It exports official source commit
`7b628eb166872965ea59d66f90ffd9bcf7d71d8a` and adds the shared target 6 HTTP/HTTPS
implementation from this checkout. It does not use another application project,
its prototype, RAM loader or installer.

The ordinary `target/u64ii/riscv/ultimate` build in the main checkout remains the
upstream contribution build. Its newer board/CPU assumptions must not be used
as the retail installation recipe. The retail snapshot preserves RV32I,
`COMMODORE=1`, product identity, menu/bling support, memory map, flash offsets,
controller power behavior and Wi-Fi bridge from the official baseline.

## Preparation and build

Use Linux or WSL with Python 3.9+, Git, GNU make, host GCC/G++, the xPack RISC-V
GCC 11.3.0 toolchain, ESP-IDF 5.3.6 with ESP32-S3 dependencies, and the host TLS
test dependencies described in `software/network/https/tests/README.md`.
Initialize the `software/lwip` and `software/httpd` submodules. The local Git
object stores must contain the full revisions pinned in `prepare.py`.

The HTTPD dependency needs the tracked `httpd-response.patch` until an upstream
revision includes the fix. For host tests or ordinary builds, first run
`python3 tools/c64u_https/httpd_patch.py`. This refuses conflicting edits and
is safe to repeat. Retail preparation applies the same patch automatically to
its exported dependency; it does not depend on uncommitted submodule changes.

Run from this checkout, selecting a **new** output directory:

```sh
python3 tools/c64u_https/prepare.py --output /path/to/c64u-build
. /path/to/esp-idf-5.3.6/export.sh
python3 tools/c64u_https/build.py \
  --tree /path/to/c64u-build \
  --cross /path/to/riscv-xpack/bin/riscv-none-elf- \
  --mbedtls-source /path/to/mbedtls \
  --mbedtls-build /path/to/host-mbedtls-build \
  --recovery-updater /path/to/c64u_v1.1.0.ue2
```

The legacy retail assembler rules also invoke `python`; make Python 3 available
under that name in the build environment. The source snapshot records hashes of
the selected working-tree files, adaptation recipe, baseline and dependencies.
It refuses to overwrite an existing directory. Keep build outputs on Linux's
filesystem under WSL. The preparation/build scripts do not upload, execute or
flash anything on a device. The separate smoke test below runs a C64 program.

The build checks the exact official updater SHA-256, compares the embedded FPGA
and ESP partition table with it, checks the management flash size and instruction
set, and verifies that the installer embeds each newly built image exactly once.
It runs portable sanitizer tests and the UCI regression/integration suites against
the **retail snapshot**, including real local TLS and certificate rejection.

Successful output is `candidate/c64u-1.1.0-https-test.ue2` plus `manifest.json`.
The `.ue2` is the retail updater's `update.app`, not a renamed management image.
It is a test candidate, not a hardware-validated release or an exact backup of
any installed experimental firmware. Read `doc/https-device-validation.md`
before an installation review.

## Adaptation boundaries

- The HTTP target, HTTPS core, UART worker and epoch logic are shared source.
- The newer HTTP parser dependency is pinned separately because retail 1.1.0
  predates target 6. JSON, string and stream helpers needed by target 6 are
  included and compiled with the rest of the retail firmware.
- The recorded HTTPD patch validates client response framing and chunk endings;
  the shared UCI HTTP/HTTPS client opts into strict response mode. The incoming
  management parser and retail CommoServe response mode keep their behavior.
- A read-only `/v1/machine:heap` diagnostic reports the retail newlib allocator's
  allocated bytes, arena size and reusable space/blocks. Unlike newer upstream,
  retail uses `heap_3`/newlib, so this does not report FreeRTOS free/minimum heap,
  total free RAM, ESP32 TLS allocations or task stack space.
- Retail `IndexedList::remove` retains its boolean return contract. JSON finds
  the removed index explicitly so old browser/drive users remain compatible.
- Retail CommoServe retains its service and behavior; its private body type and
  callbacks are isolated from the shared HTTP client's names.
- Only TLS hooks are added to the retail controller dispatcher and management
  Wi-Fi commands. Newer upstream power/wake features are not pulled in.
- The existing upstream `replace_root_state` repair is backported to the retail
  configuration browser. It frees the replaced initial state and updates both
  state pointers. Retail actions that allocate a page now use
  `OwnedConfigBrowser`, which destroys the state chain before the owned page.
  Ordinary configuration browsers still borrow their roots; static audio and
  general settings roots keep their existing caching behavior. The advanced
  owned root also deletes its store/group wrappers. These fixes address two
  reproduced ownership leaks; they do not account for every retained device
  allocation after menu or network activity.
- Retail context menus release their windows during hide/cleanup and direct
  destruction, preserving border restoration. Drawing without a window is safe.
  The host regression reproduces ten retained windows over ten root menu
  appearance/hide cycles, then checks the repair, repeated cleanup, nested
  teardown and borrowed persistent actions with memory sanitizers.
- Build identity says `C64U HTTPS TEST`, and records the baseline plus a digest
  of shared source inputs. It must not be presented as stock firmware.

The full updater replaces management/FPGA and can replace controller bootloader,
partition table/application, bundled flash files and configuration. Matching the
existing FPGA/partition contents does not make installation an application-only
operation. Preserve configuration and a reviewed recovery path first. Runtime
memory and sustained UART coexistence still need device tests; compilation and
host tests cannot establish those results. Focused HTTP/HTTPS hardware checks
have passed for the reviewed retail candidate; see `doc/https-device-validation.md`
for the exact package, observations and remaining limits.

## Hardware cleanup and memory checks

After installation, `soak.py` samples the management heap every ten rounds when
`/v1/machine:heap` is available. Compare the retail `allocated` field after
warmup, with identical measurement conditions; arena slack is not total free RAM
and steady samples do not establish ESP32 or stack safety. The separate
`lifecycle.py --host DEVICE --output NEW_DIRECTORY` checks native abort after
the first raw reply block, FREE_ALL with live object handles, and C64 reset with
live handles, with subsequent exact-byte recovery and heap samples. These tools
replace the C64 program/RAM; never run two native-UCI tests simultaneously.
The repetition runner records UTC timestamps for attempts, completed exchanges
and heap samples. An attempt without a matching completed exchange remains in
`attempts.jsonl`; `completed_without_exception` in `summary.json` distinguishes
an interrupted run from zero failed recorded exchanges. Preserve interrupted
outputs when repeating a run; a successful repeat does not explain an outage.

## Recipe regression tests

```sh
python3 -m unittest discover -s tools/c64u_https -v
```

Tests check preservation of retail board/FPGA/power source, rejection of source
drift and wrong baselines, non-overwriting preparation, forbidden instruction
sets, stale/duplicate embedded images, and an unreviewed recovery file. The
generated source is also compiled and its actual HTTP/HTTPS behavior is tested
by `build.py`; string checks alone are not the firmware validation.

The recipe tests run extracted production menu constructors/destructors under
ASan/UBSan, checking normal and nested states, repeated opening, virtual
destruction, borrowed roots, and balanced path/observer registrations. The
unchanged retail baseline must reproduce five orphan states in five cycles;
the prepared tree must retain none. UI/RTOS dependencies are substituted, so
this is not a complete menu rendering/cache or target allocator test.
`build.py` repeats the fixed ownership test against the prepared source.

The separate page-lifetime regression extracts the actual retail menu factories,
configuration-page constructors/destructors and uses the real `Browsable` and
`IndexedList` implementations. Synthetic stores provide three borrowed settings
per page. Old page/group actions retain four nodes per cycle; five cycles retain
20 nodes. The repaired actions retain zero. Nested advanced-page teardown and
repeated access to borrowed static audio/general settings roots are also checked
under ASan/UBSan. Rendering, real configuration callbacks and target allocator
byte accounting remain outside this host fixture.

## Hardware smoke test

After installation, `smoke.py` reuses the repository's native 6502 UCI agent and
REST clients. It replaces the C64's running program and RAM, but does not flash
firmware. Run on Linux/WSL with the device host and disposable GET URLs:

```sh
python3 tools/c64u_https/smoke.py --host DEVICE \
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

`soak.py` keeps one native 6502 agent running and alternates HTTP/HTTPS raw and
JSON exchanges. It frees request/response handles individually and checks handle
reuse, exact body bytes, continuation lengths, Idle completion, and configuration
preservation. It writes JSONL after every completed exchange; an application
failure is recorded without silently retrying it. A UCI transport failure stops
the run. At completion it resets only the C64 to stop the test program.

```sh
python3 tools/c64u_https/soak.py --host DEVICE --rounds 40 --faults \
  --output /private/path/new-soak-directory
```

Each round has two requests. Output directories must be new. The optional heap
endpoint is probed read-only; a retail 404 is recorded as unavailable, not as zero
heap use or evidence of no leaks. Private configuration snapshots stay in the
output directory. This short repetition run is not an overnight soak.

For controlled LAN faults, run `fault_server.py` on a host address reachable by
the device (on Windows, use a Windows Python process rather than assuming WSL's
private address is reachable). It serves only hardcoded test responses, accepts
the named device and local host, and shuts down automatically after a bounded
interval. It changes neither firewall rules nor the device's trust store.

```sh
python3 tools/c64u_https/fault_server.py --bind HOST_LAN_IP --device DEVICE_IP \
  --log /private/path/fixture.jsonl
python3 tools/c64u_https/soak.py --host DEVICE_IP --rounds 1 --dns-failure \
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

`wifi_loss_native.py` repeats up to 20 silent-peer TLS handshakes on the C64,
storing up to 32 bounded result records in RAM. While the operator uses the
Ultimate Wi-Fi menu, the host waits for a local confirmation file and does not
read C64 RAM: the menu pauses the C64 and can change the memory visible to DMA.
Preparation and operator reconnection have no timeout. The 15-second HTTPS
deadline and bounded native history remain unchanged. Start the LAN fixture
when the operator is ready, with a lifetime sufficient for the test; an expired
fixture cannot establish interruption of an active request.

```sh
python3 tools/c64u_https/wifi_loss_native.py --host DEVICE_IP \
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
python3 tools/c64u_https/long_soak.py --host DEVICE_IP --seconds 7200 \
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
python3 tools/c64u_https/diagnostic_smoke.py --host DEVICE_IP \
  --output /private/path/new-diagnostic-run \
  --stall-url https://HOST_LAN_IP:8766/ --stall-log /private/path/fixture.jsonl
```

The optional stall URL uses the already-running silent listener from
`fault_server.py`. It never authenticates or serves a TLS response. Baseline and
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

For controlled interruption during the handshake, start `fault_server.py` on
the LAN test computer with `--close-port 8777 --reset-port 8778`, an explicit
`--bind HOST_LAN_IP --device DEVICE_IP`, a new `--log` path and a bounded
`--seconds` lifetime. An optional `--stop-file` ends its listeners early.
Add these arguments to `diagnostic_smoke.py`:

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

`response_diagnostic.py --host DEVICE_IP --output /private/path/new-response-run`
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
