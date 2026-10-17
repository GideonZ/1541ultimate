// Host tests (make -C target/pc/linux/wifitls test) for the TLS calls to the WiFi module in wifi_tls.cc and for the
// channel they use (wifi_dedicated.cc), its cancel predicate included. The real wifi_tls.cc and
// wifi_dedicated.cc are compiled; only the UART, queue, tick and module are the world's.
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include "world.h"
#include "wifi_dedicated.h"
#include "wifi_tls.h"
#include "rpc_calls.h"
#include "check.h"
#undef printf

int failures;
int checks;

void check(bool condition, const char *what, ...)
{
    char line[300];
    va_list ap;
    va_start(ap, what);
    vsnprintf(line, sizeof(line), what, ap);
    va_end(ap);
    checks++;
    if (!condition) {
        failures++;
        printf("  FAIL  %s\n", line);
    } else {
        printf("  ok    %s\n", line);
    }
}


#define SEQUENCE 0x1234

// Word aligned, as the data of a command_buf_t is.
static union {
    uint32_t align;
    uint8_t bytes[CMD_BUF_SIZE];
} reply;

static void header(uint8_t command)
{
    memset(reply.bytes, 0, sizeof(reply.bytes));
    rpc_header_t *hdr = (rpc_header_t *)reply.bytes;
    hdr->command = command;
    hdr->thread = WIFI_DEDICATED_THREAD;
    hdr->sequence = SEQUENCE;
}

// A step answer: state, esp_err, ct bytes of 0xC0+i and pt bytes of 0x50+i.
static int step_reply(uint8_t state, int esp_err, uint16_t ct, uint16_t pt)
{
    header(CMD_TLS_STEP);
    rpc_tls_step_resp *r = (rpc_tls_step_resp *)reply.bytes;
    r->esp_err = esp_err;
    r->state = state;
    r->alert_in = TLS_ALERT_NONE;
    r->alert_out = TLS_ALERT_NONE;
    r->ct_out_len = ct;
    r->pt_out_len = pt;
    for (int i = 0; i < ct; i++) {
        r->data[i] = (uint8_t)(0xC0 + i);
    }
    for (int i = 0; i < pt; i++) {
        r->data[ct + i] = (uint8_t)(0x50 + i);
    }
    return offsetof(rpc_tls_step_resp, data) + ct + pt;
}

static uint8_t ct_out[CMD_BUF_SIZE];
static uint8_t pt_out[CMD_BUF_SIZE];

static void fresh_step(wifi_tls_step_t *st, uint16_t room)
{
    memset(st, 0, sizeof(*st));
    memset(ct_out, 0xEE, sizeof(ct_out));
    memset(pt_out, 0xEE, sizeof(pt_out));
    st->ct_out = ct_out;
    st->pt_out = pt_out;
    st->ct_out_room = room;
    st->pt_out_room = room;
    st->ct_out_len = 0x7777;
    st->pt_out_len = 0x7777;
    st->state = WIFI_TLS_UNKNOWN;
    st->esp_err = 0;
}

static bool untouched(const uint8_t *buf)
{
    for (int i = 0; i < CMD_BUF_SIZE; i++) {
        if (buf[i] != 0xEE) {
            return false;
        }
    }
    return true;
}

static bool nothing_out(const wifi_tls_step_t *st)
{
    return untouched(ct_out) && untouched(pt_out) && (st->ct_out_len == 0x7777) && (st->pt_out_len == 0x7777);
}

