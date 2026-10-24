/*
 * wifi_cmd.h
 *
 *  Created on: Feb 8, 2025
 *      Author: gideon
 */

#ifndef WIFI_CMD_H_
#define WIFI_CMD_H_

#include "dma_uart.h"
#include "esp32.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "rpc_calls.h"
#include "wifi_reply.h"
extern "C" {
    #include "cmd_buffer.h"
}

// U64-Elite-II only
typedef struct {
    uint16_t vbus;
    uint16_t vaux;
    uint16_t v50;
    uint16_t v33;
    uint16_t v18;
    uint16_t v10;
    uint16_t vusb;
} voltages_t;

// These functions perform the communication with the WiFi Module
void wifi_command_init(void); // Sets the callback for the ISR
int wifi_setbaud(int baudrate, uint8_t flowctrl);
BaseType_t wifi_detect(uint16_t *major, uint16_t *minor, char *str, int maxlen);
int wifi_getmac(uint8_t *mac);
int wifi_scan(void *);
int wifi_wifi_connect(const char *ssid, const char *password, uint8_t auth);
int wifi_wifi_autoconnect();
int wifi_wifi_disconnect();
int wifi_machine_off();
int wifi_machine_reboot();
void wifi_rx_packet();

int wifi_enable();
int wifi_disable();
int wifi_modem_enable(bool);
int wifi_get_voltages(voltages_t *voltages);
int wifi_is_connected(uint8_t &status);
int wifi_forget_aps();
int wifi_set_serial(const char *serial);
int wifi_get_serial(char *serial);

// Power on behavior after loss of input power; the mode values are the
// POWERON_MODE_* defines of the control module (software/u64ctrl).
int wifi_set_power_mode(uint8_t mode);
int wifi_get_power_mode(uint8_t *mode, uint8_t *last_state);

// Values are the WAKE_ON_WIFI_* defines of the control module (software/u64ctrl).
int wifi_set_wake_on_wifi(uint8_t enabled);
int wifi_get_wake_on_wifi(uint8_t *enabled);

// Registers the calling task as the waiter for the reply to this request
int wifi_reply_register(rpc_header_t *hdr);
// Sends the request and waits for its reply; NULL if there is none in time
command_buf_t *wifi_transmit(command_buf_t *buf, int timeout_ms);
command_buf_t *wifi_wait_reply(rpc_header_t hdr, int timeout_ms);

// The recovery server the control module asks first; "" for none.
int wifi_set_recovery_url(const char *url);
int wifi_get_recovery_url(char *url, int maxlen);

extern "C" { void print_uart_status(); }
extern const char *no_wifi_buf;
extern const char *no_wifi_slot;

#define WIFI_REPLY_TIMEOUT_MS   10000
#define WIFI_SLOW_TIMEOUT_MS    60000 // scan and connect

// For requests that are not answered, such as network frames. 'failed' is what
// the caller returns when there is no TX buffer: not 0 where that reads as success.
#define BUFARGS_NOREPLY(x, cmd, failed) command_buf_t *buf; \
                            BaseType_t gotbuf = esp32.uart->GetBuffer(&buf, 1000); \
                            if (gotbuf == pdFALSE) { printf(no_wifi_buf); /*print_uart_status();*/ return failed; } \
                            rpc_ ## x ## _req *args = (rpc_ ## x ## _req *)buf->data; \
                            args->hdr.command = cmd; \
                            args->hdr.sequence = 0; \
                            args->hdr.thread = 0xFF; \
                            buf->size = sizeof(rpc_ ## x ## _req);

#define BUFARGS(x, cmd)     BUFARGS_NOREPLY(x, cmd, -1) \
                            if (!wifi_reply_register(&args->hdr)) { esp32.uart->FreeBuffer(buf); printf(no_wifi_slot); return -1; }

#define TRANSMIT_T(x, ms)   buf = wifi_transmit(buf, ms); \
                            if (!buf) { printf("*** NO REPLY FROM WIFI MODULE.\n"); return -1; } \
                            rpc_ ## x ## _resp *result = (rpc_ ## x ## _resp *)buf->data;

#define TRANSMIT(x)         TRANSMIT_T(x, WIFI_REPLY_TIMEOUT_MS)

#define TRANSMITDBG(x)      printf("Transmitting %b:\n", buf->bufnr); dump_hex_relative(buf->data, buf->size);\
                            buf = wifi_transmit(buf, WIFI_REPLY_TIMEOUT_MS); \
                            if (!buf) { printf("*** NO REPLY FROM WIFI MODULE.\n"); return -1; } \
                            printf("Received %b:\n", buf->bufnr); dump_hex_relative(buf->data, buf->size);\
                            rpc_ ## x ## _resp *result = (rpc_ ## x ## _resp *)buf->data;

#define RETURN_STD          errno = result->xerrno; \
                            int retval = result->retval; \
                            esp32.uart->FreeBuffer(buf); \
                            return retval;

#define RETURN_ESP          int retval = result->esp_err; \
                            esp32.uart->FreeBuffer(buf); \
                            return retval;

#endif /* WIFI_CMD_H_ */
