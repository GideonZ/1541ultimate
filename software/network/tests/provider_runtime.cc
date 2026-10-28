#include "provider_tls_platform.h"
#include "provider_runtime_metrics.h"
#include "fixture.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <sys/time.h>
#include <string.h>
extern "C" bool provider_runtime_network(void);
static unsigned checks, failures;
static uint32_t clock_ready_flag, entropy_ready_flag;
static bool clock_ready() { return __atomic_load_n(&clock_ready_flag,__ATOMIC_ACQUIRE); }
static bool entropy_ready() { return __atomic_load_n(&entropy_ready_flag,__ATOMIC_ACQUIRE); }
static uint64_t now_ms() { return esp_timer_get_time()/1000; }
static void check(bool passed,const char *label) {
    ++checks; if (!passed) ++failures;
    printf("%s %s\n",passed ? "PASS" : "FAIL",label);
}
struct ControlPort : provider_control::Port {
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    QueueHandle_t queue = xQueueCreate(4,sizeof(provider_control::Event));
    provider_control::Frame response = {};
    void lock() override { portENTER_CRITICAL(&mux); }
    void unlock() override { portEXIT_CRITICAL(&mux); }
    bool enqueue(const provider_control::Event& event) override { return xQueueSend(queue,&event,0)==pdTRUE; }
    bool dequeue(provider_control::Event& event) override { return xQueueReceive(queue,&event,0)==pdTRUE; }
    bool send(const uint8_t *p,size_t size) override { return provider_control::decode(p,size,response); }
    ~ControlPort() { vQueueDelete(queue); }
};
static void submit(provider_control::Worker& worker,provider_control::Frame& frame) {
    uint8_t bytes[provider_control::FrameSize]; provider_control::encode(frame,bytes);
    check(worker.submit(bytes,sizeof(bytes)),"control accepted"); worker.step(now_ms());
}
struct Interrupt {
    provider_control::Worker *worker;
    unsigned action;
    TaskHandle_t waiter;
    uint64_t fired;
};
static void interrupt(void *argument) {
    Interrupt *event = static_cast<Interrupt*>(argument);
    vTaskDelay(pdMS_TO_TICKS(500));
    event->fired=now_ms();
    if (event->action==1) __atomic_store_n(&clock_ready_flag,uint32_t(0),__ATOMIC_RELEASE);
    else if (event->action==2) __atomic_store_n(&entropy_ready_flag,uint32_t(0),__ATOMIC_RELEASE);
    else event->worker->disconnect();
    xTaskNotifyGive(event->waiter); vTaskDelete(0);
}
static void attempt(const char *name,unsigned port,bool good,unsigned action=0,bool timeout=false) {
    printf("ATTEMPT name=%s run=%s\n",name,LAB_RUN_ID);
    __atomic_store_n(&clock_ready_flag,uint32_t(strcmp(name,"no-clock")!=0),__ATOMIC_RELEASE);
    __atomic_store_n(&entropy_ready_flag,uint32_t(strcmp(name,"no-entropy")!=0),__ATOMIC_RELEASE);
    bool no_open = !strcmp(name,"no-clock") || !strcmp(name,"no-entropy") || !strcmp(name,"no-roots");
    size_t before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    {
        EspProviderTlsPlatform platform;
        if (strcmp(name,"no-roots")) check(platform.configure(lab_root,sizeof(lab_root),clock_ready,entropy_ready),"roots configured");
        else check(!platform.configured(),"missing roots unavailable");
        ControlPort wire; check(wire.queue!=0,"bounded control queue");
        provider_session::Identity boot = {}; boot.bytes[0]=1;
        provider_control::Worker worker(wire,boot,provider_session::Required,{1000,5000,1000});
        provider_data::TlsBackend backend(worker,platform);
        provider_control::Frame control = {}; control.opcode=provider_control::Offer; control.request=1; control.grant.owner.bytes[0]=2;
        submit(worker,control); check(wire.response.status==provider_control::Ok,"offer accepted");
        control=wire.response; control.reply=false; control.status=0; control.opcode=provider_control::Initialize; control.request=2; control.required=provider_session::Required;
        submit(worker,control); check(wire.response.status==provider_control::Ok,"grant initialized");
        provider_data::Frame f = {}; f.opcode=provider_data::Open; f.request=1;
        bool read_case = strstr(name,"read")!=0;
        uint32_t budget = timeout ? (read_case ? 3000 : 1500) : 4000;
        f.ticket.grant=wire.response.grant; f.ticket.session=1; f.port=port; f.budget_ms=budget;
        const char *host = !strcmp(name,"hostname") ? "wrong.lab.test" : "tls.lab.test";
        f.length=strlen(host)+1; memcpy(f.payload,host,f.length);
        Interrupt event = {&worker,action,xTaskGetCurrentTaskHandle(),0};
        if (action && !read_case) check(xTaskCreate(interrupt,"Policy revoke",2048,&event,tskIDLE_PRIORITY+2,0)==pdPASS,"interrupt task started");
        uint64_t start=now_ms(); int result=backend.execute(f);
        bool opened = result==0;
        if (good || read_case) {
            check(opened,"authenticated open");
            static const char request[]="GET /probe HTTP/1.1\r\nHost: tls.lab.test\r\nConnection: close\r\n\r\n";
            f.opcode=provider_data::Write; f.length=f.count=sizeof(request)-1; memcpy(f.payload,request,f.length);
            result=opened ? backend.execute(f) : -1;
            check(result==sizeof(request)-1,"exact request sent");
            if (action) check(xTaskCreate(interrupt,"Policy revoke",2048,&event,tskIDLE_PRIORITY+2,0)==pdPASS,"interrupt task started");
            f.opcode=provider_data::Read; f.count=256; f.length=0;
            unsigned char response[256] = {}; unsigned received=0;
            do {
                result=backend.execute(f);
                if (result>0 && received+result<=sizeof(response)) { memcpy(response+received,f.payload,result); received+=result; }
                else if (result>0) { check(false,"response within bound"); break; }
            } while (result>0);
            if (good) {
                static const char header[]="HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\n";
                const unsigned char body[]={0,255,128,13,10,'A',0,10};
                check(result==0 && received==sizeof(header)-1+8 && !memcmp(response,header,sizeof(header)-1) &&
                    !memcmp(response+sizeof(header)-1,LAB_ECDSA ? body : reinterpret_cast<const unsigned char*>("lwip-ok\n"),8),"exact response and authenticated EOF");
            } else check(result<0 && received==0,"cancelled or timed out read publishes no bytes");
        } else check(result<0,"open rejected");
        unsigned elapsed=now_ms()-start;
        auto diagnostic=backend.diagnostic();
        if (timeout) check(diagnostic.status==TLS_TIMEOUT && elapsed>=budget && elapsed<=budget+250,"original deadline bound");
        if (action) { bool joined=ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(1000))>0; check(joined,"interrupt task finished"); configASSERT(joined); check(diagnostic.status==TLS_CANCELLED && event.fired && start+elapsed>=event.fired && start+elapsed-event.fired<=250,"revocation or cancellation bound"); }
        if (no_open) check(!opened && elapsed<=500,"policy rejects before transport");
        if (!good && !no_open && !timeout && !action) check(diagnostic.verify_flags!=0,"certificate rejection recorded");
        worker.disconnect(); worker.step(now_ms());
        check(!worker.occupied() && !backend.occupied(),"owner resources released");
        check(uxTaskGetStackHighWaterMark(0)>=1024,"owner stack reserve");
        printf("RESULT name=%s status=%d flags=%u elapsed_ms=%u cancel_return=%u stack=%u run=%s\n",name,int(diagnostic.status),unsigned(diagnostic.verify_flags),elapsed,unsigned(action ? start+elapsed-event.fired : 0),unsigned(uxTaskGetStackHighWaterMark(0)),LAB_RUN_ID);
    }
    printf("HEAP name=%s before=%u after=%u minimum=%u run=%s\n",name,unsigned(before),unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT)),unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),LAB_RUN_ID);
}
#if LAB_HEAP_CHECK
static provider_runtime_metrics sample(const char *phase,unsigned batch) {
    provider_runtime_metrics value = {};
    configASSERT(provider_runtime_snapshot(&value));
    printf("RESOURCE phase=%s batch=%u ms=%llu free=%u largest=%u tasks=%u active=%u time_wait=%u bound=%u pcb_size=%u pcb_limit=%u run=%s\n",
        phase,batch,static_cast<unsigned long long>(now_ms()),unsigned(value.free_heap),unsigned(value.largest_block),
        unsigned(value.tasks),unsigned(value.active),unsigned(value.time_wait),unsigned(value.bound),
        unsigned(value.pcb_size),unsigned(value.pcb_limit),LAB_RUN_ID);
    return value;
}
static provider_runtime_metrics settle(const char *phase) {
    uint64_t start=now_ms();
    auto value=sample(phase,0);
    while ((value.active || value.time_wait || value.bound) && now_ms()-start<150000) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        value=sample(phase,0);
    }
    check(!value.active && !value.time_wait && !value.bound,"TCP cleanup within 150 seconds");
    // Give the idle task and deferred frees time to finish before comparison.
    vTaskDelay(pdMS_TO_TICKS(2000));
    value=sample(phase,1);
    check(heap_caps_check_integrity_all(true),"settled heap integrity");
    return value;
}
#endif
extern "C" void app_main(void) {
    printf("RUNTIME run=%s profile=%s\n",LAB_RUN_ID,LAB_ECDSA ? "ecdsa" : "rsa");
    check(provider_runtime_network(),"private emulator network ready");
    timeval utc = {}; utc.tv_sec=LAB_UTC_SECONDS; check(settimeofday(&utc,0)==0,"fixture UTC installed");
    {
        EspProviderTlsPlatform p;
        check(!p.configure(0,0,clock_ready,entropy_ready),"empty trust rejected");
        check(!p.configure(lab_root,sizeof(lab_root),0,entropy_ready),"missing clock policy rejected");
        check(!p.configure(lab_root,sizeof(lab_root),clock_ready,0),"missing entropy policy rejected");
        const unsigned char bad[]="invalid";
        check(!p.configure(bad,sizeof(bad),clock_ready,entropy_ready) && !p.configured(),"malformed trust rejected");
        unsigned char partial[sizeof(lab_root)+128] = {};
        snprintf(reinterpret_cast<char*>(partial),sizeof(partial),"%s-----BEGIN CERTIFICATE-----\ninvalid\n-----END CERTIFICATE-----\n",lab_root);
        check(!p.configure(partial,strlen(reinterpret_cast<char*>(partial))+1,clock_ready,entropy_ready) && !p.configured(),"partially parsed trust rejected");
        check(p.configure(lab_root,sizeof(lab_root),clock_ready,entropy_ready),"valid trust after rejection");
        check(!p.configure(lab_root,sizeof(lab_root),clock_ready,entropy_ready),"live trust replacement refused");
        __atomic_store_n(&clock_ready_flag,uint32_t(1),__ATOMIC_RELEASE);
        auto callbacks=p.callbacks();
        timeval unset = {}; check(settimeofday(&unset,0)==0,"unset fixture UTC installed");
        check(!callbacks.time_ready(callbacks.context),"uninitialized UTC rejected despite ready flag");
        check(settimeofday(&utc,0)==0 && callbacks.time_ready(callbacks.context),"qualified fixture UTC restored");
    }
    const size_t roots_baseline=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    for (unsigned i=0;i<8;++i) {
        { EspProviderTlsPlatform p; check(p.configure(lab_root,sizeof(lab_root),clock_ready,entropy_ready),"temporary trust configured"); }
        check(heap_caps_get_free_size(MALLOC_CAP_8BIT)==roots_baseline,"trust allocations released");
    }
#if LAB_HEAP_CHECK
    sample("cold",0);
#endif
    const struct { const char *name; unsigned port,action; bool timeout; } cases[]={
        {"valid",8443,0,false},{"hostname",8443,0,false},{"untrusted",8445,0,false},
        {"expired",8456,0,false},{"future",8457,0,false},{"no-roots",8443,0,false},
        {"no-clock",8443,0,false},{"no-entropy",8443,0,false},
        {"handshake-timeout",8446,0,true},{"read-timeout",8447,0,true},
        {"clock-handshake-revoke",8446,1,false},{"entropy-read-revoke",8447,2,false},
        {"handshake-cancel",8446,3,false},{"read-cancel",8447,3,false}};
    for (const auto& item:cases) {
        attempt(item.name,item.port,!strcmp(item.name,"valid"),item.action,item.timeout);
        attempt("recovery",8443,true);
    }
#if LAB_HEAP_CHECK
    auto baseline=settle("warmup-cleanup");
    for (unsigned batch=1;batch<=3;++batch) {
        for (unsigned request=0;request<32;++request) attempt("repeat",8443,true);
        auto value=sample("batch",batch);
        check(value.active+value.time_wait<=value.pcb_limit && !value.bound,"TCP retention within configured limit");
        check(heap_caps_check_integrity_all(true),"batch heap integrity");
    }
    auto final=settle("repeat-cleanup");
    check(final.free_heap>=baseline.free_heap && final.largest_block>=baseline.largest_block,"quiescent heap and largest block recovered");
    check(final.tasks==baseline.tasks,"temporary tasks released");
#endif
    printf("SUMMARY checks=%u failures=%u run=%s\n",checks,failures,LAB_RUN_ID);
}
