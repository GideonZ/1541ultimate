#include "dma_uart.h"
#include "provider_manager_uart.h"
#include "provider_manager_runner.h"
#include "provider_control_test_port.h"
#include <stdio.h>
using namespace provider_control;
using namespace provider_session;
unsigned manager_critical_depth;
static unsigned checks, failures;
static void expect(bool b, const char *s) { ++checks; if (!b) { ++failures; printf("FAIL %s\n", s); } }
static Identity id(unsigned n) { Identity value = {}; value.bytes[0] = n; return value; }
static bool fresh(void *p, Identity& value) { value = id(*static_cast<unsigned*>(p)); return value.valid(); }
struct Counts { unsigned cancels = 0, releases = 0; bool done = true; };
struct TestResource : Resource {
    Counts& counts;
    explicit TestResource(Counts& c) : counts(c) {}
    void cancel() override { ++counts.cancels; }
    bool cleanup() override { return counts.done; }
    ~TestResource() { ++counts.releases; }
};
struct Fixture {
    unsigned identity = 2;
    DmaUART uart;
    ProviderManagerUart link;
    ManagerRunner runner;
    ControlTestPort port;
    Worker worker;
    CancellationSource boot;
    Fixture(unsigned renewal = 80, ManagerLimits limits = {100,200}, unsigned features = Required) :
        link({&identity,fresh},limits), runner(link.delivery,renewal), worker(port,id(1),features,{100,200,50}) {
        expect(link.bind_uart(&uart), "qualified UART bound");
    }
    void remote(uint64_t now) {
        expect(uart.sent.size() == 1, "one outgoing control request");
        if (uart.sent.empty()) return;
        auto bytes = uart.sent.front(); uart.sent.erase(uart.sent.begin());
        expect(worker.submit(bytes.data(),bytes.size()), "worker receives wire request");
        worker.step(now);
    }
    Frame reply() {
        expect(port.replies.size() == 1, "one remote reply");
        if (port.replies.empty()) return {};
        Frame frame = port.replies.front(); port.replies.pop_front(); return frame;
    }
    void ingress(Frame frame) {
        uint8_t bytes[FrameSize]; encode(frame,bytes);
        portENTER_CRITICAL(); expect(link.receive_isr(bytes,sizeof(bytes)), "reply copied at ISR ingress"); portEXIT_CRITICAL();
        memset(bytes,0xff,sizeof(bytes));
    }
    void initialize(uint64_t now = 0) {
        expect(runner.start(id(1),boot.token(),now), "explicit start sends offer");
        remote(now); ingress(reply()); runner.step(now);
        remote(now); ingress(reply()); runner.step(now);
        expect(runner.state() == ManagerRunner::Active && uart.sent.empty(), "initialized without extra send");
    }
    Ticket admit(Counts& counts, uint64_t now = 0) {
        Ticket ticket = {}, remote_ticket = {};
        expect(runner.reserve(now,ticket), "owner reserves session");
        TestResource *resource = new TestResource(counts);
        bool accepted = worker.admit(ticket.grant,ticket.session,resource,now,remote_ticket);
        expect(accepted, "worker admits actual owned resource");
        if (!accepted) delete resource;
        return ticket;
    }
};
static void renewal_and_cleanup()
{
    Counts counts; Fixture f; f.initialize(); Ticket ticket = f.admit(counts);
    expect(!f.runner.start(id(1),f.boot.token(),0), "cannot restart active driver implicitly");
    f.runner.step(79); expect(f.uart.sent.empty(), "no early renewal");
    f.runner.step(80); f.remote(80); Frame renewed = f.reply();
    f.runner.step(90); expect(f.uart.sent.empty(), "pending renewal is not repeated");
    Ticket sentinel = {}; sentinel.session = 99;
    expect(!f.runner.reserve(90,sentinel) && sentinel.session == 99, "pending work preserves failed admission output");
    f.ingress(renewed); f.runner.step(159);
    expect(f.uart.sent.empty() && f.runner.current(ticket,159), "delayed renewal keeps same active session");
    f.runner.step(160); expect(f.uart.sent.size() == 1, "next renewal uses send time, not arrival");
    f.remote(160); f.ingress(f.reply()); f.runner.step(160);
    counts.done = false;
    expect(f.runner.close(ticket,161) && !f.runner.current(ticket,161), "close retires publication immediately");
    f.remote(161);
    expect(f.port.replies.empty() && f.worker.occupied(), "cleanup acknowledgement waits for resource release");
    f.runner.step(162); expect(!f.runner.reserve(162,sentinel), "no reuse during cleanup");
    counts.done = true; f.worker.step(163); f.ingress(f.reply()); f.runner.step(163);
    expect(counts.cancels && counts.releases == 1 && !f.worker.occupied(), "remote resource released exactly once");
    expect(f.runner.reserve(164,sentinel) && sentinel.session == ticket.session + 1, "reuse only after cleanup reply");
    expect(!f.runner.current(ticket,164) && f.runner.current(sentinel,164), "old ticket remains retired");
    f.runner.stop(); expect(!f.runner.current(sentinel,164), "stop fences current ticket");
}
static void loss_stop_and_restart()
{
    for (unsigned mode = 0; mode < 4; ++mode) {
        Counts counts; Fixture f; f.initialize(); Ticket ticket = f.admit(counts);
        f.runner.step(80); f.remote(80); Frame old = f.reply();
        if (mode == 0) { portENTER_CRITICAL(); f.link.delivery.invalidate_isr(); portEXIT_CRITICAL(); }
        if (mode == 1) f.boot.signal();
        if (mode == 2) f.runner.stop();
        if (mode == 3) for (unsigned i = 0; i <= QueueDepth; ++i) f.ingress(old);
        expect(!f.runner.current(ticket,81), "loss or stop fences before owner step");
        f.ingress(old); f.runner.step(81); f.runner.step(82);
        expect(f.uart.sent.empty() && !f.runner.reserve(82,ticket), "failure never retries or reuses stale reply");
        expect(f.runner.state() == (mode == 2 ? ManagerRunner::Stopped : ManagerRunner::Failed), "explicit stopped or failed state");
        // No successful close was delivered: the remote must clean up by lease.
        f.worker.step(280);
        expect(counts.releases == 1 && !f.worker.occupied(), "remote cleanup on lease expiry");
        expect(f.runner.start(id(1),f.boot.token(),281), "explicit restart with fresh snapshot");
        f.remote(281); Frame offered = f.reply(); f.ingress(old); f.ingress(offered);
        f.runner.step(281); expect(f.uart.sent.empty(), "one stale reply per bounded step");
        f.runner.step(281); f.remote(281); f.ingress(f.reply()); f.runner.step(281);
        expect(f.runner.reserve(281,ticket), "explicit renegotiation recovers after discarded replay");
        f.runner.stop();
    }
}
static void deadlines_and_failed_sends()
{
    for (unsigned stage = 0; stage < 4; ++stage) {
        Fixture f; Ticket ticket = {};
        if (stage == 0) expect(f.runner.start(id(1),f.boot.token(),0), "offer before deadline");
        else if (stage == 1) {
            expect(f.runner.start(id(1),f.boot.token(),0), "start before initialization timeout");
            f.remote(0); f.ingress(f.reply()); f.runner.step(0);
        } else {
            f.initialize(); expect(f.runner.reserve(0,ticket), "reserve before deadline");
            if (stage == 2) f.runner.step(80);
            else expect(f.runner.close(ticket,80), "close before lost acknowledgement");
        }
        uint64_t sent = stage >= 2 ? 80 : 0;
        f.remote(sent); Frame late = f.reply();
        f.runner.step(sent + 99); f.ingress(late); f.runner.step(sent + 100);
        expect(f.runner.state() == ManagerRunner::Failed && !f.runner.reserve(sent + 100,ticket), "exact deadline rejects queued late reply");
        f.runner.step(sent + 101); expect(f.uart.sent.empty(), "deadline has no automatic retry");
    }
    for (unsigned stage = 0; stage < 4; ++stage) {
        Fixture f; Ticket ticket = {};
        if (stage == 0) {
            f.uart.available = false; expect(!f.runner.start(id(1),f.boot.token(),0), "offer buffer unavailable");
        } else if (stage == 1) {
            expect(f.runner.start(id(1),f.boot.token(),0), "offer before failed init enqueue");
            f.remote(0); f.ingress(f.reply()); f.uart.transmit_ok = false; f.runner.step(0);
        } else {
            f.initialize(); expect(f.runner.reserve(0,ticket), "reserve before failed send"); f.uart.transmit_ok = false;
            if (stage == 2) f.runner.step(80);
            else expect(!f.runner.close(ticket,1), "close enqueue fails");
        }
        expect(f.runner.state() == ManagerRunner::Failed && f.uart.sent.empty(), "failed transmit retires driver");
        expect(f.uart.consumed == (stage == 0 ? 0u : stage == 1 ? 2u : 3u), "each operation makes at most one transmit attempt");
        f.uart.available = f.uart.transmit_ok = true; f.runner.step(150);
        expect(f.uart.sent.empty() && !f.runner.current(ticket,150), "restored transport does not silently retry");
    }
}
static void qualification_and_clock()
{
    for (unsigned renewal : {0u,100u,200u,UINT32_MAX}) {
        Fixture f(renewal); expect(!f.runner.start(id(1),f.boot.token(),0), "invalid renewal margin refused");
        expect(f.runner.state() == ManagerRunner::Disabled && f.uart.gets == 0, "invalid limits never touch UART");
        f.runner.stop(); Ticket ticket = {}; f.runner.close(ticket,0);
        expect(f.runner.state() == ManagerRunner::Disabled, "disabled configuration stays disabled");
    }
    Fixture absent(80,{100,200},0);
    expect(absent.runner.start(id(1),absent.boot.token(),0), "probe backend capabilities");
    absent.remote(0); absent.ingress(absent.reply()); absent.runner.step(0);
    expect(absent.runner.state() == ManagerRunner::Failed && absent.uart.sent.empty(), "absent backend fails without initialization");
    DmaUART uart; ProviderManagerUart unqualified({NULL,NULL},{100,200}); ManagerRunner disabled(unqualified.delivery,80);
    expect(!disabled.start(id(1),{},0) && disabled.state() == ManagerRunner::Disabled && uart.gets == 0, "missing manager identity has no fallback");
    Fixture invalid;
    expect(!invalid.runner.start(id(0),{},0) && invalid.uart.gets == 0, "invalid provider identity rejected");
    auto cancelled = invalid.boot.token(); invalid.boot.signal();
    expect(!invalid.runner.start(id(1),cancelled,0) && invalid.uart.gets == 0, "retired boot snapshot rejected");
    Fixture clock; clock.initialize(100); clock.runner.step(99);
    expect(clock.runner.state() == ManagerRunner::Disabled && !clock.runner.start(id(1),clock.boot.token(),101), "backwards clock disables permanently");
    Fixture lease; lease.initialize(); lease.runner.step(200);
    expect(lease.runner.state() == ManagerRunner::Failed && lease.uart.sent.empty(), "missed lease expires before renewal send");
    Fixture boundary(99); boundary.initialize(); boundary.runner.step(99); boundary.remote(99);
    boundary.ingress(boundary.reply()); boundary.runner.step(198);
    expect(boundary.runner.state() == ManagerRunner::Active && boundary.uart.sent.size() == 1, "small valid margin accepts reply and schedules next renewal");
    boundary.runner.stop();
}
static void cancellation_during_renewal()
{
    Counts counts; Fixture f; f.initialize(); Ticket ticket = f.admit(counts);
    f.runner.step(80); f.remote(80); Frame renewed = f.reply();
    expect(!f.runner.close(ticket,81) && f.runner.state() == ManagerRunner::Failed, "cancel while renewal pending retires whole grant");
    f.ingress(renewed); f.runner.step(82);
    expect(!f.runner.current(ticket,82) && f.uart.sent.empty(), "pending reply cannot revive cancelled session");
    f.worker.step(280); expect(counts.releases == 1, "cancelled remote resource expires without reuse");
}
int main()
{
    renewal_and_cleanup(); loss_stop_and_restart(); deadlines_and_failed_sends();
    qualification_and_clock(); cancellation_during_renewal();
    expect(manager_critical_depth == 0, "all critical sections balanced");
    printf("Provider runner: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
