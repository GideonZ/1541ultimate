#ifndef HTTP_SESSION_CONTRACT_MODEL_H
#define HTTP_SESSION_CONTRACT_MODEL_H

#include <stdint.h>

// Executable design model only. No wire format, threads or resource pool.
namespace session_contract {
enum Feature { Authenticated = 1, AuthenticatedEof = 2, Deadline = 4, Cancel = 8, Identity = 16 };
const unsigned Required = Authenticated | AuthenticatedEof | Deadline | Cancel | Identity;
struct Pair { uint64_t controller, provider, probe; };
inline bool equal(Pair a, Pair b)
{
    return a.controller == b.controller && a.provider == b.provider && a.probe == b.probe;
}
struct Ticket { Pair pair; uint64_t session, operation; };
inline bool equal(Ticket a, Ticket b)
{
    return equal(a.pair, b.pair) && a.session == b.session && a.operation == b.operation;
}

class Channel {
    Pair pair_;
    uint64_t sessions_, limit_;
    bool pending_, ready_;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
public:
    // Small limits allow exhaustion tests without assuming a production width.
    explicit Channel(uint64_t boot, uint64_t limit = UINT64_MAX) :
        pair_{boot, 0, 0}, sessions_(0), limit_(limit), pending_(false), ready_(false) {}
    Pair probe()
    {
        ready_ = pending_ = false;
        if (!pair_.controller || pair_.probe == limit_) return {};
        ++pair_.probe;
        pair_.provider = 0;
        pending_ = true;
        return pair_;
    }
    bool accept(Pair reply, unsigned version, unsigned features, bool initialized)
    {
        if (!pending_ || reply.controller != pair_.controller || reply.probe != pair_.probe) return false;
        pending_ = false;
        if (!reply.provider || version != 1 || (features & Required) != Required || !initialized) return false;
        pair_ = reply;
        ready_ = true;
        return true;
    }
    void invalidate() { ready_ = pending_ = false; }
    bool current(Pair pair) const { return ready_ && equal(pair_, pair); }
    Ticket reserve()
    {
        if (!ready_ || sessions_ == limit_) return {};
        return {pair_, ++sessions_, 1};
    }
};

enum class State { Opening, Ready, Pending, Terminal };
enum class Cause { None, Complete, Cancelled, Timeout, Restart, Failed, Closed, Exhausted, Unavailable };

class Lease {
    Channel& channel_;
    const unsigned owner_;
    const uint64_t deadline_, limit_;
    Ticket ticket_;
    State state_;
    Cause cause_;
    unsigned closes_;
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    void stop(Cause cause)
    {
        if (state_ == State::Terminal) return;
        cause_ = cause;
        state_ = State::Terminal;
        ++closes_; // One logical owner-release obligation, not proof of remote reclamation.
    }
    bool usable(unsigned owner, uint64_t now, bool cancel)
    {
        if (owner != owner_ || state_ == State::Terminal) return false;
        if (cancel) stop(Cause::Cancelled);
        else if (!channel_.current(ticket_.pair)) stop(Cause::Restart);
        else if (now >= deadline_) stop(Cause::Timeout);
        return state_ != State::Terminal;
    }
public:
    Lease(Channel& channel, unsigned owner, uint64_t deadline, uint64_t operation_limit = UINT64_MAX) :
        channel_(channel), owner_(owner), deadline_(deadline), limit_(operation_limit), ticket_(channel.reserve()),
        state_(State::Opening), cause_(Cause::None), closes_(0)
    {
        if (!ticket_.session) { state_ = State::Terminal; cause_ = Cause::Unavailable; }
    }
    Ticket ticket() const { return ticket_; }
    State state() const { return state_; }
    Cause cause() const { return cause_; }
    unsigned closes() const { return closes_; }
    bool opened(unsigned owner, Ticket reply, bool success, uint64_t now, bool cancel = false)
    {
        if (state_ != State::Opening || !equal(ticket_, reply) || !usable(owner, now, cancel)) return false;
        if (!success) { stop(Cause::Failed); return false; }
        state_ = State::Ready;
        return true;
    }
    bool begin(unsigned owner, uint64_t now)
    {
        if (state_ != State::Ready || !usable(owner, now, false)) return false;
        if (ticket_.operation >= limit_) { stop(Cause::Exhausted); return false; }
        ++ticket_.operation;
        state_ = State::Pending;
        return true;
    }
    // True authorizes publication of this operation's result exactly once.
    bool completed(unsigned owner, Ticket reply, bool success, uint64_t now, bool cancel = false)
    {
        if (state_ != State::Pending || !equal(ticket_, reply) || !usable(owner, now, cancel)) return false;
        if (!success) { stop(Cause::Failed); return false; }
        state_ = State::Ready;
        return true;
    }
    bool cancel(unsigned owner, Ticket lease)
    {
        if (owner != owner_ || !equal(lease.pair, ticket_.pair) || lease.session != ticket_.session || state_ == State::Terminal) return false;
        stop(Cause::Cancelled);
        return true;
    }
    bool poll(unsigned owner, uint64_t now, bool cancel_requested = false)
    {
        return usable(owner, now, cancel_requested);
    }
    bool finish(unsigned owner, uint64_t now, bool cancel_requested = false)
    {
        if (state_ != State::Ready || !usable(owner, now, cancel_requested)) return false;
        stop(Cause::Complete);
        return true;
    }
    void close(unsigned owner) { if (owner == owner_) stop(Cause::Closed); }
};
}
#endif
