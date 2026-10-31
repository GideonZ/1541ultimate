# ESP32 WiFi bridge (raw_u64)

## Building

The bridge needs ESP-IDF 6 (CI uses v6.0.3); `CMakeLists.txt` stops an older one at configure.
`docker/Dockerfile` still installs ESP-IDF 5.3.1, so CI builds this project (and raw_c3 and u64ctrl) in
`espressif/idf:v6.0.3`, pinned by digest in `.github/workflows/build.yml`. Locally, from the top of the
repository, the same way as CI:

    docker run --rm --user $(id -u):$(id -g) -e HOME=/tmp -v "$PWD":/project -w /project/software/wifi/raw_u64 \
        espressif/idf:v6.0.3@sha256:54278f2c01e6e759502e2f04c9103589c17528923426f4d63193490c7a08f555 idf.py build

then `make u64_no_esp` packs `build/bridge.bin` into `update.u64`. `make esp32_raw_u64` (and so
`make u64`) works only where `IDF_PATH` is an ESP-IDF 6. Once `build/` exists here,
`make clean` also needs ESP-IDF 6; otherwise delete `software/wifi/raw_u64/build` by hand.

`software/io/uart/cmd_buffer.c` is compiled into all three ESP32 projects (raw_u64, raw_c3 and u64ctrl).

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

## The certificate bundle

The roots the bridge trusts in BUNDLE mode are the ones `software/u64ctrl/ca/cacert.pem` holds, the
Mozilla root list the control module's recovery trusts, not ESP-IDF's copy (v6.0.3 ships Mozilla's
data of 2025-12-02, with roots Mozilla has since dropped). There is one file for both modules:
`sdkconfig` selects `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE`, so none of ESP-IDF's roots are
merged in, and points `CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH` at `../../u64ctrl/ca/cacert.pem`.
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y` is set explicitly, and `../tls/tls_engine.c`
refuses to build without it. How the list is checked and refreshed is in `software/u64ctrl/ca/README.md`.

## The Wi-Fi scan (from the ESP-IDF example this project started as)

This example shows how to use the scan functionality of the Wi-Fi driver of ESP32.

Two scan methods are supported: fast scan and all channel scan.

* fast scan: in this mode, scan finishes right after a matching AP is detected, even if channels are not completely scanned. You can set thresholds for signal strength, as well as select desired authentication modes provided by the AP's. The Wi-Fi driver will ignore AP's that fail to meet mentioned criteria.

* all channel scan: scan will end only after all channels are scanned; the Wi-Fi driver will store 4 of the fully matching AP's. Sort methods for AP's include rssi and authmode. After the scan, the Wi-Fi driver selects the AP that fits best based on the sort.

After the scan, the Wi-Fi driver will try to connect. Because it needs to to allocate precious dynamic memory to store matching AP's, and, most of the cases, connect to the AP with the strongest reception, it does not need to record all the AP's matched. The number of matches stored is limited to 4 in order to limit dynamic memory usage. Among the 4 matches,  AP's are allowed to carry the same SSID name and all possible authentication modes - Open, WEP, WPA and WPA2.
