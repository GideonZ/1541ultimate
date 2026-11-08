// The NETCTR reporter of network_interface.cc: a production build has the
// counters but no reporter task; only a bench build (-DNET_COUNTERS_REPORT=1) creates it, and it prints at
// most one NETCTR line per 5 s round, only when a counter changed.
//
// The real text of network_interface.cc from the "Bench builds only" comment up to the InitFunction (the reporter, initLwip
// and the #if lines around them) is cut out by the Makefile and compiled here against FreeRTOS/lwIP
// stand-ins. This file is built twice: as it is (the production configuration, the default of
// net_counters.h) and with -DNET_COUNTERS_REPORT=1.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "../../usb/tests/host_test/host_test.h"
#include "net_counters.h"

#define configMINIMAL_STACK_SIZE 1600
#define configTICK_RATE_HZ 200
#define PRIO_BACKGROUND 1

namespace {

typedef void (*TaskFunction_t)(void *);
struct Created {
    TaskFunction_t fn;
    std::string name;
};
std::vector<Created> created;
std::vector<int> delays;     // the argument of every vTaskDelay
std::string console;         // everything printf wrote
int tcpip_inits = 0;
int stop_after = 0;          // vTaskDelay throws Stop on the call with this number (1-based)
bool bump_each_round = false; // every vTaskDelay returns with a counter moved (a syslog frame per line)

struct Stop {};

void host_printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    console += buf;
}

} // namespace

#define printf host_printf

static void tcpip_init(void *, void *) { tcpip_inits++; }
static int xTaskCreate(TaskFunction_t fn, const char *name, int, void *, int, void *)
{
    created.push_back({fn, name});
    return 1;
}
static void vTaskDelay(int ticks)
{
    delays.push_back(ticks);
    if (bump_each_round) {
        net_counters.wifi_tx_wait[0]++;
    }
    if ((int)delays.size() == stop_after) {
        throw Stop();
    }
}
// X11m's hook (a weak stub in network_interface.cc, defined in wifi.cc); the real file does not call it here
extern "C" void net_counters_module_report(int) {}

#include NETWORK_INTERFACE_REPORT_INC

#undef printf

namespace {

void reset()
{
    created.clear();
    delays.clear();
    console.clear();
    tcpip_inits = 0;
    stop_after = 0;
    bump_each_round = false;
    memset(&net_counters, 0, sizeof(net_counters));
}

int count(const std::string &hay, const char *needle)
{
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) {
        n++;
    }
    return n;
}

} // namespace

#ifdef NET_REPORT_EXPECT_DEFAULT_OFF
// The production configuration is the default of net_counters.h: the build does not define NET_COUNTERS_REPORT
TEST(NetReport, the_default_is_off)
{
    EXPECT_EQ(NET_COUNTERS_REPORT, 0);
}
#endif

#if NET_COUNTERS_REPORT

TEST(NetReport, bench_build_creates_the_reporter_task)
{
    reset();
    initLwip(NULL, NULL);
    EXPECT_EQ(tcpip_inits, 1);
    ASSERT_EQ((int)created.size(), 1);
    EXPECT_TRUE(created[0].name == "Net counters");
}

TEST(NetReport, silent_while_nothing_changes_one_line_per_round_after_a_change)
{
    reset();
    initLwip(NULL, NULL);
    ASSERT_EQ((int)created.size(), 1);
    stop_after = 4;
    try {
        net_counters.wifi_tx_wait[0] = 3;
        created[0].fn(NULL);
    } catch (const Stop &) {
    }
    // 3 delays returned (the 4th threw): round 1 saw the change, rounds 2 and 3 saw none
    EXPECT_EQ(count(console, "NETCTR"), 1);
    EXPECT_TRUE(console.find("NETCTR wtx=3/0/0/0/0/0 wtxmax=0 ovf=0 rxlong=0 unlocked=0\n") == 0);
    for (size_t i = 0; i < delays.size(); i++) {
        EXPECT_EQ(delays[i], 5 * configTICK_RATE_HZ);
    }
}

TEST(NetReport, a_line_per_round_at_most_even_when_every_counter_moves)
{
    reset();
    initLwip(NULL, NULL);
    // The worst case of the self-trigger through a syslog over WiFi: every line printed moves a counter
    // before the next round. Still one line per 5 s round, never faster.
    stop_after = 7;
    bump_each_round = true;
    try {
        created[0].fn(NULL);
    } catch (const Stop &) {
    }
    EXPECT_EQ((int)delays.size(), 7);
    EXPECT_EQ(count(console, "NETCTR"), 6);   // 6 rounds returned from vTaskDelay, one line each
}

#else

TEST(NetReport, production_build_creates_no_reporter_task)
{
    reset();
    net_counters.wifi_tx_wait[0] = 5;   // counters are live and changing
    net_counters_wifi_tx_wait(0, true);
    initLwip(NULL, NULL);
    EXPECT_EQ(tcpip_inits, 1);          // the rest of initLwip is untouched
    EXPECT_EQ((int)created.size(), 0);
    EXPECT_EQ((int)delays.size(), 0);
    EXPECT_EQ((int)console.size(), 0);  // and nothing is printed
    EXPECT_EQ(net_counters.wifi_tx_wait[0], (uint32_t)6);   // the counters themselves still count
}

#endif
