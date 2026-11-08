// The WiFi linkoutput wait histogram of network/net_counters.h: the bucket of a
// wait for a UART TX buffer, in ticks (5 ms): 0, 1, 2-9, 10-99, 100-999, and "timed out".

#include <string.h>
#include "../../usb/tests/host_test/host_test.h"
#include "net_counters.h"

namespace {

void clear() { memset(&net_counters, 0, sizeof(net_counters)); }

// Count one wait and say which bucket took it (-1: none, more than one: -2)
int count_wait(uint32_t ticks, bool got)
{
    clear();
    net_counters_wifi_tx_wait(ticks, got);
    int found = -1;
    for (int i = 0; i < NET_WTX_BUCKETS; i++) {
        if (net_counters.wifi_tx_wait[i] == 1) {
            found = (found == -1) ? i : -2;
        } else if (net_counters.wifi_tx_wait[i] != 0) {
            return -2;
        }
    }
    return found;
}

} // namespace

TEST(NetCountersHistogram, bucket_boundaries)
{
    EXPECT_EQ(count_wait(0, true), 0);
    EXPECT_EQ(count_wait(1, true), 1);
    EXPECT_EQ(count_wait(2, true), 2);
    EXPECT_EQ(count_wait(9, true), 2);
    EXPECT_EQ(count_wait(10, true), 3);
    EXPECT_EQ(count_wait(99, true), 3);
    EXPECT_EQ(count_wait(100, true), 4);
    EXPECT_EQ(count_wait(999, true), 4);
}

// The wait of the 5 s timeout is 1000 ticks, which is not the 100-999 bucket
TEST(NetCountersHistogram, a_timeout_is_its_own_bucket)
{
    EXPECT_EQ(count_wait(1000, false), NET_WTX_TIMEOUT);
    EXPECT_EQ(count_wait(0, false), NET_WTX_TIMEOUT);
}

TEST(NetCountersHistogram, the_maximum_follows_the_longest_wait)
{
    clear();
    net_counters_wifi_tx_wait(3, true);
    net_counters_wifi_tx_wait(250, true);
    net_counters_wifi_tx_wait(7, true);
    EXPECT_EQ(net_counters.wifi_tx_wait_max, (uint32_t)250);
    net_counters_wifi_tx_wait(1000, false);
    EXPECT_EQ(net_counters.wifi_tx_wait_max, (uint32_t)1000);
    EXPECT_EQ(net_counters.wifi_tx_wait[0] + net_counters.wifi_tx_wait[1] + net_counters.wifi_tx_wait[2]
              + net_counters.wifi_tx_wait[3] + net_counters.wifi_tx_wait[4] + net_counters.wifi_tx_wait[5], (uint32_t)4);
}
