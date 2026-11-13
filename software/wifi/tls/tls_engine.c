/* tls_engine.c: TLS sessions on the UCI network target. mbedTLS runs a TLS 1.3 client with a hardened TLS 1.2 fallback;
 * the Ultimate pumps the ciphertext between its TCP socket and CMD_TLS_STEP. The bio callbacks never block (per-session
 * input and output buffers). Everything here runs on the TLS task only. */

// Reads a few private mbedtls_ssl_context fields that have no getter (handshake state and version, the message just
// processed). It includes nothing from mbedTLS's library/ (ssl_misc.h): a fork with another
// mbedtls_ssl_handshake_params layout would otherwise be read at the wrong offsets.
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "cmd_buffer.h"
#include "wifi_modem.h"
#include "sntp.h"
#include "tls_engine.h"

#if !CONFIG_MBEDTLS_SSL_PROTO_TLS1_3 || !CONFIG_MBEDTLS_SSL_PROTO_TLS1_2
#error "The engine offers TLS 1.3 with a TLS 1.2 fallback: enable CONFIG_MBEDTLS_SSL_PROTO_TLS1_3 and _TLS1_2"
#endif
#if MBEDTLS_VERSION_MAJOR < 4
#error "This engine is written for mbedTLS 4 (ESP-IDF 6): randomness and allocation failures come from PSA"
#endif
#if !CONFIG_MBEDTLS_HAVE_TIME_DATE
#error "Without CONFIG_MBEDTLS_HAVE_TIME_DATE certificate dates are ignored: an expired certificate would verify"
#endif
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE && !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY
#error "The bundle needs CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY: without it most servers' cross-signed chains fail"
#endif
#if !defined(MBEDTLS_SSL_EXTENDED_MASTER_SECRET)
#error "TLS 1.2 needs the extended master secret, which this mbedTLS configuration leaves out"
#endif
#if defined(MBEDTLS_SSL_RENEGOTIATION)
#error "Renegotiation must stay off (CONFIG_MBEDTLS_SSL_RENEGOTIATION)"
#endif

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
// esp_crt_bundle.c's verify callback: external linkage but not in its header (as IDF's own unit test declares it)
int esp_crt_verify_callback(void *buf, mbedtls_x509_crt *crt, int depth, uint32_t *flags);
#endif

#ifndef TLS_IN_BUF_SIZE // a host test shrinks it to reach a full input buffer while handshaking
#define TLS_IN_BUF_SIZE  (2 * TLS_STEP_IN_MAX) // unread ciphertext from the Ultimate
#endif
#define TLS_OUT_BUF_SIZE 2048                  // ciphertext not yet collected by a step
// Step room kept free of plaintext for an alert that a failing read writes: header 5 + tag 16 +
// alert padded to at most 32 (CONFIG_MBEDTLS_SSL_CID_PADDING_GRANULARITY range 0..32) = 53
#define TLS_ALERT_ROOM   64
// NewSessionTickets one mbedtls_ssl_write() may run into; each is taken on its own, so this only bounds a loop
#define TLS_TICKET_RETRIES 4

_Static_assert(TLS_OUT_BUF_SIZE > TLS_STEP_IN_MAX, "one step of plaintext must fit the output buffer");

// The CUSTOM CA. A session holds its own reference, so replacing or clearing the CA never frees it under a
// session that is still verifying with it.
typedef struct {
    mbedtls_x509_crt crt;
    uint16_t tag;
    int refs;
} tls_ca_t;

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    tls_ca_t *ca;              // CUSTOM
    uint16_t tag;
    uint8_t trust;
    uint8_t opts;
    uint8_t pin[TLS_PIN_LEN];
    uint8_t pin_mismatch;
    uint8_t bundle_rejected;
    uint8_t ems_checked;
    tls_session_stats_t stats;
    int in_off;
    int in_len;
    int out_len;
    uint8_t in_buf[TLS_IN_BUF_SIZE];
    uint8_t out_buf[TLS_OUT_BUF_SIZE];
} tls_session_t;

static const char *TAG = "tls";
static tls_session_t *sessions[TLS_MAX_SESSIONS];
static tls_ca_t *custom_ca;
// PIN: mbedTLS verifies only with a CA chain configured (and a TLS 1.3 client needs one), so an empty one.
// Zeroed is the state mbedtls_x509_crt_init() leaves; verification only reads it.
static mbedtls_x509_crt tls_empty_ca;
static int psa_ready;
static int clock_set_by_nios;

// The six hardened TLS 1.2 suites and the three TLS 1.3 ones, in mbedTLS's own order of preference
static const int tls_ciphersuites[] = {
    MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS1_3_AES_256_GCM_SHA384,
    MBEDTLS_TLS1_3_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    0
};

// Key-exchange groups for both versions: mbedTLS 4's default list adds x448 and FFDHE
// 2048-8192 (an 8192-bit modexp on the ESP32). mbedTLS keeps the pointer, so static.
static const uint16_t tls_groups[] = {
    MBEDTLS_SSL_IANA_TLS_GROUP_X25519, MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1,
    MBEDTLS_SSL_IANA_TLS_GROUP_SECP384R1, MBEDTLS_SSL_IANA_TLS_GROUP_SECP521R1,
    MBEDTLS_SSL_IANA_TLS_GROUP_NONE
};

