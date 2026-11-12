/*
 * Host test of wifi_cmd.cc with stubs for the UART and the RTOS: what a request
 * answers when the UART has no free TX buffer. The RPCs answer 0 for success, so
 * they must answer -1; wifi_detect answers a success flag, so pdFALSE. And how a
 * request ends whose reply comes in time, as the wait times out, late, or never.
 */
#include <stdio.h>
#include "wifi_cmd.h"

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

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

BaseType_t DmaUART::GetBuffer(command_buf_t **b, TickType_t t)
{
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

int main(void)
{
    esp32.uart = &uart;
    test_no_tx_buffer();
    test_no_tx_buffer_keeps_slots();
    test_no_reply();
    test_reply(ON_TIME);
    test_reply(AT_TIMEOUT);
    test_reply_after_release();
    printf(failures ? "wifi_cmd: %d FAILED\n" : "wifi_cmd: all passed\n", failures);
    return failures ? 1 : 0;
}
