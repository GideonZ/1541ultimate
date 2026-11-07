/*
 * rpc_size_test.cpp
 *
 * A frame shorter than what its command reads is not run from the stale rest of its buffer. The module's
 * rpc_request_too_short() (rpc_dispatch.c, cut out as it is, once per module) and the Ultimate's
 * wifi_event_too_short() (software/io/wifi/wifi_event_size.h). The sizes asked for are those the other side sends:
 * the whole request structure of each command (software/io/wifi/wifi_cmd.cc: BUFARGS), CMD_SEND_PACKET with its data;
 * for the events only what the task reads.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "host_test/host_test.h"
#include "cmd_buffer.h"
#include "rpc_calls.h"
#include "wifi_event_size.h"

#include "rpc_code.inc" // rpc_request_too_short() of the module under test

static command_buf_t buf;
static bool short_request(uint8_t command, int size, uint32_t length = 0)
{
    memset(&buf, 0x5A, sizeof(buf)); // a buffer that held something else before
    rpc_header_t *h = (rpc_header_t *)buf.data;
    h->command = command;
    h->thread = 3;
    h->sequence = 7;
    if (command == CMD_SEND_PACKET && size >= 8) {
        ((rpc_send_eth_req *)buf.data)->length = length;
    }
    buf.size = size;
    return rpc_request_too_short(&buf);
}

// Every command: nothing below the header is accepted, whatever the command
TEST(RpcSize, nothing_below_a_header_is_accepted)
{
    for (int command = 0; command < 256; command++) {
        for (int size = 0; size < (int)sizeof(rpc_header_t); size++) {
            EXPECT_TRUE(short_request((uint8_t)command, size));
        }
    }
}

struct need { uint8_t command; int size; };

// The commands with a request structure: one byte less is too short, the structure itself and more are not
TEST(RpcSize, a_request_structure_must_be_whole)
{
    static const need needs[] = {
        { CMD_SET_BAUD, (int)sizeof(rpc_setbaud_req) },
        { CMD_WIFI_CONNECT, 98 },
        { CMD_GET_TIME, (int)sizeof(rpc_get_time_req) },
#ifdef CMD_SET_SERIAL
        { CMD_SET_SERIAL, (int)sizeof(rpc_set_serial_req) },
        { CMD_SET_POWER_MODE, (int)sizeof(rpc_set_power_mode_req) },
        { CMD_SET_WAKE_ON_WIFI, (int)sizeof(rpc_set_wake_on_wifi_req) },
        { CMD_SET_RECOVERY_URL, (int)sizeof(rpc_set_recovery_url_req) },
#endif
    };
    for (unsigned i = 0; i < sizeof(needs) / sizeof(needs[0]); i++) {
        EXPECT_TRUE(short_request(needs[i].command, needs[i].size - 1));
        EXPECT_FALSE(short_request(needs[i].command, needs[i].size));
        EXPECT_FALSE(short_request(needs[i].command, needs[i].size + 10));
    }
}

// The other commands read the header only
TEST(RpcSize, a_header_is_all_the_other_commands_need)
{
    static const uint8_t commands[] = { CMD_ECHO, CMD_IDENTIFY, CMD_WIFI_SCAN, CMD_WIFI_DISCONNECT, CMD_WIFI_AUTOCONNECT,
        CMD_WIFI_GETMAC, CMD_MODEM_ON, CMD_MODEM_OFF, CMD_WIFI_ENABLE, CMD_WIFI_DISABLE, CMD_WIFI_IS_CONNECTED, CMD_CLEAR_APS,
        0x55 /* unknown: answered not implemented */ };
    for (unsigned i = 0; i < sizeof(commands); i++) {
        EXPECT_FALSE(short_request(commands[i], (int)sizeof(rpc_header_t)));
        EXPECT_FALSE(short_request(commands[i], 40));
    }
}

