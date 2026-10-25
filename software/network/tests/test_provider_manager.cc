#include "provider_manager.h"
#include "provider_control_test_port.h"
#include <stdio.h>
using namespace provider_control;
using namespace provider_session;
static int checks, failures;
static void expect(bool value, const char *label) { ++checks; if (!value) { ++failures; printf("FAIL %s\n", label); } }
static Identity id(unsigned byte) { Identity result = {}; result.bytes[0] = byte; return result; }
struct Entropy { unsigned value, calls; bool qualified; Entropy(unsigned n) : value(n), calls(0), qualified(true) {} };
static bool fresh(void *p, Identity& result) {
    Entropy& source = *static_cast<Entropy *>(p); ++source.calls;
    result = id(source.value); return source.qualified;
}
struct Link {
    ControlTestPort port;
    Worker worker;
    CancellationSource boot_events;
    Link() : worker(port, id(1), Required, {100, 200, 50}) {}
    Frame process(const uint8_t *request, uint64_t now) {
        expect(worker.submit(request, FrameSize), "request accepted by worker"); worker.step(now);
        expect(port.replies.size() == 1, "one worker reply");
        if (port.replies.empty()) return {};
        Frame frame = port.replies.front(); port.replies.pop_front(); return frame;
    }
};
static bool receive(Manager& manager, const Frame& frame, uint64_t now)
{
    uint8_t bytes[FrameSize]; encode(frame, bytes); return manager.receive(bytes, sizeof(bytes), now);
}
static Ticket initialize(Manager& manager, Link& link, uint64_t now)
{
    uint8_t bytes[FrameSize]; Ticket result = {};
    expect(manager.offer(id(1), link.boot_events.token(), now, bytes), "manager offer");
    expect(receive(manager, link.process(bytes, now), now), "offer accepted");
    expect(manager.initialize(now, bytes), "manager init");
    expect(receive(manager, link.process(bytes, now), now) && manager.ready(now), "initialized grant accepted");
    expect(manager.reserve(now, result), "ticket reserved"); return result;
}
struct Counts { int cancels = 0, releases = 0; bool done = true; };
struct ResourceTest : Resource {
    Counts& c; explicit ResourceTest(Counts& value) : c(value) {}
    void cancel() override { ++c.cancels; }
    bool cleanup() override { return c.done; }
    ~ResourceTest() { ++c.releases; }
};
static void qualification()
{
    uint8_t bytes[FrameSize];
    Manager missing({NULL,NULL}, {100,200}); expect(!missing.offer(id(1), {}, 0, bytes), "missing entropy disabled");
    for (unsigned value = 0; value < 2; ++value) {
        Entropy e = {value}; e.qualified = value == 0;
        Manager bad({&e,fresh}, {100,200});
        expect(bad.state() == Manager::Disabled && !bad.offer(id(1), {}, 0, bytes), "zero or unqualified entropy disabled");
        bad.disconnect(); expect(e.calls == 1 && bad.state() == Manager::Disabled, "no fallback or retry for identity");
    }
    Entropy e = {2}; Manager exhausted({&e,fresh}, {100,200}, UINT64_MAX);
    expect(!exhausted.offer(id(1), {}, 0, bytes) && exhausted.state() == Manager::Disabled, "request exhaustion terminal");
}
static void reply_validation()
{
    Entropy e = {2}; Manager manager({&e,fresh}, {100,200}); Link link; uint8_t bytes[FrameSize];
    manager.offer(id(1), link.boot_events.token(), 0, bytes);
    Frame offered = link.process(bytes, 0), bad = offered;
    bad.request++; expect(!receive(manager, bad, 1), "wrong request ignored");
    bad = offered; bad.opcode = Renew; expect(!receive(manager, bad, 1), "wrong opcode ignored");
    bad = offered; bad.counter = 1; expect(!receive(manager, bad, 1), "wrong counter ignored");
    bad = offered; bad.required = Required; expect(!receive(manager, bad, 1), "wrong features ignored");
    bad = offered; bad.grant.challenge.epoch = 0; expect(!receive(manager, bad, 1), "zero epoch ignored");
    for (unsigned i = 0; i < 16; ++i) {
        bad = offered; bad.grant.owner.bytes[i] ^= 1;
        expect(!receive(manager, bad, 1), "every manager identity byte checked");
        bad = offered; bad.grant.challenge.provider.bytes[i] ^= 1;
        expect(!receive(manager, bad, 1), "every observed boot byte checked");
    }
    encode(offered, bytes);
    for (size_t size = 0; size < FrameSize; ++size) expect(!manager.receive(bytes, size, 1), "truncated reply ignored");
    uint8_t oversized[FrameSize + 1] = {}; memcpy(oversized, bytes, FrameSize);
    expect(!manager.receive(oversized, sizeof(oversized), 1), "oversized reply ignored");
    bytes[8] = 1; expect(!manager.receive(bytes, FrameSize, 1), "old wire version rejected");
    expect(receive(manager, offered, 99) && manager.state() == Manager::Offered, "invalid replies do not consume pending match");
    expect(!receive(manager, offered, 99), "duplicate offer ignored");
    manager.initialize(99, bytes); Frame initialized = link.process(bytes, 99);
    bad = initialized; ++bad.grant.challenge.epoch;
    expect(!receive(manager, bad, 99), "init epoch mismatch rejected");
    expect(receive(manager, initialized, 100) && manager.ready(100), "matching init acknowledged");
    expect(e.calls == 1, "manager identity generated once");
}
static void restart_and_close()
{
    Link link; Entropy first = {2}, second = {3};
    Manager old({&first,fresh}, {100,200}), replacement({&second,fresh}, {100,200});
    uint8_t bytes[FrameSize];
    old.offer(id(1), link.boot_events.token(), 0, bytes); Frame old_offer = link.process(bytes, 0);
    receive(old, old_offer, 0); old.initialize(0, bytes); Frame old_init = link.process(bytes, 0);
    receive(old, old_init, 0);
    replacement.offer(id(1), link.boot_events.token(), 1, bytes);
    expect(!receive(replacement, old_offer, 1), "restart with reused request number rejects old offer");
    Frame new_offer = link.process(bytes, 1); receive(replacement, new_offer, 1);
    replacement.initialize(1, bytes);
    expect(!receive(replacement, old_init, 1), "restart rejects old init acknowledgement");
    Frame wrong_request; decode(bytes, FrameSize, wrong_request); wrong_request.grant.owner = id(9);
    uint8_t forged[FrameSize]; encode(wrong_request, forged);
    expect(link.process(forged, 1).status == Rejected, "provider binds init to offer owner");
    expect(receive(replacement, link.process(bytes, 1), 1) && replacement.ready(1), "new manager initialization succeeds");
    Ticket ticket = {}; replacement.reserve(1, ticket); Counts c;
    ResourceTest *r = new ResourceTest(c); Ticket admitted = {};
    bool accepted = link.worker.admit(ticket.grant, ticket.session, r, 1, admitted);
    expect(accepted, "worker accepts manager-issued ticket");
    if (!accepted) delete r; // Failed admission retains caller ownership.
    auto lifetime = replacement.lifetime();
    expect(replacement.close(ticket, 2, bytes) && lifetime.cancelled() && !replacement.current(ticket, 2), "close fences publication before transmit");
    Frame closed = link.process(bytes, 2);
    expect(c.releases == 1 && receive(replacement, closed, 2), "close accepted only after worker releases");
    expect(!receive(replacement, closed, 2), "close duplicate ignored");
    Ticket next = {}; expect(replacement.reserve(3, next) && next.session == 2, "fresh session after close");
    expect(!replacement.current(ticket, 3) && replacement.current(next, 3), "old ticket cannot publish after reuse");
    link.boot_events.signal();
    expect(!replacement.current(next, 4), "provider restart visible before manager poll");
    replacement.poll(4); expect(replacement.state() == Manager::Idle, "provider restart invalidates pending state");
    replacement.disconnect(); link.worker.disconnect(); link.worker.step(5);
}
static void timeout_and_replay()
{
    Link link; Entropy e = {2}; Manager manager({&e,fresh}, {100,200});
    Ticket ticket = initialize(manager, link, 0); uint8_t bytes[FrameSize];
    expect(manager.renew(50, bytes), "renew sent"); Frame renewed = link.process(bytes, 50);
    Frame wrong = renewed; wrong.counter = 2; expect(!receive(manager, wrong, 60), "renew counter mismatch rejected");
    expect(receive(manager, renewed, 149), "renew reply before deadline");
    expect(!receive(manager, renewed, 150), "replay cannot refresh lease");
    expect(manager.current(ticket, 249) && !manager.current(ticket, 250), "lease anchored to send not reply arrival");
    manager.poll(250); expect(manager.state() == Manager::Idle, "lease expires");
    manager.offer(id(1), link.boot_events.token(), 251, bytes); Frame offered = link.process(bytes, 251);
    expect(!receive(manager, offered, 351) && manager.state() == Manager::Idle, "exact reply deadline rejects late packet");
    manager.offer(id(1), link.boot_events.token(), 352, bytes);
    manager.disconnect(); expect(!receive(manager, link.process(bytes, 352), 352), "enqueue/link failure retires pending reply");
    manager.offer(id(1), link.boot_events.token(), 400, bytes); manager.poll(399);
    expect(manager.state() == Manager::Disabled, "backwards clock terminal");
    Entropy delayed_entropy = {4}; Manager delayed({&delayed_entropy,fresh}, {300,200}); Link delayed_link;
    delayed.offer(id(1), {}, 0, bytes); receive(delayed, delayed_link.process(bytes, 0), 0);
    delayed.initialize(0, bytes); Frame init = delayed_link.process(bytes, 0);
    expect(receive(delayed, init, 200) && !delayed.ready(200), "old initialization cannot admit expired grant");
    Entropy absent_entropy = {5}; Manager absent({&absent_entropy,fresh}, {100,200});
    ControlTestPort p; Worker disabled(p, id(1), 0, {100,200,50});
    absent.offer(id(1), {}, 0, bytes); disabled.submit(bytes, FrameSize); disabled.step(0);
    expect(receive(absent, p.replies.front(), 0) && absent.state() == Manager::Idle, "absent backend handled without admission");
}
static void pending_and_failed_operations()
{
    uint8_t bytes[FrameSize];
    for (unsigned status : {unsigned(Busy), unsigned(Rejected), unsigned(Unavailable)}) {
        Link link; Entropy e = {2}; Manager manager({&e,fresh}, {100,200});
        Ticket ticket = initialize(manager, link, 0);
        auto lifetime = manager.lifetime();
        expect(manager.renew(50, bytes), "renew before rejection");
        Frame rejected = link.process(bytes, 50); rejected.status = status;
        expect(receive(manager, rejected, 51) && !manager.current(ticket, 51) && lifetime.cancelled(), "correlated error retires grant");
        Ticket sentinel = {}; sentinel.session = 99;
        expect(!manager.reserve(51, sentinel) && sentinel.session == 99, "failed admission preserves output");
    }
    Link link; Entropy e = {2}; Manager manager({&e,fresh}, {100,200});
    Ticket ticket = initialize(manager, link, 0);
    expect(manager.renew(150, bytes), "renew near lease expiry");
    Frame reply = link.process(bytes, 150);
    expect(manager.current(ticket, 199) && !manager.current(ticket, 200), "pending renewal does not extend original lease");
    expect(!receive(manager, reply, 200), "late renewal cannot revive expired grant");

    ticket = initialize(manager, link, 201);
    Counts counts; Ticket admitted = {};
    ResourceTest *resource = new ResourceTest(counts);
    bool accepted = link.worker.admit(ticket.grant, ticket.session, resource, 201, admitted);
    expect(accepted, "worker admits resource before delayed close");
    if (!accepted) delete resource;
    expect(manager.close(ticket, 202, bytes), "close awaiting cleanup reply");
    Frame closed = link.process(bytes, 202);
    expect(closed.status == Ok && counts.releases == 1, "provider confirms cleanup before delayed delivery");
    Frame wrong = closed; ++wrong.counter;
    expect(!receive(manager, wrong, 203), "wrong cleanup session ignored");
    Ticket next = {}; expect(!manager.reserve(203, next), "no reuse before cleanup acknowledgement");
    expect(!receive(manager, closed, 302) && !manager.reserve(302, next), "late cleanup cannot authorize reuse");

    manager.offer(id(1), link.boot_events.token(), 303, bytes);
    Frame offered = link.process(bytes, 303);
    link.boot_events.signal();
    expect(!receive(manager, offered, 304), "retired provider token rejects otherwise matching reply");
}
int main()
{
    qualification(); reply_validation(); restart_and_close(); timeout_and_replay(); pending_and_failed_operations();
    printf("Provider manager: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
