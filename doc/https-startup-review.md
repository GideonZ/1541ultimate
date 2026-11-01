# ESP32 startup and version review

The boot identity is now sampled after `esp_wifi_start()` succeeds and before
the connector and command dispatcher start. The HTTP command interface is
unchanged. Wi-Fi association is not required for this initialization.

Previously, `app_main()` called `bootloader_random_enable()` and
`bootloader_random_disable()` before ADC and Wi-Fi initialization, including in
the default build. In ESP-IDF 5.3.1 the ESP32-S3 disable implementation asserts
the SARADC digital reset and changes the SAR power setting. Those operations
bypass the ADC/PHY driver's shared power accounting. The application no longer
uses this temporary SAR entropy source; successful RF startup supplies entropy
for `esp_fill_random()` instead.

The identity is generated once per controller boot. A failed Wi-Fi start does
not generate it, a zero identity remains unavailable, and later Wi-Fi disable
and enable cycles retain it. The existing TLS entropy policy still becomes
unavailable when Wi-Fi stops. These rules preserve restart fencing without
adding a second ADC initialization sequence.

## Version selection

The default module reports **1.22** and the provider-enabled module reports
**1.23**. Both are newer than the previous default 1.21. This matters because
the ordinary C64U/U64-II updater installs a controller image only when its numeric version
is newer; equal versions are skipped. TLS capability still requires explicit
negotiation and cannot be inferred from a version number.

These numbers are relative to the reviewed source base. Reconcile them with
other pending controller changes, including
[the IDF 6 migration](https://github.com/GideonZ/1541ultimate/pull/1028), before
merging or building an installation package. Previously built 1.22 provider
packages do not contain this correction and must not be relabeled or reused.

## Local validation

Four startup regressions compile the production boot implementation, the Wi-Fi
start/stop wrappers and both version configurations under ASan/UBSan. They check
RF-start failure, successful initialization, zero-identity rejection, repeated
starts, TLS entropy revocation, ADC-state preservation and startup ordering.
All four fail against the previous source and pass with the correction.
Three deliberately reintroduced defects are also rejected: SAR manipulation,
identity generation after a failed RF start, and identity regeneration.

Default and provider-enabled modules build on ESP-IDF 5.3.1 and 5.3.6. Saved
binary labels, provider configuration and artifact hashes are checked. The
compiled boot-identity object references `esp_fill_random` and neither
`bootloader_random_enable` nor `bootloader_random_disable`.

The complete serial host run passed 82 stages, 18,865 counted assertions,
37 parser tests and 184 TLS cases across both certificate profiles. The four
new startup tests are reported separately from the assertion total. The
independent raw-log/peer audit passed and rejected all seven altered or missing
evidence examples.

An earlier run overlapped module builds and failed a connected ECDSA
`command-read-cancel` peer check: its fixed 150 ms cancellation timer fired
before authentication completed, so the primary peer received no HTTP request.
The failure and peer log are retained. The unchanged suite passed when run
serially; that does not establish that the fixture is immune to scheduling
delays. No production deadline or evidence acceptance condition was relaxed.

This establishes the software ordering and removes the identified peripheral
side effects. It does not measure physical ADC voltages, RF entropy quality or
Wi-Fi startup on a C64U. Those checks remain part of the controlled hardware
plan after recovery qualification and installation authorization.

## IDF 6 compatibility

The new boot implementation compiles against the ESP-IDF 6.0.3 `esp_random.h`
API. The complete firmware is **not yet IDF 6 compatible**. Compiler probes
against its pinned Mbed TLS revision
`ce3f3485a121c100f58f36d700cb35b060f6e866` confirm these blockers:

- The pinned TLS core includes the removed `mbedtls/ctr_drbg.h` and calls
  `mbedtls_ssl_conf_rng`, which is no longer declared.
- Recovery signature verification includes the removed `mbedtls/sha256.h` and
  calls the removed `mbedtls_pk_get_type`. Header probes accept the replacement
  PSA hash and `mbedtls_pk_get_key_type` APIs; the recovery port and runtime
  validation still remain.
- The UART integration also needs the IDF 6 header/type migration described
  in the pending SDK update; the existing source still uses `soc/uart_periph.h`.

Use the supported PSA initialization, hash and RNG interfaces when porting.
Preserve the trust, clock and RF-entropy gates, authentication failures,
cancellation, deadlines and cleanup tests. A successful header probe is not a
full IDF 6 build or runtime qualification. No production SDK or TLS dependency
pin was changed in this review.

References: [ESP32-S3 entropy requirements](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/system/random.html),
[IDF 5.3.1 SAR implementation](https://github.com/espressif/esp-idf/blob/v5.3.1/components/bootloader_support/src/bootloader_random_esp32s3.c),
[IDF 6 security migration](https://github.com/espressif/esp-idf/blob/v6.0.3/docs/en/migration-guides/release-6.x/6.0/security.rst).
