#ifndef PROVIDER_TLS_POLICY_H
#define PROVIDER_TLS_POLICY_H
#include <stdint.h>

namespace provider_data {
// The product supplies bounded task-context critical sections. They serialize
// time qualification, entropy lifecycle notifications and owner-side checks.
struct PolicyLock {
    virtual ~PolicyLock() {}
    virtual void lock() = 0;
    virtual void unlock() = 0;
};

// Readiness is explicit, temporary and revocable. Only a product-approved UTC
// source may qualify the clock. SNTP completion or a plausible RTC alone is not
// authorization. Entropy must be qualified for the actual hardware operating
// mode, and revoked BEFORE that source is disabled. This object has owner lifetime.
class TlsPolicy {
public:
    explicit TlsPolicy(PolicyLock& lock) : lock_(lock), generation_(1),
        anchor_(0), last_(0), utc_(0), lifetime_(0), tolerance_(0),
        clock_(false), entropy_(false), exhausted_(false) {}
    bool qualify_clock(int64_t utc, uint64_t now, uint32_t lifetime_ms, uint32_t tolerance_seconds) {
        Guard guard(lock_);
        advance();
        clock_ = !exhausted_ && utc >= 1577836800 && utc <= INT64_MAX - 86460 &&
            lifetime_ms && lifetime_ms <= 86400000 && tolerance_seconds <= 60;
        if (clock_) {
            utc_ = utc; anchor_ = last_ = now;
            lifetime_ = lifetime_ms; tolerance_ = tolerance_seconds;
        }
        return clock_;
    }
    void entropy_available(bool available) {
        Guard guard(lock_);
        if (available != entropy_) advance();
        entropy_ = available && !exhausted_;
    }
    void revoke() {
        Guard guard(lock_); advance(); clock_ = entropy_ = false;
    }
    void revoke_clock() {
        Guard guard(lock_); advance(); clock_ = false;
    }
    uint32_t generation() {
        Guard guard(lock_); return exhausted_ ? 0 : generation_;
    }
    bool ready(uint32_t generation, uint64_t now, int64_t utc) {
        Guard guard(lock_);
        if (exhausted_ || !clock_ || !entropy_ || !generation || generation != generation_) return false;
        if (now < last_ || now < anchor_ || now - anchor_ >= lifetime_) return invalidate_clock();
        last_ = now;
        const int64_t expected = utc_ + int64_t((now - anchor_) / 1000);
        if (utc < expected - tolerance_ || utc > expected + tolerance_) return invalidate_clock();
        return true;
    }
private:
    struct Guard {
        PolicyLock& lock;
        explicit Guard(PolicyLock& value) : lock(value) { lock.lock(); }
        ~Guard() { lock.unlock(); }
    };
    PolicyLock& lock_;
    uint32_t generation_;
    uint64_t anchor_, last_;
    int64_t utc_;
    uint32_t lifetime_, tolerance_;
    bool clock_, entropy_, exhausted_;
    void advance() {
        if (generation_ == UINT32_MAX) exhausted_ = true;
        else ++generation_;
    }
    bool invalidate_clock() { clock_ = false; advance(); return false; }
    TlsPolicy(const TlsPolicy&) = delete;
    TlsPolicy& operator=(const TlsPolicy&) = delete;
};
}
#endif
