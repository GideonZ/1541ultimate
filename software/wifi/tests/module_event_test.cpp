/*
 * module_event_test.cpp
 *
 * Fix 6: what the ESP32 modules put on the UART for a disconnect and a connect of the
 * station. EVENT_DISCONNECTED is the header plus one byte (padded to
 * sizeof(event_pkt_disconnected)), the reason ESP-IDF gives (0 when it gives none), so that
 * an Ultimate that does not know the byte reads the header only; EVENT_CONNECTED is
 * unchanged. The control module also makes up disconnects, which say 0. wifi_event_handler
 * and send_event are the module's own.
 */

#include <stddef.h>
#include <string.h>
#include "module_script.h"

extern "C" {
void module_event(int event, const wifi_event_sta_disconnected_t *data);
void module_send_event(uint8_t evcode, uint8_t reason);
}

TEST(Event, disconnect_carries_the_reason)
{
    EXPECT_EQ(offsetof(event_pkt_disconnected, reason), sizeof(rpc_header_t));
    wifi_event_sta_disconnected_t d;
    memset(&d, 0, sizeof(d));
    static const uint8_t reasons[] = { 15, 202, 204, 8, 1 };
    for (unsigned i = 0; i < sizeof(reasons); i++) {
        d.reason = reasons[i];
        module_event(WIFI_EVENT_STA_DISCONNECTED, &d);
        EXPECT_EQ(sent_n, 1);
        EXPECT_EQ((int)sent.data[0], EVENT_DISCONNECTED);
        EXPECT_EQ((int)sent.data[1], 0xFF); // thread: for the task's queue
        EXPECT_EQ(sent.size, (int)sizeof(event_pkt_disconnected));
        EXPECT_EQ((int)sent.data[sizeof(rpc_header_t)], (int)reasons[i]);
    }
}

TEST(Event, disconnect_without_data_says_unknown)
{
    module_event(WIFI_EVENT_STA_DISCONNECTED, NULL);
    EXPECT_EQ(sent_n, 1);
    EXPECT_EQ(sent.size, (int)sizeof(event_pkt_disconnected));
    EXPECT_EQ((int)sent.data[sizeof(rpc_header_t)], (int)0);
}

TEST(Event, connect_event_is_unchanged)
{
    strcpy(last_connect.ssid, "HomeNet");
    module_event(WIFI_EVENT_STA_CONNECTED, NULL);
    EXPECT_EQ(sent_n, 1);
    EXPECT_EQ((int)sent.data[0], EVENT_CONNECTED);
    EXPECT_EQ(sent.size, (int)sizeof(event_pkt_connected));
    EXPECT_EQ(memcmp(((event_pkt_connected *)sent.data)->ssid, "HomeNet", 8), 0);
}

// A disconnect the module makes up does not carry the reason of an earlier one
TEST(Event, made_up_disconnect_says_unknown)
{
    wifi_event_sta_disconnected_t d;
    memset(&d, 0, sizeof(d));
    d.reason = 15;
    module_event(WIFI_EVENT_STA_DISCONNECTED, &d);
    module_send_event(EVENT_DISCONNECTED, 0);
    EXPECT_EQ(sent_n, 1);
    EXPECT_EQ(sent.size, (int)sizeof(event_pkt_disconnected));
    EXPECT_EQ((int)sent.data[sizeof(rpc_header_t)], (int)0);
}
