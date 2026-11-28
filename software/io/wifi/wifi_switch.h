/*
 * wifi_switch.h
 *
 * The Ultimate's side of turning the WiFi module's radio off and on, on every product.
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
 * the radio off is asked for again; after WIFI_SWITCH_ASKS such waits the user is told, the
 * link goes down, and the radio off is asked for every WIFI_SWITCH_SLOW_MS from then on,
 * until the module confirms it or WiFi is enabled. The link is not taken meanwhile: the
 * module's link events are still dropped. A module that does not answer at all costs the
 * WiFi task the answer's timeout (WIFI_REPLY_TIMEOUT_MS) at each ask.
 *
 * A radio on has no confirmation of its own, but its answer counts: one that fails (an
 * error, or no answer) is asked for again at the same pace, and the user is told after
 * WIFI_SWITCH_ASKS such waits. Otherwise a module that kept a Disabled of its own (the
 * control module's copy) would stay off while the setting reads Enabled.
 *
 * The control module of the Elite II and C64 Ultimate refuses a radio off while a recovery
 * downloads through its radio (WIFI_SWITCH_REFUSED); it keeps the setting for its next boot.
 * Nothing is waited for then, and the link follows the module.
 *
 * A module without the radio switch answers the request with an error; its radio is on
 * from its boot. The application of the U64 and U2+L then holds the module off, at its
 * start and on a Disable from the menu, the REST API or the setting. At a boot with
 * Disabled stored such a module runs until it has answered, so it may associate for a
 * moment. An Enable, which stores Enabled, then starts the WiFi application again.
 *
 * Pure logic, so that the host test (software/io/wifi/tests/wifi_switch_test.cpp) runs it.
 */

#ifndef WIFI_SWITCH_H_
#define WIFI_SWITCH_H_

#include <stdbool.h>
#include <stdint.h>
#include "rpc_calls.h"

#define WIFI_SWITCH_CONFIRM_MS 10000
// How many times a radio off is waited for before the user is told that it did not happen
#define WIFI_SWITCH_ASKS 3
// From then on, how often it is asked for again
#define WIFI_SWITCH_SLOW_MS 60000

// Internal event of the WiFi application: it wakes the WiFi task to apply "WiFi Enabled" as
// it is stored now (WiFi :: radio_setting_due says that a change waits). It has no value:
// the task reads the setting when it applies it, so the last change wins.
#define EVENT_RADIO_SETTING 0x7E

// The answer of a module without the radio switch to a radio off or on:
// ESP_ERR_NOT_SUPPORTED, which such a module gives for every command it does not have.
// Any other error (the module could not queue it, or it never left) is not taken for
// that: the confirmation deadline asks again.
#define WIFI_SWITCH_NOT_SUPPORTED 0x106
// The answer of the control module (Elite II, C64 Ultimate) to a radio off during a
// recovery: ESP_ERR_INVALID_STATE. The radio stays on for the download.
#define WIFI_SWITCH_REFUSED 0x103

static inline bool wifi_switch_unsupported(int result)
{
#if U64 == 2
    // The control module has the switch in every version, and it runs the machine (power,
    // keyboard): it is never held off
    return false;
#else
    return result == WIFI_SWITCH_NOT_SUPPORTED;
#endif
}

static inline bool wifi_switch_refused(int result)
{
    return result == WIFI_SWITCH_REFUSED;
}

typedef struct {
    bool awaiting_off; // the radio off was asked for, and EVENT_DISABLED is not seen yet
    bool retry_on;     // the radio on failed, and is asked for again at the deadline
    uint32_t deadline; // when the wait for EVENT_DISABLED (or for the next radio on) ends, in the caller's time unit
    uint8_t asks;      // the waits for it so far, this one included
} wifi_switch_t;

