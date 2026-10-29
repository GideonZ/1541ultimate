#include "provider_native_startup.h"
#ifdef ULTIMATE_PROVIDER_JOURNAL
#include "provider_journal_startup.h"
#include "provider_manager_task.h"
#include "flash.h"
#include <new>
namespace {
provider_control::JournalStartup& journal_startup() {
    static provider_control::JournalStartup instance;
    return instance;
}
Flash *storage;
bool configuration_started, owner_attempted;
ProviderManagerTask *owner;
alignas(ProviderManagerTask) uint8_t owner_memory[sizeof(ProviderManagerTask)];
bool slots(Flash& flash,unsigned& a,unsigned& b) {
    int count=flash.get_number_of_config_pages();
    if (count<2) return false;
    a=unsigned(count-2); b=unsigned(count-1); return true;
}
}
bool provider_journal_install(Flash& flash,uint64_t domain)
{
    if (configuration_started || owner_attempted || (storage && storage!=&flash)) return false;
    storage=&flash;
    unsigned a,b;
    if (!slots(flash,a,b)) { journal_startup().disable(); return false; }
    return journal_startup().install(flash,a,b,domain);
}
void provider_journal_prepare(Flash& flash)
{
    if (configuration_started) return;
    configuration_started=true;
    unsigned a,b;
    if ((storage && storage!=&flash) || !slots(flash,a,b)) { journal_startup().disable(); return; }
    storage=&flash;
    journal_startup().prepare(flash,a,b); // No install or repair on ordinary boot.
}
bool provider_journal_reserved_slot(Flash& flash,int slot)
{
    unsigned a,b;
    return slot>=0 && slots(flash,a,b) && (unsigned(slot)==a || unsigned(slot)==b);
}
void provider_journal_start_owner(WiFi& wifi)
{
    if (owner_attempted) return;
    owner_attempted=true;
    if (!configuration_started || journal_startup().state()!=provider_control::JournalStartup::Prepared) return;
    // Construction consumes a prepared identity; no flash I/O in task creation,
    // UART binding, manager critical sections or the owner polling loop.
    owner=new(owner_memory) ProviderManagerTask(journal_startup().source(),{250,5000},1000);
    owner->start(wifi); // Terminal on task creation/binding failure; no retry.
}
void provider_journal_stop_owner()
{
    journal_startup().disable(); owner_attempted=true;
    if (owner) owner->request_stop();
}
#else
bool provider_journal_install(Flash&,uint64_t) { return false; }
void provider_journal_prepare(Flash&) {}
bool provider_journal_reserved_slot(Flash&,int) { return false; }
void provider_journal_start_owner(WiFi&) {}
void provider_journal_stop_owner() {}
#endif
