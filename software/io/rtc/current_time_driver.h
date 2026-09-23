#ifndef CURRENT_TIME_DRIVER_H
#define CURRENT_TIME_DRIVER_H

// The bodies of the two clock entry points, for the real time clock drivers to compile.
// Every driver declares its own Rtc class and its own rtc object, with these members in
// common, and exactly one driver is linked into a build, so the bodies live here rather
// than once per driver. A driver includes this after its own header; everything that only
// calls the clock includes current_time.h instead.

#include "current_time.h"
#include "rtc_epoch.h"

extern "C" void get_current_time(int& wd, int& year, int& month, int& day, int& hour, int& min, int& sec)
{
    rtc.get_time(year, month, day, wd, hour, min, sec);
    year += RTC_EPOCH_YEAR; // get_time() counts years from the epoch, callers do not
}

extern "C" bool set_current_time(int wd, int year, int month, int day, int hour, int min, int sec)
{
    int y = year - RTC_EPOCH_YEAR;
    int corr = rtc.get_correction();
    rtc.set_time(y, month, day, wd, hour, min, sec);
    rtc.set_time_in_chip(corr, y, month, day, wd, hour, min, sec);
    return true;
}

#endif /* CURRENT_TIME_DRIVER_H */
