/*
 * Host test of wifi_cmd.cc with stubs for the UART and the RTOS: what a request
 * answers when the UART has no free TX buffer. The RPCs answer 0 for success, so
 * they must answer -1; wifi_detect answers a success flag, so pdFALSE.
 */
#include <stdio.h>
#include "wifi_cmd.h"

Esp32 :: Esp32() : uart(NULL) {}
Esp32 esp32;
static DmaUART uart;
static command_buf_t txbuf;
static int failures;

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

extern "C" BaseType_t cmd_buffer_received_isr(command_buf_context_t *c, command_buf_t *b, BaseType_t *w) { return pdFALSE; }

// No reply ever arrives
BaseType_t xTaskNotifyFromISR(TaskHandle_t t, uint32_t v, int a, BaseType_t *w) { return pdFALSE; }
BaseType_t xTaskNotifyWait(uint32_t a, uint32_t b, uint32_t *v, TickType_t t) { return pdFALSE; }


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
    uart.buffers_free = 1;
    rpc_header_t hdr;
    CHECK(wifi_reply_register(&hdr));
}

// With a buffer, a request that gets no reply is -1 too and releases its slot
static void test_no_reply(void)
{
    uint8_t mode, last;
    uart.buffers_free = 1;
    for (int i = 0; i < 2 * WIFI_REPLY_SLOTS; i++) {
        uart.buffers_free = 1;
        CHECK(wifi_get_power_mode(&mode, &last) == -1);
    }
}

int main(void)
{
    esp32.uart = &uart;
    test_no_tx_buffer();
    test_no_tx_buffer_keeps_slots();
    test_no_reply();
    printf(failures ? "wifi_cmd: %d FAILED\n" : "wifi_cmd: all passed\n", failures);
    return failures ? 1 : 0;
}
