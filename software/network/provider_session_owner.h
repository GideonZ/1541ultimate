#ifndef PROVIDER_SESSION_OWNER_H
#define PROVIDER_SESSION_OWNER_H

#include <stdint.h>
#include <string.h>

namespace provider_session {
enum Feature { Authenticated = 1, AuthenticatedEof = 2, Deadline = 4, Cancel = 8, SessionIdentity = 16 };
const unsigned Required = Authenticated | AuthenticatedEof | Deadline | Cancel | SessionIdentity;
struct Identity {
    uint8_t bytes[16];
    bool valid() const;
    bool operator==(const Identity& other) const { return !memcmp(bytes, other.bytes, sizeof(bytes)); }
};
struct Challenge { Identity provider; uint64_t epoch; };
struct Grant { Challenge challenge; Identity owner; };
struct Ticket { Grant grant; uint64_t session; };
struct Limits { uint32_t initialization_ms, lease_ms, cleanup_ms; };

// The owner task calls these methods; interrupt/receive paths must queue work.
// cancel() only signals cancellation. cleanup() performs bounded, nonblocking
// owner-side progress and returns true only when all remote resources are gone.
// The object is deleted only after that confirmation. Neither callback reenters
// Owner. A failed admission leaves ownership with the caller.
class Resource {
public:
    virtual ~Resource() {}
    virtual void cancel() = 0;
    virtual bool cleanup() = 0;
};

class Owner {
public:
    enum State { Unavailable, Offered, Ready, Quarantined };
    enum Failure { None, LeaseExpired, InitializationExpired, CleanupExpired,
                   ClockRegression, Exhausted, Disabled };
    Owner(const Identity& boot, unsigned features, Limits limits, uint64_t initial_epoch = 0);
    // No implicit teardown: stop and drain before destroying the owner. On a
    // failed cleanup the quarantined owner/resource must remain alive until boot.
    ~Owner();
    Challenge offer(uint64_t now);
    bool initialize(const Challenge&, const Identity& owner, unsigned version,
                    unsigned required, uint64_t now, Grant& result);
    bool renew(const Grant&, uint64_t sequence, uint64_t now);
    bool admit(const Grant&, uint64_t session, Resource*, uint64_t now, Ticket& result);
    bool current(const Ticket&, uint64_t now) const;
    bool close(const Ticket&, uint64_t now);
    void stop(uint64_t now);
    void poll(uint64_t now);
    State state() const { return state_; }
    Failure failure() const { return failure_; }
    bool occupied() const { return resource_ != 0; }
private:
    Identity boot_;
    unsigned features_;
    Limits limits_;
    Grant grant_;
    uint64_t epoch_, session_, renew_sequence_, offered_at_, renewed_at_, cleanup_at_, last_now_;
    Resource *resource_;
    State state_;
    Failure failure_;
    bool draining_;
    bool advance(uint64_t now);
    bool matches(const Grant&) const;
    void drain(uint64_t now);
    void invalidate(Failure, uint64_t now, bool quarantine);
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
};
}
#endif
