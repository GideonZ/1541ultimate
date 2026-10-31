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

#define IDENT_STRING "ESP32 WiFi Bridge V1.23"
#define IDENT_MAJOR   1
// 1.23: "WiFi Enabled" is kept in the module's NVS: a stored Disabled leaves the radio off at
// boot, and the Ultimate's Enable and Disable update the copy.
// 1.22: a Disable or Autoconnect during the AP list walks is no longer lost, Enable
// reconnects after a Disconnect, a Scan while Disabled is refused and a Connect leaves
// Disabled. Joins the strongest access point of an SSID, or the one the Ultimate picked
// when it sends its BSSID. EVENT_DISCONNECTED carries the reason the station gave.
// 1.21: a recovery trusts Mozilla's root list of 2026-10-20 (software/u64ctrl/ca), which
// no longer holds the emSign roots G3, C1 and C3, instead of the one in ESP-IDF 5.3.1.
// 1.20: terminates 32-character SSIDs and 64-character passwords, and reads
// them back from NVS whole.
// 1.19: a recovery also tries the my_hotspot access point after the stored ones.
// 1.18: recovers the firmware over Wi-Fi when the switch is held down for three
// seconds while off, and stores the recovery server the menu sets.
// 1.14: wakes the machine on a magic packet, and stores whether it should.
// 1.13: the module stores the power on behavior after a loss of input power.
// 1.12: the connector retries after a dropped link instead of idling in
// Disconnected forever. The Ultimate's updater only reflashes the module
// when this is higher than what the module reports, so a module change that
// does not bump it is silently never installed, and an older release never
// replaces a newer module.
#define IDENT_MINOR   23
#define MULTITHREADED 0
#define DISPATCHER_STACK 3072

#endif /* SOFTWARE_WIFI_SCAN_MAIN_RPC_DISPATCH_H_ */
