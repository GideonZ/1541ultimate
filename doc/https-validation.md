# HTTPS validation snapshot — October 30, 2026

This is a draft review candidate, not an installation or hardware qualification.
The initial host and cross-build snapshot is
`fb8c76ab3141135f33aa74533ec068f608788d1f`. Documentation and explicit CI package
declarations added after this snapshot do not change the tested firmware inputs.
The subsequent factory-test and parser-preparation corrections are recorded below.

The later [startup and version review](https-startup-review.md) removes the
application's SAR entropy manipulation and separates default/provider versions
as 1.22/1.23. The table below is the earlier historical snapshot; its module
versions and packages are not the corrected installation candidate.

## Results

| Validation | Result |
| --- | --- |
| Device-free host regression | 81 stages; 18,865 assertions; zero failures |
| HTTP parser unit tests | 37 tests passed |
| TLS peer matrix | 184 cases across seven paths and RSA/ECDSA; each includes recovery |
| Host evidence rejection checks | All seven incomplete, missing or altered examples rejected |
| Native firmware | Default and journal-enabled variants built |
| ESP32 firmware | Default 1.21 and provider-enabled 1.22 variants built |
| Embedded owner fixture | RSA/ECDSA QEMU: 644 checks, 48 attempts, two boots |
| Embedded evidence rejection checks | All 24 altered or incomplete examples rejected |
| RAM identity diagnostic | 61 sanitized host checks and 12 instruction/MMIO scenarios |
| Experimental installer | 23 orchestration scenarios; seven unsafe variants rejected |
| Installer packages | Combined, XC7A50T and XC7A100T builds; carried payloads/checksums audited |

The host assertion total includes the session model's 1,354 checks and the assembly
suite's 172 checks. Parser test counts and mutation checks are separate; they are
not added to that assertion total. Wi-Fi reply-table and ingress tests also pass.
The published head `bf5c7b0` passed the hosted HTTPS host check, but its Build job
failed in the factory-test link described below.

Reproduce the host run using the public dependency pins and command in
[the host test README](../target/pc/linux/test_http_tls/README.md). Complete raw
logs, peer records, command results and independent audits are retained in the
review evidence package. Its download link must be verified when it is published;
no unpublished download URL is represented here as available.

## Factory-test build correction

