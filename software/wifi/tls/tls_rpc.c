/* tls_rpc.c: the CMD_TLS_* commands on the module side. They run on their own task, so a handshake never blocks
 * the dispatcher (Wi-Fi TX); the dispatcher hands them over with tls_rpc_dispatch(). */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "driver/uart.h"
#include "rpc_calls.h"
#include "my_uart.h"
#include "wifi_modem.h"
#include "tls_engine.h"
#include "tls_rpc.h"

#define UART_CHAN UART_NUM_1

void cmd_not_implemented(command_buf_t *buf);

static QueueHandle_t tls_queue;
static volatile uint16_t block_max_ms; // the longest dispatcher iteration since boot, read by the TLS task

void cmd_tls_open(command_buf_t *buf)
{
    rpc_tls_open_req *req = (rpc_tls_open_req *)buf->data;
    rpc_tls_open_resp *resp = (rpc_tls_open_resp *)buf->data;

    // tls_open() copies the pin and the host name before the response overwrites them
    uint8_t slot = TLS_SLOT_GLOBAL;
    uint8_t reason = TLS_OPEN_REASON_NONE;
    int err = ESP_ERR_INVALID_ARG;
    if ((buf->size >= (int)sizeof(rpc_tls_open_req)) && (buf->size == (int)sizeof(rpc_tls_open_req) + req->sni_len)) {
        tls_open_params_t p = {
            .trust = req->trust, .opts = req->opts, .time_source = req->time_source, .ca_tag = req->ca_tag,
            .unix_time = req->unix_time, .pin = req->pin, .sni = req->sni, .sni_len = req->sni_len,
        };
        err = tls_open(req->slot, req->tag, &p, &reason);
        if (err == ESP_OK) {
            slot = req->slot;
        }
    }
    memset(&resp->esp_err, 0, sizeof(rpc_tls_open_resp) - sizeof(rpc_header_t));
    resp->esp_err = err;
    resp->slot = slot;
    resp->reason = reason;
    buf->size = sizeof(rpc_tls_open_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_tls_step(command_buf_t *buf)
{
    rpc_tls_step_req *req = (rpc_tls_step_req *)buf->data;
    rpc_tls_step_resp *resp = (rpc_tls_step_resp *)buf->data;

    // The response overlays the request: tls_step() consumes ct_in and pt_in before it writes resp->data
    uint16_t ct_out_len = 0;
    uint16_t pt_out_len = 0;
    tls_step_result_t result = { .state = TLS_STATE_CLOSED, .alert_in = TLS_ALERT_NONE, .alert_out = TLS_ALERT_NONE };
    int err = ESP_ERR_INVALID_ARG;
    if (buf->size >= (int)sizeof(rpc_tls_step_req)) {
        int ct_in_len = req->ct_in_len;
        int pt_in_len = req->pt_in_len;
        if (buf->size == (int)sizeof(rpc_tls_step_req) + ct_in_len + pt_in_len) {
            err = tls_step(req->slot, req->tag, req->flags, req->data, ct_in_len, req->data + ct_in_len, pt_in_len,
                           resp->data, &ct_out_len, &pt_out_len, &result);
        } else {
            tls_session_stats_t stats;
            tls_get_stats(req->slot, req->tag, &stats);
            result.state = stats.state;
        }
    }
    memset(&resp->esp_err, 0, sizeof(rpc_tls_step_resp) - sizeof(rpc_header_t));
    resp->esp_err = err;
    resp->state = result.state;
    resp->alert_in = result.alert_in;
    resp->ct_out_len = ct_out_len;
    resp->pt_out_len = pt_out_len;
    resp->alert_out = result.alert_out;
    resp->fail_reason = result.fail_reason;
    resp->verify_flags = result.verify_flags;
    buf->size = sizeof(rpc_tls_step_resp) + ct_out_len + pt_out_len;
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_tls_close(command_buf_t *buf)
{
    rpc_tls_session_req *req = (rpc_tls_session_req *)buf->data;
    rpc_tls_close_resp *resp = (rpc_tls_close_resp *)buf->data;

    // The close_notify goes to resp->data, which starts past the request
    uint16_t ct_out_len = 0;
    int err = ESP_ERR_INVALID_ARG;
    if (buf->size >= (int)sizeof(rpc_tls_session_req)) {
        err = tls_close(req->slot, req->tag, resp->data, &ct_out_len);
    }
    memset(&resp->esp_err, 0, sizeof(rpc_tls_close_resp) - sizeof(rpc_header_t));
    resp->esp_err = err;
    resp->ct_out_len = ct_out_len;
    buf->size = sizeof(rpc_tls_close_resp) + ct_out_len;
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_tls_stats(command_buf_t *buf)
{
    rpc_tls_session_req *req = (rpc_tls_session_req *)buf->data;
    rpc_tls_stats_resp *resp = (rpc_tls_stats_resp *)buf->data;

    // Heap figures come with every reply, also when the session is gone
    tls_session_stats_t stats;
    int err = ESP_ERR_INVALID_ARG;
    if (buf->size >= (int)sizeof(rpc_tls_session_req)) {
        err = tls_get_stats(req->slot, req->tag, &stats);
    } else {
        tls_get_stats(TLS_SLOT_GLOBAL, 0, &stats);
    }
    memset(&resp->esp_err, 0, sizeof(rpc_tls_stats_resp) - sizeof(rpc_header_t));
    resp->esp_err = err;
    resp->heap_free = heap_caps_get_free_size(TLS_HEAP_CAPS);
    resp->heap_min = heap_caps_get_minimum_free_size(TLS_HEAP_CAPS);
    resp->largest_block = heap_caps_get_largest_free_block(TLS_HEAP_CAPS);
    resp->ct_in_bytes = stats.ct_in_bytes;
    resp->ct_out_bytes = stats.ct_out_bytes;
    resp->pt_in_bytes = stats.pt_in_bytes;
    resp->pt_out_bytes = stats.pt_out_bytes;
    resp->handshake_us = stats.handshake_us;
    resp->tls_version = stats.tls_version;
    resp->ciphersuite = stats.ciphersuite;
    resp->group = stats.group;
    resp->state = stats.state;
    resp->sessions_open = tls_sessions_open();
    resp->stack_free = uxTaskGetStackHighWaterMark(NULL); // this runs on the TLS task
    resp->esp_time = stats.esp_time;
    resp->time_source = stats.time_source;
    resp->entropy_source = entropy_source();
    resp->ems = stats.ems;
    resp->block_max_ms = block_max_ms;
    memcpy(resp->spki_sha256, stats.spki_sha256, sizeof(resp->spki_sha256));
    buf->size = sizeof(rpc_tls_stats_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_tls_close_all(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    tls_close_all();
    resp->esp_err = ESP_OK;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_tls_set_ca(command_buf_t *buf)
{
    rpc_tls_set_ca_req *req = (rpc_tls_set_ca_req *)buf->data;
    rpc_tls_set_ca_resp *resp = (rpc_tls_set_ca_resp *)buf->data;

    // tls_set_ca() parses the certificate before the response overwrites it
    uint8_t reject = TLS_CA_REJECT_NONE;
    int err = ESP_ERR_INVALID_ARG;
    if ((buf->size >= (int)sizeof(rpc_tls_set_ca_req)) && (buf->size == (int)sizeof(rpc_tls_set_ca_req) + req->der_len)) {
        err = tls_set_ca(req->der, req->der_len, req->ca_tag, &reject);
    } else {
        tls_set_ca(NULL, 0, 0, &reject); // a malformed upload still leaves no CA held
    }
    memset(&resp->esp_err, 0, sizeof(rpc_tls_set_ca_resp) - sizeof(rpc_header_t));
    resp->esp_err = err;
    resp->reject = reject;
    buf->size = sizeof(rpc_tls_set_ca_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

// The TLS task's side: runs one TLS command and transmits its reply
void tls_serve(command_buf_t *buf)
{
    rpc_header_t *hdr = (rpc_header_t *)buf->data;
    switch (hdr->command) {
    case CMD_TLS_OPEN:
        cmd_tls_open(buf);
        break;
    case CMD_TLS_STEP:
        cmd_tls_step(buf);
        break;
    case CMD_TLS_CLOSE:
        cmd_tls_close(buf);
        break;
    case CMD_TLS_STATS:
        cmd_tls_stats(buf);
        break;
    case CMD_TLS_CLOSE_ALL:
        cmd_tls_close_all(buf);
        break;
    case CMD_TLS_SET_CA:
        cmd_tls_set_ca(buf);
        break;
    default:
        cmd_not_implemented(buf);
        break;
    }
}

static void tls_task(void *arg)
{
    command_buf_t *buf;
    while (1) {
        if (xQueueReceive(tls_queue, &buf, portMAX_DELAY) == pdTRUE) {
            tls_serve(buf);
        }
    }
}

// The TLS queue is full: the command's own reply shape, all zero but for ESP_ERR_TIMEOUT
static void cmd_tls_busy(command_buf_t *buf)
{
    rpc_header_t *hdr = (rpc_header_t *)buf->data;
    int size;
    switch (hdr->command) {
    case CMD_TLS_OPEN:  size = sizeof(rpc_tls_open_resp); break;
    case CMD_TLS_STEP:  size = sizeof(rpc_tls_step_resp); break;
    case CMD_TLS_CLOSE: size = sizeof(rpc_tls_close_resp); break;
    case CMD_TLS_STATS: size = sizeof(rpc_tls_stats_resp); break;
    case CMD_TLS_SET_CA: size = sizeof(rpc_tls_set_ca_resp); break;
    default:            size = sizeof(rpc_espcmd_resp); break;
    }
    memset(buf->data + sizeof(rpc_header_t), 0, size - sizeof(rpc_header_t));
    ((rpc_espcmd_resp *)buf->data)->esp_err = ESP_ERR_TIMEOUT;
    if (hdr->command == CMD_TLS_OPEN) {
        ((rpc_tls_open_resp *)buf->data)->slot = TLS_SLOT_GLOBAL;
    } else if (hdr->command == CMD_TLS_STEP) {
        ((rpc_tls_step_resp *)buf->data)->alert_in = TLS_ALERT_NONE;
        ((rpc_tls_step_resp *)buf->data)->alert_out = TLS_ALERT_NONE;
    }
    buf->size = size;
    my_uart_transmit_packet(UART_CHAN, buf);
}

// The dispatcher's side: queues the command for the TLS task, or answers it at once when the queue is full
void tls_rpc_dispatch(command_buf_t *buf)
{
    if (xQueueSend(tls_queue, &buf, 0) != pdTRUE) {
        cmd_tls_busy(buf);
    }
}

// The dispatcher reports how long one command held it, in ms; TLS_STATS returns the longest
void tls_rpc_note_block(int64_t ms)
{
    if (ms > block_max_ms) {
        block_max_ms = (ms > 0xFFFF) ? 0xFFFF : (uint16_t)ms;
    }
}

// TLS on core 1, as Wi-Fi is pinned to core 0 (CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0), at the dispatcher's priority
void tls_rpc_start(void)
{
    tls_queue = xQueueCreate(TLS_QUEUE_DEPTH, sizeof(command_buf_t *));
    xTaskCreatePinnedToCore(tls_task, "TLS", TLS_TASK_STACK, NULL, tskIDLE_PRIORITY + 1, NULL, 1);
}
