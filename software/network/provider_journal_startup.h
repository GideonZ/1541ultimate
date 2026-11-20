#ifndef PROVIDER_JOURNAL_STARTUP_H
#define PROVIDER_JOURNAL_STARTUP_H
#include "provider_flash_journal.h"

namespace provider_control {
// Serialized, task-context startup. Installation is an explicit installer action,
// never a fallback from prepare(). This object outlives its one consumer.
class JournalStartup {
public:
    enum State { Cold, Installed, Prepared, Consumed, Failed };
    JournalStartup() : state_(Cold), identity_{} {}
    bool install(Flash&, unsigned first, unsigned second, uint64_t fresh_domain);
    bool prepare(Flash&, unsigned first, unsigned second);
    void disable() { identity_={}; state_=Failed; }
    State state() const { return state_; }
    IncarnationSource source() { return {this,take}; }
private:
    State state_;
    provider_session::Identity identity_;
    static bool take(void*, provider_session::Identity&);
    JournalStartup(const JournalStartup&) = delete;
    JournalStartup& operator=(const JournalStartup&) = delete;
};
}
#endif
