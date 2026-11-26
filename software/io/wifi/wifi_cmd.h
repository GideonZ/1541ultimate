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
int wifi_wifi_connect(const char *ssid, const char *password, uint8_t auth, const uint8_t *bssid = NULL, uint8_t channel = 0);
int wifi_wifi_autoconnect();
int wifi_wifi_disconnect();
int wifi_machine_off();
int wifi_machine_reboot();
void wifi_rx_packet();

// The radio switch: 0 when the module queued the request, its error otherwise, or -1
// when nothing reached the module (no buffer or slot, or no reply)
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

// A request that another task can end early. wifi_call gets a buffer, registers a reply slot, has fill write
// the request after its header, sends it and waits for the reply for timeout_ms. The wait also ends when
// cancel (if any) says so, at the deadline (if any), and when the module restarts. A reply that comes
// first is the caller's, whichever of these comes with it: take checks it and returns 0 or its own code,
// and WIFI_CALL_FOREIGN for a reply that is not the answer (the call then ends BUSY).
// One call at a time may wait this way; a second is UNSENT. wifi_wake is for the task that raises a cancel:
// it ends the wait at once, so that nothing has to poll. U64 == 1 and U64 == 2 only.
#define WIFI_CALL_BUSY       1  // sent, but no answer in time; the module may still act on it
#define WIFI_CALL_FOREIGN    5  // from take only: not the answer to this request
#define WIFI_CALL_UNSENT     6  // no buffer, no reply slot, another call waiting, or the UART refused it: nothing was sent
#define WIFI_CALL_CANCELLED  9  // sent, and ended by cancel
#define WIFI_CALL_RESTARTED  10 // sent, and the module restarted: no answer will come

typedef struct {
    const char *name;    // for the log
    uint8_t command;     // CMD_*
    uint32_t timeout_ms; // for the reply, from the moment the request is sent
    // Writes the request's arguments after its header, which is filled in already, and returns the size
    // of the whole request. It may not fail, so the caller checks the arguments first.
    int (*fill)(uint8_t *request, void *context);
    // Checks the size bytes at reply as the answer to request sequence and takes what it needs. Runs with the
    // scheduler suspended, so it may neither block nor print.
    int (*take)(const uint8_t *reply, int size, uint16_t sequence, void *context);
    void *context;
    // Optional: true ends the wait. Asked before every wait, and again after each wake of wifi_wake.
    bool (*cancel)(void *cancel_context);
    void *cancel_context;
    // Optional: the wait ends when the tick counter reaches deadline, if that is before the timeout
    bool use_deadline;
    uint32_t deadline;
} wifi_call_t;

int wifi_call(const wifi_call_t *call);
// Ends the wait of task, if it waits in wifi_call, and has it ask cancel. Safe from any task.
void wifi_wake(TaskHandle_t task);
// Counts the resets of the receive buffer pool: a module restart is one.
uint32_t wifi_tls_generation(void);

// The recovery server the control module asks first; "" for none.
int wifi_set_recovery_url(const char *url);
int wifi_get_recovery_url(char *url, int maxlen);

extern uint16_t sequence_nr;

// wifi_dedicated_call's thread number, which no transmit buffer has. While
// dedicated_waiting is set, the receive interrupt queues its replies in
// dedicated_replies, tagged with the buffer generation; otherwise it drops them.
// Not a reply slot (0 to WIFI_REPLY_SLOTS - 1), not 0xFF (no reply wanted), and not 0xFD to 0xFF, which
// the HTTPS provider channels of CMD_IDENTIFY use (#936)
#define WIFI_DEDICATED_THREAD 0xFC
#if U64 >= 1
typedef struct {
    command_buf_t *buf;
    uint32_t generation;
} dedicated_reply_t;
extern QueueHandle_t dedicated_replies;
extern volatile bool dedicated_waiting;
#endif
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
