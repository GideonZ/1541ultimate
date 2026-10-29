#ifndef PROVIDER_FLASH_JOURNAL_H
#define PROVIDER_FLASH_JOURNAL_H
#include "provider_incarnation.h"
class Flash;
namespace provider_control {
// Two explicitly reserved configuration sectors, serialized at startup before
// ConfigManager can allocate them. No automatic allocation, erase or repair.
// Ordinary settings reset/import must never recreate an erased journal domain.
class FlashIncarnationJournal {
public:
    FlashIncarnationJournal(Flash&, unsigned slot_a, unsigned slot_b, uint64_t domain);
    IncarnationJournal journal() { return {this,reserve}; }
    // Installer-only provisioning into two completely erased sectors. The
    // caller must supply a new qualified domain, never a restored old domain.
    bool provision();
private:
    Flash& flash_;
    unsigned slots_[2];
    uint64_t domain_;
    bool failed_;
    bool geometry(unsigned pages[2]);
    bool read(unsigned page, uint64_t& epoch);
    bool write(unsigned page, uint64_t epoch, bool erase);
    static bool reserve(void*, uint64_t&);
    FlashIncarnationJournal(const FlashIncarnationJournal&) = delete;
    FlashIncarnationJournal& operator=(const FlashIncarnationJournal&) = delete;
};
}
#endif
