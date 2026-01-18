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

#define IDENT_STRING "ESP32 WiFi Bridge V1.4"
#define IDENT_MAJOR   1
#define IDENT_MINOR   4
#define MULTITHREADED 0
#define DISPATCHER_STACK 3072

#endif /* SOFTWARE_WIFI_SCAN_MAIN_RPC_DISPATCH_H_ */
