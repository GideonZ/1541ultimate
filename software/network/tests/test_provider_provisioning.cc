#include "provider_incarnation.h"
#include "provider_tls_policy.h"
#include "provider_control_test_port.h"
#include <stdio.h>
#include <mutex>
#include <thread>
using namespace provider_control;
using namespace provider_session;
using namespace provider_data;
static int checks, failures;
static void expect(bool value, const char *label) {
    ++checks; if (!value) { ++failures; printf("FAIL %s\n", label); }
}
struct Journal {
    uint64_t committed = 0;
    unsigned calls = 0;
    bool fail = false, repeat = false;
    static bool reserve(void *p, uint64_t& value) {
        auto& s = *static_cast<Journal*>(p); ++s.calls;
        if (s.committed == UINT64_MAX) return false;
        if (!s.repeat) ++s.committed;
        value = s.committed;
        return !s.fail; // Failure after commit models an ambiguous acknowledgement.
    }
    IncarnationJournal port() { return {this,reserve}; }
};
static Identity next(DurableIncarnation& source, bool good = true) {
    Identity id; memset(&id,0xa5,sizeof(id));
    auto input = source.source();
    expect(input.fresh(input.context,id) == good,"reservation outcome");
    expect(id.valid() == good,"failed reservation clears identity");
    return id;
}
static void identities() {
    Journal journal;
    DurableIncarnation missing(1,{0,0}), no_domain(0,journal.port());
    next(missing,false); next(no_domain,false);
    expect(journal.calls==0,"invalid configuration does not reserve");
    Identity previous = {};
    for (unsigned restart=0; restart<32; ++restart) {
        DurableIncarnation source(0x8877665544332211ULL,journal.port());
        Identity id=next(source);
        expect(!(id==previous),"reconstructed manager has fresh durable epoch");
        expect(id.bytes[0]==0x11 && id.bytes[7]==0x88 && id.bytes[8]==restart+1,"stable domain and epoch encoding");
        previous=id;
    }
    DurableIncarnation source(1,journal.port());
    journal.fail=true; next(source,false);
    unsigned calls=journal.calls; journal.fail=false; next(source,false);
    expect(journal.calls==calls,"ambiguous reservation permanently disables instance");
    DurableIncarnation after_crash(1,journal.port()); next(after_crash);
    journal.repeat=true; next(after_crash,false);
    journal.repeat=false; next(after_crash,false);
    Journal zero; zero.repeat=true; DurableIncarnation invalid(1,zero.port()); next(invalid,false);
    Journal end; end.committed=UINT64_MAX-1; DurableIncarnation exhausted(1,end.port());
    next(exhausted); next(exhausted,false);
    expect(end.calls==1,"exhaustion cannot wrap or reserve again");

    Journal persistent; Identity boot={}; boot.bytes[0]=9;
    ControlTestPort port; Worker remote(port,boot,Required,{100,200,50});
    DurableIncarnation first(11,persistent.port()); Manager old(first.source(),{100,200});
    uint8_t bytes[FrameSize];
    expect(old.offer(boot,{},0,bytes) && remote.submit(bytes,sizeof(bytes)),"old manager offer accepted");
    remote.step(0); Frame stale=port.replies.front(); port.replies.pop_front();
    DurableIncarnation second(11,persistent.port()); Manager current(second.source(),{100,200});
    expect(current.offer(boot,{},1,bytes) && remote.submit(bytes,sizeof(bytes)),"new manager offer accepted");
    remote.step(1); Frame fresh=port.replies.front(); port.replies.pop_front();
    encode(stale,bytes); expect(!current.receive(bytes,sizeof(bytes),2),"pre-restart reply rejected with same provider boot");
    encode(fresh,bytes); expect(current.receive(bytes,sizeof(bytes),2),"new incarnation reply accepted");
}
struct Lock : PolicyLock {
    std::mutex mutex;
    void lock() override { mutex.lock(); }
    void unlock() override { mutex.unlock(); }
};
static const int64_t utc=1800000000;
static void policies() {
    Lock lock; TlsPolicy policy(lock);
    expect(!policy.ready(policy.generation(),0,utc),"default policy unavailable");
    expect(policy.qualify_clock(utc,100,1000,0),"qualified clock accepted");
    expect(!policy.ready(policy.generation(),100,utc),"clock alone insufficient");
    policy.entropy_available(true); uint32_t token=policy.generation();
    expect(policy.ready(token,100,utc) && policy.ready(token,1099,utc),"qualified interval including final millisecond");
    expect(!policy.ready(token,1100,utc+1),"exact clock expiry rejected");
    expect(!policy.ready(policy.generation(),100,utc),"expired clock cannot resurrect by rollback");
    expect(policy.qualify_clock(utc,2000,5000,1),"clock requalification accepted");
    expect(!policy.ready(token,2000,utc),"old session remains retired");
    token=policy.generation();
    expect(policy.ready(token,3000,utc+2),"bounded clock tolerance");
    expect(!policy.ready(token,3001,utc+3),"forward time discontinuity rejected");
    expect(!policy.ready(token,3002,utc+1),"clock returning to range does not restore session");
    policy.qualify_clock(utc,4000,5000,0); token=policy.generation();
    expect(policy.ready(token,4000,utc),"new qualification ready");
    policy.entropy_available(false); policy.entropy_available(true);
    expect(!policy.ready(token,4000,utc),"entropy revoke and restore between polls retires old session");
    token=policy.generation(); expect(policy.ready(token,4000,utc),"new session uses restored entropy");
    expect(!policy.ready(token,3999,utc),"monotonic regression rejected");
    policy.qualify_clock(utc,0,5000,0); token=policy.generation();
    expect(policy.ready(token,500,utc),"clock advances within qualification");
    expect(!policy.ready(token,400,utc),"rollback after anchor rejected independently of expiry");
    policy.qualify_clock(utc,0,5000,0); token=policy.generation();
    expect(!policy.ready(token,0,utc-1),"backward UTC jump rejected");
    for (unsigned kind=0; kind<5; ++kind) {
        expect(!policy.qualify_clock(kind==0 ? 1 : kind==1 ? INT64_MAX : utc,0,
            kind==2 ? 0 : kind==3 ? 86400001 : 1000,kind==4 ? 61 : 0),"invalid clock qualification rejected");
        expect(!policy.ready(policy.generation(),0,utc),"invalid qualification stays closed");
    }
    policy.qualify_clock(utc,UINT64_MAX-10,10,0); token=policy.generation();
    expect(policy.ready(token,UINT64_MAX-1,utc),"monotonic upper bound without addition overflow");
    expect(!policy.ready(token,UINT64_MAX,utc),"expiry at monotonic upper bound");
    policy.qualify_clock(utc,0,5000,0); token=policy.generation();
    std::thread producer([&] { for (unsigned i=0;i<1000;++i) { policy.entropy_available(false); policy.entropy_available(true); } });
    for (unsigned i=0;i<1000;++i) policy.ready(token,0,utc);
    producer.join();
    expect(!policy.ready(token,0,utc),"concurrent lifecycle changes retire captured generation");
    policy.revoke(); expect(!policy.ready(policy.generation(),0,utc),"explicit revoke clears both qualifications");
}
int main() {
    identities(); policies();
    printf("Provider provisioning: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
