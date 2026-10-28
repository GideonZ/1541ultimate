# HTTP over the standalone TLS adapter

This host-only target exercises `HttpRequest` with `TlsHttpConnection` and real
TLS 1.2 loopback peers. It also supplies the adapter to target-6 HTTPS routing.
It does not install a TLS provider in production firmware. The existing HTTP
commands, response formats and socket default remain unchanged.

## Dependencies and execution

Use Linux with GCC/G++, CMake, Ninja, Python 3 and OpenSSL. Supply an external
standalone TLS checkout at revision
`529b0450955611bd82db921db3bae10222203eba`, with Mbed TLS revision
`2b96dd8eebe880f304c69976b3c2fa0c5100cbb6` in its `.deps/mbedtls` directory.
Initialize this repository's pinned `software/httpd` submodule as well.
The external core is compiled directly; it is not copied into the HTTP parser.
Its `tests/run_probe.py` and `tests/security_peers.py` supply certificate fixtures.

From the repository root, with `TLS_ROOT` pointing to that checkout:

```sh
cmake -S target/pc/linux/test_http_tls -B /tmp/http-tls-build -G Ninja \
  -DTLS_STREAM_ROOT="$TLS_ROOT"
cmake --build /tmp/http-tls-build -j4
/tmp/http-tls-build/test_https_routing
python3 software/network/tests/run_http_tls.py \
  --binary /tmp/http-tls-build/test_http_tls --tls-root "$TLS_ROOT" \
  --output /tmp/http-tls-rsa
python3 software/network/tests/run_http_tls.py \
  --binary /tmp/http-tls-build/test_http_tls --tls-root "$TLS_ROOT" \
  --output /tmp/http-tls-ecdsa --ecdsa
python3 software/network/tests/run_http_tls.py \
  --binary /tmp/http-tls-build/test_http_tls --tls-root "$TLS_ROOT" \
  --output /tmp/https-target-rsa --routing
python3 software/network/tests/run_http_tls.py \
  --binary /tmp/http-tls-build/test_http_tls --tls-root "$TLS_ROOT" \
  --output /tmp/https-target-ecdsa --routing --ecdsa
```

Output directories must not exist. They retain raw subprocess logs, independent
peer observations, certificate validation metadata and a complete result list.
Each invocation creates temporary test-only certificate keys in its output;
do not use these certificates for anything outside the fixture.

## Coverage and limits

`run_http_tls.py --uart` uses the production `provider_data` wire codec,
single-slot delivery queues, owner execution and manager request mailbox. It runs
the same fourteen scenarios as `--connected`, with both certificate profiles.
Copied packets are transferred by a host wire fixture; no physical UART timing
or real FreeRTOS scheduler is emulated. `test_provider_data` covers malformed
frames, padding, overflow, late completion, ticket/request validation, queue
deadlines and unavailable backends. Owner-task and DMA-adapter tests separately
exercise actual firmware routing, copied input storage and terminal task failure.

Data frames use reserved command 2/subcommand 0xfd, magic PSD1/version 1, a fixed
80-byte little-endian header and 256-byte zero-padded payload (336 bytes total).
They carry provider boot/epoch, manager incarnation, session and request IDs.
Open/read/write are data operations; close and cancellation retain the existing
control protocol and require its cleanup acknowledgement. One pending operation
is accepted; queue waiting consumes the original session budget. Malformed data,
overflow and failed transmit retire the generation. No automatic retry is made.

The production owner task exposes copied submit/collect APIs, but the product HTTP
provider is not registered and the task remains uninstantiated. The experimental
ESP32 owner has no embedded TLS backend and returns Unavailable, advertising no
capability. The host fixture supplies the qualified TLS backend for these tests.
Native identity, embedded TLS callbacks and physical resource/timing qualification
are still required before activation. This is not a firmware-ready HTTPS claim.

