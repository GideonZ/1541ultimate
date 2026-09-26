# HTTPS client integration proposal

Status (2026-09-26): HTTP UCI target 6 uses a TLS/UART provider for HTTPS on
the u64ii management target. Controller bridge 1.18 executes TLS on a separate
worker and exposes correlated failure records and memory measurements.
The retail C64 Ultimate v10 candidate passed 2,846 HTTP/HTTPS exchanges over
two hours. The installed v11 subsequently passed focused response-fault and
manual Wi-Fi reconnect/recovery checks. The earlier intermittent v9 empty 503
still has no established root cause; the passing runs do not explain it.
The v12 retail candidate backports a confirmed pre-existing menu state leak
repair, with host ownership regression tests. Its post-install HTTP/HTTPS
smoke run passed, but two batches of local Wi-Fi menu cycles repeatedly retained
additional management memory. A separate owned-page leak was reproduced and repaired in the retail
recipe. V13 is now operator-confirmed installed and passed the same ten HTTP/HTTPS
smoke checks, but its ten local menu cycles retained 632 additional management
bytes. A further persistent-menu window leak has been reproduced and repaired
in the retail recipe. V14 is operator-confirmed installed and its ten HTTP/HTTPS
smoke exchanges passed. Following a zero-IPv4 interruption and manual network
recovery, separate single-cycle and five-cycle menu checks showed zero
management allocation growth. The initial interrupted attempt remains incomplete
and its address-loss cause is unresolved.
V14 subsequently passed a two-hour native run: all 2,966 HTTP/HTTPS exchanges
succeeded, original configuration and controller boot were preserved, and both
failure records remained zero. Management allocation was constant across 151
samples; final ESP32 idle free memory was eight bytes lower. Isolated ping
misses were observed without native exchange failures. This result does not
explain the earlier incidents or complete stable-release qualification.
See `https-device-validation.md` for exact evidence and
`https-release-readiness.md` for review scope and remaining gates.
This is an experimental integration, not a production-ready release.

Host tests exercise UCI with a simulated secure stream, session ownership with
fake I/O, and `tls_stream.c` against a real local TLS server. A combined suite
also joins UCI, the shared HTTP renderer/parser and real TLS through host TCP.
Those host tests do not exercise both firmware schedulers and UART; the focused
device checks do. Internal stream transfers use
at most 512 bytes per operation and one 15-second total exchange budget.

## Architecture and compatibility

Use the Ultimate network stack for DNS and TCP, including ordinary Wi-Fi.
The internal ESP32-S3 performs TLS over a supplied byte stream carried by
bounded UART messages. Keep the raw Ethernet bridge, power management,
reconnect, wake-on-Wi-Fi and other controller functions operational.
Run HTTPS work outside the UART dispatcher so stream replies and ordinary
network traffic can continue. Other hardware needs explicit capability
detection; HTTPS must never silently fall back to plaintext.

Use HTTP UCI target 6 and its existing commands, request rendering, response
parsing, header/body handles and raw reply chunking. The URL selects the
transport. There is no new public start/poll/read protocol for HTTPS.
`HttpConnection` is the internal stream boundary under `HttpRequest`; its
secure provider authenticates TLS and supplies decrypted HTTP bytes to the
same parser. Register the provider once at startup. Prototype target 14 and
UART command 0x30 are not public allocations.

The management integration handles `c64_reset` notifications between commands,
closes only its own socket, and matches replies by session and sequence.
In-flight commands remain bounded by the exchange deadline; reset is not a
hard real-time cancellation of an active command. UART sends never wait for
a full queue. A missing close frame is backed by local session/socket expiry.
DNS runs asynchronously on the lwIP thread with generation-tagged callbacks,
so a late DNS answer cannot write to a caller's expired stack or newer lookup.

Management registers the provider only on the u64ii main target, and rejects
HTTPS immediately until a compatible bridge version has been detected. It
supplies fresh UTC from its existing SNTP client. The ESP verifies clock age
(at most 24 hours) before TLS and uses the ESP certificate bundle. No separate
ESP network configuration or application-specific server list is introduced.

## Portable request core (not the public UCI contract)

The following limits describe the earlier standalone core only. It is not
called by the UCI path, and these are not newly imposed limits on HTTP UCI.
The shared HTTP parser currently has a 2048-byte header buffer and the UCI
response collector reserves a terminator in its 16384-byte body buffer.
The shared HTTP client now opts into strict response framing: malformed lengths,
ambiguous framing and incomplete chunks fail without a returned partial body.
The targeted regression coverage does not establish full HTTP conformance or
a complete resource audit; see `https-device-validation.md` for its boundaries.

The caller supplies a DNS hostname, port, origin-form path with optional query,
method, headers and binary body. Hostnames use ASCII DNS label syntax;
international names require an ASCII representation. IPv6 literals are not
part of this first contract. No application-specific host list is used.

Initial limits:

| Item | Limit |
| --- | --- |
| Hostname | 253 bytes, 63 per label |
| Path and query | 384 bytes |
| Method | 16 token characters; CONNECT unsupported |
| Caller headers | 16; 1024 bytes including separators and CRLF |
| Request body | 4096 bytes |
| Response body | 4096 bytes, binary, no trailing NUL |
| Request deadline | 1–15000 ms, shared by open and reads |
| Read operation | At most 512 bytes |

The adapter owns Host, Content-Length and connection framing. Caller headers
cannot override these, request an upgrade or use Expect. Header names and
values cannot inject CRLF. The port must bound response headers, interim
responses and chunk metadata as well as decoded body bytes; provisional
limits are 4096 header bytes, 32 fields and 4 interim responses, with each
chunk-size/trailer line limited to 512 bytes and total trailers to 1024 bytes.
These parser limits remain to be implemented and tested in the ESP adapter.

