#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "network_config.h"
#include "rtc.h"

extern "C" {
    #include "lwip/apps/sntp.h"

    void sntp_time_received(uint32_t sec)
    {
        printf("--> Time Received: %u  (current TZ = %s)\n", sec, getenv("TZ"));
        rtc.set_time_utc(sec);
    }
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

