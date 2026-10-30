/*
 * wifi_connect.h
 *
 * What a manual connect from the access point list tells the user. The module
 * answers the connect request as soon as ESP-IDF has accepted the attempt, which
 * says nothing about the outcome: that is the module's own EVENT_CONNECTED, or an
 * EVENT_DISCONNECTED when the access point refuses the client. The task notes the
 * accepted attempt here, and the outcome (or the lack of one) becomes a message.
 */
#ifndef WIFI_CONNECT_H_
#define WIFI_CONNECT_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rpc_calls.h"

// Internal event, queued by the user interface once the module has accepted a
// connect request. It carries the same payload as the module's EVENT_CONNECTED.
#define EVENT_CONNECT_PENDING 0x7F

// Longer than a join normally takes (a few seconds), shorter than a user waits.
// The tick is 5 ms (configTICK_RATE_HZ 200).
#define WIFI_CONNECT_TIMEOUT_TICKS (20 * 200)

// The reason of the module's EVENT_DISCONNECTED, 0 when it does not tell (a bare
// header from a module without it). The numbers are the station's: ESP-IDF's
// wifi_err_reason_t, which are the 802.11 reason codes, and 200 and up of its own.
static inline uint8_t wifi_disconnect_reason(const void *pkt, int size)
{
    if (size < (int)sizeof(event_pkt_disconnected)) {
        return 0;
    }
    return ((const event_pkt_disconnected *)pkt)->reason;
}

// The module's own leave of the access point it was on, which it does as part of a connect
#define WIFI_REASON_ASSOC_LEAVE 8

// Authentication or the 4-way handshake failed: the access point took the client
// for one with the wrong key (WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT, _AUTH_FAIL,
// _HANDSHAKE_TIMEOUT; the same values in ESP-IDF 5.3.1 and 6.0.3)
static inline bool wifi_disconnect_is_password(uint8_t reason)
{
    return reason == 15 || reason == 202 || reason == 204;
}

class WifiConnectFeedback
{
    bool pending;
    uint32_t deadline;
    char ssid[33];

    static void fail(char *msg, int max, const char *ssid)
    {
        snprintf(msg, max, "Could not connect to %s", ssid);
    }
public:
    WifiConnectFeedback() : pending(false), deadline(0) { ssid[0] = 0; }

    // The module accepted a connect request; the outcome is still to come
    void begin(const uint8_t *ssid32, uint32_t now)
    {
        memcpy(ssid, ssid32, 32);
        ssid[32] = 0;
        deadline = now + WIFI_CONNECT_TIMEOUT_TICKS;
        pending = true;
    }

    bool isPending(void) const { return pending; }

    // How long the task can wait for its next event
    uint32_t waitTicks(uint32_t now) const
    {
        if (!pending) {
            return 0xFFFFFFFFUL;
        }
        int32_t left = (int32_t)(deadline - now);
        return (left > 0) ? (uint32_t)left : 0;
    }

    // The module reports the link up; the row going up is the user's signal
    void onConnected(void) { pending = false; }

    // The user turned the radio off: nothing is to be reported for the pick any more
    void cancel(void) { pending = false; }

    // The module reports the link down; reason is wifi_disconnect_reason of its packet
    bool onDisconnected(uint8_t reason, char *msg, int max)
    {
        // WIFI_REASON_ASSOC_LEAVE is the module's own leave of the access point it was on,
        // which it does as part of the connect. Its event can follow the acceptance.
        if (!pending || reason == WIFI_REASON_ASSOC_LEAVE) {
            return false;
        }
        pending = false;
        if (wifi_disconnect_is_password(reason)) {
            snprintf(msg, max, "Incorrect password");
        } else {
            fail(msg, max, ssid);
        }
        return true;
    }

    // Called before every wait, so a busy queue cannot hold the timeout off
    bool onTick(uint32_t now, char *msg, int max)
    {
        if (!pending || (int32_t)(deadline - now) > 0) {
            return false;
        }
        pending = false;
        fail(msg, max, ssid);
        return true;
    }
};

#endif /* WIFI_CONNECT_H_ */