static int tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    tls_session_t *s = (tls_session_t *)ctx;
    size_t room = TLS_OUT_BUF_SIZE - s->out_len;
    if (room == 0) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    if (len > room) {
        len = room;
    }
    memcpy(s->out_buf + s->out_len, buf, len);
    s->out_len += len;
    return (int)len;
}

static int tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    tls_session_t *s = (tls_session_t *)ctx;
    size_t avail = s->in_len - s->in_off;
    if (avail == 0) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    if (len > avail) {
        len = avail;
    }
    memcpy(buf, s->in_buf + s->in_off, len);
    s->in_off += len;
    return (int)len;
}

// Every session's verify callback, set on the ssl context (mbedtls_ssl_set_verify) so it overrides the config's and
// esp_crt_bundle_attach() cannot replace it. It returns 0 so the verify result stays decodable; a rejection still
// fails the handshake because it leaves flags set.
static int tls_verify(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    tls_session_t *s = (tls_session_t *)ctx;
    int hashed = 0;
    if (depth == 0) {
        size_t n = 0;
        hashed = psa_hash_compute(PSA_ALG_SHA_256, crt->pk_raw.p, crt->pk_raw.len, s->stats.spki_sha256,
                                  sizeof(s->stats.spki_sha256), &n) == PSA_SUCCESS;
        if (!hashed) {
            memset(s->stats.spki_sha256, 0, sizeof(s->stats.spki_sha256));
        }
    }
    switch (s->trust) {
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    case TLS_TRUST_BUNDLE:
        if (esp_crt_verify_callback(NULL, crt, depth, flags) != 0) {
            *flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
            s->bundle_rejected = 1;
        }
        break;
#endif
    case TLS_TRUST_PIN:
        // The key is the identity: chain, dates and name are not checked. The mode comes from trust alone.
        *flags = 0;
        if ((depth == 0) && (!hashed || memcmp(s->stats.spki_sha256, s->pin, TLS_PIN_LEN) != 0)) {
            s->pin_mismatch = 1;
            *flags = MBEDTLS_X509_BADCERT_NOT_TRUSTED;
        }
        break;
    default: // CUSTOM: mbedTLS's own chain verification against the uploaded CA
        break;
    }
    return 0;
}

// 00:00 UTC on the day this file was compiled: a clock before it cannot be right
static time_t tls_build_time(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *date = __DATE__; // "Mmm dd yyyy"
    int m = 1;
    while ((m < 12) && strncmp(months + 3 * (m - 1), date, 3)) {
        m++;
    }
    int d = atoi(date + 4);
    int y = atoi(date + 7) - (m <= 2);
    // Days since 1970-01-01 in the proleptic Gregorian calendar (days_from_civil)
    int era = y / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + ((m > 2) ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (time_t)(era * 146097 + doe - 719468) * 86400;
}

// The clock an OPEN may use. SNTP wins: the Ultimate's time is only taken when the ESP32 has no SNTP time,
// and only when the Ultimate says it is SNTP time too (time_source). Reports the source used for this OPEN;
// a clock an earlier OPEN set does not count.
static uint8_t tls_set_clock(uint8_t time_source, uint32_t unix_time)
{
    time_t floor = tls_build_time();
    if (sntp_has_time() && (time(NULL) > floor)) {
        return TLS_TIME_SNTP;
    }
    if ((time_source == TLS_TIME_NIOS) && ((time_t)unix_time > floor)) {
        struct timeval tv = { .tv_sec = (time_t)unix_time, .tv_usec = 0 };
        if (settimeofday(&tv, NULL) == 0) {
            clock_set_by_nios = 1;
            return TLS_TIME_NIOS;
        }
    }
    return TLS_TIME_NONE;
}

// ESP-IDF 6 initialises PSA at startup, so this is a no-op there; it stays for builds that do not. Neither
// the init nor a certificate parse draws random bytes.
static int tls_psa_ready(void)
{
    if (!psa_ready) {
        psa_status_t status = psa_crypto_init();
        if (status != PSA_SUCCESS) {
            ESP_LOGE(TAG, "psa_crypto_init: %d", (int)status);
            return 0;
        }
        psa_ready = 1;
    }
    return 1;
}

static tls_session_t *tls_find(uint8_t slot, uint16_t tag)
{
    if (slot >= TLS_MAX_SESSIONS) {
        return NULL;
    }
    tls_session_t *s = sessions[slot];
    return (s && (s->tag == tag)) ? s : NULL;
}

static void tls_ca_release(tls_ca_t *ca)
{
    if (ca && (--ca->refs == 0)) {
        mbedtls_x509_crt_free(&ca->crt);
        free(ca);
    }
}

static void tls_free(tls_session_t *s)
{
    for (int i = 0; i < TLS_MAX_SESSIONS; i++) {
        if (sessions[i] == s) {
            sessions[i] = NULL;
        }
    }
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->conf);
    tls_ca_release(s->ca);
    // The certificate bundle is shared: esp_crt_bundle_detach() would free it under the other session
    free(s);
}

