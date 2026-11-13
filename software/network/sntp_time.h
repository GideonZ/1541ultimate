#ifndef SNTP_TIME_H
#define SNTP_TIME_H

#include <stdint.h>

void start_sntp();

// UTC seconds, counted on from the last time SNTP gave; 0 when it has not.
uint32_t sntp_utc_now(void);

#endif
