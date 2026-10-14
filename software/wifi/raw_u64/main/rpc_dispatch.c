/*
 * rpc_dispatch.c
 *
 *  Created on: Nov 9, 2021
 *      Author: gideon
 */


#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "rpc_calls.h"
#include "rpc_dispatch.h"
#include "my_uart.h"
#include "driver/gpio.h"
#include "pinout.h"
#include "wifi_modem.h"
#include "sntp.h"
#include "tls_engine.h"

// not officially supported
#include "../lwip/esp_netif_lwip_internal.h"
#include "esp_netif_net_stack.h"

#define UART_CHAN UART_NUM_1

static const char *TAG = "dispatch";

void cmd_echo(command_buf_t *buf)
{
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_identify(command_buf_t *buf)
{
    rpc_identify_resp *resp = (rpc_identify_resp *)buf->data;
    resp->major = IDENT_MAJOR;
    resp->minor = IDENT_MINOR;
    strcpy(&resp->string, IDENT_STRING);
    buf->size = sizeof(rpc_identify_resp) + strlen(&resp->string);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_getmac(command_buf_t *buf)
{
    rpc_getmac_resp *resp = (rpc_getmac_resp *)buf->data;
    resp->esp_err = esp_wifi_get_mac(WIFI_IF_STA, resp->mac);
    buf->size = sizeof(rpc_getmac_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_setbaud(command_buf_t *buf)
{
    rpc_setbaud_req *req = (rpc_setbaud_req *)buf->data;
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;

    esp_err_t err = uart_set_baudrate(UART_CHAN, req->baudrate);
    if (err == ESP_OK) {
        err = uart_set_hw_flow_ctrl(UART_CHAN, req->flowctrl ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE, 122);
    }
    if (err == ESP_OK) {
        err = uart_set_line_inverse(UART_CHAN, req->inversions);
    }

#if 1 || UART_DEBUG
    ESP_LOGI(TAG, "Baud Rate set to %d -> Err = %d", req->baudrate, err);
#endif
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait one second

    resp->esp_err = err;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_scan(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    ConnectCommand_t cmd;
    cmd.buf = buf;
    cmd.command = CMD_WIFI_SCAN;

    if (xQueueSend(connect_commands, &cmd, 1000) == pdFALSE) {
        resp->esp_err = ESP_ERR_NO_MEM;
        my_uart_transmit_packet(UART_CHAN, buf);
        return;
    }
    xSemaphoreGive(connect_semaphore);
    // command is now in the queue, so the connect handler will process it further
}

void cmd_wifi_connect(command_buf_t *buf)
{
    rpc_wifi_connect_req *param = (rpc_wifi_connect_req *)buf->data;
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;

    if (buf->size < 98) {
        resp->esp_err = ESP_ERR_INVALID_ARG;
        my_uart_transmit_packet(UART_CHAN, buf);
        return;
    }

    buf->size = sizeof(rpc_espcmd_resp);
    ConnectCommand_t ev;
    ev.buf = buf;
    ev.command = CMD_WIFI_CONNECT;
    ev.auth_mode = param->auth_mode;
    memcpy(&ev.ssid, param->ssid, 32);
    memcpy(&ev.pw, param->password, 64);

    if (xQueueSend(connect_commands, &ev, 1000) == pdFALSE) {
        resp->esp_err = ESP_ERR_NO_MEM;
        my_uart_transmit_packet(UART_CHAN, buf);
        return;
    }
    xSemaphoreGive(connect_semaphore);
    // command is now in the queue, so the connect handler will process it further
}

void cmd_wifi_autoconnect(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;

    buf->size = sizeof(rpc_espcmd_resp);
    ConnectCommand_t ev;
    ev.buf = buf;
    ev.command = CMD_WIFI_AUTOCONNECT;

    if (xQueueSend(connect_commands, &ev, 1000) == pdFALSE) {
        resp->esp_err = ESP_ERR_NO_MEM;
        my_uart_transmit_packet(UART_CHAN, buf);
        return;
    } else {
        resp->esp_err = ESP_OK;
        my_uart_transmit_packet(UART_CHAN, buf);
    }
    xSemaphoreGive(connect_semaphore);
    // command is now in the queue, so the connect handler will process it further
}

void cmd_wifi_disconnect(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    ESP_LOGI(TAG, "Disconnecting from WiFi.");
    // Asked for, so the connector must not undo it on its retry timer.
    wifi_note_user_disconnect();
    resp->esp_err = esp_wifi_disconnect();
    ESP_LOGI(TAG, "Disconnected from WiFi, sending response. (%d)", resp->esp_err);
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

extern esp_netif_t *my_sta_netif;

void cmd_send_eth_packet(command_buf_t *buf)
{
    rpc_send_eth_req *param = (rpc_send_eth_req *)buf->data;
    err_t _err = esp_netif_transmit(my_sta_netif, &param->data, param->length);
    my_uart_free_buffer(UART_CHAN, buf);
}

void cmd_off(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_OK;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void cmd_modem_on(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_OK;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
    enable_hook();
}

void cmd_modem_off(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_OK;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
    disable_hook();
}

void cmd_wifi_disable(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_ERR_NOT_SUPPORTED;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_wifi_enable(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_ERR_NOT_SUPPORTED;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_get_connection(command_buf_t *buf)
{
    rpc_get_connection_resp *resp = (rpc_get_connection_resp *)buf->data;
    resp->esp_err = ESP_OK;
    resp->status = wifi_get_connection();
    buf->size = sizeof(rpc_get_connection_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_get_time(command_buf_t *buf)
{
    rpc_get_time_req *param = (rpc_get_time_req *)buf->data;
    rpc_get_time_resp *resp = (rpc_get_time_resp *)buf->data;
    sntp_get_time(param->timezone, &resp->datetime);
    resp->esp_err = ESP_OK;
    buf->size = sizeof(rpc_get_time_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void cmd_clear_aps(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    buf->size = sizeof(rpc_espcmd_resp);
    resp->esp_err = wifi_clear_aps();
    my_uart_transmit_packet(UART_CHAN, buf);
}

_Static_assert(ENTROPY_NONE == 0, "ENTROPY_NONE");

// TLS. The layout is part of the wire, the header is echoed, and the fixed part of every reply is zeroed
// before it is filled. The Ultimate decodes these by offset and asserts the same ones: a layout change must
// break the build, not the wire.
_Static_assert(sizeof(rpc_tls_open_req) == 52, "rpc_tls_open_req size");
_Static_assert(offsetof(rpc_tls_open_req, trust) == 4, "rpc_tls_open_req.trust");
_Static_assert(offsetof(rpc_tls_open_req, sni_len) == 5, "rpc_tls_open_req.sni_len");
_Static_assert(offsetof(rpc_tls_open_req, slot) == 6, "rpc_tls_open_req.slot");
_Static_assert(offsetof(rpc_tls_open_req, time_source) == 7, "rpc_tls_open_req.time_source");
_Static_assert(offsetof(rpc_tls_open_req, unix_time) == 8, "rpc_tls_open_req.unix_time");
_Static_assert(offsetof(rpc_tls_open_req, tag) == 12, "rpc_tls_open_req.tag");
_Static_assert(offsetof(rpc_tls_open_req, opts) == 14, "rpc_tls_open_req.opts");
_Static_assert(offsetof(rpc_tls_open_req, reserved) == 15, "rpc_tls_open_req.reserved");
_Static_assert(offsetof(rpc_tls_open_req, ca_tag) == 16, "rpc_tls_open_req.ca_tag");
_Static_assert(offsetof(rpc_tls_open_req, reserved2) == 18, "rpc_tls_open_req.reserved2");
_Static_assert(offsetof(rpc_tls_open_req, pin) == 20, "rpc_tls_open_req.pin");
_Static_assert(offsetof(rpc_tls_open_req, sni) == 52, "rpc_tls_open_req.sni");
_Static_assert(sizeof(rpc_tls_open_resp) == 12, "rpc_tls_open_resp size");
_Static_assert(offsetof(rpc_tls_open_resp, esp_err) == 4, "rpc_tls_open_resp.esp_err");
_Static_assert(offsetof(rpc_tls_open_resp, slot) == 8, "rpc_tls_open_resp.slot");
_Static_assert(offsetof(rpc_tls_open_resp, reason) == 9, "rpc_tls_open_resp.reason");
_Static_assert(offsetof(rpc_tls_open_resp, reserved) == 10, "rpc_tls_open_resp.reserved");
_Static_assert(sizeof(rpc_tls_step_req) == 12, "rpc_tls_step_req size");
_Static_assert(offsetof(rpc_tls_step_req, slot) == 4, "rpc_tls_step_req.slot");
_Static_assert(offsetof(rpc_tls_step_req, flags) == 5, "rpc_tls_step_req.flags");
_Static_assert(offsetof(rpc_tls_step_req, tag) == 6, "rpc_tls_step_req.tag");
_Static_assert(offsetof(rpc_tls_step_req, ct_in_len) == 8, "rpc_tls_step_req.ct_in_len");
_Static_assert(offsetof(rpc_tls_step_req, pt_in_len) == 10, "rpc_tls_step_req.pt_in_len");
_Static_assert(offsetof(rpc_tls_step_req, data) == 12, "rpc_tls_step_req.data");
_Static_assert(sizeof(rpc_tls_step_resp) == 20, "rpc_tls_step_resp size");
_Static_assert(offsetof(rpc_tls_step_resp, esp_err) == 4, "rpc_tls_step_resp.esp_err");
_Static_assert(offsetof(rpc_tls_step_resp, state) == 8, "rpc_tls_step_resp.state");
_Static_assert(offsetof(rpc_tls_step_resp, alert_in) == 9, "rpc_tls_step_resp.alert_in");
_Static_assert(offsetof(rpc_tls_step_resp, ct_out_len) == 10, "rpc_tls_step_resp.ct_out_len");
_Static_assert(offsetof(rpc_tls_step_resp, pt_out_len) == 12, "rpc_tls_step_resp.pt_out_len");
_Static_assert(offsetof(rpc_tls_step_resp, alert_out) == 14, "rpc_tls_step_resp.alert_out");
_Static_assert(offsetof(rpc_tls_step_resp, fail_reason) == 15, "rpc_tls_step_resp.fail_reason");
_Static_assert(offsetof(rpc_tls_step_resp, verify_flags) == 16, "rpc_tls_step_resp.verify_flags");
_Static_assert(offsetof(rpc_tls_step_resp, data) == 20, "rpc_tls_step_resp.data");
_Static_assert(sizeof(rpc_tls_session_req) == 8, "rpc_tls_session_req size");
_Static_assert(offsetof(rpc_tls_session_req, slot) == 4, "rpc_tls_session_req.slot");
_Static_assert(offsetof(rpc_tls_session_req, reserved) == 5, "rpc_tls_session_req.reserved");
_Static_assert(offsetof(rpc_tls_session_req, tag) == 6, "rpc_tls_session_req.tag");
_Static_assert(sizeof(rpc_tls_close_resp) == 12, "rpc_tls_close_resp size");
_Static_assert(offsetof(rpc_tls_close_resp, esp_err) == 4, "rpc_tls_close_resp.esp_err");
_Static_assert(offsetof(rpc_tls_close_resp, ct_out_len) == 8, "rpc_tls_close_resp.ct_out_len");
_Static_assert(offsetof(rpc_tls_close_resp, reserved) == 10, "rpc_tls_close_resp.reserved");
_Static_assert(offsetof(rpc_tls_close_resp, data) == 12, "rpc_tls_close_resp.data");
_Static_assert(sizeof(rpc_espcmd_resp) == 8, "rpc_espcmd_resp size");
_Static_assert(offsetof(rpc_espcmd_resp, esp_err) == 4, "rpc_espcmd_resp.esp_err");
_Static_assert(sizeof(rpc_tls_stats_resp) == 96, "rpc_tls_stats_resp size");
_Static_assert(offsetof(rpc_tls_stats_resp, esp_err) == 4, "rpc_tls_stats_resp.esp_err");
_Static_assert(offsetof(rpc_tls_stats_resp, heap_free) == 8, "rpc_tls_stats_resp.heap_free");
_Static_assert(offsetof(rpc_tls_stats_resp, heap_min) == 12, "rpc_tls_stats_resp.heap_min");
_Static_assert(offsetof(rpc_tls_stats_resp, largest_block) == 16, "rpc_tls_stats_resp.largest_block");
_Static_assert(offsetof(rpc_tls_stats_resp, ct_in_bytes) == 20, "rpc_tls_stats_resp.ct_in_bytes");
_Static_assert(offsetof(rpc_tls_stats_resp, ct_out_bytes) == 24, "rpc_tls_stats_resp.ct_out_bytes");
_Static_assert(offsetof(rpc_tls_stats_resp, pt_in_bytes) == 28, "rpc_tls_stats_resp.pt_in_bytes");
_Static_assert(offsetof(rpc_tls_stats_resp, pt_out_bytes) == 32, "rpc_tls_stats_resp.pt_out_bytes");
_Static_assert(offsetof(rpc_tls_stats_resp, handshake_us) == 36, "rpc_tls_stats_resp.handshake_us");
_Static_assert(offsetof(rpc_tls_stats_resp, tls_version) == 40, "rpc_tls_stats_resp.tls_version");
_Static_assert(offsetof(rpc_tls_stats_resp, ciphersuite) == 42, "rpc_tls_stats_resp.ciphersuite");
_Static_assert(offsetof(rpc_tls_stats_resp, group) == 44, "rpc_tls_stats_resp.group");
_Static_assert(offsetof(rpc_tls_stats_resp, state) == 46, "rpc_tls_stats_resp.state");
_Static_assert(offsetof(rpc_tls_stats_resp, sessions_open) == 47, "rpc_tls_stats_resp.sessions_open");
_Static_assert(offsetof(rpc_tls_stats_resp, stack_free) == 48, "rpc_tls_stats_resp.stack_free");
_Static_assert(offsetof(rpc_tls_stats_resp, esp_time) == 52, "rpc_tls_stats_resp.esp_time");
_Static_assert(offsetof(rpc_tls_stats_resp, time_source) == 56, "rpc_tls_stats_resp.time_source");
_Static_assert(offsetof(rpc_tls_stats_resp, entropy_source) == 57, "rpc_tls_stats_resp.entropy_source");
_Static_assert(offsetof(rpc_tls_stats_resp, ems) == 58, "rpc_tls_stats_resp.ems");
_Static_assert(offsetof(rpc_tls_stats_resp, reserved) == 59, "rpc_tls_stats_resp.reserved");
_Static_assert(offsetof(rpc_tls_stats_resp, block_max_ms) == 60, "rpc_tls_stats_resp.block_max_ms");
_Static_assert(offsetof(rpc_tls_stats_resp, reserved2) == 62, "rpc_tls_stats_resp.reserved2");
_Static_assert(offsetof(rpc_tls_stats_resp, spki_sha256) == 64, "rpc_tls_stats_resp.spki_sha256");
_Static_assert(sizeof(rpc_tls_set_ca_req) == 8, "rpc_tls_set_ca_req size");
_Static_assert(offsetof(rpc_tls_set_ca_req, der_len) == 4, "rpc_tls_set_ca_req.der_len");
_Static_assert(offsetof(rpc_tls_set_ca_req, ca_tag) == 6, "rpc_tls_set_ca_req.ca_tag");
_Static_assert(offsetof(rpc_tls_set_ca_req, der) == 8, "rpc_tls_set_ca_req.der");
_Static_assert(sizeof(rpc_tls_set_ca_resp) == 12, "rpc_tls_set_ca_resp size");
_Static_assert(offsetof(rpc_tls_set_ca_resp, esp_err) == 4, "rpc_tls_set_ca_resp.esp_err");
_Static_assert(offsetof(rpc_tls_set_ca_resp, reject) == 8, "rpc_tls_set_ca_resp.reject");
_Static_assert((TLS_TIME_NONE == 0) && (TLS_TIME_NIOS == 1) && (TLS_TIME_SNTP == 2), "tls_time_source_t");
_Static_assert((TLS_STATE_HANDSHAKING == 0) && (TLS_STATE_ESTABLISHED == 1) &&
               (TLS_STATE_CLOSED == 2) && (TLS_STATE_FAILED == 3), "tls_state_t");
_Static_assert((TLS_TRUST_BUNDLE == 0) && (TLS_TRUST_CUSTOM == 1) && (TLS_TRUST_PIN == 2), "tls_trust_t");
_Static_assert((TLS_FAIL_VERSION_REFUSED == 1) && (TLS_FAIL_DOWNGRADE == 2) && (TLS_FAIL_NO_EMS == 3) &&
               (TLS_FAIL_CERT_VERIFY == 4) && (TLS_FAIL_PIN_MISMATCH == 5) && (TLS_FAIL_PEER_CLOSED == 6), "tls_fail_reason_t");
_Static_assert((TLS_OPEN_REASON_NO_ENTROPY == 1) && (TLS_OPEN_REASON_NO_TIME == 2) && (TLS_OPEN_REASON_NO_CUSTOM_CA == 3),
               "rpc_tls_open_resp.reason");
_Static_assert((TLS_CA_REJECT_RSA_KEY_SHORT == 1) && (TLS_CA_REJECT_CURVE == 3) && (TLS_CA_REJECT_NOT_CA == 4),
               "rpc_tls_set_ca_resp.reject");
// Every request and reply must fit one command buffer
_Static_assert(sizeof(rpc_tls_open_req) + TLS_SNI_MAX_LEN <= CMD_BUF_SIZE, "largest CMD_TLS_OPEN request");
_Static_assert(sizeof(rpc_tls_step_req) + TLS_STEP_IN_MAX <= CMD_BUF_SIZE, "largest CMD_TLS_STEP request");
_Static_assert(sizeof(rpc_tls_step_resp) + TLS_STEP_OUT_MAX <= CMD_BUF_SIZE, "largest CMD_TLS_STEP reply");
_Static_assert(sizeof(rpc_tls_close_resp) + TLS_STEP_OUT_MAX <= CMD_BUF_SIZE, "largest CMD_TLS_CLOSE reply");
_Static_assert(sizeof(rpc_tls_set_ca_req) + TLS_CA_MAX_LEN <= CMD_BUF_SIZE, "largest CMD_TLS_SET_CA request");
_Static_assert(sizeof(rpc_tls_stats_resp) <= CMD_BUF_SIZE, "CMD_TLS_STATS reply");
_Static_assert(TLS_SNI_MAX_LEN <= 255, "sni_len is a byte");

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

void cmd_not_implemented(command_buf_t *buf)
{
    rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
    resp->esp_err = ESP_ERR_NOT_SUPPORTED;
    buf->size = sizeof(rpc_espcmd_resp);
    my_uart_transmit_packet(UART_CHAN, buf);
}

void send_keepalive()
{
    command_buf_t *reply;
    if (my_uart_get_buffer(UART_NUM_1, &reply, 100)) {
        rpc_header_t *hdr = (rpc_header_t *)reply->data;
        hdr->command = EVENT_KEEPALIVE;
        hdr->thread = 0xFF;
        hdr->sequence = 0;
        reply->size = sizeof(rpc_header_t);
        my_uart_transmit_packet(UART_NUM_1, reply);
    } else {
        ESP_LOGW(TAG, "No buffer to keepalive.");
    }
}

// One command on the dispatcher. TLS commands go to the TLS task, so a handshake never blocks
// Wi-Fi TX; the time each command holds the dispatcher is what TLS_STATS reports as block_max_ms.
void dispatch_command(command_buf_t *pbuffer)
{
    rpc_header_t *hdr = (rpc_header_t *)(pbuffer->data);
    int64_t start = esp_timer_get_time();
    switch(hdr->command) {
    case CMD_SEND_PACKET:
        cmd_send_eth_packet(pbuffer);
        break;
    case CMD_ECHO:
        cmd_echo(pbuffer);
        break;
    case CMD_IDENTIFY:
        cmd_identify(pbuffer);
        break;
    case CMD_SET_BAUD:
        cmd_setbaud(pbuffer);
        break;
    case CMD_WIFI_SCAN:
        cmd_scan(pbuffer);
        break;
    case CMD_WIFI_CONNECT:
        cmd_wifi_connect(pbuffer);
        break;
    case CMD_WIFI_DISCONNECT:
        cmd_wifi_disconnect(pbuffer);
        break;
    case CMD_WIFI_AUTOCONNECT:
        cmd_wifi_autoconnect(pbuffer);
        break;
    case CMD_WIFI_GETMAC:
        cmd_getmac(pbuffer);
        break;
    case CMD_MACHINE_OFF:
        cmd_off(pbuffer);
        break;
    case CMD_MODEM_ON:
        cmd_modem_on(pbuffer);
        break;
    case CMD_MODEM_OFF:
        cmd_modem_off(pbuffer);
        break;
    case CMD_WIFI_DISABLE:
        cmd_wifi_disable(pbuffer);
        break;
    case CMD_WIFI_ENABLE:
        cmd_wifi_enable(pbuffer);
        break;
    case CMD_WIFI_IS_CONNECTED:
        cmd_get_connection(pbuffer);
        break;
    case CMD_GET_TIME:
        cmd_get_time(pbuffer);
        break;
    case CMD_CLEAR_APS:
        cmd_clear_aps(pbuffer);
        break;
    case CMD_TLS_OPEN:
    case CMD_TLS_STEP:
    case CMD_TLS_CLOSE:
    case CMD_TLS_STATS:
    case CMD_TLS_CLOSE_ALL:
    case CMD_TLS_SET_CA:
        if (xQueueSend(tls_queue, &pbuffer, 0) != pdTRUE) {
            cmd_tls_busy(pbuffer);
        }
        break;
    default:
        cmd_not_implemented(pbuffer);
        break;
    }
    int64_t ms = (esp_timer_get_time() - start) / 1000;
    if (ms > block_max_ms) {
        block_max_ms = (ms > 0xFFFF) ? 0xFFFF : (uint16_t)ms;
    }
}

void dispatch(void *ct)
{
    QueueHandle_t queue = (QueueHandle_t)ct;
    command_buf_t *pbuffer;

    int led = 0;
    printf("Dispatcher Start. Queue = %p\n", queue);
    while(1) {
        pbuffer = NULL;
        BaseType_t received = xQueueReceive(queue, &pbuffer, 2000 / portTICK_PERIOD_MS);
        led = 1 - led;
        gpio_set_level(IO_ESP_LED, led);
        if (received == pdFALSE) {
            continue;
        }
#if UART_DEBUG_RX
        printf("Received buffer %d with %d bytes.\n", pbuffer->bufnr, pbuffer->size);
#endif
        dispatch_command(pbuffer);
    }
}

void start_dispatch(QueueHandle_t queue)
{
    // TLS on core 1, as Wi-Fi is pinned to core 0 (CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0), at the dispatcher's priority
    tls_queue = xQueueCreate(TLS_QUEUE_DEPTH, sizeof(command_buf_t *));
    xTaskCreatePinnedToCore(tls_task, "TLS", TLS_TASK_STACK, NULL, tskIDLE_PRIORITY + 1, NULL, 1);
    xTaskCreate( dispatch, "Dispatch Task", DISPATCHER_STACK, queue, tskIDLE_PRIORITY + 1, NULL );
}

#if MULTITHREADED
void create_dispatchers(int stacksize, command_buf_context_t *bufs)
{
    static dispatcher_t dispatchers[NUM_BUFFERS];
    for (int i=0; i < NUM_BUFFERS; i++) {
        dispatchers[i].queue = xQueueCreate(1, sizeof(command_buf_t*));
        xTaskCreate( dispatch, "Dispatch Task", stacksize, dispatchers[i].queue, tskIDLE_PRIORITY + 1, &(dispatchers[i].task) );
        bufs->bufs[i].object = &dispatchers[i];
    }
}
#endif