static void test_take_step(void)
{
    printf("The check on a STEP answer (20-byte header)\n");
    wifi_tls_step_t st;
    const uint16_t sizes[][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 255, 1 }, { 256, 0 }, { 300, 700 }, { 1024, 0 }, { 0, 1024 } };
    bool all = true;
    for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        uint16_t ct = sizes[k][0], pt = sizes[k][1];
        int size = step_reply(TLS_STATE_ESTABLISHED, 0, ct, pt);
        fresh_step(&st, 1024);
        int r = wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st);
        bool ok = (r == 0) && (st.ct_out_len == ct) && (st.pt_out_len == pt) && (st.state == WIFI_TLS_ESTABLISHED) &&
                  (memcmp(ct_out, ((rpc_tls_step_resp *)reply.bytes)->data, ct) == 0) &&
                  (memcmp(pt_out, ((rpc_tls_step_resp *)reply.bytes)->data + ct, pt) == 0) &&
                  (ct_out[ct] == 0xEE) && (pt_out[pt] == 0xEE);
        all = all && ok;
    }
    check(all, "an ESTABLISHED answer hands over exactly its bytes, at lengths from 0 to 1024 and past 255");

    int size = step_reply(TLS_STATE_ESTABLISHED, 0, 10, 10);
    int foreign = 0;
    for (int command = 0; command < 256; command++) {
        if (command == CMD_TLS_STEP) {
            continue;
        }
        ((rpc_header_t *)reply.bytes)->command = command;
        fresh_step(&st, 1024);
        foreign += (wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st) == WIFI_TLS_FOREIGN) && nothing_out(&st);
    }
    check(foreign == 255, "an answer to any other command is foreign and changes nothing (%d of 255)", foreign);

    int shorts = 0;
    size = step_reply(TLS_STATE_ESTABLISHED, 0, 10, 10);
    for (int s = 0; s < 4; s++) {
        fresh_step(&st, 1024);
        shorts += (wifi_tls_take_step(reply.bytes, s, SEQUENCE, &st) == WIFI_TLS_FOREIGN) && nothing_out(&st);
    }
    for (int s = 4; s < 40; s++) {
        fresh_step(&st, 1024);
        shorts += (wifi_tls_take_step(reply.bytes, s, SEQUENCE, &st) == WIFI_TLS_INVALID) && nothing_out(&st);
    }
    for (int s = 41; s < 64; s++) {
        fresh_step(&st, 1024);
        shorts += (wifi_tls_take_step(reply.bytes, s, SEQUENCE, &st) == WIFI_TLS_INVALID) && nothing_out(&st);
    }
    check(shorts == 63, "an answer of any size but its own (40) hands out nothing (%d of 63), the older 16-byte header too",
          shorts);
    size = step_reply(TLS_STATE_ESTABLISHED, 0, 20, 0);
    fresh_step(&st, 19);
    int r = wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st);
    check((r == WIFI_TLS_INVALID) && nothing_out(&st), "more ciphertext than the caller has room for is invalid, not cut");

    // FAILED carries the reasons, and only FAILED does.
    size = step_reply(TLS_STATE_FAILED, -0x2700, 7, 3);
    rpc_tls_step_resp *res = (rpc_tls_step_resp *)reply.bytes;
    res->fail_reason = TLS_FAIL_CERT_VERIFY;
    res->verify_flags = 0x0C;
    res->alert_in = 70;
    res->alert_out = 42;
    fresh_step(&st, 1024);
    r = wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st);
    check((r == 0) && (st.state == WIFI_TLS_FAILED) && (st.esp_err == -0x2700) && (st.fail_reason == 4) &&
          (st.verify_flags == 0x0C) && (st.alert_in == 70) && (st.alert_out == 42) && (st.ct_out_len == 7) &&
          (st.pt_out_len == 0) && untouched(pt_out),
          "FAILED: esp_err, fail_reason, verify_flags, alert_in and alert_out arrive; its plaintext is withheld");
    int reasons = 0;
    for (int state = 0; state < 3; state++) {
        size = step_reply((uint8_t)state, 0, 5, 0);
        ((rpc_tls_step_resp *)reply.bytes)->fail_reason = 4;
        fresh_step(&st, 1024);
        reasons += (wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st) == WIFI_TLS_INVALID) && nothing_out(&st);
    }
    check(reasons == 3, "a fail_reason without FAILED is invalid (%d of 3)", reasons);
    size = step_reply(TLS_STATE_ESTABLISHED, 0, 5, 0);
    ((rpc_tls_step_resp *)reply.bytes)->alert_in = 0xFF;
    ((rpc_tls_step_resp *)reply.bytes)->verify_flags = 0;
    fresh_step(&st, 1024);
    r = wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st);
    check((r == 0) && (st.alert_in == WIFI_TLS_ALERT_NONE) && (st.fail_reason == 0), "no alert is 0xFF");

    // NO_READ.
    size = step_reply(TLS_STATE_ESTABLISHED, 0, 30, 1);
    fresh_step(&st, 1024);
    st.flags = WIFI_TLS_STEP_NO_READ;
    r = wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st);
    check((r == WIFI_TLS_INVALID) && nothing_out(&st), "plaintext on a NO_READ step is malformed (-> 17)");
    size = step_reply(TLS_STATE_ESTABLISHED, 0, 30, 0);
    fresh_step(&st, 1024);
    st.flags = WIFI_TLS_STEP_NO_READ;
    r = wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st);
    check((r == 0) && (st.ct_out_len == 30), "a NO_READ step with ciphertext only is fine");
    size = step_reply(TLS_STATE_ESTABLISHED, 0, 30, 1);
    fresh_step(&st, 1024);
    r = wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st);
    check((r == 0) && (st.pt_out_len == 1), "without NO_READ the same answer is fine");

    // The refusals, AGAIN, CLOSED.
    size = step_reply(TLS_STATE_HANDSHAKING, 0x101, 9, 0);
    fresh_step(&st, 1024);
    r = wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st);
    check((r == WIFI_TLS_AGAIN) && (st.ct_out_len == 9) && (st.state == WIFI_TLS_HANDSHAKING),
          "ESP_ERR_NO_MEM: AGAIN, with the output");
    size = step_reply(TLS_STATE_CLOSED, 0x101, 4, 7);
    fresh_step(&st, 1024);
    r = wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st);
    check((r == 0) && (st.state == WIFI_TLS_CLOSED) && (st.pt_out_len == 7), "CLOSED with NO_MEM: the last plaintext");
    size = step_reply(TLS_STATE_ESTABLISHED, 0x105, 0, 0);
    fresh_step(&st, 1024);
    r = wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st);
    check((r == WIFI_TLS_REFUSED) && (st.esp_err == 0x105) && nothing_out(&st),
          "ESP_ERR_NOT_FOUND (a stale tag) is refused, nothing taken");
    header(CMD_TLS_STEP);
    ((rpc_espcmd_resp *)reply.bytes)->esp_err = 0x105;
    fresh_step(&st, 1024);
    r = wifi_tls_take_step(reply.bytes, 8, SEQUENCE, &st);
    check((r == WIFI_TLS_REFUSED) && (st.esp_err == 0x105) && nothing_out(&st),
          "a bare 8-byte refusal is refused with its error");
    fresh_step(&st, 1024);
    ((rpc_espcmd_resp *)reply.bytes)->esp_err = 0;
    r = wifi_tls_take_step(reply.bytes, 8, SEQUENCE, &st);
    check((r == WIFI_TLS_INVALID) && nothing_out(&st), "... but a bare 8-byte answer with no error is invalid");
    int states = 0;
    for (int state = 4; state < 256; state++) {
        size = step_reply((uint8_t)state, 0, 5, 5);
        fresh_step(&st, 1024);
        states += (wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st) == WIFI_TLS_INVALID) && nothing_out(&st);
    }
    check(states == 252, "a state that does not exist is invalid (%d of 252)", states);

    // Random answers: bytes only whole, only from an answer that holds them,
    // plaintext only once established and never on a NO_READ step.
    world_reset(99, NULL, true);
    int broken = 0;
    for (int n = 0; n < 300000; n++) {
        for (int i = 0; i < 64; i++) {
            reply.bytes[i] = (uint8_t)world_rand(256);
        }
        res = (rpc_tls_step_resp *)reply.bytes;
        if (world_rand(2)) {
            res->hdr.command = CMD_TLS_STEP;
            res->hdr.sequence = SEQUENCE;
        }
        if (world_rand(2)) {
            res->esp_err = world_rand(2) ? 0 : (world_rand(2) ? -0x2700 : 0x101);
        }
        if (world_rand(2)) {
            res->state = world_rand(5);
        }
        if (world_rand(2)) {
            res->fail_reason = world_rand(2) ? 0 : world_rand(8);
        }
        if (world_rand(2)) {
            res->ct_out_len = world_rand(1100);
            res->pt_out_len = world_rand(4) ? world_rand(1100) : 0;
        }
        size = world_rand(2) ? (int)world_rand(CMD_BUF_SIZE + 1)
                             : (int)(offsetof(rpc_tls_step_resp, data) + res->ct_out_len + res->pt_out_len);
        if (size > CMD_BUF_SIZE) {
            size = CMD_BUF_SIZE;
        }
        uint16_t room = (uint16_t)world_rand(1100);
        fresh_step(&st, room);
        st.flags = world_rand(2) ? WIFI_TLS_STEP_NO_READ : 0;
        r = wifi_tls_take_step(reply.bytes, size, SEQUENCE, &st);
        if ((r == 0) || (r == WIFI_TLS_AGAIN)) {
            uint16_t ct = res->ct_out_len, pt = res->pt_out_len;
            bool failed = (res->state == TLS_STATE_FAILED);
            bool good = (ct <= room) && (pt <= room) && (size == (int)offsetof(rpc_tls_step_resp, data) + ct + pt) &&
                        (st.ct_out_len == ct) && (res->state <= TLS_STATE_FAILED) &&
                        (failed || (res->fail_reason == 0)) &&
                        !((st.flags & WIFI_TLS_STEP_NO_READ) && pt) &&
                        ((pt == 0) || failed || (res->state != TLS_STATE_HANDSHAKING)) &&
                        (memcmp(ct_out, res->data, ct) == 0) &&
                        (failed ? ((st.pt_out_len == 0) && untouched(pt_out))
                                : ((st.pt_out_len == pt) && (memcmp(pt_out, res->data + ct, pt) == 0)));
            broken += !good;
        } else {
            broken += !nothing_out(&st);
        }
    }
    check(broken == 0, "300000 random answers: bytes only whole, only from an answer that holds them (%d not)", broken);
}

