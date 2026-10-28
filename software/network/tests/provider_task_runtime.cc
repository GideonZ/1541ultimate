// Runs the production owner task. Only the UART device and boot identity are
// fixture boundaries; control/data queues, scheduling and TLS are production.
#include "provider_control_task.h"
#include "provider_data.h"
#include "fixture.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <sys/time.h>
extern "C" {
#include "my_uart.h"
bool provider_runtime_network(void);
}
namespace {
unsigned checks, failures;
uint64_t now_ms() { return esp_timer_get_time()/1000; }
void check(bool ok,const char *label) {
    ++checks; if (!ok) ++failures;
    printf("%s %s\n",ok ? "PASS" : "FAIL",label);
}
struct Packet { size_t size; uint64_t ms; uint8_t bytes[provider_data::FrameSize]; };
QueueHandle_t output;
command_buf_t buffer;
bool borrowed;
TaskHandle_t owner_task;
uint32_t fault, uart_errors, sent, monitor_stop, monitor_ticks, monitor_gap;
uint32_t load(uint32_t& n) { return __atomic_load_n(&n,__ATOMIC_ACQUIRE); }
void store(uint32_t& n,uint32_t v) { __atomic_store_n(&n,v,__ATOMIC_RELEASE); }
void uart_error() { __atomic_fetch_add(&uart_errors,1,__ATOMIC_RELAXED); }
bool policy_ready() { return true; }
void monitor(void *context) {
    uint64_t previous=now_ms();
    while (!load(monitor_stop)) {
        vTaskDelay(pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1);
        uint64_t current=now_ms();
        uint32_t gap=current-previous;
        if (gap>load(monitor_gap)) store(monitor_gap,gap);
        __atomic_fetch_add(&monitor_ticks,1,__ATOMIC_RELAXED);
        previous=current;
    }
    xTaskNotifyGive(static_cast<TaskHandle_t>(context)); vTaskDelete(0);
}
void send_control(const provider_control::Frame& frame) {
    uint8_t bytes[provider_control::FrameSize]; provider_control::encode(frame,bytes);
    configASSERT(provider_control_receive(bytes,sizeof(bytes)));
    memset(bytes,0xa5,sizeof(bytes)); // Ingress must retain its own copy.
}
void send_data(const provider_data::Frame& frame) {
    uint8_t bytes[provider_data::FrameSize]; configASSERT(provider_data::encode(frame,bytes));
    configASSERT(provider_control_receive(bytes,sizeof(bytes)));
    memset(bytes,0xa5,sizeof(bytes));
}
bool control_exchange(provider_control::Frame& frame) {
    const uint64_t request=frame.request;
    send_control(frame);
    uint64_t start=now_ms(); Packet packet;
    while (now_ms()-start<500) {
        if (xQueueReceive(output,&packet,pdMS_TO_TICKS(10))!=pdTRUE) continue;
        provider_control::Frame reply;
        if (provider_control::decode(packet.bytes,packet.size,reply) && reply.reply && reply.request==request) {
            printf("CONTROL opcode=%u request=%llu status=%u counter=%llu ms=%llu\n",unsigned(reply.opcode),static_cast<unsigned long long>(reply.request),unsigned(reply.status),static_cast<unsigned long long>(reply.counter),static_cast<unsigned long long>(packet.ms));
            frame=reply; return reply.status==provider_control::Ok;
        }
        uart_error();
    }
    return false;
}
enum Mode { Valid, Hostname, Untrusted, Timeout, Renewed, Missing, Close, Restart, ControlOverflow, DataOverflow, BufferLoss, SendLoss };
const char *names[]={"valid","hostname","untrusted","timeout","renewed","missing","close","restart","control-overflow","data-overflow","buffer-loss","send-loss"};
struct Call {
    provider_session::Grant grant;
    uint64_t control_id, renew_sequence, next_renew, signal_ms;
    unsigned renew_sent, renew_ok, max_ack_ms;
    uint64_t renew_at;
    bool terminal_ack;
};
bool exchange(provider_data::Frame& f,Call& call,Mode mode,bool reading) {
    if (reading && mode==BufferLoss) store(fault,1);
    if (reading && mode==SendLoss) store(fault,2);
    send_data(f);
    printf("DATA_SEND opcode=%u request=%llu budget=%u ms=%llu\n",unsigned(f.opcode),static_cast<unsigned long long>(f.request),unsigned(f.budget_ms),static_cast<unsigned long long>(now_ms()));
    const uint64_t start=now_ms(), request=f.request;
    bool injected=false;
    Packet packet;
    while (now_ms()-start<f.budget_ms+750) {
        uint64_t now=now_ms();
        if (reading && !injected && mode>=Close && mode<=DataOverflow && now-start>=500) {
            injected=true; call.signal_ms=now;
            provider_control::Frame event={}; event.request=++call.control_id; event.grant=call.grant;
            event.opcode=provider_control::Close; event.counter=1;
            if (mode==Restart) { event.opcode=provider_control::Offer; event.counter=0; event.grant={}; event.grant.owner.bytes[0]=99; }
            if (mode==ControlOverflow) {
                event.opcode=provider_control::Renew;
                // Burst without yielding on the ingress core; the outcome below
                // must still prove overflow fencing on the SMP scheduler.
                vTaskSuspendAll();
                for (unsigned i=0;i<provider_control::QueueDepth+1;++i) { event.counter=++call.renew_sequence; send_control(event); }
                xTaskResumeAll();
            } else if (mode==DataOverflow) send_data(f);
            else send_control(event);
        }
        bool loss=mode==ControlOverflow || mode==DataOverflow || mode==BufferLoss || mode==SendLoss;
        if (reading && loss && now-start>=750) return false;
        if (mode!=Missing && mode<Close && now>=call.next_renew) {
            provider_control::Frame renew={}; renew.opcode=provider_control::Renew;
            renew.request=++call.control_id; renew.grant=call.grant; renew.counter=++call.renew_sequence;
            send_control(renew); call.renew_at=now; ++call.renew_sent; call.next_renew=now+1000;
        }
        if (xQueueReceive(output,&packet,pdMS_TO_TICKS(10))!=pdTRUE) continue;
        provider_control::Frame control;
        if (provider_control::decode(packet.bytes,packet.size,control)) {
            printf("CONTROL opcode=%u request=%llu status=%u counter=%llu ms=%llu\n",unsigned(control.opcode),static_cast<unsigned long long>(control.request),unsigned(control.status),static_cast<unsigned long long>(control.counter),static_cast<unsigned long long>(packet.ms));
            if (control.opcode==provider_control::Renew) {
                if (control.status==provider_control::Ok) ++call.renew_ok;
                unsigned delay=packet.ms-call.renew_at;
                if (delay>call.max_ack_ms) call.max_ack_ms=delay;
            } else if ((control.opcode==provider_control::Close || control.opcode==provider_control::Offer) && control.status==provider_control::Ok) call.terminal_ack=true;
            else uart_error();
            continue;
        }
        provider_data::Frame response;
        if (!provider_data::decode(packet.bytes,packet.size,response) || !response.reply || response.request!=request || !provider_data::same_ticket(response.ticket,f.ticket)) { uart_error(); continue; }
        printf("DATA_REPLY opcode=%u request=%llu status=%u count=%u ms=%llu\n",unsigned(response.opcode),static_cast<unsigned long long>(response.request),unsigned(response.status),unsigned(response.count),static_cast<unsigned long long>(packet.ms));
        f=response; return true;
    }
    return false;
}
void attempt(Mode mode,unsigned ordinal,bool recovery) {
    const char *name=recovery ? "recovery" : names[mode];
    printf("TASK_ATTEMPT ordinal=%u name=%s run=%s\n",ordinal,name,LAB_RUN_ID);
    check(uxQueueMessagesWaiting(output)==0,"no stale UART output");
    Call call={}; call.control_id=uint64_t(ordinal)*100;
    provider_control::Frame c={}; c.opcode=provider_control::Offer; c.request=++call.control_id; c.grant.owner.bytes[0]=ordinal+1;
    check(control_exchange(c),"production task offered grant"); call.grant=c.grant;
    c.reply=false; c.status=0; c.opcode=provider_control::Initialize; c.request=++call.control_id; c.required=provider_session::Required;
    check(control_exchange(c),"production task initialized");
    const uint64_t initialized=now_ms(); call.next_renew=initialized+1000;
    provider_data::Frame f={}; f.opcode=provider_data::Open; f.request=1; f.ticket.grant=call.grant; f.ticket.session=1;
    uint32_t budget=mode==Timeout ? 2000 : (mode==Renewed || mode==Missing) ? 8000 : 4000;
    f.budget_ms=budget; f.port=mode==Untrusted ? 8445 : mode==Renewed ? 8448 : mode>=Timeout && mode<=DataOverflow ? 8447 : 8443;
    const char *host=mode==Hostname ? "wrong.lab.test" : "tls.lab.test";
    f.length=strlen(host)+1; memcpy(f.payload,host,f.length);
    const uint64_t begin=now_ms(); unsigned ticks=load(monitor_ticks);
    bool received=exchange(f,call,mode,false);
    bool reject=mode==Hostname || mode==Untrusted;
    check(received && f.status==(reject ? provider_data::Failed : provider_data::Ok),"authenticated open outcome");
    unsigned total=0; uint8_t bytes[256]={}; bool final_ok=false, terminal_failed=false, lost=false;
    if (!reject && received && f.status==provider_data::Ok) {
        static const char request[]="GET /probe HTTP/1.1\r\nHost: tls.lab.test\r\nConnection: close\r\n\r\n";
        f.reply=false; f.status=0; f.opcode=provider_data::Write; ++f.request; f.budget_ms=budget;
        f.count=f.length=sizeof(request)-1; memcpy(f.payload,request,f.length);
        received=exchange(f,call,mode,false);
        check(received && f.status==provider_data::Ok && f.count==sizeof(request)-1,"copied request delivered");
        for (unsigned n=0;n<64 && received && f.status==provider_data::Ok;++n) {
            f.reply=false; f.status=0; f.opcode=provider_data::Read; ++f.request; f.budget_ms=budget; f.count=256; f.length=0;
            received=exchange(f,call,mode,true);
            if (!received) { lost=true; break; }
            if (f.status!=provider_data::Ok) { terminal_failed=true; break; }
            if (!f.count) { final_ok=true; break; }
            if (total+f.count>sizeof(bytes)) { check(false,"bounded response"); break; }
            memcpy(bytes+total,f.payload,f.count); total+=f.count;
        }
    }
    uint64_t ended=now_ms(); unsigned elapsed=ended-begin;
    bool good=mode==Valid || mode==Renewed;
    if (good) {
        static const char header[]="HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\n";
        const unsigned char body[]={0,255,128,13,10,'A',0,10};
        check(final_ok && total==sizeof(header)-1+8 && !memcmp(bytes,header,sizeof(header)-1) && !memcmp(bytes+sizeof(header)-1,LAB_ECDSA ? body : reinterpret_cast<const unsigned char*>("lwip-ok\n"),8),"exact response and authenticated EOF");
    } else if (!reject) check(!final_ok && total==0 && ((mode>=ControlOverflow) ? lost : terminal_failed),"failure publishes no body");
    if (mode==Renewed) check(elapsed>=6000 && elapsed<=8250 && call.renew_ok>=6,"long call survives production lease with renewals");
    if (mode==Timeout) check(elapsed>=2000 && elapsed<=2250,"original deadline survives queue and renewals");
    if (mode==Missing) check(ended-initialized>=5000 && ended-initialized<=5250 && call.renew_sent==0,"production lease expires without renewal");
    if (mode==Close || mode==Restart) check(call.signal_ms && ended-call.signal_ms<=250,"control cancellation return bound");
    if (mode>=ControlOverflow) {
        provider_control::Frame probe={}; probe.opcode=provider_control::Renew;
        probe.request=++call.control_id; probe.grant=call.grant; probe.counter=call.renew_sequence+1;
        // Silence alone could mean the stalled read is still running. Prove
        // that the owner processed the fault before requesting explicit close.
        bool accepted=control_exchange(probe);
        check(!accepted && probe.reply && probe.status==provider_control::Rejected,"delivery fault retired grant before explicit close");
    }
    // Drain the deferred close/restart acknowledgement after the failed data reply.
    if (mode==Close || mode==Restart) {
        Packet p; uint64_t deadline=now_ms()+250;
        while (!call.terminal_ack && now_ms()<deadline) {
            if (xQueueReceive(output,&p,pdMS_TO_TICKS(10))!=pdTRUE) continue;
            provider_control::Frame response;
            call.terminal_ack=provider_control::decode(p.bytes,p.size,response) && response.status==provider_control::Ok && response.opcode==(mode==Close ? provider_control::Close : provider_control::Offer);
        }
        check(call.terminal_ack,"control acknowledged after TLS unwind");
    } else {
        c={}; c.opcode=provider_control::Close; c.request=++call.control_id; c.grant=call.grant; c.counter=1;
        check(control_exchange(c),"owner cleanup acknowledged");
    }
    check(load(fault)==0,"injected UART fault consumed");
    check(call.renew_ok==call.renew_sent && call.max_ack_ms<=250,"renewals acknowledged within bound");
    check(load(monitor_ticks)>ticks && load(monitor_gap)<=250,"independent task remains scheduled");
    unsigned stack=owner_task ? uxTaskGetStackHighWaterMark(owner_task) : 0;
    check(stack>=1024,"production owner stack reserve");
    check(heap_caps_check_integrity_all(true),"heap integrity after owner cleanup");
    printf("TASK_RESULT ordinal=%u name=%s elapsed_ms=%u lease_ms=%u renewals=%u ack_ms=%u signal_ms=%u stack=%u ticks=%u gap_ms=%u free=%u run=%s\n",ordinal,name,elapsed,unsigned(ended-initialized),call.renew_ok,call.max_ack_ms,unsigned(call.signal_ms ? ended-call.signal_ms : 0),stack,unsigned(load(monitor_ticks)-ticks),unsigned(load(monitor_gap)),unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT)),LAB_RUN_ID);
}
}
extern "C" const uint8_t *provider_boot_identity(void) {
    static const uint8_t fixture_identity[16]={7,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    return fixture_identity;
}
extern "C" BaseType_t my_uart_get_buffer(uint8_t uart,command_buf_t **out,TickType_t ticks) {
    TaskHandle_t caller=xTaskGetCurrentTaskHandle();
    if (!owner_task) owner_task=caller;
    if (caller!=owner_task || strcmp(pcTaskGetName(caller),"Provider contro") || uart!=1 || ticks || borrowed) uart_error();
    if (load(fault)==1) { store(fault,0); return pdFALSE; }
    borrowed=true; *out=&buffer; return pdTRUE;
}
extern "C" BaseType_t my_uart_try_transmit_packet(uint8_t uart,command_buf_t *b) {
    if (uart!=1 || b!=&buffer || !borrowed || b->size<=0 || unsigned(b->size)>provider_data::FrameSize) { uart_error(); return pdFALSE; }
    borrowed=false;
    if (load(fault)==2) { store(fault,0); memset(b->data,0xa5,sizeof(b->data)); return pdFALSE; }
    Packet packet={}; packet.size=b->size; packet.ms=now_ms(); memcpy(packet.bytes,b->data,packet.size);
    BaseType_t result=xQueueSend(output,&packet,0);
    memset(b->data,0xa5,sizeof(b->data));
    if (result!=pdTRUE) uart_error();
    __atomic_fetch_add(&sent,1,__ATOMIC_RELAXED); return result;
}
extern "C" void app_main(void) {
    printf("TASK_RUNTIME run=%s profile=%s\n",LAB_RUN_ID,LAB_ECDSA ? "ecdsa" : "rsa");
    check(provider_runtime_network(),"private emulator network ready");
    timeval utc={}; utc.tv_sec=LAB_UTC_SECONDS; check(settimeofday(&utc,0)==0,"fixture UTC installed");
    output=xQueueCreate(8,sizeof(Packet)); configASSERT(output);
    check(provider_control_configure_tls(lab_root,sizeof(lab_root),policy_ready,policy_ready),"boot trust configured");
    provider_control_start();
    check(!provider_control_configure_tls(lab_root,sizeof(lab_root),policy_ready,policy_ready),"live trust replacement refused");
    check(xTaskCreate(monitor,"Schedule probe",2048,xTaskGetCurrentTaskHandle(),tskIDLE_PRIORITY+2,0)==pdPASS,"independent scheduling task started");
    unsigned ordinal=0;
    for (unsigned mode=0;mode<=SendLoss;++mode) { attempt(Mode(mode),++ordinal,false); attempt(Valid,++ordinal,true); }
    store(monitor_stop,1); check(ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(500))>0,"scheduling task joined");
    check(!borrowed && !load(uart_errors) && uxQueueMessagesWaiting(output)==0,"UART ownership and copied replies intact");
    // The monitor notifies before self-deletion; let both idle tasks reclaim it
    // before comparing persistent task counts across an idempotent start.
    vTaskDelay(pdMS_TO_TICKS(50));
    unsigned tasks=uxTaskGetNumberOfTasks(); provider_control_start(); vTaskDelay(pdMS_TO_TICKS(50));
    printf("TASK_COUNT before=%u after=%u run=%s\n",tasks,unsigned(uxTaskGetNumberOfTasks()),LAB_RUN_ID);
    check(uxTaskGetNumberOfTasks()==tasks,"repeated start creates no duplicate task");
    printf("TASK_SCHED ticks=%u gap_ms=%u uart_frames=%u errors=%u stack=%u run=%s\n",unsigned(load(monitor_ticks)),unsigned(load(monitor_gap)),unsigned(load(sent)),unsigned(load(uart_errors)),unsigned(uxTaskGetStackHighWaterMark(owner_task)),LAB_RUN_ID);
    printf("SUMMARY checks=%u failures=%u run=%s\n",checks,failures,LAB_RUN_ID);
    // Production owner, trust and UART fixture queue intentionally have firmware lifetime.
}
