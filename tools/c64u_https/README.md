# Retail C64 Ultimate HTTPS test build

This is the device build path for the **Commodore 64 Ultimate** retail 1.1.0
baseline, including Slimline/BASIC Beige. It exports official source commit
`7b628eb166872965ea59d66f90ffd9bcf7d71d8a` and adds the shared target 6 HTTP/HTTPS
implementation from this checkout. It does not use another application project,
its prototype, RAM loader or installer.

The ordinary `target/u64ii/riscv/ultimate` build in the main checkout remains the
upstream contribution build. Its newer board/CPU assumptions must not be used
as the retail installation recipe. The retail snapshot preserves RV32I,
`COMMODORE=1`, product identity, menu/bling support, memory map, flash offsets,
controller power behavior and Wi-Fi bridge from the official baseline.

## Preparation and build

Use Linux or WSL with Python 3.9+, Git, GNU make, host GCC/G++, the xPack RISC-V
GCC 11.3.0 toolchain, ESP-IDF 5.3.6 with ESP32-S3 dependencies, and the host TLS
test dependencies described in `software/network/https/tests/README.md`.
Initialize the `software/lwip` and `software/httpd` submodules. The local Git
object stores must contain the full revisions pinned in `prepare.py`.

The retail baseline is not an ancestor of every fork's default branch, so a
full clone of that branch alone may omit it. Fetch the pinned inputs once
before offline preparation or recipe tests; these commands do not check out
files or change the current branch:

```sh
git fetch --no-tags https://github.com/GideonZ/1541ultimate.git 7b628eb166872965ea59d66f90ffd9bcf7d71d8a
git -C software/httpd fetch --no-tags origin ad73d3217bb0e1b3f37e0dbba595309b942aea17
git -C software/lwip fetch --no-tags origin 26a22151f4b7ebb2925523192edc83fa7f31cba9
```

The HTTPD dependency needs the tracked `httpd-response.patch` until an upstream
revision includes the fix. For host tests or ordinary builds, first run
`python3 tools/c64u_https/httpd_patch.py`. This refuses conflicting edits and
is safe to repeat. Retail preparation applies the same patch automatically to
its exported dependency; it does not depend on uncommitted submodule changes.

Run from this checkout, selecting a **new** output directory:

```sh
python3 tools/c64u_https/prepare.py --output /path/to/c64u-build
. /path/to/esp-idf-5.3.6/export.sh
python3 tools/c64u_https/build.py \
  --tree /path/to/c64u-build \
  --cross /path/to/riscv-xpack/bin/riscv-none-elf- \
  --mbedtls-source /path/to/mbedtls \
  --mbedtls-build /path/to/host-mbedtls-build \
  --recovery-updater /path/to/c64u_v1.1.0.ue2
```

The legacy retail assembler rules also invoke `python`; make Python 3 available
under that name in the build environment. The source snapshot records hashes of
the selected working-tree files, adaptation recipe, baseline and dependencies.
It refuses to overwrite an existing directory. Keep build outputs on Linux's
filesystem under WSL. The preparation/build scripts do not upload, execute or
flash anything on a device. The separate smoke test below runs a C64 program.

The build checks the exact official updater SHA-256, compares the embedded FPGA
and ESP partition table with it, checks the management flash size and instruction
set, and verifies that the installer embeds each newly built image exactly once.
It runs portable sanitizer tests and the UCI regression/integration suites against
the **retail snapshot**, including real local TLS and certificate rejection.

Successful output is `candidate/c64u-1.1.0-https-test.ue2` plus `manifest.json`.
The `.ue2` is the retail updater's `update.app`, not a renamed management image.
It is a test candidate, not a hardware-validated release or an exact backup of
any installed experimental firmware. Read `doc/https-device-validation.md`
before an installation review.

## Adaptation boundaries

- The HTTP target, HTTPS core, UART worker and epoch logic are shared source.
- The newer HTTP parser dependency is pinned separately because retail 1.1.0
  predates target 6. JSON, string and stream helpers needed by target 6 are
  included and compiled with the rest of the retail firmware.
- The recorded HTTPD patch validates client response framing and chunk endings;
  the shared UCI HTTP/HTTPS client opts into strict response mode. The incoming
  management parser and retail CommoServe response mode keep their behavior.
- A read-only `/v1/machine:heap` diagnostic reports the retail newlib allocator's
  allocated bytes, arena size and reusable space/blocks. Unlike newer upstream,
  retail uses `heap_3`/newlib, so this does not report FreeRTOS free/minimum heap,
  total free RAM, ESP32 TLS allocations or task stack space.
