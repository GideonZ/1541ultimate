// Local integration transport: copied, bounded requests between serialized
// manager/provider owners. This is not the firmware UART data protocol.
#include "provider_manager_runner.h"
#include <deque>
#include <array>
namespace connected_test {
using namespace provider_control;
using namespace provider_session;
enum { Chunk = 256 };
struct ManagerWire : ManagerPort {
    std::mutex mutex;
    std::deque<std::array<uint8_t,FrameSize>> requests;
    void lock() override { mutex.lock(); }
    void unlock() override { mutex.unlock(); }
    bool send(const uint8_t *bytes, size_t size) override {
        if (size != FrameSize || requests.size() == QueueDepth) return false;
        std::array<uint8_t,FrameSize> frame; memcpy(frame.data(),bytes,size); requests.push_back(frame); return true;
    }
};
struct Remote;
struct Resource final : provider_session::Resource {
    Remote& remote;
    TlsHttpConnection tls;
    Resource(Remote&, const tls_stream_port&, uint32_t);
    void cancel() override;
    bool cleanup() override;
    ~Resource();
};
struct Remote {
    Platform& platform;
    ControlTestPort wire;
    Worker worker;
    Resource *resource = NULL;
    Ticket ticket = {};
    tls_stream_diagnostics last = {};
    unsigned releases = 0, cancels = 0, opens = 0, reads = 0, writes = 0;
    uint64_t sequence = 0;
    const uint32_t lease;
    Remote(Platform& p, Identity boot, uint32_t lease_ms) : platform(p), worker(wire,boot,Required,{1000,lease_ms,250}),lease(lease_ms) {}
    bool admit(const Ticket& requested, uint32_t timeout) {
        if (resource) return false;
        tls_stream_port callbacks = {&platform,clock_read,clock_ready,cancelled,entropy,trust,open_socket,read_socket,write_socket,close_socket};
        Resource *candidate = new Resource(*this,callbacks,timeout);
        if (!worker.admit(requested.grant,requested.session,candidate,now_ms(),ticket)) { delete candidate; return false; }
        resource = candidate; sequence = 0; return true;
    }
    // Each request carries its session identity and a strictly increasing number.
    // Only this serialized owner may touch the TLS object or run worker.step().
    int perform(const Ticket& request, uint64_t number, unsigned op, uint8_t data[Chunk], int length, uint16_t port) {
        if (!resource || !worker.current(request,now_ms()) || number != sequence + 1 || !number ||
            length < 0 || length > Chunk) return -1;
        sequence = number;
        int result = -1;
        if (op == 0 && length && data[length-1] == 0) { ++opens; result = resource->tls.open(reinterpret_cast<char*>(data),port); }
        if (op == 1) { ++reads; result = resource->tls.read(data,length); }
        if (op == 2) { ++writes; result = resource->tls.write(data,length); }
        last = resource->tls.diagnostic();
        // TLS calls have unwound before owner cleanup may release the resource.
        worker.step(now_ms());
        return result;
    }
};
Resource::Resource(Remote& r, const tls_stream_port& callbacks, uint32_t timeout) : remote(r), tls(callbacks,timeout) {}
void Resource::cancel() { ++remote.cancels; }
bool Resource::cleanup() { remote.last = tls.diagnostic(); tls.close_session(); return remote.platform.fd == -1; }
Resource::~Resource() { ++remote.releases; if (remote.resource == this) remote.resource = NULL; }

struct Provider : HttpSecureProvider {
    Platform& platform;
    Remote& remote;
    ManagerWire wire;
    ManagerDelivery delivery;
    ManagerRunner runner;
    Identity boot;
    CancellationSource lifetime_events;
    OperationCancellation command;
    Ticket ticket = {};
    uint32_t timeout;
    const char *mode;
    unsigned created = 0, destroyed = 0, data_calls = 0;
    uint64_t request = 0;
    int64_t interrupt_at = 0;
    bool interrupted = false;
    Provider(Platform& p, Remote& r, Identity id, const char *m, uint32_t budget) :
        platform(p),remote(r),delivery(wire,{&p,[](void *context, Identity& value) {
            return entropy(context,value.bytes,sizeof(value.bytes)) == 0;
        }},{100,r.lease}),runner(delivery,r.lease/3),boot(id),timeout(budget),mode(m) {}
    void ingress() {
        while (!wire.requests.empty()) {
            auto bytes = wire.requests.front(); wire.requests.pop_front();
            expect(remote.worker.submit(bytes.data(),bytes.size()),"connected control request accepted");
        }
    }
    void replies() {
        while (!remote.wire.replies.empty()) {
            Frame frame = remote.wire.replies.front(); remote.wire.replies.pop_front();
            uint8_t bytes[FrameSize]; encode(frame,bytes);
            wire.lock(); bool accepted = delivery.receive_isr(bytes,sizeof(bytes)); wire.unlock();
            expect(accepted,"connected control reply copied");
        }
    }
    void pump() {
        ingress(); remote.worker.step(now_ms()); replies(); runner.step(now_ms());
    }
    void initialize() {
        expect(runner.start(boot,lifetime_events.token(),now_ms()),"connected explicit startup");
        pump(); pump();
        expect(runner.state() == ManagerRunner::Active,"connected grant initialized");
    }
    bool available() const override { return runner.state() == ManagerRunner::Active && !lifetime_events.token().cancelled(); }
    OperationCancellation lifetime() const override { return lifetime_events.token(); }
    void interrupt(int64_t now) {
        if (interrupted || !interrupt_at || now < interrupt_at) return;
        interrupted = true;
        if (!strncmp(mode,"provider-",9)) {
            lifetime_events.signal(); remote.worker.disconnect();
        } else if (!strncmp(mode,"manager-",8)) {
            // Model manager loss: no remote close is invented. The provider
            // resource is retired by lease, polled while its TLS wait is blocked.
            runner.stop(); lifetime_events.signal();
        }
    }
    bool remote_cancelled(int64_t now) {
        interrupt(now);
        if (command.cancelled() && !interrupted) {
            interrupted = true;
            expect(runner.close(ticket,now),"connected command cancellation sends close");
            ingress(); // Ingress signals only; never free a live TLS call here.
        }
        return !remote.worker.current(ticket,now);
    }
    class Proxy : public HttpConnection {
        Provider& owner;
        OperationCancellation cancellation, generation;
        bool attempted = false, closed = false;
        int transact(unsigned op, void *buffer, int length, uint16_t port = 0) {
            if (closed || cancellation.cancelled() || generation.cancelled() || !owner.runner.current(owner.ticket,now_ms())) return -1;
            uint8_t bytes[Chunk] = {};
            if (length < 0) return -1;
            int count = length > Chunk ? Chunk : length;
            if (op != 1 && count) memcpy(bytes,buffer,count);
            ++owner.data_calls;
            int result = owner.remote.perform(owner.ticket,++owner.request,op,bytes,count,port);
            owner.replies(); owner.runner.step(now_ms());
            // Fence even an otherwise valid reply after cancellation/restart.
            if (cancellation.cancelled() || generation.cancelled() || !owner.runner.current(owner.ticket,now_ms())) return -1;
            if (op != 0 && result > count) return -1;
            if (op == 1 && result > 0) memcpy(buffer,bytes,result);
            return result;
        }
    public:
        Proxy(Provider& p, OperationCancellation token) : owner(p),cancellation(token),generation(p.lifetime()) {}
        ~Proxy() { close_session(); ++owner.destroyed; }
        int open(const char *host, uint16_t port) override {
            if (attempted || closed || !host || strlen(host) >= Chunk) return -1;
            attempted = true;
            return transact(0,const_cast<char*>(host),int(strlen(host)+1),port);
        }
        int read(void *buffer,int length) override { return attempted ? transact(1,buffer,length) : -1; }
        int write(const void *buffer,int length) override { return attempted ? transact(2,const_cast<void*>(buffer),length) : -1; }
        void close_session() override {
            if (closed) return;
            closed = true;
            owner.pump(); // Finish a renewal reply before sending terminal close.
            if (owner.runner.current(owner.ticket,now_ms())) owner.runner.close(owner.ticket,now_ms());
            owner.pump();
            expect(!owner.remote.resource && !owner.remote.worker.occupied(),"connected owner cleanup completed");
            if (!generation.cancelled()) {
                Manager::Status status = owner.delivery.status(now_ms());
                expect(status.state == Manager::Ready && !status.pending,"connected close acknowledgement validated before reuse");
            }
        }
    };
    HttpConnection *create_connection(const OperationCancellation& token) override {
        command = token;
        if (!runner.reserve(now_ms(),ticket)) return NULL;
        if (!remote.admit(ticket,timeout)) { runner.stop(); return NULL; }
        uint8_t invalid[Chunk] = {};
        Ticket stale = ticket; stale.grant.owner.bytes[0] ^= 0x80;
        expect(remote.perform(stale,1,1,invalid,1,0) < 0 && remote.perform(ticket,0,1,invalid,1,0) < 0,
               "connected stale session and replay rejected before TLS access");
        expect(remote.opens == 0 && remote.reads == 0 && remote.writes == 0,
               "connected rejected requests never enter TLS methods");
        ++created; return new Proxy(*this,token);
    }
};
static bool cancellation(Platform *platform,int64_t now) {
    return static_cast<Provider*>(platform->worker_context)->remote_cancelled(now);
}
static void exchange(const char *mode,const char *ca,int port) {
    int before_allocations = live_allocations, before_descriptors = descriptors();
    {
        Platform platform(ca); Identity boot = {}; boot.bytes[0] = !strcmp(mode,"recovery") ? 2 : 1;
        bool timeout = strstr(mode,"timeout") != NULL;
        bool cancel = strstr(mode,"cancel") != NULL;
        bool manager_loss = !strncmp(mode,"manager-",8);
        Remote remote(platform,boot,manager_loss ? 350 : 1000);
        bool success = !strcmp(mode,"valid") || !strcmp(mode,"recovery");
        Provider provider(platform,remote,boot,mode,timeout ? 650 : 3000);
        platform.worker_context = &provider; platform.worker_cancelled = cancellation;
        provider.initialize();
        if (!strcmp(mode,"total-timeout")) platform.open_delay = 250;
        CancellationSource commands;
        auto token = commands.token();
        std::thread canceller;
        if (!strncmp(mode,"command-",8)) canceller = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(150)); commands.signal();
        });
        if (cancel && strncmp(mode,"command-",8)) provider.interrupt_at = now_ms()+150;
        int64_t begin = now_ms();
        {
            HttpTarget target(7,&provider); target.begin_command(token);
            char url[128]; snprintf(url,sizeof(url),"https://%s:%d/probe",!strcmp(mode,"hostname") ? "wrong.lab.test" : "tls.lab.test",port);
            std::vector<uint8_t> bytes = {6,HTTP_CMD_HEADER_CREATE,1}; bytes.insert(bytes.end(),url,url+strlen(url));
            Message input = {int(bytes.size()),true,bytes.data()},*data,*status;
            target.parse_command(&input,&data,&status);
            expect(data->length == 1,"connected C64 header created");
            uint8_t handle = data->length ? data->message[0] : 0;
            const char *header = "Connection: close";
            bytes = {6,HTTP_CMD_HEADER_ADD,handle}; bytes.insert(bytes.end(),header,header+strlen(header));
            input = {int(bytes.size()),true,bytes.data()}; target.parse_command(&input,&data,&status);
            uint8_t request[] = {6,HTTP_CMD_DO_EXCHANGE_RAW,handle,255};
            input = {sizeof(request),true,request}; target.parse_command(&input,&data,&status); target.prepare_reply(&data,&status);
            if (success) {
                expect(data->last_part && data->length == 8 && !memcmp(data->message,"tls-ok!\n",8),"connected exact HTTPS response body");
                expect(status->length >= 15 && !memcmp(status->message,"HTTP/1.1 200 OK",15),"connected remote HTTP status");
            } else {
                expect(data->last_part && data->length == 0,"connected failure exposes no body");
                expect(status->length == 23 && !memcmp(status->message,"503 SERVICE UNAVAILABLE",23),"connected local failure status");
            }
            target.finish_reply(true); target.get_more_data(&data,&status); target.prepare_reply(&data,&status);
            expect(data->last_part && data->length == 0,"connected no stale reply bytes"); target.finish_reply(true);
        }
        if (canceller.joinable()) canceller.join();
        int64_t elapsed = now_ms()-begin;
        expect(provider.created == 1 && provider.destroyed == 1,"connected proxy ownership");
        expect(remote.releases == 1 && remote.cancels == 1 && !remote.resource,"connected TLS resource released once");
        expect(platform.opens == 1 && platform.closes == 1 && platform.fd == -1,"connected socket balance");
        expect(provider.data_calls && remote.opens == 1,"connected copied data path used");
        if (success) expect(remote.last.status == TLS_OK && remote.writes && remote.reads,"connected authenticated stream");
        if (timeout) expect(remote.last.status == TLS_TIMEOUT && elapsed >= 630 && elapsed <= 900,"connected single original deadline");
        if (cancel) expect(remote.last.status == TLS_CANCELLED && elapsed <= 750,"connected bounded cancellation or lost-owner lease");
        if (!success && !timeout && !cancel) expect(remote.last.verify_flags != 0 && remote.writes == 0,"connected authentication failure sends no HTTP");
        printf("CONNECTED %s status=%d flags=%u elapsed_ms=%d opens=%d closes=%d releases=%u calls=%u\n",mode,
            remote.last.status,remote.last.verify_flags,int(elapsed),platform.opens,platform.closes,remote.releases,provider.data_calls);
        platform.worker_cancelled = NULL;
    }
    expect(live_allocations == before_allocations,"connected scoped allocation balance");
    expect(descriptors() == before_descriptors,"connected descriptor balance");
}
}
