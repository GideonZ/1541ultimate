/*
 * Host test of wifi_cmd.cc with stubs for the UART and the RTOS: what a request
 * answers when the UART has no free TX buffer. The RPCs answer 0 for success, so
 * they must answer -1; wifi_detect answers a success flag, so pdFALSE. And how a
 * request ends whose reply comes in time, as the wait times out, late, or never.
 */
#include <stdio.h>
#include <string.h>
#include "wifi_cmd.h"
#include "wifi_random.h"

Esp32 :: Esp32() : uart(NULL) {}
Esp32 esp32;
static DmaUART uart;
static command_buf_t txbuf, rxbuf;
static int failures;
static int queued;                  // frames the ISR passed to the receive queue
static command_buf_t *notify_value; // the notification value of the waiting task
static bool notify_pending;
static TickType_t first_wait;       // the ticks of the first wait of a request
static int waits;
enum reply_time { NEVER, ON_TIME, AT_TIMEOUT, AFTER_RELEASE };
static reply_time reply_at;
static bool timed_out;
static bool module_ready;
static TickType_t buffer_wait;      // the ticks the last GetBuffer was given
// What the module answers: a GET_RANDOM answer, or one of the power mode (the default)
static bool random_answer;
static int random_err;
static uint8_t random_source = 1;
static uint16_t random_length;
static int random_command = -1;    // when not -1, the command the answer carries instead

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

BaseType_t DmaUART::GetBuffer(command_buf_t **b, TickType_t t)
{
    buffer_wait = t;
    if (!buffers_free) {
        return pdFALSE;
    }
    buffers_free--;
    *b = &txbuf;
    return pdTRUE;
}

void DmaUART::FreeBuffer(command_buf_t *b)
{
    buffers_free++;
}

extern "C" BaseType_t cmd_buffer_received_isr(command_buf_context_t *c, command_buf_t *b, BaseType_t *w) { queued++; return pdFALSE; }

BaseType_t wifi_rx_isr(command_buf_context_t *context, command_buf_t *buf, BaseType_t *w);

// The module answers the request with this header
static void answer(rpc_header_t hdr)
{
    if (random_answer) {
        rpc_get_random_resp *rnd = (rpc_get_random_resp *)rxbuf.data;
        rnd->hdr = hdr;
        if (random_command >= 0) {
            rnd->hdr.command = (uint8_t)random_command;
        }
        rnd->esp_err = random_err;
        rnd->source = random_source;
        rnd->length = random_length;
        for (int i = 0; i < random_length; i++) {
            rnd->data[i] = (uint8_t)(0xA0 + i);
        }
        rxbuf.size = offsetof(rpc_get_random_resp, data) + random_length;
        BaseType_t w;
        wifi_rx_isr(NULL, &rxbuf, &w);
        return;
    }
    rpc_get_power_mode_resp *resp = (rpc_get_power_mode_resp *)rxbuf.data;
    resp->hdr = hdr;
    resp->esp_err = 0;
    resp->mode = 3;
    resp->last_state = 1;
    BaseType_t w;
    wifi_rx_isr(NULL, &rxbuf, &w);
}

BaseType_t xTaskNotifyFromISR(TaskHandle_t t, uint32_t v, int a, BaseType_t *w)
{
    notify_value = (command_buf_t *)v;
    notify_pending = true;
    return pdTRUE;
}

// The module answers the request just sent: during the wait (ON_TIME), after the
// wait timed out but before the slot is released (AT_TIMEOUT), so only a drain
// after the release can take it, or during that drain (AFTER_RELEASE). As in
// FreeRTOS, the value is written whether or not a notification was pending.
BaseType_t xTaskNotifyWait(uint32_t a, uint32_t b, uint32_t *v, TickType_t t)
{
    if (!waits++) {
        first_wait = t;
    }
    if ((t && reply_at == ON_TIME) || (!t && reply_at == AFTER_RELEASE)) {
        reply_at = NEVER;
        answer(*(rpc_header_t *)txbuf.data);
    }
    *v = (uint32_t)notify_value;
    if (t && reply_at == AT_TIMEOUT) {
        reply_at = NEVER;
        timed_out = true;
        return pdFALSE;
    }
    if (!notify_pending) {
        return pdFALSE;
    }
    notify_pending = false;
    return pdTRUE;
}

bool wifi_module_ready(void)
{
    return module_ready;
}

