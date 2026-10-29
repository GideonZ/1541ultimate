#include "owner_http_provider.h"
#include "provider_data_client.h"
#include <stdio.h>
#include <string.h>
#include <initializer_list>
static bool fail_allocation;
static unsigned allocations,releases;
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void*);
extern "C" void *__wrap_malloc(size_t size) {
    if (fail_allocation) return 0;
    void *p=__real_malloc(size); if (p) ++allocations; return p;
}
extern "C" void __wrap_free(void *p) { if (p) ++releases; __real_free(p); }
static unsigned checks,failures;
static void check(bool ok,const char *label) { ++checks; if (!ok) { ++failures; printf("FAIL %s\n",label); } }
struct Endpoint : HttpOwnerEndpoint {
    uint32_t now = 0;
    CancellationSource boot,command;
    provider_data::Frame pending = {};
    bool active = true,blocked = false,queued = false,reject = false,oversize = false,restart = false;
    unsigned submits = 0,retires = 0,closes = 0;
    bool ready() const override { return active; }
    OperationCancellation lifetime() const override { return boot.token(); }
    bool submit(const provider_data::Frame& f,OperationCancellation) override {
        ++submits;
        if (reject || queued) return false;
        pending = f; queued = true; if (f.opcode == provider_data::Client::Close) ++closes;
        return true;
    }
    bool collect(provider_data::Frame& f) override {
        if (!queued || blocked) return false;
        f = pending; f.status = provider_data::Ok; f.reply = true;
        if (f.opcode == provider_data::Read) { memset(f.payload,0x42,sizeof(f.payload)); f.length = f.count; }
        if (oversize) f.count = 257;
        if (restart) boot.signal();
        queued = false; return true;
    }
    uint32_t now_ms() const override { return now; }
    void wait() override { ++now; }
    void retire() override { active = false; ++retires; }
    void lock() override {}
    void unlock() override {}
};
int main() {
    {
        Endpoint endpoint; OwnerHttpProvider provider(endpoint,100,20);
        fail_allocation=true;
        check(!provider.create_connection({}) && !endpoint.submits,"allocation failure returns unavailable without owner request");
        fail_allocation=false;
        auto connection=provider.create_connection({});
        check(connection!=0,"allocation failure releases the occupied slot"); delete connection;
        check(allocations==releases,"connection uses matching allocation and release");
    }
    {
        Endpoint endpoint; OwnerHttpProvider provider(endpoint,100,20);
        HttpConnection *a = provider.create_connection({}); check(a != 0,"factory ready");
        check(!provider.create_connection({}),"second factory cannot steal mailbox");
        check(a->open("example.test",443) == 0,"open via owner");
        check(endpoint.pending.length == 13 && !memcmp(endpoint.pending.payload,"example.test",13),"hostname copied");
        unsigned char data[600]; memset(data,0x19,sizeof(data));
        check(a->write(data,sizeof(data)) == 256 && endpoint.pending.count == 256,"bounded partial write");
        check(a->read(data,sizeof(data)) == 256 && data[255] == 0x42 && data[256] == 0x19,"bounded read publishes only count");
        a->close_session(); a->close_session(); delete a;
        check(endpoint.closes == 1 && !endpoint.retires,"close once");
        a = provider.create_connection({}); check(a != 0,"factory lease released");
        a->close_session(); check(a->open("example.test",443) < 0,"close before open is terminal"); delete a;
    }
    {
        Endpoint endpoint; OwnerHttpProvider provider(endpoint,100,20);
        auto token = endpoint.command.token(); endpoint.command.signal();
        check(!provider.create_connection(token),"cancel before factory");
        endpoint.active = false; check(!provider.create_connection({}),"unavailable owner");
    }
    for (bool wrapping : {false,true}) {
        Endpoint endpoint; endpoint.blocked = true; endpoint.now = wrapping ? UINT32_MAX-30 : 0;
        uint32_t begin = endpoint.now; OwnerHttpProvider provider(endpoint,100,20);
        HttpConnection *a = provider.create_connection({}); check(a->open("example.test",443) < 0,"stalled owner bounded");
        check(uint32_t(endpoint.now-begin) == 120 && endpoint.retires == 1,"exchange plus cleanup limit including wrap");
        delete a;
    }
    {
        Endpoint endpoint; OwnerHttpProvider provider(endpoint,100,20);
        HttpConnection *a = provider.create_connection({}); check(a->open("example.test",443) == 0,"open before restart");
        endpoint.restart = true; unsigned char bytes[4] = {1,2,3,4};
        check(a->read(bytes,4) < 0 && bytes[0] == 1 && bytes[3] == 4,"restart at collect suppresses bytes"); delete a;
    }
    {
        Endpoint endpoint; OwnerHttpProvider provider(endpoint,100,20);
        HttpConnection *a = provider.create_connection({}); check(a->open("example.test",443) == 0,"open before malformed reply");
        endpoint.oversize = true; unsigned char bytes[4] = {1,2,3,4};
        check(a->read(bytes,4) < 0 && bytes[0] == 1 && endpoint.retires,"oversized reply quarantined"); delete a;
    }
    {
        Endpoint endpoint; OwnerHttpProvider provider(endpoint,100,20);
        HttpConnection *a = provider.create_connection({}); check(a->open("example.test",443) == 0,"open before deadline");
        endpoint.now = 100; unsigned char byte = 7;
        check(a->read(&byte,1) < 0 && byte == 7,"budget never restarts at read"); delete a;
    }
    check(allocations==releases,"all connection storage released");
    printf("Owner HTTP: %u checks, %u failures\n",checks,failures); return failures ? 1 : 0;
}
