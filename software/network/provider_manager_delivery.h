#ifndef PROVIDER_MANAGER_DELIVERY_H
#define PROVIDER_MANAGER_DELIVERY_H
#include "provider_manager.h"

namespace provider_control {
// Task locking must exclude receive/loss ISR ingress. send() copies the whole
// frame with zero wait, does not reenter delivery, and consumes its own buffers.
class ManagerPort {
public:
    virtual ~ManagerPort() {}
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual bool send(const uint8_t*, size_t) = 0;
};
class ManagerDelivery {
public:
    ManagerDelivery(ManagerPort&, IncarnationSource, ManagerLimits);
    // ISR only, or with the port lock held. Copies bytes; never calls Manager.
    // Caller retains and frees the receive buffer exactly once.
    bool receive_isr(const uint8_t*, size_t);
    void invalidate_isr();
    // Serialized owner-task API. Each operation takes the port lock briefly.
    bool offer(const provider_session::Identity&, OperationCancellation, uint64_t);
    bool initialize(uint64_t);
    bool renew(uint64_t);
    bool close(const provider_session::Ticket&, uint64_t);
    bool reserve(uint64_t, provider_session::Ticket&);
    bool current(const provider_session::Ticket&, uint64_t);
    bool enabled();
    void step(uint64_t); // At most one reply; deadlines use processing time.
    void disconnect();
    OperationCancellation link_lifetime() const { return invalidation_.token(); }
private:
    ManagerPort& port_;
    Manager manager_;
    CancellationSource invalidation_;
    OperationCancellation grant_generation_;
    uint8_t queue_[QueueDepth][FrameSize];
    unsigned read_, count_;
    bool lost_;
    void synchronize();
    bool transmit(bool prepared, const uint8_t*);
    ManagerDelivery(const ManagerDelivery&) = delete;
    ManagerDelivery& operator=(const ManagerDelivery&) = delete;
};
}
#endif
