# HTTP client regression tests

Build on Linux with GCC/G++ and the pinned `software/httpd` dependency:

```sh
make -C target/pc/linux/test_http_target -j1 \
  'CC=gcc -fsanitize=address,undefined -fno-sanitize-recover=all' \
  'CPP=g++ -fsanitize=address,undefined -fno-sanitize-recover=all'
target/pc/linux/test_http_target/result/test_http --framing
```

The framing suite uses a deterministic byte-stream provider, with 1-, 7- and
1024-byte fragments. It checks fixed-length, chunked and close-delimited bodies;
orderly EOF versus read failure; HEAD, 204, 304 and interim responses; framing
fields beyond the stored-field limit; conflicting/invalid framing; chunk
extensions and trailers; and the 16384-byte payload boundary. Failed exchanges
must expose no partial body and close their session exactly once.

Responses support a single chunked transfer coding, bounded by a 2048-byte
header, the 1024-byte chunk-line input buffer, 2048 bytes of trailers and eight
interim responses. Other transfer codings, length lists and protocol upgrades
are rejected. Identical individual Content-Length fields are accepted. The
payload buffer includes a separate byte for its string terminator.

Run the existing regression modes too: no argument, `--transport`,
`--integer-widths`, `--body-removal`, `--connect-cleanup`,
`--response-completion`, `--response-boundaries` and `--response-cleanup`.
Shared parser/server tests live in `software/httpd/c-version/tests` and accept
`CXXFLAGS` for the local compiler and sanitizer configuration. Real TLS peers
and authenticated EOF tests are in the adjacent `test_http_tls` target.

These are host tests. They do not qualify firmware timing, hardware memory use
or a production TLS provider.
