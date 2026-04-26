#ifndef SNTP_H
#define SNTP_H

#include "rpc_calls.h"

void configure_sntp_dhcp(void);
void configure_sntp_servers(void);
void obtain_time(void);
void print_time(void);
void sntp_get_time(const char *zone, esp_datetime_t *datetime);
void print_all_zones(void);
void print_time_for_zone(const char *zone);

#endif