int tls_sessions_open(void)
{
    int n = 0;
    for (int i = 0; i < TLS_MAX_SESSIONS; i++) {
        if (sessions[i]) {
            n++;
        }
    }
    return n;
}

// Builds a complete session without putting it in the table; frees it on failure
static int tls_setup(tls_session_t **out, uint16_t tag, const tls_open_params_t *p, const char *hostname,
                     uint8_t time_source)
{
    *out = NULL;
    tls_session_t *s = calloc(1, sizeof(tls_session_t));
    if (!s) {
        return ESP_ERR_NO_MEM;
    }
    s->tag = tag;
    s->trust = p->trust;
    s->opts = p->opts;
    if (p->trust == TLS_TRUST_PIN) {
        memcpy(s->pin, p->pin, TLS_PIN_LEN);
    }
    s->stats.state = TLS_STATE_HANDSHAKING;
    s->stats.time_source = time_source;
    s->stats.esp_time = (time_source == TLS_TIME_NONE) ? 0 : (uint32_t)time(NULL);
    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);

    // Every random byte (client random, key shares) comes from PSA: see tls_open()
    int ret = mbedtls_ssl_config_defaults(&s->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret == 0) {
        // TLS 1.3, and the hardened TLS 1.2 unless the program requires 1.3
        mbedtls_ssl_conf_min_tls_version(&s->conf, (p->opts & TLS_OPT_REQUIRE_TLS13) ? MBEDTLS_SSL_VERSION_TLS1_3
                                                                                     : MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_max_tls_version(&s->conf, MBEDTLS_SSL_VERSION_TLS1_3);
        // The CBC suites are compiled in (wpa_supplicant needs AES-CBC): only this list keeps them out
        mbedtls_ssl_conf_ciphersuites(&s->conf, tls_ciphersuites);
        mbedtls_ssl_conf_groups(&s->conf, tls_groups);
        // Offered here, and required after the ServerHello (tls_note_handshake): mbedTLS has no switch for that
        mbedtls_ssl_conf_extended_master_secret(&s->conf, MBEDTLS_SSL_EXTENDED_MS_ENABLED);
        // No session_ticket extension in TLS 1.2 (TLS 1.3 tickets are taken and dropped as before)
        mbedtls_ssl_conf_session_tickets(&s->conf, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
        mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        // One config per session: a CUSTOM or PIN config never has the bundle attached, since a config with the
        // bundle's CA callback makes mbedTLS ignore conf->ca_chain
        if (p->trust == TLS_TRUST_CUSTOM) {
            s->ca = custom_ca;
            s->ca->refs++;
            mbedtls_ssl_conf_ca_chain(&s->conf, &s->ca->crt, NULL);
        } else if (p->trust == TLS_TRUST_PIN) {
            mbedtls_ssl_conf_ca_chain(&s->conf, &tls_empty_ca, NULL);
        }
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        if (p->trust == TLS_TRUST_BUNDLE) {
            ret = esp_crt_bundle_attach(&s->conf);
        }
#endif
    }
    if (ret == 0) {
        ret = mbedtls_ssl_setup(&s->ssl, &s->conf);
    }
    if (ret == 0) {
        // No name: mbedTLS 4 refuses to verify unless set_hostname was called, so NULL explicitly (no SNI)
        ret = mbedtls_ssl_set_hostname(&s->ssl, p->sni_len ? hostname : NULL);
    }
    if (ret != 0) {
        ESP_LOGW(TAG, "Open failed: -0x%04x", (unsigned)-ret);
        tls_free(s);
        return ret;
    }
    mbedtls_ssl_set_verify(&s->ssl, tls_verify, s);
    mbedtls_ssl_set_bio(&s->ssl, s, tls_bio_send, tls_bio_recv, NULL);
    *out = s;
    return 0;
}

int tls_open(uint8_t slot, uint16_t tag, const tls_open_params_t *p, uint8_t *reason)
{
    char hostname[TLS_SNI_MAX_LEN + 1];

    *reason = TLS_OPEN_REASON_NONE;
    // Verifying without a host name would skip the name check, so only PIN may go without one
    if ((slot >= TLS_MAX_SESSIONS) || (tag == 0) || (p->trust > TLS_TRUST_PIN) ||
        (p->opts & ~TLS_OPT_REQUIRE_TLS13) || (p->time_source > TLS_TIME_NIOS) ||
        (p->sni_len < 0) || (p->sni_len > TLS_SNI_MAX_LEN) || (memchr(p->sni, 0, p->sni_len) != NULL) ||
        ((p->trust != TLS_TRUST_PIN) && (p->sni_len == 0))) {
        return ESP_ERR_INVALID_ARG;
    }
    // The slot is the Ultimate's: whatever it still holds there is an orphan (its OPEN or CLOSE was lost)
    if (sessions[slot]) {
        ESP_LOGW(TAG, "OPEN into slot %d frees the session held there under tag %u", slot, sessions[slot]->tag);
        tls_free(sessions[slot]);
    }
    if (entropy_source() == ENTROPY_NONE) {
        *reason = TLS_OPEN_REASON_NO_ENTROPY;
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t time_source = tls_set_clock(p->time_source, p->unix_time);
    if ((p->trust != TLS_TRUST_PIN) && (time_source == TLS_TIME_NONE)) {
        // Never verify against a 1970 clock: every validity check would be meaningless
        ESP_LOGW(TAG, "Open refused: BUNDLE and CUSTOM need a clock, and neither unix_time nor SNTP gave one");
        *reason = TLS_OPEN_REASON_NO_TIME;
        return ESP_ERR_INVALID_STATE;
    }
    if ((p->trust == TLS_TRUST_CUSTOM) && !(custom_ca && (p->ca_tag != 0) && (custom_ca->tag == p->ca_tag))) {
        *reason = TLS_OPEN_REASON_NO_CUSTOM_CA;
        return ESP_ERR_INVALID_STATE;
    }
#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    if (p->trust == TLS_TRUST_BUNDLE) {
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    // All randomness comes from PSA, which (MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG) keeps no seeded state and draws every byte
    // from esp_fill_random() when needed. The entropy gate above runs at OPEN only and the handshake steps draw later:
    // this relies on the bridge never stopping the radio once started.
    if (!tls_psa_ready()) {
        return ESP_FAIL;
    }
    memcpy(hostname, p->sni, p->sni_len);
    hostname[p->sni_len] = 0;

    if (tls_sessions_open() == 0) {
        // heap_min then reports the peak of this burst of sessions, not of the Wi-Fi start
        heap_caps_monitor_local_minimum_free_size_stop();
        heap_caps_monitor_local_minimum_free_size_start();
    }
    tls_session_t *s = NULL;
    int ret = tls_setup(&s, tag, p, hostname, time_source);
    if (ret != 0) {
        return ret;
    }
    sessions[slot] = s;
    return ESP_OK;
}

// The handshake message mbedTLS has just processed (ssl->in_msg, reassembled, header included), when it is
// of the given type and complete; NULL otherwise
static const uint8_t *tls_hs_msg(tls_session_t *s, uint8_t type, size_t *len)
{
    const uint8_t *m = s->ssl.in_msg;
    size_t n = s->ssl.in_hslen;
    if (!m || (s->ssl.in_msgtype != MBEDTLS_SSL_MSG_HANDSHAKE) || (n < 4) || (m[0] != type) ||
        ((size_t)((m[1] << 16) | (m[2] << 8) | m[3]) != n - 4)) {
        return NULL;
    }
    *len = n;
    return m;
}

// Finds an extension of a ServerHello (or HelloRetryRequest); returns its data length, -1 when absent (also when
// a TLS 1.2 ServerHello has no extensions block at all), or -2 when the message is malformed
static int tls_sh_ext(const uint8_t *m, size_t len, uint16_t type, const uint8_t **data)
{
    const uint8_t *p = m + 4, *end = m + len;
    if (end - p < 2 + 32 + 1) {
        return -2;
    }
    p += 2 + 32;                        // legacy_version, random
    if (end - p < 1 + p[0] + 2 + 1) {
        return -2;
    }
    p += 1 + p[0];                      // session id
    p += 2 + 1;                         // cipher suite, compression
    if (p == end) {
        return -1;
    }
    if (end - p < 2) {
        return -2;
    }
    const uint8_t *eend = p + 2 + ((p[0] << 8) | p[1]);
    p += 2;
    if (eend > end) {
        return -2;
    }
    while (eend - p >= 4) {
        int t = (p[0] << 8) | p[1], n = (p[2] << 8) | p[3];
        if (eend - p - 4 < n) {
            return -2;
        }
        if (t == type) {
            *data = p + 4;
            return n;
        }
        p += 4 + n;
    }
    return -1;
}

// After each successful handshake step: records the negotiated group (no mbedTLS getter) from the TLS 1.3 ServerHello
// key_share (or the HelloRetryRequest group) or the TLS 1.2 ServerKeyExchange curve, and requires EMS in a TLS 1.2
// ServerHello, as mbedTLS has just parsed them. Returns MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE when EMS is not echoed.
static int tls_note_handshake(tls_session_t *s, int state_before, int version_before)
{
    int state = s->ssl.state;
    const uint8_t *m, *d;
    size_t len;
    if ((state_before == MBEDTLS_SSL_SERVER_HELLO) && (state != MBEDTLS_SSL_SERVER_HELLO)) {
        m = tls_hs_msg(s, MBEDTLS_SSL_HS_SERVER_HELLO, &len);
        if (version_before == MBEDTLS_SSL_VERSION_TLS1_3) {
            if (m && (tls_sh_ext(m, len, 51, &d) >= 2)) { // key_share: the group comes first in both forms
                s->stats.group = (d[0] << 8) | d[1];
            }
            // Not found: the group stays 0, and tls_handshake() refuses the session
        } else if (!s->ems_checked) {
            // mbedtls_ssl_conf_extended_master_secret() only offers it: a server that does not echo it is accepted
            // (ssl_tls12_client.c:742-759), so the engine refuses it here, before any key is derived. A ServerHello
            // it cannot find counts as one without EMS: fail closed.
            s->ems_checked = 1;
            s->stats.ems = m && (tls_sh_ext(m, len, 23, &d) == 0);
            if (!s->stats.ems) {
                ESP_LOGW(TAG, "TLS 1.2 server without the extended master secret: refused");
                mbedtls_ssl_send_alert_message(&s->ssl, MBEDTLS_SSL_ALERT_LEVEL_FATAL,
                                               MBEDTLS_SSL_ALERT_MSG_HANDSHAKE_FAILURE);
                return MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE;
            }
        }
    }
    if ((state_before == MBEDTLS_SSL_SERVER_KEY_EXCHANGE) && (version_before == MBEDTLS_SSL_VERSION_TLS1_2) &&
        (m = tls_hs_msg(s, MBEDTLS_SSL_HS_SERVER_KEY_EXCHANGE, &len)) != NULL && (len >= 4 + 3) && (m[4] == 3)) {
        s->stats.group = (m[5] << 8) | m[6]; // ECParameters: named_curve (3), then the NamedGroup
    }
    return 0;
}

// Classifies a failed handshake step for the Ultimate's failure reasons. The Ultimate infers nothing from bare mbedTLS codes.
static void tls_classify(tls_session_t *s, int ret, int state_before, int version_before, tls_step_result_t *r)
{
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        r->fail_reason = TLS_FAIL_PEER_CLOSED;
    } else if ((state_before == MBEDTLS_SSL_SERVER_HELLO) && (ret == MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION)) {
        // A TLS 1.2 ServerHello below the minimum: the 1.2 parser has set tls_version to the server's choice
        r->fail_reason = TLS_FAIL_VERSION_REFUSED;
        r->verify_flags = (uint16_t)s->ssl.tls_version;
    } else if ((state_before == MBEDTLS_SSL_SERVER_HELLO) && (version_before == MBEDTLS_SSL_VERSION_TLS1_3) &&
               (ret == MBEDTLS_ERR_SSL_ILLEGAL_PARAMETER)) {
        // This one step is all of mbedTLS's TLS 1.3 ServerHello processing (ssl_tls13_client.c:1995-2060), so
        // ILLEGAL_PARAMETER has many causes. Only a ServerHello without supported_versions chose TLS 1.2 or lower, and
        // of those only one whose random ends in the RFC 8446 4.1.3 sentinel is a downgrade; all else is fail_reason 0.
        size_t len;
        const uint8_t *d, *m = tls_hs_msg(s, MBEDTLS_SSL_HS_SERVER_HELLO, &len);
        if (m && (len >= 4 + 2 + 32) && (tls_sh_ext(m, len, 43, &d) == -1)) {
            const uint8_t *tail = m + 4 + 2 + 32 - 8; // the last 8 bytes of the random
            if (s->opts & TLS_OPT_REQUIRE_TLS13) {
                r->fail_reason = TLS_FAIL_VERSION_REFUSED;
                r->verify_flags = (m[4] << 8) | m[5]; // legacy_version: the version it chose
            } else if (!memcmp(tail, "\x44\x4F\x57\x4E\x47\x52\x44", 7) && (tail[7] <= 1)) {
                r->fail_reason = TLS_FAIL_DOWNGRADE;
            }
        }
    } else if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
        r->fail_reason = ((s->trust == TLS_TRUST_PIN) && s->pin_mismatch) ? TLS_FAIL_PIN_MISMATCH : TLS_FAIL_CERT_VERIFY;
        r->verify_flags = mbedtls_ssl_get_verify_result(&s->ssl);
    } else if (ret == MBEDTLS_ERR_SSL_BAD_CERTIFICATE) {
        // A secondary check (usage, TLS 1.2 curve), or a Certificate message refused before verification ran
        r->fail_reason = TLS_FAIL_CERT_VERIFY;
        r->verify_flags = mbedtls_ssl_get_verify_result(&s->ssl);
    } else if (ret == MBEDTLS_ERR_X509_FATAL_ERROR) {
        r->fail_reason = TLS_FAIL_CERT_VERIFY;
        r->verify_flags = 0xFFFFFFFF;
    }
}

