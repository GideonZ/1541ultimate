/*
 * weak_hooks_check.cpp
 *
 * wifi_radio.c linked WITHOUT an entropy gate, as in the U2+L module: the weak defaults of the
 * radio_entropy_* hooks must let every change through and hold nothing.
 */

#include "host_test/host_test.h"
extern "C" {
#include "wifi_radio.h"
#include "wifi_radio_port.h"
}

static int starts, stops;

extern "C" int wr_port_wifi_start(void)
{
    starts++;
    return 0;
}

extern "C" int wr_port_wifi_stop(void)
{
    stops++;
    return 0;
}

TEST(WeakHooks, start_and_stop_go_through)
{
    EXPECT_TRUE(radio_start() == 0);
    EXPECT_TRUE(radio_is_on());
    EXPECT_TRUE(starts == 1);
    EXPECT_TRUE(radio_stop() == 0);
    EXPECT_TRUE(!radio_is_on());
    EXPECT_TRUE(stops == 1);
}
