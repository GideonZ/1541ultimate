#include "provider_journal_startup.h"
namespace provider_control {
bool JournalStartup::install(Flash& flash,unsigned a,unsigned b,uint64_t domain)
{
    if (state_!=Cold) return false;
    disable();
    FlashIncarnationJournal journal(flash,a,b,domain);
    if (!journal.provision()) return false;
    state_=Installed;
    return true;
}
bool JournalStartup::prepare(Flash& flash,unsigned a,unsigned b)
{
    if (state_!=Cold && state_!=Installed) return false;
    disable();
    uint64_t domain=0;
    if (!FlashIncarnationJournal::installed_domain(flash,a,b,domain)) return false;
    FlashIncarnationJournal journal(flash,a,b,domain);
    DurableIncarnation incarnation(domain,journal.journal());
    IncarnationSource input=incarnation.source();
    if (!input.fresh(input.context,identity_)) return false;
    state_=Prepared;
    return true;
}
bool JournalStartup::take(void *context,provider_session::Identity& value)
{
    auto& self=*static_cast<JournalStartup*>(context);
    value={};
    if (self.state_!=Prepared) return false;
    value=self.identity_;
    self.identity_={};
    self.state_=Consumed;
    return true;
}
}
