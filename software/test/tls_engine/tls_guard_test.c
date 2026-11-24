/* tls_guard_test.c: host test of the TLS version-confusion guard in tls_engine.c (Mbed TLS advisory 2026-11,
 * CVE-2026-115490: a ServerHello without supported_versions, legacy_version 0x0304 and a TLS 1.3 suite, sent to a
 * client that offers both TLS 1.2 and 1.3, moves mbedTLS 4.1.1 into its TLS 1.3 machine with no key exchange).
 *
 * The unit under test is tls_engine.c itself, included below so the test can look at the session it holds. Build and
 * run with target/pc/linux/tlsguard (make test). The peers are in this file:
 *   - a scripted attacker that answers the ClientHello with the ServerHello above;
 *   - an honest mbedTLS server, once limited to TLS 1.2 and once to TLS 1.3 (control: the guard refuses neither).
 * Exit status 0 only when every check passes. With the guard compiled out (make red) the attack check fails and the
 * test prints what the client did instead. */

#include TLS_ENGINE_SRC // the engine under test (default: the tree's own tls_engine.c)

#include <stdio.h>
#include <string.h>
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "psa/crypto.h"
#include "test_cert.h"
#include "test_key.h"

int tlsguard_verbose;
static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
                              else { printf("  ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void hexline(const char *label, const uint8_t *p, size_t n)
{
    printf("  %s (%u bytes):", label, (unsigned)n);
    for (size_t i = 0; i < n && i < 48; i++) {
        printf(" %02x", p[i]);
    }
    printf(n > 48 ? " ...\n" : "\n");
}

// SHA-256 of the test certificate's SubjectPublicKeyInfo: what the engine's PIN trust compares
static void pin_of_test_cert(uint8_t pin[TLS_PIN_LEN])
{
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    if (mbedtls_x509_crt_parse_der(&crt, test_cert_der, sizeof(test_cert_der)) != 0) {
        printf("test certificate does not parse\n");
        exit(2);
    }
    size_t n = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, crt.pk_raw.p, crt.pk_raw.len, pin, TLS_PIN_LEN, &n) != PSA_SUCCESS) {
        exit(2);
    }
    mbedtls_x509_crt_free(&crt);
}

static int open_client(void)
{
    uint8_t pin[TLS_PIN_LEN], reason = 0;
    pin_of_test_cert(pin);
    tls_open_params_t p = { .trust = TLS_TRUST_PIN, .opts = 0, .time_source = TLS_TIME_NONE, .pin = pin, .sni = NULL, .sni_len = 0 };
    tls_close_all();
    return tls_open(0, 1, &p, &reason);
}

static int client_step(const uint8_t *in, int in_len, uint8_t *out, uint16_t *out_len, tls_step_result_t *r)
{
    uint16_t pt = 0;
    return tls_step(0, 1, TLS_STEP_NO_READ, in, in_len, NULL, 0, out, out_len, &pt, r);
}

// ---- the attacker -----------------------------------------------------------------------------------------------

// ServerHello per the advisory: legacy_version 0x0304, a TLS 1.3 suite the client offered, and no supported_versions
// (RFC 8446 4.1.3 would make it mandatory). The session id is echoed as a TLS 1.3 server does. Two forms: with no
// extensions at all, and with the extended_master_secret extension, which a real attacker adds because the engine
// refuses a TLS 1.2 ServerHello without it (a second barrier that would otherwise hide the guard under test).
static size_t build_attack_server_hello(const uint8_t *client_hello_record, uint8_t *out, int with_ems)
{
    const uint8_t *ch = client_hello_record + 5;   // handshake header, then the body
    const uint8_t *body = ch + 4;
    uint8_t sid_len = body[2 + 32];
    const uint8_t *sid = body + 2 + 32 + 1;
    uint8_t *p = out + 5 + 4;
    *p++ = 0x03; *p++ = 0x04;                      // legacy_version 0x0304
    for (int i = 0; i < 32; i++) {
        *p++ = (uint8_t)(0xA0 + i);                // random: no downgrade sentinel
    }
    *p++ = sid_len;
    memcpy(p, sid, sid_len); p += sid_len;
    *p++ = 0x13; *p++ = 0x01;                      // TLS_AES_128_GCM_SHA256, in the client's list
    *p++ = 0x00;                                   // compression null
    if (with_ems) {                                // extensions: extended_master_secret only, which the engine's TLS 1.2
        *p++ = 0x00; *p++ = 0x04;                  // checks demand (tls_note_handshake); still no supported_versions
        *p++ = 0x00; *p++ = 0x17; *p++ = 0x00; *p++ = 0x00;
    }                                              // else nothing follows: no extensions at all
    size_t hs_len = (size_t)(p - (out + 5 + 4));
    out[5] = 2; out[6] = 0; out[7] = (uint8_t)(hs_len >> 8); out[8] = (uint8_t)hs_len;
    size_t rec_len = hs_len + 4;
    out[0] = 22; out[1] = 3; out[2] = 3; out[3] = (uint8_t)(rec_len >> 8); out[4] = (uint8_t)rec_len;
    return rec_len + 5;
}

