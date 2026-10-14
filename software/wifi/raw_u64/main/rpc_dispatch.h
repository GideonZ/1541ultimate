/*
 * rpc_dispatch.h
 *
 *  Created on: Nov 10, 2021
 *      Author: gideon
 */

#ifndef SOFTWARE_WIFI_SCAN_MAIN_RPC_DISPATCH_H_
#define SOFTWARE_WIFI_SCAN_MAIN_RPC_DISPATCH_H_

#ifndef RECOVERYAPP
    #include "freertos/FreeRTOS.h"
    #include "freertos/queue.h"
    #include "freertos/task.h"
#endif
#include "cmd_buffer.h" // For the constant NUM_BUFFERS

typedef struct {
    TaskHandle_t task;
    QueueHandle_t queue;
} dispatcher_t;

void create_dispatchers(int stacksize, command_buf_context_t *bufs);
void dispatch(void *ct);
void dispatch_command(command_buf_t *buf);
void tls_serve(command_buf_t *buf);
void start_dispatch(QueueHandle_t queue);
void send_button_event(uint8_t button);
void send_keepalive();

#define IDENT_STRING "ESP32 WiFi Bridge V1.11"
#define IDENT_MAJOR   1
// 1.11: TLS client sessions (CMD_TLS_*) on their own task, and the Mozilla root list of
//       software/u64ctrl/ca as the certificate bundle.
// 1.10: CMD_GET_RANDOM, served with the radio off too, from the SAR ADC source.
// 1.9: turns its radio off and on when asked (CMD_WIFI_DISABLE / CMD_WIFI_ENABLE) and
// starts with it off until the Ultimate sends Enable or Autoconnect (or, without the
// switch, asks for the connection state); after a restart of its own it puts the radio
// back as it was and sends EVENT_RESTARTED. Joins the strongest access point of an
// SSID, or the one the Ultimate picked when it sends its BSSID. EVENT_DISCONNECTED
// carries the reason the station gave.
// 1.8: built with ESP-IDF 6.0.3.
// 1.7: terminates 32-character SSIDs and 64-character passwords, and reads
// them back from NVS whole.
// 1.6: the connector retries after a dropped link instead of idling in
// Disconnected forever. The Ultimate's updater only reflashes the module
// when this differs from what the module reports, so a module change that
// does not bump it is silently never installed.
#define IDENT_MINOR   11
#define MULTITHREADED 0
#define DISPATCHER_STACK 3072 // TLS runs on its own task, so this stays small
#define TLS_TASK_STACK   8192 // twice the stack handshakes were measured to use on the dispatcher
#define TLS_QUEUE_DEPTH  2    // one request outstanding, and a CLOSE behind a step that timed out

#endif /* SOFTWARE_WIFI_SCAN_MAIN_RPC_DISPATCH_H_ */
