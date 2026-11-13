#include "provider_data_client.h"
#include "provider_control_test_port.h"
#include <stdio.h>
#include <functional>
using namespace provider_data;
using provider_session::Identity;
static unsigned checks,failures;
static void expect(bool b,const char *s) { ++checks; if (!b) { ++failures; printf("FAIL %s\n",s); } }
static Identity id(unsigned n) { Identity i = {}; i.bytes[0] = n; return i; }
static Frame request(unsigned op=Open) {
    Frame f = {}; f.opcode = op; f.request = 1; f.ticket = {{{id(1),1},id(2)},1}; f.budget_ms = 100;
    if (op == Open) { f.port = 443; f.length = 5; memcpy(f.payload,"host",5); }
    else { f.count = 3; if (op == Write) { f.length = 3; memcpy(f.payload,"abc",3); } }
    return f;
}
struct TestPort : Port {
    std::mutex mutex;
    std::vector<std::vector<uint8_t>> sent;
    bool transmit = true;
    std::function<void()> lock_hook = []{};
    std::function<void()> unlock_hook = []{};
    void lock() override { mutex.lock(); lock_hook(); }
    void unlock() override { mutex.unlock(); unlock_hook(); }
    bool send(const uint8_t *p,size_t n) override { if (!transmit) return false; sent.emplace_back(p,p+n); return true; }
};
static void codec() {
    Frame f = request(),out = {}; uint8_t bytes[FrameSize];
    expect(encode(f,bytes) && decode(bytes,sizeof(bytes),out),"canonical roundtrip");
    expect(same_ticket(f.ticket,out.ticket) && out.length == 5,"wire identity and payload");
    for (unsigned n = 0; n < FrameSize; ++n) expect(!decode(bytes,n,out),"all truncations rejected");
    expect(!decode(bytes,FrameSize+1,out) && !decode(NULL,FrameSize,out),"overlong and null rejected");
    for (unsigned offset : {0u,1u,2u,3u,4u,8u,78u,79u,85u,335u}) {
        bytes[offset] ^= 0x80; expect(!decode(bytes,sizeof(bytes),out),"bad envelope or padding rejected"); bytes[offset] ^= 0x80;
    }
    for (unsigned op : {unsigned(Read),unsigned(Write)}) {
        f = request(op); expect(encode(f,bytes) && decode(bytes,sizeof(bytes),out),"data operation roundtrip");
        f.reply = true; f.status = Ok; f.budget_ms = 0; f.length = op == Read ? f.count : 0;
        expect(encode(f,bytes) && decode(bytes,sizeof(bytes),out),"success reply roundtrip");
        f.status = Failed; expect(!encode(f,bytes),"error payload rejected");
        f.count = f.length = 0; expect(encode(f,bytes),"empty failure reply");
    }
    f = request(); f.payload[1] = 0; expect(!encode(f,bytes),"embedded host NUL rejected");
    f = request(); f.length = 257; expect(!encode(f,bytes),"oversize payload rejected");
    f = request(); f.request = 0; expect(!encode(f,bytes),"zero request rejected");
    f = request(); f.budget_ms = 0; expect(!encode(f,bytes),"zero budget rejected");
    f = request(); f.budget_ms = 0x80000000; expect(!encode(f,bytes),"ambiguous budget rejected");
    f = request(); f.ticket.session = 0; expect(!encode(f,bytes),"zero session rejected");
}
static void mailbox() {
    TestPort p; Delivery d(p,false); uint8_t bytes[FrameSize]; Frame f = request(); encode(f,bytes);
    auto generation = d.lifetime();
    expect(d.receive_isr(bytes,sizeof(bytes),15),"mailbox admission");
    memset(bytes,0xcc,sizeof(bytes)); Event e;
    expect(d.take(e) && e.frame.payload[0] == 'h' && e.arrived_ms == 15,"mailbox copies input");
    Event other; expect(!d.take(other),"only one owner take");
    encode(f,bytes); expect(!d.receive_isr(bytes,sizeof(bytes),16) && generation.cancelled(),"overflow while executing invalidates");
    expect(!d.send(f,e.generation) && p.sent.empty(),"late result fenced");
    expect(d.receive_isr(bytes,sizeof(bytes),17),"new generation admitted");
    d.finish(e.generation); expect(d.take(other),"old finish cannot erase new request");
    d.finish(other.generation); expect(!d.take(e),"finish frees slot");
    expect(d.receive_isr(bytes,sizeof(bytes)) && d.take(e),"completion test takes request");
    Frame reply=f; reply.reply=true; reply.status=Ok; reply.budget_ms=reply.port=reply.length=0;
    CancellationSource cancel; auto cancelled=cancel.token(); cancel.signal();
    expect(!d.complete(reply,e.generation,cancelled) && p.sent.empty(),"cancelled completion publishes nothing");
    expect(d.receive_isr(bytes,sizeof(bytes)) && d.take(e),"cancelled completion releases its slot");
    expect(d.complete(reply,e.generation) && p.sent.size()==1,"live completion publishes once");
    expect(!d.complete(reply,e.generation) && p.sent.size()==1,"duplicate completion rejected");
    generation = d.lifetime(); p.transmit = false;
    expect(!d.send(f,generation) && generation.cancelled(),"failed send retires generation");
    p.transmit = true; generation = d.lifetime(); bytes[0] = 0;
    expect(!d.receive_isr(bytes,sizeof(bytes)) && generation.cancelled(),"malformed packet retires generation");
}
struct Resource : provider_session::Resource {
    unsigned& releases;
    explicit Resource(unsigned& n):releases(n) {}
    void cancel() override {}
    bool cleanup() override { return true; }
    ~Resource() { ++releases; }
};
static provider_session::Grant initialize(provider_control::Worker& worker,ControlTestPort& port) {
    provider_control::Frame f = {}; f.opcode = provider_control::Offer; f.request = 1; f.grant.owner = id(2);
    expect(control_submit(worker,f),"offer queued"); worker.step(0);
    f.grant = port.replies.front().grant; port.replies.pop_front(); f.opcode = provider_control::Initialize; f.request = 2; f.required = provider_session::Required;
    expect(control_submit(worker,f),"initialize queued"); worker.step(0); port.replies.pop_front(); return f.grant;
}
struct TestBackend : provider_data::Backend {
    provider_control::Worker& worker;
    unsigned calls = 0,releases = 0; uint64_t now = 0; uint32_t budget = 0;
    std::function<void()> hook = []{};
    explicit TestBackend(provider_control::Worker& w):worker(w) {}
    uint64_t now_ms() override { return now; }
    int execute(Frame& f) override {
        ++calls; budget = f.budget_ms;
        if (f.opcode == Open) {
            auto *resource = new Resource(releases); provider_session::Ticket ticket;
            if (!worker.admit(f.ticket.grant,f.ticket.session,resource,now,ticket)) { delete resource; return -1; }
        }
        hook();
        return f.opcode == Open ? 0 : f.count;
    }
};
static void owner() {
    {
        TestPort p; Delivery d(p,false); ControlTestPort control;
        provider_control::Worker worker(control,id(1),provider_session::Required,{100,200,50});
        TestBackend backend(worker); Owner owner(d,worker,&backend);
        Frame f=request(); f.ticket.grant=initialize(worker,control);
        uint8_t bytes[FrameSize]; encode(f,bytes); expect(d.receive_isr(bytes,sizeof(bytes)),"first pipelined operation admitted");
        f.opcode=Read; f.request=2; f.port=f.length=0; f.count=3; encode(f,bytes);
        bool injected=false,accepted=false;
        p.unlock_hook=[&] {
            if (!injected && p.sent.size()==1) {
                injected=true;
                p.lock(); accepted=d.receive_isr(bytes,sizeof(bytes)); p.unlock();
            }
        };
        owner.step(0); p.unlock_hook=[]{};
        expect(injected && accepted,"next request admitted immediately after reply publication");
        owner.step(0);
        expect(backend.calls==2 && p.sent.size()==2,"completion cannot erase next request");
        Frame reply={};
        expect(p.sent.size()==2 && decode(p.sent.back().data(),p.sent.back().size(),reply) && reply.status==Ok && reply.request==2,"immediate next request receives valid reply");
        worker.disconnect(); worker.step(0); expect(!worker.occupied(),"pipelined owner cleanup");
    }
    for (unsigned mode = 0; mode < 8; ++mode) {
        TestPort p; Delivery d(p,false); ControlTestPort control;
        provider_control::Worker worker(control,id(1),provider_session::Required,{100,200,50});
        TestBackend backend(worker); Owner owner(d,worker,mode == 0 ? NULL : &backend);
        auto grant = initialize(worker,control); Frame f = request(); f.ticket.grant = grant;
        uint8_t bytes[FrameSize]; encode(f,bytes); expect(d.receive_isr(bytes,sizeof(bytes),0),"owner copy");
        if (mode == 1) backend.now = 100;
        if (mode == 2) backend.now = 25;
        if (mode == 3) backend.hook = [&] { worker.disconnect(); };
        if (mode == 4) backend.hook = [&] { d.disconnect(); worker.disconnect(); };
        unsigned locks = 0;
        if (mode == 7) p.lock_hook = [&] { if (++locks == 2) worker.disconnect(); };
        owner.step(backend.now);
        p.lock_hook = []{};
        if (mode == 0 || mode == 1) expect(!backend.calls,"unavailable or expired never executes");
        else expect(backend.calls == 1,"backend executes once");
        if (mode == 2) expect(backend.budget == 75,"queue delay deducted");
        if (mode == 4 || mode == 7) expect(p.sent.empty(),"generation loss suppresses reply");
        else {
            expect(p.sent.size() == 1,"one owner reply"); Frame reply;
            expect(decode(p.sent[0].data(),p.sent[0].size(),reply),"canonical owner reply");
            expect(reply.status == (mode == 0 ? Unavailable : mode == 1 || mode == 3 ? Failed : Ok),"owner status");
        }
        if (mode == 5 || mode == 6) {
            f.opcode = Read; f.port = f.length = 0; f.count = 3; f.request = mode == 5 ? 1 : 2;
            if (mode == 6) f.ticket.grant.owner = id(9);
            encode(f,bytes); d.receive_isr(bytes,sizeof(bytes),0); owner.step(0);
            expect(backend.calls == 1,"replay and wrong owner rejected before backend");
        }
        worker.disconnect(); worker.step(backend.now);
        expect(!worker.occupied(),"owner cleanup");
    }
}
struct ControlWire : provider_control::ManagerPort {
    std::mutex mutex;
    std::vector<std::vector<uint8_t>> sent;
    void lock() override { mutex.lock(); }
    void unlock() override { mutex.unlock(); }
    bool send(const uint8_t *p,size_t n) override { sent.emplace_back(p,p+n); return true; }
};
static void client() {
    for (unsigned mode = 0; mode < 10; ++mode) {
        ControlWire wire; TestPort port; Delivery data(port,true);
        provider_control::ManagerDelivery manager(wire,{0,[](void*,Identity& output) { output=id(2); return true; }},{20,200});
        provider_control::ManagerRunner runner(manager,100); Client client(port,data,manager,runner);
        CancellationSource boot; client.bind_lifetime(boot.token());
        ControlTestPort remote; provider_control::Worker worker(remote,id(1),provider_session::Required,{100,200,50});
        auto pump = [&](unsigned now) {
            while (!wire.sent.empty()) {
                auto b = wire.sent.front(); wire.sent.erase(wire.sent.begin()); worker.submit(b.data(),b.size()); worker.step(now);
                while (!remote.replies.empty()) {
                    uint8_t bytes[provider_control::FrameSize]; provider_control::encode(remote.replies.front(),bytes); remote.replies.pop_front();
                    manager.receive_isr(bytes,sizeof(bytes));
                }
            }
            runner.step(now);
        };
        expect(runner.start(id(1),{},0),"client negotiation starts"); pump(0); pump(0);
        CancellationSource cancellation; Frame f = request();
        expect(client.submit(f,cancellation.token(),0),"client queued");
        expect(!client.submit(f,{},0),"client saturation refuses second caller");
        memset(f.payload,0x5a,sizeof(f.payload));
        if (mode == 0) cancellation.signal();
        if (mode == 1) client.stop();
        unsigned locks = 0;
        if (mode == 9) port.lock_hook = [&] { if (++locks == 2) cancellation.signal(); };
        client.step(mode == 2 ? 100 : 10,mode == 2 ? 100 : 10);
        port.lock_hook = []{};
        if (mode <= 1) {
            expect(port.sent.empty() && client.collect(f) && f.status == Failed,"cancel or stop before transmit");
            if (mode == 1) expect(!client.submit(request(),{},20),"stopped queue cannot strand new work");
        } else if (mode == 2 || mode == 9) {
            expect(port.sent.empty(),"queued timeout cannot open TLS");
            client.stop(); expect(client.collect(f) && f.status == Failed,"expired queue fails");
        } else {
            Frame sent; expect(port.sent.size() == 1 && decode(port.sent[0].data(),port.sent[0].size(),sent),"one encoded queued request");
            expect(sent.payload[0] == 'h' && sent.budget_ms == 90 && sent.request == 1,"copied data and queue budget");
            Frame reply = sent; reply.reply = true; reply.status = Ok; reply.budget_ms = reply.port = reply.length = reply.count = 0;
            if (mode == 3) ++reply.request;
            if (mode == 4) reply.ticket.grant.owner = id(8);
            if (mode == 5) reply.opcode = Write;
            uint8_t bytes[FrameSize]; encode(reply,bytes); expect(data.receive_isr(bytes,sizeof(bytes)),"client reply copy");
            if (mode == 6) data.disconnect();
            client.step(11,11);
            if (mode == 8) boot.signal(); // Complete but not yet collected.
            expect(client.collect(f) && f.status == (mode == 7 ? Ok : Failed),"stale metadata and loss rejected");
        }
        client.stop(); worker.disconnect(); worker.step(110);
    }
}
int main() { codec(); mailbox(); owner(); client(); printf("Provider data: %u checks, %u failures\n",checks,failures); return failures ? 1 : 0; }
