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
int main()
{
    startup_gates(); owner_lifecycle(); identity_and_deadlines(); owner_data_queue();
    expect(manager_critical_depth == 0,"all critical sections released");
    printf("Provider task: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
