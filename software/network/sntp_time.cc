#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "rtc.h"

extern "C" {
    #include "lwip/apps/sntp.h"

    void print_time_for_zone(uint32_t sec, const char *zone)
    {
        char strftime_buf[64];
        time_t now;
        struct tm timeinfo;
        setenv("TZ", zone, 1);
        tzset();
        now = sec;
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        printf("The current date/time in zone %s is: %s\n", zone, strftime_buf);
    }

    void sntp_time_received(uint32_t sec)
    {
        printf("--> Time Received: %u\n", sec);
        rtc.set_time_utc(sec);
    }
}

void start_sntp(void)
{
    sntp_stop();
    sntp_setservername(0, "time.windows.com");
    sntp_setservername(1, "time.google.com");
    sntp_setservername(2, "pool.ntp.org");
    sntp_init();
}