`run_http_tls.py --connected` exercises the existing C64 HTTP command interface
through a proxy, copied data operations, ManagerDelivery/ManagerRunner, the
provider Worker and real TlsHttpConnection/Mbed TLS. Run it once per RSA/ECDSA
certificate profile. It includes successful HTTPS, hostname/chain/date failures,
handshake/read/total deadlines, command cancellation and simulated provider or
manager loss during handshake/read. Every case has a fresh successful recovery.
Independent peers verify exact request bytes, authentication and connection close.

The local data transport caps each copy at 256 bytes and checks session identity
and request order before touching TLS. It fences replies after blocked work and
requires control cleanup acknowledgement before healthy reuse. Manager loss uses
a 350 ms provider lease; other cases use 1000 ms. Neither path fabricates a remote
close after losing its owner. Existing 750 ms cancellation and 630-900 ms timeout
bounds remain in force. The 256-byte choice is a test boundary, not a committed
firmware buffer size or throughput claim.

This fixture supplies the missing connection between components locally. Its
copied synchronous transport is not a firmware UART data protocol, task queue or
physical restart. Recovery constructs fresh owners and a new provider boot ID.
Production owner-task data delivery, native restart identity, embedded TLS port
qualification and actual device scheduling/resources still require integration.

`test_provider_task` runs the optional management owner-task entry against
deterministic scheduler and WiFi shims, using the actual UART adapter, manager
and provider worker. It checks qualification gates, one-time identity sampling,
creation/binding cleanup, the startup barrier, stale manager/provider identities,
boot/reply deadlines, tick rollover and backwards time, send/loss failures and
stop races. A copied boot snapshot never bypasses a concurrent stop request.

No default task instance is installed. The management FPGA wrapper currently
disables its random-number peripheral, so a native manager incarnation source
remains unqualified. A nonzero test identity is not evidence of restart freshness.
Activation requires an externally qualified source; MAC/time/counter fallbacks
are not supported. The existing provider boot snapshot is only liveness evidence;
the subsequent control exchange must also match manager incarnation and provider
boot before becoming active. Replayed replies cannot satisfy that exchange.

The owner uses the existing 1600-word minimum stack and driver priority, with a
10 ms poll and 1500 ms boot wait. The task exits on failure or stop and cannot be
restarted on the same object. The bound object must have firmware lifetime.
These are configured bounds, not measured scheduler latency or stack high-water
marks. Session data requests are not exposed across tasks until their bounded
delivery path is implemented. HTTP commands and default provider activation are
unchanged; the TLS data plane and physical qualification remain separate work.

`test_provider_runner` exercises the serialized manager control driver through
the UART adapter and real provider worker, with copied wire frames and controlled
timestamps. It covers offer/initialize, conservative lease renewal, cleanup
acknowledgements, delayed and stale replies, send failures, exact deadlines,
provider/link cancellation, queue overflow, explicit restart and terminal clock
regression. An interrupted renewal retires the grant; unreachable remote
resources are released by their lease rather than a fabricated acknowledgement.

The driver consumes at most one reply and sends at most one control frame per
step. Its owner must call it often enough to meet the configured reply and lease
bounds. A missed deadline fails closed without retry. Session operations and
driver calls must share one serialized owner; ISR ingress only copies replies
or signals loss. The optional driver is compiled into the management build but
not instantiated: qualified manager identity, task activation, the TLS data plane
and hardware scheduling/resource validation remain required. These controlled
timestamps do not establish real firmware latency.

Both certificate profiles cover authenticated HTTP, certificate chain/hostname/
date rejection, unavailable trusted time and entropy, failed allocation,
handshake/read/write deadlines and cancellation, cancellation before opening,
clean TLS EOF, and truncated responses. Close-delimited responses additionally
require authenticated TLS shutdown: sending the complete body followed by a
bare TCP close must fail and expose no partial body. Every case includes a fresh successful
HTTP recovery exchange. HTTP parsing uses fragmented TLS transport reads/writes.
Write-stall cases inject a cooperative blocking callback after authentication;
they do not measure real network backpressure.

One 650 ms deadline covers open and subsequent I/O, including a 250 ms injected
open delay. The fixture requires 630–900 ms elapsed for timeout cases,
cancellation within 750 ms, and repeated cleanup within 250 ms. Those bounds
describe this cooperative host callback implementation, not firmware scheduling.

