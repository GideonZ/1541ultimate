/*
 * wifi_switch.h
 *
 * The Ultimate 64's and Ultimate II+L's side of turning the WiFi module's radio off and on
 * (U64 != 2). The Elite II and C64 Ultimate send the same requests from their own code
 * paths.
 *
 * The WiFi application always runs, because it is what asks the module. The module starts
 * with its radio off. With "WiFi Enabled" stored as Enabled the application asks for it to
 * go on; stored as Disabled, it asks for it to go off as soon as it finds the module,
 * before the interface is up. Until the module confirms that with EVENT_DISABLED, a link
 * event from it (a module that associated at boot after all) would put the WiFi
 * interface on the network while WiFi is off, so those events are dropped.
 *
 * A change of the setting at runtime (the menu, the REST API, a reset or load of the
 * settings) is marked for the WiFi task, which EVENT_RADIO_SETTING wakes; on its next pass
 * the task reads the setting, sends the request, and times the confirmation of a radio off
 * as at boot.
 *
 * The answer to the request only says that the module queued it. The application shows
 * Disabled when EVENT_DISABLED arrives, not before. If it does not arrive within
 * WIFI_SWITCH_CONFIRM_MS (the module could not stop its radio, or the request was lost),
 * the wait ends and the application takes the link state from the module again.
 *
 * A module without the radio switch answers the request with an error; its radio is on
 * from its boot. The application then holds the module off, at its start and on a Disable
 * from the menu, the REST API or the setting. At a boot with Disabled stored such a module
 * runs until it has answered, so it may associate for a moment. An Enable, which stores
 * Enabled, then starts the WiFi application again.
 *
 * Pure logic, so that the host test (software/io/wifi/tests/wifi_switch_test.cpp) runs it.
 */

#ifndef WIFI_SWITCH_H_
#define WIFI_SWITCH_H_

#include <stdbool.h>
#include <stdint.h>
#include "rpc_calls.h"

#define WIFI_SWITCH_CONFIRM_MS 10000

// Internal event of the WiFi application: it wakes the WiFi task to apply "WiFi Enabled" as
// it is stored now (WiFi :: radio_setting_due says that a change waits). It has no value:
// the task reads the setting when it applies it, so the last change wins.
#define EVENT_RADIO_SETTING 0x7E

// The answer of a module without the radio switch to a radio off or on:
// ESP_ERR_NOT_SUPPORTED, which such a module gives for every command it does not have.
// Any other error (the module could not queue it, or it never left) is not taken for
// that: the confirmation deadline asks again.
#define WIFI_SWITCH_NOT_SUPPORTED 0x106

static inline bool wifi_switch_unsupported(int result)
{
    return result == WIFI_SWITCH_NOT_SUPPORTED;
}

typedef struct {
    bool awaiting_off; // the radio off was asked for, and EVENT_DISABLED is not seen yet
    uint32_t deadline; // when the wait for EVENT_DISABLED ends, in the caller's time unit
} wifi_switch_t;

// At the start of the WiFi application, when it has found the module: whether the radio
// is to be turned off now. enabled is the stored "WiFi Enabled".
static inline bool wifi_switch_wants_off(wifi_switch_t *sw, bool enabled)
{
    sw->awaiting_off = false;
    return !enabled;
}

// The radio off was asked for and the module queued it (or the answer was an error other
// than WIFI_SWITCH_NOT_SUPPORTED, taken as asked: the deadline asks again).
// now and limit are in one time unit (RTOS ticks in the application): the wait for the
// module's confirmation ends at now + limit.
static inline void wifi_switch_asked_off(wifi_switch_t *sw, uint32_t now, uint32_t limit)
{
    sw->awaiting_off = true;
    sw->deadline = now + limit;
}

// How long the application may wait for the next event: -1 for as long as it takes, else
// the time left until the confirmation is overdue (0 when it is)
static inline int32_t wifi_switch_wait(const wifi_switch_t *sw, uint32_t now)
{
    if (!sw->awaiting_off) {
        return -1;
    }
    int32_t left = (int32_t)(sw->deadline - now);
    return (left > 0) ? left : 0;
}

// True once, when the confirmation is overdue: the wait ends, and the application asks the
// module for its link state again
static inline bool wifi_switch_expired(wifi_switch_t *sw, uint32_t now)
{
    if (!sw->awaiting_off || (wifi_switch_wait(sw, now) > 0)) {
        return false;
    }
    sw->awaiting_off = false;
    return true;
}

// Whether the WiFi application must drop this event from the module
static inline bool wifi_switch_drop_event(wifi_switch_t *sw, uint8_t command)
{
    if (!sw->awaiting_off) {
        return false;
    }
    switch (command) {
    case EVENT_DISABLED:
        // Only the module that turned its radio off ends the wait
        sw->awaiting_off = false;
        return false;
    case EVENT_CONNECTED:
    case EVENT_DISCONNECTED:
    case EVENT_GOTIP:
        return true;
    default:
        return false;
    }
}

// On an Enable: the wait for a radio-off confirmation ends
static inline void wifi_switch_on(wifi_switch_t *sw)
{
    sw->awaiting_off = false;
}

#endif /* WIFI_SWITCH_H_ */
