# ESP32 WiFi bridge (raw_u64)

## Building

The bridge needs ESP-IDF 6 (CI uses v6.0.3); `CMakeLists.txt` stops an older one at configure.
raw_c3 and u64ctrl still build with ESP-IDF 5.3.1, as in `docker/Dockerfile`, so CI builds this
project in `espressif/idf:v6.0.3`, pinned by digest in `.github/workflows/build.yml`. Locally, from
the top of the repository, the same way as CI:

    docker run --rm --user $(id -u):$(id -g) -e HOME=/tmp -v "$PWD":/project -w /project/software/wifi/raw_u64 \
        espressif/idf:v6.0.3@sha256:54278f2c01e6e759502e2f04c9103589c17528923426f4d63193490c7a08f555 idf.py build

then `make u64_no_esp` packs `build/bridge.bin` into `update.u64`. `make esp32_raw_u64` (and so
`make u64` and `make all`) works only where `IDF_PATH` is an ESP-IDF 6. Once `build/` exists here,
`make clean` also needs ESP-IDF 6; otherwise delete `software/wifi/raw_u64/build` by hand.

`software/io/uart/cmd_buffer.c` is compiled into all three ESP32 projects, so a change to it has to
build on both ESP-IDF 5.3.1 and 6.

`sdkconfig` marks values that follow ESP-IDF's defaults with `# default:`; a reconfigure on a newer
ESP-IDF takes its new defaults there. After any ESP-IDF update, diff the values, not just the build.

