#ifndef PROVIDER_CONTROL_H
#define PROVIDER_CONTROL_H
#include "provider_session_owner.h"
#include "operation_cancellation.h"
#include <stddef.h>

namespace provider_control {
enum { FrameSize = 76, QueueDepth = 4 };
enum Opcode { Offer = 1, Initialize = 2, Renew = 3, Close = 4 };
enum Status { Ok = 1, Busy = 2, Rejected = 3, Unavailable = 4 };
struct Frame {
    uint8_t opcode, status;
    bool reply;
    uint64_t request;
    provider_session::Grant grant;
    uint64_t counter;
    uint32_t required;
};
bool decode(const uint8_t*, size_t, Frame&);
void encode(const Frame&, uint8_t[FrameSize]);
struct Event { Frame frame; OperationCancellation generation; };

// Lock serializes ingress, cancellation writers and the final reply enqueue.
// Queue and send operations must have zero wait and must not reenter Worker.
// send() returns false unless it takes a complete copy of the frame.
class Port {
public:
    virtual ~Port() {}
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual bool enqueue(const Event&) = 0;
    virtual bool dequeue(Event&) = 0;
    virtual bool send(const uint8_t*, size_t) = 0;
};
class Worker {
public:
    Worker(Port&, const provider_session::Identity&, unsigned features, provider_session::Limits);
    bool submit(const uint8_t*, size_t); // Task ingress only; no resource calls.
    void step(uint64_t now); // Serialized owner task, bounded work per iteration.
    bool admit(const provider_session::Grant&, uint64_t session, provider_session::Resource*, uint64_t now,
               provider_session::Ticket&);
    bool current(const provider_session::Ticket&, uint64_t now) const; // Owner TLS callback.
    // Owner TLS callback: consume at most one control frame without resource
    // callbacks. Stop/restart/close only fence the call; step cleans up afterward.
    bool service(const provider_session::Ticket&, uint64_t now);
    OperationCancellation resource_lifetime() const { return resource_generation_; } // Owner snapshot for final reply fence.
    void disconnect(); // Task ingress: retain cancellation even if queue is full.
    bool occupied() const { return owner_.occupied(); } // Owner task only.
private:
    Port& port_;
    provider_session::Owner owner_;
    CancellationSource delivery_;
    OperationCancellation resource_generation_;
    provider_session::Ticket last_;
    provider_session::Identity offered_owner_ = {};
    bool active_, known_, stop_, close_, pending_;
    Event pending_event_;
    bool matches(const Frame&) const;
    void invalidate_locked();
    void respond(const Event&, Status, const provider_session::Grant* = 0);
    void finish_close();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
};
}
#endif
