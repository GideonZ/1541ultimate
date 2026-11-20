#ifndef PROVIDER_INCARNATION_H
#define PROVIDER_INCARNATION_H
#include "provider_manager.h"

namespace provider_control {
// A storage adapter must atomically reserve and durably commit a strictly
// increasing epoch BEFORE returning success. Failed/ambiguous writes burn the
// reservation. Erase, rollback, cloning or loss of the journal requires a new
// externally qualified domain; never recreate an empty journal automatically.
struct IncarnationJournal {
    void *context;
    bool (*reserve)(void*, uint64_t&);
};

// Serialized startup only. The domain identifies one journal for its entire
// lifetime. This is a uniqueness source, not entropy or peer authentication.
// No storage adapter or domain is selected implicitly by the firmware.
class DurableIncarnation {
public:
    DurableIncarnation(uint64_t domain, IncarnationJournal journal) :
        journal_(journal), domain_(domain), last_(0), failed_(!domain || !journal.reserve) {}
    IncarnationSource source() { return {this, fresh}; }
private:
    IncarnationJournal journal_;
    uint64_t domain_, last_;
    bool failed_;
    static bool fresh(void *context, provider_session::Identity& result) {
        auto& self = *static_cast<DurableIncarnation*>(context);
        result = {};
        uint64_t epoch = 0;
        if (self.failed_) return false;
        if (!self.journal_.reserve(self.journal_.context, epoch) || !epoch || epoch <= self.last_) {
            self.failed_ = true;
            return false;
        }
        self.last_ = epoch;
        for (unsigned i = 0; i < 8; ++i) {
            result.bytes[i] = uint8_t(self.domain_ >> (8 * i));
            result.bytes[i + 8] = uint8_t(epoch >> (8 * i));
        }
        if (epoch == UINT64_MAX) self.failed_ = true;
        return true;
    }
    DurableIncarnation(const DurableIncarnation&) = delete;
    DurableIncarnation& operator=(const DurableIncarnation&) = delete;
};
}
#endif
