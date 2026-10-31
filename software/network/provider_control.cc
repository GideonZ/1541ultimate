#include "provider_control.h"

namespace provider_control {
static uint64_t get64(const uint8_t *p)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= uint64_t(p[i]) << (8 * i);
    return value;
}
static void put64(uint8_t *p, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) p[i] = uint8_t(value >> (8 * i));
}
bool decode(const uint8_t *data, size_t size, Frame& result)
{
    if (size != FrameSize || data[0] != 2 || data[1] != 0xfe || data[2] || data[3] ||
        memcmp(data + 4, "PSN2", 4) || data[8] != 2 || data[9] < Offer || data[9] > Close ||
        data[11] > 1 || (data[11] ? (data[10] < Ok || data[10] > Unavailable) : data[10] != 0) ||
        data[72] || data[73] || data[74] || data[75]) return false;
    Frame frame = {};
    frame.opcode = data[9]; frame.status = data[10]; frame.reply = data[11];
    frame.request = get64(data + 12);
    if (!frame.request) return false;
    memcpy(frame.grant.challenge.provider.bytes, data + 20, 16);
    frame.grant.challenge.epoch = get64(data + 36);
    memcpy(frame.grant.owner.bytes, data + 44, 16);
    frame.counter = get64(data + 60);
    for (unsigned i = 0; i < 4; ++i) frame.required |= uint32_t(data[68 + i]) << (8 * i);
    if (!frame.reply) {
        if (frame.opcode == Offer) {
            if (frame.grant.challenge.provider.valid() || frame.grant.challenge.epoch ||
                !frame.grant.owner.valid() || frame.counter || frame.required) return false;
        } else {
            if (!frame.grant.challenge.provider.valid() || !frame.grant.challenge.epoch || !frame.grant.owner.valid()) return false;
            if (frame.opcode == Initialize ? frame.counter != 0 : (!frame.counter || frame.required)) return false;
        }
    }
    result = frame;
    return true;
}
void encode(const Frame& frame, uint8_t data[FrameSize])
{
    memset(data, 0, FrameSize);
    data[0] = 2; data[1] = 0xfe; memcpy(data + 4, "PSN2", 4);
    data[8] = 2; data[9] = frame.opcode; data[10] = frame.status; data[11] = frame.reply;
    put64(data + 12, frame.request);
    memcpy(data + 20, frame.grant.challenge.provider.bytes, 16);
    put64(data + 36, frame.grant.challenge.epoch);
    memcpy(data + 44, frame.grant.owner.bytes, 16);
    put64(data + 60, frame.counter);
    for (unsigned i = 0; i < 4; ++i) data[68 + i] = uint8_t(frame.required >> (8 * i));
}
Worker::Worker(Port& port, const provider_session::Identity& boot, unsigned features, provider_session::Limits limits) :
    port_(port), owner_(boot, features, limits), last_{}, active_(false), known_(false),
    stop_(false), close_(false), pending_(false), pending_event_{} {}