The [hosted Build job](https://github.com/GideonZ/1541ultimate/actions/runs/41028507948/job/123972319735)
failed because the U64-II factory tester links `wifi_cmd.cc` without the WiFi
application that defines `wifi_provider_receive`. A fresh local cross-build of
`bf5c7b0` reproduced that undefined reference.

The factory target now selects `WIFI_COMMANDS_ONLY`, excluding the provider
ingress hook while retaining frame-length checks and ordinary RPC reply handling.
Normal firmware keeps the hook; recovery keeps its existing exclusion. No HTTP
interface or provider protocol changes are involved.

Local checks on the correction passed:

- Factory application, ELF, simulation output and raw binary cross-build.
- Default and journal-enabled U64-II application cross-builds.
- Sanitized command tests for normal, factory and recovery configurations,
  covering frame rejection, reply delivery, timeouts and late replies. Factory
  and recovery tests supply no provider stub. The factory regression fails to
  link with the original guard and passes with the correction.
- Symbol checks: factory retains the receive ISR without the provider hook;
  normal firmware retains the provider hook.
- Complete host regression: 81 stages, 18,865 counted assertions, 37 parser tests
  and 184 TLS cases with recovery. Independent evidence audit passed and all
  seven missing or altered evidence controls were rejected.

Cross-builds used xPack RISC-V GCC 11.3.0. These results are local; the full hosted
multi-board Build job has not been rerun with this correction. No firmware was
installed or executed on a device. Previous hardware and installation gates remain.

## Reused-checkout parser preparation

The next [hosted Build run](https://github.com/GideonZ/1541ultimate/actions/runs/41031130807/job/123980867788)
at `5953aaf` stopped before compilation: parser inputs did not match the complete
before or after manifest. Its log shows a reused checkout with a top-level reset
and forced submodule checkout. It does not include individual parser file hashes.

A local regression reproduces the same error with that checkout sequence:
tracked files return to the public pin, but the patch-created, untracked
`response_test.cpp` survives. Preparation now retains that addition only when it
matches the reviewed result hash and all tracked inputs match the public pin.
It applies the remaining entries and verifies all result hashes. Unknown contents
are still rejected, and no broad cleanup or reset was added to preparation.

All 14 preparation tests pass, including three consecutive real submodule
checkout/reset cycles, repeat application, source exports, LF/CRLF inputs and
preservation of unrelated files. Negative tests check that modified or missing
inputs, partial patches and changed patch contents fail without writing files.
The local workflow changes add this regression to both jobs.

The corrected preparation also passes the full 81-stage host suite (18,865 counted
assertions, 37 parser tests and 184 TLS cases with recovery), the independent host
evidence audit and all seven evidence rejection controls. Default U64-II application
and factory cross-builds pass. The regression fails with the original helper.
These are local results; hosted CI has not run with this preparation correction.

## Provenance and limits

The candidate includes upstream `03c70e6e` and preserves published PR history at
`26a8938319e35494c5a7371d992797bc0f8743fb`. The TLS core is pinned to
`529b0450955611bd82db921db3bae10222203eba`, Mbed TLS to
`2b96dd8eebe880f304c69976b3c2fa0c5100cbb6`, and ESP-IDF to
`79e3454c68248bd7d881721c9b2e94561378a8ce`. The public HTTP submodule base is
`ad73d3217bb0e1b3f37e0dbba595309b942aea17`; the checked-in framing patch and hash
manifest are applied by `tools/prepare_http_parser.py`. A private submodule
commit is not required.

QEMU ran source `645a15cc4d0b6f349920b3def04ead3e296c7ec5`. Archive comparison
confirms that its embedded runtime inputs are byte-identical to the final host
snapshot. Subsequent differences are the host runner/fixture and diagnostic or
installer files, not the embedded runtime. Both use the current repository CA
list, SHA-256 `796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe`.

The embedded runs measured minimum stack margins of 9,188 bytes (RSA) and 8,344
bytes (ECDSA), maximum scheduling gaps of 11/25 ms and maximum acknowledgement
latency of 21 ms. Free heap declined by 2,756/2,736 bytes between the first and
last samples. Balanced scoped allocations do not establish global leak freedom;
that decline and the earlier long-duration findings remain part of the record.
These are emulator measurements, not native-device memory or timing guarantees.

No revised firmware was installed or executed on a physical device during this
validation. U64/U2-family device coverage, actual UART behavior, entropy, journal
durability, exact post-write controller verification and recovery remain open.
The diagnostic and installer artifacts are held, not released for installation.
See [the installation gates](https-release-readiness.md).

## Failures retained and corrected

- Fresh exports exposed an HTTP patch application problem when a parent Git
  repository was present. The helper now applies from the repository root with
  an explicit destination; clean exports and real submodule layouts pass.
- Parser tests initially used C++14 against a GoogleTest installation requiring
  C++17. The host runner now selects C++17 explicitly.
- Upstream Wi-Fi tests exposed a heavyweight ingress-header dependency. A small
  ingress interface preserves the upstream reply tests and validates frame size.
- The HTTP general test initially ran outside its fixture directory. The runner
  now sets its working directory explicitly.
- One connected ECDSA total-deadline case exposed an undelivered renewal in the
  synchronous host fixture. The fixture now delivers that exchange before the
  next operation or terminal close. Production owner code and time bounds were
  not relaxed; the complete matrix subsequently passed.
- A separate installer export omitted the HTTP submodule and failed to compile.
  Its corrected export includes the public submodule and verified patch.
- Review found that strict installer gates affected the ordinary updater and
  that the paired controller label could differ. The ordinary updater is restored
  byte-for-byte from upstream. Strict gates live in the explicitly selected
  coordinated updater, with required native/controller hashes and label checks.

Failed runs remain separate from the successful snapshot. Earlier prototype
hardware evidence and standalone TLS soaks are historical results, not passes
for this connected revision.

## Migration from the published prototype

The command target, HTTP operations and caller-visible interface remain unchanged.
The prototype-specific TLS stream and metrics commands are replaced by identity,
capability and owner-session negotiation behind a replaceable secure provider.
The development UART allocation still requires maintainer agreement.

The old prototype's device scripts depend on its removed protocol and metrics.
They are not presented as passing tests for this implementation. The current host
matrix and embedded fixture cover the replacement locally; adapting the physical
runner to the project's E2E framework and qualifying the device remain explicit
work. Original scripts and hardware reports remain in published commit history.
The independently merged HTTP fixes and upstream tests are retained.
