# HTTPS implementation

The earlier prototype has been replaced by the connection/owner architecture
described in [HTTP and HTTPS transport](http-transport.md). The public HTTP command
interface remains unchanged; the TLS backend is replaceable.

See [session ownership](https-session-contract.md),
[host regression](../target/pc/linux/test_http_tls/README.md),
[embedded runtime](../target/esp32/test_provider_runtime/README.md) and
[readiness](https-release-readiness.md). The revised implementation is opt-in and
has not yet been qualified on a physical Commodore.
