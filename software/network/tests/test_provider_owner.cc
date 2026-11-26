#include "provider_session_owner.h"
#include <stdio.h>
using namespace provider_session;
static int checks, failures;
static void expect(bool value, const char *label)
{
    ++checks;
    if (!value) { ++failures; printf("FAIL %s\n", label); }
}
static Identity identity(unsigned byte) { Identity id = {}; id.bytes[0] = byte; return id; }
static const Limits limits = {100, 200, 50}; // Host fixture bounds, not product defaults.
struct Counts { int cancels, polls, releases; bool done; Counts() : cancels(0), polls(0), releases(0), done(false) {} };
struct Session : Resource {
    Counts& count;
    explicit Session(Counts& c) : count(c) {}
    void cancel() override { ++count.cancels; }
    bool cleanup() override { ++count.polls; return count.done; }
    ~Session() { ++count.releases; }
};
static Grant initialize(Owner& owner, unsigned manager, uint64_t now)
{
    Challenge challenge = owner.offer(now);
    Grant grant = {};
    expect(challenge.epoch != 0, "fresh initialization offer");
    expect(owner.initialize(challenge, identity(manager), 1, Required, now, grant), "initialized after drain");
    return grant;
}
static Ticket admit(Owner& owner, const Grant& grant, Counts& count, uint64_t number, uint64_t now)
{
    Ticket ticket = {};
    Session *session = new Session(count);
    bool accepted = owner.admit(grant, number, session, now, ticket);
    expect(accepted, "session admission");
    if (!accepted) delete session;
    return ticket;
}
static void drain(Owner& owner, Counts& count, uint64_t now)
{
    owner.stop(now);
    count.done = true;
    owner.poll(now);
    expect(!owner.occupied() && count.cancels == 1 && count.releases == 1, "owner releases once after confirmation");
    owner.poll(now);
    expect(count.releases == 1, "duplicate poll never releases twice");
}
static void initialization_cases()
{
    Owner owner(identity(1), Required, limits);
    Grant output = {};
    Challenge first = owner.offer(0), second = owner.offer(1);
    expect(!owner.initialize(first, identity(2), 1, Required, 1, output), "old challenge rejected");
    Challenge wrong = second; wrong.provider = identity(3);
    expect(!owner.initialize(wrong, identity(2), 1, Required, 1, output), "foreign provider rejected");
    for (unsigned bit = 1; bit <= SessionIdentity; bit <<= 1) {
        expect(!owner.initialize(second, identity(2), 1, Required & ~bit, 1, output), "missing required feature rejected");
    }
    expect(!owner.initialize(second, identity(2), 1, Required | 128, 1, output), "unsupported required feature rejected");
    expect(!owner.initialize(second, identity(2), 2, Required, 1, output), "unknown version rejected");
    expect(!owner.initialize(second, identity(0), 1, Required, 1, output), "zero owner rejected");
    expect(output.challenge.epoch == 0, "failed initialization exposes no grant");
    expect(owner.initialize(second, identity(2), 1, Required, 100, output), "just before initialization deadline");
    expect(!owner.initialize(second, identity(2), 1, Required, 100, output), "duplicate initialization rejected");
    owner.stop(100);
    Challenge expired = owner.offer(100);
    expect(!owner.initialize(expired, identity(2), 1, Required, 200, output), "exact initialization deadline fails");
    expect(owner.failure() == Owner::InitializationExpired || owner.failure() == Owner::Disabled, "causal failure retained");
    for (unsigned bit = 1; bit <= SessionIdentity; bit <<= 1) {
        Owner disabled(identity(1), Required & ~bit, limits);
        expect(!disabled.offer(0).epoch && disabled.state() == Owner::Quarantined, "unqualified backend cannot offer");
    }
    Owner zero(identity(0), Required, limits);
    expect(!zero.offer(0).epoch, "zero boot fails closed");
    Owner invalid(identity(1), Required, {0, 200, 50});
    expect(!invalid.offer(0).epoch, "zero limits fail closed");
    Owner exhausted(identity(1), Required, limits, UINT64_MAX - 1);
    expect(exhausted.offer(0).epoch == UINT64_MAX && !exhausted.offer(1).epoch, "challenge never wraps");
}
static void restart_cases()
{
    Owner owner(identity(1), Required, limits);
    Grant old = initialize(owner, 2, 0);
    Counts first;
    Ticket old_ticket = admit(owner, old, first, 1, 0);
    expect(owner.current(old_ticket, 0), "initial session usable");
    Challenge takeover = owner.offer(10);
    Grant fresh = {};
    expect(!owner.current(old_ticket, 10), "restart fences old callback immediately");
    expect(first.cancels == 1 && first.releases == 0, "restart signals without deleting live resource");
    expect(!owner.initialize(takeover, identity(3), 1, Required, 10, fresh), "no initialization acknowledgement before cleanup");
    expect(!owner.renew(old, 1, 11), "old owner cannot renew");
    expect(!owner.close(old_ticket, 11), "old owner cannot close replacement generation");
    owner.poll(11);
    expect(first.polls == 1 && first.releases == 0, "pending cleanup retains ownership");
    first.done = true;
    owner.poll(12);
    expect(first.cancels == 1 && first.releases == 1, "cleanup confirmation releases exactly once");
    expect(owner.initialize(takeover, identity(3), 1, Required, 12, fresh), "new manager acknowledged after cleanup");
    Counts second;
    Ticket current = admit(owner, fresh, second, 1, 12);
    expect(!owner.current(old_ticket, 12) && owner.current(current, 12), "session number reuse requires new grant");
    expect(!owner.close(old_ticket, 13) && second.cancels == 0, "stale close cannot affect new resource");
    Ticket wrong = current; wrong.grant.owner = identity(4);
    expect(!owner.current(wrong, 13) && !owner.close(wrong, 13), "foreign owner fenced");
    for (unsigned i = 0; i < 16; ++i) {
        wrong = current; wrong.grant.challenge.provider.bytes[i] ^= 1;
        expect(!owner.current(wrong, 13) && !owner.close(wrong, 13), "every provider identity byte checked");
        wrong = current; wrong.grant.owner.bytes[i] ^= 1;
        expect(!owner.current(wrong, 13) && !owner.close(wrong, 13), "every manager identity byte checked");
    }
    drain(owner, second, 14);
    Owner rebooted(identity(9), Required, limits);
    Grant reboot = initialize(rebooted, 3, 0);
    Counts third; Ticket reboot_ticket = admit(rebooted, reboot, third, 1, 0);
    expect(!rebooted.current(current, 0) && rebooted.current(reboot_ticket, 0), "ESP32 boot fences old grant even with same counter");
    drain(rebooted, third, 1);
}
static void lease_and_cleanup_cases()
{
    Owner owner(identity(1), Required, limits);
    Grant grant = initialize(owner, 2, 0);
    Counts count; Ticket ticket = admit(owner, grant, count, 1, 0);
    expect(!owner.renew(grant, 2, 50), "out of order renewal rejected");
    expect(owner.renew(grant, 1, 50), "current renewal accepted");
    expect(!owner.renew(grant, 1, 100), "replayed renewal does not extend lease");
    expect(owner.current(ticket, 249) && !owner.current(ticket, 250), "callback sees owner loss without poll");
    expect(!owner.renew(grant, 2, 250), "expired owner cannot revive lease");
    expect(count.cancels == 1 && owner.failure() == Owner::LeaseExpired, "expiry signals owner cleanup");
    owner.poll(299);
    expect(owner.state() != Owner::Quarantined && count.releases == 0, "pending cleanup before bound");
    count.done = true;
    owner.poll(300);
    expect(owner.state() == Owner::Quarantined && count.releases == 1, "deadline checked before late cleanup confirmation");
    expect(owner.failure() == Owner::LeaseExpired && !owner.offer(301).epoch, "first cause retained and quarantine cannot reopen");
    owner.poll(301);
    expect(count.releases == 1, "late completion never double frees");

    Owner changing(identity(1), Required, limits);
    Grant original = initialize(changing, 2, 0);
    Counts stalled; admit(changing, original, stalled, 1, 0);
    for (unsigned now = 10; now < 60; now += 10) expect(changing.offer(now).epoch != 0, "replacement offer before cleanup deadline");
    expect(!changing.offer(60).epoch && changing.state() == Owner::Quarantined, "offers cannot extend cleanup budget");
    stalled.done = true; changing.poll(60);
    expect(stalled.cancels == 1 && stalled.releases == 1, "repeated takeover signals once");

    Owner late(identity(1), Required, limits);
    Grant late_grant = initialize(late, 2, 0);
    Counts overdue; admit(late, late_grant, overdue, 1, 0);
    overdue.done = true; late.poll(251);
    expect(late.state() == Owner::Quarantined && overdue.releases == 1,
           "late polling cannot shift original lease cleanup deadline");
}
static void admission_and_clock_cases()
{
    Owner owner(identity(1), Required, limits);
    Grant grant = initialize(owner, 2, 100);
    Counts count; Ticket ticket = admit(owner, grant, count, 1, 100);
    Counts rejected; Session *extra = new Session(rejected); Ticket unchanged = {};
    expect(!owner.admit(grant, 2, extra, 100, unchanged) && rejected.releases == 0, "capacity failure leaves resource with caller");
    expect(unchanged.session == 0, "rejected admission exposes no ticket");
    delete extra;
    expect(owner.close(ticket, 101) && owner.close(ticket, 101) && count.cancels == 1, "close idempotent");
    count.done = true; owner.poll(102);
    Counts next; extra = new Session(next);
    expect(!owner.admit(grant, 1, extra, 102, unchanged), "old session cannot be reused");
    expect(!owner.admit(grant, 3, extra, 102, unchanged), "out of order session cannot be admitted");
    expect(owner.admit(grant, 2, extra, 102, unchanged), "next session admitted only after release");
    expect(!owner.current(ticket, 102), "late completion for old slot is rejected");
    expect(!owner.current(unchanged, 99), "clock rollback fails in callback");
    owner.poll(99);
    expect(owner.state() == Owner::Quarantined && owner.failure() == Owner::ClockRegression, "clock regression quarantines");
    next.done = true; owner.poll(103);
    expect(next.cancels == 1 && next.releases == 1, "clock failure drained by owner");
}
int main()
{
    initialization_cases(); restart_cases(); lease_and_cleanup_cases(); admission_and_clock_cases();
    printf("Provider owner: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
