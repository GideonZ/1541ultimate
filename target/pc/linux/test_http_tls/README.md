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
