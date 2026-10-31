# Host HTTPS regression

This Linux target exercises the existing HTTP commands through a replaceable
connection backend, native owner queue, manager protocol and TLS provider. RSA
and ECDSA loopback peers verify authentication and exact request bytes. It does
not contact a Commodore or establish firmware installation readiness.

## Reproduce

Install GCC/G++, Make, CMake, Ninja, Python 3, OpenSSL and GoogleTest development
files. Initialize the firmware submodules. The standalone TLS source is public:

```sh
git clone https://github.com/araxis/ultimate-tls-validation.git /tmp/ultimate-tls
git -C /tmp/ultimate-tls checkout 529b0450955611bd82db921db3bae10222203eba
git clone https://github.com/espressif/mbedtls.git /tmp/ultimate-tls/.deps/mbedtls
git -C /tmp/ultimate-tls/.deps/mbedtls checkout 2b96dd8eebe880f304c69976b3c2fa0c5100cbb6
git -C /tmp/ultimate-tls/.deps/mbedtls submodule update --init --recursive
python3 tools/prepare_http_parser.py
python3 tools/test_https_host.py --tls-root /tmp/ultimate-tls --output /tmp/https-host-run
```

Run from the firmware repository root. Use a new output directory and a clean
build tree. The runner records every subprocess status and complete log, checks
for sanitizer failures, bounds subprocess execution, and stops on failure.
`result.json` is complete only after every selected suite succeeds. Preserve failed
directories. Generated peer private keys are test-only; exclude them from public
evidence packages. A timeout or missing case is never a pass.

## Coverage

- Existing HTTP modes: transport, framing, integer widths, body removal, connect
  cleanup, response completion/boundaries/cleanup and the general suite.
- Real TLS chain, hostname and date failures; unavailable trust, time and entropy;
  handshake/read/total deadlines; cancellation; authenticated and truncated EOF.
- Direct adapter, target routing, owner, worker, connected, UART and bound-provider
  paths with both certificate profiles. Every failure case has a fresh recovery.
- Manager identity, stale replies, sequence exhaustion, queue overflow, restart,
  owner leases, close acknowledgements and cancellation ordering.
- Native owner scheduling with controlled ports; explicit journal installation,
  flash fault injection, source revocation and combined startup failures.
- Session event-order model and the existing assembly suite.

ASan/UBSan instrument the host TLS core and adapter. Peer logs independently
account for connections, SNI, requests and closure. Allocation/descriptor balance
is scoped ownership evidence, not a measurement of firmware-wide heap usage.
Host wire and scheduler fixtures do not emulate physical UART or RTOS timing.

Individual binaries are built under the output directory's `tls-build`. The
`run_http_tls.py` runner also accepts `--case`, `--ecdsa`, `--routing`, `--owner`,
`--worker`, `--connected`, `--uart` and `--bound` for focused reproduction. Each
mode must use a new output directory. The output of a focused run does not replace
the complete matrix.

## Production status

The optional native factory, owner queue, ESP32 task and TLS backend are connected
in source. Default firmware keeps them inactive. Production roots use the pinned
repository CA list, with explicit clock and entropy policy; see
[PROVISIONING.md](PROVISIONING.md). Native and ESP32 options must both be selected,
the identity journal installed explicitly, and negotiation completed.

The [embedded runtime fixture](../../../esp32/test_provider_runtime/README.md)
checks the ESP32 owner under QEMU. Physical revised-firmware HTTPS, UART timing,
native memory/stack behavior, actual entropy, journal durability, post-write image
verification and recovery are pending. Historical prototype hardware results and
standalone long-duration runs are separate evidence, not passes for this revision.
