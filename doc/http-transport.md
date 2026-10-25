# HTTP connection boundary

`HttpRequest` renders and parses HTTP through `HttpConnection`. The default
connection uses the existing socket implementation. Callers may supply a
different byte-stream backend to the request constructor; the HTTP parser does
not depend on a TLS library or interprocessor protocol.

Target 6 selects an optional secure provider for HTTPS URLs. A production TLS
provider is not installed by default.
Target-6 commands, response blocks, status strings and JSON handling are
unchanged. Parser compatibility fixes remain separate work.

## Ownership and results

- A supplied backend must outlive the request and must not be used by another
  request while its session is owned. The request closes the session but does
  not delete the backend object. Both request and socket wrapper are noncopyable.
- `open` returns a nonnegative value on success. A failed open is followed by
  `close_session`, allowing the backend to release partially initialized resources.
  A request accepts only one open attempt; recovery uses a fresh request.
- Positive reads and writes report actual byte counts, at most the requested
  size. A read of zero means EOF, never temporary lack of data. Writes must make
  progress. Negative results are terminal; socket-specific interruptions are
  handled inside the socket backend.
- Partial writes are continued from the first unsent byte. A failed write may
  already have delivered data, so the request closes the session and rejects
  further operations rather than replaying the request.
- Read/framing failures close the connection and release incomplete response
  bodies. Successful responses remain available until the request is destroyed.
- The descriptor-based `get_response` and `read_socket` helpers retain their
  existing signatures. They borrow the socket and leave it open for the caller.

A secure backend must authenticate the certificate chain, hostname and dates
before opening succeeds, enforce one total exchange deadline with bounded
cancellation and cleanup, and never fall back to plaintext. These are provider
requirements; the byte-stream interface alone does not implement them.

## Host regression

From `target/pc/linux/test_http_target`, run `make test-transport` on Linux.
The bounded AddressSanitizer/UndefinedBehaviorSanitizer test uses deterministic
byte streams, not a TLS emulator. It covers fragmented responses, partial
writes across request blocks, zero/error/oversized writes, partial-delivery
failure, incomplete responses, terminal backend errors, independent sessions,
single cleanup and the legacy borrowed-descriptor parser.

Also run the existing HTTP suites and `make test` in
`target/pc/linux/test_assembly` when changing this boundary. Host tests do not
qualify cryptography, firmware deadlines or hardware behaviour.

## Optional standalone TLS adapter

`TlsHttpConnection` implements the same connection interface using the external
standalone TLS stream. Only its adapter header depends on that provider;
`HttpRequest`, its parser and its existing callers do not. Replacing this adapter
with another authenticated byte-stream implementation does not change HTTP
request construction or response handling.

The adapter copies the platform callbacks and owns one TLS session. Its callback
context and trust roots must outlive the adapter. Construct a fresh adapter for
each exchange, then pass its address to `HttpRequest`. The configured timeout is
one total exchange budget, starting at open; reads and writes do not reset it.
Platform callbacks must honor the remaining budget and cancellation. Certificate
chain, hostname and date checks are mandatory; unavailable qualified time or
entropy fails closed. There is no plaintext fallback or automatic retry.

Failure destroys the TLS session promptly and retains stage, status, library
error and certificate verification flags through `diagnostic()`. Destruction and
repeated `close_session()` are safe. Authenticated TLS EOF maps to a zero read;
an unauthenticated transport EOF is an error. A failed write may have sent bytes
and must not be replayed. Both the adapter and request are noncopyable.

The adapter is currently built only by the host fixture described in
[`test_http_tls`](../target/pc/linux/test_http_tls/README.md). The fixture supplies
it to target 6; a production firmware image still needs a qualified provider.

## Scheme routing and failure behaviour

The existing header-create and raw/object exchange commands accept the same
request data. Ordinary HTTP and schemeless `host[:port]/path` continue to use the
inline socket backend. HTTPS uses `HttpSecureProvider`, supplied to the target
constructor; the default is no provider. Scheme matching is case-insensitive.
The default secure port is 443, and explicit secure ports must be decimal 1–65535.
Unsupported explicit schemes, malformed `https:` prefixes, empty secure hosts,
userinfo and unsupported secure authority syntax are rejected before transport
creation. Existing header rendering is shared by HTTP and HTTPS, including its
Host-header formatting; this increment does not revise request syntax.

The provider is a trusted implementation boundary. Its factory must return a
fresh authenticated connection or null, never a plain socket substitute. The
target owns that connection and destroys it after its borrowing `HttpRequest`.
The factory and callback context must outlive the target. Completion, failure,
abort, reset, free-all and exchange replacement release the owned connection.
Pending raw response blocks retain ownership until drained or discarded.

An absent provider, null factory result or secure open/I/O failure produces the
existing `503 SERVICE UNAVAILABLE` status with an empty reply. It cannot trigger
a DNS lookup or plaintext connection through the default HTTP backend. A later,
explicit HTTP command can still succeed. Invalid URL routing uses the existing
`500 BAD FORMAT` status at exchange time; it does not allocate response handles.
Remote HTTP errors still return their genuine response bodies and status through
the normal raw/object paths. No new TLS-specific command or numeric status is
introduced; structured status text alone is not a new machine-readable TLS error
taxonomy. Detailed TLS diagnostics remain a provider concern.