- Retail `IndexedList::remove` retains its boolean return contract. JSON finds
  the removed index explicitly so old browser/drive users remain compatible.
- Retail CommoServe retains its service and behavior; its private body type and
  callbacks are isolated from the shared HTTP client's names.
- Only TLS hooks are added to the retail controller dispatcher and management
  Wi-Fi commands. Newer upstream power/wake features are not pulled in.
- The existing upstream `replace_root_state` repair is backported to the retail
  configuration browser. It frees the replaced initial state and updates both
  state pointers. Retail actions that allocate a page now use
  `OwnedConfigBrowser`, which destroys the state chain before the owned page.
  Ordinary configuration browsers still borrow their roots; static audio and
  general settings roots keep their existing caching behavior. The advanced
  owned root also deletes its store/group wrappers. These fixes address two
  reproduced ownership leaks; they do not account for every retained device
  allocation after menu or network activity.
- Retail context menus release their windows during hide/cleanup and direct
  destruction, preserving border restoration. Drawing without a window is safe.
  The host regression reproduces ten retained windows over ten root menu
  appearance/hide cycles, then checks the repair, repeated cleanup, nested
  teardown and borrowed persistent actions with memory sanitizers.
- Build identity says `C64U HTTPS TEST`, and records the baseline plus a digest
  of shared source inputs. It must not be presented as stock firmware.

The full updater replaces management/FPGA and can replace controller bootloader,
partition table/application, bundled flash files and configuration. Matching the
existing FPGA/partition contents does not make installation an application-only
operation. Preserve configuration and a reviewed recovery path first. Runtime
memory and sustained UART coexistence still need device tests; compilation and
host tests cannot establish those results. Focused HTTP/HTTPS hardware checks
have passed for the reviewed retail candidate; see `doc/https-device-validation.md`
for the exact package, observations and remaining limits.

## Hardware cleanup and memory checks

After installation, `soak.py` samples the management heap every ten rounds when
`/v1/machine:heap` is available. Compare the retail `allocated` field after
warmup, with identical measurement conditions; arena slack is not total free RAM
and steady samples do not establish ESP32 or stack safety. The separate
`lifecycle.py --host DEVICE --output NEW_DIRECTORY` checks native abort after
the first raw reply block, FREE_ALL with live object handles, and C64 reset with
live handles, with subsequent exact-byte recovery and heap samples. These tools
replace the C64 program/RAM; never run two native-UCI tests simultaneously.
The repetition runner records UTC timestamps for attempts, completed exchanges
and heap samples. An attempt without a matching completed exchange remains in
`attempts.jsonl`; `completed_without_exception` in `summary.json` distinguishes
an interrupted run from zero failed recorded exchanges. Preserve interrupted
outputs when repeating a run; a successful repeat does not explain an outage.

## Recipe regression tests

```sh
python3 -m unittest discover -s tools/c64u_https -v
```

Tests check preservation of retail board/FPGA/power source, rejection of source
drift and wrong baselines, non-overwriting preparation, forbidden instruction
sets, stale/duplicate embedded images, and an unreviewed recovery file. The
generated source is also compiled and its actual HTTP/HTTPS behavior is tested
by `build.py`; string checks alone are not the firmware validation.

The recipe tests run extracted production menu constructors/destructors under
ASan/UBSan, checking normal and nested states, repeated opening, virtual
destruction, borrowed roots, and balanced path/observer registrations. The
unchanged retail baseline must reproduce five orphan states in five cycles;
the prepared tree must retain none. UI/RTOS dependencies are substituted, so
this is not a complete menu rendering/cache or target allocator test.
`build.py` repeats the fixed ownership test against the prepared source.

The separate page-lifetime regression extracts the actual retail menu factories,
configuration-page constructors/destructors and uses the real `Browsable` and
`IndexedList` implementations. Synthetic stores provide three borrowed settings
per page. Old page/group actions retain four nodes per cycle; five cycles retain
20 nodes. The repaired actions retain zero. Nested advanced-page teardown and
repeated access to borrowed static audio/general settings roots are also checked
under ASan/UBSan. Rendering, real configuration callbacks and target allocator
byte accounting remain outside this host fixture.

## Hardware tests

Hardware suites are registered in `run-tests` under the command-interface
subsystem. See the [HTTP/HTTPS hardware test guide](../../tests/e2e/io/command_interface/https.md)
for prerequisites, selectors, fault fixtures, operator coordination and evidence.
The shared harness regression suite is `python3 tests/lib/https_harness_test.py`;
`python3 -m unittest discover -s tools/c64u_https -v` covers the retail build recipe.
