# ESP32-C3 WiFi bridge (raw_c3)

The WiFi module of the U2+L.

## Building

The bridge needs ESP-IDF 6 (CI uses v6.0.3); `CMakeLists.txt` stops an older one at configure.
`docker/Dockerfile` still installs ESP-IDF 5.3.1, so CI builds this project (and raw_u64 and u64ctrl) in
`espressif/idf:v6.0.3`, pinned by digest in `.github/workflows/build.yml`. Locally, from the top of the
repository, the same way as CI:

    docker run --rm --user $(id -u):$(id -g) -e HOME=/tmp -v "$PWD":/project -w /project/software/wifi/raw_c3 \
        espressif/idf:v6.0.3@sha256:54278f2c01e6e759502e2f04c9103589c17528923426f4d63193490c7a08f555 idf.py build

then `make u2pl_no_esp` packs `build/bridge.bin` into the update file. `make esp32_raw_c3` (and so
`make u2pl`) works only where `IDF_PATH` is an ESP-IDF 6. Once `build/` exists here, `make clean` also
needs ESP-IDF 6; otherwise delete `software/wifi/raw_c3/build` by hand.

`sdkconfig` marks values that follow ESP-IDF's defaults with `# default:`; a reconfigure on a newer
ESP-IDF takes its new defaults there. After any ESP-IDF update, diff the values, not just the build.


This example shows how to use the scan functionality of the Wi-Fi driver of ESP32.

Two scan methods are supported: fast scan and all channel scan.

* fast scan: in this mode, scan finishes right after a matching AP is detected, even if channels are not completely scanned. You can set thresholds for signal strength, as well as select desired authentication modes provided by the AP's. The Wi-Fi driver will ignore AP's that fail to meet mentioned criteria.

* all channel scan: scan will end only after all channels are scanned; the Wi-Fi driver will store 4 of the fully matching AP's. Sort methods for AP's include rssi and authmode. After the scan, the Wi-Fi driver selects the AP that fits best based on the sort.

After the scan, the Wi-Fi driver will try to connect. Because it needs to to allocate precious dynamic memory to store matching AP's, and, most of the cases, connect to the AP with the strongest reception, it does not need to record all the AP's matched. The number of matches stored is limited to 4 in order to limit dynamic memory usage. Among the 4 matches,  AP's are allowed to carry the same SSID name and all possible authentication modes - Open, WEP, WPA and WPA2.
