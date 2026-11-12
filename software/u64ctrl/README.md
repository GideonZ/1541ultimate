# Control module firmware (u64ctrl)

The ESP32-S3 module of the Ultimate 64 Elite II and the C64 Ultimate: WiFi, power and button control.

## Building

The module needs ESP-IDF 6 (CI uses v6.0.3); `CMakeLists.txt` stops an older one at configure.
raw_c3 still builds with ESP-IDF 5.3.1, as in `docker/Dockerfile`, so CI builds this
project (and raw_u64) in `espressif/idf:v6.0.3`, pinned by digest in `.github/workflows/build.yml`. Locally, from
the top of the repository, the same way as CI:

    docker run --rm --user $(id -u):$(id -g) -e HOME=/tmp -v "$PWD":/project -w /project/software/u64ctrl \
        espressif/idf:v6.0.3@sha256:54278f2c01e6e759502e2f04c9103589c17528923426f4d63193490c7a08f555 idf.py build

then `make u64ii_no_esp` packs `build/u64ctrl.bin` into the update files. `make esp32_u64ctrl` and
`make u64ii` work only where `IDF_PATH` is an ESP-IDF 6, and `make` (all) cannot complete with one
`IDF_PATH`, because raw_c3 needs 5.3.1. Once `build/` exists here,
`make clean` also needs ESP-IDF 6; otherwise delete `software/u64ctrl/build` by hand.

`software/io/uart/cmd_buffer.c` is compiled into all three ESP32 projects, so a change to it has to
build on both ESP-IDF 5.3.1 and 6.

`sdkconfig` marks values that follow ESP-IDF's defaults with `# default:`; a reconfigure on a newer
ESP-IDF takes its new defaults there. After any ESP-IDF update, diff the values, not just the build.
