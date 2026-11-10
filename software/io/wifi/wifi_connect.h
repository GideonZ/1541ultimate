/*
 * wifi_connect.h
 *
 * What a manual connect from the access point list tells the user. The module
 * answers the connect request as soon as ESP-IDF has accepted the attempt, which
 * says nothing about the outcome: that is the module's own EVENT_CONNECTED, or an
 * EVENT_DISCONNECTED when the access point refuses the client. The task notes the
 * attempt here before the request goes out, and the outcome (or the lack of one, or
 * the module's refusal of the request) becomes a message.
 */
#ifndef WIFI_CONNECT_H_
#define WIFI_CONNECT_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rpc_calls.h"

// Internal events of a connect from the AP list. PENDING is queued by the user
// interface before the request goes out; the task takes it in order with the module's
// events, so an outcome the module reports before its answer to the request arrives
// comes after it. It carries the same payload as the module's EVENT_CONNECTED, and the
// number of the attempt in hdr.sequence. The module's answer is left for the task in
// the WiFi object (WiFi :: ConnectAnswered); ANSWER only wakes the task for it.
#define EVENT_CONNECT_PENDING  0x7F
#define EVENT_CONNECT_ANSWER   0x7D

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

// The module's own EVENT_DISCONNECTED when it turns its radio on (for an Enable, or for a
// connect while its radio was off): a packet with the reason, which is 0. ESP-IDF gives
// every disconnect of the station a reason, and 0 is none of them. No outcome of a connect,
// which it can follow: the attempt is noted before the request goes out.
static inline bool wifi_disconnect_is_radio_start(const void *pkt, int size)
{
    return (size >= (int)sizeof(event_pkt_disconnected)) && (((const event_pkt_disconnected *)pkt)->reason == 0);
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

// The message for a connect that failed. ssid32 is an SSID as the scan records and the
// module's events hold it: 32 bytes, with no terminator when it has 32 characters. It is
// printed from a terminated copy with a plain %s, because the firmware's printf
// (small_printf.cc) has no precision.
static inline void wifi_connect_failed_text(char *msg, int max, const char *ssid32)
{
    char name[33];
    strncpy(name, ssid32, 32);
    name[32] = 0;
    snprintf(msg, max, "Could not connect to %s", name);
}

class WifiConnectFeedback
{
    bool pending;
    bool radio_starting; // the radio was off at the pick: the module reports its start
    uint32_t deadline;
    char ssid[33];

    static void fail(char *msg, int max, const char *ssid)
    {
        wifi_connect_failed_text(msg, max, ssid);
    }
public:
    WifiConnectFeedback() : pending(false), radio_starting(false), deadline(0) { ssid[0] = 0; }

    // A connect request goes out; the outcome is still to come. radio_off: the module's
    // radio is off, so the module reports the start of its radio before the outcome.
    void begin(const uint8_t *ssid32, uint32_t now, bool radio_off = false)
    {
        memcpy(ssid, ssid32, 32);
        ssid[32] = 0;
        deadline = now + WIFI_CONNECT_TIMEOUT_TICKS;
        radio_starting = radio_off;
        pending = true;
    }

    // The module accepted the request at the time at: the outcome is timed from then, as
    // the request can wait longer for its answer than the outcome is waited for
    void accepted(uint32_t at)
    {
        if (pending) {
            deadline = at + WIFI_CONNECT_TIMEOUT_TICKS;
        }
    }

    // The module did not accept the request: the message, once (not when the attempt has
    // ended already: its timeout ran out while the request waited, or WiFi was turned off)
    bool rejected(char *msg, int max)
    {
        if (!pending) {
            return false;
        }
        pending = false;
        fail(msg, max, ssid);
        return true;
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

    // The module reports the link up, to the AP with ssid32; the row going up is the user's
    // signal. A join of another AP (one the module knows) is not the outcome of the pick.
    void onConnected(const uint8_t *ssid32)
    {
        if (strncmp(ssid, (const char *)ssid32, 32) == 0) {
            pending = false;
        }
    }

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
        // A module without the reason field (its event is a bare header: reason 0)
        // reports the start of its radio as it reports a disconnect: the first one after
        // a pick with the radio off is taken for that
        if ((reason == 0) && radio_starting) {
            radio_starting = false;
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
