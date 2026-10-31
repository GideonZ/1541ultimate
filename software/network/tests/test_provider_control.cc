#include "provider_control_test_port.h"
#include <stdio.h>
#include <thread>
using namespace provider_control;
using provider_session::Grant;
using provider_session::Ticket;
static int checks, failures;
static void expect(bool value, const char *label) { ++checks; if (!value) { ++failures; printf("FAIL %s\n", label); } }
static provider_session::Identity id(unsigned n) { provider_session::Identity result = {}; result.bytes[0] = n; return result; }
static Frame request(unsigned opcode, uint64_t serial, Grant grant = {}, uint64_t counter = 0)
{
    Frame frame = {}; frame.opcode = opcode; frame.request = serial; frame.grant = grant; frame.counter = counter;
    if (opcode == Offer && !frame.grant.owner.valid()) frame.grant.owner = id(2);
    if (opcode == Initialize) frame.required = provider_session::Required;
    return frame;
}
struct Counts { int cancel = 0, released = 0; bool done = false; Worker *interrupt = NULL; };
struct Session : provider_session::Resource {
    Counts& c;
    explicit Session(Counts& count) : c(count) {}
    void cancel() override { ++c.cancel; }
    bool cleanup() override {
        if (c.interrupt) { Worker *w = c.interrupt; c.interrupt = NULL; Frame offer = request(Offer, 900); offer.grant.owner = id(8); control_submit(*w, offer); }
        return c.done;
    }
    ~Session() { ++c.released; }
};
static Frame reply(ControlTestPort& port)
{
    expect(!port.replies.empty(), "reply present");
    if (port.replies.empty()) return {};
    Frame frame = port.replies.front(); port.replies.pop_front();
    expect(frame.reply, "reply direction explicit");
    return frame;
}
static Grant initialize(Worker& worker, ControlTestPort& port, uint64_t now, unsigned manager = 2)
{
    Frame probe = request(Offer, 1); probe.grant.owner = id(manager);
    expect(control_submit(worker, probe), "offer queued"); worker.step(now);
    Frame offer = reply(port); expect(offer.status == Ok && offer.request == 1, "solicited challenge");
    offer.grant.owner = id(manager);
    expect(control_submit(worker, request(Initialize, 2, offer.grant)), "initialize queued"); worker.step(now);
    Frame initialized = reply(port); expect(initialized.status == Ok, "initialization acknowledged");
    return initialized.grant;
}
static Ticket admit(Worker& worker, Grant grant, Counts& counts, uint64_t session, uint64_t now)
{
    Ticket ticket = {}; Session *resource = new Session(counts);
    bool accepted = worker.admit(grant, session, resource, now, ticket);
    expect(accepted, "resource admitted"); if (!accepted) delete resource;
    return ticket;
}
static void wire_cases()
{
    Frame frame = request(Offer, 0x0123456789abcdefULL), decoded = {};
    uint8_t bytes[FrameSize + 5]; encode(frame, bytes);
    expect(bytes[12] == 0xef && bytes[19] == 1 && bytes[1] == 0xfe, "explicit little endian envelope");
    for (size_t size = 0; size < sizeof(bytes); ++size)
        expect(decode(bytes, size, decoded) == (size == FrameSize), "exact frame length required");
    const unsigned offsets[] = {0,1,2,3,4,5,6,7,8,9,10,11,72,73,74,75};
    for (unsigned offset : offsets) {
        uint8_t saved = bytes[offset]; bytes[offset] = 0xff;
        expect(!decode(bytes, FrameSize, decoded), "malformed framing rejected"); bytes[offset] = saved;
    }
    for (unsigned offset = 20; offset < 72; ++offset) {
        if (offset >= 44 && offset < 60) continue;
        bytes[offset] = 1; expect(!decode(bytes, FrameSize, decoded), "offer cannot carry stale fields"); bytes[offset] = 0;
    }
    memset(bytes + 12, 0, 8); expect(!decode(bytes, FrameSize, decoded), "zero request rejected");
    frame.opcode = Initialize; frame.grant = {{id(1), 9}, id(2)}; frame.required = provider_session::Required;
    encode(frame, bytes); expect(decode(bytes, FrameSize, decoded), "init round trip");
    frame.counter = 1; encode(frame, bytes); expect(!decode(bytes, FrameSize, decoded), "init counter must be zero");
    frame.opcode = Renew; frame.required = 0; encode(frame, bytes);
    expect(decode(bytes, FrameSize, decoded) && decoded.grant.challenge.epoch == 9, "renew round trip");
    frame.reply = true; frame.status = Ok; encode(frame, bytes);
    ControlTestPort port; Worker worker(port, id(1), provider_session::Required, {100,200,50});
    expect(decode(bytes, FrameSize, decoded) && !worker.submit(bytes, FrameSize), "reply cannot be submitted as request");
    expect(port.queue.empty(), "invalid ingress has no queue side effects");
}
static void lifecycle_cases()
{
    ControlTestPort port; Worker worker(port, id(1), provider_session::Required, {100,200,50});
    Grant grant = initialize(worker, port, 0); Counts first;
    Ticket ticket = admit(worker, grant, first, 1, 0);
    Frame close = request(Close, 3, grant, 1);
    Frame wrong = close; wrong.grant.owner = id(3);
    expect(control_submit(worker, wrong) && worker.current(ticket, 1), "foreign close does not signal cancellation");
    worker.step(1); expect(reply(port).status == Rejected, "foreign close rejected by owner");
    std::thread ingress([&] { control_submit(worker, close); }); ingress.join();
    expect(!worker.current(ticket, 2) && first.cancel == 0 && first.released == 0,
           "ingress cancels callback without executing resource code");
    worker.step(2); expect(first.cancel == 1 && first.released == 0 && port.replies.empty(), "close acknowledgement waits for cleanup");
    first.done = true; worker.step(3);
    Frame closed = reply(port); expect(closed.request == 3 && closed.status == Ok && first.released == 1, "confirmed close acknowledged once");
    Counts next; Ticket replacement = admit(worker, grant, next, 2, 4);
    expect(control_submit(worker, close) && worker.current(replacement, 4), "stale close cannot cancel reused slot");
    worker.step(4); expect(reply(port).status == Rejected, "stale close rejected");
    Frame restart = request(Offer, 4); restart.grant.owner = id(3);
    expect(control_submit(worker, restart), "restart queued");
    expect(!worker.current(replacement, 5) && next.released == 0, "new offer fences old callback at ingress");
    worker.step(5); Frame offer = reply(port); offer.grant.owner = id(3);
    control_submit(worker, request(Initialize, 5, offer.grant)); worker.step(6);
    expect(reply(port).status == Busy && next.cancel == 1 && next.released == 0, "new owner waits for old cleanup");
    next.done = true; worker.step(7);
    control_submit(worker, request(Initialize, 6, offer.grant)); worker.step(7);
    expect(reply(port).status == Ok && next.released == 1, "new owner initialized after cleanup");
    worker.disconnect(); worker.step(8);
}
static void delivery_failure_cases()
{
    ControlTestPort port; Worker worker(port, id(1), provider_session::Required, {100,200,50});
    Grant grant = initialize(worker, port, 0); Counts c; Ticket ticket = admit(worker, grant, c, 1, 0);
    for (unsigned i = 1; i <= QueueDepth; ++i) expect(control_submit(worker, request(Renew, i + 10, grant, i)), "bounded queue fill");
    expect(!control_submit(worker, request(Renew, 99, grant, 5)), "queue overflow rejected");
    expect(!worker.current(ticket, 1) && c.cancel == 0, "overflow cancellation survives lost notification");
    c.done = true;
    for (unsigned i = 0; i < QueueDepth + 1; ++i) worker.step(1);
    expect(port.replies.empty() && c.cancel == 1 && c.released == 1, "queued old renewals cannot revive overflowed owner");
    grant = initialize(worker, port, 2); Counts d; ticket = admit(worker, grant, d, 1, 2);
    port.transmit = false; control_submit(worker, request(Renew, 100, grant, 1)); worker.step(3);
    expect(!worker.current(ticket, 3) && port.replies.empty(), "reply delivery failure fences callback");
    d.done = true; worker.step(4); expect(d.released == 1, "failed reply drains owner");
    port.transmit = true; grant = initialize(worker, port, 5); Counts e; admit(worker, grant, e, 1, 5);
    control_submit(worker, request(Close, 101, grant, 1)); worker.step(6);
    e.done = true; e.interrupt = &worker; worker.step(7);
    // A new offer during cleanup retires the pending close acknowledgement.
    Frame only = reply(port); expect(only.opcode == Offer && only.request == 900 && port.replies.empty(), "retired pending acknowledgement not published");
    only.grant.owner = id(8);
    control_submit(worker, request(Initialize, 901, only.grant)); worker.step(8);
    expect(reply(port).status == Ok, "cleanup-time offer is not invalidated on next iteration");
    worker.disconnect(); worker.step(8);
}
static void timeout_and_absent_backend()
{
    ControlTestPort port; Worker worker(port, id(1), provider_session::Required, {100,200,50});
    Grant grant = initialize(worker, port, 0); Counts c; admit(worker, grant, c, 1, 0);
    control_submit(worker, request(Close, 3, grant, 1)); worker.step(1); worker.step(51);
    expect(reply(port).status == Unavailable && c.released == 0, "missed cleanup never acknowledges success");
    c.done = true; worker.step(52); expect(c.released == 1, "late cleanup frees resource");
    control_submit(worker, request(Offer, 4)); worker.step(53);
    expect(reply(port).status == Unavailable, "quarantine cannot be cleared by UART offer");
    ControlTestPort absent; Worker disabled(absent, id(1), 0, {100,200,50});
    control_submit(disabled, request(Offer, 1)); disabled.step(0);
    expect(reply(absent).status == Unavailable, "firmware without backend cannot promise initialization");
}
static void long_call_servicing()
{
    ControlTestPort port; Worker worker(port, id(1), provider_session::Required, {100,200,50});
    Grant grant = initialize(worker, port, 0); Counts c;
    Ticket ticket = admit(worker, grant, c, 1, 0);
    for (unsigned i=1;i<=20;++i) {
        expect(control_submit(worker,request(Renew,10+i,grant,i)),"long-call renewal queued");
        expect(worker.service(ticket,i*150),"live call renewed beyond original lease");
        expect(reply(port).status==Ok,"in-call renewal acknowledged");
        expect(c.cancel==0 && c.released==0,"servicing does not invoke resource callbacks");
    }
    expect(control_submit(worker,request(Renew,40,grant,20)),"replayed renewal queued");
    expect(worker.service(ticket,3100) && reply(port).status==Rejected,"replay cannot extend lease");
    Grant wrong=grant; wrong.owner=id(9);
    control_submit(worker,request(Renew,41,wrong,21));
    expect(worker.service(ticket,3150) && reply(port).status==Rejected,"foreign grant cannot renew live call");
    control_submit(worker,request(Renew,42,grant,21));
    expect(!worker.service(ticket,3200) && c.cancel==0 && c.released==0,"queued renewal cannot revive expired lease or tear down TLS");
    c.done=true; worker.step(3200);
    expect(c.cancel==1 && c.released==1 && reply(port).status==Rejected,"expired call cleaned up after unwind");
}
static void in_call_control_fences()
{
    for (unsigned mode=0;mode<5;++mode) {
        ControlTestPort port; Worker worker(port,id(1),provider_session::Required,{100,200,50});
        Grant grant=initialize(worker,port,0); Counts c; Ticket ticket=admit(worker,grant,c,1,0);
        if (mode==0) control_submit(worker,request(Close,3,grant,1));
        if (mode==1) control_submit(worker,request(Offer,3));
        if (mode==2) worker.disconnect();
        if (mode==3) {
            for (unsigned i=1;i<=QueueDepth;++i) control_submit(worker,request(Renew,10+i,grant,i));
            expect(!control_submit(worker,request(Renew,99,grant,5)),"in-call overflow rejected");
        }
        if (mode==4) {
            port.transmit=false; control_submit(worker,request(Renew,3,grant,1));
        }
        expect(!worker.service(ticket,1),"control loss or close fences in-flight TLS");
        expect(c.cancel==0 && c.released==0,"fence defers all resource callbacks");
        c.done=true; worker.step(2);
        expect(c.cancel==1 && c.released==1,"fenced resource released once after return");
    }
    ControlTestPort port; Worker worker(port,id(1),provider_session::Required,{100,200,50});
    Grant grant=initialize(worker,port,0); Counts c; Ticket ticket=admit(worker,grant,c,1,0);
    Grant foreign=grant; foreign.owner=id(9);
    control_submit(worker,request(Close,3,foreign,1));
    control_submit(worker,request(Initialize,4,grant));
    control_submit(worker,request(Renew,5,grant,1));
    expect(worker.service(ticket,100) && port.queue.size()==2 && reply(port).status==Rejected,"foreign close consumes one bounded event");
    expect(worker.service(ticket,101) && port.queue.size()==1 && reply(port).status==Busy,"initialize cannot replace live resource");
    expect(worker.service(ticket,102) && port.queue.empty() && reply(port).status==Ok,"renewal behind rejected controls is serviced");
    expect(!worker.service(ticket,101) && c.cancel==0,"regressed clock rejects callback without resource calls");
    c.done=true; worker.step(101);
    expect(c.released==1,"regression quarantines and cleans only after unwind");
}
int main()
{
    wire_cases(); lifecycle_cases(); delivery_failure_cases(); timeout_and_absent_backend();
    long_call_servicing(); in_call_control_fences();
    printf("Provider control: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