// Runs handshake steps until mbedTLS wants input or output, the handshake is over, or it fails
static int tls_handshake(tls_session_t *s, tls_step_result_t *r)
{
    while (!mbedtls_ssl_is_handshake_over(&s->ssl)) {
        int state_before = s->ssl.state;
        int version_before = s->ssl.tls_version;
        int64_t start = esp_timer_get_time();
        int ret = mbedtls_ssl_handshake_step(&s->ssl);
        s->stats.handshake_us += (uint32_t)(esp_timer_get_time() - start);
        int ems = (ret == 0) ? tls_note_handshake(s, state_before, version_before) : 0;
        if (ems) {
            r->fail_reason = TLS_FAIL_NO_EMS;
            return ems;
        }
        if ((ret == MBEDTLS_ERR_SSL_WANT_READ) || (ret == MBEDTLS_ERR_SSL_WANT_WRITE)) {
            return 0;
        }
        if (ret != 0) {
            tls_classify(s, ret, state_before, version_before, r);
            return ret;
        }
    }
    if (s->stats.group == 0) {
        // The group is never 0 once ESTABLISHED: if it was not found, the reads above no longer match this
        // mbedTLS, and nothing they check (EMS) can be trusted either. Fail closed.
        ESP_LOGE(TAG, "Negotiated group not found in the server's messages: session refused");
        mbedtls_ssl_send_alert_message(&s->ssl, MBEDTLS_SSL_ALERT_LEVEL_FATAL, MBEDTLS_SSL_ALERT_MSG_INTERNAL_ERROR);
        return MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE;
    }
    s->stats.state = TLS_STATE_ESTABLISHED;
    s->stats.tls_version = (uint16_t)mbedtls_ssl_get_version_number(&s->ssl);
    s->stats.ciphersuite = (uint16_t)mbedtls_ssl_get_ciphersuite_id_from_ssl(&s->ssl);
    return 0;
}

