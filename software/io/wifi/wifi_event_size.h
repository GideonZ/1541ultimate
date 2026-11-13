#ifndef WIFI_EVENT_SIZE_H_
#define WIFI_EVENT_SIZE_H_

/*
 * wifi_event_too_short(): the event is shorter than what the task reads from it. A module that runs a damaged frame as a
 * command answers it with the command byte echoed, which can be an event code (R10B on a C64U: "Could not connect to
 * <garbage>" from an EVENT_CONNECTED made of 8 bytes of "not implemented" and the SSID of whatever the buffer held before).
 * Only what the task reads is asked for, so that an older module with shorter events still works: EVENT_CONNECTED its 32
 * byte SSID (and EVENT_CONNECT_PENDING, the same payload), EVENT_DISCONNECTED its reason only through wifi_disconnect_reason(), which asks for
 * the size itself (a bare header means "not known", so it is let through), EVENT_GOTIP its address and the flag after the gateway, EVENT_RECV_PACKET its length and that many bytes; every other
 * event the header. The modules send EVENT_GOTIP whole (sizeof(event_pkt_got_ip), wifi_modem.c).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rpc_calls.h"
#include "wifi_connect.h" // EVENT_CONNECT_PENDING

static inline bool wifi_event_too_short(const uint8_t *data, int size)
{
    if (size < (int)sizeof(rpc_header_t)) {
        return true;
    }
    switch (((const rpc_header_t *)data)->command) {
    case EVENT_CONNECTED:
    case EVENT_CONNECT_PENDING: // internal, from the user interface: the SSID of the attempt, read by feedback.begin()
        return size < (int)(offsetof(event_pkt_connected, ssid) + sizeof(((event_pkt_connected *)0)->ssid));
    case EVENT_GOTIP:
        return size < (int)(offsetof(event_pkt_got_ip, changed) + sizeof(((event_pkt_got_ip *)0)->changed)); // the handler prints ip and changed
    case EVENT_RECV_PACKET: {
        int head = (int)offsetof(rpc_rx_pkt, data);
        return (size < head) || (((const rpc_rx_pkt *)data)->len > (uint32_t)(size - head));
    }
    default:
        return false;
    }
}

#endif /* WIFI_EVENT_SIZE_H_ */
