#include "provider_journal_startup.h"
#include "provider_native_startup.h"
#include "provider_flash_test_port.h"
#include "wifi.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
using namespace provider_control;
unsigned manager_critical_depth;
static unsigned checks,failures,creates,deletes,notifications;
static bool create_ok=true;
static void check(bool ok,const char *name) { ++checks; if (!ok) { ++failures; printf("FAIL %s\n",name); } }
BaseType_t xTaskCreate(TaskFunction_t,const char*,unsigned,void *context,UBaseType_t,TaskHandle_t *handle) {
    ++creates; check(manager_critical_depth==0,"task created outside critical section");
    if (!create_ok) return pdFALSE;
    *handle=context; return pdPASS;
}
void vTaskDelete(TaskHandle_t) { ++deletes; }
void xTaskNotifyGive(TaskHandle_t) { ++notifications; }
uint32_t ulTaskNotifyTake(BaseType_t,TickType_t) { return 0; }
TickType_t xTaskGetTickCount() { return 0; }
void vTaskDelay(TickType_t) {}
static bool take(JournalStartup& startup,provider_session::Identity& id) {
    auto source=startup.source(); return source.fresh(source.context,id);
}
static uint64_t epoch(const provider_session::Identity& id) {
    uint64_t n=0; for (unsigned i=0;i<8;++i) n|=uint64_t(id.bytes[i+8])<<(8*i); return n;
}
static void portable_cases() {
    MemoryFlash flash; JournalStartup installer;
    check(installer.install(flash,0,1,17),"explicit installer writes erased slots");
    check(!installer.install(flash,0,1,18),"installer cannot overwrite its own domain");
    check(installer.prepare(flash,0,1) && installer.state()==JournalStartup::Prepared,"install then prepare reserves an epoch");
    check(!installer.prepare(flash,0,1),"repeated preparation cannot reserve again");
    unsigned reads=flash.reads,writes=flash.writes,erases=flash.erases;
    provider_session::Identity id={};
    check(take(installer,id) && id.bytes[0]==17 && epoch(id)==2,"prepared identity includes installed domain and committed epoch");
    check(!take(installer,id) && !id.valid(),"prepared identity consumed once and failed output cleared");
    check(reads==flash.reads && writes==flash.writes && erases==flash.erases,"identity handoff performs no flash I/O");
    for (unsigned n=0;n<50;++n) {
        JournalStartup reboot;
        check(reboot.prepare(flash,0,1) && take(reboot,id) && epoch(id)==n+3,"boot reconstructs a strictly newer identity");
    }
    JournalStartup stopped; check(stopped.prepare(flash,0,1),"identity reserved before stop");
    stopped.disable(); check(!take(stopped,id) && !id.valid(),"stop burns the prepared identity");
    JournalStartup next; check(next.prepare(flash,0,1) && take(next,id) && epoch(id)==54,"next boot never reuses burned identity");
    for (unsigned mode=0;mode<6;++mode) {
        MemoryFlash bad; JournalStartup boot;
        if (mode!=0) { FlashIncarnationJournal seed(bad,0,1,17); check(seed.provision(),"invalid boot seed"); }
        if (mode==1) bad.memory[8192+8]^=1;
        if (mode==2) bad.memory[4096+16]^=2;
        if (mode==3) bad.fault=MemoryFlash::EraseAfter;
        if (mode==4) bad.fault=MemoryFlash::WriteAfter;
        if (mode==5) bad.geometry_fault=4;
        check(!boot.prepare(bad,0,1) && boot.state()==JournalStartup::Failed,"invalid storage never prepares identity");
        unsigned count=bad.writes;
        bad.fault=MemoryFlash::None;
        check(!boot.install(bad,0,1,18) && !boot.prepare(bad,0,1) && !take(boot,id) && !id.valid() && bad.writes==count,"failed boot neither repairs nor retries");
    }
    MemoryFlash occupied; occupied.memory[4096]=0x55;
    JournalStartup conflict;
    check(!conflict.install(occupied,0,1,17) && !occupied.writes && !occupied.erases,"installer preserves existing settings");
    MemoryFlash zero; JournalStartup invalid;
    check(!invalid.install(zero,0,1,0) && !zero.writes,"zero domain cannot install");
    MemoryFlash cleared; FlashIncarnationJournal seed(cleared,0,1,17); check(seed.provision(),"reset seed");
    memset(cleared.memory,0xff,sizeof(cleared.memory));
    JournalStartup after_reset; check(!after_reset.prepare(cleared,0,1),"settings reset cannot recreate a domain");
    MemoryFlash mixed,foreign;
    FlashIncarnationJournal left(mixed,0,1,17),right(foreign,0,1,18);
    check(left.provision() && right.provision(),"distinct valid domain records seeded");
    memcpy(mixed.memory+8192,foreign.memory+8192,256);
    uint64_t domain=99; unsigned before=mixed.writes;
    check(!FlashIncarnationJournal::installed_domain(mixed,0,1,domain) && !domain && mixed.writes==before,"discovery rejects valid but mixed domains without writes");
    memcpy(mixed.memory+8192,mixed.memory+4096,256);
    check(!FlashIncarnationJournal::installed_domain(mixed,0,1,domain) && !domain,"discovery rejects duplicate counter records");
    struct InstallFault : MemoryFlash {
        unsigned fail_at; Fault injected;
        InstallFault(unsigned at,Fault f) : fail_at(at),injected(f) {}
        bool write_page(int page,const void *data) override {
            if (writes==fail_at) fault=injected;
            return MemoryFlash::write_page(page,data);
        }
    };
    for (unsigned at=0;at<2;++at) for (unsigned f=MemoryFlash::WriteBefore;f<=MemoryFlash::Verify;++f) {
        InstallFault partial(at,MemoryFlash::Fault(f)); JournalStartup action;
        check(!action.install(partial,0,1,17),"interrupted installer reports failure");
        unsigned count=partial.writes; partial.fault=MemoryFlash::None;
        check(!action.install(partial,0,1,18) && !action.prepare(partial,0,1) && !take(action,id) && !id.valid() && count==partial.writes,"failed installer cannot activate or overwrite under another domain");
        JournalStartup reboot;
        bool complete=at==1 && (f==MemoryFlash::WriteAfter || f==MemoryFlash::Verify);
        check(reboot.prepare(partial,0,1)==complete,"only two completely verified records permit later boot");
    }
}
static void native_case(const char *mode) {
    MemoryFlash flash,other; WiFi wifi;
#ifdef ULTIMATE_PROVIDER_JOURNAL
    check(provider_journal_reserved_slot(flash,0) && provider_journal_reserved_slot(flash,1) &&
        !provider_journal_reserved_slot(flash,-1) && !provider_journal_reserved_slot(flash,2),"only final two configuration slots reserved");
    bool missing=!strcmp(mode,"missing"),late=!strcmp(mode,"late"),early=!strcmp(mode,"early"),
        corrupt=!strcmp(mode,"corrupt"),foreign=!strcmp(mode,"foreign"),
        task_failure=!strcmp(mode,"task"),bind_failure=!strcmp(mode,"bind"),install=!strcmp(mode,"install");
    if (install || foreign) check(provider_journal_install(flash,17) && !provider_journal_install(flash,18),"native installer is explicit and one shot");
    else if (!missing && !late) { FlashIncarnationJournal seed(flash,0,1,17); check(seed.provision(),"prior boot storage seeded"); }
    if (foreign) { FlashIncarnationJournal seed(other,0,1,18); check(seed.provision(),"foreign flash contains a valid but different journal"); }
    if (early) provider_journal_start_owner(wifi);
    if (corrupt) flash.memory[8192+8]^=1;
    provider_journal_prepare(foreign ? other : flash);
    check(!provider_journal_install(flash,18),"installation forbidden after configuration startup");
    unsigned reads=flash.reads,writes=flash.writes,erases=flash.erases;
    provider_journal_prepare(flash);
    check(reads==flash.reads && writes==flash.writes && erases==flash.erases,"repeat startup never accesses storage");
    create_ok=!task_failure; wifi.bind_ok=!bind_failure;
    provider_journal_start_owner(wifi); provider_journal_start_owner(wifi);
    bool good=!(missing||late||early||corrupt||foreign);
    check(creates==unsigned(good),"owner created only for prepared storage and at most once");
    check(notifications==unsigned(good && !task_failure && !bind_failure),"only successfully bound owner is awakened");
    check(deletes==unsigned(good && bind_failure),"binding failure deletes unstarted task");
    check(reads==flash.reads && writes==flash.writes && erases==flash.erases,"owner construction and binding do not touch flash");
    auto lifetime=wifi.bound ? wifi.bound->data.lifetime() : OperationCancellation();
    provider_journal_stop_owner();
    if (wifi.bound) check(lifetime.cancelled(),"configuration erase fences bound owner lifetime first");
    provider_journal_start_owner(wifi);
    check(creates==unsigned(good),"stopped owner cannot restart under old identity");
#else
    (void)mode;
    check(!provider_journal_install(flash,17),"default build has no installer");
    provider_journal_prepare(flash); provider_journal_start_owner(wifi); provider_journal_stop_owner();
    check(!provider_journal_reserved_slot(flash,0) && !provider_journal_reserved_slot(flash,1),"default configuration allocation unchanged");
    check(!flash.reads && !flash.writes && !flash.erases && !creates,"default build has no storage or task effects");
#endif
}
int main(int argc,char **argv) {
    if (argc==1) portable_cases(); else native_case(argv[1]);
    printf("Provider startup: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
