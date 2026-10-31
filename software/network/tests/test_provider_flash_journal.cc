#include "provider_flash_journal.h"
#include "provider_sntp_clock.h"
#include "flash.h"
#include <stdio.h>
#include <string.h>
#include <initializer_list>
using namespace provider_control;
static int checks, failures;
static void check(bool ok,const char *name) { ++checks; if (!ok) { ++failures; printf("FAIL %s\n",name); } }
#include "provider_flash_test_port.h"
static bool reserve(FlashIncarnationJournal& journal,uint64_t& value) { auto port=journal.journal(); return port.reserve(port.context,value); }
int main() {
    MemoryFlash flash; uint64_t value=99;
    FlashIncarnationJournal empty(flash,0,1,7);
    check(!reserve(empty,value) && value==0 && flash.writes==0,"empty journal is not recreated by reserve");
    FlashIncarnationJournal provision(flash,0,1,7);
    check(provision.provision(),"explicit erased-sector provisioning");
    check(flash.erases==0 && flash.writes==2,"provisioning never erases existing data");
    for (unsigned n=0;n<100;++n) {
        FlashIncarnationJournal boot(flash,0,1,7);
        check(reserve(boot,value) && value==n+2,"fresh epoch across reconstruction");
    }
    check(flash.erases==100,"one inactive sector erase per reservation");
    FlashIncarnationJournal duplicate(flash,0,1,7);
    check(!duplicate.provision(),"existing journal cannot be reprovisioned");
    for (unsigned f=MemoryFlash::EraseBefore;f<=MemoryFlash::Verify;++f) {
        MemoryFlash broken;
        FlashIncarnationJournal seed(broken,0,1,7); check(seed.provision(),"fault seed provisioned");
        broken.fault=MemoryFlash::Fault(f); broken.verify=false;
        FlashIncarnationJournal attempt(broken,0,1,7);
        check(!reserve(attempt,value) && value==0,"fault never publishes identity");
        broken.fault=MemoryFlash::None;
        check(!reserve(attempt,value),"failed instance cannot retry");
        FlashIncarnationJournal reboot(broken,0,1,7);
        bool ok=reserve(reboot,value);
        if (f==MemoryFlash::EraseBefore) check(ok && value==2,"unmodified journal resumes safely");
        else if (f==MemoryFlash::WriteAfter || f==MemoryFlash::Verify) check(ok && value==3,"ambiguous completed write burns epoch");
        else check(!ok && value==0,"erased or torn sector fails closed without stale fallback");
    }
    for (unsigned slot=0;slot<2;++slot) for (unsigned offset=0;offset<256;++offset) {
        MemoryFlash corrupt; FlashIncarnationJournal seed(corrupt,0,1,7); check(seed.provision(),"corruption seed provisioned");
        corrupt.memory[(slot+1)*4096+offset]^=1;
        FlashIncarnationJournal boot(corrupt,0,1,7);
        check(!reserve(boot,value) && corrupt.erases==0,"corrupt record never falls back or writes");
    }
    for (unsigned mode=0;mode<4;++mode) {
        MemoryFlash other; FlashIncarnationJournal invalid(other,0,mode==0 ? 0 : mode==1 ? 2 : 1,mode==2 ? 0 : 7);
        if (mode==3) other.memory[4096+512]=0;
        check(!invalid.provision() && other.writes==0 && other.erases==0,"invalid slots or occupied sectors remain untouched");
    }
    MemoryFlash checksum; FlashIncarnationJournal checksum_seed(checksum,0,1,7);
    check(checksum_seed.provision(),"checksum seed provisioned");
    checksum.memory[4096+16]=2; checksum.memory[8192+16]=3;
    FlashIncarnationJournal checksum_boot(checksum,0,1,7);
    check(!reserve(checksum_boot,value) && !checksum.erases,"CRC rejects altered but consecutive epochs");
    FlashIncarnationJournal foreign(flash,0,1,8); check(!reserve(foreign,value),"wrong domain rejected");
    for (unsigned mode=1;mode<=11;++mode) {
        MemoryFlash geometry; geometry.geometry_fault=mode;
        FlashIncarnationJournal journal(geometry,0,1,7);
        if (mode==11) {
            check(journal.provision() && reserve(journal,value) && value==2,"configuration slots use physical end inside larger image");
            check(geometry.memory[0]==0xff,"unused leading image sector preserved");
        } else check(!journal.provision() && !geometry.erases && !geometry.writes,"unsupported geometry remains untouched");
    }
    bool neighbors=true;
    for (unsigned i=0;i<4096;++i) neighbors &= flash.memory[i]==0xff && flash.memory[12288+i]==0xff;
    check(neighbors,"neighbor sectors unchanged");
    const int64_t utc=1800000000;
    for (int delta : {-301,-300,0,300,301}) {
        provider_data::SntpClock clock;
        check(clock.accept(utc,0,0),"first bounded SNTP sample accepted");
        bool ok=clock.accept(utc+3600+delta,999999,3600000);
        check(ok==(delta>=-300 && delta<=300),"SNTP jump threshold exact");
        if (!ok) check(!clock.accept(utc+3600,0,3600001),"clock discontinuity remains latched");
    }
    for (unsigned mode=0;mode<6;++mode) {
        provider_data::SntpClock clock;
        check(clock.accept(utc,0,1000),"clock seed accepted");
        check(!clock.accept(mode==0 ? 1 : mode==1 ? INT64_MAX : utc,
            mode==2 ? -1 : mode==3 ? 1000000 : 0,mode==4 ? 999 : mode==5 ? 86401001 : 1000),"invalid SNTP update rejected");
    }
    printf("Provider flash sources: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
