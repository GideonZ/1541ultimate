#include "provider_manager_delivery.h"
namespace provider_control {
namespace {
struct Guard {
    ManagerPort& port;
    explicit Guard(ManagerPort& p) : port(p) { port.lock(); }
    ~Guard() { port.unlock(); }
};
}
ManagerDelivery::ManagerDelivery(ManagerPort& port, IncarnationSource source, ManagerLimits limits) :
    port_(port), manager_(source, limits), read_(0), count_(0), lost_(false) {}
void ManagerDelivery::invalidate_isr()
{
    invalidation_.signal(); lost_ = true;
    read_ = count_ = 0; // Discard all pre-loss replies, independently of queue capacity.
}
bool ManagerDelivery::receive_isr(const uint8_t *bytes, size_t size)
{
    Frame frame;
    if (!decode(bytes, size, frame) || !frame.reply) return false;
    if (count_ == QueueDepth) { invalidate_isr(); return false; }
    memcpy(queue_[(read_ + count_) % QueueDepth], bytes, FrameSize);
    ++count_;
    return true;
}
void ManagerDelivery::synchronize()
{
    if (lost_) { manager_.disconnect(); read_ = count_ = 0; lost_ = false; }
}
bool ManagerDelivery::transmit(bool prepared, const uint8_t *bytes)
{
    if (!prepared) return false;
    if (port_.send(bytes, FrameSize)) return true;
    invalidate_isr(); synchronize(); return false;
}
bool ManagerDelivery::offer(const provider_session::Identity& boot, OperationCancellation lifetime, uint64_t now)
{
    Guard guard(port_); synchronize();
    // A new negotiation also retires any caller's old delivery token.
    invalidate_isr(); synchronize(); grant_generation_ = invalidation_.token();
    if (grant_generation_.cancelled()) return false;
    uint8_t bytes[FrameSize];
    return transmit(manager_.offer(boot, lifetime, now, bytes), bytes);
}
bool ManagerDelivery::initialize(uint64_t now)
{
    Guard guard(port_); synchronize(); uint8_t bytes[FrameSize];
    return transmit(manager_.initialize(now, bytes), bytes);
}
bool ManagerDelivery::renew(uint64_t now)
{
    Guard guard(port_); synchronize(); uint8_t bytes[FrameSize];
    return transmit(manager_.renew(now, bytes), bytes);
}
bool ManagerDelivery::close(const provider_session::Ticket& ticket, uint64_t now)
{
    Guard guard(port_); synchronize(); uint8_t bytes[FrameSize];
    return transmit(manager_.close(ticket, now, bytes), bytes);
}
bool ManagerDelivery::reserve(uint64_t now, provider_session::Ticket& ticket)
{
    Guard guard(port_); synchronize();
    return !grant_generation_.cancelled() && manager_.reserve(now, ticket);
}
bool ManagerDelivery::current(const provider_session::Ticket& ticket, uint64_t now)
{
    Guard guard(port_);
    return !grant_generation_.cancelled() && manager_.current(ticket, now);
}
bool ManagerDelivery::enabled()
{
    Guard guard(port_); return manager_.state() != Manager::Disabled;
}
Manager::Status ManagerDelivery::status(uint64_t now)
{
    Guard guard(port_); synchronize(); manager_.poll(now); return manager_.status();
}
void ManagerDelivery::step(uint64_t now)
{
    Guard guard(port_); synchronize(); manager_.poll(now);
    if (!count_) return;
    manager_.receive(queue_[read_], FrameSize, now);
    read_ = (read_ + 1) % QueueDepth; --count_;
}
void ManagerDelivery::disconnect()
{
    Guard guard(port_); invalidate_isr(); synchronize();
}
}