int tls_step(uint8_t slot, uint16_t tag, uint8_t flags, const uint8_t *ct_in, int ct_in_len, const uint8_t *pt_in,
             int pt_in_len, uint8_t *out, uint16_t *ct_out_len, uint16_t *pt_out_len, tls_step_result_t *r)
{
    tls_session_t *s = tls_find(slot, tag);
    *ct_out_len = 0;
    *pt_out_len = 0;
    r->alert_in = TLS_ALERT_NONE;
    r->alert_out = TLS_ALERT_NONE;
    r->fail_reason = TLS_FAIL_NONE;
    r->verify_flags = 0;
    if (!s) {
        r->state = TLS_STATE_CLOSED;
        return ESP_ERR_NOT_FOUND;
    }
    r->state = s->stats.state;

    // Caller errors consume nothing, return nothing and leave the session as it is
    if ((flags & ~TLS_STEP_NO_READ) || (ct_in_len < 0) || (pt_in_len < 0) || (ct_in_len + pt_in_len > TLS_STEP_IN_MAX)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pt_in_len && (s->stats.state != TLS_STATE_ESTABLISHED)) {
        return ESP_ERR_INVALID_STATE;
    }
    // A full buffer refuses the input with ESP_ERR_NO_MEM, but the step still returns output,
    // so the Ultimate can send the same step again until it is taken
    int ret = 0;
    int refused = (ct_in_len > TLS_IN_BUF_SIZE - (s->in_len - s->in_off)) ? ESP_ERR_NO_MEM : ESP_OK;
    if (!refused && pt_in_len) {
        // mbedtls_ssl_write() must never see WANT_WRITE: it would keep part of pt_in for a
        // repeat call, and with output pending it reports len written without copying it
        int payload = mbedtls_ssl_get_max_out_record_payload(&s->ssl);
        int expansion = mbedtls_ssl_get_record_expansion(&s->ssl);
        if (payload <= 0) {
            ret = payload ? payload : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        } else if (expansion < 0) {
            ret = expansion;
        } else {
            int need = pt_in_len + ((pt_in_len + payload - 1) / payload) * expansion;
            if (need > TLS_OUT_BUF_SIZE) {
                return ESP_ERR_INVALID_ARG; // could never fit: send less plaintext per step
            }
            if (need > TLS_OUT_BUF_SIZE - s->out_len) {
                refused = ESP_ERR_NO_MEM;
            }
        }
    }

    if (!refused && (ret == 0)) {
        if (s->in_off) {
            memmove(s->in_buf, s->in_buf + s->in_off, s->in_len - s->in_off);
            s->in_len -= s->in_off;
            s->in_off = 0;
        }
        memcpy(s->in_buf + s->in_len, ct_in, ct_in_len);
        s->in_len += ct_in_len;
        s->stats.ct_in_bytes += ct_in_len;
    }
    // While handshaking only the handshake drains in_buf, so it also runs when the input was refused:
    // otherwise a full in_buf would refuse every resend of the step
    int just_established = 0;
    if (ret == 0) {
        if (s->stats.state == TLS_STATE_HANDSHAKING) {
            ret = tls_handshake(s, r);
            just_established = (s->stats.state == TLS_STATE_ESTABLISHED);
        } else if (!refused && pt_in_len) {
            if (s->ssl.out_left != 0) {
                ret = MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            }
            for (int done = 0, tickets = 0; (ret == 0) && (done < pt_in_len); ) {
                int n = mbedtls_ssl_write(&s->ssl, pt_in + done, pt_in_len - done);
                if (n > 0) {
                    done += n;
                } else if ((n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) && (++tickets <= TLS_TICKET_RETRIES)) {
                    // A ticket an earlier read left pending: mbedtls_ssl_write() took it first and wrote
                    // nothing, so write again (ssl_msg.c:5938-5942 in mbedTLS 4.1.1). Not a failure.
                    continue;
                } else {
                    ret = n ? n : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
                }
            }
            if (ret == 0) {
                s->stats.pt_in_bytes += pt_in_len;
            }
        }
    }
    // From here on out may be written: the input is either consumed or refused

    // No plaintext from the step that reaches ESTABLISHED (the Ultimate reads STATS right after it), nor from
    // a NO_READ step (a WRITE's): what is pending stays in the engine for the next plain step
    int ct_len = (s->out_len < TLS_STEP_OUT_MAX) ? s->out_len : TLS_STEP_OUT_MAX;
    int pt_len = 0;
    while ((ret == 0) && (s->stats.state == TLS_STATE_ESTABLISHED) && !just_established && !(flags & TLS_STEP_NO_READ) &&
           (ct_len + pt_len + TLS_ALERT_ROOM < TLS_STEP_OUT_MAX)) {
        int in_before = s->in_off;
        int n = mbedtls_ssl_read(&s->ssl, out + ct_len + pt_len, TLS_STEP_OUT_MAX - TLS_ALERT_ROOM - ct_len - pt_len);
        if (n > 0) {
            pt_len += n;
        } else if ((n == MBEDTLS_ERR_SSL_WANT_READ) && mbedtls_ssl_check_pending(&s->ssl)) {
            // A complete ticket makes mbedtls_ssl_read() return WANT_READ with the ticket kept for the
            // next call (ssl_msg.c:5466-5473 in mbedTLS 4.1.1), which takes it without new input.
            // Take every one now, or the reply behind them waits for a step the Ultimate never sends.
            continue;
        } else if ((n == MBEDTLS_ERR_SSL_WANT_READ) && (s->in_off > in_before) && (s->in_off < s->in_len)) {
            // A record mbedTLS skipped with WANT_READ (a TLS 1.2 no_renegotiation warning alert,
            // ssl_msg.c:5002-5006) while more input waits: read on, or the data behind it would wait
            // for another step. The read took input, so this ends.
            continue;
        } else if ((n == MBEDTLS_ERR_SSL_WANT_READ) || (n == MBEDTLS_ERR_SSL_WANT_WRITE)) {
            break;
        } else if (n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
            continue; // a TLS 1.3 ticket after the handshake: no plaintext yet, read again (never FAILED)
        } else if (n == 0) {
            // An empty application data record, which RFC 8446 allows: read on. It never means the
            // connection closed, because tls_bio_recv() never reports end of input.
            continue;
        } else if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            // RFC 8446 6.1 / RFC 5246 7.2.1: answer the peer's close_notify with our own. Best effort, as in
            // tls_close(); it lands in out_buf and goes out with this step (TLS_ALERT_ROOM is kept free for it).
            mbedtls_ssl_close_notify(&s->ssl);
            s->stats.state = TLS_STATE_CLOSED;
        } else {
            ret = n;
        }
    }
    s->stats.pt_out_bytes += pt_len;

    if (ret != 0) {
        s->stats.state = TLS_STATE_FAILED;
        if (ret == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE) {
            int alert = mbedtls_ssl_get_fatal_alert(&s->ssl);
            r->alert_in = (alert >= 0) ? (uint8_t)alert : TLS_ALERT_NONE;
        }
        // The alert this engine sent, when mbedTLS wrote it before any keys (mbedTLS has no getter for it)
        if ((s->ssl.out_msgtype == MBEDTLS_SSL_MSG_ALERT) && (s->ssl.transform_out == NULL) &&
            (s->ssl.out_msg[0] == MBEDTLS_SSL_ALERT_LEVEL_FATAL)) {
            r->alert_out = s->ssl.out_msg[1];
        }
        ESP_LOGW(TAG, "Session %d/%u failed: -0x%04x, reason %d, verify 0x%x", slot, tag, (unsigned)-ret, r->fail_reason,
                 (unsigned)r->verify_flags);
    }

    // Ciphertext the reads produced (a fatal alert before the session is freed) goes out in this
    // step too: move the plaintext up into the room the reads left for it
    int ct_all = (s->out_len < TLS_STEP_OUT_MAX - pt_len) ? s->out_len : TLS_STEP_OUT_MAX - pt_len;
    if (ct_all > ct_len) {
        memmove(out + ct_all, out + ct_len, pt_len);
        ct_len = ct_all;
    }
    memcpy(out, s->out_buf, ct_len);
    memmove(s->out_buf, s->out_buf + ct_len, s->out_len - ct_len);
    s->out_len -= ct_len;
    s->stats.ct_out_bytes += ct_len;
    *ct_out_len = ct_len;
    *pt_out_len = pt_len;

    r->state = s->stats.state;
    if ((s->stats.state == TLS_STATE_FAILED) || (s->stats.state == TLS_STATE_CLOSED)) {
        tls_free(s);
    }
    return ret ? ret : refused;
}

