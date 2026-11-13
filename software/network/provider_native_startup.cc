#include "provider_native_startup.h"
#ifdef ULTIMATE_PROVIDER_JOURNAL
#include "provider_journal_startup.h"
#include "provider_manager_task.h"
#include "wifi.h"
#include "flash.h"
#include <new>
namespace {
provider_control::JournalStartup& journal_startup() {
    static provider_control::JournalStartup instance;
    return instance;
}
Flash *storage;
bool configuration_started, owner_attempted, installable;
ProviderManagerTask *owner;
alignas(ProviderManagerTask) uint8_t owner_memory[sizeof(ProviderManagerTask)];
alignas(OwnerHttpProvider) uint8_t provider_memory[sizeof(OwnerHttpProvider)];
OwnerHttpProvider *published_provider;
class NativeHttpsFactory : public HttpSecureProvider {
    OwnerHttpProvider *get() const { return __atomic_load_n(&published_provider,__ATOMIC_ACQUIRE); }
public:
    bool available() const override { auto p=get(); return p && p->available(); }
    OperationCancellation lifetime() const override { auto p=get(); return p ? p->lifetime() : OperationCancellation(); }
    HttpConnection *create_connection(const OperationCancellation& token) override {
        auto p=get(); return p ? p->create_connection(token) : 0;
    }
};
bool slots(Flash& flash,unsigned& a,unsigned& b) {
    int count=flash.get_number_of_config_pages();
    if (count<2) return false;
    a=unsigned(count-2); b=unsigned(count-1); return true;
}
}
HttpSecureProvider *provider_https_factory()
{
    static NativeHttpsFactory factory;
    return &factory;
}
bool provider_journal_install_after_boot(Flash& flash,const char *hex)
{
    // Parse the whole operator-supplied value before consuming the one attempt.
    uint64_t domain=0;
    if (!hex) return false;
    for (unsigned i=0;i<16;++i) {
        unsigned char c=hex[i]; unsigned digit;
        if (c>='0' && c<='9') digit=c-'0';
        else if (c>='a' && c<='f') digit=c-'a'+10;
        else if (c>='A' && c<='F') digit=c-'A'+10;
        else return false;
        domain=(domain<<4)|digit;
    }
    if (hex[16] || !domain || !configuration_started || storage!=&flash || owner || !installable) return false;
    installable=false;
    journal_startup().disable(); // Never reactivate the failed boot identity.
    unsigned a,b;
    if (!slots(flash,a,b)) return false;
    provider_control::FlashIncarnationJournal journal(flash,a,b,domain);
    return journal.provision(); // Rechecks every byte; no erase or retry.
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
    if (!journal_startup().prepare(flash,a,b))
        installable=provider_control::FlashIncarnationJournal::erased(flash,a,b);
    // No install or repair on ordinary boot. Only initially empty slots may be
    // installed from the UI; occupied slots may have live configuration owners.
}
bool provider_journal_reserved_slot(Flash& flash,int slot)
{
    unsigned a,b;
    return slot>=0 && slots(flash,a,b) && (unsigned(slot)==a || unsigned(slot)==b);
}
void provider_journal_start_owner(WiFi& wifi)
{
    if (owner_attempted || !wifi.ProviderTransportReady()) return;
    owner_attempted=true;
    if (!configuration_started || journal_startup().state()!=provider_control::JournalStartup::Prepared) return;
    // Construction consumes a prepared identity; no flash I/O in task creation,
    // UART binding, manager critical sections or the owner polling loop.
    owner=new(owner_memory) ProviderManagerTask(journal_startup().source(),{250,5000},1000);
    if (owner->start(wifi)) {
        auto provider=new(provider_memory) OwnerHttpProvider(*owner,30000,500);
        __atomic_store_n(&published_provider,provider,__ATOMIC_RELEASE);
    } // Publication is not readiness: negotiation/security gates still apply.
}
void provider_journal_stop_owner()
{
    journal_startup().disable(); owner_attempted=true; installable=false;
    if (owner) owner->request_stop();
}
#else
bool provider_journal_install(Flash&,uint64_t) { return false; }
bool provider_journal_install_after_boot(Flash&,const char*) { return false; }
HttpSecureProvider *provider_https_factory() { return 0; }
void provider_journal_prepare(Flash&) {}
bool provider_journal_reserved_slot(Flash&,int) { return false; }
void provider_journal_start_owner(WiFi&) {}
void provider_journal_stop_owner() {}
#endif
