#ifndef PROVIDER_MANAGER_RUNNER_H
#define PROVIDER_MANAGER_RUNNER_H
#include "provider_manager_delivery.h"

namespace provider_control {
// One serialized task owns this driver AND its delivery object. ISR ingress may
// only use delivery's receive/invalidate API. Neither object may move or die while
// cancellation tokens exist. start() requires an atomic provider boot/token snapshot.
// The caller must schedule step() often enough to meet reply and lease deadlines;
// missed deadlines fail closed, without retries. No task or UART binding is created.
class ManagerRunner {
public:
    enum State { Stopped, Negotiating, Active, Failed, Disabled };
    ManagerRunner(ManagerDelivery&, uint32_t renew_ms);
    static bool valid_limits(ManagerLimits limits, uint32_t renew_ms) {
        return limits.reply_ms && renew_ms && renew_ms < limits.lease_ms &&
               limits.reply_ms < limits.lease_ms - renew_ms;
    }
    bool start(const provider_session::Identity&, OperationCancellation, uint64_t now);
    void step(uint64_t now); // At most one queued reply and one outgoing control frame.
    bool reserve(uint64_t now, provider_session::Ticket&);
    bool current(const provider_session::Ticket&, uint64_t now);
    bool close(const provider_session::Ticket&, uint64_t now);
    void stop(); // Local retirement now; unreachable remote resources expire by lease.
    State state() const { return state_; } // Last owner observation, not an admission check.
private:
    ManagerDelivery& delivery_;
    uint32_t renew_ms_;
    OperationCancellation generation_;
    State state_;
    bool running() const { return state_ == Negotiating || state_ == Active; }
    void fail();
    ManagerRunner(const ManagerRunner&) = delete;
    ManagerRunner& operator=(const ManagerRunner&) = delete;
};
}
#endif
