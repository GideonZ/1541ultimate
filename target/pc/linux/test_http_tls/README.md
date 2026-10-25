# HTTP over the standalone TLS adapter

This host-only target exercises `HttpRequest` with `TlsHttpConnection` and real
TLS 1.2 loopback peers. It does not enable HTTPS routing or add TLS to a firmware
image. The existing HTTP interface and socket default remain unchanged.

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
python3 software/network/tests/run_http_tls.py \
  --binary /tmp/http-tls-build/test_http_tls --tls-root "$TLS_ROOT" \
  --output /tmp/http-tls-rsa
python3 software/network/tests/run_http_tls.py \
  --binary /tmp/http-tls-build/test_http_tls --tls-root "$TLS_ROOT" \
  --output /tmp/http-tls-ecdsa --ecdsa
```

Output directories must not exist. They retain raw subprocess logs, independent
peer observations, certificate validation metadata and a complete result list.
Each invocation creates temporary test-only certificate keys in its output;
do not use these certificates for anything outside the fixture.

## Coverage and limits

Both certificate profiles cover authenticated HTTP, certificate chain/hostname/
date rejection, unavailable trusted time and entropy, failed allocation,
handshake/read/write deadlines and cancellation, cancellation before opening,
clean TLS EOF, and truncated responses. Every case includes a fresh successful
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