extern "C" void enter_safe_section(void)
{
    if (timed_out) {
        timed_out = false;
        answer(*(rpc_header_t *)txbuf.data);
    }
}

// Claims every reply slot and answers them all, so they are free again
static bool all_slots_free(void)
{
    rpc_header_t hdr[WIFI_REPLY_SLOTS];
    int n = 0;
    while (n < WIFI_REPLY_SLOTS && wifi_reply_register(&hdr[n])) {
        n++;
    }
    for (int i = 0; i < n; i++) {
        answer(hdr[i]);
    }
    notify_pending = false;
    return n == WIFI_REPLY_SLOTS;
}


static void test_no_tx_buffer(void)
{
    uint8_t mode, last;
    uart.buffers_free = 0;
    uart.transmitted = 0;
    CHECK(wifi_get_power_mode(&mode, &last) == -1);
    CHECK(wifi_set_power_mode(1) == -1);
    CHECK(wifi_set_wake_on_wifi(1) == -1);
    CHECK(uart.transmitted == 0);

    uint16_t major, minor;
    char str[32];
    CHECK(wifi_detect(&major, &minor, str, sizeof(str)) == pdFALSE);
    CHECK(uart.transmitted == 0);
}

// A starved buffer must not use up reply slots either
static void test_no_tx_buffer_keeps_slots(void)
{
    uart.buffers_free = 0;
    for (int i = 0; i < WIFI_REPLY_SLOTS + 1; i++) {
        uint8_t mode, last;
        CHECK(wifi_get_power_mode(&mode, &last) == -1);
    }
    CHECK(all_slots_free());
}

// With a buffer, a request that gets no reply is sent, is -1 too and releases its slot
static void test_no_reply(void)
{
    uint8_t mode, last;
    for (int i = 0; i < 2 * WIFI_REPLY_SLOTS; i++) {
        uart.buffers_free = 1;
        uart.transmitted = 0;
        waits = 0;
        CHECK(wifi_get_power_mode(&mode, &last) == -1);
        CHECK(uart.transmitted == 1);
        CHECK(first_wait == pdMS_TO_TICKS(WIFI_REPLY_TIMEOUT_MS));
    }
    CHECK(all_slots_free());
}

// The reply arrives in time or as the wait times out: the caller gets it, not -1,
// and frees it
static void test_reply(reply_time when)
{
    uint8_t mode = 0, last = 0;
    uart.buffers_free = 1;
    uart.transmitted = 0;
    queued = 0;
    waits = 0;
    reply_at = when;
    CHECK(wifi_get_power_mode(&mode, &last) == 0);
    CHECK(mode == 3 && last == 1);
    CHECK(uart.transmitted == 1);
    CHECK(first_wait == pdMS_TO_TICKS(WIFI_REPLY_TIMEOUT_MS));
    CHECK(uart.buffers_free == 1);  // the reply buffer (the stub keeps the TX buffer)
    CHECK(queued == 0);
    CHECK(!notify_pending);
    CHECK(all_slots_free());
}

// The reply arrives after the slot is released: it is not for the caller any more
static void test_reply_after_release(void)
{
    uint8_t mode, last;
    uart.buffers_free = 1;
    uart.transmitted = 0;
    queued = 0;
    reply_at = AFTER_RELEASE;
    CHECK(wifi_get_power_mode(&mode, &last) == -1);
    CHECK(uart.transmitted == 1);
    CHECK(queued == 1);
    CHECK(!notify_pending);
    CHECK(all_slots_free());
}

// wifi_get_random: the request, and each way it ends
static void random_setup(uint16_t len)
{
    random_answer = true;
    random_err = 0;
    random_source = 1;
    random_length = len;
    module_ready = true;
    uart.buffers_free = 1;
    uart.transmitted = 0;
    queued = 0;
    waits = 0;
    buffer_wait = 0;
    reply_at = NEVER;
    memset(txbuf.data, 0, 16);
}

