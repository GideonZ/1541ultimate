# HTTPS experimental review readiness

Updated 2026-09-26. Intended scope: review of the HTTPS integration and an
experimental retail Commodore 64 Ultimate package. Stable-release qualification
is incomplete. Nothing in this document asserts that a package was published.

The v12 retail review package is built and staged with full USB readback
verification: 4,761,024 bytes, SHA-256
`05fb3c80b5f386447033113bfc3cb72acab3eae9323b7eb1db251f4dd7b735af`.
Its offline build/test gates passed. Installation is operator-confirmed and the
ten-exchange HTTP/HTTPS smoke check passed. Two batches of five local menu
cycles retained 6,056 and 6,048 bytes, confirming repeated growth. The v13
owned-page repair has passed offline build/tests; its 4,761,288-byte local package
has SHA-256 `fe02ef145ec958ed647826d3721dc3ae3af2c8856f6ab4ba9b7e51920c44abd7`.
Its first USB staging attempt failed full-hash verification and was quarantined.
A separate paced transfer passed two full readbacks, including a fresh FTP
session; the verified copy is now staged. The initial transfer/storage mismatch
remains unexplained. V13 installation is now operator-confirmed and its ten
HTTP/HTTPS smoke checks passed; two local menu batches retained 392 and 240
bytes. The second batch spanned 6,211.933 seconds and its allocation source is
not independently established. A further persistent-menu window leak has been
reproduced and repaired in the retail recipe with a host lifetime regression.
Runtime version fields do not provide an
independent installed-image hash.

V14 adds the window lifetime repair. Its 4,761,368-byte package has SHA-256
`81c191b6f863c0aea579c466e4c50f11c9f63c546416c97304b0902b76941999`.
All 40 tool/recipe tests, prepared-image build gates and independent source/image
verification passed. USB staging passed two complete readbacks with original
configuration restored. Installation is now operator-confirmed and all ten
post-install HTTP/HTTPS smoke exchanges passed independent evidence checks.
The first local menu memory attempt remains incomplete: after five operator-confirmed cycles,
the device was unreachable from both Windows and WSL, preventing the post-cycle
sample. Gateway reachability was intact; the cause needs investigation.
One manual reconnect restored management access without a controller reboot or
configuration change. The resulting heap sample includes recovery activity;
a separate one-cycle menu experiment passed with zero management allocation
growth, original configuration and unchanged boot/failure records. The separate
five-cycle repetition also passed with zero management growth and unchanged
ESP32 heap/stack measurements. The earlier address loss remains unexplained.

The installed v14 candidate passed its two-hour native HTTP/HTTPS run on
2026-09-26, from 16:25:21 to 18:25:26 UTC. Independent checks matched all 2,966
attempts/results (1,483 per protocol), verified a 7,201.928-second exchange span,
original configuration, continuous controller boot and unchanged zero failure
records. All 151 management allocation samples were 1,487,704 bytes. Final
ESP32 idle free memory was 204,756 bytes, eight below its initial value; minimum
TLS stack headroom was 8,028 bytes. The independent network observer recorded
32 isolated device ping misses and six gateway misses, six simultaneous,
among 3,549 probes per host. Their cause is not established; no native exchange
failed. This pass does not resolve the historical incidents or qualify a stable
release.

## Behavior and review scope

Applications keep HTTP UCI target 6, header/body handles, exchange commands and
continuation semantics. Selecting an `https://` URL enables authenticated TLS
on the internal controller. Certificate roots and synchronized time are device
responsibilities. A failed secure connection does not fall back to plaintext.
The existing empty-503 transport-failure response is retained; correlated
processor diagnostics provide additional failure evidence.

Review the shared HTTP stream boundary, strict response framing and 895-byte
data/255-byte status limits together with their HTTP compatibility tests. The
UART/TLS changes include deadline, reset/session ownership, receive recovery
and controller cache invalidation tests. The retail preparation recipe pins
the original RV32I board baseline and dependencies; the ordinary upstream
u64ii build is not a substitute retail firmware image.

The retail recipe also backports the existing configuration-browser state
replacement repair. A separate owning browser now releases roots allocated by
retail menu actions, while static roots remain borrowed. Both ownership tests
reproduce the old leaks before checking the fixes.

