#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "network_config.h"
#include "rtc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "itu.h"
#include "sntp_time.h"

// The last time SNTP gave, UTC, and the tick it came at; 0 before the first.
static uint32_t sntp_seconds;
static TickType_t sntp_tick;

extern "C" {
    #include "lwip/apps/sntp.h"

    void sntp_time_received(uint32_t sec)
    {
        printf("--> Time Received: %u  (current TZ = %s)\n", sec, getenv("TZ"));
        ENTER_SAFE_SECTION;
        sntp_seconds = sec;
        sntp_tick = xTaskGetTickCount();
        LEAVE_SAFE_SECTION;
        rtc.set_time_utc(sec);
    }
}

uint32_t sntp_utc_now(void)
{
    uint32_t seconds;
    TickType_t tick;
    ENTER_SAFE_SECTION;
    seconds = sntp_seconds;
    tick = sntp_tick;
    LEAVE_SAFE_SECTION;
    if (!seconds) {
        return 0;
    }
    return seconds + (xTaskGetTickCount() - tick) / configTICK_RATE_HZ;
}

void start_sntp(void)
{
    sntp_stop();
    if (networkConfig.cfg->get_value(CFG_NETWORK_NTP_EN) == 0) {
        return;
    }
    const char *timezone = networkConfig.get_posix_timezone();
    printf("Setting timezone: %s\n", timezone);
    setenv("TZ", timezone, 1);
    tzset();
    sntp_setservername(0, networkConfig.cfg->get_string(CFG_NETWORK_NTP_SRV1));
    sntp_setservername(1, networkConfig.cfg->get_string(CFG_NETWORK_NTP_SRV2));
    sntp_setservername(2, networkConfig.cfg->get_string(CFG_NETWORK_NTP_SRV3));
    sntp_init();
}

