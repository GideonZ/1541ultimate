#include "rtc_dummy.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

const char *month_strings_short[]={ "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

const char *month_strings_long[] = { "", "January", "February", "March", "April", "May", "June",
                                    "July", "August", "September", "October", "November", "December" };

const char *weekday_strings[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

Rtc::Rtc()
{
}

Rtc::~Rtc()
{
}

void Rtc::get_time(int &y, int &M, int &D, int &wd, int &h, int &m, int &s)
{
    uint32_t sec = RTC_TIMER_SECONDS;

    time_t now;
    struct tm timeinfo;

    now = sec;
    localtime_r(&now, &timeinfo);

    y = timeinfo.tm_year - 80;
    M = timeinfo.tm_mon + 1;
    D = timeinfo.tm_mday;
    wd = timeinfo.tm_wday;
    h = timeinfo.tm_hour;
    m = timeinfo.tm_min;
    s = timeinfo.tm_sec;
}

const char * Rtc::get_time_string(char *dest, int len)
{
    if (len < 9)
        return "";
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
    sprintf(dest, "%02d:%02d:%02d", h, m, s);
    return dest;
}

const char * Rtc::get_date_string(char *dest, int len)
{
    if (len < 14)
        return "";
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
    sprintf(dest, "%s %2d, %4d", month_strings_short[M], D, 1980 + y);
    return dest;
}

const char * Rtc::get_long_date(char *dest, int len)
{
    // Wednesday September 30, 2009
    if (len < 29)
        return "";
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);
    sprintf(dest, "%s %s %2d, %4d", weekday_strings[wd], month_strings_long[M], D, 1980 + y);
    return dest;
}

uint32_t Rtc::get_fat_time(void)
{
    int y, M, D, wd, h, m, s;
    get_time(y, M, D, wd, h, m, s);

    uint32_t result = 0;
    result |= (y << 25);
    result |= (M << 21);
    result |= (D << 16);
    result |= (h << 11);
    result |= (m << 5);
    result |= (s >> 1);

    return result;
}

int Rtc::get_correction(void)
{
    return 0;
}

void Rtc::set_time_in_chip(int corr_ppm, int y, int M, int D, int wd, int h, int m, int s)
{
}

void Rtc::set_time(int y, int M, int D, int wd, int h, int m, int s)
{
    struct tm timeinfo;
    timeinfo.tm_year = y + 80;
    timeinfo.tm_mon = M - 1;
    timeinfo.tm_mday = D;
    timeinfo.tm_wday = wd;
    timeinfo.tm_hour = h;
    timeinfo.tm_min = m;
    timeinfo.tm_sec = s;
    time_t seconds = mktime(&timeinfo);
    RTC_TIMER_SECONDS = seconds;
}

void Rtc::set_time_utc(int seconds)
{
    RTC_TIMER_SECONDS = seconds;
}

Rtc rtc; // global

extern "C" uint32_t get_fattime(void) /* 31-25: Year(0-127 org.1980), 24-21: Month(1-12), 20-16: Day(1-31) */
/* 15-11: Hour(0-23), 10-5: Minute(0-59), 4-0: Second(0-29 *2) */
{
    return rtc.get_fat_time();
}