ASan and UBSan instrument the executable, TLS core and Mbed TLS by default.
Descriptor counts and linked `calloc`/`free` accounting must balance after every
exchange. This is scoped ownership evidence, not a device-wide heap measurement.
The peers verify exact HTTP bytes and the absence of application requests after
authentication failures. Subprocesses have a 12-second outer bound, and peer
threads must stop within six seconds. A timeout or missing result is not a pass.

No device, TAP interface, DNS lookup or external service is used. The socket
callback deliberately maps fixture hostnames to loopback while TLS verifies the
requested hostname. Production entropy, UTC qualification, trust storage,
interprocessor integration, firmware resource limits and hardware validation
remain separate work.

`test_https_routing` uses observable link wrappers for the default HTTP socket
path and a deterministic secure-provider double. It checks missing factories,
null creation and failed open/write/read without any plaintext fallback, URL
selection, binary POST rendering, raw/status/JSON parity, exact reply-block
boundaries, response-header cloning, conversion/capacity failures and provider
ownership through completion, abort, reset, free-all, replacement and destruction.
These doubles establish routing behaviour, not TLS authentication.

The `--routing` matrix separately runs real TLS through target-6 command messages,
including certificate failures, unavailable providers, clock/entropy failures,
cancellation, total deadlines and truncation. Each case has its own authenticated
recovery, independent peer records and allocation/descriptor balance checks.

The adapter-only `closed-before-open` case retires an unused adapter, then checks
that open fails without a socket or application bytes. A fresh adapter must still
complete its recovery exchange. Closing never makes a connection reusable.

`test_command_cancellation` compiles the actual command dispatcher and ISR code
against fake registers and queues. Run it directly from the build directory. It
checks initial/continuation interruption, reset/abort, dropped notification with
stale events still queued, owner cleanup and fresh-command recovery. This is not
an FPGA or scheduler emulator. The routing suite separately tests token expiry,
factory delivery, buffered-response fencing and generation exhaustion. The real
TLS routing matrix adds `command-pre-cancel`, `command-handshake-cancel` and
`command-read-cancel`, using the same token type and 750 ms cancellation bound.

### Owner queue binding and optional embedded backend

`OwnerHttpProvider` implements the existing secure factory using a single
`HttpOwnerEndpoint`. Use one factory per endpoint and retain both until every
connection has been destroyed. `ProviderManagerTask` implements this endpoint;
pass the factory to the existing `HttpTarget` provider injection point after
qualified startup. It is not registered automatically. Only the owner task
touches the data client/runner; command callers submit and collect copied frames.
Short reads/writes are bounded at 256 bytes. One exchange deadline spans all
operations. A stalled owner is retired after the exchange plus cleanup budget.

`provider_data::TlsBackend` transfers a TLS resource to the control Worker before
opening a socket. Its platform must poll the bound cancellation predicate during
DNS, connect and every network wait. Cleanup is owner-only after TLS unwinds.
The `--bound` RSA/ECDSA suites use these production classes and real Mbed TLS,
with copied host packets and controlled host transport callbacks. They do not
measure physical UART or FreeRTOS scheduling. `test_owner_http` covers mailbox
exclusion, partial transfers, stopped owners, deadline wrap and late output.

The ESP32 build optionally selects `ULTIMATE_PROVIDER_TLS` together with control
delivery. Supply `TLS_STREAM_ROOT` from standalone revision
`529b0450955611bd82db921db3bae10222203eba`; CMake verifies core/header/DNS hashes.
The build requires certificate date verification. Before `provider_control_start`,
explicitly call `provider_control_configure_tls` with roots and live predicates
qualifying the actual UTC clock and hardware entropy. There is no automatic trust
store, clock qualification or entropy fallback. An unprovisioned build advertises
zero capabilities. Configuration is boot-time only and immutable after startup.
The optional TLS worker reserves 12288 stack bytes; high-water is not measured.

Manager incarnation, trusted provisioning, lease/control servicing during long
TLS calls and device resource/scheduling measurements remain activation gates.
Default firmware does not enable this path or change HTTP commands.
