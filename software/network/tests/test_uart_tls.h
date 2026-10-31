#include "provider_data_client.h"
namespace uart_test {
using namespace connected_test;
struct Wire : provider_data::Port {
    std::mutex mutex;
    std::deque<std::vector<uint8_t>> packets;
    void lock() override { mutex.lock(); }
    void unlock() override { mutex.unlock(); }
    bool send(const uint8_t *p,size_t n) override {
        if (n != provider_data::FrameSize || !packets.empty()) return false;
        packets.emplace_back(p,p+n); return true;
    }
    void deliver(provider_data::Delivery& destination) {
        while (!packets.empty()) {
            auto bytes = packets.front(); packets.pop_front();
            expect(destination.receive_isr(bytes.data(),bytes.size(),uint32_t(now_ms())),"UART data packet copied");
            memset(bytes.data(),0xa5,bytes.size()); // Receiver cannot retain DMA storage.
        }
    }
};
struct Backend : provider_data::Backend {
    Remote& remote;
    explicit Backend(Remote& r) : remote(r) {}
    uint64_t now_ms() override { return ::now_ms(); }
    int execute(provider_data::Frame& f) override {
        if (f.opcode == provider_data::Open && !remote.admit(f.ticket,f.budget_ms)) return -1;
        return remote.perform(f.ticket,f.request,f.opcode-1,f.payload,
            f.opcode == provider_data::Open ? f.length : f.count,f.port);
    }
};
struct Provider : HttpSecureProvider {
    Platform& platform;
    Remote remote;
    Backend backend;
    ManagerWire control_wire;
    ManagerDelivery control;
    ManagerRunner runner;
    Wire outbound,inbound;
    provider_data::Delivery replies,requests;
    provider_data::Client client;
    provider_data::Owner owner;
    CancellationSource lifetime_events;
    OperationCancellation command;
    const char *mode;
    uint32_t timeout;
    int64_t interrupt_at = 0;
    bool interrupted = false;
    unsigned created = 0,destroyed = 0;
    Provider(Platform& p, Identity boot,const char *m,uint32_t budget) : platform(p),
        remote(p,boot,!strncmp(m,"manager-",8) ? 350 : 1000),backend(remote),
        control(control_wire,{&p,[](void *ctx,Identity& id) { return entropy(ctx,id.bytes,sizeof(id.bytes)) == 0; }},{100,remote.lease}),
        runner(control,remote.lease/3),replies(outbound,true),requests(inbound,false),
        client(outbound,replies,control,runner),owner(requests,remote.worker,&backend),mode(m),timeout(budget) {
        client.bind_lifetime(lifetime_events.token());
        expect(runner.start(boot,lifetime_events.token(),now_ms()),"UART manager starts");
        controls(); controls(); expect(runner.state() == ManagerRunner::Active,"UART grant initialized");
    }
    void ingress() {
        while (!control_wire.requests.empty()) {
            auto b = control_wire.requests.front(); control_wire.requests.pop_front();
            expect(remote.worker.submit(b.data(),b.size()),"UART control ingress");
        }
    }
    void controls() {
        ingress(); remote.worker.step(now_ms());
        while (!remote.wire.replies.empty()) {
            Frame f = remote.wire.replies.front(); remote.wire.replies.pop_front(); uint8_t b[FrameSize]; encode(f,b);
            control_wire.lock(); expect(control.receive_isr(b,sizeof(b)),"UART control reply"); control_wire.unlock();
        }
        runner.step(now_ms());
    }
    void service() {
        controls(); client.step(now_ms(),uint32_t(now_ms()));
        outbound.deliver(requests); owner.step(now_ms());
        inbound.deliver(replies); controls(); client.step(now_ms(),uint32_t(now_ms()));
    }
    bool cancelled_at(int64_t now) {
        if (!interrupted && interrupt_at && now >= interrupt_at) {
            interrupted = true; lifetime_events.signal();
            if (!strncmp(mode,"provider-",9)) { remote.worker.disconnect(); requests.disconnect(); replies.disconnect(); }
            else runner.stop();
        }
        // Owner queue cancellation produces a control close, independently of
        // the occupied provider data slot. No resource cleanup in this callback.
        if (command.cancelled()) { client.step(now,uint32_t(now)); ingress(); }
        return !remote.worker.current(remote.ticket,now);
    }
    bool available() const override { return runner.state() == ManagerRunner::Active; }
    OperationCancellation lifetime() const override { return lifetime_events.token(); }
    class Proxy : public HttpConnection {
        Provider& p;
        OperationCancellation token;
        bool attempted = false,closed = false;
        int transact(unsigned op,void *buffer,int count,uint16_t port=0) {
            if (count < 0 || count > provider_data::PayloadSize) return -1;
            provider_data::Frame f = {}; f.opcode = op; f.budget_ms = p.timeout; f.port = port;
            if (op == provider_data::Open) f.length = count;
            else f.count = count;
            if (op == provider_data::Write) f.length = count;
            if (op != provider_data::Read && count) memcpy(f.payload,buffer,count);
            if (!p.client.submit(f,op == provider_data::Client::Close ? OperationCancellation() : token,uint32_t(now_ms()))) return -1;
            int64_t limit = now_ms()+4000;
            do {
                p.service();
                if (p.client.collect(f)) {
                    if (f.status != provider_data::Ok) return -1;
                    if (op == provider_data::Read && f.length) memcpy(buffer,f.payload,f.length);
                    return f.count;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (now_ms() < limit);
            expect(false,"UART request terminates"); p.client.stop(); return -1;
        }
    public:
        Proxy(Provider& owner,OperationCancellation cancel) : p(owner),token(cancel) {}
        ~Proxy() { close_session(); ++p.destroyed; }
        int open(const char *host,uint16_t port) override {
            if (closed || attempted || !host || strlen(host) >= provider_data::PayloadSize) return -1;
            attempted = true; return transact(provider_data::Open,const_cast<char*>(host),strlen(host)+1,port);
        }
        int read(void *buffer,int count) override {
            return !closed && attempted ? transact(provider_data::Read,buffer,count > 256 ? 256 : count) : -1;
        }
        int write(const void *buffer,int count) override {
            return !closed && attempted ? transact(provider_data::Write,const_cast<void*>(buffer),count > 256 ? 256 : count) : -1;
        }
        void close_session() override { if (!closed) { closed = true; transact(provider_data::Client::Close,0,0); } }
    };
    HttpConnection *create_connection(const OperationCancellation& token) override { command = token; ++created; return new Proxy(*this,token); }
};
static bool cancelled(Platform *p,int64_t now) { return static_cast<Provider*>(p->worker_context)->cancelled_at(now); }
static void exchange(const char *mode,const char *ca,int port) {
    int allocations = live_allocations, fds = descriptors();
    {
        Platform platform(ca); Identity boot = {}; boot.bytes[0] = !strcmp(mode,"recovery") ? 2 : 1;
        bool timeout = strstr(mode,"timeout"), cancel = strstr(mode,"cancel");
        bool good = !strcmp(mode,"valid") || !strcmp(mode,"recovery");
        Provider provider(platform,boot,mode,timeout ? 650 : 3000);
        platform.worker_context = &provider; platform.worker_cancelled = cancelled;
        if (!strcmp(mode,"total-timeout")) platform.open_delay = 250;
        CancellationSource commands; std::thread canceller;
        if (!strncmp(mode,"command-",8)) canceller = std::thread([&] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); commands.signal(); });
        else if (cancel) provider.interrupt_at = now_ms()+150;
        int64_t begin = now_ms();
        {
            HttpTarget target(7,&provider); target.begin_command(commands.token());
            char url[128]; snprintf(url,sizeof(url),"https://%s:%d/probe",!strcmp(mode,"hostname") ? "wrong.lab.test" : "tls.lab.test",port);
            std::vector<uint8_t> b = {6,HTTP_CMD_HEADER_CREATE,1}; b.insert(b.end(),url,url+strlen(url));
            Message input = {int(b.size()),true,b.data()},*data,*status; target.parse_command(&input,&data,&status);
            expect(data->length == 1,"UART header handle"); uint8_t handle = data->length ? data->message[0] : 0;
            const char *header = "Connection: close";
            b = {6,HTTP_CMD_HEADER_ADD,handle}; b.insert(b.end(),header,header+strlen(header));
            input = {int(b.size()),true,b.data()}; target.parse_command(&input,&data,&status);
            uint8_t command[] = {6,HTTP_CMD_DO_EXCHANGE_RAW,handle,255};
            input = {sizeof(command),true,command}; target.parse_command(&input,&data,&status); target.prepare_reply(&data,&status);
            expect(data->last_part && data->length == (good ? 8 : 0),"UART complete or empty failure body");
            if (good) expect(!memcmp(data->message,"tls-ok!\n",8),"UART exact response");
            else expect(status->length == 23 && !memcmp(status->message,"503 SERVICE UNAVAILABLE",23),"UART safe failure");
            target.finish_reply(true);
        }
        if (canceller.joinable()) canceller.join();
        auto& remote = provider.remote; auto d = remote.last; int elapsed = int(now_ms()-begin);
        expect(provider.created == 1 && provider.destroyed == 1,"UART proxy balanced");
        expect(remote.releases == 1 && remote.cancels == 1 && !remote.resource,"UART resource released once");
        expect(platform.opens == 1 && platform.closes == 1 && platform.fd == -1,"UART socket balanced");
        if (timeout) expect(d.status == TLS_TIMEOUT && elapsed >= 630 && elapsed <= 900,"UART total timeout");
        if (cancel) expect(d.status == TLS_CANCELLED && elapsed <= 750,"UART cancellation bounded");
        if (!good && !timeout && !cancel) expect(d.verify_flags && !remote.writes,"UART auth sends no HTTP");
        printf("UART %s status=%d flags=%u elapsed_ms=%d opens=%d closes=%d releases=%u\n",mode,d.status,d.verify_flags,elapsed,platform.opens,platform.closes,remote.releases);
        platform.worker_cancelled = NULL;
    }
    expect(live_allocations == allocations && descriptors() == fds,"UART scoped resources balanced");
}
}
