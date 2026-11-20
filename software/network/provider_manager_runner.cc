#include "provider_manager_runner.h"

namespace provider_control {
ManagerRunner::ManagerRunner(ManagerDelivery& delivery, uint32_t renew_ms) :
    delivery_(delivery), renew_ms_(renew_ms), state_(Stopped) {}
void ManagerRunner::fail()
{
    delivery_.disconnect();
    if (state_ != Disabled) state_ = delivery_.enabled() ? Failed : Disabled;
}
bool ManagerRunner::start(const provider_session::Identity& boot, OperationCancellation lifetime, uint64_t now)
{
    if (running() || state_ == Disabled) return false;
    Manager::Status status = delivery_.status(now);
    // Keep a whole reply window before lease expiry, with strictly positive
    // scheduling margin. Subtraction avoids overflow for large configured limits.
    if (!valid_limits(status.limits, renew_ms_)) {
        delivery_.disconnect(); state_ = Disabled; return false;
    }
    if (!delivery_.offer(boot, lifetime, now)) { fail(); return false; }
    generation_ = delivery_.link_lifetime();
    state_ = Negotiating;
    return true;
}
void ManagerRunner::step(uint64_t now)
{
    if (!running()) return;
    delivery_.step(now);
    Manager::Status status = delivery_.status(now);
    if (generation_.cancelled() || status.state == Manager::Idle || status.state == Manager::Disabled) {
        fail(); return;
    }
    if (status.state == Manager::Offered) {
        if (!delivery_.initialize(now)) fail();
    } else if (status.state == Manager::Ready) {
        state_ = Active;
        // lease_at is the accepted request's SEND time, never reply arrival.
        if (!status.pending && now - status.lease_at >= renew_ms_ && !delivery_.renew(now)) fail();
    }
}
bool ManagerRunner::reserve(uint64_t now, provider_session::Ticket& ticket)
{
    return state_ == Active && !generation_.cancelled() && delivery_.reserve(now, ticket);
}
bool ManagerRunner::current(const provider_session::Ticket& ticket, uint64_t now)
{
    return state_ == Active && !generation_.cancelled() && delivery_.current(ticket, now);
}
bool ManagerRunner::close(const provider_session::Ticket& ticket, uint64_t now)
{
    // A close that cannot be delivered (including a pending renewal) retires
    // the whole grant. Do not leave a cancelled session available for reuse.
    if (!current(ticket, now) || !delivery_.close(ticket, now)) { fail(); return false; }
    return true;
}
void ManagerRunner::stop()
{
    delivery_.disconnect();
    if (state_ != Disabled) state_ = Stopped;
}
}