int tls_close(uint8_t slot, uint16_t tag, uint8_t *out, uint16_t *ct_out_len)
{
    *ct_out_len = 0;
    tls_session_t *s = tls_find(slot, tag);
    if (!s) {
        return ESP_ERR_NOT_FOUND;
    }
    // Best effort: close_notify lands in out_buf behind anything still pending there. A session still
    // handshaking has nothing to close, and its half flight is not sent.
    if (s->stats.state == TLS_STATE_ESTABLISHED) {
        mbedtls_ssl_close_notify(&s->ssl);
        int n = (s->out_len < TLS_STEP_OUT_MAX) ? s->out_len : TLS_STEP_OUT_MAX;
        memcpy(out, s->out_buf, n);
        *ct_out_len = n;
    }
    tls_free(s);
    return ESP_OK;
}

void tls_close_all(void)
{
    for (int i = 0; i < TLS_MAX_SESSIONS; i++) {
        if (sessions[i]) {
            tls_free(sessions[i]);
        }
    }
    tls_ca_release(custom_ca);
    custom_ca = NULL;
}

// SET_CA: the upload-time refusals, in their order of precedence
static uint8_t tls_ca_reject(const mbedtls_x509_crt *crt)
{
    psa_key_type_t type = mbedtls_pk_get_key_type(&crt->pk);
    size_t bits = mbedtls_pk_get_bitlen(&crt->pk);
    if (PSA_KEY_TYPE_IS_RSA(type) && (bits < mbedtls_x509_crt_profile_default.rsa_min_bitlen)) {
        return TLS_CA_REJECT_RSA_KEY_SHORT;
    }
    if (PSA_KEY_TYPE_IS_ECC(type)) {
        // The curves of mbedtls_x509_crt_profile_default, which the engine verifies with (x509_crt.c:98-104 in
        // 4.1.1), not the key-exchange groups
        psa_ecc_family_t family = PSA_KEY_TYPE_ECC_GET_FAMILY(type);
        int ok = ((family == PSA_ECC_FAMILY_SECP_R1) && ((bits == 256) || (bits == 384) || (bits == 521))) ||
                 ((family == PSA_ECC_FAMILY_BRAINPOOL_P_R1) && ((bits == 256) || (bits == 384) || (bits == 512)));
        if (!ok) {
            return TLS_CA_REJECT_CURVE;
        }
    }
    // mbedTLS refuses such a certificate as an issuer. A v1 root has no extensions and is an anchor as it is;
    // a keyUsage extension that is absent passes check_key_usage.
    if (((crt->version >= 3) && (mbedtls_x509_crt_get_ca_istrue(crt) != 1)) ||
        (mbedtls_x509_crt_check_key_usage(crt, MBEDTLS_X509_KU_KEY_CERT_SIGN) != 0)) {
        return TLS_CA_REJECT_NOT_CA;
    }
    return TLS_CA_REJECT_NONE;
}

