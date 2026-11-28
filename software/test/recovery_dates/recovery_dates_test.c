/*
 * Host side tests for the certificate date policy of the control module's recovery.
 *
 *   make -C target/pc/linux/recoverydates test
 *
 * The real software/u64ctrl/main/recovery_dates.h is compiled here.
 */
#include <stdio.h>
#include "recovery_dates.h"

static int failures;
static int checks;

static void check(int condition, const char *what)
{
    checks++;
    if (!condition) {
        failures++;
        printf("  FAIL  %s\n", what);
    } else {
        printf("  ok    %s\n", what);
    }
}

// mbedTLS: BADCERT_EXPIRED 0x01, REVOKED 0x02, CN_MISMATCH 0x04, NOT_TRUSTED 0x08, BADCRL_NOT_TRUSTED 0x10
#define OTHER_FLAGS 0x1Eu
#define NOT_TRUSTED 0x08u
#define BAD_MD 0x4000u                // MBEDTLS_X509_BADCERT_BAD_MD
#define VERIFY_FAILED (-0x2700)       // MBEDTLS_ERR_X509_CERT_VERIFY_FAILED

static int inner_calls;
static bool bundle_has_issuer = true;
static bool signature_good = true;

// esp_crt_verify_callback (ESP-IDF 6.0.3 and 6.1.1, esp_crt_bundle.c): a weak hash is ignored when comparing, anything other
// than exactly "not trusted" comes back unchanged with 0; then the issuer is looked up in the bundle: found and
// signed, the flags go; found but badly signed, or not found, it is an error and the flags stay
static int fake_bundle(void *ctx, void *crt, int depth, uint32_t *flags)
{
    (void)ctx; (void)crt; (void)depth;
    inner_calls++;
    if ((*flags & ~BAD_MD) != NOT_TRUSTED) {
        return 0;
    }
    if (bundle_has_issuer && signature_good) {
        *flags = 0;
        return 0;
    }
    return VERIFY_FAILED;
}

