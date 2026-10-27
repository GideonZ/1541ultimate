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
void start_dispatch(QueueHandle_t queue);
void send_button_event(uint8_t button);
void send_keepalive();

#define IDENT_STRING "ESP32 WiFi Bridge V1.10"
#define IDENT_MAJOR   1
// 1.10: CMD_GET_RANDOM.
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
#define IDENT_MINOR   10
#define MULTITHREADED 0
#define DISPATCHER_STACK 3072

#endif /* SOFTWARE_WIFI_SCAN_MAIN_RPC_DISPATCH_H_ */
