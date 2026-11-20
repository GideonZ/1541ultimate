#include "session_contract.h"
#include <stdio.h>
#include <algorithm>
#include <array>
#include <type_traits>
using namespace session_contract;

static int checks, failures, scenarios;
static void expect(bool ok, const char *label)
{
    ++checks;
    if (!ok) { ++failures; printf("FAIL %s\n", label); }
}
static void scenario(const char *name) { ++scenarios; printf("SCENARIO %s\n", name); }
static Pair ready(Channel& channel, uint64_t provider = 20)
{
    Pair p = channel.probe(); p.provider = provider;
    expect(channel.accept(p, 1, Required, true), "compatible capability reply");
    return p;
}
static void negotiation()
{
    scenario("capability-pairings");
    for (unsigned version : {0u, 1u, 2u}) for (unsigned mask = 0; mask < 64; ++mask) for (bool init : {false, true}) {
        Channel channel(10);
        Pair p = channel.probe(); p.provider = 20;
        bool allowed = version == 1 && (mask & Required) == Required && init;
        expect(channel.accept(p, version, mask, init) == allowed, "version/features/initialization gate");
        expect((channel.reserve().session != 0) == allowed, "HTTPS admission matches negotiation");
        expect(!channel.accept(p, 1, Required, true), "reply consumed once even after failure");
    }
    scenario("probe-replay-and-retry");
    Channel channel(10);
    Pair old = channel.probe(); old.provider = 20;
    Pair current = channel.probe(); current.provider = 21;
    expect(!channel.accept(old, 1, Required, true), "retired probe rejected");
    Pair wrong = current; ++wrong.controller;
    expect(!channel.accept(wrong, 1, Required, true), "wrong controller rejected");
    expect(channel.accept(current, 1, Required, true), "current probe accepted");
    expect(!channel.accept(old, 1, Required, true) && channel.current(current), "old provider cannot roll back ready channel");
    channel.invalidate();
    expect(!channel.reserve().session, "link loss disables admission");
    expect(!channel.accept(current, 1, Required, true), "invalidated reply rejected");
    ready(channel, 22);
    expect(channel.reserve().session != 0, "new negotiation recovers");
    scenario("missing-identity");
    Channel no_identity(0);
    expect(!no_identity.probe().controller && !no_identity.reserve().session, "unavailable controller identity fails closed");
    Channel no_provider(10);
    expect(!no_provider.accept(no_provider.probe(), 1, Required, true), "zero provider identity rejected");
}
static void operation_ownership()
{
    scenario("owner-and-operation-identity");
    Channel channel(10); ready(channel);
    Lease lease(channel, 1, 100);
    Ticket open = lease.ticket();
    expect(!lease.opened(2, open, true, 1), "foreign owner cannot open");
    expect(lease.opened(1, open, true, 1), "owner authenticated open");
    expect(!lease.opened(1, open, true, 2), "duplicate open rejected");
    expect(lease.begin(1, 2), "first operation starts");
    Ticket op = lease.ticket();
    expect(!lease.begin(1, 3), "only one operation in flight");
    for (int field = 0; field < 5; ++field) {
        Ticket bad = op;
        if (field == 0) ++bad.pair.controller;
        if (field == 1) ++bad.pair.provider;
        if (field == 2) ++bad.pair.probe;
        if (field == 3) ++bad.session;
        if (field == 4) ++bad.operation;
        expect(!lease.completed(1, bad, true, 4), "foreign or future completion rejected");
        expect(lease.state() == State::Pending && lease.closes() == 0, "invalid completion has no side effects");
    }
    expect(!lease.completed(2, op, true, 4), "foreign owner cannot publish");
    expect(lease.completed(1, op, true, 4), "matching completion publishes");
    expect(!lease.completed(1, op, true, 4), "duplicate cannot publish twice");
    expect(lease.begin(1, 5), "next operation starts");
    expect(!lease.completed(1, op, true, 5), "previous operation rejected while next pending");
    expect(lease.completed(1, lease.ticket(), true, 5), "next operation publishes");
    expect(lease.finish(1, 6), "exchange commits");
    expect(!lease.cancel(1, op) && !lease.poll(1, 200, true), "late cancellation cannot retract committed exchange");
    lease.close(1); lease.close(1);
    expect(lease.closes() == 1 && lease.cause() == Cause::Complete, "terminal cause and release stable");
}
static void restarts()
{
    scenario("endpoint-restarts-in-every-phase");
    for (int phase = 0; phase < 3; ++phase) for (bool controller_restart : {false, true}) {
        Channel channel(10); ready(channel);
        Lease old(channel, 1, 100);
        if (phase > 0) expect(old.opened(1, old.ticket(), true, 1), "open before restart");
        if (phase > 1) expect(old.begin(1, 2), "pending before restart");
        Ticket stale = old.ticket();
        channel.invalidate();
        expect(!old.poll(1, 3), "restart fences lease");
        expect(old.cause() == Cause::Restart && old.closes() == 1, "restart owner releases once");
        Channel replacement(11);
        Channel& active = controller_restart ? replacement : channel;
        ready(active, controller_restart ? 20 : 21);
        Lease fresh(active, 1, 200);
        expect(!fresh.opened(1, stale, true, 4), "old open cannot authenticate fresh lease");
        expect(fresh.opened(1, fresh.ticket(), true, 4) && fresh.begin(1, 5), "fresh exchange recovers");
        expect(!fresh.completed(1, stale, true, 5), "old data cannot publish into fresh lease");
        expect(!fresh.cancel(1, stale), "old cancel cannot stop fresh lease");
        expect(fresh.completed(1, fresh.ticket(), true, 6) && fresh.finish(1, 6), "fresh result commits");
        old.close(1);
        expect(old.closes() == 1 && old.cause() == Cause::Restart, "late old cleanup harmless");
    }
    scenario("restart-before-owner-polls");
    Channel channel(10); ready(channel);
    Lease lease(channel, 1, 100);
    expect(lease.opened(1, lease.ticket(), true, 1) && lease.begin(1, 2), "pending operation");
    Ticket old = lease.ticket();
    ready(channel, 21);
    expect(!lease.completed(1, old, true, 3) && lease.cause() == Cause::Restart, "completion itself checks epoch");
}
static void cancellation()
{
    scenario("cancellation-isolation-and-first-cause");
    for (int phase = 0; phase < 3; ++phase) {
        Channel channel(10); ready(channel);
        Lease a(channel, 1, 100), b(channel, 2, 100);
        if (phase > 0) expect(a.opened(1, a.ticket(), true, 1), "open before cancel");
        if (phase > 1) expect(a.begin(1, 2), "pending before cancel");
        Ticket old = a.ticket();
        expect(!a.cancel(2, old) && !a.cancel(1, b.ticket()), "cancel must identify owner and lease");
        expect(a.cancel(1, old), "owner cancels own lease");
        expect(!a.opened(1, old, true, 3) && !a.completed(1, old, true, 3), "cancelled completion cannot publish");
        a.close(1); a.poll(1, 1000);
        expect(a.cause() == Cause::Cancelled && a.closes() == 1, "cancel survives cleanup and expiry");
        expect(b.opened(2, b.ticket(), true, 3) && b.begin(2, 4), "neighbor remains usable");
        expect(b.completed(2, b.ticket(), true, 5) && b.finish(2, 5), "neighbor completes");
    }
    scenario("ordered-completion-cancel-restart");
    std::array<int, 3> events = {{0, 1, 2}};
    do {
        Channel channel(10); ready(channel);
        Lease lease(channel, 1, 100);
        expect(lease.opened(1, lease.ticket(), true, 1) && lease.begin(1, 2), "interleaving setup");
        Ticket ticket = lease.ticket();
        int published = 0;
        for (int event : events) {
            if (event == 0) published += lease.completed(1, ticket, true, 3);
            if (event == 1) lease.cancel(1, ticket);
            if (event == 2) { channel.invalidate(); lease.poll(1, 3); }
        }
        expect(published == (events[0] == 0 ? 1 : 0), "only completion before stop may publish");
        expect(lease.closes() == 1, "interleaving releases once");
        expect(!lease.completed(1, ticket, true, 3), "replayed result never publishes");
        printf("ORDER %d%d%d published=%d\n", events[0], events[1], events[2], published);
    } while (std::next_permutation(events.begin(), events.end()));
}
static void deadlines_and_limits()
{
    scenario("deadline-and-cancellation-precedence");
    for (bool cancel : {false, true}) for (uint64_t now : {99u, 100u, 101u}) {
        Channel channel(10); ready(channel);
        Lease lease(channel, 1, 100);
        expect(lease.opened(1, lease.ticket(), true, 1) && lease.begin(1, 2), "deadline setup");
        bool ok = lease.completed(1, lease.ticket(), true, now, cancel);
        expect(ok == (!cancel && now < 100), "commit observes original absolute deadline");
        expect(lease.cause() == (cancel ? Cause::Cancelled : now >= 100 ? Cause::Timeout : Cause::None), "cancel wins simultaneous expiry");
        if (ok) expect(!lease.begin(1, 100) && lease.cause() == Cause::Timeout, "progress cannot extend deadline");
    }
    scenario("initialization-failure-and-terminal-close");
    Channel channel(10); ready(channel);
    Lease failed(channel, 1, 100);
    expect(!failed.opened(1, failed.ticket(), false, 1), "failed open terminates");
    failed.close(1);
    expect(failed.cause() == Cause::Failed && failed.closes() == 1, "failure survives cleanup");
    Lease closed(channel, 1, 100); Ticket old = closed.ticket();
    closed.close(2);
    expect(closed.closes() == 0, "wrong task cannot release");
    closed.close(1); closed.close(1);
    expect(!closed.opened(1, old, true, 1) && closed.closes() == 1, "close-before-open is terminal");
    Lease io_failed(channel, 1, 100);
    expect(io_failed.opened(1, io_failed.ticket(), true, 1) && io_failed.begin(1, 2), "I/O failure setup");
    expect(!io_failed.completed(1, io_failed.ticket(), false, 3), "failed operation cannot publish");
    expect(!io_failed.begin(1, 4) && io_failed.cause() == Cause::Failed, "failed write cannot be replayed");
    io_failed.close(1);
    expect(io_failed.closes() == 1, "failed I/O released once");
    Lease opening_cancel(channel, 1, 100);
    expect(!opening_cancel.opened(1, opening_cancel.ticket(), true, 100, true), "cancel before open commit");
    expect(opening_cancel.cause() == Cause::Cancelled, "opening cancellation beats expiry");
    scenario("counter-exhaustion");
    Channel small(10, 2); ready(small);
    Lease one(small, 1, 100, 2), two(small, 2, 100), exhausted(small, 3, 100);
    expect(exhausted.cause() == Cause::Unavailable && exhausted.closes() == 0, "session IDs do not wrap");
    expect(one.opened(1, one.ticket(), true, 1) && one.begin(1, 2), "last permitted operation starts");
    expect(one.completed(1, one.ticket(), true, 3), "last permitted operation finishes");
    expect(!one.begin(1, 4) && one.cause() == Cause::Exhausted, "operation IDs do not wrap");
    ready(small, 21);
    expect(!small.probe().controller && !small.reserve().session, "probe IDs do not wrap");
}
int main()
{
    static_assert(!std::is_copy_constructible<Lease>::value, "lease has one owner");
    static_assert(!std::is_copy_constructible<Channel>::value, "channel issuer cannot be copied");
    negotiation(); operation_ownership(); restarts(); cancellation(); deadlines_and_limits();
    printf("Session contract: %d scenarios, %d checks, %d failures\n", scenarios, checks, failures);
    return failures ? 1 : 0;
}