static void test_take_others(void)
{
    printf("The checks on OPEN, CLOSE, STATS, CLOSE_ALL and SET_CA answers\n");
    int esp_err = 0;
    uint8_t reason = 0x77;

    header(CMD_TLS_OPEN);
    ((rpc_tls_open_resp *)reply.bytes)->slot = 1;
    int r = wifi_tls_take_open(reply.bytes, sizeof(rpc_tls_open_resp), SEQUENCE, 1, &esp_err, &reason);
    check(r == 0, "OPEN: 12 bytes, no error, slot 1 as asked: the session is open");
    int other = 0;
    const uint8_t slots[] = { 0, 2, 0xFF };
    for (unsigned k = 0; k < sizeof(slots); k++) {
        ((rpc_tls_open_resp *)reply.bytes)->slot = slots[k];
        other += (wifi_tls_take_open(reply.bytes, 12, SEQUENCE, 1, &esp_err, &reason) == WIFI_TLS_INVALID);
    }
    ((rpc_tls_open_resp *)reply.bytes)->slot = 1;
    check(other == 3, "OPEN: success naming another slot (0, 2, 0xFF) is malformed (%d of 3)", other);
    int wrong = 0;
    const int open_sizes[] = { 4, 9, 10, 11, 13, 16 };
    for (unsigned k = 0; k < sizeof(open_sizes) / sizeof(open_sizes[0]); k++) {
        wrong += (wifi_tls_take_open(reply.bytes, open_sizes[k], SEQUENCE, 1, &esp_err, &reason) == WIFI_TLS_INVALID);
    }
    check(wrong == 6, "OPEN: any size but 12 without an error is invalid (%d of 6)", wrong);
    check(wifi_tls_take_open(reply.bytes, 8, SEQUENCE, 1, &esp_err, &reason) == WIFI_TLS_INVALID,
          "OPEN: 8 bytes and no error is invalid");
    ((rpc_tls_open_resp *)reply.bytes)->esp_err = 0x103;
    ((rpc_tls_open_resp *)reply.bytes)->reason = TLS_OPEN_REASON_NO_CUSTOM_CA;
    r = wifi_tls_take_open(reply.bytes, sizeof(rpc_tls_open_resp), SEQUENCE, 1, &esp_err, &reason);
    check((r == WIFI_TLS_REFUSED) && (esp_err == 0x103) && (reason == WIFI_TLS_REASON_NO_CUSTOM_CA),
          "OPEN: refused, with reason 3 NO_CUSTOM_CA");
    // The refusal as the ESP32 sends it, slot 0xFF (tls_rpc.c cmd_tls_open).
    ((rpc_tls_open_resp *)reply.bytes)->slot = 0xFF;
    ((rpc_tls_open_resp *)reply.bytes)->reason = TLS_OPEN_REASON_NO_TIME;
    r = wifi_tls_take_open(reply.bytes, sizeof(rpc_tls_open_resp), SEQUENCE, 1, &esp_err, &reason);
    check((r == WIFI_TLS_REFUSED) && (reason == WIFI_TLS_REASON_NO_TIME),
          "OPEN refused as the ESP32 sends it (slot byte 0xFF) -> REFUSED with its reason, not malformed");
    ((rpc_tls_open_resp *)reply.bytes)->slot = 1;
    header(CMD_TLS_OPEN);
    ((rpc_espcmd_resp *)reply.bytes)->esp_err = 0x106;
    reason = 0;
    r = wifi_tls_take_open(reply.bytes, 8, SEQUENCE, 1, &esp_err, &reason);
    check((r == WIFI_TLS_REFUSED) && (esp_err == 0x106) && (reason == 0),
          "OPEN: a module without TLS answers a bare NOT_SUPPORTED: refused (-> 90)");
    ((rpc_header_t *)reply.bytes)->command = CMD_TLS_STEP;
    check(wifi_tls_take_open(reply.bytes, 12, SEQUENCE, 1, &esp_err, &reason) == WIFI_TLS_FOREIGN, "OPEN: a STEP answer is foreign");

    uint8_t notify[64];
    uint16_t notify_len = 0x7777;
    header(CMD_TLS_CLOSE);
    rpc_tls_close_resp *c = (rpc_tls_close_resp *)reply.bytes;
    c->ct_out_len = 24;
    for (int i = 0; i < 24; i++) {
        c->data[i] = (uint8_t)(0x90 + i);
    }
    r = wifi_tls_take_close(reply.bytes, 36, SEQUENCE, notify, sizeof(notify), &notify_len, &esp_err);
    check((r == 0) && (notify_len == 24) && (notify[0] == 0x90) && (notify[23] == 0xA7),
          "CLOSE: 12 + 24 bytes: the close_notify record arrives");
    check(wifi_tls_take_close(reply.bytes, 35, SEQUENCE, notify, sizeof(notify), &notify_len, &esp_err) ==
              WIFI_TLS_INVALID, "CLOSE: a byte short is invalid");
    check(wifi_tls_take_close(reply.bytes, 37, SEQUENCE, notify, sizeof(notify), &notify_len, &esp_err) ==
              WIFI_TLS_INVALID, "CLOSE: a byte long is invalid");
    check(wifi_tls_take_close(reply.bytes, 36, SEQUENCE, notify, 23, &notify_len, &esp_err) == WIFI_TLS_INVALID,
          "CLOSE: a record past the room is invalid");
    c->ct_out_len = 0;
    c->esp_err = 0x105;
    r = wifi_tls_take_close(reply.bytes, 12, SEQUENCE, notify, sizeof(notify), &notify_len, &esp_err);
    check((r == WIFI_TLS_REFUSED) && (esp_err == 0x105), "CLOSE: NOT_FOUND in the full answer is refused, with its error");
    r = wifi_tls_take_close(reply.bytes, 8, SEQUENCE, notify, sizeof(notify), &notify_len, &esp_err);
    check((r == WIFI_TLS_REFUSED) && (esp_err == 0x105), "CLOSE: ... and as a bare 8-byte answer");

    header(CMD_TLS_STATS);
    rpc_tls_stats_resp *s = (rpc_tls_stats_resp *)reply.bytes;
    s->heap_free = 100001;
    s->tls_version = 0x0303;
    s->ciphersuite = 0xC02F;
    s->group = 0x0017;
    s->state = TLS_STATE_ESTABLISHED;
    s->time_source = 2;
    s->entropy_source = 1;
    s->ems = 1;
    s->block_max_ms = 0x1234;
    for (int i = 0; i < 32; i++) {
        s->spki_sha256[i] = (uint8_t)(i * 7);
    }
    wifi_tls_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    r = wifi_tls_take_stats(reply.bytes, sizeof(rpc_tls_stats_resp), SEQUENCE, &stats, &esp_err);
    check((r == 0) && (stats.heap_free == 100001) && (stats.tls_version == 0x0303) && (stats.ciphersuite == 0xC02F) &&
          (stats.group == 0x0017) && (stats.time_source == 2) && (stats.entropy_source == 1) && (stats.ems == 1) &&
          (stats.block_max_ms == 0x1234) && (stats.spki_sha256[0] == 0) && (stats.spki_sha256[31] == 217),
          "STATS: 96 bytes, entropy, ems, block_max_ms and the SPKI hash arrive");
    const int stat_sizes[] = { 8, 60, 64, 95, 97 };
    wrong = 0;
    for (unsigned k = 0; k < sizeof(stat_sizes) / sizeof(stat_sizes[0]); k++) {
        memset(&stats, 0, sizeof(stats));
        wrong += (wifi_tls_take_stats(reply.bytes, stat_sizes[k], SEQUENCE, &stats, &esp_err) == WIFI_TLS_INVALID) &&
                 (stats.heap_free == 0);
    }
    check(wrong == 5, "STATS: any size but 96, the older 60 included, is invalid (%d of 5)", wrong);

    header(CMD_TLS_CLOSE_ALL);
    check(wifi_tls_take_close_all(reply.bytes, 8, SEQUENCE, &esp_err) == 0, "CLOSE_ALL: 8 bytes, no error");
    check(wifi_tls_take_close_all(reply.bytes, 12, SEQUENCE, &esp_err) == WIFI_TLS_INVALID, "CLOSE_ALL: 12 is invalid");
    ((rpc_espcmd_resp *)reply.bytes)->esp_err = 0x106;
    check(wifi_tls_take_close_all(reply.bytes, 8, SEQUENCE, &esp_err) == WIFI_TLS_REFUSED, "CLOSE_ALL: an error");

    struct { int err; int reject; int want; } ca[] = {
        { 0, 0, 0 },
        { -0x2180, 0, WIFI_TLS_REFUSED },
        { -2, 0, WIFI_TLS_REFUSED },
        { -1, 0, WIFI_TLS_INVALID },          // ESP_FAIL is not a parse code
        { 0, 1, WIFI_TLS_INVALID },
        { 0x102, 1, WIFI_TLS_REFUSED },
        { 0x102, 3, WIFI_TLS_REFUSED },
        { 0x102, 4, WIFI_TLS_REFUSED },
        { 0x102, 0, WIFI_TLS_INVALID },
        { 0x102, 2, WIFI_TLS_INVALID },       // reserved (was WEAK_SIG_HASH)
        { 0x102, 5, WIFI_TLS_INVALID },
        { 0x101, 0, WIFI_TLS_INVALID },       // NO_MEM
        { -0x2180, 1, WIFI_TLS_INVALID },
    };
    int good = 0;
    for (unsigned k = 0; k < sizeof(ca) / sizeof(ca[0]); k++) {
        header(CMD_TLS_SET_CA);
        ((rpc_tls_set_ca_resp *)reply.bytes)->esp_err = ca[k].err;
        ((rpc_tls_set_ca_resp *)reply.bytes)->reject = ca[k].reject;
        int e = 0x77;
        uint8_t j = 0x77;
        r = wifi_tls_take_set_ca(reply.bytes, 12, SEQUENCE, &e, &j);
        bool ok = (r == ca[k].want) && ((r != WIFI_TLS_REFUSED) || ((e == ca[k].err) && (j == ca[k].reject)));
        if (!ok) {
            printf("        SET_CA %d/%d gave %d\n", ca[k].err, ca[k].reject, r);
        }
        good += ok;
    }
    check(good == (int)(sizeof(ca) / sizeof(ca[0])), "SET_CA: only the three well formed answers pass");
    header(CMD_TLS_SET_CA);
    check(wifi_tls_take_set_ca(reply.bytes, 11, SEQUENCE, &esp_err, &reason) == WIFI_TLS_INVALID, "SET_CA: 11 bytes");
    ((rpc_espcmd_resp *)reply.bytes)->esp_err = 0x106;
    check(wifi_tls_take_set_ca(reply.bytes, 8, SEQUENCE, &esp_err, &reason) == WIFI_TLS_REFUSED && esp_err == 0x106,
          "SET_CA: a bare NOT_SUPPORTED is refused (-> 90)");
}

