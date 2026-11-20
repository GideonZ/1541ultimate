#ifndef PROVIDER_MANAGER_H
#define PROVIDER_MANAGER_H
#include "provider_control.h"

namespace provider_control {
// A successful callback must supply a fresh, qualified 128-bit incarnation.
// No clock/MAC/volatile-counter fallback. Failure or zero output disables it.
struct IncarnationSource { void *context; bool (*fresh)(void*, provider_session::Identity&); };
struct ManagerLimits { uint32_t reply_ms, lease_ms; };
// All calls, including current()/ready(), belong to one serialized owner task.
// Only copied cancellation tokens may be read concurrently. This object and the
// provider lifetime source must outlive their tokens. Limits are local bounds;
// lease_ms must not exceed the configured provider lease.
class Manager {
public:
    enum State { Disabled, Idle, Offering, Offered, Initializing, Ready, Closing };
    Manager(IncarnationSource, ManagerLimits, uint64_t initial_request = 0);
    bool offer(const provider_session::Identity& observed_boot, OperationCancellation provider_lifetime,
               uint64_t now, uint8_t output[FrameSize]);
    bool initialize(uint64_t now, uint8_t output[FrameSize]);
    bool renew(uint64_t now, uint8_t output[FrameSize]);
    bool close(const provider_session::Ticket&, uint64_t now, uint8_t output[FrameSize]);
    bool receive(const uint8_t*, size_t, uint64_t now);
    bool reserve(uint64_t now, provider_session::Ticket&); // Failed remote admission requires disconnect().
    bool current(const provider_session::Ticket&, uint64_t now) const;
    bool ready(uint64_t now) const;
    void poll(uint64_t now);
    void disconnect(); // Also required after failed request enqueue.
    OperationCancellation lifetime() const { return cancellation_.token(); }
    State state() const { return state_; }
    struct Status { State state; bool pending; uint64_t lease_at; ManagerLimits limits; };
    Status status() const { return {state_, pending_, lease_at_, limits_}; }
private:
    provider_session::Identity incarnation_, boot_;
    provider_session::Grant grant_;
    ManagerLimits limits_;
    CancellationSource cancellation_;
    OperationCancellation provider_lifetime_;
    Frame pending_frame_;
    uint64_t request_, renewal_, session_, sent_at_, lease_at_, last_now_;
    State state_;
    bool pending_, active_;
    bool begin(unsigned opcode, uint64_t counter, uint32_t required, uint64_t now, uint8_t*);
    bool grant_matches(const provider_session::Grant&) const;
    void fail(bool terminal = false);
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
};
}
#endif
