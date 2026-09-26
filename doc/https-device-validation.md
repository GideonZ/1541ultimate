# HTTPS device validation

Current status (2026-09-26): v13 is operator-confirmed installed, with a new
controller boot and fresh bridge 1.18 telemetry. Its ten-exchange HTTP/HTTPS
smoke run passed; the owned-page repair's local menu measurements are pending.
The preceding v12 passed its smoke check but two batches of five local Wi-Fi
menu cycles retained 6,056 and 6,048 additional management bytes respectively.
That repeated growth motivated the v13 repair, whose host regression tests
reproduce the old page leak and verify cleanup. The preceding v11's
focused response-fault checks and manual Wi-Fi reconnect/recovery passed.
The preceding v10 candidate completed 2,846/2,846 exchanges over two hours,
with original configuration, unchanged controller boot identity and unchanged
failure records. Those results are specific to the tested images and workloads.

The original v9 run failed after 19 minutes 28.6 seconds: 421 exchanges passed,
then HTTPS returned empty 503 on exchange 422. Its root cause remains unresolved.
Manual Wi-Fi recovery did not prove interruption during active TLS. The retained
704-byte management allocation difference is only partially investigated: a
pre-existing menu state leak was reproduced, but cannot explain the full delta
by itself. A minimal retail backport is now covered by host ownership tests;
its first menu-specific hardware check did not establish stable allocation.
Flash durability and other listed fault cases
remain unvalidated. This report is evidence for experimental review, not full
release qualification; earlier failed runs remain recorded below.

## First retail hardware run (2026-09-22)

The operator installed the reviewed retail candidate with SHA-256
`6b8ed41074e615489e9c654319ce05a275ca96b7b3ffe728d61afa9bd5d9d815`
and powered the device on again. REST returned C64 Ultimate, firmware 1.1.0,
FPGA 122 and core 1.49. A native 6502 test agent then identified target 6.
Private device addresses and detailed logs remain outside public source.

- Identical target-6 GET commands over HTTP and HTTPS returned `200 OK` and
  identical encoded JSON objects for `{"ok":true}`. The observed exchanges
  took about 0.38 s and 3.29 s, including host polling overhead.
- Both schemes preserved the raw bytes `41 00 42 7f`, including embedded NUL.
- The expired, wrong-host and self-signed BadSSL endpoints returned the
  existing `503 SERVICE UNAVAILABLE` status with no body. These public-endpoint
  checks observe rejection; exact certificate failure causes and absence of
  plaintext fallback are additionally covered by controlled host tests, not
  inferred solely from that generic device status.
- A 1024-byte HTTPS response exposed the full-queue FPGA boundary: a 896-byte
  reply block did not clear DATA_AV. The bounded agent stopped at 4096 bytes.
- Long raw headers lost their status text: the 8-bit hardware status-length
  register cannot represent 256. Short headers remained visible.

The shared HTTP target now sends at most 895 data bytes per continuation and
255 status bytes. This applies to both HTTP and HTTPS, preserving commands,
handles and continuation semantics. Host regression cases assert these hardware
limits, exact body bytes, empty final continuations at exact multiples and long
raw/object status handling. The second hardware run below validates these fixes.

The public HTTP test endpoint requires a User-Agent; without one it returned
HTTP 402 with an empty body, also reproduced from the host. The test supplies
that header through the existing HEADER_ADD command. No automatic application
header or public API change was added to the firmware.

## Corrected retail hardware run (2026-09-22)

The operator reported installing `c64u-https-fix2.ue2` and powering on again.
The staged package was fully read back and verified before installation with
SHA-256 `087c4c8d692e84c83ecd2e4c489c59c67f7b63f2bf40c4c7917a0788e2362c51`.
The running target now exhibits both corrected hardware boundaries. REST info
alone still reports 1.1.0/FPGA122/core1.49 and is not an installed-image hash.

Both HTTP and HTTPS passed exact byte comparisons for these response sizes:

| Response bytes | Observed continuation lengths (both schemes) |
| --- | --- |
| 894 | 894 |
| 895 | 895, 0 |
| 896 | 895, 1 |
| 1024 | 895, 129 |
| 1790 | 895, 895, 0 |
| 2048 | 895, 895, 258 |

Every exchange returned to Idle. Raw status headers were present at the usable
255-byte limit, rather than disappearing. The 1024-byte fixture was independently
downloaded on the host; the range fixtures contain repeating ASCII a through z.
Binary bytes `41 00 42 7f` were preserved over both schemes. The three invalid
certificate endpoints again returned 503 with no data; subsequent HTTP/HTTPS
JSON requests succeeded with identical typed object bytes `04 01 02 6f 6b 02 01`.
These are 19 successful endpoint exchanges across the binary, boundary,
certificate-rejection and JSON checks, using a native 6502 agent throughout.

The first HTTP request of this run returned 503 after about 7.14 seconds.
Its cause was not identified. An explicit retry succeeded, and the remaining
checks completed without automatic exchange retries. Preserve that failed run
as reliability evidence; successful later checks do not explain its cause.
Observed successful HTTPS exchanges generally took about 2.4–3.5 seconds,
including host polling overhead; these are observations, not a latency guarantee.

All exported configuration settings matched the pre-install backup. The test
program was stopped with a C64 reset and REST remained available. This run did
not re-enable FTP, modify configuration or install additional firmware. Detailed
private logs and the operator's installation report accompany the candidate.

## Repetition and controlled connection faults (2026-09-22)

The same installed retail v6 package completed 40 alternating HTTP/HTTPS rounds:
80 exchanges, with raw bodies up to 2048 bytes and JSON object queries. All 80
passed exact body and continuation checks. Each request freed its individual
header/body handles, and the next request reused header slot zero. The agent
remained resident throughout the run, without a C64 reset or FREE_ALL between
exchanges. The prior unexplained initial HTTP 503 did not recur; its cause is
still unknown. Median exchange times were about 0.47 s for HTTP and 2.5 s for
successful HTTPS. Including certificate checks, this run took 233 seconds; it
does not establish long-duration stability or absence of memory leaks.

