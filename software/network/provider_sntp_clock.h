#ifndef PROVIDER_SNTP_CLOCK_H
#define PROVIDER_SNTP_CLOCK_H
#include <stdint.h>
namespace provider_data {
// Explicitly selected ordinary SNTP policy; this is not authenticated time.
// Serialized by the source adapter. A discontinuity latches closed until boot.
class SntpClock {
public:
    SntpClock() : seen_(false), failed_(false), utc_(0), monotonic_(0) {}
    bool accept(int64_t utc,int32_t microseconds,uint64_t now) {
        if (failed_) return false;
        if (utc<1577836800 || utc>4102444800LL || microseconds<0 || microseconds>=1000000 ||
            (seen_ && (now<monotonic_ || now-monotonic_>86400000))) return fail();
        if (seen_) {
            int64_t expected=utc_+int64_t((now-monotonic_)/1000);
            if (utc<expected-300 || utc>expected+300) return fail();
        }
        seen_=true; utc_=utc; monotonic_=now; return true;
    }
    void reject() { failed_=true; }
private:
    bool seen_,failed_;
    int64_t utc_;
    uint64_t monotonic_;
    bool fail() { failed_=true; return false; }
};
}
#endif