// At the start of the WiFi application, when it has found the module: whether the radio
// is to be turned off now. enabled is the stored "WiFi Enabled".
static inline bool wifi_switch_wants_off(wifi_switch_t *sw, bool enabled)
{
    sw->awaiting_off = false;
    sw->retry_on = false;
    return !enabled;
}

// The radio off was asked for and the module queued it (or the answer was an error other
// than WIFI_SWITCH_NOT_SUPPORTED, taken as asked: the deadline asks again).
// now and limit are in one time unit (RTOS ticks in the application): the wait for the
// module's confirmation ends at now + limit.
static inline void wifi_switch_asked_off(wifi_switch_t *sw, uint32_t now, uint32_t limit)
{
    sw->awaiting_off = true;
    sw->retry_on = false;
    sw->deadline = now + limit;
    sw->asks = 1;
}

// The radio on was asked for and failed (an error other than WIFI_SWITCH_NOT_SUPPORTED, or
// no answer): it is asked for again at now + limit
static inline void wifi_switch_on_failed(wifi_switch_t *sw, uint32_t now, uint32_t limit)
{
    sw->awaiting_off = false;
    sw->retry_on = true;
    sw->deadline = now + limit;
    sw->asks = 1;
}

// How long the application may wait for the next event: -1 for as long as it takes, else
// the time left until the confirmation is overdue, or a radio on is due again (0 when it is)
static inline int32_t wifi_switch_wait(const wifi_switch_t *sw, uint32_t now)
{
    if (!sw->awaiting_off && !sw->retry_on) {
        return -1;
    }
    int32_t left = (int32_t)(sw->deadline - now);
    return (left > 0) ? left : 0;
}

// True once, when the confirmation is overdue: the wait ends, and the application asks for
// the radio off again (wifi_switch_ask_again)
static inline bool wifi_switch_expired(wifi_switch_t *sw, uint32_t now)
{
    if (!sw->awaiting_off || (wifi_switch_wait(sw, now) > 0)) {
        return false;
    }
    sw->awaiting_off = false;
    return true;
}

// True once, when a radio on that failed is due to be asked for again
static inline bool wifi_switch_on_due(wifi_switch_t *sw, uint32_t now)
{
    if (!sw->retry_on || (wifi_switch_wait(sw, now) > 0)) {
        return false;
    }
    sw->retry_on = false;
    return true;
}

// The next wait of wifi_switch_ask_again or wifi_switch_on_again
static inline bool wifi_switch_next_wait(wifi_switch_t *sw, uint32_t now, uint32_t limit, uint32_t slow)
{
    if (sw->asks < WIFI_SWITCH_ASKS) {
        sw->deadline = now + limit;
        sw->asks++;
        return false;
    }
    sw->deadline = now + slow;
    if (sw->asks > WIFI_SWITCH_ASKS) {
        return false;
    }
    sw->asks++;
    return true;
}

// After wifi_switch_expired, with the radio off asked for again: the next wait for its
// confirmation starts. The first WIFI_SWITCH_ASKS waits last limit, every later one slow.
// True once, when the waits that last limit have run out: the user is to be told then.
static inline bool wifi_switch_ask_again(wifi_switch_t *sw, uint32_t now, uint32_t limit, uint32_t slow)
{
    sw->awaiting_off = true;
    return wifi_switch_next_wait(sw, now, limit, slow);
}

// After wifi_switch_on_due, with the radio on asked for again and failed again: the same
// for the next radio on
static inline bool wifi_switch_on_again(wifi_switch_t *sw, uint32_t now, uint32_t limit, uint32_t slow)
{
    sw->retry_on = true;
    return wifi_switch_next_wait(sw, now, limit, slow);
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

// On an Enable: the wait for a radio-off confirmation ends (and a radio on due again is
// asked for now)
static inline void wifi_switch_on(wifi_switch_t *sw)
{
    sw->awaiting_off = false;
    sw->retry_on = false;
}

#endif /* WIFI_SWITCH_H_ */
