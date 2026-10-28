# Embedded TLS runtime fixture

This target runs the production `EspProviderTlsPlatform`, `TlsBackend` and
control worker under ESP32-S3 QEMU with ESP-IDF 5.3.6. It uses a private OpenETH
network (guest `192.0.2.2/30`, peer `192.0.2.1`, no gateway). It does not contact
the Commodore, flash firmware, register an HTTP provider or change the HTTP API.
The worker is driven directly; physical UART, the management processor and the
product owner task are outside this fixture. Host tests cover their connections.

## Provisioning contract

The product backend remains disabled until its owner supplies all of:

- A complete, explicitly selected CA bundle. Parse errors, including a partly
  valid bundle, reject the whole configuration. Roots remain immutable for the
  platform lifetime. Release owner sessions before destroying the platform;
  destruction closes its socket and frees the parsed roots.
- A readiness predicate for the UTC clock actually used by Mbed TLS. The clock
  must be initialized and qualified by product policy before a request starts.
  A date after January 2020 is only a sanity floor, not proof of correct time.
  Product policy still needs a clock source, freshness limit and rules for
  rollback, large jumps and loss of synchronization. Unauthenticated time alone
  must not be treated as authenticated time. Build with both
  `MBEDTLS_HAVE_TIME` and `MBEDTLS_HAVE_TIME_DATE`.
- A readiness predicate for cryptographic entropy. The port calls
  `esp_fill_random`; its ESP-IDF prerequisites still apply. In the pinned SDK,
  `esp_random.h` specifies true randomness with Wi-Fi or Bluetooth enabled and
  directs other configurations to the RNG prerequisites. The product must
  qualify its actual operating mode; a nonzero buffer is not sufficient proof.

Readiness predicates must be bounded, thread-safe and valid for the entire
platform lifetime. Revocation is checked during bounded socket waits and TLS
operations. The policy owner must retain a revoked state until the affected
request has unwound; a brief false/true pulse can otherwise escape polling.
Deadlines use monotonic time, independently of certificate UTC. Certificate
chain, hostname and validity checks are mandatory, with no plaintext fallback.

The fixture supplies a freshly generated test CA and fixture UTC. It deliberately
marks emulator entropy ready to test the integration path. This does **not**
qualify physical RNG entropy, production clock provenance or root provisioning.
Those product decisions remain open; this target must not supply product defaults.

## Preparing and running

Use frozen source and the standalone TLS revision
`529b0450955611bd82db921db3bae10222203eba`. The pinned ESP-IDF revision is
`79e3454c68248bd7d881721c9b2e94561378a8ce`. Generate separate RSA and ECDSA test
roots, a trusted leaf for `tls.lab.test`, an unrelated-root leaf, and expired and
future leaves. The local peer utilities in that TLS revision generate these
fixtures. Keep generated private keys local.

Supply a per-run `fixture.h` defining `LAB_RUN_ID`, `LAB_UTC_SECONDS`,
`LAB_ECDSA` and a NUL-terminated `lab_root` PEM byte array. Build using
`idf.py -B <build> -D IDF_TARGET=esp32s3 -D TLS_STREAM_ROOT=<frozen-tls>
-D FIXTURE_DIR=<fixture> build`. Merge the resulting images with esptool's
`merge_bin` command; never use `flash` for this test.

The runtime runner requires a fresh `build.json` containing the profile, UTC,
successful build result and SHA-256 hashes of prepared artifacts. It refuses
changed artifacts, an old fixture or an existing result. Run
`software/network/tests/run_provider_runtime.py --fixture <fixture>
--tls-root <frozen-tls> --qemu <qemu-system-xtensa> --uid <nonroot-uid>`.
Root is required only by the host runner for TAP setup and the private DNS
listener; QEMU runs under the specified non-root user. Run profiles sequentially.
An existing TAP is not replaced. Create `operator.stop` in the fixture directory
to stop an incomplete run. Preserve failed runs and use a new directory after a
fix. The runner removes the TAP and terminates its guest before reporting.

## Acceptance and limits

Each profile has 14 cases with a fresh recovery after each: authentication,
hostname/root/date failures, missing trust/clock/entropy, handshake/read timeout,
clock/entropy revocation and owner cancellation. Expect 28 complete attempts,
337 successful checks, one boot and no failures. Missing prerequisites must
reject before transport. The independent peer records must account for every
connection, exact request, TLS version/cipher and certificate alert.

Timeouts retain the original 1500 ms handshake or 3000 ms request budget, with
at most 250 ms overrun. Revocation/cancellation must return within 250 ms of the
signal. Stack reserve must remain at least 1024 bytes from a 12288-byte main-task
stack. Eight isolated trust configure/destroy cycles must return free heap to
the warmed baseline. Per-request free/minimum heap is observational: TCP and
deferred task cleanup make it different from scoped TLS allocation accounting.
These bounds apply to this fixture, not physical scheduling guarantees or a soak.