bool Worker::matches(const Frame& frame) const
{
    return known_ && frame.grant.challenge.provider == last_.grant.challenge.provider &&
        frame.grant.challenge.epoch == last_.grant.challenge.epoch && frame.grant.owner == last_.grant.owner &&
        frame.counter == last_.session;
}
void Worker::invalidate_locked() { delivery_.signal(); stop_ = true; }
void Worker::disconnect()
{
    port_.lock(); invalidate_locked(); port_.unlock();
}
bool Worker::submit(const uint8_t *bytes, size_t size)
{
    Frame frame;
    if (!decode(bytes, size, frame) || frame.reply) return false;
    port_.lock();
    if (frame.opcode == Offer) invalidate_locked();
    else if (frame.opcode == Close && active_ && matches(frame)) { delivery_.signal(); close_ = true; }
    Event event = {frame, delivery_.token()};
    bool accepted = !event.generation.cancelled() && port_.enqueue(event);
    if (!accepted) invalidate_locked();
    port_.unlock();
    return accepted;
}
void Worker::respond(const Event& event, Status status, const provider_session::Grant *grant)
{
    Frame response = event.frame; response.reply = true; response.status = status;
    if (grant) response.grant = *grant;
    uint8_t bytes[FrameSize]; encode(response, bytes);
    port_.lock();
    if (!event.generation.cancelled() && !port_.send(bytes, sizeof(bytes))) invalidate_locked();
    port_.unlock();
}
void Worker::finish_close()
{
    if (!pending_) return;
    if (pending_event_.generation.cancelled()) { pending_ = false; return; }
    if (owner_.occupied() && owner_.state() != provider_session::Owner::Quarantined) return;
    respond(pending_event_, owner_.state() == provider_session::Owner::Quarantined ? Unavailable : Ok);
    pending_ = false;
}
void Worker::step(uint64_t now)
{
    port_.lock();
    bool stop = stop_, close = close_; stop_ = close_ = false;
    port_.unlock();
    if (stop) owner_.stop(now);
    else if (close && known_) owner_.close(last_, now);
    owner_.poll(now);
    port_.lock(); active_ = owner_.occupied(); port_.unlock();
    finish_close();
    if (pending_) return;
    Event event;
    port_.lock();
    bool received = port_.dequeue(event);
    stop = stop_; close = close_; stop_ = close_ = false;
    port_.unlock();
    // Ingress can arrive during cleanup. Consume its signal before executing
    // the corresponding queued request, never on the following iteration.
    if (stop) owner_.stop(now);
    else if (close && known_) owner_.close(last_, now);
    if (!received || event.generation.cancelled()) return;
    const Frame& frame = event.frame;
    provider_session::Grant grant = {};
    switch (frame.opcode) {
    case Offer:
        port_.lock(); known_ = false; port_.unlock();
        offered_owner_ = frame.grant.owner;
        grant.challenge = owner_.offer(now);
        grant.owner = offered_owner_;
        respond(event, grant.challenge.epoch ? Ok : Unavailable, &grant);
        break;
    case Initialize:
        if (!(frame.grant.owner == offered_owner_)) respond(event, Rejected);
        else if (owner_.initialize(frame.grant.challenge, frame.grant.owner, 1, frame.required, now, grant)) respond(event, Ok, &grant);
        else respond(event, owner_.state() == provider_session::Owner::Quarantined ? Unavailable : owner_.occupied() ? Busy : Rejected);
        break;
    case Renew:
        respond(event, owner_.renew(frame.grant, frame.counter, now) ? Ok : Rejected);
        break;
    case Close:
        if (!matches(frame)) { respond(event, Rejected); break; }
        if (owner_.occupied()) owner_.close(last_, now);
        pending_event_ = event; pending_ = true;
        finish_close();
        break;
    }
}
bool Worker::admit(const provider_session::Grant& grant, uint64_t session, provider_session::Resource *resource,
                   uint64_t now, provider_session::Ticket& result)
{
    if (owner_.occupied()) return false;
    port_.lock();
    bool accepted = !stop_ && !close_ && !pending_ && !delivery_.token().cancelled() &&
        owner_.admit(grant, session, resource, now, result);
    if (accepted) { last_ = result; known_ = active_ = true; resource_generation_ = delivery_.token(); }
    port_.unlock();
    return accepted;
}
bool Worker::current(const provider_session::Ticket& ticket, uint64_t now) const
{
    return !resource_generation_.cancelled() && owner_.current(ticket, now);
}
bool Worker::service(const provider_session::Ticket& ticket, uint64_t now)
{
    Event event;
    Status status = Rejected;
    port_.lock();
    if (!current(ticket, now)) { port_.unlock(); return false; }
    bool received = port_.dequeue(event);
    if (received && !event.generation.cancelled()) {
        const Frame& frame = event.frame;
        if (frame.opcode == Renew)
            status = owner_.renew_active(ticket, frame.grant, frame.counter, now) ? Ok : Rejected;
        else if (frame.opcode == Initialize && frame.grant.owner == offered_owner_)
            status = Busy;
        // Matching Close and any Offer already invalidate resource_generation_
        // at ingress. A foreign Close cannot mutate the live session.
    }
    port_.unlock();
    if (received) respond(event, status);
    return current(ticket, now);
}
}