// Answers to the TLS calls, from what the request asked.
static void tls_maker(command_buf_t *rx, const uint8_t *req, int req_size, int kind)
{
    const rpc_header_t *request = (const rpc_header_t *)req;
    memset(rx->data, 0, sizeof(rx->data));
    rpc_header_t *hdr = (rpc_header_t *)rx->data;
    *hdr = *request;
    switch (request->command) {
        case CMD_TLS_OPEN:
            ((rpc_tls_open_resp *)rx->data)->slot = ((const rpc_tls_open_req *)req)->slot;
            rx->size = sizeof(rpc_tls_open_resp);
            break;
        case CMD_TLS_STEP: {
            rpc_tls_step_resp *r = (rpc_tls_step_resp *)rx->data;
            r->state = TLS_STATE_ESTABLISHED;
            r->alert_in = 0xFF;
            r->alert_out = 0xFF;
            r->ct_out_len = 5;
            r->pt_out_len = 4;
            for (int i = 0; i < 9; i++) {
                r->data[i] = (uint8_t)(i + 1);
            }
            rx->size = offsetof(rpc_tls_step_resp, data) + 9;
            break;
        }
        case CMD_TLS_CLOSE:
            rx->size = sizeof(rpc_tls_close_resp);
            break;
        case CMD_TLS_STATS:
            ((rpc_tls_stats_resp *)rx->data)->heap_free = 123456;
            rx->size = sizeof(rpc_tls_stats_resp);
            break;
        case CMD_TLS_SET_CA:
            rx->size = sizeof(rpc_tls_set_ca_resp);
            break;
        default:
            rx->size = sizeof(rpc_espcmd_resp);
            break;
    }
}

