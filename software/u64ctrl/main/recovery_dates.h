/*
 * recovery_dates.h
 *
 * What the recovery does about certificate dates while the clock is unknown.
 * mbedTLS checks them (the build has MBEDTLS_HAVE_TIME_DATE for the TLS engine), but this module
 * has no battery clock: until SNTP has run, time() is near 1970 and every certificate looks "not yet
 * valid". A recovery must still reach its server then, as it did before dates were checked; the manifest
 * is signed, so what the server sends is checked in any case. Once the clock is valid, dates count.
 *
 * Free of ESP-IDF and mbedTLS, so software/test/recovery_dates tests what ships. recovery.c asserts that
 * the flag values below are mbedTLS's.
 */

#ifndef SOFTWARE_U64CTRL_MAIN_RECOVERY_DATES_H_
#define SOFTWARE_U64CTRL_MAIN_RECOVERY_DATES_H_

#include <stdbool.h>
#include <stdint.h>

#define RECOVERY_CLOCK_FLOOR 1704067200 // 2024-01-01: an earlier time() is a clock nobody has set

// mbedTLS x509 verification flags that are about time and nothing else
#define RECOVERY_X509_BADCERT_EXPIRED 0x01u
#define RECOVERY_X509_BADCRL_EXPIRED  0x20u
#define RECOVERY_X509_BADCERT_FUTURE  0x200u
#define RECOVERY_X509_BADCRL_FUTURE   0x400u
#define RECOVERY_X509_DATE_FLAGS (RECOVERY_X509_BADCERT_EXPIRED | RECOVERY_X509_BADCRL_EXPIRED | \
                                  RECOVERY_X509_BADCERT_FUTURE | RECOVERY_X509_BADCRL_FUTURE)

/* The clock can be trusted for certificate dates: SNTP has set it, or it reads at least the floor. A time() at or after
   the floor comes from SNTP, from the RTC time the S3 keeps over a soft restart (SNTP has not run again yet), or from
   the Ultimate's SNTP time that tls_set_clock() (software/wifi/tls/tls_engine.c) sets with settimeofday() at an OPEN. A
   wrong but plausible time is stricter than the unknown clock, which checks nothing: dates count, and a certificate that
   does not fit that time fails as it would on any machine with that clock. A time that is behind the real date can let
   an expired certificate pass; the manifest's signature is what the recovery relies on.
   The floor is 2024-01-01 here, a fixed date; the TLS engine's floor is the firmware's build time, which is static in
   tls_engine.c. The two are not shared: the engine's moves with every build and recovery has to work from the oldest
   image it may meet. */
static inline bool recovery_clock_valid(bool sntp_has_time, int64_t now)
{
    return sntp_has_time || now >= RECOVERY_CLOCK_FLOOR;
}

/* The verification flags of one certificate after the date policy: with a valid clock as they are; with an
   unknown clock the date flags are cleared and every other flag (untrusted, name, revoked, ...) is kept. */
static inline uint32_t recovery_date_policy(uint32_t flags, bool clock_valid)
{
    return clock_valid ? flags : (flags & ~RECOVERY_X509_DATE_FLAGS);
}

/* The verify callback of mbedTLS (the shape esp_crt_verify_callback has, with the certificate opaque). */
typedef int (*recovery_inner_verify_t)(void *ctx, void *crt, int depth, uint32_t *flags);

/* One certificate of the chain. The date policy goes BEFORE the bundle's callback: ESP-IDF's returns at once
   unless the flags are exactly "not trusted", so a date flag left on would skip its lookup of the issuer in the
   bundle. `*cleared` says whether the policy cleared a flag; the result is the callback's. */
static inline int recovery_verify(recovery_inner_verify_t inner, void *ctx, void *crt, int depth, uint32_t *flags,
                                  bool clock_valid, bool *cleared)
{
    uint32_t kept = recovery_date_policy(*flags, clock_valid);
    *cleared = (kept != *flags);
    *flags = kept;
    return inner(ctx, crt, depth, flags);
}

#endif /* SOFTWARE_U64CTRL_MAIN_RECOVERY_DATES_H_ */
