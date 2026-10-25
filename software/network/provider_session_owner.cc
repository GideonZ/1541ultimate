#include "provider_session_owner.h"
#include <assert.h>

namespace provider_session {
bool Identity::valid() const
{
    unsigned value = 0;
    for (unsigned i = 0; i < sizeof(bytes); ++i) value |= bytes[i];
    return value != 0;
}

Owner::Owner(const Identity& boot, unsigned features, Limits limits, uint64_t initial_epoch) :
    boot_(boot), features_(features), limits_(limits), grant_{}, epoch_(initial_epoch),
    session_(0), renew_sequence_(0), offered_at_(0), renewed_at_(0), cleanup_at_(0), last_now_(0),
    resource_(0), state_(Unavailable), failure_(None), draining_(false)
{
    if (!boot.valid() || !limits.initialization_ms || !limits.lease_ms || !limits.cleanup_ms ||
        (features & Required) != Required) {
        state_ = Quarantined;
        failure_ = Disabled;
    }
}

Owner::~Owner() { assert(!resource_); }

bool Owner::matches(const Grant& grant) const
{
    return grant.challenge.provider == boot_ && grant.challenge.epoch == epoch_ && grant.owner == grant_.owner;
}

void Owner::drain(uint64_t now)
{
    if (!resource_ || draining_) return;
    draining_ = true;
    cleanup_at_ = now;
    resource_->cancel();
}

void Owner::invalidate(Failure failure, uint64_t now, bool quarantine)
{
    if (state_ != Quarantined) state_ = quarantine ? Quarantined : Unavailable;
    if (failure_ == None) failure_ = failure;
    drain(now);
}

bool Owner::advance(uint64_t now)
{
    if (now < last_now_) {
        invalidate(ClockRegression, last_now_, true);
        return false;
    }
    last_now_ = now;
    if (state_ == Ready && now - renewed_at_ >= limits_.lease_ms)
        invalidate(LeaseExpired, renewed_at_ + limits_.lease_ms, false);
    if (state_ == Offered && now - offered_at_ >= limits_.initialization_ms)
        invalidate(InitializationExpired, offered_at_ + limits_.initialization_ms, false);
    if (draining_ && now - cleanup_at_ >= limits_.cleanup_ms)
        invalidate(CleanupExpired, now, true);
    return state_ != Quarantined;
}

Challenge Owner::offer(uint64_t now)
{
    if (!advance(now)) return {};
    if (epoch_ == UINT64_MAX) { invalidate(Exhausted, now, true); return {}; }
    drain(now); // Repeated offers cannot restart the cleanup deadline.
    ++epoch_;
    state_ = Offered;
    offered_at_ = now;
    return {boot_, epoch_};
}

bool Owner::initialize(const Challenge& challenge, const Identity& owner, unsigned version,
                       unsigned required, uint64_t now, Grant& result)
{
    if (!advance(now) || state_ != Offered || resource_ || !owner.valid() || version != 1 ||
        (required & Required) != Required || (features_ & required) != required ||
        !(challenge.provider == boot_) || challenge.epoch != epoch_) return false;
    grant_ = {challenge, owner};
    state_ = Ready;
    failure_ = None;
    renewed_at_ = now;
    session_ = renew_sequence_ = 0;
    result = grant_;
    return true;
}

bool Owner::renew(const Grant& grant, uint64_t sequence, uint64_t now)
{
    if (!advance(now) || state_ != Ready || !matches(grant) || renew_sequence_ == UINT64_MAX ||
        sequence != renew_sequence_ + 1) return false;
    renew_sequence_ = sequence;
    renewed_at_ = now;
    return true;
}

bool Owner::admit(const Grant& grant, uint64_t session, Resource *resource, uint64_t now, Ticket& result)
{
    if (!advance(now) || state_ != Ready || !matches(grant) || resource_ || !resource ||
        session_ == UINT64_MAX || session != session_ + 1) return false;
    session_ = session;
    resource_ = resource;
    draining_ = false;
    result = {grant_, session_};
    return true;
}

bool Owner::current(const Ticket& ticket, uint64_t now) const
{
    // Safe to call from the owner's TLS cancellation callback, including while
    // poll() cannot run. A lease cannot be extended by work or partial progress.
    return state_ == Ready && resource_ && !draining_ && matches(ticket.grant) &&
        ticket.session == session_ && now >= last_now_ && now - renewed_at_ < limits_.lease_ms;
}

bool Owner::close(const Ticket& ticket, uint64_t now)
{
    if (!advance(now) || state_ != Ready || !matches(ticket.grant) || ticket.session != session_ || !resource_)
        return false;
    drain(now);
    return true;
}

void Owner::stop(uint64_t now)
{
    advance(now);
    invalidate(Disabled, last_now_, false);
}

void Owner::poll(uint64_t now)
{
    advance(now);
    if (draining_ && resource_->cleanup()) {
        Resource *released = resource_;
        resource_ = 0;
        draining_ = false;
        delete released;
    }
    // A late cleanup can release memory, but cannot undo quarantine.
}
}
