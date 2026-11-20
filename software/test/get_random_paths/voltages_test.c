/*
 * voltages_test.c
 *
 * The Elite II / C64 Ultimate module's answer to CMD_GET_VOLTAGES (cmd_get_voltages(), cut
 * out of its rpc_dispatch.c as it is): a read that fails says so and carries no stale bytes.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "rpc_calls.h"

typedef int esp_err_t;
typedef int BaseType_t;
#define UART_NUM_0 0
#define UART_NUM_1 1
#define ESP_LOGI(tag, ...) ((void)0)
static const char *TAG = "test";
typedef struct { uint8_t data[1600]; int bufnr; int size; } command_buf_t;
BaseType_t my_uart_transmit_packet(uint8_t uart_num, command_buf_t *buf);
esp_err_t read_adc_channels(uint16_t *adc_data);

#include "voltages_extract.c"

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __func__, __LINE__, what); failures++; } } while (0)

static int read_result;
static int sent;
esp_err_t read_adc_channels(uint16_t *adc_data)
{
    if (read_result == 0) {
        for (int i = 0; i < 7; i++) adc_data[i] = 1000 + i;
    }
    return read_result;
}
BaseType_t my_uart_transmit_packet(uint8_t uart_num, command_buf_t *buf) { sent++; return 1; }

int main(void)
{
    static command_buf_t buf;
    rpc_get_voltages_resp *resp = (rpc_get_voltages_resp *)buf.data;

    memset(buf.data, 0xAA, sizeof(buf.data));
    read_result = 0x107;
    cmd_get_voltages(&buf);
    CHECK(resp->esp_err == 0x107, "the read's error is the answer's");
    CHECK((resp->vbus == 0) && (resp->vusb == 0), "and no stale bytes in the voltages");
    CHECK((sent == 1) && (buf.size == sizeof(rpc_get_voltages_resp)), "sent, whole");

    memset(buf.data, 0xAA, sizeof(buf.data));
    read_result = 0;
    cmd_get_voltages(&buf);
    CHECK((resp->esp_err == 0) && (resp->vbus == 1000) && (resp->vusb == 1006), "a good read");

    if (failures) {
        printf("getrandompaths voltages: %d check(s) failed\n", failures);
        return 1;
    }
    printf("getrandompaths voltages: all checks passed\n");
    return 0;
}
