/*
 * Host test of wifi_connect.h: what the user is told after the module accepted a
 * connect from the access point list. The acceptance says nothing about the
 * outcome, so there is no message until the module's own event, or the timeout.
 */
#include <stdio.h>
#include <stdint.h>
typedef uint16_t u16_t; // for rpc_calls.h; the stub FreeRTOS.h would widen uint32_t
#include "wifi_connect.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static uint8_t ssid32[32];
static char msg[80];

static void set_ssid(const char *s)
{
    memset(ssid32, 0, sizeof(ssid32));
    strncpy((char *)ssid32, s, 32);
}

static void test_refused(void)
{
    WifiConnectFeedback fb;
    set_ssid("HomeNet");
    fb.begin(ssid32, 1000);
    CHECK(fb.isPending());
    CHECK(!fb.onTick(1001, msg, sizeof(msg))); // the acceptance is not an outcome
    CHECK(fb.onDisconnected(0, msg, sizeof(msg)));
    CHECK(strcmp(msg, "Could not connect to HomeNet") == 0);
    CHECK(!fb.isPending());
    CHECK(!fb.onDisconnected(0, msg, sizeof(msg))); // one message per attempt
    CHECK(fb.waitTicks(1002) == 0xFFFFFFFFUL);
}

// The module's EVENT_DISCONNECTED: a bare header, or with the reason
static int module_disconnected(event_pkt_disconnected *ev, int reason)
{
    memset(ev, 0, sizeof(*ev));
    ev->hdr.command = EVENT_DISCONNECTED;
    ev->hdr.thread = 0xFF;
    ev->reason = reason;
    return reason < 0 ? sizeof(rpc_header_t) : sizeof(*ev);
}

static const char *refused_with(int reason)
{
    WifiConnectFeedback fb;
    set_ssid("HomeNet");
    fb.begin(ssid32, 1000);
    event_pkt_disconnected ev;
    int size = module_disconnected(&ev, reason);
    if (reason < 0) {
        ev.reason = 15; // what follows a bare header is not a reason
    }
    CHECK(fb.onDisconnected(wifi_disconnect_reason(&ev, size), msg, sizeof(msg)));
    return msg;
}

static void test_reasons(void)
{
    CHECK(strcmp(refused_with(-1), "Could not connect to HomeNet") == 0); // an old module
    CHECK(strcmp(refused_with(15), "Incorrect password") == 0);
    CHECK(strcmp(refused_with(202), "Incorrect password") == 0);
    CHECK(strcmp(refused_with(204), "Incorrect password") == 0);
    CHECK(strcmp(refused_with(201), "Could not connect to HomeNet") == 0); // no AP
    CHECK(strcmp(refused_with(2), "Could not connect to HomeNet") == 0);
    CHECK(strcmp(refused_with(9), "Could not connect to HomeNet") == 0);
}

static void test_connected(void)
{
    WifiConnectFeedback fb;
    set_ssid("HomeNet");
    fb.begin(ssid32, 1000);
    fb.onConnected();
    CHECK(!fb.isPending());
    CHECK(!fb.onDisconnected(0, msg, sizeof(msg))); // a later drop is not a failed connect
}

static void test_32_character_ssid(void)
{
    WifiConnectFeedback fb;
    const char *name = "0123456789abcdef0123456789abcdef";
    set_ssid(name);
    fb.begin(ssid32, 5);
    CHECK(fb.onDisconnected(0, msg, sizeof(msg)));
    CHECK(strcmp(msg, "Could not connect to 0123456789abcdef0123456789abcdef") == 0);
}

static void test_timeout(void)
{
    WifiConnectFeedback fb;
    set_ssid("HomeNet");
    fb.begin(ssid32, 1000);
    uint32_t t = fb.waitTicks(1000);
    CHECK(t == WIFI_CONNECT_TIMEOUT_TICKS);
    CHECK(t == 20 * 200); // 20 s of 5 ms ticks
    CHECK(fb.waitTicks(1000 + t / 2) == t - t / 2);
    CHECK(!fb.onTick(1000 + t - 1, msg, sizeof(msg)));
    CHECK(fb.waitTicks(1000 + t + 7) == 0);
    CHECK(fb.onTick(1000 + t, msg, sizeof(msg)));
    CHECK(strcmp(msg, "Could not connect to HomeNet") == 0);
    CHECK(!fb.isPending());
    CHECK(!fb.onTick(1000 + t + 1, msg, sizeof(msg)));
}

static void test_tick_wrap(void)
{
    WifiConnectFeedback fb;
    set_ssid("HomeNet");
    fb.begin(ssid32, 0xFFFFFF00UL);
    CHECK(fb.waitTicks(0xFFFFFF00UL) == WIFI_CONNECT_TIMEOUT_TICKS);
    CHECK(!fb.onTick(5, msg, sizeof(msg)));
    CHECK(fb.onTick(0xFFFFFF00UL + WIFI_CONNECT_TIMEOUT_TICKS, msg, sizeof(msg)));
}

static void test_not_a_manual_connect(void)
{
    // The module's own reconnects say nothing to the user
    WifiConnectFeedback fb;
    fb.onConnected();
    CHECK(!fb.onDisconnected(0, msg, sizeof(msg)));
    CHECK(!fb.onTick(1u << 31, msg, sizeof(msg)));
}

// The module's own leave of the AP it was on is no outcome, before or after the acceptance
static void test_leave_is_no_outcome(void)
{
    WifiConnectFeedback fb;
    set_ssid("HomeNet");
    fb.begin(ssid32, 1000);
    CHECK(!fb.onDisconnected(8, msg, sizeof(msg)));
    CHECK(fb.isPending());
    CHECK(fb.onDisconnected(202, msg, sizeof(msg)));
    CHECK(strcmp(msg, "Incorrect password") == 0);
}

static void test_second_attempt(void)
{
    WifiConnectFeedback fb;
    set_ssid("First");
    fb.begin(ssid32, 100);
    set_ssid("Second");
    fb.begin(ssid32, 200);
    CHECK(fb.onDisconnected(0, msg, sizeof(msg)));
    CHECK(strcmp(msg, "Could not connect to Second") == 0);
}

int main(void)
{
    test_refused();
    test_reasons();
    test_connected();
    test_32_character_ssid();
    test_timeout();
    test_tick_wrap();
    test_not_a_manual_connect();
    test_leave_is_no_outcome();
    test_second_attempt();
    printf(failures ? "wifi_connect: %d FAILED\n" : "wifi_connect: all passed\n", failures);
    return failures != 0;
}