// CMD_SEND_PACKET: length and data, and the frame must hold the length it claims
TEST(RpcSize, a_packet_holds_the_length_it_claims)
{
    EXPECT_TRUE(short_request(CMD_SEND_PACKET, 7));                      // no length field
    EXPECT_FALSE(short_request(CMD_SEND_PACKET, 8, 0));                  // an empty packet
    EXPECT_TRUE(short_request(CMD_SEND_PACKET, 8, 1));                   // claims a byte it does not have
    EXPECT_FALSE(short_request(CMD_SEND_PACKET, 20, 12));                // exactly
    EXPECT_TRUE(short_request(CMD_SEND_PACKET, 20, 13));                 // one more than it holds
    EXPECT_TRUE(short_request(CMD_SEND_PACKET, 20, 0xFFFFFFFFu));        // junk from the stale rest of a buffer
    EXPECT_FALSE(short_request(CMD_SEND_PACKET, 8 + 1500, 1500));
}

// --- the Ultimate: events ----------------------------------------------------------------------------------------

static uint8_t ev[2048];
static bool short_event(uint8_t command, int size, uint16_t len = 0)
{
    memset(ev, 0x5A, sizeof(ev));
    ev[0] = command;
    ev[1] = 0xFF;
    ev[2] = ev[3] = 0;
    if (command == EVENT_RECV_PACKET && size >= 6) {
        ((rpc_rx_pkt *)ev)->len = len;
    }
    return wifi_event_too_short(ev, size);
}

TEST(EventSize, nothing_below_a_header_is_accepted)
{
    for (int command = 0; command < 256; command++) {
        for (int size = 0; size < (int)sizeof(rpc_header_t); size++) {
            EXPECT_TRUE(short_event((uint8_t)command, size));
        }
    }
}

// An EVENT_CONNECTED of 8 bytes (a module's "not implemented" answer to a command byte that is its code) has no SSID: the task
// would take one from the buffer's earlier life
TEST(EventSize, a_connected_event_holds_its_ssid)
{
    int need = (int)(offsetof(event_pkt_connected, ssid) + 32);
    EXPECT_TRUE(short_event(EVENT_CONNECTED, 8));
    EXPECT_TRUE(short_event(EVENT_CONNECTED, need - 1));
    EXPECT_FALSE(short_event(EVENT_CONNECTED, need));
}

TEST(EventSize, a_got_ip_event_holds_the_address)
{
    int need = (int)(offsetof(event_pkt_got_ip, ip) + 4);
    EXPECT_TRUE(short_event(EVENT_GOTIP, 4));
    EXPECT_TRUE(short_event(EVENT_GOTIP, need - 1));
    EXPECT_FALSE(short_event(EVENT_GOTIP, need));
    EXPECT_FALSE(short_event(EVENT_GOTIP, (int)sizeof(event_pkt_got_ip)));
}

TEST(EventSize, a_packet_event_holds_the_length_it_claims)
{
    int head = (int)offsetof(rpc_rx_pkt, data);
    EXPECT_TRUE(short_event(EVENT_RECV_PACKET, head - 1));
    EXPECT_FALSE(short_event(EVENT_RECV_PACKET, head, 0));
    EXPECT_TRUE(short_event(EVENT_RECV_PACKET, head + 10, 11));
    EXPECT_FALSE(short_event(EVENT_RECV_PACKET, head + 10, 10));
    EXPECT_TRUE(short_event(EVENT_RECV_PACKET, 8, 0xFFFF));
    EXPECT_FALSE(short_event(EVENT_RECV_PACKET, head + 1500, 1500));
}

// Events that carry nothing the task reads are only a header (an older module's disconnect has no reason, the keepalive none)
TEST(EventSize, a_header_is_all_the_other_events_need)
{
    static const uint8_t events[] = { EVENT_DISCONNECTED, EVENT_RESCAN, EVENT_KEEPALIVE, EVENT_DISABLED, 0xF1, 0xF2, 0x55 };
    for (unsigned i = 0; i < sizeof(events); i++) {
        EXPECT_FALSE(short_event(events[i], (int)sizeof(rpc_header_t)));
    }
}
