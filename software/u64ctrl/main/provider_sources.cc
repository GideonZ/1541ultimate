#include "sdkconfig.h"
#include "provider_sources.h"
#ifdef CONFIG_ULTIMATE_PROVIDER_SOURCES
#include "provider_control_task.h"
#include "provider_tls_policy.h"
#include "provider_sntp_clock.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
namespace {
class Lock : public provider_data::PolicyLock {
    portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
public:
    void lock() override { portENTER_CRITICAL(&mux); }
    void unlock() override { portEXIT_CRITICAL(&mux); }
} lock;
provider_data::TlsPolicy policy(lock);
provider_data::SntpClock clock_source;
SemaphoreHandle_t time_mutex;
bool attempted;
}
bool provider_sources_init(void) {
    if (attempted) return false;
    attempted=true; time_mutex=xSemaphoreCreateMutex();
    return time_mutex && provider_control_provision_system_tls(policy);
}
void provider_sources_wifi_started(bool success) { policy.entropy_available(success); }
void provider_sources_wifi_stopping(void) { policy.entropy_available(false); }
bool provider_sources_sync_time(const timeval *value) {
    // Invalidate every old session before touching the UTC clock used by TLS.
    if (!value || !time_mutex || xSemaphoreTake(time_mutex,pdMS_TO_TICKS(10))!=pdTRUE) {
        policy.revoke_clock(); return false;
    }
    policy.revoke_clock();
    const int64_t now=esp_timer_get_time()/1000;
    bool ok=now>=0 && value->tv_usec>=0 && value->tv_usec<1000000 &&
        clock_source.accept(value->tv_sec,int32_t(value->tv_usec),uint64_t(now));
    if (ok) ok=settimeofday(value,0)==0;
    if (ok) ok=policy.qualify_clock(value->tv_sec,uint64_t(now),7200000,5);
    if (!ok) clock_source.reject();
    xSemaphoreGive(time_mutex); return ok;
}
#else
bool provider_sources_init(void) { return false; }
void provider_sources_wifi_started(bool) {}
void provider_sources_wifi_stopping(void) {}
bool provider_sources_sync_time(const timeval*) { return false; }
#endif
