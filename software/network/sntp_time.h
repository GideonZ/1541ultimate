#ifndef SNTP_TIME_H
#define SNTP_TIME_H

typedef struct {
    const char *timezone;
    const char *utc;
    const char *location;
    const char *posix;
} timezone_entry_t;

void start_sntp();

#endif
