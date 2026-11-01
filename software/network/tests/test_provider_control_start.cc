// Actual ESP32 startup routine with controlled RTOS and UART boundaries.
#include "provider_control_task.h"
#include "provider_control.h"
#include "freertos/task.h"
extern "C" {
#include "my_uart.h"
#include "provider_boot.h"
}
#include <stdio.h>
#include <string.h>
#include <deque>
#ifdef CONFIG_ULTIMATE_PROVIDER_TLS
#include "provider_tls_platform.h"
// Only the provisioning boundary is controlled here; real roots, crypto and
// callbacks are exercised by the embedded runtime fixture.
static unsigned configurations;
EspProviderTlsPlatform::EspProviderTlsPlatform() : socket_(-1),roots_{},configured_(false),system_roots_(false),
    clock_ready_(0),entropy_ready_(0),cancel_(0),cancel_context_(0),policy_(0),policy_generation_(0) {}
EspProviderTlsPlatform::~EspProviderTlsPlatform() {}
bool EspProviderTlsPlatform::configure(const unsigned char*,size_t,bool (*)(),bool (*)()) { ++configurations; configured_=true; return true; }
bool EspProviderTlsPlatform::configure(const unsigned char*,size_t,provider_data::TlsPolicy&) { ++configurations; configured_=true; return true; }
bool EspProviderTlsPlatform::configure_system_roots(provider_data::TlsPolicy&) { ++configurations; configured_=true; return true; }
tls_stream_port EspProviderTlsPlatform::callbacks() { return {}; }
void EspProviderTlsPlatform::bind_cancel(bool (*)(void*),void*) {}
#endif
using namespace provider_control;
static unsigned checks,failures,queues,tasks,deletes,submits;
static bool boot_ok=true,queue_ok=true,task_ok=true;
static uint8_t boot[16]={1};
static std::deque<Event> events;
static void check(bool ok,const char *name) { ++checks; if (!ok) { ++failures; printf("FAIL %s\n",name); } }
const uint8_t *provider_boot_identity() { return boot_ok ? boot : 0; }
int64_t esp_timer_get_time() { return 10000; }
QueueHandle_t xQueueCreate(UBaseType_t count,UBaseType_t size) {
    ++queues; check(count==QueueDepth && size==sizeof(Event),"bounded event queue");
    return queue_ok ? &events : 0;
}
BaseType_t xQueueSend(QueueHandle_t q,const void *event,TickType_t wait) {
    ++submits; check(q==&events && !wait,"zero wait ingress on owned queue");
    if (events.size()==QueueDepth) return pdFALSE;
    events.push_back(*static_cast<const Event*>(event)); return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q,void *event,TickType_t wait) {
    check(q==&events && !wait,"zero wait worker receive");
    if (events.empty()) return pdFALSE;
    *static_cast<Event*>(event)=events.front(); events.pop_front(); return pdTRUE;
}
void vQueueDelete(QueueHandle_t q) { ++deletes; check(q==&events,"delete failed startup queue"); events.clear(); }
BaseType_t xTaskCreate(TaskFunction_t,const char*,unsigned stack,void*,UBaseType_t priority,TaskHandle_t*) {
    ++tasks;
#ifdef CONFIG_ULTIMATE_PROVIDER_TLS
    check(stack==12288 && priority==1,"TLS worker budget");
#else
    check(stack==4096 && priority==1,"control-only worker budget");
#endif
    return task_ok ? pdPASS : pdFALSE;
}
void vTaskDelay(TickType_t) {}
BaseType_t my_uart_get_buffer(uint8_t,command_buf_t**,TickType_t) { return pdFALSE; }
BaseType_t my_uart_try_transmit_packet(uint8_t,command_buf_t*) { return pdFALSE; }
int main(int argc,char **argv) {
    const char *mode=argc>1 ? argv[1] : "ok";
    boot_ok=strcmp(mode,"boot"); queue_ok=strcmp(mode,"queue"); task_ok=strcmp(mode,"task");
    const bool success=boot_ok && queue_ok && task_ok;
    uint8_t reserved[]={2,0xfe},ordinary[]={1,0xfe};
    check(provider_control_receive(reserved,sizeof(reserved)) && !submits,"reserved ingress before startup consumed without work");
    check(!provider_control_receive(ordinary,sizeof(ordinary)),"ordinary dispatcher traffic unchanged");
    provider_control_start();
    check(queues==unsigned(boot_ok) && tasks==unsigned(boot_ok && queue_ok),"failed prerequisite prevents later startup steps");
    check(deletes==unsigned(boot_ok && queue_ok && !task_ok),"task failure releases queue once");
    unsigned old_queues=queues,old_tasks=tasks;
    boot_ok=queue_ok=task_ok=true; boot[0]=2;
    provider_control_start(); provider_control_start();
    check(queues==old_queues && tasks==old_tasks,"startup cannot retry under the same process boot");
    Frame hello={}; hello.opcode=Offer; hello.request=1; hello.grant.owner.bytes[0]=1;
    uint8_t bytes[FrameSize]; encode(hello,bytes);
    check(provider_control_receive(bytes,sizeof(bytes)),"reserved valid frame consumed");
    check((submits!=0)==success,"failed startup never admits ingress after prerequisites recover");
    check(!provider_control_configure_tls(0,0,0,0),"late callback provisioning refused");
#ifdef CONFIG_ULTIMATE_PROVIDER_TLS
    struct Lock : provider_data::PolicyLock { void lock() override {} void unlock() override {} } lock;
    provider_data::TlsPolicy policy(lock);
    check(!provider_control_provision_tls(0,0,policy) && !provider_control_provision_system_tls(policy),"late private and system provisioning refused");
    check(!configurations,"failed startup cannot reach platform provisioning");
#endif
    printf("Provider control startup: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
