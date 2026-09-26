# HTTPS host tests

These tests never contact a device. They follow the repository convention of
keeping host tests beside their owning code (`tests/README.md`).

Run the portable request tests with:

```
make test sanitize
```

These targets also test the internal UART payload validator: truncated frames,
invalid hostnames and sizes, stale sessions/sequences, mismatched operations,
short results and malformed input under sanitizers. The validator is not
responsible for scheduling or buffer ownership. The firmware endpoints now use
it, but their real queues still require hardware stress tests.

CI runs both targets; sanitizer findings terminate the test with a failure.
`make deadlines` separately extracts and executes the production TCP owner and
queued-request expiry decisions under sanitizers. It checks boundary times,
clock wrap, stale/cancelled ownership, and preservation of the first deadline
record when a later generic TLS reply arrives. It substitutes clock and socket
ownership, so scheduler timing still requires hardware validation.
The separate UCI compatibility gate runs without the Mbed TLS dependency:

```
python3 tools/c64u_https/httpd_patch.py
make -B -C target/pc/linux/test_http_target test-secure-exchange
python3 tests/lib/esp_depends_test.py
```

Run these commands from the repository root. The UCI target uses isolated output
directories and ASan/UBSan. It tests plain HTTP over loopback and secure routing
through a simulated provider; it is not the real-TLS suite described below.
The cache test ensures shared TLS/UART source changes invalidate cached ESP32
binaries. These checks need no device or public service.

The session-service tests cover exclusive ownership, busy rejection, repeated
operation sequences, a wrong session, queue deadlines, cleanup on read failure,
idempotent close, expiry, single-use reservations, replacing a live session,
stale grants after replacement, and counter exhaustion without wrap. The
restart regression replays old reserve/open/read/close traffic with repeated
session and sequence values. It also repeats the controller seed after a
simulated restart, verifies stale TCP replies cannot match, and ensures delayed
older reservations do not close a newer stream.
The UCI host suite additionally exercises C64
reset and FREE_ALL while a raw response still has unread chunks.

The epoch-store tests exercise first-use initialization, boot and exchange
counter increments, every prefix of an interrupted record write, malformed or
missing records, read/write/readback failures and exhaustion without wrap.
They model synchronous durable record storage, not FAT metadata or real flash
power-loss behavior. Both epoch and restart suites run in `test` and `sanitize`.

The TLS tests compile the actual `tls_stream.c` and connect it to a local
Python TLS server through bounded POSIX TCP callbacks. Mbed TLS performs real
handshakes and certificate verification; only the UART transport, platform
clock freshness decision and trust-store attachment are replaced. A simulated
missing or stale synchronization rejects the request before opening TCP.

Requirements: C compiler with AddressSanitizer/UndefinedBehaviorSanitizer,
Python 3 with `cryptography`, and a standalone Mbed TLS build matching the
ESP-IDF dependency. Build Mbed TLS with its default host configuration,
including `MBEDTLS_HAVE_TIME_DATE`, using CMake. Then run:

```
make tls MBEDTLS_SOURCE=/path/to/mbedtls MBEDTLS_BUILD=/path/to/mbedtls-build
```

All generated certificates, private keys and probe binaries live in a temporary
directory that is removed afterward. No public server, credential or endpoint
is required. Adapter and probe code run with sanitizers; the supplied Mbed TLS
library is only instrumented if its own build enabled sanitizers.

Coverage includes verified binary responses, TLS 1.2 with short TCP transfers,
wrong hostname, untrusted certificate, expired/not-yet-valid certificate,
unavailable time, cancellation before and during a handshake, TCP/trust-store
failures, invalid callback counts, zero writes, handshake/read deadlines and
an abrupt close without TLS close_notify. Exact error categories and balanced
open/close counts are asserted, not just a nonzero process exit.

These are not UART, Wi-Fi coexistence or device boot tests. Resource use and
latency on ESP32, time synchronization transport and the public UCI path over
the real TLS provider still require integration validation.

## UCI through real TLS on the host

From the repository root, run:

```
python3 tools/c64u_https/httpd_patch.py
make -C target/pc/linux/test_http_target test-real-tls \
    MBEDTLS_SOURCE=/path/to/mbedtls MBEDTLS_BUILD=/path/to/mbedtls-build
```

This optional build links target 6, the actual HTTP request renderer/response
parser and `tls_stream.c` against a local TLS server. It runs under ASan/UBSan
and covers raw JSON, object handles and queries, embedded NUL bytes, raw reply
continuations, chunked responses, HTTP error responses, truncation, untrusted
roots, wrong hostnames and expired certificates. Each exchange must release its
TLS connection. The server verifies the exact HTTP request generated by UCI.
Oversized HTTP reason phrases and raw headers verify the 255-byte usable status
limit of the eight-bit hardware length register. Reply-boundary tests at 894,
895, 896, 1790 and 2048 bytes verify exact bytes and termination with blocks no
larger than 895: the FPGA pointer saturates at the last byte of its 896-byte
buffer, so filling every byte would keep DATA_AV asserted indefinitely.
Temporary test certificates are supplied only to the host adapter; firmware
trust configuration and the system trust store are unchanged.

Four response-interruption cases send 1,024 bytes of a declared 2,048-byte body
over authenticated TLS, then send close_notify, close TCP without close_notify,
reset TCP, or stall past the deadline. The production client must have consumed
the partial plaintext, return an empty native 503, report the read-stage/parser
failure, and destroy the connection before another request. A second verified
response runs in the same process with the same target, without a C64 reset;
the header handle is reused and both connections are released. These host
checks observe diagnostics at connection teardown; processor-to-processor
telemetry persistence is validated separately on hardware.

The combined suite also supplies malformed wire responses one byte per TLS
record: invalid/overflowing or duplicate Content-Length, ambiguous framing,
unsupported/duplicate transfer codings, invalid/overflowing chunk sizes,
missing chunk separators/final terminators, invalid/oversized trailers and
malformed/oversized headers. All must fail without exposing a partial body.
Positive controls cover decimal lengths with leading zeros, whitespace,
mixed-case chunked coding, extensions, trailers and clean close-delimited
responses. This is focused framing coverage, not a claim of complete HTTP
conformance (e.g. informational response sequences are not supported).

This closes the gap between separate UCI and TLS tests, but the host TCP
adapter still replaces UART, ESP queues, DNS, flash epochs and device time
synchronization. It is not an end-to-end test of the two firmware images.
## Controller telemetry codec

`make test sanitize` also tests the independent read-only telemetry frame:
exact size, little-endian encoding, zero/maximum counters, every truncated
length, every one-byte header mutation and unchanged output on invalid input.
These tests do not measure the ESP32 heap or emulate the UART ISR. Actual
measurements require the bridge 1.17 hardware build and timed soak described in
`tools/c64u_https/README.md`.