Three invalid certificate endpoints were rejected, each followed by a successful
HTTPS recovery request. One attempted public slow-body fixture was invalid:
the service returned HTTP 400 because the requested 25-second duration exceeded
its 10-second limit. The failed assertion and raw report were retained rather
than counted as a timeout pass or firmware regression. The service limit was
also observed independently on the host and matches its
[documented default](https://github.com/mccutchen/go-httpbin#configuration).
There were 87 successful checks and one invalid fixture in that 88-exchange run.

A separate 16-exchange run passed every check with a local, controlled server:

- HTTP/HTTPS baseline and recovery after a reserved `.invalid` DNS failure.
- Fragmented chunked response preserving `41 00 42 43 44 45 46` exactly.
- An HTTP 503 response with its body preserved, distinguishable from the UCI
  transport-failure status. A 302 response was returned without following it;
  the fixture log contains no resulting `/ok` request before the next test.
- Immediate disconnect, early EOF with an incomplete Content-Length body, and
  delayed early EOF: all returned unavailable with no partial body, then a
  subsequent healthy request succeeded.
- Silent peer during TLS handshake: the server recorded a 246-byte TLS record
  beginning with handshake type 0x16, and the device failed at 15.137 seconds.
  The next authenticated HTTPS request succeeded. This tests a handshake stall;
  it does not establish the deadline for a stalled authenticated response body.

The retail firmware returned 404 for `/v1/machine:heap`, so no device heap or
stack watermark measurements were obtained. Handle reuse is not proof of heap
stability. Both runs left exported configuration unchanged, reset the C64 test
program on completion, and left REST available. The LAN fixture was stopped;
no firewall, trust-store, Wi-Fi setting or firmware change was needed.

Still outstanding: prolonged runs with memory/stack measurements, actual Wi-Fi
loss/reconnect, controlled missing/stale clock, complete malformed-framing audit,
queue pressure, independent controller restart and flash power-loss durability.

## Response framing correction (2026-09-23)

The installed v6 completed a longer native-UCI run: 200 alternating HTTP/HTTPS
rounds (400 exact-byte/object exchanges), plus three bad-certificate rejections
and recovery pairs, followed by reserved `.invalid` DNS failure and recovery.
All 408 checks passed in 1008.8 seconds (16.8 minutes), without intervening C64
resets or FREE_ALL. Median exchange times were 0.4435 s for HTTP and 2.4535 s
for HTTPS (the latter includes the deliberate failures); maximums were 1.438 s
and 4.244 s. Configuration remained identical, the C64 agent was reset at the
end, and management remained available. This extends repetition evidence but
does not measure leaks: v6 still has no heap endpoint, and this is not an
hours-long reliability run. These device results do not validate the new parser.

The expanded real-TLS/UCI tests reproduced 16 failures on the previous parser,
including successful responses for negative/suffixed Content-Length, conflicting
lengths, invalid chunk sizes and incomplete chunk endings. A valid mixed-case
`Chunked` response also lost its body. The correction validates the shared client
response path for both HTTP and HTTPS, following the framing rules in
[RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3).
It requires decimal lengths and complete chunk separators/trailer termination,
rejects conflicting/duplicate framing and unsupported transfer codings, bounds
trailer/header storage, and safely handles clean close-delimited responses.

The parent repository records the dependency change in
`tools/c64u_https/httpd-response.patch`; CI applies it and retail preparation
applies it to the pinned export. A fresh clone does not depend on a local dirty
submodule. The incoming management request parser retains its prior behavior;
the shared buffer compaction also uses overlap-safe `memmove`. Strict response
parsing is explicitly selected by the shared HTTP/HTTPS client; retail
CommoServe retains its legacy response mode and read lifecycle.

Validation: 50 combined real-TLS/UCI cases pass under ASan/UBSan. A separate
portable gate runs 22 framing cases at 96 fragment sizes (2112 checks), also
under sanitizers. The retail build passes all five core suites with sanitizers,
63 body-removal checks, 99 integer checks, 8 secure-routing checks, full firmware
linking, RV32I instruction checks, FPGA/partition compatibility and embedded-image
verification. Ten recipe regressions and 16 standalone real-TLS cases also pass.

The new package SHA-256 is
`462905ca08f5b115b6a984c8f23f003df720bf0e2e3f5ff22e3009435b3e1a3d`
(4,755,648 bytes). It also adds read-only management heap telemetry. Retail uses
newlib malloc through `heap_3` and C++ allocation, so the endpoint reports
allocated bytes, arena size and reusable arena bytes/blocks, not a FreeRTOS
low-water mark or total free RAM. ESP32 memory and task stacks remain unmeasured.
The controller protocol and runtime sources are unchanged from v6; only build
metadata differs in the packaged controller image.

The final package was staged as `USB0/HTTPS-Fix-20260923/c64u-https-fix3.ue2`
and fully read back with a matching hash. FTP configuration was restored and
all exported settings match the pre-install backup. No updater was executed;
installation at that point was pending. The subsequent operator report and
hardware checks are recorded below. Earlier intermediate v7 build attempts
were never installed or staged.

These build results are distinct from the device measurements below.
Informational response sequences, broader HTTP conformance, actual
Wi-Fi interruption, independent controller restart, queue pressure and storage
power-loss behavior remain separate validation work.

## Installed v7 framing and fault tests (2026-09-23)

The operator reported completing the update and powering on. The old IP was
initially unreachable from Windows and WSL even though the gateway responded;
the operator confirmed Link Up and the same active IP. It subsequently became
reachable without an agent-side network/configuration change. The cause of this
transient interruption is unknown. The new `allocator: newlib` endpoint and the
new framing behavior were verified; there was no readback/hash of running flash.

All 40 controlled checks passed in 105.9 seconds: HTTP/HTTPS JSON, three bad
certificate cases with successful recovery, DNS failure/recovery, chunked binary,
preserved HTTP errors, redirects without following, disconnect/truncation and
recovery, and a silent TLS handshake peer. That handshake failed at 15.168 s;
the next authenticated request succeeded. Eight malformed-response cases were
each followed by a healthy request: negative/suffixed/duplicate lengths, invalid
or suffixed chunk size, missing chunk separator/final CRLF and ambiguous framing.
Each malformed response failed with no partial data. Mixed-case chunked coding
with an extension/trailer, and a clean close-delimited response, both returned
the exact valid JSON body.

The management allocator read exactly the same before and after these checks:
1,489,720 allocated bytes, 1,660,824 arena bytes, 171,104 reusable arena bytes and
10 free blocks. These samples include the resident native agent/test context;
they do not measure ESP32 memory, task stacks or unsampled transient peaks.
One healthy LAN recovery took 7.519 s; it succeeded, but its slower latency is
retained rather than omitted from the result. All settings matched the run's
baseline and the C64 test program was reset at completion.

## Installed v7 repetition, deadlines and cleanup (2026-09-23)

The planned 200-round repetition stopped after 112 complete rounds: 224
successful exchanges (112 HTTP and 112 HTTPS) in a 611.2-second run. The next
header-create command could not establish its management TCP connection; it
was not sent and no next HTTP exchange was triggered. All recorded responses
passed exact body/object and continuation checks. This is an interrupted run,
not a pass of the planned 400 exchanges or its subsequent fault checks. The
13 sampled allocator readings were identical to the baseline above. They
support no observed retained-allocation growth, not a general leak-free claim.
The harness now reports fatal run exceptions separately from failed recorded
exchanges, so a zero failed-exchange count cannot imply full completion.

A first cleanup repetition also stopped on a management connection timeout,
after six successful abort checks and five recorded recovery exchanges. The
timeout prevented the sixth recovery's HEADER_FREE command from being sent.
Its final pre-reset allocation was 392 bytes above baseline, consistent with
the still-live request header; subsequent post-reset runs returned to baseline.
The incomplete run and original exception were retained.

A separate focused cleanup run completed in 70.4 seconds. For each of HTTP and
HTTPS it tested aborting after the first 895-byte raw block, FREE_ALL with live
JSON object handles, and C64 reset with live JSON object handles. All six cleanup
operations and all six subsequent exact-binary recovery requests passed. The
reset checks deliberately did not send FREE_ALL after reset. Allocated bytes
rose from 1,489,720 to 1,491,280 with live object handles and returned to exactly
1,489,720 after each cleanup. Header slot zero was reusable. The slowest healthy
recovery took 8.603 seconds and is retained in the report. This tests reset with
live handles, not reset during an in-flight TLS exchange.

A separate three-exchange deadline run also passed. An authenticated HTTPS
drip response of 20 bytes over one second completed in 2.099 seconds. The
18-second version failed with unavailable status and no partial body at
15.250 seconds; the original fast request then succeeded in 2.844 seconds.
The slow fixture had independently returned HTTP 200 and exactly 20 bytes on
the host in 17.759 seconds. Together these validate the bounded response-body
case with positive controls, without claiming a device-side packet trace.
All four allocator samples in this run matched baseline.

Concurrent native Windows ICMP observations found periods when the device
did not reply while the gateway continued replying. This also occurred after
the final native test had ended, while no HTTP/HTTPS UCI exchange was running.
Missing ICMP replies alone do not identify the cause; the earlier management
TCP timeouts and these observations leave Wi-Fi/AP/firmware reachability as
an unresolved reliability issue. No VPN, router, firewall, network settings or
firmware were changed to conceal it. A stable connection and a complete repeat
of the longer v7 test were still needed at this point; the subsequent repeat is
recorded below. The successful v6 soak was not counted as v7 validation.

Every run's final exported configuration matched the original pre-install
backup, the C64 test program was reset, and management answered at cleanup.
The evidence preserves both interrupted runs as well as the completed focused
runs. ESP32 heap, task-stack watermarks and hours-long stability remain untested.

## Complete v7 repetition after manual Wi-Fi reconnect (2026-09-23)

The operator selected Disconnect, then Connect to last AP, and confirmed the
active IP was unchanged. A two-minute idle preflight returned all 39 management
requests successfully with no missed device or gateway ICMP replies. The new
run then completed all 200 alternating HTTP/HTTPS rounds and eight subsequent
certificate/DNS fault-and-recovery checks: **408/408 passed in 1068.5 seconds
(17.8 minutes)**, with no fatal exception or failed recorded exchange.

The first 400 exchanges comprise 200 HTTP and 200 HTTPS requests, covering
typed JSON and exact raw bytes at 894, 895, 896, 1024, 1790 and 2048 bytes.
Continuation lengths and header-slot reuse passed throughout. There were no
intervening C64 resets or FREE_ALL calls, and no application-level exchange
retries. The test transport retains its existing bounded safe retry policy;
this result does not claim that every management TCP connection succeeded on
its first attempt. The final eight checks reject expired, wrong-host and
self-signed certificate endpoints and an unresolvable reserved name, each
followed by a successful authenticated HTTPS request.

All 22 allocator samples were identical: 1,490,792 allocated bytes, 1,660,824
arena bytes, 170,032 reusable arena bytes and 12 free blocks. This run's baseline
was already 1,072 allocated bytes above the earlier run before the first UCI
exchange, following the manual reconnect. No growth was observed during this
run; the differing baselines are not an allocation diagnosis. ESP32 memory and
stack watermarks remain unmeasured. Median measured exchange times were 0.4485 s
for HTTP and 2.4525 s for HTTPS (including deliberate failures). The slowest
successful HTTPS exchange took 12.956 s; the result is retained, not hidden
by the median or presented as a latency guarantee.

Concurrent Windows observation recorded two occasions when both gateway and
device ICMP requests timed out together, without interrupting the native test.
There were no device-only missing replies in this observation. This is improved
evidence after reconnection, not proof of the cause or permanent resolution of
the earlier reachability issue. A manual reconnect while idle does not cover
Wi-Fi loss during an active TLS exchange.

All exported settings matched the original pre-install backup. The runner reset
the C64 test program at completion and management remained responsive. The
read-only observer was stopped. The original interrupted runs are preserved
beside this successful repeat. UTC attempt, completion and heap-sample timestamps
were added to the runner so future interruptions can be compared with network
observations, including attempts that never produce a complete result.

## Build and CI review after hardware validation (2026-09-23)

The review reproduced a controller cache invalidation bug: the ESP32 dependency
manifest watched project roots and main directories, but omitted the shared TLS
sources, their component definition and shared UART buffer code. A new regression
failed on the original manifest, listing all 11 omitted compiled inputs. The
manifest now includes those directories. Four cache checks pass, including edits
to each input, input removal, rejection of a missing directory and exclusion of
generated build output. The regression is also included in the build workflow.

CI now runs the five portable HTTPS suites normally and under ASan/UBSan, with
sanitizer errors fatal, and the HTTP/HTTPS UCI compatibility gate under sanitizers.
The latter builds in its own output directories, including on a first parallel
build. Local validation passed all five suites in both modes, all eight counted
UCI compatibility checks, the four cache checks, ten retail recipe tests and the
repository's pinned lint check over the entire tests tree and `run-tests`.
Remote CI was configured but not executed as part of this local review.

These are build/test/documentation changes only. No runtime source, installed
firmware or device settings changed during this review. The hardware evidence
above remains associated with the existing v7 package; no new firmware package
or installation is implied. Hours-long reliability and the remaining physical
fault/measurement cases remain separate work before production qualification.

## Baseline and candidate identification

Record the product, board revision/FPGA type, firmware version, build date,
Git identity, FPGA core versions, bridge version, network interface and IP.
Use the system information screen and, when available, read-only `GET /v1/info`.
`GET /v1/version` identifies the REST interface version, not the firmware.
Keep device addresses and logs outside public source files.

The current integration builds the `target/u64ii/riscv/ultimate` management
target and `software/u64ctrl` controller. The candidate bridge is version 1.16
and the internal TLS stream protocol is version 2. Both images must agree;
old controllers must fail HTTPS without plaintext fallback. A retail case or
color name alone does not verify a particular FPGA image or board revision.

The raw `ultimate.app` and `u64ctrl.bin` build products are not a complete
Commodore update package. The repository's `target/u64ii/riscv/update` target
produces an updater that also incorporates FPGA images, controller bootloader
and partition table, ROMs and flash files. Its source can update the FPGA and
reset configuration. Do not relabel one of the raw images as an installer.
Resolve the current board/FPGA compatibility and preserve an exact known-good
installer before selecting that path. A stock firmware version with the same
number is not an exact backup of a locally modified experimental build.

### Current upstream versus the retail 1.1.0 baseline

The shared `u64ii` directory name does not establish binary compatibility.
Comparing upstream commit `1ba94abd314186559dfdeee9690e16d5d251f552`
with retail 1.1.0 (`7b628eb166872965ea59d66f90ffd9bcf7d71d8a`) shows:

- The management target changed from `rv32i` to `rv32im` with `-mno-div`
  and removed `COMMODORE=1` and Commodore-specific menu/board sources.
- The updater now embeds separate 50T/100T FPGA images and selects different
  management flash offsets according to the FPGA type.
- Product identity and board naming differ from the retail release.

Consequently the ordinary management build from this checkout is compile
evidence, not a retail C64 Ultimate installation candidate. The device build
recipe in `tools/c64u_https/README.md` now resolves this by exporting the exact
retail 1.1.0 baseline and adding the shared HTTP/HTTPS sources. It preserves
retail board support, builds RV32I, checks actual instructions and compares the
packaged FPGA and partition table against the reviewed official retail updater.
This is a software compatibility gate; physical-device validation is still
required. Do not run an updater on the device just to discover its board type.

References for the operator: [Commodore downloads](https://commodore.net/downloads/),
[official update/configuration procedure](https://commodore.net/commodore-64-ultimate-firmware-version-1-1-0/)
and the repository's `recovery/u64ii/README.md`. The recovery procedure covers
U64 Elite II and C64 Ultimate, but requires additional hardware and preparation;
it must not be assumed available merely because normal network access works.

## Before installing a candidate

1. Establish read-only access and record the current baseline. If access fails,
   confirm the IP on the device, the network connection and local-network access
   through any VPN. Do not change firmware to diagnose a network-access problem.
2. Save user configuration and required flash files independently of the updater.
   Retain the exact existing experimental package where possible, as well as the
   vendor recovery option. Record which components each package replaces.
3. Produce a manifest with source revision plus dirty diff, toolchain versions,
   SHA-256 hashes of every image, board/FPGA compatibility, host test results and
   the selected installation/recovery procedure. Review the actual package before
   executing it; a successful build is not installation authorization.
4. Keep the private epoch records described in `https-uart.md` out of restored
   configuration. If flash is erased or restored, fully remove power from both
   endpoints and clear transport state before using HTTPS again. The front power
   switch alone may leave the ESP powered, as the recovery guide explains.

## Test sequence and expected results

Use a disposable test application with the existing target 6 commands. Keep
credentials out of captured traffic and use controlled read-only endpoints.
For HTTP/HTTPS comparison, use the same request and expected response while
changing only the URL scheme and transport port. A positive TLS endpoint needs
a hostname and chain trusted by the firmware bundle; the temporary self-signed
certificates used by host tests are not trusted by the device.

| Test | Expected result | Evidence |
| --- | --- | --- |
| Existing HTTP baseline | Raw and object exchanges remain usable | Request/response bytes and status |
| HTTPS JSON and binary | Same target, commands, handles and body bytes as HTTP | Raw and object results, embedded NUL preserved |
| Raw response over 896 bytes | Existing continuation mechanism returns the exact body | Length, byte comparison, completion flag |
| Chunked response and HTTP error | Decoded body is correct; HTTP status is preserved | Body and status, no automatic redirect/retry |
| Wrong host, untrusted/expired certificate | Exchange fails with no returned partial body | Existing failure status; no plaintext connection |
| Missing/stale synchronized time | HTTPS fails until fresh synchronization is available | Clock age and result; HTTP still works |
| Timeout, Wi-Fi loss, full queues | Bounded failure; owned sessions/sockets expire | Elapsed time, queue/buffer counts, successful subsequent request |
| Independent management/controller restart | Old traffic is rejected; next exchange uses a new epoch | Fault-injection log and subsequent request result |
| C64 reset and FREE_ALL | Pending response resources are released | Allocation/connection counts before and after |
| Boot epoch persistence | First HTTPS use advances durable state; later requests do not rewrite boot state | Record values across management restarts |
| Storage failure/interrupted write | No epoch is used unless its persistence/readback succeeds | Record and filesystem state, failure result |
| Wi-Fi coexistence and sustained requests | Normal bridge traffic/power functions work; memory use stabilizes | Heap/stack/queue high-water marks and request counts |

Run destructive storage and restart fault injection only on a prepared lab
device with its recovery path available. Record actual elapsed times and memory
watermarks; image partition free space is not a runtime memory measurement.

## Host evidence and remaining boundaries

- Five portable suites cover request validation, wire framing, ownership, durable
  counter failures and replay after simulated independent restarts, with ASan/UBSan.
- Sixteen TLS adapter cases exercise real local TLS and certificate failures.
- Fifty UCI/HTTP/TLS cases now join target 6, the production HTTP renderer/parser
  and the production TLS stream through a host TCP adapter, with ASan/UBSan.
- Existing UCI secure-routing, body-removal and integer-width regressions pass.
- Retail build recipe regressions preserve board/power/FPGA inputs and reject
  wrong source baselines, unsupported instructions and stale packaged images.

The combined host suite still substitutes for UART, scheduler behavior, DNS,
flash and device clock synchronization. The focused malformed-framing cases
above are covered, but this is not a complete HTTP conformance/resource audit.
Record those boundaries separately from device results.
## Wi-Fi interruption coordination and controller measurements (2026-09-23)

The operator confirmed disconnecting the Commodore's Wi-Fi, reconnecting, and
exiting the Ultimate menu. A native repetition agent retained 18 silent-peer
handshake results: all returned 503, no response bytes, no native wait/drain
flags, and Idle completion. Subsequent exact binary HTTP and authenticated
HTTPS controls passed in 0.578 and 3.492 seconds without a reset or FREE_ALL
between the interruption and recovery. Exported settings were unchanged.
The C64 was reset only during final cleanup.

This **does not establish Wi-Fi loss during an active TLS handshake**. Paired
network probes recorded 41 device failures spanning 19.815 seconds while the
gateway remained reachable, but their onset followed the last ClientHello's
15-second deadline. The menu pauses the C64, so repeated native transactions
cannot guarantee another handshake while the operator navigates the menu.
The controlled active-handshake interruption remains open. Individual missed
pings also occurred before the operator action and are not attributed to it.

Management newlib allocation rose from 1,493,424 to 1,494,088 bytes (+664).
This run included Wi-Fi/menu activity and does not isolate the allocation's
owner; it is not evidence of flat heap use or proof of a TLS leak. ESP32 heap
and TLS worker stack were unavailable in the installed v7 image.

An earlier timed attempt is retained as incomplete: its baseline, deadline
failure and HTTP recovery passed, then a RAM read failed during HTTPS setup.
Operator action timing was not confirmed. Menu-related DMA visibility is a
harness hypothesis, not a diagnosed firmware defect. The revised runner avoids
RAM reads until the operator confirms leaving the menu. Six host CPU-emulation
cases validate the real 6502 repetition/history instructions with only the UCI
transaction substituted; they do not emulate hardware.

Controller bridge 1.17 adds read-only allocator and TLS worker stack samples
for the next experimental build. Hardware measurements and the timed two-hour
soak remain pending installation and execution. The timed runner rejects absent
or stale samples, controller restart, unexpected exchange results and operator
stop as successful completion. No hours-long hardware result is claimed here.

The bridge 1.17 candidate was built from a new retail snapshot and staged as
`/USB0/HTTPS-Metrics-20260923/c64u-https-metrics1.ue2` (4,757,648 bytes).
SHA-256: `483e05484815dc97f4225c0c5d9db346973a793ef31264e688bf8638fe7f4dc9`.
USB readback matched the package, and settings including the temporary FTP
service change were restored exactly. The operator subsequently reported
installation; the initial hardware result below prevents qualification.
The package passed RV32I/instruction/flash-size checks, embedded-image and
official FPGA/partition comparisons, six portable suites normally and with
ASan/UBSan, 2,112 framing checks, 50 real TLS/UCI integration cases, and the
body/integer/routing regression suites. Additional host validation passed 16
recipe/timed-runner tests, 191 OpenAPI tests and the 12-input cache regression.

### Initial bridge 1.17 candidate hardware result

After the operator reported installation and power-on, the management heap
endpoint exposed the new `esp32` object but repeatedly returned
`available=false`. The exact four-byte binary HTTP control passed (0.429 s);
the equivalent HTTPS request returned empty status 503 in 0.183 s and failed
its positive control. This is not an expected certificate-negative case.
The C64 test program was subsequently stopped using the C64 reset endpoint.
Exported settings still matched the original backup. The timed soak has not
started, and there are no valid ESP32 memory/stack measurements yet.

The updater's compiled comparison expects controller minor version 17, but
the running controller version and cause of the missing telemetry/HTTPS
failure are not yet established. A running-flash hash was not read. Do not
infer successful controller installation from the operator's general install
confirmation, or identify this as a TLS regression without further evidence.

The operator subsequently confirmed that all installer stages reported success.
This removes an operator-observed flash error as evidence; the running bridge
identity is still unobserved. Device-only network unreachability also occurred
and recovered during investigation, with the gateway reachable. Its cause is
not yet assigned.

### DMA receive recovery and startup diagnostics

A host regression reproduced a receive rearming defect in the actual DMA UART
ISR: simultaneous buffer-needed/receive interrupts can disable buffer requests
before a callback frees the received buffer. A successful consuming callback
did not wake the receiver again. Rearming after either callback outcome fixes
that reproduction, while rejection and deferred task release still pass and
an empty pool does not spin. This establishes a driver defect, **not proof that
it caused the observed device outage or initial HTTPS 503**.

The management heap response now includes optional `https_bridge` diagnostics:
detected controller version, compatibility, socket-worker readiness, clock
synchronization, telemetry receive/accept counts and connection allocation
state. These are management observations, separate from ESP32 heap values.
`created` means a connection object was allocated, not that TLS authenticated.
Hardware observations of these fields await installation of the next candidate.

The new candidate passed the complete retail build and the added UART ISR
regression under ASan/UBSan, plus the previous portable/framing/TLS/UCI gates.
The 16 recipe/timed-runner tests, 191 OpenAPI tests and cache-input checks also
passed. It was staged and read back at
`/USB0/HTTPS-Diagnostics-20260923/c64u-https-check1.ue2` (4,758,456 bytes), SHA-256
`78ec026e0001f3819f33d71cb0515e30ef072b6c2b1b5c2806135aa31a1b2c3e`.
Settings were restored to the original snapshot after transfer. Installation
and hardware validation of this candidate remain pending; the two-hour soak
is still not started.

### Installed diagnostic candidate: short run passed

The operator reported successful installation of the diagnostic candidate and
the device at its original address. Its heap endpoint now reports controller
1.17, a ready socket worker and synchronized clock, with fresh ESP32 telemetry.
The previous immediate HTTPS 503 did not recur in the initial short run.
This is a successful recovery observation after installation, not isolation of
the earlier failure's cause.

The 60-second requested run completed 22/22 HTTP/HTTPS exchanges over 65.977
seconds of test work (68.7 seconds including setup and cleanup), covering JSON
and exact raw bodies across continuation boundaries. Independent checks matched
all attempts/results, raw hashes/block lengths, elapsed duration, controller
identity and original settings. Management allocation stayed at 1,492,368 bytes
in all four samples. ESP32 internal free memory was 205,636 bytes initially and
204,924 at the end; the allocator's since-boot low-water mark was 163,548 bytes.
The TLS worker's observed minimum free stack was 8,108 bytes. Largest free block
fell from 139,264 to 110,592 bytes. These include first-use TLS/Wi-Fi activity;
cached instantaneous samples may overlap an active request and do not alone
establish a leak trend. No controller restart was observed. HTTP/HTTPS median
exchange times were 0.517/2.593 seconds. Settings were unchanged and the C64 test
program was reset after completion.

A separate requested 7,200-second run started with the same controller boot ID.
Its failed result is recorded below; the requested duration was not completed.

### Diagnostic candidate: two-hour attempt failed (2026-09-23)

The run lasted 1,168.6 seconds including setup and cleanup. All 422 attempts
have matching completed results: 211 HTTP and 210 HTTPS exchanges passed,
then HTTPS raw 894 in round 211 returned `503 SERVICE UNAVAILABLE`, no body,
and a single zero-length continuation in 2.392 seconds. This was an unexpected
positive-control failure. The runner stopped at the first failure, preserved
the evidence and reset the C64 test program during normal cleanup. It did not
retry the exchange or restart the soak. No firmware was flashed.

Independent review checked attempt/result ordering, successful raw body hashes
and continuation lengths, object-result summaries, final failure, settings and
telemetry. All exported settings still match the original backup. Management
remained reachable. Across 23 samples, management allocation stayed at
1,492,368 bytes, controller boot identity stayed unchanged, and telemetry was
fresh. ESP32 internal free memory ranged from 174,304 to 204,924 bytes; its
since-boot low-water mark fell from 163,548 to 161,888 bytes. Largest free block
ranged from 106,496 to 110,592 bytes. TLS worker minimum free stack fell from
8,108 to 8,028 bytes. These samples do not indicate stack exhaustion or a
controller restart, and do not alone establish absence of memory leaks.

A bounded read-only check about 141 seconds after completion found ESP32
internal free memory back at its initial 204,924 bytes, with the same boot ID,
fresh telemetry, ready worker and synchronized clock. The failure-time cached
174,824-byte sample therefore does not establish retained allocation. The
connection state `created` still does not identify the stage of TLS failure.

The independent network observer covered the final 16.5 minutes, with 488
probes per host: nine device probes and six gateway probes failed, including
six simultaneous failures. The last failed pair was approximately 14 seconds
before the failing HTTPS attempt; probes during that attempt succeeded.
These observations do not isolate device Wi-Fi, host connectivity or Internet
transport as the cause. A later HTTPS request from the host to the same fixture
returned HTTP 200 and the exact 894 bytes, which does not reconstruct its state
at the original failure or validate device-side recovery.

The original evidence remains unchanged, with a separate audit and read-only
diagnostic report. This is a failed soak, not two-hour qualification. Further
diagnosis needs the device-side transport/TLS failure reason; generic 503 and
successful later host access cannot distinguish firmware, network and peer
causes. The temporary completion heartbeat was removed after handling this
result; no replacement run was started.

### Failure-reason candidate (v10, bridge 1.18)

The next candidate preserves the existing target-6 calls and HTTP response
format while retaining the original failure evidence. Management records DNS,
socket, UART and HTTP parsing failures by epoch/session. The controller records
TLS stage, status, original Mbed TLS/trust error and certificate verification
flags before stream teardown. Cleanup, successful later requests and stale
older management work cannot erase the recorded failure. Telemetry remains
bounded and nonblocking; legacy heap-only telemetry is still accepted. This
adds observability, **not a claimed fix for the failed v9 soak**.

Controlled local real-TLS host tests passed 16 cases, now also asserting stage,
error/verification evidence and preservation after failed read/write calls.
Portable normal/sanitized tests include telemetry v1/v2 decoding, invalid
packet rejection and retention of the first error across wrapper/late errors.
The full retail build passed six portable suites, the UART receive regression,
2,112 framing checks, 50 real-TLS/UCI integration cases, body/integer/routing
checks, RV32I instruction and flash-size checks, and embedded-image/recovery
FPGA/partition verification. HTTP diagnostic callbacks are checked without
changing UCI response bytes. Twenty recipe/runner tests and 191 OpenAPI tests
passed; generated API documents match their source.

`diagnostic_smoke.py` is ready for a bounded hardware check after installation:
baseline requests, DNS failure, certificate rejection, optional LAN silent-peer
deadline, and recovery with correlated persistent diagnostic records. The
hardware runner saves the original failure before a delayed read-only telemetry
sample; losing that sample cannot erase or retry the failed exchange. Local
authenticated TLS fixtures run on the host with test-only roots, not on the
device; no device trust-store change has been made.

The v10 package is 4,760,832 bytes, SHA-256
`3ae43a62f187a5343a9ff817fffff50b3d846df73b9a7fb0cddafe962755b615`.
It was staged as `/USB0/HTTPS-Failure-20260923/c64u-https-diag2.ue2` and fully
read back with the same hash. Temporary FTP settings were restored and all
exported settings still match the original snapshot.
Installation and hardware checks followed staging; their results appear below.

### Installed v10: correlated diagnostic and recovery checks

After operator installation, read-only preflight observed bridge 1.18, fresh
telemetry v2, a ready worker, synchronized clock and settings identical to the
original backup. The first diagnostic run completed seven expected HTTP/UCI
responses, but failed its diagnostic assertion at the silent TLS handshake.
It is retained as a failed run, not retroactively marked passed; its recovery
step after the silent peer was not executed.

That assertion expected only internal `HTTPS_TIMEOUT` (3). The management TCP
wait actually expired first, recording `tcp_timeout`, code 0, read direction,
while the TLS worker recorded handshake stage 8, `HTTPS_TRANSPORT_ERROR` (4)
and Mbed TLS wrapper code -27648. The native request ended after 15.335 seconds,
and the controlled LAN server recorded its 246-byte ClientHello. The existing
tunnel converts negative TCP replies to transport failure; its independently
tracked remaining deadline need not expire on the same tick. No firmware change
or reinstall was made to change this behavior.

The runner now accepts that alternative only with the correlated TCP read-timeout
record, matching epoch/session, a 14–19 second native duration and exactly one
server ClientHello in the exchange window. Unit tests reject early/late failures,
ordinary TCP errors, write timeouts, missing/duplicate/out-of-window hellos,
stale telemetry, restarts and mismatched identities. Verification flags
`ffffffff` mean verification is unavailable and cannot prove certificate rejection.
All 21 recipe/runner tests and the changed helper lint passed.

A new run using that corrected evidence criterion completed 8/8 checks in
46.4 seconds: HTTP/HTTPS positive controls, reserved-name DNS failure and HTTPS
recovery, wrong-host certificate rejection and HTTPS recovery, and the silent
handshake followed by HTTPS recovery. DNS recorded local `dns_lookup` and TLS
TCP-open stage 7/status 4. Wrong-host rejection recorded handshake stage 8,
status 5, original code -9984 and hostname verification flag `00000004`.
The silent handshake ended at 15.175 seconds with the correlated timeout
evidence; subsequent authenticated HTTPS succeeded in 2.476 seconds.

Independent verification matched all eight attempts/results, raw hashes and
continuation lengths, diagnostic identities and server evidence, unchanged
latched records after recovery, and original configuration. Controller boot
identity remained unchanged. Management allocation stayed at 1,489,288 bytes;
ESP32 free memory returned to its initial 204,892 bytes, with 162,148-byte
since-boot low-water mark and 8,108-byte minimum TLS worker stack headroom.
Cleanup stopped the C64 test program. These checks validate diagnostics and
controlled recovery, not the cause or resolution of the earlier intermittent 503.

A subsequent bounded repetition run requested 300 seconds and completed 104/104
HTTP/HTTPS exchanges over 317.114 seconds of test work (the final pair extends
the requested duration). Independent verification matched all attempts/results,
raw body hashes, continuation boundaries, duration, original settings and the
same controller boot identity. All eight telemetry samples retained the exact
same deliberate-failure records and counts; no new failure was observed.
Management allocation remained 1,489,288 bytes. ESP32 free memory ranged from
174,596 to 204,920 bytes; since-boot minimum was 161,848 bytes, largest free block
stayed at 106,496 bytes, and minimum TLS worker stack headroom was 8,028 bytes.
Median HTTP/HTTPS exchange times were 0.4795/2.5305 seconds. Cleanup stopped the
C64 program, and both temporary LAN servers exited. No additional firmware
installation was performed. The earlier v9 intermittent failure did not recur
in this short window; neither its cause nor two-hour stability is established.

### Twenty-minute v10 reproduction attempt (2026-09-26)

Preflight confirmed bridge 1.18, fresh diagnostics, ready worker, synchronized
clock and settings identical to the original backup. The controller retained
the same boot identity as the earlier v10 tests; this preflight observation
does not extend the duration of active request testing. No other native test
was running, and no firmware, trust-store or network configuration was changed.

The requested 1,200-second run completed 478/478 exchanges (239 HTTP and 239
HTTPS) in 1,200.396 seconds of test work, or 1,203.1 seconds including setup
and cleanup. Independent verification matched all attempts/results, raw hashes,
continuation boundaries, duration, configuration and controller identity.
All 26 telemetry samples preserved the existing deliberate-failure records
and counters without a new failure. The C64 test program was stopped by normal
cleanup; the paired network observer also exited.

Management allocation remained 1,489,288 bytes throughout. ESP32 free memory
ranged from 174,572 to 204,900 bytes; the initial sample was 204,892 bytes and
the final and later idle samples were 204,900. The since-boot low-water mark
remained 161,848 bytes, largest free block stayed at 106,496 bytes, and minimum
TLS worker stack headroom remained 8,028 bytes. These observations show no
retained allocation growth over this run; they do not prove absence of all leaks.

HTTP median/maximum exchange times were 0.457/1.381 seconds; HTTPS values were
2.482/12.837 seconds. The slow HTTPS exchange was request 430, a 1,790-byte
response that still passed exact body/hash and `[895, 895, 0]` continuation checks.
Its latency cause was not isolated. All local network probes during that
request succeeded; those probes do not test the Internet path or remote server.

During the run, the independent observer recorded 516 probes each for the
gateway and device. One gateway probe and three device probes were unanswered,
including one simultaneous pair. No HTTP/HTTPS exchange failed during this run;
these isolated probe results do not establish a device disconnect or its cause.

The earlier intermittent empty 503 did not recur, including after request 422
and beyond the earlier failure's elapsed time. Its original failed evidence is
preserved and its cause remains unresolved. This is a completed 20-minute
reproduction attempt, not a passed two-hour soak or release qualification.

### Completed two-hour v10 soak (2026-09-26)

The same installed bridge 1.18 candidate completed the requested 7,200-second
native target-6 workload without firmware or configuration changes. The run
started at 08:29:35 UTC and finished at 10:29:41 UTC: 7,204.267 seconds of test
work, or 7,206.9 seconds including setup and cleanup. All 2,846 exchanges passed,
split equally between 1,423 HTTP and 1,423 HTTPS requests. The final request pair
extended the requested duration as designed; no retry or replacement run was
used to obtain this result.

Independent verification matched every attempt to a completed result, checked
raw response hashes and continuation boundaries, JSON-result summaries,
duration, released request handles and settings against the original backup.
All 145 telemetry samples were fresh and retained the same controller boot
identity and exact earlier deliberate-failure records. Neither diagnostic
failure counter advanced. Normal cleanup stopped the C64 test program, and
the independent network observer exited.

Management allocation started and ended at 1,489,288 bytes, with a transient
maximum of 1,489,528 bytes. ESP32 sampled internal free memory ranged from
173,040 to 204,900 bytes. Its initial value was 204,892 bytes; the last periodic
sample still overlapped activity at 174,776 bytes, while the final heap response
and a separate read-only check about 87 seconds later both showed 204,892 bytes.
Thus the lower cached sample is not evidence of retained allocation. The
since-boot low-water mark moved from 161,848 to 161,668 bytes, largest free block
remained 106,496 bytes, and minimum TLS worker stack headroom moved from 8,028
to 8,012 bytes. These observations show no retained allocation growth at the
measured idle endpoint or controller restart; they do not prove absence of
all leaks or qualify every resource-pressure scenario.

HTTP median/maximum exchange times were 0.461/11.059 seconds; HTTPS values were
2.483/12.745 seconds. Every response passed despite these latency outliers;
their causes were not isolated. The independent observer recorded 3,422 local
probes per host, with eight unanswered gateway probes and 32 unanswered device
probes, including seven simultaneous pairs. Local probe loss alone does not
establish a device disconnect or diagnose the Internet path or remote peer.

This run passes the two-hour workload and preservation checks on this device.
The earlier v9 empty-503 failure remains preserved and its root cause remains
unresolved; successful repetition is not proof that the underlying cause was
fixed. This result also does not replace the outstanding fault-injection and
release-qualification coverage described elsewhere in this report. The
temporary completion heartbeat was removed after handling the result.

### Controlled handshake interruption and failure capture (2026-09-26)

Two LAN fixtures now interrupt the connection after receiving a complete
246-byte ClientHello and waiting two seconds. One closes normally; the other
requests an abortive socket close using `SO_LINGER`. Independent fixture logs
record the receipt and completed socket-operation times. They do not send a
certificate, change device trust or capture application traffic. The runner
requires exactly one injection within the request window, matching management
and TLS failure identities, and an authenticated HTTPS recovery that preserves
the latched failure. Unit tests reject absent, duplicate, mistimed, incomplete
or misclassified evidence; the 23 recipe/runner tests and changed-helper lint
passed.

The first run returned the expected responses through exchange nine but failed
the runner's classification assertion on normal close. It remains a failed
47.2-second run; recovery after that close and the reset case were not executed.
The original Mbed TLS code was `MBEDTLS_ERR_SSL_CONN_EOF` (`-0x7280`, -29312),
at handshake stage 8 with status 5. Source inspection confirmed that a zero-byte
TCP read takes this path: status 5 is general TLS failure, not exclusively a
certificate error. The criterion was corrected specifically for this EOF,
correlated management `tls_reply` -5/open operation and unavailable verification
flags; neither firmware nor the first run's result was changed.

A separate run completed all 12 exchanges in 58.7 seconds: HTTP/HTTPS positive
controls, five induced failures, and HTTPS recovery after each failure.

| Induced condition | Native result | Captured diagnostic evidence |
| --- | --- | --- |
| Reserved-name DNS failure | Empty 503 | Management `dns_lookup`; TLS stage 7, status 4 |
| Wrong-host certificate | Empty 503 | TLS stage 8, status 5, code -9984, verification flag `00000004` |
| Silent handshake peer | Empty 503 after about 15.16 seconds | Management `tcp_timeout`; TLS stage 8, status 4, code -27648 |
| Normal close after ClientHello | Empty 503 after about 2.23 seconds | Management `tls_reply` -5; TLS stage 8, status 5, EOF code -29312 |
| Abortive close after ClientHello | Empty 503 after about 2.23 seconds | Management `tcp_read`, errno 104; TLS stage 8, status 4, code -27648 |

Independent verification matched every attempt/result, response body hash,
continuation length, handle cleanup and original configuration. Each induced
fault advanced both counters exactly once with matching epoch/session IDs;
successful recovery did not erase the record. Controller boot identity remained
unchanged. Management allocation stayed at 1,489,288 bytes; ESP32 internal free
memory returned to its initial 204,892 bytes, with 161,668-byte since-boot
low-water mark and 8,012-byte minimum TLS stack headroom. Cleanup stopped the
C64 program and both fixture processes were closed.

This demonstrates capture and recovery for these deliberately induced 503s.
The socket reset is supported by the fixture operation and device read error,
not an independent packet capture. Similar response timing does not identify
the cause of the earlier v9 failure. Interruption after successful TLS
authentication or during an HTTPS response body, and physical Wi-Fi loss during
active TLS work, remain separate coverage gaps.

### Authenticated response interruption and deadline diagnostics (2026-09-26)

The local real-TLS/UCI suite now has 54 passing cases, including four new
response interruptions. A verified TLS peer declares 2,048 body bytes, sends
1,024, then sends close_notify, closes without close_notify, resets TCP, or
stalls. The production client consumed 1,065 plaintext bytes including headers
in each case, returned an empty native 503, captured the read/parser failure,
and released the connection. A verified recovery response succeeded in the
same process and target with a reused header handle, before any reset. Both
connections were destroyed. ASan/UBSan instrumented the adapter, target and
parser; the linked Mbed TLS library used its existing host build.

The first local run's clean-close fixture treated the client's missing reciprocal
close_notify as a server error. The fixture now allows only that expected EOF;
the client must independently report TLS status OK plus HTTP body truncation.
All four cases subsequently passed, including in the full retail build.

On the installed v10 device, two separate runs verified an authenticated short
response using HTTPBingo's response-header fixture: a host preflight observed
104 bytes against a declared length of 2,048. The native request returned an
empty 503 in 2.49 and 3.10 seconds respectively, with matching processor
identities and read stage 9, status 8, original code 0 and verification flags 0.
Management recorded `tls_reply` -8/read operation. A subsequent valid HTTPS
response succeeded and preserved that record in each run. These establish
device read-stage failure and recovery; the host's measured body count is from
a separate connection, not a trace of the device's received bytes.

Neither complete device run passed. The first stopped at its fourth exchange
because HTTPBingo rejected a 20-second drip with HTTP 400 (its maximum is ten
seconds). That is a fixture mismatch, not an unexpected device transport error.
The second used HTTPBin's 18-second drip, independently observed from the host
to return all 20 bytes over 17.764 seconds. The device produced the expected
empty 503 at 15.178 seconds with verified-peer read stage 9/status 4/code -27648,
but management retained only `tls_reply` -4/read operation. The required primary
timeout evidence was missing, so that run remains failed and recovery after
the slow response was not executed. Both runs preserved settings, stopped the
C64 program and retained their original evidence; independent audits separately
verified their successful short-response checks without changing the overall
failure verdicts.

Source inspection identified two paths that can lose the local deadline cause:
expiry of the socket owner between operations, and expiry of a request while
queued. The management fix records `tcp_session_deadline` before closing an
active expired owner and `tcp_queue_deadline` before rejecting an expired active
request. It preserves cancellation/stale-owner behavior and the existing timeout
and UCI response format. This closes known diagnostic gaps; the v10 observation
does not distinguish which path occurred and is not proof of the earlier v9
503's cause.

The new deadline regression executes the actual production decision functions
with controlled clock/ownership under sanitizers. It checks exact deadline
boundaries, wrap, cancellation, stale requests and first-cause preservation.
CI and the retail build run it. All 27 recipe/runner tests and lint on the new
helpers passed. The hardware runner explicitly rejects a generic transport
wrapper as sufficient evidence of a timeout.

The v11 review candidate passed the full retail build, portable/sanitized suites,
UART regression, deadline regression, response framing, 54 real-TLS/UCI cases,
body/integer/routing checks, RV32I validation, and embedded FPGA/partition checks
against the recovery image. The package is 4,760,952 bytes with SHA-256
`a75d91de617834226db3bf726cf6dcb139014b389d2ac3b54a913e937992a86a`.
The bridge protocol remains 1.18. The operator reported installation complete;
post-installation observations and their limits are recorded below.

The v11 package was staged as
`/USB0/HTTPS-Deadline-20260926/c64u-https-deadline.ue2`. Full USB readback matched
the package hash, temporary FTP settings were restored, and exported settings
still matched the original backup. Staging did not install or flash firmware.

The first post-installation preflight could not reach the previous device
address. The information request failed from the test environment with
`No route to host` and from Windows with a connection timeout. Two device
pings went unanswered while two gateway pings succeeded. No native test was
started. These observations establish a connectivity blocker, not a firmware
regression or an HTTPS test failure; device link state and current address
had to be confirmed before continuing.

### Post-installation response capture and recovery (2026-09-26)

After the operator disconnected and reconnected Wi-Fi, the original address
became reachable. Read-only preflight confirmed the original configuration,
bridge 1.18, synchronized clock, a ready socket worker and a new controller
boot identity. Both failure counters initially contained only `count: 0`.
The shared version fields and new boot identify a restarted compatible device;
they do not independently fingerprint the installed v11 package.

The first run stopped after two expected responses because the test verifier
incorrectly assumed an earlier failure epoch existed. Its overall result
remains failed, with the original evidence preserved. A separate audit confirms
the successful baseline and correlated authenticated short-response failure;
neither recovery nor the slow-response case ran in that attempt. The verifier
now treats zero previous failures as an empty history. A regression covers
the first clean EOF, abrupt EOF and deadline, while rejecting missing counter
increments and zero epochs. All 28 recipe/runner tests and changed-file lint
passed. No firmware change was required for this test-tool correction.

A separate complete run passed all five native exchanges in 40.8 seconds:

| Exchange | Result | Request duration |
| --- | --- | --- |
| Authenticated baseline | HTTP 200, 20 expected bytes | 2.889 s |
| Incomplete authenticated response | Empty native 503 | 2.570 s |
| HTTPS recovery after incomplete response | HTTP 200, 20 expected bytes | 4.590 s |
| Slow authenticated response | Empty native 503 | 15.473 s |
| HTTPS recovery after deadline | HTTP 200, 20 expected bytes | 3.829 s |

The incomplete response recorded TLS read stage 9/status 8/code 0, with
management `tls_reply` -8/read operation. The slow response recorded TLS read
stage 9/status 4/code -27648 and primary management `tcp_timeout`, code 0,
read direction 0. Both had successful certificate verification flags 0 and
matching processor epoch/session identities. Each fault increased both counters
once; each following successful HTTPS request preserved those records. The
observed timeout used the existing `tcp_timeout` branch. The new
`tcp_session_deadline` and `tcp_queue_deadline` branches remain covered by
deterministic production-function tests, not by this hardware observation.

Independent verification checked all five attempts/results, response hashes,
continuation lengths, handle reuse, original settings, fresh telemetry and
unchanged controller boot identity. No reset occurred between a fault and its
recovery; final cleanup stopped the C64 test program. Management allocation
was 1,490,576 bytes throughout the complete run. ESP32 free memory was 204,752
bytes in every sampled point and the later idle sample; its since-boot minimum
was 163,360 bytes, largest free block 106,496 bytes, and minimum TLS stack
headroom 8,280 bytes. These short-run samples do not establish long-term
memory behavior. The earlier connectivity interruption and original intermittent
v9 503 still have unresolved root causes; the deliberate failures above do
not establish that they share a cause.

### Unattended Wi-Fi interruption feasibility (2026-09-26)

The operator cannot perform the disconnect/reconnect steps. Read-only review
of the retail candidate found separate Wi-Fi menu actions but no exposed
timed disconnect/reconnect operation. The configuration API does not execute
function items. The candidate's disconnect RPC calls `esp_wifi_disconnect`
without scheduling reconnection; its disconnected state waits for subsequent
commands/events. The remote menu shares the network connection being tested,
so it does not provide an independent recovery channel. Pre-sending menu keys
does not establish a reliable timed reconnect.

Runtime checks confirmed device reachability and the configuration API's
DHCP/static-address fields. The newer input and menu-screen API endpoints
returned 404, consistent with the retail source. No independent serial control
connection was identified. No Wi-Fi interruption was attempted. Completion
without an operator requires a verified independent control connection or an
installed device-side test hook that arms bounded reconnection before the
disconnect and records the actual outage. This coverage remains outstanding;
socket interruption tests must not be relabeled as physical Wi-Fi tests.

### Manual interruption without an operator deadline (2026-09-26)

The operator subsequently chose manual Wi-Fi control with no short countdown.
The host runner now waits for an explicit ready/menu-exited confirmation before
starting native work, and waits without a time limit for explicit reconnection
and menu-exit confirmation before reading RAM or cleaning up. A cancellation
before that confirmation is incomplete and defers device cleanup. Recovery
failures are retained and stop further exchanges. HTTPS deadlines and the
bounded native repetition count are unchanged.

Host regression tests cover long preparation/reconnection waits, cancellation
before and after arming, missing menu-exit confirmation, failed recovery, strict
confirmation values and uncertain partial initialization. This is a test-tool
change; no new firmware installation is needed. It removes operator timing
pressure but cannot guarantee that manual Wi-Fi loss overlaps an active TLS
request, because the Ultimate menu pauses the C64. The revised manual flow
was subsequently exercised on hardware as recorded below; reconnect/recovery
and proven active interruption remain separate results.

### Manual Wi-Fi reconnect/recovery verified (2026-09-26)

The revised manual flow completed in 116.0 seconds after the operator confirmed
reconnection and menu exit. Independent network observations recorded 31
consecutive unanswered device probes spanning 30.014 seconds, followed by
successful responses, plus one later isolated unanswered probe. All 251 gateway
probes succeeded. These observations corroborate the reported interruption;
probe timestamps do not measure the exact Wi-Fi disassociation interval.

The authenticated baseline succeeded in 3.388 seconds. After reconnection,
HTTP and HTTPS returned the exact expected four-byte body in 0.380 and 2.343
seconds respectively, with no C64 reset or FREE_ALL between the interruption
and recovery. Final cleanup stopped the test program only after those checks.
Independent verification matched all three control attempts/results, hashes,
block lengths and handle reuse, all 20 saved native empty-503 records against
their raw 32-byte entries, original configuration and unchanged controller
boot identity. Only three native attempts reached the silent peer as observed
ClientHellos; the 20 attempts must not be described as 20 TLS handshakes.

Both diagnostic counters advanced from 3 to 23. The retained final failure
had matching processor epoch/session identities: management `tcp_connect`,
code 118/detail 0, and controller connect stage 7/status 4/code 0. The record
remained present after recovery and in the later idle sample. This is a
connection-stage failure, so its zero verification flags do not establish an
authenticated peer.

The first unanswered probe began 14.419 seconds after the last observed
ClientHello, close to the nominal 15-second request deadline. The deadline
starts before that ClientHello; polling uncertainty and the paused C64 menu
prevent a reliable assertion that Wi-Fi loss interrupted an active handshake.
**Reconnect and HTTP/HTTPS recovery are verified; active-TLS interruption is
not proven.** The original intermittent v9 503's cause remains unresolved.

Management allocation increased from 1,490,576 to 1,491,280 bytes (+704), and
remained there in the idle sample. ESP32 free memory was initially 204,756,
174,412 in the immediate recovery sample, and 204,736 bytes after settling.
The since-boot low-water mark stayed 163,360 bytes; minimum TLS stack headroom
was 8,216 bytes. The allocation differences include menu/network activity and
remain unexplained; this single cycle is not evidence of either a leak or
unchanged memory use. The native runner, fixture server and network observer
all exited; no new firmware was installed for this test.

### Follow-up on the 704-byte management difference (2026-09-26)

Three idle observations spanning 802.253 seconds retained the same management
allocation of 1,491,280 bytes, arena and free-block count. Controller boot
identity and both diagnostic records remained unchanged, and configuration
still matched the original backup. ESP32 free memory eventually returned to
204,756 bytes, exactly its pre-interruption sample. These are separated
observations, not a continuous allocation trace or proof that repeated menu
operations cannot leak.

Inspection found a reproducible state-ownership defect in the retail menu:
`TreeBrowser` allocates its initial `TreeBrowserState`; `ConfigBrowser` replaces
the `state` pointer with a new `ConfigBrowserState` without deleting the first
object or updating `state_root`. Destruction frees the replacement but leaves
the original state allocated. The relevant retail sources match the pinned
1.1.0 baseline byte-for-byte; this defect predates the HTTPS changes.

A focused host test extracted the production constructors and destructors,
substituting UI dependencies and tracking live state objects. Five open/close
cycles retained one additional state each, for five total. Repeating the same
test with the checkout's existing `replace_root_state` implementation retained
zero states and kept the root pointer consistent. Both variants ran under
ASan/UBSan; deliberately reproduced orphan objects were released by the harness
after measurement. This verifies the state-ownership defect and that specific
fix, not the complete UI lifecycle or target allocator behavior.

The retail RV32I object code requests 52 bytes for the orphan state, excluding
allocator overhead. That does not account for the entire observed 704-byte
difference. Menu page/root allocations, other retained objects and allocator
accounting were not isolated by the existing aggregate `mallinfo` endpoint.
The DHCP restart path reuses its existing client object rather than allocating
a new client unconditionally, but that source observation does not exclude all
network-related retention.

The result is a confirmed pre-existing menu leak plus an incomplete attribution
of the device's full 704-byte delta. The newer checkout already contains the
state replacement fix, but the retail build recipe does not import that UI
change. A future retail correction should port only reviewed ownership changes
and test all affected menu lifetimes before a new installation. This diagnostic
pass changed no firmware, device settings or network state, and does not explain
the original intermittent v9 503.

### Retail menu-state repair candidate (v12, 2026-09-26)

The retail recipe now backports the checkout's existing `replace_root_state`
repair: delete the superseded initial state and keep `state` and `state_root`
consistent. Comparison of the v11/v12 prepared source manifests shows only
three runtime UI source changes, the build identity and two host test files.
The controller, network/TLS code, menu actions and borrowed root/page ownership
are unchanged. The immutable v11 source snapshot was preserved.

The maintained ownership regression executes production constructors,
destructors and replacement with substituted UI dependencies. It covers five
open/close cycles, root and nested-state teardown, virtual deletion, borrowed
stack/heap roots and balanced path/observer lifetimes under ASan/UBSan. The old
retail source retains five orphan states; the prepared fixed source retains
zero. This does not establish complete menu-cache cleanup or attribute the
entire 704-byte device delta.

The fresh RV32I candidate passed the complete offline build gates: controller,
management and updater compilation; six portable suites normally and with
sanitizers; UART receive and TCP deadline regressions; menu ownership;
2,112 response framing/fragmentation checks; 54 real-TLS/UCI cases; 63 body
removal, 99 integer-width and eight routing checks. Architecture/instruction,
embedded-image, flash-size and official recovery FPGA/partition comparisons
passed. Prepared-source hashes were checked again after building.

Separate repository checks passed 36 recipe/tool tests, 191 OpenAPI tests,
generated-document comparison, four controller-cache checks and the tests/tool
Python lint gates. Remote CI was not executed.

The updater is 4,761,024 bytes with SHA-256
`05fb3c80b5f386447033113bfc3cb72acab3eae9323b7eb1db251f4dd7b735af`.
Management is 1,062,684 bytes; the ESP application is 983,072 bytes. After an
initial host FTP connection timeout, direct-host staging succeeded and full USB
readback matched the hash. Original configuration was verified restored.
**This candidate has not been installed or hardware-tested.** The previous
v11 results do not qualify v12; installation, a bounded smoke run and repeated
menu measurements are the next device checks.

### v12 installation and bounded smoke check (2026-09-26)

After the operator's installation confirmation, read-only preflight observed
a new controller boot identity, fresh bridge 1.18 metrics, synchronized time,
zero diagnostic failure counts and configuration matching the original backup.
The reported retail product/FPGA/core remained unchanged. These observations
corroborate reboot and compatibility; the REST version fields do not expose a
running image hash, so exact v12 identification still relies on the operator's
installation of the previously readback-verified package.

The single native-UCI run completed ten exchanges in 29.6 seconds: HTTP and
HTTPS each returned the binary four-byte fixture, the typed JSON fixture and
895-, 896- and 2,048-byte bodies. All passed; the raw boundary continuations
were respectively `[895, 0]`, `[895, 1]` and `[895, 895, 258]`. The independent
audit matched all ten attempts/results, eight raw body hashes and continuation
sequences, object response status/handle lengths, original configuration,
unchanged controller boot and zero failure records. Typed object-query equality
was asserted by the runner; those query bytes were not persisted for a second
independent byte comparison.

Management allocation at agent readiness was 1,488,736 bytes. It increased by
224 bytes on first HTTPS use, then remained 1,488,960 through the remaining
checks and later idle samples. ESP32 free memory started at 205,608 and settled
at 204,880 bytes; the since-boot low-water mark was 163,532 and minimum TLS stack
headroom was 8,104 bytes. This short post-boot run does not attribute startup
allocations or establish long-run stability. No diagnostic failure was added.
Final cleanup stopped the C64 test program; no native run remains active.

A separate manual test will compare the warmed-up baseline with five local
Wi-Fi configuration-page open/close cycles, without changing any setting or
disconnecting. That menu-specific result is pending and is not included in
the smoke pass above.

### Retained allocation after five local menu cycles (2026-09-26)

The operator confirmed opening the local Wi-Fi configuration page five times,
fully exiting each time, without changing settings or disconnecting. Management
allocation rose from 1,488,960 to 1,495,016 bytes (+6,056); the allocator arena
remained 1,660,232 bytes. Original configuration, controller boot identity and
both zero failure records were unchanged. ESP32 free memory remained 204,880,
with the same 163,532-byte low-water mark and 8,104-byte TLS stack headroom.
This is retained allocation after menu use, not a passing memory-stability
result. A second batch was requested without a time limit to distinguish first
opening effects from repeated growth; that observation remains pending.

Inspection confirmed another pre-existing ownership defect beyond the v12
state repair. Retail `S_cfg_page` and `S_cfg_group` allocate new configuration
wrappers for each opening, but `ConfigBrowser` borrows its root and never
deletes those wrappers or their privately cached item nodes. The existing
state destructor clears the separate base `Browsable::children` list, which
does not release the derived page's private list. Advanced dynamically
allocated roots also lacked cleanup of their store/group wrappers.

A separate regression executes the actual retail menu factories and production
page/container lifetimes with synthetic stores and substituted UI rendering.
With three settings per store/group, old actions retain four nodes per cycle,
20 after five cycles. The proposed explicit owning browser retains zero,
deleting the state chain before its root. Tests also cover nested advanced-page
teardown and repeated use of borrowed static audio/general settings roots.
The complete 38-test recipe/tool suite passed with these ASan/UBSan checks.
Host node counts are not target byte measurements and do not by themselves
attribute all 6,056 retained bytes or the earlier 704-byte difference.

The v13 package completed all offline build/test gates, including the new page
ownership test, 2,112 framing checks and 54 real-TLS/UCI cases. The prepared
source was reverified after compilation. Compared with v12, runtime source
changes are limited to the retail menu's three owning constructor calls, the
advanced configuration root destructor and the new owning browser header.
Controller and HTTP/TLS source inputs are unchanged; rebuilt binary hashes
need not be identical because build metadata changes.

The updater is 4,761,288 bytes, SHA-256
`fe02ef145ec958ed647826d3721dc3ae3af2c8856f6ab4ba9b7e51920c44abd7`.
Management is 1,062,948 bytes and the ESP application is 983,072 bytes. The
candidate is stored locally and has not been staged, installed or tested on
hardware. The device remains on v12 while the requested second menu batch is
pending; no firmware operation should replace that comparison baseline.

### Second menu batch confirms repeated retention (2026-09-26)

The operator confirmed five more identical local page open/close cycles and
complete menu exit. Management allocation increased again, from 1,495,016 to
1,501,064 bytes: +6,048 in the second batch, +12,104 across ten cycles. This
rules out a purely first-opening effect for the observed increase; the v12
menu memory check has not passed. The exact contribution of each retained
allocation still requires target-level attribution.

The arena stayed 1,660,232 bytes and free blocks increased from 21 to 27.
Configuration still matched the original backup, controller boot identity
was unchanged and both diagnostic failure records remained at count zero.
ESP32 free memory, low-water mark and TLS stack headroom stayed at 204,880,
163,532 and 8,104 bytes. Independent comparison checked all three timestamped
samples, operator-confirmed cycle counts and source evidence hashes.

The v12 comparison is now complete. The next step is installing the built v13
owned-page repair and repeating smoke and menu checks against a fresh baseline.
Neither the passing host regression nor the repeated v12 growth proves the
v13 device outcome in advance, and neither explains the historical v9 503.

### v13 staging integrity check caught a transfer failure (2026-09-26)

The first staged USB copy had the correct length (4,761,288 bytes) but the wrong
SHA-256. Two additional full downloads returned exactly the same incorrect
bytes/hash as the first readback. The local build package still matched its
manifest. The suspect USB file was renamed with an `.unverified` suffix and
preserved; it was not installed. Configuration was restored and verified after
each staging/diagnostic operation.

The readbacks differ from the source at 974,327 byte positions, beginning at
offset 967,681. Repeated readback agreement establishes a persistent mismatch
in this transfer/storage/read path, but does not by itself identify whether
upload, storage or deterministic read behavior caused it. A single separate
upload using smaller, paced blocks subsequently matched the complete source
hash. A second full download in a fresh FTP session matched it again. The
original failed copy and readbacks are retained; successful restaging does not
explain the initial mismatch. Original configuration was verified restored.
The verified v13 copy is staged for manual installation, which has not yet
occurred. This staging failure is
separate from both the menu allocation findings and the unresolved v9 503.

### v13 post-install smoke check (2026-09-26)

The operator confirmed installing the verified copy and powering the device on.
Preflight observed a new controller boot, fresh bridge 1.18 metrics, synchronized
time and both failure counters at zero. Configuration matched the original
backup; product/FPGA/core identity remained the retail values. Exact installed
image identification still relies on the operator's installation confirmation
because the REST version fields do not provide a running image hash.

A single native run passed all ten HTTP/HTTPS checks in 29.8 seconds: binary
bytes, typed JSON, and 895-, 896- and 2,048-byte raw bodies with the expected
continuation boundaries. Independent verification matched all attempts/results,
eight raw body hashes/block sequences, object status/handle lengths, original
configuration, controller boot and unchanged zero failure records. As for v12,
typed query bytes were checked by the runner but not saved for independent
byte-level reinspection. Final cleanup stopped the C64 test program.

Management allocation was 1,487,520 at agent readiness and 1,487,632 after
first HTTPS use, remaining there through subsequent exchanges and idle samples.
ESP32 free memory settled at 204,896 bytes; its since-boot low-water mark was
165,244 and minimum TLS stack headroom was 8,216 bytes. These short-run values
do not establish long-term stability or explain startup allocation differences.
The separate local menu test starts from the warmed 1,487,632-byte management
baseline. Five unchanged Wi-Fi page open/close cycles have been requested;
their result remains pending and is not included in this smoke pass.

### v13 first menu batch (2026-09-26)

The operator confirmed five Wi-Fi page open/close cycles and complete menu exit.
At 13:41:07 UTC, management allocation was 1,488,024 bytes, an increase of 392
from the warmed 1,487,632-byte baseline. This is smaller than the v12 first-batch
increase of 6,056 bytes, but does not yet distinguish initial-use allocation
from repeated retention. Five further identical cycles have been requested;
the menu stability result remains pending.

The arena stayed at 1,659,960 bytes, with 12 free blocks. ESP32 free memory,
low-water mark and minimum TLS stack headroom were unchanged at 204,896,
165,244 and 8,216 bytes. Configuration still matched the original backup,
controller boot identity was unchanged, metrics were fresh and both failure
records remained at count zero. No native run or reset occurred between these
menu measurements.

### v13 repeat menu measurement and window lifetime repair (2026-09-26)

The operator confirmed five more identical cycles. The 15:24:39 UTC sample
reported 1,488,264 allocated management bytes: +240 in the second batch and
+632 across ten cycles. The two batch endpoints were 6,211.933 seconds apart,
so background allocation during this interval cannot be excluded. This is not
a menu memory stability pass or proof that all measured bytes came from the
menu. Independent assessment checked the three timestamped samples, confirmed
cycle counts and evidence hashes. Original configuration, controller boot and
both zero failure records were preserved; ESP32 free memory, low-water mark
and TLS stack headroom stayed at 204,896, 165,244 and 8,216 bytes.

Source review identified another definite retail lifetime defect. The persistent
CommodoreMenu allocates a Window on every appearance, but inherited
ContextMenu::deinit only restores the border. The next appearance overwrites
the pointer. ContextMenu destruction also omitted window deletion. The retail
recipe now releases and clears the window during deinit, retains border
restoration, releases any remaining window at destruction and guards drawing
when no window exists.

A host regression extracts the production window constructor/destructor,
CommodoreMenu initialization, ContextMenu cleanup/drawing and UserInterface
appearance/release methods. External UI dependencies and rendering are shims.
The old code retains ten windows after ten root appearance/hide cycles; the
repair retains zero. Direct destruction, repeated cleanup, empty-menu drawing,
nested teardown/color restoration and borrowed persistent actions also pass
with address/undefined-behavior sanitizers. This reproduces the source defect,
but does not replace validation of the repaired image on the device.

### v14 window repair build (2026-09-26)

The v14 package is 4,761,368 bytes with SHA-256
`81c191b6f863c0aea579c466e4c50f11c9f63c546416c97304b0902b76941999`.
Management is 1,063,028 bytes and the controller application is 983,072 bytes.
Compared with the v13 prepared source, the only runtime source change is
`context_menu.cc`; the other changed inputs are build identity and the two new
window regression files. HTTP, TLS and controller source inputs are unchanged.

All 40 tool/recipe tests passed, as did the prepared-image build gates: portable
sanitizer suites, UART receive and deadline checks, three menu lifetime suites,
2,112 framing cases, 54 real TLS/UCI cases, 63 body-removal checks, 99 integer
checks and eight secure-routing checks. RV32I instruction, size, embedded-image,
recovery FPGA and partition checks passed. Independent post-build verification
matched the complete source manifest, images, package and test log hashes.
The wrapper returned an error after successful package creation because of a
trailing carriage-return-only line; that wrapper was normalized. The original
nonzero exit and independent verification are retained rather than relabeled.

Compilation with the actual RV32I header confirmed a 44-byte Window object.
This is consistent with a small per-opening allocation, but exact allocator
overhead and attribution of the measured 240 bytes have not been established.
The package has not yet been installed or validated on hardware.

The staged v14 USB copy matched the complete package SHA-256 in two independent
FTP readback sessions. Original configuration was restored and verified after
staging. The verified copy is ready for manual installation; the older v13
staging mismatch remains unexplained.

### v14 post-install smoke check (2026-09-26)

The operator confirmed installation and power-on. Read-only preflight observed
a new controller boot, fresh bridge 1.18 metrics, synchronized time, original
configuration and both failure counters at zero. Running version fields still
do not expose an image hash; exact installation identity relies on the operator
confirmation of the verified package.

One native run passed all ten HTTP/HTTPS exchanges in 30.209 seconds: binary
data, typed JSON and 895-, 896- and 2,048-byte bodies with expected continuation
boundaries. Independent assessment verified all attempts/results, eight raw
hashes and block sequences, two object status/handle lengths, configuration,
boot continuity and unchanged zero failure records. Typed query equality was
checked by the runner; those query bytes are not persisted for an independent
byte-level recheck. Cleanup stopped the C64 test program.

Management allocation rose from 1,486,992 bytes at program readiness to
1,487,216 at the first HTTPS exchange, then stayed there through remaining
exchanges and idle samples. ESP32 idle free memory was 204,908 bytes, its
since-boot low-water mark 163,548 and minimum TLS stack headroom 8,108 bytes.
These short measurements do not establish long-term stability.

The separate local menu check starts from a fresh warmed baseline of 1,487,216
management bytes. Five unchanged Wi-Fi page open/close cycles have been
requested; their result is pending and is not part of the smoke pass.

### v14 menu measurement interrupted by device reachability (2026-09-26)

The operator confirmed five page open/close cycles, no setting changes or
disconnect, and full menu exit. The attempted post-cycle heap read failed:
WSL reported no route to the device, and an independent direct Windows read
timed out after bounded transport retries. Two device pings received no reply;
two gateway pings succeeded, and the device's neighbor entry was incomplete.
These observations establish a reachability problem, not its cause.

No post-cycle memory sample was obtained, so the menu memory check remains
incomplete. This is neither a stability pass nor evidence of a measured leak.
No reset, reconnect, native test or configuration change was performed during
diagnosis. The operator was asked to read the local Wi-Fi status and active
address without changing settings, then exit the menu. That diagnostic visit
must be counted separately from the original five cycles. The earlier ten
smoke exchanges remain valid for their recorded interval.

The subsequent local observation was `Link Up` with active IPv4 `0.0.0.0`;
the operator reopened the page and saw the same value. Thus there is no usable
IPv4 address at that moment, rather than merely an unreachable old address.
The reason is not established. Source inspection shows that the displayed
association state and interface address come from separate state fields; it
does not prove that the menu repair caused the address loss.

A single manual disconnect/reconnect was requested, without a power cycle or
reset, to try to recover management access while preserving the current boot.
Recovery is not yet confirmed. Any subsequent heap sample includes additional
diagnostic menu visits and reconnect activity and must not be presented as the
missing original five-cycle measurement.

### v14 access recovered without reboot (2026-09-26)

After the operator's manual reconnect, direct management reads succeeded at
16:10:13 UTC. Controller boot identity remained unchanged, metrics were fresh,
configuration matched the original backup and both HTTPS failure records still
had count zero. Management allocation was 1,487,704 bytes, 488 above the initial
menu baseline. ESP32 free memory was 204,736 bytes, with unchanged since-boot
low-water mark 163,548 and TLS stack headroom 8,108 bytes. These values include
the additional diagnostic page visits and reconnect, so they cannot determine
the memory effect of the original five cycles or the cause of the lost address.

A separate one-cycle menu experiment was prepared after recovery. Two baseline
heap reads five seconds apart were saved, followed by verification of original
configuration. The final baseline at 16:11:15 UTC was 1,487,704 management bytes
and 204,736 free ESP32 bytes. The operator was asked to open the Wi-Fi page once,
read the displayed address, change nothing and fully exit. This experiment is
pending; the interrupted original test remains incomplete.

### v14 single menu cycle after recovery passed (2026-09-26)

The operator confirmed one page open/close and full menu exit. Both post-cycle
reads succeeded. Management allocation remained exactly 1,487,704 bytes across
all four before/after samples, with arena 1,659,880 and 17 free blocks unchanged.
The final post-cycle sample was at 16:12:40 UTC, 84.665 seconds after the final
baseline sample. ESP32 free memory increased from 204,736 to 204,764 bytes;
the low-water mark and TLS stack headroom stayed at 163,548 and 8,108 bytes.
Configuration matched the original backup, controller boot was unchanged and
both failure records remained at zero. Independent assessment checked the
sample sequence, values and evidence hashes.

This is a pass for the single measured cycle only. It does not explain the
earlier loss of IPv4 or complete the interrupted five-cycle test. A separate
five-cycle repetition was prepared with a fresh baseline at 16:13:29 UTC:
1,487,704 management bytes and 204,764 free ESP32 bytes. The operator has been
asked to perform that repetition without changing settings or disconnecting;
its result is pending.

### v14 five-cycle repetition after recovery passed (2026-09-26)

After the operator's completion/recheck reply to the five-cycle request, both
post-cycle heap reads succeeded. At 16:16:25 UTC, management allocation was
still 1,487,704 bytes: zero growth from the fresh 16:13:29 baseline. All four
before/after samples agreed on management allocation, arena 1,659,880 bytes,
17 free blocks, ESP32 free memory 204,764 bytes, low-water mark 163,548 bytes
and minimum TLS stack headroom 8,108 bytes. Configuration remained identical
to the original backup, controller boot was unchanged, metrics were fresh and
both failure records remained at zero. The final sample interval was 175.747
seconds. Independent assessment verified the samples and evidence hashes.

The measured single cycle and subsequent five-cycle repetition therefore
showed no management allocation growth on v14 after network recovery. This
supports the menu lifetime repair for these observed cycles. It does not
establish long-term stability, explain the earlier zero IPv4 address, or turn
the interrupted original five-cycle attempt into a pass. No native test, reset
or reconnect occurred during these two successful menu experiments.

### v14 two-hour native run passed (2026-09-26)

One uninterrupted native run completed from 16:25:21.248560 to
18:25:26.322459 UTC. All 2,966 attempts have matching successful results:
1,483 HTTP and 1,483 HTTPS exchanges. The timed workload lasted 7,202.503
seconds; independent first-request to last-response timestamps span 7,201.928
seconds, with 7,205.074 seconds for the complete run and cleanup. No second
native workload was started. The runner stopped the C64 test program during
its final cleanup, and both the native process and network observer exited.

Independent verification checked the full attempt/result sequence, expected
alternating protocols and workload, raw body SHA-256 values, exact lengths,
895-byte continuation boundaries and object status/handle lengths. Each
protocol completed 371 typed JSON requests and 1,112 raw requests covering
894, 895, 896, 1,024, 1,790 and 2,048 bytes. Typed JSON query equality was
asserted by the runner, but the query bytes were not persisted for an
independent byte-level comparison. Original configuration was preserved.
Controller boot identity remained unchanged, bridge minor version was 18,
sample sequence and uptime progressed, and both diagnostic failure records
remained exactly count zero throughout and at cleanup.

All 151 management samples were exactly 1,487,704 allocated bytes. ESP32
internal free memory began at 204,764 and ended idle at 204,756 bytes: an
eight-byte decrease whose allocation ownership is not established. Samples
during activity ranged from 172,888 to 204,764 bytes; instantaneous readings
can overlap TLS activity. The since-boot low-water mark reached 161,688 bytes.
The largest internal free block began and ended at 110,592 bytes, with a
transient minimum of 106,496. Minimum TLS stack headroom was 8,028 bytes,
compared with 8,108 at readiness. All telemetry was fresh, at most 1,040 ms
old. These observations establish no management allocation growth during
this run, rather than proving that every allocation path is leak-free.

HTTP exchange timing had median 0.449 seconds and maximum 1.602; HTTPS had
median 2.463 and maximum 4.108. The independent network log contained 3,549
probes per host during the run: 32 device misses and six gateway misses,
with six simultaneous misses. Each host's longest miss streak was one probe,
there were no observer process timeouts, and the maximum sample gap was
3.887 seconds. Ping loss was therefore observed despite all native requests
passing; its cause is not established and uninterrupted network reachability
is not claimed.

The sustained v14 workload passes within these recorded conditions. The
historical v9 intermittent 503, first v13 USB staging mismatch and earlier
v14 zero-IPv4 incident remain unresolved. The original interrupted menu
measurement remains incomplete. This run does not replace missing active-TLS
Wi-Fi interruption, independent processor restart, UART pressure, clock fault
or flash power-loss qualification, and does not establish stable-release
readiness.
