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

extern uint16_t sequence_nr;
extern TaskHandle_t tasksWaitingForReply[NUM_TX_BUFFERS];
extern "C" { void print_uart_status(); }
extern const char *no_wifi_buf;

#define BUFARGS(x, cmd)     command_buf_t *buf; \
                            BaseType_t gotbuf = esp32.uart->GetBuffer(&buf, 1000); \
                            if (gotbuf == pdFALSE) { printf(no_wifi_buf); /*print_uart_status();*/ return pdFALSE; } \
                            rpc_ ## x ## _req *args = (rpc_ ## x ## _req *)buf->data; \
                            args->hdr.command = cmd; \
                            args->hdr.sequence = sequence_nr++; \
                            args->hdr.thread = (uint8_t)buf->bufnr; \
                            buf->size = sizeof(rpc_ ## x ## _req); \
                            tasksWaitingForReply[buf->bufnr] = xTaskGetCurrentTaskHandle();


#define TRANSMIT(x)         esp32.uart->TransmitPacket(buf); \
                            xTaskNotifyWait(0, 0, (uint32_t *)&buf, portMAX_DELAY); \
                            rpc_ ## x ## _resp *result = (rpc_ ## x ## _resp *)buf->data; \
                            if(result->hdr.thread < 16) { \
                                tasksWaitingForReply[result->hdr.thread] = NULL; \
                            }

#define TRANSMITDBG(x)      printf("Transmitting %b:\n", buf->bufnr); dump_hex_relative(buf->data, buf->size);\
                            esp32.uart->TransmitPacket(buf); \
                            xTaskNotifyWait(0, 0, (uint32_t *)&buf, portMAX_DELAY); \
                            printf("Received %b:\n", buf->bufnr); dump_hex_relative(buf->data, buf->size);\
                            rpc_ ## x ## _resp *result = (rpc_ ## x ## _resp *)buf->data; \
                            if(result->hdr.thread < 16) { \
                                tasksWaitingForReply[result->hdr.thread] = NULL; \
                            }

#define RETURN_STD          errno = result->xerrno; \
                            int retval = result->retval; \
                            esp32.uart->FreeBuffer(buf); \
                            return retval;

#define RETURN_ESP          int retval = result->esp_err; \
                            esp32.uart->FreeBuffer(buf); \
                            return retval;

#endif /* WIFI_CMD_H_ */
