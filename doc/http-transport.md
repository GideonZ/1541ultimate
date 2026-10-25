# HTTP connection boundary

`HttpRequest` renders and parses HTTP through `HttpConnection`. The default
connection uses the existing socket implementation. Callers may supply a
different byte-stream backend to the request constructor; the HTTP parser does
not depend on a TLS library or interprocessor protocol.

This increment does not add HTTPS URL handling or a production TLS backend.
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
