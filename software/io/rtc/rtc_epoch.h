#ifndef RTC_EPOCH_H
#define RTC_EPOCH_H

// The year the clock chips count from, which is also the year a FAT time stamp
// counts from. Every Rtc::get_time() returns the year as an offset from it, while
// callers outside this directory want a calendar year.
#define RTC_EPOCH_YEAR 1980

#endif /* RTC_EPOCH_H */
