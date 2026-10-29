#include "provider_journal_startup.h"
#include "provider_native_startup.h"
#include "provider_flash_test_port.h"
#include "wifi.h"
#include "task.h"
#include "http_target.h"
#include "provider_control_test_port.h"
#include "https_installation.h"
#include "userinterface.h"
#include <stdio.h>
#include <string.h>
using namespace provider_control;
extern "C" void outbyte(int c) { fputc(c,stdout); }
unsigned manager_critical_depth;
static unsigned checks,failures,creates,deletes,notifications;
static bool create_ok=true;
static TaskFunction_t task_function;
static void *task_context;
static uint32_t ticks;
static std::function<void()> delay_hook=[]{};
struct TaskFinished {};
static void check(bool ok,const char *name) { ++checks; if (!ok) { ++failures; printf("FAIL %s\n",name); } }
BaseType_t xTaskCreate(TaskFunction_t function,const char*,unsigned,void *context,UBaseType_t,TaskHandle_t *handle) {
    ++creates; check(manager_critical_depth==0,"task created outside critical section");
    if (!create_ok) return pdFALSE;
    task_function=function; task_context=context; *handle=context; return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { ++deletes; if (!task) throw TaskFinished(); }
void xTaskNotifyGive(TaskHandle_t) { ++notifications; }
uint32_t ulTaskNotifyTake(BaseType_t,TickType_t) { return notifications ? 1 : 0; }
TickType_t xTaskGetTickCount() { return ticks; }
void vTaskDelay(TickType_t interval) { ticks+=interval; delay_hook(); }
static unsigned socket_calls;
extern "C" int __wrap_socket(int,int,int) { ++socket_calls; return -1; }
extern HttpTarget http_target;
static void unavailable_target() {
    const char *url="https://127.0.0.1/private";
    std::vector<uint8_t> header={6,HTTP_CMD_HEADER_CREATE,1};
    header.insert(header.end(),url,url+strlen(url));
    Message cmd={int(header.size()),true,header.data()},*data,*status;
    http_target.parse_command(&cmd,&data,&status);
    check(data->length==1,"production target creates the unchanged header handle");
    uint8_t exchange[]={6,HTTP_CMD_DO_EXCHANGE_RAW,data->length ? data->message[0] : uint8_t(0),255};
    http_target.finish_reply(true);
    cmd={4,true,exchange}; http_target.parse_command(&cmd,&data,&status);
    http_target.prepare_reply(&data,&status);
    check(status->length==23 && !memcmp(status->message,"503 SERVICE UNAVAILABLE",23) && !data->length,"production target fails closed while provider unavailable");
    http_target.finish_reply(true);
    uint8_t free_all[]={6,HTTP_CMD_FREE_ALL}; cmd={2,true,free_all};
    http_target.parse_command(&cmd,&data,&status); http_target.finish_reply(true);
    check(!socket_calls,"unavailable production HTTPS never opens plaintext socket");
}
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
    auto factory=provider_https_factory();
    check(factory && factory==provider_https_factory() && !factory->available() && !factory->create_connection({}),"stable factory unavailable before boot");
    unavailable_target();
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
    check(!factory->available() && !factory->create_connection({}),"launched owner is not yet available");
    if (good && !task_failure && !bind_failure) {
        ControlTestPort port; provider_session::Identity boot={}; boot.bytes[0]=1;
        Worker worker(port,boot,provider_session::Required,{250,5000,500});
        bool active=false,in_request=false;
        OperationCancellation token;
        delay_hook=[&] {
            if (in_request) return; // Deliberately stall owner delivery during the target probe.
            if (!wifi.uart.sent.empty()) {
                auto bytes=wifi.uart.sent.front(); wifi.uart.sent.erase(wifi.uart.sent.begin());
                check(worker.submit(bytes.data(),bytes.size()),"native owner control accepted by worker");
            }
            worker.step(ticks*5);
            while (!port.replies.empty()) {
                uint8_t bytes[FrameSize]; encode(port.replies.front(),bytes); port.replies.pop_front();
                portENTER_CRITICAL(); check(wifi.bound->receive_isr(bytes,sizeof(bytes)),"worker reply delivered through native UART"); portEXIT_CRITICAL();
            }
            if (factory->available() && !active) {
                active=true; token=factory->lifetime();
                CancellationSource cancel; auto cancelled=cancel.token(); cancel.signal();
                auto refused=factory->create_connection(cancelled);
                check(!refused,"native factory rejects cancelled caller"); delete refused;
                auto connection=factory->create_connection({});
                check(connection && !factory->create_connection({}),"native factory creates one owned connection");
                delete connection;
                connection=factory->create_connection({}); check(connection!=0,"unused connection releases slot"); delete connection;
                if (!strcmp(mode,"target")) {
                    uint32_t began=ticks;
                    in_request=true; unavailable_target(); in_request=false;
                    uint32_t elapsed=(ticks-began)*5;
                    check(elapsed>=30000 && elapsed<=31000,"production target uses native factory and bounds an unserviced request");
                    check(token.cancelled(),"unserviced production request retires its owner");
                }
                wifi.lifetime.signal();
            }
            if (ticks>100 && !active) { check(false,"native factory negotiation bounded"); provider_journal_stop_owner(); }
        };
        try { task_function(task_context); check(false,"owner task never returns"); } catch (const TaskFinished&) {}
        delay_hook=[]{};
        check(active && token.cancelled() && !factory->available() && !factory->create_connection({}),"remote restart revokes factory and captured lifetime");
    }
    auto lifetime=wifi.bound ? wifi.bound->data.lifetime() : OperationCancellation();
    provider_journal_stop_owner();
    if (wifi.bound) check(lifetime.cancelled(),"configuration erase fences bound owner lifetime first");
    provider_journal_start_owner(wifi);
    check(creates==unsigned(good),"stopped owner cannot restart under old identity");
    unavailable_target();
#else
    (void)mode;
    check(!provider_journal_install(flash,17),"default build has no installer");
    provider_journal_prepare(flash); provider_journal_start_owner(wifi); provider_journal_stop_owner();
    check(!provider_journal_reserved_slot(flash,0) && !provider_journal_reserved_slot(flash,1),"default configuration allocation unchanged");
    check(!flash.reads && !flash.writes && !flash.erases && !creates,"default build has no storage or task effects");
    check(!provider_https_factory() && !provider_journal_install_after_boot(flash,"123456789abcdef0"),"default build has no factory or operator installer");
    unavailable_target();
#endif
}
static void operator_case(const char *mode) {
#ifdef ULTIMATE_PROVIDER_JOURNAL
    MemoryFlash flash,other; WiFi wifi;
    const bool occupied=!strcmp(mode,"operator-occupied"),damaged=!strcmp(mode,"operator-damaged"),
        failed=!strcmp(mode,"operator-fault"),stopped=!strcmp(mode,"operator-stop"),changed=!strcmp(mode,"operator-changed");
    if (occupied) flash.memory[4096+255]=0;
    if (damaged) { FlashIncarnationJournal seed(flash,0,1,17); check(seed.provision(),"damaged seed"); flash.memory[4096]^=1; }
    check(!provider_journal_install_after_boot(flash,"123456789abcdef0"),"operator install refused before boot");
    provider_journal_prepare(flash); provider_journal_start_owner(wifi);
    unsigned writes=flash.writes;
    for (const char *bad : {static_cast<const char*>(0),"","0","123456789abcdef","123456789abcdef01","0000000000000000"," 23456789abcdef0","123456789abcdeg0"})
        check(!provider_journal_install_after_boot(flash,bad) && flash.writes==writes,"invalid fresh-domain input cannot write");
    check(!provider_journal_install_after_boot(other,"123456789abcdef0") && !other.writes,"foreign flash refused");
    if (stopped) provider_journal_stop_owner();
    if (changed) flash.memory[4096+4095]=0;
    if (failed) flash.fault=MemoryFlash::Torn;
    UserInterface ui; ui.input="123456789aBcDeF0";
    check(install_https_identity(0,&flash)==HttpsInstallationResult::Unavailable &&
        install_https_identity(&ui,0)==HttpsInstallationResult::Unavailable && !ui.prompts,"missing installation UI or flash has no prompts");
    ui.input_result=-1;
    check(install_https_identity(&ui,&flash)==HttpsInstallationResult::Cancelled && flash.writes==writes && ui.messages.empty(),"input cancellation never asks confirmation or writes");
    ui.input_result=1; ui.confirmation=BUTTON_NO;
    check(install_https_identity(&ui,&flash)==HttpsInstallationResult::Cancelled && flash.writes==writes,"declined confirmation never writes");
    ui.confirmation=BUTTON_YES;
    bool installed=install_https_identity(&ui,&flash)==HttpsInstallationResult::Installed;
    check(ui.messages.back()==(installed ? "Identity installed. Cold boot required." : "Not installed. Check ID and reserved storage."),"operator receives actual installation result");
    check(installed==!(occupied||damaged||failed||stopped||changed),"operator install only succeeds on reserved empty boot storage");
    check(!flash.erases,"operator installer never erases");
    unsigned after=flash.writes; flash.fault=MemoryFlash::None;
    check(!provider_journal_install_after_boot(flash,"123456789abcdef1") && flash.writes==after,"no overwrite or silent retry");
    provider_journal_prepare(flash); provider_journal_start_owner(wifi);
    check(!creates && !provider_https_factory()->available(),"successful install cannot activate until cold boot");
    if (installed) {
        JournalStartup reboot; provider_session::Identity id={};
        check(reboot.prepare(flash,0,1) && take(reboot,id) && epoch(id)==2 && id.bytes[0]==0xf0 && id.bytes[7]==0x12,"next boot reserves the installed identity");
    }
    unavailable_target();
#else
    (void)mode;
#endif
}
int main(int argc,char **argv) {
    if (argc==1) portable_cases();
    else if (!strncmp(argv[1],"operator-",9)) operator_case(argv[1]);
    else native_case(argv[1]);
    printf("Provider startup: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
