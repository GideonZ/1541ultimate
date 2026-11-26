#include "provider_sources.h"
#include "provider_control_task.h"
#include "provider_tls_policy.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
static provider_data::TlsPolicy *policy;
static bool allocation=true,available=true,set_ok=true,locked;
static int64_t milliseconds;
static int64_t installed;
static unsigned writes,old_generation;
static int checks,failures;
static void check(bool ok,const char *name) { ++checks; if (!ok) { ++failures; printf("FAIL %s\n",name); } }
SemaphoreHandle_t xSemaphoreCreateMutex() { return allocation ? &allocation : 0; }
int xSemaphoreTake(SemaphoreHandle_t p,unsigned timeout) { check(p && timeout==10 && !locked,"bounded clock serialization"); locked=available; return available; }
int xSemaphoreGive(SemaphoreHandle_t) { check(locked,"clock serialization released"); locked=false; return pdTRUE; }
extern "C" int64_t esp_timer_get_time() { return milliseconds*1000; }
bool provider_control_provision_system_tls(provider_data::TlsPolicy& value) { policy=&value; return true; }
extern "C" int settimeofday(const timeval *value,const struct timezone*) noexcept {
    check(policy && !policy->ready(old_generation,milliseconds,installed),"UTC invalidated before setter");
    ++writes; if (!set_ok) return -1;
    installed=value->tv_sec; return 0;
}
static bool ready() { return policy && policy->ready(policy->generation(),milliseconds,installed); }
int main(int argc,char **argv) {
    if (argc>1 && !strcmp(argv[1],"allocation")) {
        allocation=false;
        check(!provider_sources_init() && !provider_sources_init(),"failed initialization cannot retry");
        timeval tv={1800000000,0}; check(!provider_sources_sync_time(&tv) && writes==0,"missing synchronization prevents clock write");
    } else {
        check(provider_sources_init() && !provider_sources_init(),"production roots provisioned exactly once");
        check(!ready(),"startup remains unavailable");
        timeval tv={1800000000,0};
        if (argc>1 && !strcmp(argv[1],"setter")) {
            set_ok=false;
            check(!provider_sources_sync_time(&tv) && !ready(),"failed system clock setter stays closed");
            set_ok=true; check(!provider_sources_sync_time(&tv) && writes==1,"setter failure latched until boot");
        } else {
            check(provider_sources_sync_time(&tv) && !ready(),"SNTP alone does not qualify entropy");
            provider_sources_wifi_started(false); check(!ready(),"failed Wi-Fi start provides no entropy");
            provider_sources_wifi_started(true); check(ready(),"running RF and fresh SNTP qualify TLS");
            old_generation=policy->generation(); provider_sources_wifi_stopping();
            check(!ready(),"entropy revoked before driver stop");
            provider_sources_wifi_started(true);
            check(ready() && !policy->ready(old_generation,0,installed),"restart permits only fresh sessions");
            old_generation=policy->generation(); milliseconds=3600000; tv.tv_sec+=3600;
            check(provider_sources_sync_time(&tv) && ready(),"hourly SNTP renews bounded clock");
            check(!policy->ready(old_generation,milliseconds,installed),"clock renewal retires previous sessions");
            milliseconds+=7200000; installed+=7200; check(!ready(),"clock expires at two hours");
            old_generation=policy->generation(); tv.tv_sec+=7200;
            check(provider_sources_sync_time(&tv) && ready(),"new in-range SNTP restores fresh sessions");
            available=false; old_generation=policy->generation();
            check(!provider_sources_sync_time(&tv) && !ready(),"busy clock setter fails closed without update");
            available=true; check(provider_sources_sync_time(&tv) && ready(),"bounded contention can recover");
            old_generation=policy->generation(); tv.tv_sec+=301;
            unsigned before=writes;
            check(!provider_sources_sync_time(&tv) && !ready() && writes==before,"large jump never changes system clock");
            tv.tv_sec-=301; check(!provider_sources_sync_time(&tv) && writes==before,"jump rejection latched until boot");
        }
    }
    printf("Provider sources: %d checks, %d failures\n",checks,failures); return failures ? 1 : 0;
}