static void late_maker(command_buf_t *rx, const uint8_t *req, int req_size, int kind)
{
    tls_maker(rx, req, req_size, kind);
    ((rpc_header_t *)rx->data)->sequence++;
}

// The request that went out, as the world recorded it: "transmit n: xx xx ...".
static std::string sent(void)
{
    size_t at = world_trace.find("transmit ");
    if (at == std::string::npos) {
        return "";
    }
    size_t end = world_trace.find('\n', at);
    return world_trace.substr(at, end - at);
}

static std::string hex(const uint8_t *bytes, int n)
{
    std::string out;
    char b[4];
    for (int i = 0; i < n; i++) {
        snprintf(b, sizeof(b), " %02x", bytes[i]);
        out += b;
    }
    return out;
}

static int cancel_calls;
static int cancel_after;
static bool cancel_after_n(void *context)
{
    return ++cancel_calls >= cancel_after;
}

static int count(const std::string &text, const std::string &what)
{
    int n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        n++;
    }
    return n;
}

static void test_calls(void)
{
    printf("The TLS calls through the channel (wire layouts, byte for byte)\n");
    int esp_err = 0x55;
    uint8_t reason = 0x55;

    uint8_t pin[32];
    for (int i = 0; i < 32; i++) {
        pin[i] = (uint8_t)(0xE0 + i);
    }
    wifi_tls_open_t o;
    memset(&o, 0, sizeof(o));
    o.trust = WIFI_TLS_TRUST_PIN;
    o.slot = 1;
    o.tag = 0x0203;
    o.opts = WIFI_TLS_OPT_REQUIRE_TLS13;
    o.time_source = WIFI_TLS_TIME_NIOS;
    o.unix_time = 0x6789ABCD;
    o.ca_tag = 0x0405;
    o.pin = pin;
    o.sni = "a.b";
    o.sni_len = 3;
    world_reset(1, tls_maker, true);
    uint16_t seq = sequence_nr;
    int r = wifi_tls_open(&o, 5000, NULL, &esp_err, &reason);
    const uint8_t fixed[] = { CMD_TLS_OPEN, WIFI_DEDICATED_THREAD, (uint8_t)seq, (uint8_t)(seq >> 8), 2, 3, 1, 1,
                              0xCD, 0xAB, 0x89, 0x67, 0x03, 0x02, 0x01, 0, 0x05, 0x04, 0, 0 };
    std::string want = "transmit 55:" + hex(fixed, 20) + hex(pin, 32) + hex((const uint8_t *)"a.b", 3);
    check((r == 0) && (esp_err == 0) && (sent() == want),
          "OPEN: hdr, trust, sni_len, slot, time_source, unix_time, tag, opts, 0, ca_tag, 0 0, pin[32], sni");
    o.pin = NULL;
    o.sni = NULL;
    o.sni_len = 0;
    o.trust = 0;
    world_reset(2, tls_maker, true);
    r = wifi_tls_open(&o, 5000, NULL, &esp_err, &reason);
    std::string s = sent();
    check((r == 0) && (s.compare(0, 12, "transmit 52:") == 0) && (count(s, " 00") >= 36),
          "OPEN: no name, no pin: 52 bytes, the pin as zeros");
    o.sni_len = 254;
    world_reset(3, tls_maker, true);
    r = wifi_tls_open(&o, 5000, NULL, &esp_err, &reason);
    check((r == WIFI_TLS_TOO_LONG) && (world_trace.find("getbuffer") == std::string::npos), "OPEN: 254 bytes of name send nothing");

    wifi_tls_step_t st;
    uint8_t ct_in[3] = { 0xA1, 0xA2, 0xA3 };
    uint8_t pt_in[2] = { 0xB1, 0xB2 };
    fresh_step(&st, 1024);
    st.flags = WIFI_TLS_STEP_NO_READ;
    st.ct_in = ct_in;
    st.ct_in_len = 3;
    st.pt_in = pt_in;
    st.pt_in_len = 2;
    world_reset(6, tls_maker, true);
    seq = sequence_nr;
    r = wifi_tls_step(1, 0x0A0B, &st, 5000, NULL);
    uint8_t expect_step[] = { CMD_TLS_STEP, WIFI_DEDICATED_THREAD, (uint8_t)seq, (uint8_t)(seq >> 8), 1, 1, 0x0B,
                              0x0A, 3, 0, 2, 0, 0xA1, 0xA2, 0xA3, 0xB1, 0xB2 };
    check((r == WIFI_TLS_INVALID) && (sent() == "transmit 17:" + hex(expect_step, sizeof(expect_step))),
          "STEP: slot, flags, tag, ct_in_len, pt_in_len, ciphertext, plaintext (and the maker's plaintext on NO_READ is refused)");
    fresh_step(&st, 1024);
    st.ct_in = ct_in;
    st.ct_in_len = 3;
    world_reset(7, tls_maker, true);
    r = wifi_tls_step(0, 1, &st, 5000, NULL);
    check((r == 0) && (st.ct_out_len == 5) && (st.pt_out_len == 4) && (ct_out[0] == 1) && (pt_out[0] == 6),
          "STEP: the answer is split into ciphertext and plaintext");
    static uint8_t big[WIFI_TLS_STEP_MAX_IN + 1];
    fresh_step(&st, 1024);
    st.ct_in = big;
    st.ct_in_len = 1537;
    world_reset(8, tls_maker, true);
    r = wifi_tls_step(0, 1, &st, 5000, NULL);
    check((r == WIFI_TLS_TOO_LONG) && (world_trace.find("getbuffer") == std::string::npos), "STEP: 1537 bytes in send nothing");
    st.ct_in_len = 1536;
    world_reset(8, tls_maker, true);
    r = wifi_tls_step(0, 1, &st, 5000, NULL);
    check((r == 0) && (sent().compare(0, 15, "transmit 1548: ") == 0), "STEP: 1536 bytes in go in one request");

    uint8_t notify[64];
    uint16_t notify_len;
    world_reset(9, tls_maker, true);
    seq = sequence_nr;
    r = wifi_tls_close(1, 0xBEEF, notify, sizeof(notify), &notify_len, &esp_err, 1000, NULL);
    uint8_t expect_close[] = { CMD_TLS_CLOSE, WIFI_DEDICATED_THREAD, (uint8_t)seq, (uint8_t)(seq >> 8), 1, 0,
                               0xEF, 0xBE };
    check((r == 0) && (notify_len == 0) && (sent() == "transmit 8:" + hex(expect_close, sizeof(expect_close))),
          "CLOSE: slot, 0, tag (8 bytes)");
    wifi_tls_stats_t stats;
    world_reset(10, tls_maker, true);
    seq = sequence_nr;
    r = wifi_tls_stats(WIFI_TLS_SLOT_GLOBAL, 0, &stats, &esp_err, 1000, NULL);
    uint8_t expect_stats[] = { CMD_TLS_STATS, WIFI_DEDICATED_THREAD, (uint8_t)seq, (uint8_t)(seq >> 8), 0xFF, 0,
                               0, 0 };
    check((r == 0) && (stats.heap_free == 123456) && (sent() == "transmit 8:" + hex(expect_stats, 8)),
          "STATS: the module as a whole is slot $FF, tag 0");
    world_reset(11, tls_maker, true);
    seq = sequence_nr;
    r = wifi_tls_close_all(&esp_err, 500);
    uint8_t expect_all[] = { CMD_TLS_CLOSE_ALL, WIFI_DEDICATED_THREAD, (uint8_t)seq, (uint8_t)(seq >> 8) };
    check((r == 0) && (sent() == "transmit 4:" + hex(expect_all, 4)), "CLOSE_ALL: the header only");
    uint8_t der[5] = { 0x30, 0x82, 0x01, 0x02, 0x03 };
    uint8_t reject;
    world_reset(12, tls_maker, true);
    seq = sequence_nr;
    r = wifi_tls_set_ca(der, 5, 0x1122, &esp_err, &reject, 5000, NULL);
    uint8_t expect_ca[] = { CMD_TLS_SET_CA, WIFI_DEDICATED_THREAD, (uint8_t)seq, (uint8_t)(seq >> 8), 5, 0,
                            0x22, 0x11, 0x30, 0x82, 0x01, 0x02, 0x03 };
    check((r == 0) && (sent() == "transmit 13:" + hex(expect_ca, sizeof(expect_ca))), "SET_CA: der_len, ca_tag, der");
    static uint8_t big_ca[WIFI_TLS_CA_MAX + 1];
    world_reset(12, tls_maker, true);
    r = wifi_tls_set_ca(big_ca, WIFI_TLS_CA_MAX + 1, 1, &esp_err, &reject, 5000, NULL);
    check((r == WIFI_TLS_TOO_LONG) && (world_trace.find("getbuffer") == std::string::npos), "SET_CA: 1473 bytes send nothing");
    world_reset(12, tls_maker, true);
    r = wifi_tls_set_ca(big_ca, WIFI_TLS_CA_MAX, 1, &esp_err, &reject, 5000, NULL);
    check((r == 0) && (sent().compare(0, 15, "transmit 1480: ") == 0), "SET_CA: 1472 bytes go in one request");

    // The waits: each call its caller's timeout; with a cancel, in slices.
    world_reset(13, late_maker, true);
    fresh_step(&st, 1024);
    r = wifi_tls_step(0, 1, &st, 5000, NULL);
    check((r == WIFI_TLS_BUSY) && (world_trace.find("receive 1000") != std::string::npos) && !dedicated_waiting,
          "no cancel: one wait of the whole timeout (5 s = 1000 ticks), then BUSY");
    world_reset(14, late_maker, true);
    cancel_calls = 0;
    cancel_after = 1000;
    wifi_tls_cancel_t never = { cancel_after_n, NULL };
    fresh_step(&st, 1024);
    r = wifi_tls_step(0, 1, &st, 1000, &never);
    check((r == WIFI_TLS_BUSY) && (count(world_trace, "receive 20") == 10) && (cancel_calls == 10) &&
          (world_trace.find("print: TLS Step: no reply.") != std::string::npos),
          "a cancel that never fires: ten 100-ms slices, asked after each (%d), then BUSY", cancel_calls);
    world_reset(15, late_maker, true);
    cancel_calls = 0;
    cancel_after = 3;
    wifi_tls_cancel_t soon = { cancel_after_n, NULL };
    fresh_step(&st, 1024);
    r = wifi_tls_step(0, 1, &st, 5000, &soon);
    // The first slice brings the late (foreign) answer and is skipped; three empty ones follow.
    check((r == WIFI_TLS_BUSY) && (cancel_calls == 3) && (count(world_trace, "receive 20") == 4) &&
          (world_trace.find("print: TLS Step: cancelled.") != std::string::npos) && !dedicated_waiting &&
          (world_queued() == 0) && (st.ct_out_len == 0) && (st.pt_out_len == 0) && untouched(ct_out) &&
          untouched(pt_out),
          "a cancel after 3 slices ends the call with BUSY at once, frees the channel, takes nothing");
    // The answer arrives just as the cancel fires: the call has given up, so
    // it is dropped, not taken.
    world_reset(20, late_maker, true);
    fresh_step(&st, 1024);
    struct arriving_right {
        static void maker(command_buf_t *rx, const uint8_t *req, int req_size, int kind)
        {
            tls_maker(rx, req, req_size, kind);
        }
        static bool now(void *context)
        {
            world_reset_maker(maker);
            world_deliver_now();
            return true;
        }
    };
    wifi_tls_cancel_t race = { arriving_right::now, NULL };
    r = wifi_tls_step(0, 1, &st, 5000, &race);
    check((r == WIFI_TLS_BUSY) && (st.ct_out_len == 0) && untouched(ct_out) && (world_queued() == 0) &&
              !dedicated_waiting,
          "an answer that arrives as the cancel fires is dropped: BUSY, nothing taken, the queue emptied (%d)", r);

    world_reset(16, tls_maker, true);
    cancel_calls = 0;
    cancel_after = 1;
    fresh_step(&st, 1024);
    r = wifi_tls_step(0, 1, &st, 5000, &soon);
    check((r == 0) && (cancel_calls == 0), "an answer inside the first slice is taken; cancel is not even asked");
    world_reset(17, tls_maker, true);
    world_ready = false;
    r = wifi_tls_close_all(&esp_err, 500);
    check((r == WIFI_TLS_NO_MODULE) && world_trace.empty(), "without the module application nothing is tried");
    world_reset(18, tls_maker, true);
    dedicated_waiting = true;
    r = wifi_tls_stats(0, 1, &stats, &esp_err, 1000, NULL);
    check((r == WIFI_TLS_UNSENT) && dedicated_waiting, "another request outstanding: UNSENT at once");
}

int main(void)
{
    test_take_step();
    test_take_others();
    test_calls();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
