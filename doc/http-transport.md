# HTTP and HTTPS transport

The target-6 HTTP command interface is unchanged. Select HTTPS with an `https://`
URL; header creation, request bodies, handles, response blocks and JSON rendering
use the same commands as HTTP. The default build still uses ordinary HTTP and
rejects HTTPS when no qualified provider is available.

## Request path

`HttpTarget` owns a connection returned by its borrowed `HttpSecureProvider`.
`HttpRequest` borrows that connection and renders/parses HTTP through
`HttpConnection`. The ordinary socket implementation remains the default for
HTTP and schemeless URLs. The parser does not depend on TLS or the UART protocol.

For the opt-in firmware, `OwnerHttpProvider` submits copied requests to the native
owner queue. The manager exchanges identity-bound control and data messages with
the ESP32 owner task. `TlsBackend` supplies `TlsHttpConnection`, whose platform
callbacks use ESP-IDF sockets, entropy, time and certificate verification. Another
backend can implement the same authenticated stream contract without changing
the C64 command interface.

The native `PROVIDER_JOURNAL=1` option and the ESP32
`ULTIMATE_PROVIDER_CONTROL`, `ULTIMATE_PROVIDER_TLS` and
`ULTIMATE_PROVIDER_SOURCES` options are disabled by default. Building both sides
is insufficient: an explicitly installed restart identity, successful owner
negotiation, qualified time and entropy are required before HTTPS is admitted.
See [provisioning](../target/pc/linux/test_http_tls/PROVISIONING.md).

## Failure and ownership rules

- Each connection is single-use. Failure closes partially initialized resources;
  repeated close is harmless. Recovery creates a fresh exchange without replay.
- Reads and writes may be short. Zero read means authenticated EOF for TLS; a bare
  TCP close is not authenticated EOF. Negative results are terminal.
- The request is destroyed before its owned connection. A borrowed connection,
  factory, platform context and trust material must outlive their users.
- One absolute budget covers open and subsequent I/O. Progress and lease renewal
  do not extend it. Cancellation, clock/entropy revocation and identity changes
  fence results before publication.
- A failed write may already have sent bytes. It is never automatically retried.
- Authentication requires certificate chain, hostname and date verification.
  Missing providers or failed secure operations never fall back to plaintext.
- Secure failures use the existing `503 SERVICE UNAVAILABLE` status and empty
  response. Invalid URL syntax uses the existing `500 BAD FORMAT` status.

Scheme matching is case-insensitive. HTTPS defaults to port 443 and accepts
explicit decimal ports 1–65535. Unsupported schemes, malformed secure prefixes,
empty hosts, userinfo and unsupported authority syntax are rejected before opening.

## Response framing

The parser rejects conflicting lengths, unsupported transfer codings, malformed
chunks, premature EOF and oversized responses. It supports one chunked coding,
bounded trailers and interim responses. The limits are a 2048-byte header,
1024-byte chunk-line input buffer, 2048 bytes of trailers, eight interim responses
and a 16384-byte body plus a separate terminator byte. Identical individual
Content-Length fields are accepted; length lists and protocol upgrades are not.
Failures release partial bodies rather than expose them as successful responses.

The public HTTP submodule revision is accompanied by
`patches/http-response-framing.patch`. Run `python3 tools/prepare_http_parser.py`
after initializing submodules and before building. It checks every affected file
against the public base or exact patched contents, refuses unrelated local edits,
and verifies the result. CI performs the same preparation. The submodule then
appears modified by this reviewed patch; do not commit those changes in the parent
as a different, unpublished submodule reference.

## Validation and remaining qualification

The [host runner](../target/pc/linux/test_http_tls/README.md) covers the unchanged
HTTP commands, framing, authenticated RSA/ECDSA peers, ownership, cancellation,
deadlines, restart fencing and cleanup. The [embedded fixture](../target/esp32/test_provider_runtime/README.md)
runs the ESP32 platform and production owner task in QEMU. Native and ESP32
firmware are also cross-built with default and opt-in configurations.

These tests do not qualify the revised firmware on a physical Commodore. Physical
UART timing, native stack/resource measurements, actual entropy, journal power-loss
behavior, exact controller-image installation verification and recovery remain
installation gates. Previous HTTP-only or prototype hardware results are not
hardware evidence for this implementation. Keep the coordinated candidate off
devices until those gates and an explicit hardware plan have been reviewed.
