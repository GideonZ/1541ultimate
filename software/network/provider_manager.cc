#include "provider_manager.h"

namespace provider_control {
Manager::Manager(IncarnationSource source, ManagerLimits limits, uint64_t initial_request) :
    incarnation_{}, boot_{}, grant_{}, limits_(limits), pending_frame_{}, request_(initial_request),
    renewal_(0), session_(0), sent_at_(0), lease_at_(0), last_now_(0), state_(Disabled), pending_(false), active_(false)
{
    if (limits.reply_ms && limits.lease_ms && source.fresh && source.fresh(source.context, incarnation_) && incarnation_.valid())
        state_ = Idle;
    else memset(&incarnation_, 0, sizeof(incarnation_));
}
void Manager::fail(bool terminal)
{
    cancellation_.signal(); pending_ = active_ = false;
    if (state_ != Disabled) state_ = terminal ? Disabled : Idle;
}
void Manager::disconnect() { fail(); }
void Manager::poll(uint64_t now)
{
    if (now < last_now_) { fail(true); return; }
    last_now_ = now;
    if (state_ == Disabled || state_ == Idle) return;
    if (provider_lifetime_.cancelled() || (pending_ && now - sent_at_ >= limits_.reply_ms) ||
        ((state_ == Ready || state_ == Closing) && now - lease_at_ >= limits_.lease_ms)) fail();
}
bool Manager::begin(unsigned opcode, uint64_t counter, uint32_t required, uint64_t now, uint8_t *output)
{
    if (pending_ || request_ == UINT64_MAX || cancellation_.token().cancelled()) { fail(true); return false; }
    Frame frame = {};
    frame.opcode = opcode; frame.request = ++request_; frame.grant = grant_;
    frame.counter = counter; frame.required = required;
    if (opcode == Offer) { frame.grant = {}; frame.grant.owner = incarnation_; }
    pending_frame_ = frame; pending_ = true; sent_at_ = now;
    encode(frame, output);
    return true;
}
bool Manager::offer(const provider_session::Identity& observed_boot, OperationCancellation provider_lifetime,
                    uint64_t now, uint8_t *output)
{
    poll(now);
    if (state_ == Disabled) return false;
    fail();
    if (!observed_boot.valid() || provider_lifetime.cancelled()) return false;
    boot_ = observed_boot; provider_lifetime_ = provider_lifetime;
    if (!begin(Offer, 0, 0, now, output)) return false;
    state_ = Offering;
    return true;
}
bool Manager::initialize(uint64_t now, uint8_t *output)
{
    poll(now);
    if (state_ != Offered || pending_) return false;
    if (!begin(Initialize, 0, provider_session::Required, now, output)) return false;
    state_ = Initializing;
    return true;
}
bool Manager::renew(uint64_t now, uint8_t *output)
{
    poll(now);
    if (state_ != Ready || pending_) return false;
    if (renewal_ == UINT64_MAX) { fail(true); return false; }
    return begin(Renew, renewal_ + 1, 0, now, output);
}
bool Manager::grant_matches(const provider_session::Grant& grant) const
{
    return grant.challenge.provider == boot_ && grant.challenge.epoch == grant_.challenge.epoch && grant.owner == incarnation_;
}
bool Manager::ready(uint64_t now) const
{
    return state_ == Ready && now >= last_now_ && !provider_lifetime_.cancelled() &&
        !cancellation_.token().cancelled() && now - lease_at_ < limits_.lease_ms &&
        (!pending_ || now - sent_at_ < limits_.reply_ms);
}
bool Manager::reserve(uint64_t now, provider_session::Ticket& result)
{
    poll(now);
    if (!ready(now) || pending_ || active_) return false;
    if (session_ == UINT64_MAX) { fail(true); return false; }
    result = {grant_, ++session_}; active_ = true;
    return true;
}
bool Manager::current(const provider_session::Ticket& ticket, uint64_t now) const
{
    return ready(now) && active_ && ticket.session == session_ && grant_matches(ticket.grant);
}
bool Manager::close(const provider_session::Ticket& ticket, uint64_t now, uint8_t *output)
{
    poll(now);
    if (!current(ticket, now) || pending_) return false;
    cancellation_.signal(); active_ = false;
    if (!begin(Close, session_, 0, now, output)) return false;
    state_ = Closing;
    return true;
}
bool Manager::receive(const uint8_t *bytes, size_t size, uint64_t now)
{
    poll(now);
    if (!pending_) return false;
    Frame reply;
    if (!decode(bytes, size, reply) || !reply.reply || reply.request != pending_frame_.request ||
        reply.opcode != pending_frame_.opcode || reply.counter != pending_frame_.counter ||
        reply.required != pending_frame_.required || !(reply.grant.owner == incarnation_)) return false;
    if (reply.opcode == Offer) {
        if (reply.status == Ok) {
            if (!(reply.grant.challenge.provider == boot_) || !reply.grant.challenge.epoch) return false;
        } else if (reply.grant.challenge.provider.valid() || reply.grant.challenge.epoch) return false;
    } else if (!grant_matches(reply.grant)) return false;
    pending_ = false;
    if (reply.status != Ok) { fail(); return true; }
    switch (reply.opcode) {
    case Offer: grant_ = reply.grant; state_ = Offered; break;
    case Initialize:
        // The remote lease cannot start before we sent initialization. Use that
        // conservative origin, never reply arrival, and reject already-old grants.
        if (now - sent_at_ >= limits_.lease_ms) { fail(); break; }
        lease_at_ = sent_at_; session_ = renewal_ = 0; state_ = Ready; break;
    case Renew:
        if (now - sent_at_ >= limits_.lease_ms) { fail(); break; }
        lease_at_ = sent_at_; renewal_ = reply.counter; break;
    case Close: state_ = Ready; break;
    }
    return true;
}
}
