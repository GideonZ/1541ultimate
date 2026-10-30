# Embedded TLS runtime fixture

This target runs the production `EspProviderTlsPlatform`, `TlsBackend` and
control worker under ESP32-S3 QEMU with ESP-IDF 5.3.6. It uses a private OpenETH
network (guest `192.0.2.2/30`, peer `192.0.2.1`, no gateway). It does not contact
the Commodore, flash firmware, register an HTTP provider or change the HTTP API.
The default profile drives the worker directly. The optional production-task
profile below runs the ESP32 owner task itself. Physical UART and the management
processor remain outside both embedded profiles; host tests cover their connections.

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
The selected product policy is documented in
[PROVISIONING.md](../../pc/linux/test_http_tls/PROVISIONING.md); its physical
qualification remains open. This target does not establish hardware readiness.

## Preparing and running

Use frozen source and the standalone TLS revision
`529b0450955611bd82db921db3bae10222203eba`. The pinned ESP-IDF revision is
`79e3454c68248bd7d881721c9b2e94561378a8ce`. Generate separate RSA and ECDSA test
roots, a trusted leaf for `tls.lab.test`, an unrelated-root leaf, and expired and
future leaves. The public peer utilities in `araxis/ultimate-tls-validation` at that revision generate these
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

## Heap recovery investigation

Define `LAB_HEAP_CHECK` as 1 in the generated fixture header and set `heap_check`
to true in its build record to extend the run. The original fault/recovery cases
are followed by a TCP cleanup barrier, three batches of 32 successful requests
on the same boot, and another cleanup barrier. Each barrier allows 150 seconds
for active, bound and TIME_WAIT TCP objects to disappear, followed by 2 seconds
for deferred cleanup. The pinned lwIP uses a 60-second maximum segment lifetime
and expires TIME_WAIT objects after twice that interval. Socket close does not
necessarily free these objects immediately, even with zero linger.

The fixture samples TCP lists on the TCP/IP thread, using a retained callback
message and semaphore so repeated measurements do not allocate new objects.
It records free heap, largest free block, task count, PCB size and retention
limit. Do not traverse these TCP lists from the application task.

This mode requires 124 complete attempts and 1405 successful checks per profile.
All 121 peer connections must close, with 114 exact authenticated requests,
three stalled handshakes and four certificate alerts. After the final cleanup,
free heap and the largest free block must both reach or exceed the warmed
baseline, and task count must match. Heap integrity must hold throughout; TCP
retention cannot exceed its configured limit. The runner allows 480 seconds
for this extended run and preserves operator-stop handling. A complete short
run establishes recovery for these cases, not long-duration or hardware safety.

## Long-call renewal

Use `LAB_RENEWAL_CHECK=1` in the fixture header and `renewal_check: true` in the
build record for a separate 505-check profile. Do not combine this with heap
mode. A second FreeRTOS task submits encoded control messages while the owner
is inside a TLS read. Six cases exercise delayed success, the original request
deadline despite renewals, missing renewals, replayed renewals, close and a new
owner offer. Each is followed by a fresh successful recovery.

The worker services at most one control event at each TLS cancellation check.
Only a valid next-sequence renewal of the current unexpired grant extends its
lease. This path cannot invoke resource cancellation, cleanup or deletion;
normal owner stepping resumes after TLS unwinds. Close/restart/loss still fence
the call at ingress, and close acknowledgement waits for completed cleanup.
Renewals do not extend the HTTP request deadline or change the public interface.

The fixture uses a 1500 ms lease and 500 ms renewals. Delayed success takes at
least 2200 ms; a stalled request retains its 3500 ms budget with at most 250 ms
overrun. Missing renewals cancel near 1500 ms and a replayed first renewal near
2000 ms. Close and new-owner offers cancel within 250 ms of their signal. Per
profile, 37 peer connections must close and 30 exact requests authenticate.
This validates the embedded callback/worker path, not physical UART timing or
the complete native manager task's scheduling.

## Production owner task and scheduling

Build with `-D PROVIDER_TASK_PROBE=ON` and set `task_check: true` in the build
record. Keep heap and renewal modes false. This selects a different application
entry point and compiles the actual `provider_control_task.cc`, including its
control queue, data delivery loop, TLS backend and production scheduling limits.
Only the UART device functions and boot identity are substituted. UART ingress
uses `provider_control_receive`; transmitted packets are copied to a fixture
queue and source buffers are overwritten to check ownership. The test does not
register the provider in the product or install trust/clock/entropy defaults.

Each certificate profile requires 301 checks across 12 cases and 12 fresh
recoveries: authenticated success, hostname and root rejection, original timeout,
six-second success with renewals, missing renewal, close, new owner offer,
control/data overflow, unavailable transmit buffer and failed transmit submission.
The five-second production lease, four control slots, 10 ms loop and 12288-byte
owner-task stack remain unchanged. A concurrent ingress task feeds renewals once
per second while TLS blocks; an independent higher-priority task measures its own
10 ms scheduling gaps. Both renewal acknowledgements and scheduling gaps must
stay within 250 ms. Actual owner stack reserve must remain at least 1024 bytes.

Timeout keeps its original 2000 ms budget with at most 250 ms overrun. Missing
renewals expire within 5000..5250 ms of initialization. Close/restart must return
failure within 250 ms of ingress and acknowledge cleanup after TLS unwinds.
Overflow or transmit loss must fence replies and reject renewal of the affected
grant before explicit close; silence alone is insufficient. Every fresh recovery
must succeed. The peer must record 24 closed connections, 22 authenticated exact
requests and two certificate alerts. `assess_provider_task.py` checks ordered raw
case/check records, time bounds, task counts, stack observations and peer records.

The temporary scheduling task is joined and given an idle cleanup interval before
testing idempotent startup. The production owner task, root bundle and fixture
UART queue intentionally retain firmware lifetime. Per-case global free heap is
observational, not a new heap recovery or soak claim. These emulator measurements
do not establish physical UART latency, native manager scheduling or product
restart identity, trust, UTC and entropy qualification.

## Current source fixture

The source profile uses the same pinned repository CA list as product recovery,
with SDK default roots disabled. It verifies ISRG Root X1, rejects the private
fixture CA through the system bundle, and retains date rejection. Explicit private
roots remain confined to the local fault-injection exchanges. The actual source
adapter and factory are now implemented; physical qualification remains pending.