// Without the guard: shows that the TLS 1.3 machine takes a plaintext TLS 1.3 Certificate (the attacker holds the pinned
// key here, as the intended server would) and moves on to CertificateVerify, with no key schedule behind it.
static void observe_certificate_path(void)
{
    uint8_t msg[5 + 4 + 1 + 3 + 3 + sizeof(test_cert_der) + 2], out[TLS_STEP_OUT_MAX];
    size_t cl = sizeof(test_cert_der), hl = 1 + 3 + 3 + cl + 2;
    uint8_t *p = msg;
    *p++ = 22; *p++ = 3; *p++ = 3; *p++ = (uint8_t)((hl + 4) >> 8); *p++ = (uint8_t)(hl + 4);
    *p++ = 11; *p++ = 0; *p++ = (uint8_t)(hl >> 8); *p++ = (uint8_t)hl;
    *p++ = 0;                                                        // certificate_request_context
    size_t ll = 3 + cl + 2;
    *p++ = 0; *p++ = (uint8_t)(ll >> 8); *p++ = (uint8_t)ll;         // certificate_list
    *p++ = 0; *p++ = (uint8_t)(cl >> 8); *p++ = (uint8_t)cl;
    memcpy(p, test_cert_der, cl); p += cl;
    *p++ = 0; *p++ = 0;                                              // no entry extensions
    uint16_t out_len = 0;
    tls_step_result_t r;
    int ret = client_step(msg, (int)(p - msg), out, &out_len, &r);
    printf("  OBSERVED (wrong): a plaintext TLS 1.3 Certificate from the attacker: ret=%d state=%d fail_reason=%d, ", ret, r.state, r.fail_reason);
    if (sessions[0]) {
        printf("ssl.state now %d (%s)\n", sessions[0]->ssl.state,
               (sessions[0]->ssl.state == MBEDTLS_SSL_CERTIFICATE_VERIFY && !sessions[0]->pin_mismatch) ? "CERTIFICATE_VERIFY: the certificate was taken, PIN matched" : "?");
    } else {
        printf("session freed\n");
    }
}

static void test_attack(int with_ems)
{
    uint8_t ch[TLS_STEP_OUT_MAX], sh[256], out[TLS_STEP_OUT_MAX];
    uint16_t ch_len = 0, out_len = 0;
    tls_step_result_t r;
    printf("attack%s: ServerHello, legacy_version 0x0304, suite 0x1301, no supported_versions%s\n", with_ems ? " B" : " A",
           with_ems ? ", with extended_master_secret" : ", no extensions");
    CHECK(open_client() == ESP_OK, "client opened (TLS 1.2 + 1.3 offered, PIN trust)");
    int ret = client_step(NULL, 0, ch, &ch_len, &r);
    CHECK(ret == 0 && ch_len > 5 && ch[0] == 22 && ch[5] == 1, "ClientHello produced (%u bytes)", ch_len);
    size_t sh_len = build_attack_server_hello(ch, sh, with_ems);
    hexline("attack ServerHello record", sh, sh_len);

    ret = client_step(sh, (int)sh_len, out, &out_len, &r);
    printf("  client result: ret=%d (-0x%04x) state=%d fail_reason=%d verify_flags=0x%x alert_out=%d alert_in=%d\n", ret,
           (unsigned)(ret < 0 ? -ret : ret), r.state, r.fail_reason, (unsigned)r.verify_flags, r.alert_out, r.alert_in);
    hexline("client output", out, out_len);
    if (sessions[0]) {
        // Only without the guard. mbedTLS now runs its TLS 1.3 machine on a connection that never had a key exchange.
        printf("  OBSERVED (wrong): the session is still live, ssl.tls_version=0x%04x, ssl.state=%d%s, handshake_over=%d\n",
               (unsigned)sessions[0]->ssl.tls_version, sessions[0]->ssl.state,
               sessions[0]->ssl.state == MBEDTLS_SSL_SERVER_CERTIFICATE ? " (SERVER_CERTIFICATE: EncryptedExtensions skipped)" : "",
               mbedtls_ssl_is_handshake_over(&sessions[0]->ssl));
        observe_certificate_path();
    }
    CHECK(ret == MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION, "step returns BAD_PROTOCOL_VERSION (got %d)", ret);
    CHECK(r.state == TLS_STATE_FAILED, "session state FAILED (got %d)", r.state);
    CHECK(r.fail_reason == TLS_FAIL_VERSION_REFUSED, "fail_reason TLS_FAIL_VERSION_REFUSED = Ultimate status 18 (got %d)", r.fail_reason);
    CHECK((r.verify_flags & 0xFFFF) == 0x0304, "version reported is 0x0304 (got 0x%x)", (unsigned)(r.verify_flags & 0xFFFF));
    static const uint8_t alert[] = { 0x15, 0x03, 0x03, 0x00, 0x02, 0x02, 0x46 }; // fatal protocol_version
    CHECK(r.alert_out == 0x46, "engine reports the unencrypted fatal alert it sent: protocol_version (got %d)", r.alert_out);
    CHECK(out_len == sizeof(alert) && !memcmp(out, alert, sizeof(alert)), "the alert record is on the wire to the peer");
    CHECK(sessions[0] == NULL, "session freed");
}

