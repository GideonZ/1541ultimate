# HTTPS UART integration

The prototype TLS_STREAM/METRICS command pair has been replaced by the
[owner/session protocol](https-session-contract.md). Version strings alone do not
enable HTTPS. Identity, capability, lease and operation validation happen before
requests or completions are accepted.

The current development layout is defined by `provider_identity.h`,
`provider_control.h` and `provider_data.h`, with native delivery in
`provider_manager_uart.cc` and ESP32 routing in `rpc_dispatch.c`. Receive ingress
copies bounded messages and preserves the ordinary Wi-Fi reply table.

The framing is still experimental. Shared command allocation and interoperability
with other TLS clients require maintainer agreement before stabilizing it.
Physical UART timing and buffer-pressure qualification remain pending.
