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

#define IDENT_STRING "ESP32 WiFi Bridge V1.14"
#define IDENT_MAJOR   1
// 1.14: the module wakes the machine on a magic packet, and stores whether it
// should.
// 1.13: the module stores the power on behavior after a loss of input power.
// 1.12: the connector retries after a dropped link instead of idling in
// Disconnected forever. The Ultimate's updater only reflashes the module
// when this differs from what the module reports, so a module change that
// does not bump it is silently never installed.
#define IDENT_MINOR   14
#define MULTITHREADED 0
#define DISPATCHER_STACK 3072

#endif /* SOFTWARE_WIFI_SCAN_MAIN_RPC_DISPATCH_H_ */