// ---- the honest servers (controls) ------------------------------------------------------------------------------

typedef struct { uint8_t b[16384]; size_t n; } queue_t;
static queue_t to_client, to_server;

static int srv_send(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    memcpy(to_client.b + to_client.n, buf, len);
    to_client.n += len;
    return (int)len;
}

static int srv_recv(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    if (to_server.n == 0) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    size_t n = len < to_server.n ? len : to_server.n;
    memcpy(buf, to_server.b, n);
    memmove(to_server.b, to_server.b + n, to_server.n - n);
    to_server.n -= n;
    return (int)n;
}

static void test_honest(int version, const char *name)
{
    printf("control: honest %s server\n", name);
    mbedtls_ssl_context srv;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt crt;
    mbedtls_pk_context pk;
    mbedtls_ssl_init(&srv); mbedtls_ssl_config_init(&conf); mbedtls_x509_crt_init(&crt); mbedtls_pk_init(&pk);
    int ok = (mbedtls_x509_crt_parse_der(&crt, test_cert_der, sizeof(test_cert_der)) == 0) &&
             (mbedtls_pk_parse_key(&pk, test_key_der, sizeof(test_key_der), NULL, 0) == 0) &&
             (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0);
    if (ok) {
        mbedtls_ssl_conf_min_tls_version(&conf, version);
        mbedtls_ssl_conf_max_tls_version(&conf, version);
        ok = (mbedtls_ssl_conf_own_cert(&conf, &crt, &pk) == 0) && (mbedtls_ssl_setup(&srv, &conf) == 0);
    }
    CHECK(ok, "server set up");
    mbedtls_ssl_set_bio(&srv, NULL, srv_send, srv_recv, NULL);
    to_client.n = to_server.n = 0;
    CHECK(open_client() == ESP_OK, "client opened");

    uint8_t out[TLS_STEP_OUT_MAX];
    uint16_t out_len;
    tls_step_result_t r = { .state = TLS_STATE_HANDSHAKING };
    int ret = 0;
    for (int i = 0; ok && i < 60 && r.state == TLS_STATE_HANDSHAKING && ret == 0; i++) {
        int n = to_client.n < TLS_STEP_IN_MAX ? (int)to_client.n : TLS_STEP_IN_MAX;
        ret = client_step(to_client.b, n, out, &out_len, &r);
        memmove(to_client.b, to_client.b + n, to_client.n - n);
        to_client.n -= n;
        memcpy(to_server.b + to_server.n, out, out_len);
        to_server.n += out_len;
        int sret;
        do {
            sret = mbedtls_ssl_handshake(&srv);
        } while (sret == 0 && !mbedtls_ssl_is_handshake_over(&srv) && to_server.n);
    }
    CHECK(ret == 0 && r.state == TLS_STATE_ESTABLISHED, "client ESTABLISHED (ret=%d state=%d fail_reason=%d)", ret, r.state, r.fail_reason);
    tls_session_stats_t st;
    memset(&st, 0, sizeof(st));
    if (tls_get_stats(0, 1, &st) == ESP_OK) {
        CHECK(st.tls_version == (version == MBEDTLS_SSL_VERSION_TLS1_3 ? 0x0304 : 0x0303), "negotiated version 0x%04x", st.tls_version);
        printf("  suite 0x%04x group 0x%04x\n", st.ciphersuite, st.group);
    }
    tls_close_all();
    mbedtls_ssl_free(&srv); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&crt); mbedtls_pk_free(&pk);
}

int main(int argc, char **argv)
{
    tlsguard_verbose = (argc > 1) && !strcmp(argv[1], "-v");
    printf("Mbed TLS %s\n", MBEDTLS_VERSION_STRING);
    if (psa_crypto_init() != PSA_SUCCESS) {
        return 2;
    }
    test_honest(MBEDTLS_SSL_VERSION_TLS1_2, "TLS 1.2");
    test_honest(MBEDTLS_SSL_VERSION_TLS1_3, "TLS 1.3");
    test_attack(1);
    test_attack(0);
    printf("%s (%d failed checks)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
