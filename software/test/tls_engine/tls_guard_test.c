/* tls_guard_test.c: host test of the TLS version-confusion guard in tls_engine.c (Mbed TLS advisory 2026-11,
 * CVE-2026-115490: a ServerHello without supported_versions, legacy_version 0x0304 and a TLS 1.3 suite, sent to a
 * client that offers both TLS 1.2 and 1.3, moves mbedTLS 4.1.1 into its TLS 1.3 machine with no key exchange).
 *
 * The unit under test is tls_engine.c itself, included below so the test can look at the session it holds. Build and
 * run with target/pc/linux/tlsguard (see its Makefile). The peers are in this file:
 *   - a scripted attacker that answers the ClientHello with the ServerHello above;
 *   - honest mbedTLS servers (TLS 1.2 only, TLS 1.3 only, TLS 1.3 with a HelloRetryRequest, and one offering both):
 *     controls, the guard refuses none of them.
 *
 * What it asserts, on Mbed TLS 4.1.1 and on a 4.1.x that has the upstream fix, alike: the handshake is refused at the
 * ServerHello, with a fatal protocol_version alert, the session is freed, and the version reported to the Ultimate is
 * the 0x0304 the server sent. Which layer refuses differs: on 4.1.1 the engine's guard (the library accepts the
 * ServerHello); on the fixed library the library does, before the guard is reached. The test probes the linked library
 * with a bare client and prints which case ran.
 *
 * The guard is defence in depth and an early, specific refusal, not the only barrier: with the guard compiled out
 * (make red) the ServerHello is accepted, but the handshake still cannot end ESTABLISHED, because the engine's
 * end-of-handshake check finds no negotiated group. The "barrier" mode of this test shows that, on the guard-disabled
 * build, by driving the whole attack (Certificate, CertificateVerify and Finished, computed from the client's own
 * key schedule, which has no secret in it).
 *
 * Usage: tlsguard [-v]        the guard checks and the controls; exit status 0 only when every check passes
 *        tlsguard barrier     only on the guard-disabled build: the second barrier; "make test" runs it
 * With the guard compiled out the guard checks fail (make red expects that) and the test prints what the client did. */

#include TLS_ENGINE_SRC // the engine under test (default: the tree's own tls_engine.c)

#include <stdio.h>
#include <string.h>
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "psa/crypto.h"
#include "test_cert.h"
#include "test_key.h"
#include "ssl_misc.h"
#include "ssl_tls13_keys.h"
#include "mbedtls/md.h"

int tlsguard_verbose;
char tlsguard_last_error[160];
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
// key here, as the intended server would) and moves on to CertificateVerify, with no key exchange behind it. The
// handshake is not established by this: see test_second_barrier for how it ends.
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
    printf("  OBSERVED without the guard: a plaintext TLS 1.3 Certificate from the attacker: ret=%d state=%d fail_reason=%d, ", ret, r.state, r.fail_reason);
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
        // Only without the guard (and on an unfixed library). mbedTLS now runs its TLS 1.3 machine on a connection that
        // never had a key exchange; the handshake is not over, and does not end ESTABLISHED (see test_second_barrier).
        printf("  OBSERVED without the guard: the session is still live, ssl.tls_version=0x%04x, ssl.state=%d%s, handshake_over=%d\n",
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

// min/max: the server's TLS versions; groups: its supported groups (NULL: the default, so no HelloRetryRequest);
// want_version/want_group: what the client must report (group 0: only that one is set)
static void test_honest(const char *name, int min_v, int max_v, const uint16_t *groups, uint16_t want_version, uint16_t want_group)
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
        mbedtls_ssl_conf_min_tls_version(&conf, min_v);
        mbedtls_ssl_conf_max_tls_version(&conf, max_v);
        if (groups) {
            mbedtls_ssl_conf_groups(&conf, groups);
        }
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
        CHECK(st.tls_version == want_version, "negotiated version 0x%04x (want 0x%04x)", st.tls_version, want_version);
        CHECK(want_group ? st.group == want_group : st.group != 0, "negotiated group 0x%04x%s", st.group, want_group ? "" : " (not 0)");
        printf("  suite 0x%04x group 0x%04x\n", st.ciphersuite, st.group);
    }
    tls_close_all();
    mbedtls_ssl_free(&srv); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&crt); mbedtls_pk_free(&pk);
}

// ---- which library is linked -------------------------------------------------------------------------------------

static uint8_t probe_out[TLS_STEP_OUT_MAX * 2];
static size_t probe_out_n;
static const uint8_t *probe_in;
static size_t probe_in_n;

static int probe_send(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    memcpy(probe_out + probe_out_n, buf, len);
    probe_out_n += len;
    return (int)len;
}

static int probe_recv(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    if (probe_in_n == 0) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    size_t n = len < probe_in_n ? len : probe_in_n;
    memcpy(buf, probe_in, n);
    probe_in += n;
    probe_in_n -= n;
    return (int)n;
}

