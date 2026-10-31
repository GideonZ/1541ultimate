#ifndef PROVIDER_SOURCES_H
#define PROVIDER_SOURCES_H
#include <stdbool.h>
#include <sys/time.h>
#ifdef __cplusplus
extern "C" {
#endif
// Boot before Wi-Fi, SNTP and the owner task. Failure leaves TLS unavailable.
bool provider_sources_init(void);
void provider_sources_wifi_started(bool success);
void provider_sources_wifi_stopping(void);
bool provider_sources_sync_time(const struct timeval*);
#ifdef __cplusplus
}
#endif
#endif
