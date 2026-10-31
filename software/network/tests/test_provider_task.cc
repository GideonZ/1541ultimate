#include "provider_manager_task.h"
#include "wifi.h"
#include "task.h"
#include "provider_control_test_port.h"
#include <functional>
#include <stdio.h>
using namespace provider_control;
using namespace provider_session;
unsigned manager_critical_depth;
static unsigned checks, failures;
static void expect(bool b, const char *s) { ++checks; if (!b) { ++failures; printf("FAIL %s\n",s); } }
struct Finished {};
struct Waiting {};
static TaskFunction_t task_function;
static void *task_context;
static unsigned creates, deletes, notifications, delays;
static uint32_t ticks;
static bool create_ok;
static std::function<void()> creation_hook, delay_hook;
static void reset_scheduler() {
    creates = deletes = notifications = delays = ticks = 0; create_ok = true;
    task_function = 0; task_context = 0; creation_hook = delay_hook = []{};
}
BaseType_t xTaskCreate(TaskFunction_t f, const char*, unsigned stack, void *context, UBaseType_t priority, TaskHandle_t *task)
{
    ++creates; expect(stack == 1600 && priority == PRIO_DRIVER, "explicit task stack and priority");
    if (!create_ok) return pdFALSE;
    task_function = f; task_context = context; *task = context; creation_hook(); return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { ++deletes; if (!task) throw Finished(); }
void xTaskNotifyGive(TaskHandle_t task) { expect(task == task_context, "startup notification targets owner"); ++notifications; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait)
{
    expect(clear == pdTRUE && wait == portMAX_DELAY, "startup blocks without polling");
    if (!notifications) throw Waiting();
    notifications = 0; return 1;
}
TickType_t xTaskGetTickCount() { return ticks; }
void vTaskDelay(TickType_t interval)
{
    expect(interval == 2 && manager_critical_depth == 0, "bounded 10 ms poll outside critical section");
    if (++delays > 200) { expect(false,"owner loop bounded by test"); throw Finished(); }
    ticks += interval; delay_hook();
}
struct Source { unsigned value = 2, calls = 0; bool qualified = true; };
static bool fresh(void *p, Identity& value) {
    Source& s = *static_cast<Source*>(p); ++s.calls; value = {}; value.bytes[0] = s.value; return s.qualified;
}
static Identity id(unsigned n) { Identity value = {}; value.bytes[0] = n; return value; }
struct Link {
    ControlTestPort port;
    Worker worker;
    std::vector<Frame> sent;
    Link() : worker(port,id(1),Required,{100,200,50}) {}
    void receive(WiFi& wifi, const Frame& frame) {
        uint8_t bytes[FrameSize]; encode(frame,bytes);
        portENTER_CRITICAL(); expect(wifi.bound->receive_isr(bytes,sizeof(bytes)), "owner reply ingress"); portEXIT_CRITICAL();
    }
    void exchange(WiFi& wifi, uint64_t now, bool deliver = true) {
        expect(wifi.uart.sent.size() <= 1,"one outgoing request per owner iteration");
        if (wifi.uart.sent.empty()) { worker.step(now); return; }
        auto bytes = wifi.uart.sent.front(); wifi.uart.sent.erase(wifi.uart.sent.begin());
        Frame frame = {}; expect(decode(bytes.data(),bytes.size(),frame),"valid request frame"); sent.push_back(frame);
        expect(worker.submit(bytes.data(),bytes.size()),"worker accepts owner frame"); worker.step(now);
        expect(port.replies.size() == 1,"worker supplies one reply");
        if (!port.replies.empty()) { Frame reply = port.replies.front(); port.replies.pop_front(); if (deliver) receive(wifi,reply); }
    }
};
static void run_owner() {
    try { task_function(task_context); expect(false,"task never returns"); }
    catch (const Finished&) {}
}
static void startup_gates()
{
    for (unsigned mode = 0; mode < 5; ++mode) {
        reset_scheduler(); Source source; WiFi wifi;
        source.value = mode == 0 ? 0 : 2; source.qualified = mode != 1;
        IncarnationSource input = mode == 2 ? IncarnationSource{0,0} : IncarnationSource{&source,fresh};
        ProviderManagerTask task(input,{100,200},mode == 3 ? 100 : 80);
        if (mode == 4) task.request_stop();
        expect(!task.start(wifi),"unqualified invalid or stopped startup refused");
        expect(creates == 0 && wifi.binds == 0 && wifi.uart.gets == 0,"failed startup has no task or UART effects");
        expect(!task.start(wifi),"startup not retried");
        expect(source.calls == (mode == 2 ? 0u : 1u),"identity sampled exactly once with no fallback");
    }
    for (unsigned mode = 0; mode < 2; ++mode) {
        reset_scheduler(); Source source; WiFi wifi; ProviderManagerTask task({&source,fresh},{100,200},80);
        create_ok = mode != 0; wifi.bind_ok = false;
        expect(!task.start(wifi) && task.state() == ProviderManagerTask::Failed,"task creation or binding failure reported");
        expect(wifi.uart.gets == 0 && notifications == 0,"no send or wake after failed startup");
        expect(deletes == mode && wifi.binds == mode,"created task cleaned when binding fails");
        expect(!task.start(wifi) && creates == 1,"no duplicate creation after failure");
    }
}
static void owner_lifecycle()
{
    for (unsigned mode = 0; mode < 6; ++mode) {
        reset_scheduler(); Source source; WiFi wifi; Link remote;
        ProviderManagerTask task({&source,fresh},{100,200},80);
        creation_hook = [&] {
            try { task_function(task_context); expect(false,"task must wait before binding"); }
            catch (const Waiting&) { expect(wifi.binds == 0 && wifi.snapshots == 0 && wifi.uart.gets == 0,"no owner work before start barrier"); }
            catch (const Finished&) { expect(false,"owner exited before binding barrier"); }
        };
        expect(task.start(wifi),"qualified task launched");
        expect(!task.start(wifi) && creates == 1,"single owner only");
        uint32_t origin = mode == 5 ? UINT32_MAX - 8 : 0; ticks = origin;
        bool active = false;
        delay_hook = [&] {
            uint64_t now = uint64_t(uint32_t(ticks - origin)) * 5;
            remote.exchange(wifi,now);
            if (task.state() == ProviderManagerTask::Active) active = true;
            if (delays == 15) {
                expect(active,"owner negotiated and renewed before interruption");
                if (mode == 0 || mode == 5) task.request_stop();
                if (mode == 1) wifi.lifetime.signal();
                if (mode == 2) { portENTER_CRITICAL(); wifi.bound->delivery.invalidate_isr(); portEXIT_CRITICAL(); }
                if (mode == 3) wifi.uart.transmit_ok = false;
                if (mode == 4) ticks -= 10;
            }
        };
        run_owner();
        expect(task.state() == ((mode == 0 || mode == 5) ? ProviderManagerTask::Stopped : ProviderManagerTask::Failed),"stop or failure terminal state");
        expect(deletes == 1 && delays <= 18,"owner exits without retry and frees its task");
        expect(source.calls == 1 && !task.start(wifi),"owner does not resample identity or recreate task");
        expect(remote.sent.size() >= 3 && remote.sent[0].opcode == Offer && remote.sent[1].opcode == Initialize && remote.sent[2].opcode == Renew,"negotiation and renewal use worker protocol");
        expect(wifi.uart.gets == wifi.uart.consumed,"all acquired transmit buffers consumed");
    }
}
static void identity_and_deadlines()
{
    for (unsigned mode = 0; mode < 5; ++mode) {
        reset_scheduler(); Source source; WiFi wifi; Link remote;
        wifi.online = mode != 0;
        if (mode == 1) wifi.boot = id(9); // Stale observed boot cannot match the actual worker.
        ProviderManagerTask task({&source,fresh},{100,200},80);
        expect(task.start(wifi),"start bounded bootstrap");
        delay_hook = [&] {
            uint64_t now = uint64_t(ticks) * 5;
            if (mode == 0 && now == 1500) wifi.online = true;
            if (mode == 2 && !wifi.uart.sent.empty()) {
                auto bytes = wifi.uart.sent.front(); wifi.uart.sent.clear();
                Frame old = {}; decode(bytes.data(),bytes.size(),old);
                old.reply = true; old.status = Ok; old.grant.owner = id(3); old.grant.challenge = {id(1),1};
                remote.receive(wifi,old); // Previous manager incarnation, same request number.
            } else if (mode == 3) remote.exchange(wifi,now,false);
            else remote.exchange(wifi,now);
            if (mode == 4 && task.state() == ProviderManagerTask::Active) task.request_stop();
        };
        run_owner();
        expect(task.state() == (mode == 4 ? ProviderManagerTask::Stopped : ProviderManagerTask::Failed),"fresh handshake alone permits active state");
        if (mode == 0) expect(delays == 150 && wifi.uart.gets == 0,"bootstrap exact deadline wins over arriving snapshot");
        if (mode > 0 && mode < 4) expect(delays == 10 && wifi.uart.gets == 1,"stale or missing offer expires without retry");
        expect(source.calls == 1,"source not resampled on stale identity or reply timeout");
    }
    reset_scheduler(); Source source; WiFi wifi; ProviderManagerTask task({&source,fresh},{100,200},80);
    creation_hook = [&] { task.request_stop(); };
    expect(task.start(wifi),"stop racing startup still completes safe binding"); run_owner();
    expect(task.state() == ProviderManagerTask::Stopped && wifi.snapshots == 0 && wifi.uart.gets == 0,"startup cancellation sends nothing");
    reset_scheduler(); Source racing_source; WiFi racing_wifi;
    ProviderManagerTask racing({&racing_source,fresh},{100,200},80);
    racing_wifi.snapshot_hook = [&] { racing.request_stop(); };
    expect(racing.start(racing_wifi),"start before snapshot cancellation"); run_owner();
    expect(racing.state() == ProviderManagerTask::Stopped && racing_wifi.uart.gets == 0,"cancellation during snapshot prevents offer transmission");
}
static void owner_data_queue()
{
    for (unsigned stale = 0; stale < 2; ++stale) {
        reset_scheduler(); Source source; WiFi wifi; Link remote;
        ProviderManagerTask task({&source,fresh},{100,200},80);
        provider_data::Frame f = {}; f.opcode = provider_data::Open; f.budget_ms = 100;
        f.port = 443; f.length = 5; memcpy(f.payload,"host",5);
        expect(!task.submit_data(f,{}),"inactive owner rejects data");
        bool submitted = false, delivered = false, collected = false;
        expect(task.start(wifi),"data owner starts");
        delay_hook = [&] {
            if (!wifi.uart.sent.empty() && wifi.uart.sent.front()[1] == 0xfd) {
                auto bytes = wifi.uart.sent.front(); wifi.uart.sent.erase(wifi.uart.sent.begin());
                provider_data::Frame reply;
                expect(provider_data::decode(bytes.data(),bytes.size(),reply),"owner emits data wire frame");
                expect(reply.payload[0] == 'h' && reply.request == 1 && reply.ticket.session,"owner copies and reserves session");
                delivered = true; reply.reply = true; reply.status = provider_data::Ok;
                reply.count = reply.length = reply.port = 0; reply.budget_ms = 0; reply.request += stale;
                provider_data::encode(reply,bytes.data());
                portENTER_CRITICAL(); wifi.bound->receive_isr(bytes.data(),bytes.size()); portEXIT_CRITICAL();
            } else remote.exchange(wifi,uint64_t(ticks)*5);
            if (!submitted && task.state() == ProviderManagerTask::Active) {
                submitted = task.submit_data(f,{}); expect(submitted,"data queued for owner");
                expect(!task.submit_data(f,{}),"one request only"); memset(f.payload,0xee,sizeof(f.payload));
            }
            provider_data::Frame result;
            if (task.collect_data(result)) { collected = true; expect(result.status == provider_data::Ok,"matching data reply"); task.request_stop(); }
        };
        run_owner();
        if (stale) { provider_data::Frame result; expect(task.collect_data(result) && result.status == provider_data::Failed,"stale reply fails queued caller"); }
        expect(submitted && delivered && (collected != bool(stale)),"owner data queue exercised");
        expect(task.state() == (stale ? ProviderManagerTask::Failed : ProviderManagerTask::Stopped),"data failure retires owner task");
        expect(!task.submit_data(f,{}),"terminated owner refuses new data");
    }
}
static void scheduling_boundaries()
{
    // The scheduler and ISR ingress are controlled independently: an on-time
    // reply cannot revive a task which processes it at or after its deadline.
    for (unsigned wrap=0;wrap<2;++wrap) for (unsigned age : {95u,100u,105u}) {
        reset_scheduler(); Source source; WiFi wifi; Link remote;
        ProviderManagerTask task({&source,fresh},{100,200},80);
        ticks=wrap ? UINT32_MAX-8 : 0; uint32_t origin=ticks;
        bool injected=false,active=false;
        expect(task.start(wifi),"schedule deadline task starts");
        delay_hook=[&] {
            if (!injected) {
                remote.exchange(wifi,10); // Reply arrives at 10 ms.
                ticks=origin+age/5; injected=true; // Owner wakes later.
            } else remote.exchange(wifi,uint64_t(uint32_t(ticks-origin))*5);
            if (task.state()==ProviderManagerTask::Active) { active=true; task.request_stop(); }
        };
        run_owner();
        expect(active==(age<100),"deadline tested at processing time across tick wrap");
        expect(task.state()==(age<100 ? ProviderManagerTask::Stopped : ProviderManagerTask::Failed),"late wake fails closed");
        expect(age<100 || (wifi.uart.gets==1 && delays==1),"expired offer never retries or initializes");
        expect(deletes==1 && manager_critical_depth==0,"delayed task exits with balanced locks");
        printf("SCHEDULE reply_age=%u wrap=%u active=%u sends=%u\n",age,wrap,unsigned(active),wifi.uart.gets);
    }
    for (unsigned mode=0;mode<4;++mode) {
        reset_scheduler(); Source source; WiFi wifi; Link remote;
        ProviderManagerTask task({&source,fresh},{100,200},80);
        bool injected=false; unsigned before=0;
        expect(task.start(wifi),"active scheduling task starts");
        delay_hook=[&] {
            remote.exchange(wifi,uint64_t(ticks)*5);
            if (!injected && task.state()==ProviderManagerTask::Active) {
                injected=true; before=wifi.uart.gets;
                if (mode==0) ticks+=40; // Full lease without an owner wake.
                if (mode==1) ticks+=UINT32_MAX/2+1; // Ambiguous clock gap.
                if (mode==2) task.request_stop();
                if (mode==3) wifi.lifetime.signal();
                provider_data::Frame f={}; f.opcode=provider_data::Open; f.budget_ms=100;
                f.port=443; f.length=5; memcpy(f.payload,"host",5);
                if (mode==2) expect(!task.submit_data(f,{}),"stop rejects submissions before owner wakes");
            }
        };
        run_owner();
        expect(injected && task.state()==(mode==2 ? ProviderManagerTask::Stopped : ProviderManagerTask::Failed),"starvation stop or restart retires owner");
        expect(wifi.uart.gets==before,"no catch-up renewal or data after retirement");
        expect(deletes==1 && !task.ready(),"retired task exits once");
        printf("SCHEDULE retirement=%u sends_after=%u state=%u\n",mode,wifi.uart.gets-before,unsigned(task.state()));
    }
}
struct ScheduledResource : Resource {
    unsigned& releases;
    explicit ScheduledResource(unsigned& n):releases(n) {}
    void cancel() override {}
    bool cleanup() override { return true; }
    ~ScheduledResource() { ++releases; }
};
static void pending_data_scheduling()
{
    for (unsigned mode=0;mode<8;++mode) {
        reset_scheduler(); Source source; WiFi wifi; Link remote;
        ProviderManagerTask task({&source,fresh},{100,200},80);
        CancellationSource command;
        ticks=mode==5 ? UINT32_MAX-8 : 0; const uint32_t origin=ticks;
        unsigned releases=0, renewals=0, close_at=0, queued_at=0, reply_at=0, completed_at=0;
        bool submitted=false, received=false, replied=false, collected=false, signalled=false;
        provider_data::Frame pending={},result={};
        Frame held={}; bool holding=false; unsigned release_at=0;
        expect(task.start(wifi),"pending-data scheduler starts");
        delay_hook=[&] {
            unsigned now=uint32_t(ticks-origin)*5;
            expect(wifi.uart.sent.size()<=2,"bounded control plus data work per wake");
            while (!wifi.uart.sent.empty()) {
                auto bytes=wifi.uart.sent.front(); wifi.uart.sent.erase(wifi.uart.sent.begin());
                if (bytes[1]==0xfd) {
                    expect(!received && provider_data::decode(bytes.data(),bytes.size(),pending),"one copied data operation reaches remote");
                    received=true;
                    expect(pending.budget_ms==(mode==2 ? 120u : 500u),"native queue preserves original budget");
                    Ticket ticket; auto resource=new ScheduledResource(releases);
                    bool admitted=remote.worker.admit(pending.ticket.grant,pending.ticket.session,resource,now,ticket);
                    expect(admitted,"remote admits pending session"); if (!admitted) delete resource;
                } else {
                    Frame frame={}; expect(decode(bytes.data(),bytes.size(),frame),"scheduled control frame decoded");
                    if (frame.opcode==Renew) ++renewals;
                    if (frame.opcode==Close) {
                        close_at=now;
                        expect(!holding && now>=release_at,"close waits for outstanding renewal acknowledgement");
                    }
                    expect(remote.worker.submit(bytes.data(),bytes.size()),"scheduled control accepted"); remote.worker.step(now);
                    expect(remote.port.replies.size()==1,"scheduled control answered once");
                    if (!remote.port.replies.empty()) {
                        Frame response=remote.port.replies.front(); remote.port.replies.pop_front();
                        if ((mode==1 || mode==2) && frame.opcode==Renew && renewals==1) {
                            held=response; holding=true; release_at=now+60;
                        } else remote.receive(wifi,response);
                    }
                }
            }
            if (holding && now>=release_at) { remote.receive(wifi,held); holding=false; }
            remote.worker.step(now);
            if (!submitted && task.state()==ProviderManagerTask::Active) {
                provider_data::Frame f={}; f.opcode=provider_data::Open; f.budget_ms=mode==2 ? 120 : 500;
                f.port=443; f.length=5; memcpy(f.payload,"host",5);
                submitted=task.submit_data(f,command.token()); queued_at=now;
                expect(submitted,"pending data queued");
            }
            if (submitted && !signalled && now-queued_at>=100 && (mode==1 || mode==3 || mode==4)) {
                signalled=true;
                if (mode==1) command.signal();
                if (mode==3) task.request_stop();
                if (mode==4) wifi.lifetime.signal();
            }
            if (received && !replied && now-queued_at>=250) {
                pending.reply=true; pending.status=provider_data::Ok; pending.count=pending.length=pending.port=0; pending.budget_ms=0;
                uint8_t bytes[provider_data::FrameSize]; expect(provider_data::encode(pending,bytes),"delayed data reply encoded");
                portENTER_CRITICAL(); wifi.bound->receive_isr(bytes,sizeof(bytes)); portEXIT_CRITICAL();
                replied=true; reply_at=now;
            }
            if (replied && now>reply_at && !signalled && (mode==6 || mode==7)) {
                signalled=true;
                if (mode==6) command.signal(); else task.request_stop();
            }
            if (task.collect_data(result)) {
                collected=true; completed_at=now; task.request_stop();
            }
        };
        run_owner();
        if (!collected) { collected=task.collect_data(result); completed_at=uint32_t(ticks-origin)*5; }
        bool success=mode==0 || mode==5;
        expect(submitted && received && collected,"pending caller always completes");
        expect(result.status==(success ? provider_data::Ok : provider_data::Failed) && !result.count && !result.length,"cancellation deadline and restart cannot expose stale success");
        if (success || mode>=6) expect(renewals>=3 && replied,"renewals continue while data is pending");
        if (mode==1 || mode==2) expect(close_at && close_at>=release_at && completed_at-queued_at<=240,"cancel or deadline drains renewal then closes within bound");
        if (mode==3 || mode==4) expect(completed_at-queued_at<=110 && !close_at,"stop or restart retires on next wake without extra send");
        expect(deletes==1 && !task.ready() && wifi.uart.gets==wifi.uart.consumed,"pending owner exits once with no UART buffer debt");
        remote.worker.disconnect(); remote.worker.step(uint64_t(uint32_t(ticks-origin))*5);
        expect(releases==1 && !remote.worker.occupied(),"remote fixture resource released once");
        printf("SCHEDULE_DATA mode=%u elapsed=%u renewals=%u close_at=%u reply_at=%u status=%u\n",mode,completed_at-queued_at,renewals,close_at,reply_at,unsigned(result.status));
    }
}
int main()
{
    startup_gates(); owner_lifecycle(); identity_and_deadlines(); owner_data_queue(); scheduling_boundaries(); pending_data_scheduling();
    expect(manager_critical_depth == 0,"all critical sections released");
    printf("Provider task: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