// Sends the attack ServerHello to a bare Mbed TLS client (TLS 1.2 + 1.3, no engine) and returns 1 when the library
// itself refuses it (BAD_PROTOCOL_VERSION: the upstream fix), 0 when it takes it (4.1.1: only the guard stops it)
static int library_refuses_first(void)
{
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf);
    int ok = (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0);
    if (ok) {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
        ok = (mbedtls_ssl_setup(&ssl, &conf) == 0);
    }
    int ret = -1;
    for (int i = 0; ok && i < 10 && ssl.state != MBEDTLS_SSL_SERVER_HELLO; i++) {
        mbedtls_ssl_set_bio(&ssl, NULL, probe_send, probe_recv, NULL);
        ret = mbedtls_ssl_handshake_step(&ssl);
        ok = (ret == 0);
    }
    uint8_t sh[256];
    if (ok && mbedtls_ssl_flush_output(&ssl) != 0) {  // the ClientHello is sent by the next step: flush it
        ok = 0;
    }
    if (ok && probe_out_n > 9 && probe_out[0] == 22 && probe_out[5] == 1) {
        probe_in_n = build_attack_server_hello(probe_out, sh, 1);
        probe_in = sh;
        // the first step may only switch from the TLS 1.3 to the TLS 1.2 parser (no supported_versions): step on
        for (int i = 0; i < 4 && ret == 0 && ssl.state == MBEDTLS_SSL_SERVER_HELLO; i++) {
            ret = mbedtls_ssl_handshake_step(&ssl);
        }
    } else {
        ok = 0;
    }
    mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf);
    if (!ok || (ret != 0 && ret != MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION)) {
        printf("library probe inconclusive (ret=-0x%04x)\n", (unsigned)(ret < 0 ? -ret : ret));
        exit(2);
    }
    return ret == MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION;
}

// ---- the second barrier (guard-disabled build only) -------------------------------------------------------------

// Drives the attack as far as it goes, as an attacker that holds the pinned key and computes Finished from the client's
// own key schedule (public: no key exchange went into it). Then the engine's end-of-handshake check must refuse.
static void test_second_barrier(void)
{
    uint8_t ch[TLS_STEP_OUT_MAX], sh[256], out[TLS_STEP_OUT_MAX], msg[2048];
    uint16_t ch_len = 0, out_len = 0;
    tls_step_result_t r;
    printf("second barrier (guard disabled): the full attack, ServerHello B then Certificate, CertificateVerify, Finished\n");
    CHECK(open_client() == ESP_OK, "client opened");
    int ret = client_step(NULL, 0, ch, &ch_len, &r);
    CHECK(ret == 0 && ch_len > 5 && ch[0] == 22 && ch[5] == 1, "ClientHello produced (%u bytes)", ch_len);
    size_t sh_len = build_attack_server_hello(ch, sh, 1);
    ret = client_step(sh, (int)sh_len, out, &out_len, &r);
    CHECK(ret == 0 && sessions[0], "ServerHello accepted (ret=%d): this is the guard-disabled build", ret);
    if (ret != 0 || !sessions[0]) {
        return;
    }
    CHECK(sessions[0]->ssl.tls_version == MBEDTLS_SSL_VERSION_TLS1_3, "mbedTLS is in its TLS 1.3 machine (0x%04x)", (unsigned)sessions[0]->ssl.tls_version);
    mbedtls_ssl_context *ssl = &sessions[0]->ssl;

    size_t cl = sizeof(test_cert_der), hl = 1 + 3 + 3 + cl + 2, ll = 3 + cl + 2;
    uint8_t *p = msg;
    *p++ = 22; *p++ = 3; *p++ = 3; *p++ = (uint8_t)((hl + 4) >> 8); *p++ = (uint8_t)(hl + 4);
    *p++ = 11; *p++ = 0; *p++ = (uint8_t)(hl >> 8); *p++ = (uint8_t)hl; *p++ = 0;
    *p++ = 0; *p++ = (uint8_t)(ll >> 8); *p++ = (uint8_t)ll;
    *p++ = 0; *p++ = (uint8_t)(cl >> 8); *p++ = (uint8_t)cl;
    memcpy(p, test_cert_der, cl); p += cl;
    *p++ = 0; *p++ = 0;
    ret = client_step(msg, (int)(p - msg), out, &out_len, &r);
    CHECK(ret == 0 && sessions[0], "Certificate accepted (PIN matched), ret=%d", ret);
    if (ret != 0 || !sessions[0]) {
        return;
    }

    // CertificateVerify over the client's own transcript hash
    uint8_t th[32], tbs[64 + 33 + 1 + 32], hash[32], sig[128];
    size_t thl = 0, sl = 0, hn = 0;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    memset(tbs, 0x20, 64); memcpy(tbs + 64, "TLS 1.3, server CertificateVerify", 33); tbs[97] = 0;
    int ok = (mbedtls_ssl_get_handshake_transcript(ssl, MBEDTLS_MD_SHA256, th, sizeof(th), &thl) == 0) && (thl == 32);
    memcpy(tbs + 98, th, 32);
    ok = ok && (psa_hash_compute(PSA_ALG_SHA_256, tbs, sizeof(tbs), hash, 32, &hn) == PSA_SUCCESS) &&
         (mbedtls_pk_parse_key(&pk, test_key_der, sizeof(test_key_der), NULL, 0) == 0) &&
         (mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, sizeof(sig), &sl) == 0);
    mbedtls_pk_free(&pk);
    CHECK(ok, "attacker signed the CertificateVerify");
    if (!ok) {
        return;
    }
    hl = 2 + 2 + sl;
    p = msg;
    *p++ = 22; *p++ = 3; *p++ = 3; *p++ = (uint8_t)((hl + 4) >> 8); *p++ = (uint8_t)(hl + 4);
    *p++ = 15; *p++ = 0; *p++ = (uint8_t)(hl >> 8); *p++ = (uint8_t)hl; *p++ = 4; *p++ = 3; *p++ = (uint8_t)(sl >> 8); *p++ = (uint8_t)sl;
    memcpy(p, sig, sl); p += sl;
    ret = client_step(msg, (int)(p - msg), out, &out_len, &r);
    CHECK(ret == 0 && sessions[0], "CertificateVerify accepted, ret=%d", ret);
    if (ret != 0 || !sessions[0]) {
        return;
    }

    // Finished, from the client's own key schedule
    uint8_t vd[64];
    size_t vl = 0;
    ok = (mbedtls_ssl_tls13_calculate_verify_data(ssl, vd, sizeof(vd), &vl, MBEDTLS_SSL_IS_SERVER) == 0) && (vl <= 48);
    CHECK(ok, "attacker computed the Finished verify data (%u bytes)", (unsigned)vl);
    if (!ok) {
        return;
    }
    CHECK(sessions[0]->stats.group == 0, "no negotiated group was ever recorded (group 0x%04x)", sessions[0]->stats.group);
    p = msg;
    *p++ = 22; *p++ = 3; *p++ = 3; *p++ = (uint8_t)((vl + 4) >> 8); *p++ = (uint8_t)(vl + 4);
    *p++ = 20; *p++ = 0; *p++ = 0; *p++ = (uint8_t)vl;
    memcpy(p, vd, vl); p += vl;
    ret = client_step(msg, (int)(p - msg), out, &out_len, &r);
    printf("  client result: ret=%d (-0x%04x) state=%d fail_reason=%d\n", ret, (unsigned)(ret < 0 ? -ret : ret), r.state, r.fail_reason);
    CHECK(ret == MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE, "end of the handshake refused: 'Negotiated group not found' (got %d)", ret);
    CHECK(strstr(tlsguard_last_error, "Negotiated group not found") != NULL, "the engine logged: %s", tlsguard_last_error);
    CHECK(r.state == TLS_STATE_FAILED && r.state != TLS_STATE_ESTABLISHED, "session FAILED, never ESTABLISHED (state %d)", r.state);
    CHECK(r.fail_reason == 0, "no specific reason: this is the late, generic refusal, not the guard's (got %d)", r.fail_reason);
    CHECK(sessions[0] == NULL, "session freed");
}