static void test_random(void)
{
    uint8_t out[16];

    // No module application: nothing is sent and nothing waits
    random_setup(8);
    module_ready = false;
    CHECK(wifi_get_random(out, 8) == WIFI_RANDOM_NO_MODULE);
    CHECK(uart.transmitted == 0 && waits == 0);

    // No TX buffer within 20 ms: busy, and no slot is used up
    for (int i = 0; i < WIFI_REPLY_SLOTS + 1; i++) {
        random_setup(8);
        uart.buffers_free = 0;
        CHECK(wifi_get_random(out, 8) == WIFI_RANDOM_BUSY);
        CHECK(buffer_wait == 4 && uart.transmitted == 0);
    }
    CHECK(all_slots_free());
    random_answer = false; // all_slots_free answers in the module's other format

    // No answer: the request went out, was waited for 200 ms, and its slot is free again
    for (int i = 0; i < 2 * WIFI_REPLY_SLOTS; i++) {
        random_setup(8);
        memset(out, 0, sizeof(out));
        CHECK(wifi_get_random(out, 8) == WIFI_RANDOM_BUSY);
        CHECK(uart.transmitted == 1);
        CHECK(first_wait == pdMS_TO_TICKS(200));
        CHECK(out[0] == 0);
    }
    CHECK(all_slots_free());

    // An answer in time, or as the wait times out: the bytes, once, and the buffer freed
    reply_time when[2] = { ON_TIME, AT_TIMEOUT };
    for (int i = 0; i < 2; i++) {
        random_setup(8);
        reply_at = when[i];
        memset(out, 0, sizeof(out));
        CHECK(wifi_get_random(out, 8) == 0);
        CHECK(out[0] == 0xA0 && out[7] == 0xA7 && out[8] == 0);
        rpc_get_random_req *req = (rpc_get_random_req *)txbuf.data;
        CHECK(req->hdr.command == CMD_GET_RANDOM && req->length == 8);
        CHECK(uart.transmitted == 1 && uart.buffers_free == 1 && queued == 0 && !notify_pending);
        random_answer = false;
        CHECK(all_slots_free());
    }

    // The module refuses, is busy, answers short or without a source: no byte is handed out
    int errs[4] = { 0x105, RPC_ERR_ENTROPY_BUSY, 0, 0 };
    int want[4] = { WIFI_RANDOM_REFUSED, WIFI_RANDOM_BUSY, WIFI_RANDOM_INVALID, WIFI_RANDOM_REFUSED };
    for (int i = 0; i < 4; i++) {
        random_setup(8);
        reply_at = ON_TIME;
        random_err = errs[i];
        random_length = (i == 2) ? 4 : 8; // asked for 8
        random_source = (i == 3) ? ENTROPY_NONE : 1;
        memset(out, 0, sizeof(out));
        CHECK(wifi_get_random(out, 8) == want[i]);
        CHECK(out[0] == 0);
        CHECK(uart.buffers_free == 1);
    }

    // An answer to another command, with this request's sequence: not ours, no byte is handed out
    random_setup(8);
    reply_at = ON_TIME;
    random_command = CMD_WIFI_GETMAC;
    memset(out, 0, sizeof(out));
    CHECK(wifi_get_random(out, 8) == WIFI_RANDOM_FOREIGN);
    CHECK(out[0] == 0 && uart.buffers_free == 1);
    random_command = -1;

    // Every reply slot taken: busy, nothing is sent, and the TX buffer is given back
    {
        rpc_header_t held[WIFI_REPLY_SLOTS];
        random_setup(8);
        for (int i = 0; i < WIFI_REPLY_SLOTS; i++) {
            CHECK(wifi_reply_register(&held[i]));
        }
        memset(out, 0, sizeof(out));
        CHECK(wifi_get_random(out, 8) == WIFI_RANDOM_BUSY);
        CHECK(uart.transmitted == 0 && uart.buffers_free == 1 && out[0] == 0);
        random_answer = false;
        for (int i = 0; i < WIFI_REPLY_SLOTS; i++) {
            answer(held[i]);
        }
        notify_pending = false;
        CHECK(all_slots_free());
    }

    // An answer after the slot was released is not the caller's any more
    random_setup(8);
    reply_at = AFTER_RELEASE;
    memset(out, 0, sizeof(out));
    CHECK(wifi_get_random(out, 8) == WIFI_RANDOM_BUSY);
    CHECK(out[0] == 0 && queued == 1 && !notify_pending);
    random_answer = false;
    CHECK(all_slots_free());
}

int main(void)
{
    esp32.uart = &uart;
    test_no_tx_buffer();
    test_no_tx_buffer_keeps_slots();
    test_no_reply();
    test_reply(ON_TIME);
    test_reply(AT_TIMEOUT);
    test_reply_after_release();
    test_random();
    printf(failures ? "wifi_cmd: %d FAILED\n" : "wifi_cmd: all passed\n", failures);
    return failures ? 1 : 0;
}