int main(void)
{
    const int64_t good = RECOVERY_CLOCK_FLOOR + 1000;

    printf("The clock\n");
    check(!recovery_clock_valid(false, 0), "SNTP has not run and time() is 1970: not valid");
    check(!recovery_clock_valid(false, RECOVERY_CLOCK_FLOOR - 1), "one second before 2024-01-01, no SNTP: not valid");
    check(recovery_clock_valid(false, RECOVERY_CLOCK_FLOOR), "no SNTP but time() at 2024-01-01 (RTC kept over a soft restart): valid");
    check(recovery_clock_valid(false, good), "no SNTP but time() later (soft restart): valid");
    check(recovery_clock_valid(true, 0), "SNTP has set the clock: valid");
    check(recovery_clock_valid(true, good), "SNTP and a late time(): valid");

    printf("Unknown clock: only the date flags go\n");
    check(recovery_date_policy(RECOVERY_X509_BADCERT_FUTURE, false) == 0, "not yet valid is cleared");
    check(recovery_date_policy(RECOVERY_X509_BADCERT_EXPIRED, false) == 0, "expired is cleared");
    check(recovery_date_policy(RECOVERY_X509_BADCRL_FUTURE | RECOVERY_X509_BADCRL_EXPIRED, false) == 0,
          "the CRL date flags are cleared");
    check(recovery_date_policy(RECOVERY_X509_DATE_FLAGS | OTHER_FLAGS, false) == OTHER_FLAGS,
          "untrusted, revoked, name mismatch and CRL-untrusted stay");
    check(recovery_date_policy(OTHER_FLAGS, false) == OTHER_FLAGS, "no date flag: unchanged");
    check(recovery_date_policy(0, false) == 0, "no flags: none");
    check(recovery_date_policy(0x8000u | RECOVERY_X509_BADCERT_FUTURE, false) == 0x8000u, "flags outside the set stay");

    printf("Valid clock: dates count\n");
    check(recovery_date_policy(RECOVERY_X509_BADCERT_EXPIRED, true) == RECOVERY_X509_BADCERT_EXPIRED, "expired stays");
    check(recovery_date_policy(RECOVERY_X509_BADCERT_FUTURE, true) == RECOVERY_X509_BADCERT_FUTURE, "not yet valid stays");
    check(recovery_date_policy(RECOVERY_X509_DATE_FLAGS | OTHER_FLAGS, true) == (RECOVERY_X509_DATE_FLAGS | OTHER_FLAGS),
          "all flags stay");

    printf("The callback order (a stand-in for esp_crt_verify_callback)\n");
    // ESP-IDF's: only a certificate whose flags are exactly NOT_TRUSTED gets its issuer looked up in the bundle
    // (found: flags cleared); any other flags come back unchanged, with a 0 result.
    {
        uint32_t f = RECOVERY_X509_BADCERT_FUTURE | NOT_TRUSTED;
        bool cleared = false;
        inner_calls = 0;
        int r = recovery_verify(fake_bundle, NULL, NULL, 1, &f, false, &cleared);
        check(r == 0 && f == 0, "unknown clock, 1970: not trusted + not yet valid ends with no flags (bundle lookup reached)");
        check(cleared && inner_calls == 1, "... the policy reports it cleared a date flag; the inner callback ran once");
        f = RECOVERY_X509_BADCERT_FUTURE | NOT_TRUSTED;
        r = recovery_verify(fake_bundle, NULL, NULL, 1, &f, true, &cleared);
        check(r == 0 && f == (RECOVERY_X509_BADCERT_FUTURE | NOT_TRUSTED) && !cleared,
              "valid clock: the flags reach the inner callback as they are, and it leaves them");
        f = NOT_TRUSTED;
        r = recovery_verify(fake_bundle, NULL, NULL, 1, &f, false, &cleared);
        check(r == 0 && f == 0 && !cleared, "no date flag: the bundle lookup runs, nothing cleared");
        f = RECOVERY_X509_BADCERT_EXPIRED | OTHER_FLAGS;
        r = recovery_verify(fake_bundle, NULL, NULL, 0, &f, false, &cleared);
        check(r == 0 && f == OTHER_FLAGS, "unknown clock: the other flags survive the whole callback");

        // The callback's own result and what it leaves are the caller's, whatever the policy cleared
        bundle_has_issuer = false;
        f = RECOVERY_X509_BADCERT_FUTURE | NOT_TRUSTED;
        r = recovery_verify(fake_bundle, NULL, NULL, 1, &f, false, &cleared);
        check(r == VERIFY_FAILED, "unknown clock, issuer not in the bundle: the callback's error is returned");
        check(f == NOT_TRUSTED, "... and the trust flag is still on; only the date flag went");
        bundle_has_issuer = true;
        signature_good = false;
        f = RECOVERY_X509_BADCERT_FUTURE | NOT_TRUSTED;
        r = recovery_verify(fake_bundle, NULL, NULL, 1, &f, false, &cleared);
        check(r == VERIFY_FAILED && f == NOT_TRUSTED, "unknown clock, bad signature: the error is returned, trust flag kept");
        f = NOT_TRUSTED;
        r = recovery_verify(fake_bundle, NULL, NULL, 1, &f, true, &cleared);
        check(r == VERIFY_FAILED, "valid clock, bad signature: the error is returned");
        signature_good = true;
        f = RECOVERY_X509_BADCERT_FUTURE | NOT_TRUSTED | BAD_MD;
        r = recovery_verify(fake_bundle, NULL, NULL, 1, &f, false, &cleared);
        check(r == 0 && f == 0, "unknown clock, weak hash too: the bundle lookup still runs (BAD_MD is filtered) and clears all");
        f = RECOVERY_X509_BADCERT_EXPIRED | NOT_TRUSTED | BAD_MD;
        r = recovery_verify(fake_bundle, NULL, NULL, 1, &f, true, &cleared);
        check(r == 0 && f == (RECOVERY_X509_BADCERT_EXPIRED | NOT_TRUSTED | BAD_MD),
              "valid clock, expired + weak hash: not looked up, flags unchanged, so the handshake fails on the date");
    }

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