Any complete final HTTP response (200–599), including redirects and errors,
returns HTTPS_OK with its HTTP status and body. TLS, timeout, framing and
transport failures are separate. Redirects are returned to the caller without
following them. HEAD, 204 and 304 carry no body. Protocol upgrades are rejected.
Content-Length must agree with the decoded body for responses that carry one.
Unknown lengths are allowed but may not exceed the caller's buffer.

On failure, response length is zero; the buffer may contain partial bytes
and must not be consumed. The core always calls transport cleanup after an
open attempt. Cancellation is cooperative between bounded calls, not a hard
real-time interrupt. Transport time uses a monotonic clock; certificate dates
use UTC with a separately checked synchronization freshness policy.

## Native usage

Create a header with the existing HEADER_CREATE command, specifying an
`https://` URL instead of `http://`. Add headers and a body with the existing
commands, then use DO_EXCHANGE_RAW (0x32) or DO_EXCHANGE_OBJ (0x31). Read raw
reply continuations or use the returned header/body handles exactly as for
HTTP. Assembly and a BASIC SYS helper use this same interface.

TLS trust roots and synchronized time are device responsibilities, not extra
fields in each UCI request. Transport failures currently use the existing
503 SERVICE UNAVAILABLE status channel. More specific TLS failure text can
be introduced without changing the data-channel format.

Internal UART work may need job identifiers and polling, but these are not
exposed as replacement C64 commands. Its messages need explicit versioned
encoding, bounded lengths and generation identifiers. Keep blocking TLS
operations out of the UART dispatcher so the TCP tunnel can make progress.
The ESP TLS stream provider for `HttpConnection` implements:
open with hostname verification, write/read plaintext HTTP over authenticated
TLS, and close on every exit. Its encrypted stream uses Ultimate-owned TCP
sockets through UART. HTTP rendering and response handles remain in the
existing management implementation.

## Security and verification gates

Repository guidance checked: the top-level README, `tests/README.md` and
`.github/workflows/build.yml`. No top-level contribution guide was present in
the checked-out revision; `neorv32/CONTRIBUTING.md` belongs to that submodule.
Keep host tests beside their owning code and preserve upstream notices.

The TLS host suite is documented in `software/network/https/tests/README.md`.
It exercises real handshakes with temporary local certificates, asserts error
categories and cleanup, and runs the adapter under ASan/UBSan. The request core
also runs in the build workflow. The workflow runs the six portable suites
both normally and under ASan/UBSan, with sanitizer findings treated as failures,
and runs the shared HTTP/HTTPS UCI compatibility test under sanitizers. Real TLS
tests still require the separately documented Mbed TLS host dependency.
The separate HTTPS host workflow also runs the real loopback TLS and retail
recipe suites on a standard GitHub runner. Consult the pull request checks for
the result at a specific source revision; the full firmware/FPGA build still
requires its dedicated runner.

The ESP32 cache manifest includes the shared TLS sources/headers, their component
definition and shared UART buffer sources. A regression test reproduces the
missing-input bug, verifies edits to all 12 required shared inputs and a removal
invalidate the cache, excludes generated build output, and rejects missing
source directories. This prevents an unchanged controller cache from concealing
a TLS source change. `python3 tests/lib/esp_depends_test.py` runs locally and in CI.
The controller configuration enables MBEDTLS_HAVE_TIME_DATE; the TLS source
refuses to compile when certificate date verification is disabled. A complete
controller build has passed with ESP-IDF 5.3.6, including compilation of the TLS
and wire components. The TLS worker is now linked into the controller image;
this build does not establish hardware correctness or peak runtime memory use.
The complete u64ii management firmware also links with the RISC-V 11.3.0
toolchain, including the UART-backed HttpConnection provider.

The internal protocol now binds reservations, operations and TCP ownership to
a client epoch consisting of a durable management boot counter and an exchange
counter. Host regressions reject replayed grants and successes across simulated
management/controller restarts, including repeated controller session tokens.
Two private records on `/flash` are synchronized and read back before the first
HTTPS exchange in each management run; storage failures disable HTTPS instead
of reusing an epoch. These records must not be rolled back as configuration.
See `https-uart.md` for storage assumptions and recovery restrictions.
Storage power-loss behavior, real queue pressure, prolonged Wi-Fi coexistence
and controlled missing/stale time scenarios remain hardware validation gates.

The ESP adapter must verify the certificate trust chain, requested hostname
and certificate dates. Reject unavailable or stale synchronized time. Keep
credentials caller-supplied and out of logs. No plaintext retry, implicit
redirect or persisted application credentials.

Run `make -C software/network/https/tests` and the `sanitize` target on a host.
These mock-transport tests verify the core, not real TLS authentication.
Run `make -B -C target/pc/linux/test_http_target test-secure-exchange`
to exercise secure routing, unchanged request rendering,
raw and object replies, short writes, truncated input and cleanup. Create the
ordinary target's `output` and `result` directories before a first parallel
build; the existing gitinfo build rule otherwise races directory creation.
The `test-secure-exchange` target creates its own isolated output directories
and enables ASan/UBSan automatically.
The host suites cover malformed UART payloads, stale session/sequence values,
reservation expiry/exhaustion, restart replay, durable counter failures, TLS
certificate failures and missing time. Both firmware targets build. Remaining
gates include physical storage persistence, real UART backpressure, independent
resets, Wi-Fi coexistence and cold-boot time
acquisition. Initial installation and focused device checks have completed;
they do not establish these remaining reliability properties.

The staged hardware procedure and evidence checklist are in
`https-device-validation.md`. Raw firmware build outputs are not a reviewed
installation package.