int main(int argc, char **argv)
{
    int barrier = (argc > 1) && !strcmp(argv[1], "barrier");
    tlsguard_verbose = (argc > 1) && !strcmp(argv[1], "-v");
    printf("Mbed TLS %s\n", MBEDTLS_VERSION_STRING);
    if (psa_crypto_init() != PSA_SUCCESS) {
        return 2;
    }
    int lib_first = library_refuses_first();
    if (lib_first) {
        printf("case: library refuses first (the linked Mbed TLS refuses the ServerHello itself; the guard is not reached)\n");
    } else {
        printf("case: engine guard refuses (the linked Mbed TLS accepts the ServerHello; only the guard stops it)\n");
    }
    if (barrier) {
        if (lib_first) {
            printf("second barrier: not applicable: library refuses first\n");
            return 0;
        }
        test_second_barrier();
    } else {
        static const uint16_t g384[] = { MBEDTLS_SSL_IANA_TLS_GROUP_SECP384R1, MBEDTLS_SSL_IANA_TLS_GROUP_NONE };
        test_honest("TLS 1.2", MBEDTLS_SSL_VERSION_TLS1_2, MBEDTLS_SSL_VERSION_TLS1_2, NULL, 0x0303, 0);
        test_honest("TLS 1.3", MBEDTLS_SSL_VERSION_TLS1_3, MBEDTLS_SSL_VERSION_TLS1_3, NULL, 0x0304, 0);
        test_honest("TLS 1.3 with HelloRetryRequest (secp384r1 only)", MBEDTLS_SSL_VERSION_TLS1_3, MBEDTLS_SSL_VERSION_TLS1_3, g384, 0x0304, 0x0018);
        test_honest("dual TLS 1.2 + 1.3", MBEDTLS_SSL_VERSION_TLS1_2, MBEDTLS_SSL_VERSION_TLS1_3, NULL, 0x0304, 0);
        test_attack(1);
        test_attack(0);
    }
    printf("%s (%d failed checks)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