int tls_set_ca(const uint8_t *der, int der_len, uint16_t ca_tag, uint8_t *reject)
{
    *reject = TLS_CA_REJECT_NONE;
    // The one held is dropped first: on any error none is held (a session keeps its own reference)
    tls_ca_release(custom_ca);
    custom_ca = NULL;
    if ((der_len <= 0) || (der_len > TLS_CA_MAX_LEN) || (ca_tag == 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!tls_psa_ready()) {
        return ESP_ERR_INVALID_STATE; // never ESP_FAIL: the Ultimate reads a SET_CA ESP_FAIL as no reply
    }
    tls_ca_t *ca = calloc(1, sizeof(tls_ca_t));
    if (!ca) {
        return ESP_ERR_NO_MEM;
    }
    mbedtls_x509_crt_init(&ca->crt);
    ca->tag = ca_tag;
    ca->refs = 1;
    int ret = mbedtls_x509_crt_parse_der(&ca->crt, der, der_len);
    if (ret == 0) {
        *reject = tls_ca_reject(&ca->crt);
        if (*reject != TLS_CA_REJECT_NONE) {
            ret = ESP_ERR_INVALID_ARG;
        }
    } else if (ret > -2) {
        ret = MBEDTLS_ERR_X509_INVALID_FORMAT; // never ESP_FAIL (-1), which the Ultimate does not read as a parse code
    }
    if (ret != 0) {
        ESP_LOGW(TAG, "CA refused: %d, reject %d", ret, *reject);
        tls_ca_release(ca);
        return ret;
    }
    custom_ca = ca;
    return ESP_OK;
}

int tls_get_stats(uint8_t slot, uint16_t tag, tls_session_stats_t *stats)
{
    memset(stats, 0, sizeof(tls_session_stats_t));
    stats->state = TLS_STATE_CLOSED;
    if ((slot == TLS_SLOT_GLOBAL) && (tag == 0)) {
        time_t now = time(NULL);
        if (now <= tls_build_time()) {
            stats->time_source = TLS_TIME_NONE;
        } else {
            stats->time_source = sntp_has_time() ? TLS_TIME_SNTP : (clock_set_by_nios ? TLS_TIME_NIOS : TLS_TIME_NONE);
        }
        stats->esp_time = (stats->time_source == TLS_TIME_NONE) ? 0 : (uint32_t)now;
        return ESP_OK;
    }
    tls_session_t *s = tls_find(slot, tag);
    if (!s) {
        return ESP_ERR_NOT_FOUND;
    }
    *stats = s->stats;
    return ESP_OK;
}