## Evidence and limits

| Area | Evidence | Remaining limit |
| --- | --- | --- |
| Native HTTP/HTTPS API | Exact body bytes, boundary continuations, object/raw mode and recovery checks | Limited to recorded fixtures and device |
| Sustained use | v14: 2,966 exchanges over 7,202.503 seconds of workload; v10: 2,846 over 7,204.266 seconds | Bounded runs do not explain historical incidents or validate future images |
| Failure capture | Controlled certificate, handshake, close/reset, incomplete body and deadline cases | New session/queue primary-deadline paths have host tests only |
| Wi-Fi recovery | Manual disconnect/reconnect followed by exact-byte HTTP/HTTPS recovery without intermediate reset | Overlap with active TLS was not proven |
| Runtime memory | v14 management allocation constant across 151 two-hour samples; separate single-plus-five menu cycles showed zero growth after recovery | Final ESP32 idle free was eight bytes lower; exact allocation ownership, earlier retention attribution and longer-term behavior remain open |
| Menu ownership | Host tests reproduce leaked states, page/item wrappers and windows; v14 single-plus-five device cycles passed after recovery | First v14 attempt lacks a post-cycle sample after zero IPv4; cause remains unresolved |
| Build/distribution | Pinned source manifest, RV32I inspection, embedded-image and recovery comparisons | Remote CI has not been observed; final source/package pairing requires review |

The original intermittent v9 503 remains unexplained. A deliberately induced
503, a passing repeat, and a generic status match do not establish its cause.
Other outstanding device cases include independent processor restart, sustained
UART queue pressure, cold/stale clock injection and actual flash power loss.
Keep these limitations attached to any experimental review or package.

The separate `HTTPS host validation` workflow runs portable sanitizer, framing,
UART/deadline, real loopback TLS/UCI, retail recipe/menu lifetime, native
repetition, cache, API and lint checks on a standard GitHub runner. It pins the
Mbed TLS revision used by ESP-IDF 5.3.6. The existing full firmware/FPGA workflow
still needs its self-hosted build environment. Host CI does not flash a device
or replace the recorded retail hardware evidence.

## Reproduction and publication preparation

Build instructions, toolchain versions and recipe tests are in
[`tools/c64u_https/README.md`](../tools/c64u_https/README.md). The reviewed HTTPD
dependency change is distributed as `tools/c64u_https/httpd-response.patch`;
apply it using the accompanying helper for a normal checkout. Retail preparation
and CI apply it explicitly. Do not rely on an uncommitted submodule working tree
to reproduce the change.

The checked-out top-level README, tests guidance and build workflow were
reviewed; no top-level contribution guide or pull-request template was found.
The contribution guide under `neorv32` belongs to that dependency. Preserve the
repository license and third-party notices.

Before public submission, review the complete proposed source diff, dependency
patch and source manifest; run the recorded host gates; and attach the current
hardware report. Exclude device configuration backups, credentials, private
addresses, local evidence directories and build outputs from source submission.
Pair any distributed experimental binary with the exact source and build recipe,
its SHA-256 and matching validation results. Do not transfer older hardware pass
claims onto a newly built image.

The v14 source/package pairing is recorded in
[`https-source-provenance.json`](https-source-provenance.json): pinned baseline
and dependencies, preparation recipe, shared input hashes, built image hashes
and test-log hashes. The original build manifest predates hardware testing;
the separate device report records subsequent installation and validation.
Original input hashes preserve mixed checkout line endings; separate LF hashes
identify the corresponding Git source content, and CRLF inputs are listed.
Compiler timestamps can change rebuilt binary hashes, so a rebuild requires
its own identity and validation rather than inheriting the v14 binary claim.

Installation and bounded HTTP/HTTPS smoke checks of the v13 owned-page repair
are complete. Its repeated menu check did not establish stability. The v14
single-plus-five menu comparisons after recovery are complete and passed.
The original v14 interruption and its zero-IPv4 cause remain separate open
findings; stable-release qualification is not established by the menu checks
or the subsequent two-hour pass.
Retain original configuration and compare controller boot identity, failure
records, management allocation and ESP32 heap/stack after warmup. A failed or
incomplete check stays failed or incomplete; do not silently restart it.