mbedTLS is ESP-IDF's own, as ESP-IDF imports it: the build carries no patch to it. Known issue:
stock mbedTLS refuses a cookie in a TLS 1.3 HelloRetryRequest (Mbed-TLS/mbedtls#9543), so a
server that sends one, as OpenJDK's JSSE does, fails the handshake.

## Testing the TLS engine

The engine (`../tls/tls_engine.c`, `../tls/tls_rpc.c`; `software/wifi/tls` is shared by module firmware, and holds the RPC layout, `rpc_tls.h`) is checked from the C64's side by the E2E
suite `tests/e2e/io/command_interface/net_tls_test.py` (against a device) and `net_tls_selftest.py`
(against a simulated one), and the Ultimate's side of the module calls by
`make -C target/pc/linux/wifitls test`. The engine can also be run on its own, on QEMU or an
ESP32 dev board, with a red/green gate: <https://github.com/JC-000/1541u-tls-standalone/tree/0e2a7c4752516acd3971601d476c198bc987d4d7>.

## The certificate bundle: the Mozilla root store, vendored

The roots the bridge trusts in BUNDLE mode come from `software/wifi/ca_store/cacert.pem`, not
from ESP-IDF's copy. ESP-IDF refreshes its own `cacrt_all.pem` on its release branches only now
and then: v6.0.3 ships Mozilla's data of 2025-12-02, with roots Mozilla has since dropped.

- **Where it comes from.** `cacert.pem` is curl's extract of Mozilla's `certdata.txt`
  (<https://curl.se/ca/cacert.pem>), byte for byte. `CA_PINS` beside it records the source URL,
  the Mozilla date, the file's sha256 (`CA_SHA256`), the `certdata.txt` commit on GitHub it was
  regenerated from as a cross-check (`CA_CERTDATA_REF`), the sha256 of its set of certificates
  (`CA_BUNDLE_SET_SHA256`) and the age limit (`CA_MAX_AGE_DAYS`).
- **How the build uses it.** `sdkconfig` selects `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE`,
  so none of ESP-IDF's roots are merged in, and points `CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH`
  at `../ca_store/cacert.pem`; ESP-IDF's `gen_crt_bundle.py` builds the bundle from that file and
  builds it again whenever the file changes.
- **How the build verifies it.** `CMakeLists.txt` (through `../ca_store/ca_store.cmake`) runs `python3 tools/ca_store/ca_store.py check`
  at every configure (an edit to `cacert.pem`, `CA_PINS` or the checker configures again) and
  again at every build, before the bundle is made, so an already-configured tree cannot skip it.
  It checks the configuration the build really uses (its `config/sdkconfig.json`, which
  `idf.py -D SDKCONFIG=...` can point away from this directory's `sdkconfig`). The build fails
  when `cacert.pem` does not match `CA_SHA256`, when its date disagrees with the pins, when its
  certificates changed without `CA_BUNDLE_SET_SHA256`, when another certificate file (such as an
  `anchors.pem`) is in the store, or when the configuration no longer builds the bundle from the
  store alone with `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y`. A store older than
  `CA_MAX_AGE_DAYS` is a warning, and an error when the build runs with `RELEASE=1` in its
  environment (`RELEASE=1 idf.py build`, `make RELEASE=1 esp32_raw_u64`). Run the check by hand
  with `python3 tools/ca_store/ca_store.py check`, and its tests with `make ca_store_test`.
- **How to refresh it.** `python3 tools/ca_store/refresh.py --ident-minor N` (needs the network
  and Perl) fetches curl's file and its sum, and takes it only when curl's own `mk-ca-bundle.pl`,
  pinned by commit and sha256, gives the same file from the newest `certdata.txt` on Firefox's
  release branch, which must descend from `CA_CERTDATA_REF`. It writes `cacert.pem` and `CA_PINS`.
  When the certificates changed it also sets `IDENT_MINOR` (and `IDENT_STRING`) in
  `main/rpc_dispatch.h` to N (of every project whose own sources or `ca_store.SHARED_SOURCES` attach the
  bundle; with more than one, `--ident-minor PROJECT=N` for each), since the updater reflashes a module only for a new version. N must
  be allocated for this image and above the current minor: the tool refuses a certificate change
  without `--ident-minor`, because on a development line the next number may already belong to
  another image. `--summary FILE` lists what changed by fingerprint, for review. Commit the
  result. Never edit `cacert.pem` by hand: the build refuses it. Moving the pins by hand without
  a new `IDENT_MINOR` is not caught by the check, so review that the two move together.
- **Roots with serial number zero.** Six roots in Mozilla's set as of 2026-09-25 (Go Daddy Root
  CA - G2, Starfield Root CA - G2, Starfield Services Root CA - G2, Security Communication
  RootCA2 and the two HARICA 2015 roots) have serial number 0, which the `cryptography` package
  (48.0) warns is not positive. ESP-IDF's `gen_crt_bundle.py` silences that warning
  today; if a future `cryptography` refuses such certificates, the bundle step fails and the build
  stops (it fails closed, as it would with ESP-IDF's own bundle, which holds the same roots).
- **No anchors.** ESP-IDF 6 verifies cross-signed chains itself
  (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY`), so the store needs no hand-kept
  roots: `sdkconfig` sets it explicitly, `../tls/tls_engine.c` refuses to build without it, and the
  check refuses an anchor file. Only raw_u64 uses the store; raw_c3 and u64ctrl keep ESP-IDF's
  bundle.

## The Wi-Fi scan (from the ESP-IDF example this project started as)

This example shows how to use the scan functionality of the Wi-Fi driver of ESP32.

Two scan methods are supported: fast scan and all channel scan.

* fast scan: in this mode, scan finishes right after a matching AP is detected, even if channels are not completely scanned. You can set thresholds for signal strength, as well as select desired authentication modes provided by the AP's. The Wi-Fi driver will ignore AP's that fail to meet mentioned criteria.

* all channel scan: scan will end only after all channels are scanned; the Wi-Fi driver will store 4 of the fully matching AP's. Sort methods for AP's include rssi and authmode. After the scan, the Wi-Fi driver selects the AP that fits best based on the sort.

After the scan, the Wi-Fi driver will try to connect. Because it needs to to allocate precious dynamic memory to store matching AP's, and, most of the cases, connect to the AP with the strongest reception, it does not need to record all the AP's matched. The number of matches stored is limited to 4 in order to limit dynamic memory usage. Among the 4 matches,  AP's are allowed to carry the same SSID name and all possible authentication modes - Open, WEP, WPA and WPA2.
